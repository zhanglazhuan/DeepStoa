"""Check production EPD SPI streams against a RAM-window controller model."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
driver = (ROOT / 'drivers/gdem0397t81p/Display_EPD_W21.c').read_text(encoding='utf-8')


def function(signature):
    start = driver.index(signature)
    brace = driver.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (driver[end] == '{') - (driver[end] == '}')
        end += 1
    return driver[start:end]


harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define EPD_WIDTH 480
#define EPD_HEIGHT 800
#define EPD_ARRAY 48000
#define EPD_TX_CHUNK 4096
#define EPD_SPI_DMA_FROM_PSRAM 0
#define EPD_PIN_CS 12
#define EPD_AW_PIN_DC 13
#define SPI_TRANS_USE_TXDATA 1
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define taskYIELD() ((void)0)
typedef int esp_err_t;
typedef struct {
    unsigned length, flags;
    uint8_t tx_data[4];
    const uint8_t *tx_buffer;
} spi_transaction_t;
static uint8_t dma_buffer[EPD_TX_CHUNK], frame[129 * EPD_WIDTH];
static uint8_t ram[2][EPD_ARRAY], expected[EPD_ARRAY];
static uint8_t *s_tx_bounce;
static int spi_dev, s_dc_level, dc, cs;
static unsigned command, params[4], param_count;
static unsigned xs, xe, ys, ye, px, py;
static unsigned transactions, plane_bytes[2], updates, waits, update_mode;
static unsigned cs_epoch, payload_epoch;
static bool payload_started;
static bool esp_ptr_dma_capable(const void *p) { return p == dma_buffer; }
static void epd_read_busy(void) { waits++; }
static void aw9523_set_pin(unsigned pin, int value) {
    assert(pin == EPD_AW_PIN_DC); dc = value;
}
static void gpio_set_level(unsigned pin, int value) {
    assert(pin == EPD_PIN_CS);
    if (cs && !value) cs_epoch++;
    cs = value;
}
static void accept_byte(uint8_t value) {
    if (!dc) {
        command = value; param_count = 0; payload_started = false;
        if (command == 0x20) updates++;
        return;
    }
    if (command == 0x24 || command == 0x26) {
        if (!payload_started) { payload_epoch = cs_epoch; payload_started = true; }
        assert(payload_epoch == cs_epoch); /* No CS gap inside a RAM stream. */
        assert(px >= xs && px <= xe && py >= ys && py <= ye);
        unsigned plane = command == 0x26;
        ram[plane][py * 100 + px / 8] = value;
        plane_bytes[plane]++;
        px += 8;
        if (px > xe) { px = xs; if (++py > ye) py = ys; }
    } else if (command == 0x44 || command == 0x45) {
        assert(param_count < 4); params[param_count++] = value;
        if (param_count == 4) {
            unsigned first = params[0] | (params[1] << 8);
            unsigned last = params[2] | (params[3] << 8);
            if (command == 0x44) { xs = first; xe = last; }
            else { ys = first; ye = last; }
        }
    } else if (command == 0x4e || command == 0x4f) {
        assert(param_count < 2); params[param_count++] = value;
        if (param_count == 2) {
            unsigned pos = params[0] | (params[1] << 8);
            if (command == 0x4e) px = pos; else py = pos;
        }
    } else {
        assert(command == 0x22 && (value == 0xf7 || value == 0xff));
        update_mode = value;
    }
}
static esp_err_t spi_device_transmit(int device, spi_transaction_t *t) {
    (void)device; assert(!cs);
    assert(t->length && !(t->length % 8) && t->length <= EPD_TX_CHUNK * 8);
    transactions++;
    const uint8_t *data = t->flags & SPI_TRANS_USE_TXDATA ? t->tx_data : t->tx_buffer;
    for (unsigned i = 0; i < t->length / 8; i++) accept_byte(data[i]);
    return ESP_OK;
}
static void reset_model(bool dma) {
    memset(ram, 0xa5, sizeof(ram));
    xs = ys = px = py = 0; xe = 799; ye = 479;
    command = param_count = transactions = updates = waits = update_mode = 0;
    cs_epoch = payload_epoch = 0; payload_started = false;
    plane_bytes[0] = plane_bytes[1] = 0;
    cs = 1; dc = s_dc_level = 0; s_tx_bounce = dma ? dma_buffer : NULL;
}
'''
harness += '\n'.join(function(signature) for signature in [
    'static void spi_write_byte(', 'static void spi_write_bytes(',
    'static void epd_set_dc(', 'static void epd_write_cmd(',
    'static void epd_write_data(', 'static void epd_write_data_fill(',
    'static void EPD_Update(', 'static void EPD_Part_Update_Async(',
    'void EPD_WhiteScreen_White(', 'void EPD_Dis_Part_Window_Async(',
])
harness += r'''
static void check_clear(bool dma) {
    reset_model(dma);
    EPD_WhiteScreen_White();
    for (unsigned p = 0; p < 2; p++) {
        assert(plane_bytes[p] == EPD_ARRAY);
        for (unsigned i = 0; i < EPD_ARRAY; i++) assert(ram[p][i] == 0xff);
    }
    assert(updates == 1 && waits == 1 && cs == 1 && update_mode == 0xf7);
    assert(transactions <= (dma ? 32u : 800u));
}
static void check_window(bool dma, unsigned x0, unsigned y0,
                         unsigned x1, unsigned y1, unsigned stride) {
    reset_model(dma); memset(expected, 0xa5, sizeof(expected));
    unsigned left_byte = (x0 & ~7u) / 8, right_byte = (x1 | 7u) / 8;
    for (unsigned y = y0; y <= y1; y++) {
        for (unsigned x = left_byte; x <= right_byte; x++)
            expected[y * 100 + x] = frame[y * stride + x];
    }
    EPD_Dis_Part_Window_Async(x0, y0, x1, y1, frame, stride);
    assert(!memcmp(ram[0], expected, sizeof(expected)));
    for (unsigned i = 0; i < EPD_ARRAY; i++) assert(ram[1][i] == 0xa5);
    assert(plane_bytes[0] == (right_byte - left_byte + 1) * (y1 - y0 + 1));
    assert(plane_bytes[1] == 0 && updates == 1 && waits == 0 && cs == 1 && update_mode == 0xff);
    assert(transactions <= (dma ? 40u : 500u));
}
int main(void) {
    for (unsigned i = 0; i < sizeof(frame); i++) frame[i] = (uint8_t)(i * 37 + i / 113);
    for (unsigned dma = 0; dma < 2; dma++) {
        check_clear(dma);
        check_window(dma, 0, 0, 799, 479, 100);
        check_window(dma, 13, 7, 189, 237, 129);
        check_window(dma, 799, 479, 799, 479, 100);
        check_window(dma, 1, 0, 1, 479, 100);
        reset_model(dma);
        EPD_Dis_Part_Window_Async(0, 0, 800, 479, frame, 100);
        EPD_Dis_Part_Window_Async(16, 0, 23, 0, frame, 2);
        assert(transactions == 0);
    }
    puts("PASS: clear planes, strided windows, alignment, CS continuity, DMA fallback and transfer budgets");
}
'''

with tempfile.TemporaryDirectory(prefix='deepstoa-epd-transfer-') as temp:
    source = pathlib.Path(temp) / 'test.c'
    exe = pathlib.Path(temp) / 'test.exe'
    source.write_text(harness, encoding='utf-8')
    subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
