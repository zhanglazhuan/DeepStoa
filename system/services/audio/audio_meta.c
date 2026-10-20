#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "esp_log.h"

#include "audio_meta.h"

static const char *TAG = "audio_meta";

/* ── 小工具 ───────────────────────────────────────────────────────── */

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint32_t rd_le32(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8)  |  (uint32_t)p[0];
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[1] << 8) | p[0]);
}

/* ID3v2 的长度字段是 syncsafe 的：每字节只用低 7 位 */
static uint32_t rd_syncsafe(const uint8_t *p)
{
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7)  |  (uint32_t)(p[3] & 0x7F);
}

/* ── ID3v2 文本帧 ─────────────────────────────────────────────────── */

/* 文本帧第一个字节是编码：0=ISO-8859-1 1=UTF-16+BOM 2=UTF-16BE 3=UTF-8 */
static void copy_text_frame(const uint8_t *data, uint32_t len, char *out, size_t out_len)
{
    out[0] = '\0';
    if (len < 2) return;

    uint8_t enc = data[0];
    const uint8_t *p = data + 1;
    uint32_t n = len - 1;

    if (enc == 0 || enc == 3) {
        /* ISO-8859-1 直接当 ASCII 拷（非 ASCII 字节原样保留，UTF-8 则本来就对） */
        size_t k = 0;
        for (uint32_t i = 0; i < n && k + 1 < out_len; i++) {
            if (p[i] == '\0') break;
            out[k++] = (char)p[i];
        }
        out[k] = '\0';
        return;
    }

    /* UTF-16 → UTF-8，只转 BMP，够用了 */
    bool be = (enc == 2);
    if (enc == 1 && n >= 2) {
        if (p[0] == 0xFF && p[1] == 0xFE)      { be = false; p += 2; n -= 2; }
        else if (p[0] == 0xFE && p[1] == 0xFF) { be = true;  p += 2; n -= 2; }
    }

    size_t k = 0;
    for (uint32_t i = 0; i + 1 < n && k + 4 < out_len; i += 2) {
        uint16_t u = be ? (uint16_t)((p[i] << 8) | p[i + 1])
                        : (uint16_t)((p[i + 1] << 8) | p[i]);
        if (u == 0) break;
        if (u < 0x80) {
            out[k++] = (char)u;
        } else if (u < 0x800) {
            out[k++] = (char)(0xC0 | (u >> 6));
            out[k++] = (char)(0x80 | (u & 0x3F));
        } else {
            out[k++] = (char)(0xE0 | (u >> 12));
            out[k++] = (char)(0x80 | ((u >> 6) & 0x3F));
            out[k++] = (char)(0x80 | (u & 0x3F));
        }
    }
    out[k] = '\0';
}

/**
 * 解析 ID3v2 头部（缓冲区里能放下的那部分）。
 * @return 整个 ID3v2 标签的字节数（音频数据从这里开始）；没有标签返回 0
 */
static uint32_t parse_id3v2(const uint8_t *buf, uint32_t buf_len, audio_meta_t *m)
{
    if (buf_len < 10 || memcmp(buf, "ID3", 3) != 0) return 0;

    uint8_t  ver  = buf[3];
    uint32_t size = rd_syncsafe(buf + 6) + 10;   /* 不含 header 的 10 字节 */

    /* v2.2 的帧头是 3+3 字节，格式不同；用得极少，只跳过不解析 */
    if (ver < 3) return size;

    uint32_t pos = 10;
    uint32_t end = (size < buf_len) ? size : buf_len;

    while (pos + 10 <= end) {
        const uint8_t *fh = buf + pos;
        if (fh[0] == 0) break;   /* padding */

        /* v2.4 的帧长是 syncsafe，v2.3 是普通大端 */
        uint32_t flen = (ver >= 4) ? rd_syncsafe(fh + 4) : rd_be32(fh + 4);
        pos += 10;
        if (flen == 0 || pos + flen > end) break;

        if      (memcmp(fh, "TIT2", 4) == 0) copy_text_frame(buf + pos, flen, m->title,  sizeof(m->title));
        else if (memcmp(fh, "TPE1", 4) == 0) copy_text_frame(buf + pos, flen, m->artist, sizeof(m->artist));
        else if (memcmp(fh, "TALB", 4) == 0) copy_text_frame(buf + pos, flen, m->album,  sizeof(m->album));

        pos += flen;
    }
    return size;
}

