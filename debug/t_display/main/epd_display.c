// debug/t_display/main/epd_display.c
// GDEM0397T81P e-ink display driver (480x800)
//
// Display init sequences and command logic preserved VERBATIM from
// drivers/gdem0397t81p/Display_EPD_W21.c (originally from Zephyr RTOS port).
//
// Low-level transport: ESP-IDF hardware SPI (mode 0, MSB first) + direct GPIO.
//
// Board selection (compile definition, set in the project's main/CMakeLists.txt):
//   default                            -> boards/esp32s3/esp32s3_devkit.h
//                                         GPIO 11=MOSI, 12=SCK, 15=CS, 6=DC, 7=RST, 8=BUSY
//   -DEPD_BOARD_HELTEC_WIRELESS_STICK_V3 -> boards/heltec_wireless_stick_v3/heltec_wireless_stick_v3.h
//                                         GPIO 35=MOSI, 36=SCK, 34=CS, 6=DC, 7=RST, 3=BUSY
// SPI clock can be overridden with -DEPD_SPI_CLOCK_HZ=<hz> (default 10 MHz).

#include <string.h>
#include "epd_display.h"

#if defined(EPD_BOARD_HELTEC_WIRELESS_STICK_V3)
#include "heltec_wireless_stick_v3.h"
#define EPD_PIN_MOSI    HELTEC_STICK_PIN_EPD_MOSI
#define EPD_PIN_SCK     HELTEC_STICK_PIN_EPD_SCK
#define EPD_PIN_CS      HELTEC_STICK_PIN_EPD_CS
#define EPD_PIN_DC      HELTEC_STICK_PIN_EPD_DC
#define EPD_PIN_RST     HELTEC_STICK_PIN_EPD_RST
#define EPD_PIN_BUSY    HELTEC_STICK_PIN_EPD_BUSY
#define EPD_SPI_HOST    HELTEC_STICK_EPD_SPI_HOST
#define EPD_BOARD_NAME  "heltec_wireless_stick_v3"
#else
#include "esp32s3_devkit.h"
#define EPD_PIN_MOSI    DEVKIT_PIN_EPD_MOSI
#define EPD_PIN_SCK     DEVKIT_PIN_EPD_SCK
#define EPD_PIN_CS      DEVKIT_PIN_EPD_CS
#define EPD_PIN_DC      DEVKIT_PIN_EPD_DC
#define EPD_PIN_RST     DEVKIT_PIN_EPD_RST
#define EPD_PIN_BUSY    DEVKIT_PIN_EPD_BUSY
#define EPD_SPI_HOST    DEVKIT_EPD_SPI_HOST
#define EPD_BOARD_NAME  "esp32s3_devkit"
#endif

#ifndef EPD_SPI_CLOCK_HZ
#define EPD_SPI_CLOCK_HZ    (10 * 1000 * 1000)  // Arduino ref uses 10 MHz
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_hwspi";

// ─── SPI device handle ─────────────────────────────────────────────────
static spi_device_handle_t spi_dev;

// ─── GPIO / Delay helpers ──────────────────────────────────────────────

static void delay_xms(unsigned int xms)
{
    vTaskDelay(pdMS_TO_TICKS(xms));
}

// ─── SPI write (hardware SPI, mode 0, MSB first) ──────────────────────

static void spi_write_byte(uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_data = {data},              // USE_TXDATA reads from inline tx_data[4], NOT tx_buffer
        .flags = SPI_TRANS_USE_TXDATA,
    };
    spi_device_transmit(spi_dev, &t);
}

static void spi_write_bytes(const uint8_t *data, uint32_t len)
{
    // Transmit in 4096-byte chunks via DMA for efficiency.
    // ESP-IDF SPI driver handles polling vs DMA automatically.
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk = (len - offset) > 4096 ? 4096 : (len - offset);
        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = data + offset,
        };
        spi_device_transmit(spi_dev, &t);
        offset += chunk;
        vTaskDelay(1);  // yield to prevent watchdog timeout
    }
}

// ─── SPI command / data write ──────────────────────────────────────────

static void epd_write_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);   // DC=0: command
    gpio_set_level(EPD_PIN_CS, 0);   // CS=0: assert
    spi_write_byte(cmd);
    gpio_set_level(EPD_PIN_CS, 1);   // CS=1: de-assert
}

