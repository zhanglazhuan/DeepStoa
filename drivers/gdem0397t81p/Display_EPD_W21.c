// debug/t_display/main/epd_display.c
// GDEM0397T81P e-ink display driver (480x800)
//
// Display init sequences and command logic preserved VERBATIM from
// drivers/gdem0397t81p/Display_EPD_W21.c (originally from Zephyr RTOS port).
//
// Low-level transport: ESP-IDF hardware SPI (mode 0, MSB first) + direct GPIO.
// Pin definitions from: boards/esp32s3/esp32s3_devkit.h
//
// Wiring (matches Arduino reference):
//   GPIO 11=SPI MOSI, GPIO 12=SPI SCK,
//   GPIO 15=CS, GPIO 6=DC, GPIO 7=RST, GPIO 8=BUSY

#include <string.h>
#include "Display_EPD_W21.h"
#include "esp32s3_devkit.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "epd_hwspi";

// ─── SPI device handle ─────────────────────────────────────────────────
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"   /* esp_ptr_external_ram() */

/* 外部 RAM DMA 的对齐要求（cache line）。与 display_control.c 的
 * EPD_DMA_ALIGN 必须一致 —— 那边负责按这个对齐分配帧缓冲。 */
#define EPD_DMA_TX_ALIGN  64

/* 让 SPI 直接从 PSRAM 里的帧缓冲做 DMA（SPI_TRANS_DMA_USE_PSRAM），
 * 省掉每笔传输现场申请的那块内部 DMA 反弹缓冲和一次 memcpy。
 *
 * 默认关：开启后实测出现过"日志正常翻页、面板不刷新"的现象，而 SPI 这边
 * 一条错误都没有。原因还没定位（spi_master 的 check_trans_valid 不拒绝这个
 * 标志，esp_cache_msync 失败也会打日志），所以先退回已知可用的反弹缓冲路径。
 *
 * 排查前提是下面 spi_write_bytes() 里新加的返回值检查 —— 之前
 * spi_device_transmit() 的返回值被直接丢掉，传输失败在外面完全无感，
 * 这才是这个问题难查的根本原因。
 *
 * 想再试这条路：改成 1 重编。判据是 monitor 的 DMA 那一行 largest 是否
 * 还够 4096，以及面板是否正常刷新。 */
#define EPD_SPI_DMA_FROM_PSRAM  0

/* 每笔 SPI 传输的分块大小，与 bus_cfg.max_transfer_sz 一致。 */
#define EPD_TX_CHUNK  4096

/* 常驻的 DMA 反弹缓冲。
 *
 * 帧缓冲在 PSRAM，SPI 主机认定它不可 DMA，于是**每一笔**传输都会现场
 * heap_caps_aligned_alloc(4096, MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL) 一块
 * 临时内存再 memcpy。而本机内部 RAM 常年 85%+，这 4KB 时有时无 ——
 * 表现就是刷屏随机失败（Failed to allocate priv TX buffer），而且随便加个
 * UI 元素就可能把它压过临界点，非常难归因。
 *
 * 改成开机时一次性占住：那会儿内存最宽裕，之后这条路永不失败。代价是常驻
 * 4KB 内部 RAM —— 但这 4KB 本来每帧也要借 12 次，只是以前借完就还。
 * memcpy 的开销没有变化，驱动内部本来也在做同一件事。 */
static uint8_t *s_tx_bounce;

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
        uint32_t chunk = (len - offset) > EPD_TX_CHUNK ? EPD_TX_CHUNK : (len - offset);
        const uint8_t *src = data + offset;

        /* 源在 PSRAM（或别的不可 DMA 的地方）时，自己搬到常驻的内部 DMA 缓冲，
         * 别让 SPI 驱动去现场申请 —— 那一刻 LVGL 正在渲染，内部 RAM 被它的
         * 小分配掏到过只剩 318 字节，抢不到就整帧发不出去。 */
        if (s_tx_bounce && !esp_ptr_dma_capable(src)) {
            memcpy(s_tx_bounce, src, chunk);
            src = s_tx_bounce;
        }

        /* 帧缓冲在 PSRAM 里。不加这个标志的话，SPI 主机认为 TX 缓冲不可 DMA，
         * 每一笔都要现场 heap_caps_aligned_alloc() 一块**内部** DMA 反弹缓冲
         * 再 memcpy 进去（spi_master.c:1186 的 use_psram 判定）。内部 RAM 一紧
         * 就会刷屏 "Failed to allocate priv TX buffer" 然后整屏刷不出来。
         *
         * 只在地址和长度都对齐时才打标志：不对齐的话驱动仍会去申请反弹缓冲，
         * 而且那时 mem_cap 变成 DMA|SPIRAM —— PSRAM 堆没有 DMA 这个 cap，
         * 一样分配不出来。所以宁可让不对齐的那笔走回原来的内部反弹路径。
         * 当前 EPD_ARRAY=48000 拆成 11x4096 + 2944，两者都是 64 的整数倍。 */
        uint32_t flags = 0;