/* ── MPEG 帧头 ────────────────────────────────────────────────────── */

/* Layer III 的比特率表，单位 kbps */
static const uint16_t k_br_v1l3[16] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
static const uint16_t k_br_v2l3[16] = {0, 8,16,24,32,40,48,56, 64, 80, 96,112,128,144,160,0};
static const uint32_t k_sr[4][3] = {
    {11025, 12000,  8000},   /* MPEG 2.5 */
    {    0,     0,     0},
    {22050, 24000, 16000},   /* MPEG 2   */
    {44100, 48000, 32000},   /* MPEG 1   */
};

typedef struct {
    uint32_t sample_rate;
    uint32_t bitrate_kbps;
    uint16_t samples_per_frame;
    uint8_t  channels;
    uint8_t  ver_idx;   /* 3 = MPEG1 */
    uint32_t frame_len;
} mpeg_hdr_t;

static bool parse_mpeg_header(const uint8_t *p, mpeg_hdr_t *h)
{
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;

    uint8_t ver_idx   = (p[1] >> 3) & 0x03;
    uint8_t layer_idx = (p[1] >> 1) & 0x03;
    uint8_t br_idx    = (p[2] >> 4) & 0x0F;
    uint8_t sr_idx    = (p[2] >> 2) & 0x03;
    uint8_t padding   = (p[2] >> 1) & 0x01;
    uint8_t chan_mode = (p[3] >> 6) & 0x03;

    if (ver_idx == 1 || layer_idx != 1) return false;   /* 只认 Layer III */
    if (br_idx == 0 || br_idx == 15 || sr_idx == 3) return false;

    h->ver_idx      = ver_idx;
    h->sample_rate  = k_sr[ver_idx][sr_idx];
    if (h->sample_rate == 0) return false;

    h->bitrate_kbps = (ver_idx == 3) ? k_br_v1l3[br_idx] : k_br_v2l3[br_idx];
    if (h->bitrate_kbps == 0) return false;

    h->samples_per_frame = (ver_idx == 3) ? 1152 : 576;
    h->channels          = (chan_mode == 3) ? 1 : 2;
    h->frame_len = (h->samples_per_frame / 8) * h->bitrate_kbps * 1000 / h->sample_rate + padding;
    return true;
}

/* Xing/Info 头相对帧头的偏移，取决于 MPEG 版本和声道数 */
static uint32_t xing_offset(const mpeg_hdr_t *h)
{
    if (h->ver_idx == 3) return (h->channels == 1) ? 21 : 36;   /* MPEG1 */
    return (h->channels == 1) ? 13 : 21;                        /* MPEG2/2.5 */
}

/* ── WAV ──────────────────────────────────────────────────────────── */

static bool parse_wav(const uint8_t *buf, uint32_t len, audio_meta_t *m)
{
    if (len < 44 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        return false;
    }
    m->fmt = AUDIO_FMT_WAV;

    uint32_t pos = 12;
    uint32_t byte_rate = 0, data_bytes = 0;

    while (pos + 8 <= len) {
        const uint8_t *ck = buf + pos;
        uint32_t cksz = rd_le32(ck + 4);

        if (memcmp(ck, "fmt ", 4) == 0 && pos + 8 + 16 <= len) {
            m->channels    = (uint8_t)rd_le16(ck + 8 + 2);
            m->sample_rate = rd_le32(ck + 8 + 4);
            byte_rate      = rd_le32(ck + 8 + 8);
        } else if (memcmp(ck, "data", 4) == 0) {
            data_bytes = cksz;
            break;   /* data 一定在 fmt 之后，拿到就够了 */
        }
        pos += 8 + cksz + (cksz & 1);   /* chunk 按偶数对齐 */
    }

    if (byte_rate > 0 && data_bytes > 0) {
        m->duration_ms  = (uint32_t)((uint64_t)data_bytes * 1000u / byte_rate);
        m->bitrate_kbps = byte_rate * 8 / 1000;
    }
    return true;
}