static void epd_write_data(uint8_t data)
{
    gpio_set_level(EPD_PIN_DC, 1);   // DC=1: data
    gpio_set_level(EPD_PIN_CS, 0);   // CS=0: assert
    spi_write_byte(data);
    gpio_set_level(EPD_PIN_CS, 1);   // CS=1: de-assert
}

static void epd_write_data_pkg(const uint8_t *data, uint32_t len)
{
    gpio_set_level(EPD_PIN_DC, 1);   // DC=1: data
    gpio_set_level(EPD_PIN_CS, 0);   // CS=0: assert
    spi_write_bytes(data, len);
    gpio_set_level(EPD_PIN_CS, 1);   // CS=1: de-assert
}

// ─── GPIO Configuration ────────────────────────────────────────────────

void epd_gpio_config(void)
{
    ESP_LOGI(TAG, "Configuring GPIO and SPI for GDEM0397T81P (board=%s, MOSI=%d SCK=%d CS=%d DC=%d RST=%d BUSY=%d, %d Hz)",
             EPD_BOARD_NAME, EPD_PIN_MOSI, EPD_PIN_SCK, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY, EPD_SPI_CLOCK_HZ);

    // Configure control pins as outputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << EPD_PIN_CS) |
                        (1ULL << EPD_PIN_DC) |
                        (1ULL << EPD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Configure BUSY pin as input
    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // Initial pin states
    gpio_set_level(EPD_PIN_CS, 1);   // CS inactive
    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_RST, 1);

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = EPD_PIN_MOSI,
        .miso_io_num = -1,         // no MISO needed
        .sclk_io_num = EPD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Attach device
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,                           // CPOL=0, CPHA=0
        .clock_speed_hz = EPD_SPI_CLOCK_HZ,
        .spics_io_num = -1,                  // manual CS control
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &dev_cfg, &spi_dev));

    ESP_LOGI(TAG, "GPIO + SPI config done");
}

// ─── Busy / Status ─────────────────────────────────────────────────────

bool epd_is_busy(void)
{
    return (gpio_get_level(EPD_PIN_BUSY) == 1);
}

static void epd_read_busy(void)
{
    while (1) {
        if (gpio_get_level(EPD_PIN_BUSY) == 0)
            break;
        delay_xms(10);
    }
}

// ─── Hardware Reset ────────────────────────────────────────────────────

static void epd_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 0);
    delay_xms(10);
    gpio_set_level(EPD_PIN_RST, 1);
    delay_xms(10);
}

// ═══════════════════════════════════════════════════════════════════════════
// BELOW: All init sequences and display functions preserved VERBATIM from
// drivers/gdem0397t81p/Display_EPD_W21.c
// Only the low-level primitives (epd_write_cmd, epd_write_data, etc.)
// were changed for hardware SPI transport.
// ═══════════════════════════════════════════════════════════════════════════

// ─── Update functions ──────────────────────────────────────────────────

static void EPD_Update(void)
{
    epd_write_cmd(0x22); // Display Update Control
    epd_write_data(0xF7);
    epd_write_cmd(0x20); // Activate Display Update Sequence
    epd_read_busy();
}

static void EPD_Update_Fast(void)
{
    epd_write_cmd(0x22);
    epd_write_data(0xD7);
    epd_write_cmd(0x20);
    epd_read_busy();
}

static void EPD_Update_4G(void)
{
    epd_write_cmd(0x22);
    epd_write_data(0xD7);
    epd_write_cmd(0x20);
    epd_read_busy();
}

static void EPD_Part_Update(void)
{
    epd_write_cmd(0x22);
    epd_write_data(0xFF);
    epd_write_cmd(0x20);
    epd_read_busy();
}

// ─── Full screen update initialization ─────────────────────────────────

void EPD_HW_Init(void)
{
    epd_reset();
    epd_read_busy();
    epd_write_cmd(0x12); // SWRESET
    epd_read_busy();

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    epd_write_cmd(0x0C);
    epd_write_data(0xAE);
    epd_write_data(0xC7);
    epd_write_data(0xC3);
    epd_write_data(0xC0);
    epd_write_data(0x80);

    epd_write_cmd(0x01); // Driver output control
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_data(0x02);

    epd_write_cmd(0x3C); // BorderWavefrom
    epd_write_data(0x01);

    epd_write_cmd(0x11); // data entry mode
    epd_write_data(0x03);

    epd_write_cmd(0x44); // set Ram-X address start/end position
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_HEIGHT - 1) % 256);
    epd_write_data((EPD_HEIGHT - 1) / 256);

    epd_write_cmd(0x45); // set Ram-Y address start/end position
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);

    epd_write_cmd(0x4E); // set RAM x address count to 0
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_cmd(0x4F); // set RAM y address count to 0
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_read_busy();
}

