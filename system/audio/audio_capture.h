#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
// Called from the UI thread. Stop is asynchronous; release only after done.
esp_err_t audio_capture_start(void);
void audio_capture_stop(void);
bool audio_capture_done(void);
const int16_t *audio_capture_pcm(size_t *samples);
uint32_t audio_capture_elapsed_ms(void);
uint8_t audio_capture_level(void);
esp_err_t audio_capture_result(void);
void audio_capture_release(void);
