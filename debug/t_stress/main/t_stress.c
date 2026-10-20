#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "driver/temperature_sensor.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "Display_EPD_W21.h"
#include "aw9523.h"
#include "board_audio.h"
#include "deepstoa_v1.h"

static const char *TAG = "t_stress";

#define RUN_SECONDS       600
#define STOP_TEMP_C       80.0f
#define SD_MOUNT          "/sdcard"
#define SD_DIR            SD_MOUNT "/.deepstoa_stress"
#define SD_FILE           SD_DIR "/read.bin"
#define SD_FILE_BYTES     (4U * 1024U * 1024U)
#define CPU_WINDOW_US     100000

static atomic_bool stop_load;
static uint8_t *epd_frame;
static temperature_sensor_handle_t die_temp;
static FILE *sd_file;
static bool remove_sd_file;
static uint32_t frame_no;

static void cpu_load(void *arg)
{
    const unsigned core = (unsigned)(uintptr_t)arg;
    volatile uint32_t x = 0xA341316CU ^ core;
    while (!atomic_load(&stop_load)) {
        int64_t until = esp_timer_get_time() + CPU_WINDOW_US;
        while (esp_timer_get_time() < until && !atomic_load(&stop_load)) {
            x = (x ^ (x << 13)) * 0x45d9f3bU + (x >> 7) + 1U;
        }
        vTaskDelay(1); /* yield to watchdog, audio and system tasks */
    }
    ESP_LOGI(TAG, "CPU worker core %u stopped (checksum=%08x)", core, (unsigned)x);
    vTaskDelete(NULL);
}

static void sd_load(void *arg)
{
    (void)arg;
    static uint8_t block[64 * 1024] __attribute__((aligned(16)));
    uint64_t bytes = 0;
    int64_t report_at = esp_timer_get_time() + 10000000;
    while (!atomic_load(&stop_load)) {
        if (fseek(sd_file, 0, SEEK_SET) != 0) break;
        while (!atomic_load(&stop_load)) {
            size_t n = fread(block, 1, sizeof(block), sd_file);
            if (!n) break;
            bytes += n;
            /* Touch every cache line so the SD stream is actually consumed. */
            for (size_t i = 0; i < n; i += 64) block[i] ^= (uint8_t)i;
            int64_t now = esp_timer_get_time();
            if (now >= report_at) {
                ESP_LOGI(TAG, "SD read %.2f MiB/s total=%.1f MiB",
                         bytes / 10.0 / (1024.0 * 1024.0),
                         bytes / (1024.0 * 1024.0));
                bytes = 0;
                report_at = now + 10000000;
            }
        }
        if (ferror(sd_file)) {
            ESP_LOGE(TAG, "SD read failed");
            break;
        }
    }
    ESP_LOGI(TAG, "SD worker stopped");
    vTaskDelete(NULL);
}

static void audio_load(void *arg)
{
    (void)arg;
    enum { FRAMES = 512, SAMPLES = FRAMES * 2 };
    static int16_t pcm[SAMPLES];
    uint32_t phase = 0;
    const uint32_t phase_step = (uint32_t)((880.0 * 4294967296.0) / 44100.0);
    for (unsigned i = 0; i < FRAMES; ++i) {
        int16_t sample = (int16_t)(sinf((float)phase * (6.283185307f / 4294967296.0f)) * 3000.0f);
        pcm[i * 2] = sample;
        pcm[i * 2 + 1] = sample;
        phase += phase_step;
    }
    while (!atomic_load(&stop_load)) {
        if (board_audio_write(pcm, SAMPLES) != ESP_OK) {
            ESP_LOGE(TAG, "speaker write failed");
            break;
        }
    }
    board_audio_set_amplifier(false);
    ESP_LOGI(TAG, "audio worker stopped");
    vTaskDelete(NULL);
}

static esp_err_t mount_sd(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_4BIT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = DEEPV1_PIN_SD_CLK;
    slot.cmd = DEEPV1_PIN_SD_CMD;
    slot.d0 = DEEPV1_PIN_SD_DAT0;
    slot.d1 = DEEPV1_PIN_SD_DAT1;
    slot.d2 = DEEPV1_PIN_SD_DAT2;
    slot.d3 = DEEPV1_PIN_SD_DAT3;
    slot.width = 4;
    esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &cfg, &card);
    if (err == ESP_OK) sdmmc_card_print_info(stdout, card);
    return err;
}