// ─── Fast update initialization ────────────────────────────────────────

void EPD_HW_Init_Fast(void)
{
    epd_reset();
    epd_read_busy();
    epd_write_cmd(0x12); // SWRESET
    epd_read_busy();

    epd_write_cmd(0x0C);
    epd_write_data(0xAE);
    epd_write_data(0xC7);
    epd_write_data(0xC3);
    epd_write_data(0xC0);
    epd_write_data(0x80);

    epd_write_cmd(0x01);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_data(0x02);

    epd_write_cmd(0x11);
    epd_write_data(0x03);

    epd_write_cmd(0x44);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_HEIGHT - 1) % 256);
    epd_write_data((EPD_HEIGHT - 1) / 256);

    epd_write_cmd(0x45);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);

    epd_write_cmd(0x4E);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_cmd(0x4F);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_read_busy();

    epd_write_cmd(0x3C);
    epd_write_data(0x01);

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    // Fast(1.5s)
    epd_write_cmd(0x1A);
    epd_write_data(0x6A);
}

// ─── 4 Gray update initialization ──────────────────────────────────────

void EPD_HW_Init_4G(void)
{
    epd_reset();
    epd_read_busy();
    epd_write_cmd(0x12); // SWRESET
    epd_read_busy();

    epd_write_cmd(0x0C);
    epd_write_data(0xAE);
    epd_write_data(0xC7);
    epd_write_data(0xC3);
    epd_write_data(0xC0);
    epd_write_data(0x80);

    epd_write_cmd(0x01);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_data(0x02);

    epd_write_cmd(0x11);
    epd_write_data(0x03);

    epd_write_cmd(0x44);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_HEIGHT - 1) % 256);
    epd_write_data((EPD_HEIGHT - 1) / 256);

    epd_write_cmd(0x45);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);

    epd_write_cmd(0x4E);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_cmd(0x4F);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_read_busy();

    epd_write_cmd(0x3C);
    epd_write_data(0x01);

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    // 4 Gray
    epd_write_cmd(0x1A);
    epd_write_data(0x5A);
}

// ─── 180-degree rotation initialization ────────────────────────────────

void EPD_HW_Init_180(void)
{
    epd_reset();
    epd_read_busy();
    epd_write_cmd(0x12); // SWRESET
    epd_read_busy();

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    epd_write_cmd(0x0C);
    epd_write_data(0xAE);
    epd_write_data(0xC7);
    epd_write_data(0xC3);
    epd_write_data(0xC0);
    epd_write_data(0x80);

    epd_write_cmd(0x01);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_data(0x02);

    epd_write_cmd(0x3C);
    epd_write_data(0x01);

    epd_write_cmd(0x11); // data entry mode
    epd_write_data(0x00); // 180

    epd_write_cmd(0x44); // set Ram-X address start/end position
    epd_write_data((EPD_HEIGHT - 1) % 256);
    epd_write_data((EPD_HEIGHT - 1) / 256);
    epd_write_data(0x00);
    epd_write_data(0x00);

    epd_write_cmd(0x45); // set Ram-Y address start/end position
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_data(0x00);
    epd_write_data(0x00);

    epd_write_cmd(0x4E);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_write_cmd(0x4F);
    epd_write_data(0x00);
    epd_write_data(0x00);
    epd_read_busy();
}

// ═══════════════════════════════════════════════════════════════════════════
// Display data transfer functions
// ═══════════════════════════════════════════════════════════════════════════

// Full screen update
void EPD_WhiteScreen_ALL(const unsigned char *datas)
{
    epd_write_cmd(0x24); // write RAM for black(0)/white (1)
    epd_write_data_pkg(datas, EPD_ARRAY);
    EPD_Update();
}

// Fast update display
void EPD_WhiteScreen_ALL_Fast(const unsigned char *datas)
{
    unsigned int i;
    epd_write_cmd(0x24);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(datas[i]);
    }

    epd_write_cmd(0x26);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(0xff);
    }
    EPD_Update_Fast();
}

