#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

// One owner at a time. Open/close on the system/UI thread; stop I/O tasks
// before closing. Capture uses the verified 16 kHz, four-slot ES7210 path.
esp_err_t board_audio_open(bool capture, unsigned rate, unsigned channels);
void board_audio_close(void);
esp_err_t board_audio_read(int16_t *raw, size_t frames);
esp_err_t board_audio_write(int16_t *pcm, size_t samples);
esp_err_t board_audio_volume(unsigned volume);
void board_audio_set_amplifier(bool enabled);
// ES7210 serial order: MIC1, MIC3 (reference), MIC2, MIC4 (unused).
void board_audio_to_mmr(const int16_t *raw, int16_t *mmr, size_t frames);