/* ── MP3 ──────────────────────────────────────────────────────────── */

static bool parse_mp3(const uint8_t *buf, uint32_t len, uint32_t id3_size,
                      uint32_t file_size, audio_meta_t *m)
{
    /* 从 ID3 之后开始找同步字。标签比缓冲区大时就从头找，
     * 反正 0xFF 0xEx 加上后续字段的合法性校验足够可靠。 */
    uint32_t start = (id3_size < len) ? id3_size : 0;

    mpeg_hdr_t h;
    uint32_t   fpos = 0;
    bool       found = false;

    for (uint32_t i = start; i + 4 <= len; i++) {
        if (buf[i] != 0xFF) continue;
        if (parse_mpeg_header(buf + i, &h)) { fpos = i; found = true; break; }
    }
    if (!found) return false;

    m->fmt          = AUDIO_FMT_MP3;
    m->sample_rate  = h.sample_rate;
    m->channels     = h.channels;
    m->bitrate_kbps = h.bitrate_kbps;

    /* Xing / Info：有它才能算出 VBR 的精确时长 */
    uint32_t xo = fpos + xing_offset(&h);
    if (xo + 12 <= len &&
        (memcmp(buf + xo, "Xing", 4) == 0 || memcmp(buf + xo, "Info", 4) == 0)) {

        m->vbr = (memcmp(buf + xo, "Xing", 4) == 0);
        uint32_t flags = rd_be32(buf + xo + 4);
        if (flags & 0x0001) {
            uint32_t frames = rd_be32(buf + xo + 8);
            if (frames > 0) {
                m->duration_ms = (uint32_t)((uint64_t)frames * h.samples_per_frame
                                            * 1000u / h.sample_rate);
                /* 有精确时长就能反算平均比特率，比帧头里那个准 */
                uint32_t audio_bytes = (file_size > id3_size) ? (file_size - id3_size) : 0;
                if (m->duration_ms > 0 && audio_bytes > 0) {
                    m->bitrate_kbps = (uint32_t)((uint64_t)audio_bytes * 8u
                                                 / m->duration_ms);
                }
            }
        }
        return true;
    }

    /* 没有 Xing 头：按 CBR 估。VBR 文件估出来会偏，但比给 0 强 ——
     * 真正精确的 VBR 时长必须有 Xing/VBRI，或者整文件扫帧。 */
    uint32_t audio_bytes = (file_size > id3_size) ? (file_size - id3_size) : 0;
    if (audio_bytes > 0 && h.bitrate_kbps > 0) {
        m->duration_ms = (uint32_t)((uint64_t)audio_bytes * 8u / h.bitrate_kbps);
    }
    return true;
}

/* ── 入口 ─────────────────────────────────────────────────────────── */

bool audio_meta_read(const char *path, audio_meta_t *out)
{
    if (!path || !out) return false;
    memset(out, 0, sizeof(*out));

    struct stat st;
    if (stat(path, &st) != 0) return false;
    out->size = (uint32_t)st.st_size;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    static uint8_t buf[AUDIO_META_SCAN_BYTES];   /* 4KB，别放栈上 */
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 16) return false;

    if (parse_wav(buf, (uint32_t)n, out)) return true;

    uint32_t id3 = parse_id3v2(buf, (uint32_t)n, out);
    if (parse_mp3(buf, (uint32_t)n, id3, out->size, out)) return true;

    ESP_LOGD(TAG, "unrecognized: %s", path);
    return false;
}

void audio_meta_display_name(const audio_meta_t *m, const char *fallback,
                             char *out, size_t out_len)
{
    if (!out || out_len == 0) return;

    if (m && m->title[0] != '\0') {
        if (m->artist[0] != '\0') snprintf(out, out_len, "%s - %s", m->artist, m->title);
        else                      snprintf(out, out_len, "%s", m->title);
        return;
    }
    snprintf(out, out_len, "%s", fallback ? fallback : "");
}