// Clear screen to white
void EPD_WhiteScreen_White(void)
{
    unsigned int i;
    epd_write_cmd(0x24);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(0xff);
    }
    epd_write_cmd(0x26);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(0xff);
    }
    EPD_Update();
}

// Fill screen black
void EPD_WhiteScreen_Black(void)
{
    unsigned int i;
    epd_write_cmd(0x24);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(0x00);
    }
    EPD_Update();
}

// Partial update base map
void EPD_SetRAMValue_BaseMap(const unsigned char *datas)
{
    unsigned int i;
    epd_write_cmd(0x24);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(datas[i]);
    }
    epd_write_cmd(0x26);
    for (i = 0; i < EPD_ARRAY; i++) {
        epd_write_data(datas[i]);
    }
    EPD_Update();
}

// Partial update at specified region
void EPD_Dis_Part(unsigned int x_start, unsigned int y_start,
                  const unsigned char *datas,
                  unsigned int PART_COLUMN, unsigned int PART_LINE)
{
    unsigned int i;
    unsigned int x_end, y_end;

    x_start = x_start - x_start % 8;
    x_end = x_start + PART_LINE - 1;
    y_start = y_start;
    y_end = y_start + PART_COLUMN - 1;

    epd_reset();

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    epd_write_cmd(0x3C); // BorderWavefrom
    epd_write_data(0x80);

    epd_write_cmd(0x44); // set RAM x address start/end
    epd_write_data(x_start % 256);
    epd_write_data(x_start / 256);
    epd_write_data(x_end % 256);
    epd_write_data(x_end / 256);
    epd_write_cmd(0x45); // set RAM y address start/end
    epd_write_data(y_start % 256);
    epd_write_data(y_start / 256);
    epd_write_data(y_end % 256);
    epd_write_data(y_end / 256);

    epd_write_cmd(0x4E);
    epd_write_data(x_start % 256);
    epd_write_data(x_start / 256);
    epd_write_cmd(0x4F);
    epd_write_data(y_start % 256);
    epd_write_data(y_start / 256);

    epd_write_cmd(0x24);
    for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++) {
        epd_write_data(datas[i]);
    }
    EPD_Part_Update();
}

// Full screen partial update
void EPD_Dis_PartAll(const unsigned char *datas)
{
    unsigned int PART_COLUMN = EPD_HEIGHT, PART_LINE = EPD_WIDTH;

    epd_reset();

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    epd_write_cmd(0x3C); // BorderWavefrom
    epd_write_data(0x80);
    epd_write_cmd(0x24);

    epd_write_data_pkg(datas, PART_COLUMN * PART_LINE / 8);
    EPD_Part_Update();
}

// Deep sleep
void EPD_DeepSleep(void)
{
    epd_write_cmd(0x10); // Enter deep sleep
    epd_write_data(0x01);
    delay_xms(100);
}

// ─── Clock display (partial update at 5 positions) ─────────────────────

static void EPD_Dis_Part_RAM(unsigned int x_start, unsigned int y_start,
                             const unsigned char *datas,
                             unsigned int PART_COLUMN, unsigned int PART_LINE)
{
    unsigned int i;
    unsigned int x_end, y_end;

    x_start = x_start - x_start % 8;
    x_end = x_start + PART_LINE - 1;
    y_start = y_start;
    y_end = y_start + PART_COLUMN - 1;

    epd_reset();

    epd_write_cmd(0x18);
    epd_write_data(0x80);

    epd_write_cmd(0x3C);
    epd_write_data(0x80);

    epd_write_cmd(0x44);
    epd_write_data(x_start % 256);
    epd_write_data(x_start / 256);
    epd_write_data(x_end % 256);
    epd_write_data(x_end / 256);
    epd_write_cmd(0x45);
    epd_write_data(y_start % 256);
    epd_write_data(y_start / 256);
    epd_write_data(y_end % 256);
    epd_write_data(y_end / 256);

    epd_write_cmd(0x4E);
    epd_write_data(x_start % 256);
    epd_write_data(x_start / 256);
    epd_write_cmd(0x4F);
    epd_write_data(y_start % 256);
    epd_write_data(y_start / 256);

    epd_write_cmd(0x24);
    for (i = 0; i < PART_COLUMN * PART_LINE / 8; i++) {
        epd_write_data(datas[i]);
    }
}

