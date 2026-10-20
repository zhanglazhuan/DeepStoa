#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
bool pcm_wav_parse(FILE *f, unsigned *hz, unsigned *ch, uint32_t *offset, uint32_t *bytes);
