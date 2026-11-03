#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FRAME_AUTH, FRAME_ASSOC_REQ, FRAME_ASSOC_RESP,
    FRAME_REASSOC_REQ, FRAME_REASSOC_RESP, FRAME_DEAUTH,
    FRAME_DISASSOC, FRAME_PROBE_REQ, FRAME_PROBE_RESP,
    FRAME_BEACON, FRAME_EAPOL, FRAME_ACK
} frame_kind_t;

typedef struct {
    frame_kind_t kind;
    uint8_t ra[6], ta[6], bssid[6];
    uint16_t sequence, algorithm, auth_sequence, status, reason;
    bool retry, protected_frame, from_ap;
    uint8_t eapol_type;
} frame_info_t;

/* Input excludes FCS. No unaligned reads, packet casts, or external dependencies. */
bool frame_parse(const uint8_t *data, size_t len, frame_info_t *info);
const char *frame_kind_name(frame_kind_t kind);
