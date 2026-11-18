#include "frame_parser.h"
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
    return p[0] | ((uint16_t)p[1] << 8);
}

const char *frame_kind_name(frame_kind_t kind)
{
    static const char *names[] = {
        "AUTH", "ASSOC_REQ", "ASSOC_RESP", "REASSOC_REQ", "REASSOC_RESP",
        "DEAUTH", "DISASSOC", "PROBE_REQ", "PROBE_RESP", "BEACON", "EAPOL", "ACK"
    };
    return (unsigned)kind < sizeof(names) / sizeof(names[0]) ? names[kind] : "?";
}

bool frame_parse(const uint8_t *p, size_t len, frame_info_t *f)
{
    if (!p || !f || len < 2) return false;
    memset(f, 0, sizeof(*f));
    uint16_t fc = le16(p);
    if (fc & 3) return false; /* Only 802.11 protocol version 0. */
    unsigned type = (fc >> 2) & 3, subtype = (fc >> 4) & 15;
    f->retry = (fc & 0x0800) != 0;
    f->protected_frame = (fc & 0x4000) != 0;
    if (type == 1 && subtype == 13) {
        if (len < 10) return false;
        f->kind = FRAME_ACK;
        memcpy(f->ra, p + 4, 6);
        return true; /* ACK has no transmitter address or BSSID. */
    }
    if ((type != 0 && type != 2) || len < 24) return false;
    memcpy(f->ra, p + 4, 6);
    memcpy(f->ta, p + 10, 6);
    memcpy(f->bssid, p + 16, 6);
    f->sequence = le16(p + 22) >> 4;
    if (type == 0) {
        if ((fc & 0x0400) || (p[22] & 15)) return false;
        f->from_ap = memcmp(f->ta, f->bssid, 6) == 0;
        switch (subtype) {
        case 0: f->kind = FRAME_ASSOC_REQ; if (len < 28) return false; break;
        case 1: f->kind = FRAME_ASSOC_RESP; if (len < 30) return false; break;
        case 2: f->kind = FRAME_REASSOC_REQ; if (len < 34) return false; break;
        case 3: f->kind = FRAME_REASSOC_RESP; if (len < 30) return false; break;
        case 4: f->kind = FRAME_PROBE_REQ; break;
        case 5: f->kind = FRAME_PROBE_RESP; if (len < 36) return false; break;
        case 8: f->kind = FRAME_BEACON; if (len < 36) return false; break;
        case 10: f->kind = FRAME_DISASSOC; if (len < 26) return false; break;
        case 11: f->kind = FRAME_AUTH; if (len < 30) return false; break;
        case 12: f->kind = FRAME_DEAUTH; if (len < 26) return false; break;
        default: return false;
        }
        /* Protected management body is ciphertext, not status/reason fields. */
        if (f->protected_frame) return true;
        if (f->kind == FRAME_AUTH) {
            f->algorithm = le16(p + 24);
            f->auth_sequence = le16(p + 26);
            f->status = le16(p + 28);
        } else if (f->kind == FRAME_ASSOC_RESP || f->kind == FRAME_REASSOC_RESP) {
            f->status = le16(p + 26);
        } else if (f->kind == FRAME_DEAUTH || f->kind == FRAME_DISASSOC) {
            f->reason = le16(p + 24);
        }
        return true;
    }
    /* Only unencrypted, nonfragmented LLC/SNAP EAPOL data is useful here. */
    if (f->protected_frame || (fc & 0x0400) || (p[22] & 15) || (subtype & 4)) return false;
    unsigned ds = (fc >> 8) & 3;
    size_t hdr = ds == 3 ? 30 : 24;
    if (len < hdr) return false;
    if (ds == 1) memcpy(f->bssid, f->ra, 6);
    if (ds == 2) memcpy(f->bssid, f->ta, 6);
    if (ds == 3) return false; /* WDS has no infrastructure BSSID. */
    f->from_ap = ds == 2;
    if (subtype & 8) {
        if (len < hdr + 2 || (p[hdr] & 0x80)) return false; /* A-MSDU */
        hdr += 2;
        if (fc & 0x8000) hdr += 4; /* HT control in QoS data. */
    }
    static const uint8_t snap[] = {0xaa, 0xaa, 0x03, 0, 0, 0, 0x88, 0x8e};
    if (len < hdr + 12 || memcmp(p + hdr, snap, sizeof(snap)) != 0) return false;
    f->kind = FRAME_EAPOL;
    f->eapol_type = p[hdr + 9];
    return true;
}