static bool prepare_sd_file(void)
{
    if (mkdir(SD_DIR, 0755) != 0) {
        struct stat st;
        if (stat(SD_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    }
    struct stat st;
    if (stat(SD_FILE, &st) == 0) {
        if (!S_ISREG(st.st_mode) || st.st_size < SD_FILE_BYTES) {
            ESP_LOGE(TAG, "%s exists but is smaller than 4 MiB; leaving it untouched", SD_FILE);
            return false;
        }
    } else {
        FILE *f = fopen(SD_FILE, "wb");
        if (!f) return false;
        static uint8_t zeros[4096];
        for (unsigned n = 0; n < SD_FILE_BYTES; n += sizeof(zeros)) {
            if (fwrite(zeros, 1, sizeof(zeros), f) != sizeof(zeros)) {
                fclose(f);
                remove(SD_FILE);
                return false;
            }
        }
        if (fclose(f) != 0) { remove(SD_FILE); return false; }
        remove_sd_file = true;
    }
    sd_file = fopen(SD_FILE, "rb");
    return sd_file != NULL;
}

static bool init_temperature(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
    esp_err_t err = temperature_sensor_install(&cfg, &die_temp);
    if (err != ESP_OK) return false;
    return temperature_sensor_enable(die_temp) == ESP_OK;
}

static void log_temperature(unsigned elapsed_s)
{
    float celsius;
    if (temperature_sensor_get_celsius(die_temp, &celsius) != ESP_OK) {
        ESP_LOGW(TAG, "die temperature read failed");
        return;
    }
    ESP_LOGI(TAG, "elapsed=%u s die_temp=%.1f C heap_internal_free=%u PSRAM_free=%u",
             elapsed_s, celsius,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (celsius >= STOP_TEMP_C) {
        ESP_LOGE(TAG, "temperature limit %.1f C reached; stopping stress load", (double)celsius);
        atomic_store(&stop_load, true);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "DeepStoa stress test: dual-core CPU + SDMMC read + EPD + speaker, %u min, stop at %.0f C",
             RUN_SECONDS / 60, (double)STOP_TEMP_C);
    ESP_LOGI(TAG, "Place device on a nonflammable open surface; press RESET to stop early.");

    ESP_ERROR_CHECK(aw9523_init(DEEPV1_I2C_PORT, DEEPV1_PIN_I2C_SDA,
                                DEEPV1_PIN_I2C_SCL, DEEPV1_PIN_IO_RESET,
                                DEEPV1_AW9523_ADDR));
    if (!init_temperature()) {
        ESP_LOGE(TAG, "on-chip temperature sensor unavailable; refusing unmonitored stress test");
        return;
    }
    if (mount_sd() != ESP_OK || !prepare_sd_file()) {
        ESP_LOGE(TAG, "SD setup failed; check card and available space (card will not be formatted)");
        return;
    }

    epd_frame = heap_caps_malloc(EPD_ARRAY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!epd_frame) { ESP_LOGE(TAG, "EPD frame allocation failed"); goto cleanup; }
    epd_gpio_config();
    EPD_HW_Init();
    EPD_WhiteScreen_White();

    esp_err_t err = board_audio_open(false, 44100, 2);
    if (err != ESP_OK) { ESP_LOGE(TAG, "speaker init failed: %s", esp_err_to_name(err)); goto cleanup; }
    board_audio_volume(10);
    board_audio_set_amplifier(true);

    xTaskCreatePinnedToCore(cpu_load, "stress_cpu0", 3072, (void *)(uintptr_t)0, 2, NULL, 0);
    xTaskCreatePinnedToCore(cpu_load, "stress_cpu1", 3072, (void *)(uintptr_t)1, 2, NULL, 1);
    xTaskCreatePinnedToCore(sd_load, "stress_sd", 4096, NULL, 3, NULL, 0);
    xTaskCreatePinnedToCore(audio_load, "stress_audio", 4096, NULL, 4, NULL, 1);

    ESP_LOGI(TAG, "LOAD STARTED: CPU workers pinned to cores 0/1, SD sequential read, 880 Hz low-volume tone, EPD refresh every 3 s");
    int64_t start = esp_timer_get_time();
    int64_t next_display = start;
    int64_t next_report = start;
    while (!atomic_load(&stop_load)) {
        int64_t now = esp_timer_get_time();
        unsigned elapsed = (unsigned)((now - start) / 1000000);
        if (elapsed >= RUN_SECONDS) break;
        if (now >= next_report) {
            log_temperature(elapsed);
            next_report = now + 2000000;
        }
        if (atomic_load(&stop_load)) break;
        if (now >= next_display && !epd_is_busy()) {
            memset(epd_frame, (frame_no++ & 1) ? 0x55 : 0xAA, EPD_ARRAY);
            EPD_Dis_PartAll_Async(epd_frame);
            ESP_LOGI(TAG, "display refresh submitted #%u", frame_no);
            next_display = now + 3000000;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    atomic_store(&stop_load, true);
    vTaskDelay(pdMS_TO_TICKS(500));
    board_audio_close();
    (void)epd_wait_ready();
    ESP_LOGI(TAG, "Stress test finished after %lld s", (long long)((esp_timer_get_time() - start) / 1000000));

cleanup:
    atomic_store(&stop_load, true);
    if (sd_file) { fclose(sd_file); sd_file = NULL; }
    if (remove_sd_file) remove(SD_FILE);
    if (epd_frame) { heap_caps_free(epd_frame); epd_frame = NULL; }
    if (die_temp) {
        temperature_sensor_disable(die_temp);
        temperature_sensor_uninstall(die_temp);
        die_temp = NULL;
    }
    ESP_LOGI(TAG, "Stress test stopped; SD card was not formatted");
}