#if EPD_SPI_DMA_FROM_PSRAM
        if (esp_ptr_external_ram(src) &&
            ((((uintptr_t)src) | chunk) & (EPD_DMA_TX_ALIGN - 1)) == 0) {
            flags = SPI_TRANS_DMA_USE_PSRAM;
        }
#endif

        spi_transaction_t t = {
            .length = chunk * 8,
            .tx_buffer = src,
            .flags = flags,
        };

        /* 必须看返回值。传输失败时面板收不到任何数据，表现是"日志一切正常、
         * 屏幕纹丝不动"，从外面看像假死 —— 之前就是因为这里丢了返回值，
         * 白白多花了一轮才定位。限流打印，避免一帧 12 笔全失败时刷爆串口。 */
        esp_err_t err = spi_device_transmit(spi_dev, &t);
        if (err != ESP_OK) {
            static uint32_t s_err_count;
            if ((s_err_count++ % 64) == 0) {
                ESP_LOGE(TAG, "spi tx failed: %s (len=%u off=%u flags=0x%x, %u errors so far)",
                         esp_err_to_name(err), (unsigned)chunk, (unsigned)offset,
                         (unsigned)flags, (unsigned)s_err_count);
            }
        }
        offset += chunk;
        /* Match EPOS' package write: a partial-window row is sent without a
         * forced delay.  Yield only between chunks of an actually large
         * transfer so full-frame writes still remain scheduler-friendly. */
        if (offset < len) taskYIELD();
    }
}

// ─── SPI command / data write ──────────────────────────────────────────

static void epd_write_cmd(uint8_t cmd)
{
    gpio_set_level(DEVKIT_PIN_EPD_DC, 0);   // DC=0: command
    gpio_set_level(DEVKIT_PIN_EPD_CS, 0);   // CS=0: assert
    spi_write_byte(cmd);
    gpio_set_level(DEVKIT_PIN_EPD_CS, 1);   // CS=1: de-assert
}

static void epd_write_data(uint8_t data)
{
    gpio_set_level(DEVKIT_PIN_EPD_DC, 1);   // DC=1: data
    gpio_set_level(DEVKIT_PIN_EPD_CS, 0);   // CS=0: assert
    spi_write_byte(data);
    gpio_set_level(DEVKIT_PIN_EPD_CS, 1);   // CS=1: de-assert
}

static void epd_write_data_pkg(const uint8_t *data, uint32_t len)
{
    gpio_set_level(DEVKIT_PIN_EPD_DC, 1);   // DC=1: data
    gpio_set_level(DEVKIT_PIN_EPD_CS, 0);   // CS=0: assert
    spi_write_bytes(data, len);
    gpio_set_level(DEVKIT_PIN_EPD_CS, 1);   // CS=1: de-assert
}

// ─── GPIO Configuration ────────────────────────────────────────────────