void EPD_Dis_Part_Time(unsigned int x_startA, unsigned int y_startA, const unsigned char *datasA,
                       unsigned int x_startB, unsigned int y_startB, const unsigned char *datasB,
                       unsigned int x_startC, unsigned int y_startC, const unsigned char *datasC,
                       unsigned int x_startD, unsigned int y_startD, const unsigned char *datasD,
                       unsigned int x_startE, unsigned int y_startE, const unsigned char *datasE,
                       unsigned int PART_COLUMN, unsigned int PART_LINE)
{
    EPD_Dis_Part_RAM(x_startA, y_startA, datasA, PART_COLUMN, PART_LINE);
    EPD_Dis_Part_RAM(x_startB, y_startB, datasB, PART_COLUMN, PART_LINE);
    EPD_Dis_Part_RAM(x_startC, y_startC, datasC, PART_COLUMN, PART_LINE);
    EPD_Dis_Part_RAM(x_startD, y_startD, datasD, PART_COLUMN, PART_LINE);
    EPD_Dis_Part_RAM(x_startE, y_startE, datasE, PART_COLUMN, PART_LINE);
    EPD_Part_Update();
}

// ═══════════════════════════════════════════════════════════════════════════
// 4 Gray scale support
// ═══════════════════════════════════════════════════════════════════════════

static unsigned char In2bytes_Out1byte_RAM1(unsigned char data1, unsigned char data2)
{
    unsigned int i;
    unsigned char TempData1, TempData2;
    unsigned char outdata = 0x00;
    TempData1 = data1;
    TempData2 = data2;

    for (i = 0; i < 4; i++) {
        outdata = outdata << 1;
        if (((TempData1 & 0xC0) == 0xC0) || ((TempData1 & 0xC0) == 0x40))
            outdata = outdata | 0x01;
        else
            outdata = outdata | 0x00;
        TempData1 = TempData1 << 2;
    }

    for (i = 0; i < 4; i++) {
        outdata = outdata << 1;
        if ((TempData2 & 0xC0) == 0xC0 || (TempData2 & 0xC0) == 0x40)
            outdata = outdata | 0x01;
        else
            outdata = outdata | 0x00;
        TempData2 = TempData2 << 2;
    }
    return outdata;
}

static unsigned char In2bytes_Out1byte_RAM2(unsigned char data1, unsigned char data2)
{
    unsigned int i;
    unsigned char TempData1, TempData2;
    unsigned char outdata = 0x00;
    TempData1 = data1;
    TempData2 = data2;

    for (i = 0; i < 4; i++) {
        outdata = outdata << 1;
        if (((TempData1 & 0xC0) == 0xC0) || ((TempData1 & 0xC0) == 0x80))
            outdata = outdata | 0x01;
        else
            outdata = outdata | 0x00;
        TempData1 = TempData1 << 2;
    }

    for (i = 0; i < 4; i++) {
        outdata = outdata << 1;
        if ((TempData2 & 0xC0) == 0xC0 || (TempData2 & 0xC0) == 0x80)
            outdata = outdata | 0x01;
        else
            outdata = outdata | 0x00;
        TempData2 = TempData2 << 2;
    }
    return outdata;
}

void EPD_WhiteScreen_ALL_4G(const unsigned char *datas)
{
    unsigned int i;
    unsigned char tempOriginal;

    epd_write_cmd(0x24);
    for (i = 0; i < EPD_ARRAY * 2; i += 2) {
        tempOriginal = In2bytes_Out1byte_RAM1(*(datas + i), *(datas + i + 1));
        epd_write_data(~tempOriginal);
    }

    epd_write_cmd(0x26);
    for (i = 0; i < EPD_ARRAY * 2; i += 2) {
        tempOriginal = In2bytes_Out1byte_RAM2(*(datas + i), *(datas + i + 1));
        epd_write_data(~tempOriginal);
    }
    EPD_Update_4G();
}

// 4 Gray with pre-split plane buffers
void EPD_Update_4Gray_WithBuffers(const uint8_t *ram1_data, const uint8_t *ram2_data)
{
    epd_write_cmd(0x24);
    epd_write_data_pkg(ram1_data, EPD_ARRAY);

    epd_write_cmd(0x26);
    epd_write_data_pkg(ram2_data, EPD_ARRAY);

    EPD_Update_4G();
}
