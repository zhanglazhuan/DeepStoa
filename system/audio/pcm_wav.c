#include "pcm_wav.h"
#include <string.h>
static uint16_t le16(const unsigned char *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const unsigned char *p) { return le16(p) | ((uint32_t)le16(p + 2) << 16); }

// Validate chunk boundaries against both RIFF and the physical file before
// accepting it; never send compressed or malformed WAV data to the DAC.
bool pcm_wav_parse(FILE *f, unsigned *hz, unsigned *ch, uint32_t *offset, uint32_t *bytes)
{
    unsigned char h[16];
    if (fseek(f, 0, SEEK_END)) return false;
    long length = ftell(f);
    rewind(f);
    if (length < 12 || fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    uint64_t end = (uint64_t)le32(h + 4) + 8;
    if (end > (uint64_t)length || end < 12) return false;
    bool fmt = false;
    for (uint64_t pos = 12; pos + 8 <= end;) {
        if (fseek(f, (long)pos, SEEK_SET) || fread(h, 1, 8, f) != 8) return false;
        uint32_t n = le32(h + 4);
        uint64_t next = pos + 8 + n + (n & 1);
        if (next > end) return false;
        if (!memcmp(h, "fmt ", 4)) {
            if (n < 16 || fread(h, 1, 16, f) != 16) return false;
            *ch = le16(h + 2); *hz = le32(h + 4);
            if (le16(h) != 1 || le16(h + 14) != 16 || (*ch != 1 && *ch != 2) ||
                (*hz != 16000 && *hz != 44100) || le16(h + 12) != *ch * 2 || le32(h + 8) != *hz * *ch * 2) return false;
            fmt = true;
        } else if (!memcmp(h, "data", 4)) {
            if (!fmt || !n || n % (*ch * 2)) return false;
            *offset = (uint32_t)pos + 8; *bytes = n;
            return fseek(f, *offset, SEEK_SET) == 0;
        }
        pos = next;
    }
    return false;
}