void epd_gpio_config(void)
{
    ESP_LOGI(TAG, "Configuring GPIO and SPI for GDEM0397T81P");

    // Configure control pins as outputs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DEVKIT_PIN_EPD_CS) |
                        (1ULL << DEVKIT_PIN_EPD_DC) |
                        (1ULL << DEVKIT_PIN_EPD_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Configure BUSY pin as input
    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << DEVKIT_PIN_EPD_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // Initial pin states
    gpio_set_level(DEVKIT_PIN_EPD_CS, 1);   // CS inactive
    gpio_set_level(DEVKIT_PIN_EPD_DC, 0);
    gpio_set_level(DEVKIT_PIN_EPD_RST, 1);

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = DEVKIT_PIN_EPD_MOSI,
        .miso_io_num = -1,         // no MISO needed
        .sclk_io_num = DEVKIT_PIN_EPD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(DEVKIT_EPD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // Attach device
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,                           // CPOL=0, CPHA=0
        .clock_speed_hz = 10 * 1000 * 1000,  // 10 MHz (Arduino ref uses 10 MHz)
        .spics_io_num = -1,                  // manual CS control
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(DEVKIT_EPD_SPI_HOST, &dev_cfg, &spi_dev));

    /* 开机就把 DMA 反弹缓冲占住 —— 此刻内部 RAM 最宽裕。晚一步等到刷屏时
     * 再要，就要和正在渲染的 LVGL 抢，抢不到整帧就发不出去。 */
    s_tx_bounce = heap_caps_aligned_alloc(64, EPD_TX_CHUNK,
                                          MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_tx_bounce) {
        ESP_LOGI(TAG, "TX bounce buffer: %d B internal DMA @ %p", EPD_TX_CHUNK, s_tx_bounce);
    } else {
        /* 退回旧行为：由 SPI 驱动每笔自己申请。会不会失败看当时的内存。 */
        ESP_LOGE(TAG, "TX bounce buffer alloc failed, falling back to per-transfer alloc");
    }

    ESP_LOGI(TAG, "GPIO + SPI config done");
}

// ─── Busy / Status ─────────────────────────────────────────────────────

bool epd_is_busy(void)
{
    return (gpio_get_level(DEVKIT_PIN_EPD_BUSY) == 1);
}

static void epd_read_busy(void)
{
    while (1) {
        if (gpio_get_level(DEVKIT_PIN_EPD_BUSY) == 0)
            break;
        delay_xms(10);
    }
}

// ─── Hardware Reset ────────────────────────────────────────────────────

static void epd_reset(void)
{
    gpio_set_level(DEVKIT_PIN_EPD_RST, 0);
    delay_xms(10);
    gpio_set_level(DEVKIT_PIN_EPD_RST, 1);
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

// ─── Partial update async (no BUSY wait) ──────────────────────────────

static void EPD_Part_Update_Async(void)
{
    epd_write_cmd(0x22);
    epd_write_data(0xFF);
    epd_write_cmd(0x20);
}

static void EPD_Restore_Full_Window(void)
{
    epd_write_cmd(0x44);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_data((EPD_HEIGHT - 1) % 256);
    epd_write_data((EPD_HEIGHT - 1) / 256);
    epd_write_cmd(0x45);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_data((EPD_WIDTH - 1) % 256);
    epd_write_data((EPD_WIDTH - 1) / 256);
    epd_write_cmd(0x4E);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_cmd(0x4F);
    epd_write_data(0x00); epd_write_data(0x00);
}

void EPD_Dis_PartAll_Async(const unsigned char *datas)
{
    EPD_Restore_Full_Window();
    epd_write_cmd(0x18); epd_write_data(0x80);
    epd_write_cmd(0x3C); epd_write_data(0x80);
    epd_write_cmd(0x24);
    epd_write_data_pkg(datas, EPD_HEIGHT * EPD_WIDTH / 8);
    EPD_Part_Update_Async();
}

void EPD_Sync_Base_Map(const unsigned char *datas)
{
    EPD_Restore_Full_Window();
    epd_write_cmd(0x24);
    epd_write_data_pkg((uint8_t *)datas, EPD_ARRAY);
    epd_write_cmd(0x26);
    epd_write_data_pkg((uint8_t *)datas, EPD_ARRAY);
}

void EPD_Dis_Part_Window_Activate(void)
{
    epd_write_cmd(0x18); epd_write_data(0x80);
    epd_write_cmd(0x3C); epd_write_data(0x80);
}

void EPD_Dis_Part_Window_Async(unsigned int x_start, unsigned int y_start,
                               unsigned int x_end, unsigned int y_end,
                               const unsigned char *frame_buffer,
                               unsigned int stride_bytes)
{
    unsigned int xs = x_start & ~0x07;
    unsigned int xe = x_end | 0x07;
    unsigned int bytes = (xe - xs + 1) / 8;
    uint8_t line[128] __attribute__((aligned(4)));

    for (unsigned int y = y_start; y <= y_end; y++) {
        epd_write_cmd(0x44);
        epd_write_data(xs % 256); epd_write_data(xs / 256);
        epd_write_data(xe % 256); epd_write_data(xe / 256);
        epd_write_cmd(0x45);
        epd_write_data(y % 256); epd_write_data(y / 256);
        epd_write_data(y % 256); epd_write_data(y / 256);
        epd_write_cmd(0x4E);
        epd_write_data(xs % 256); epd_write_data(xs / 256);
        epd_write_cmd(0x4F);
        epd_write_data(y % 256); epd_write_data(y / 256);
        epd_write_cmd(0x24);
        unsigned int off = y * stride_bytes + (xs / 8);
        memcpy(line, &frame_buffer[off], bytes);
        epd_write_data_pkg(line, bytes);
    }
    EPD_Part_Update_Async();
}
