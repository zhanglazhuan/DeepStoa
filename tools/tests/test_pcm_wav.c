#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pcm_wav.h"

static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned v) { put16(p, v); put16(p + 2, v >> 16); }
static void fixture(unsigned char *p, unsigned rate, unsigned channels)
{
    memset(p, 0, 52);
    memcpy(p, "RIFF", 4); put32(p + 4, 44); memcpy(p + 8, "WAVEfmt ", 8);
    put32(p + 16, 16); put16(p + 20, 1); put16(p + 22, channels);
    put32(p + 24, rate); put32(p + 28, rate * channels * 2);
    put16(p + 32, channels * 2); put16(p + 34, 16);
    memcpy(p + 36, "data", 4); put32(p + 40, 8);
}
static bool parse(const unsigned char *p, size_t n, unsigned rate, unsigned channels)
{
    FILE *f = tmpfile(); assert(f);
    assert(fwrite(p, 1, n, f) == n); rewind(f);
    unsigned hz = 0, ch = 0; uint32_t offset = 0, bytes = 0;
    bool ok = pcm_wav_parse(f, &hz, &ch, &offset, &bytes);
    if (ok) { assert(hz == rate && ch == channels); assert(offset == 44 && bytes == 8); assert(ftell(f) == 44); }
    fclose(f); return ok;
}
int main(void)
{
    unsigned char p[52];
    for (unsigned ch = 1; ch <= 2; ++ch) {
        fixture(p, 16000, ch); assert(parse(p, sizeof(p), 16000, ch));
        fixture(p, 44100, ch); assert(parse(p, sizeof(p), 44100, ch));
    }
    fixture(p, 48000, 2); assert(!parse(p, sizeof(p), 0, 0));
    fixture(p, 16000, 1); put16(p + 20, 3); assert(!parse(p, sizeof(p), 0, 0)); // float
    fixture(p, 16000, 1); put16(p + 34, 24); assert(!parse(p, sizeof(p), 0, 0));
    fixture(p, 16000, 1); put16(p + 32, 4); assert(!parse(p, sizeof(p), 0, 0)); // bad alignment
    fixture(p, 16000, 1); put32(p + 28, 1); assert(!parse(p, sizeof(p), 0, 0)); // bad byte rate
    fixture(p, 16000, 1); assert(!parse(p, sizeof(p) - 1, 0, 0)); // truncation
    fixture(p, 16000, 1); put32(p + 40, 0xffffffff); assert(!parse(p, sizeof(p), 0, 0));
    fixture(p, 16000, 1); put32(p + 40, 7); assert(!parse(p, sizeof(p), 0, 0)); // partial sample
    fixture(p, 16000, 1); memcpy(p, "ID3", 3); assert(!parse(p, sizeof(p), 0, 0));
    puts("PCM WAV validation: PASS");
    return 0;
}
