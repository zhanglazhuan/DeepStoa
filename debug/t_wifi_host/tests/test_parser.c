#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "frame_parser.h"

static void header(uint8_t *p, uint16_t fc)
{
    memset(p, 0, 128);
    p[0] = fc; p[1] = fc >> 8;
    const uint8_t sta[] = {2, 1, 2, 3, 4, 5}, ap[] = {2, 6, 7, 8, 9, 10};
    memcpy(p + 4, ap, 6);
    memcpy(p + 10, sta, 6);
    memcpy(p + 16, ap, 6);
    p[22] = 0x30; p[23] = 0x12;
}

int main(void)
{
    uint8_t p[128];
    frame_info_t f;
    header(p, 0x08b0); /* Retried authentication from STA. */
    p[24] = 3; p[26] = 1; p[28] = 76;
    for (unsigned n = 0; n < 30; ++n) assert(!frame_parse(p, n, &f));
    assert(frame_parse(p, 30, &f));
    assert(f.kind == FRAME_AUTH && f.retry && !f.from_ap);
    assert(f.algorithm == 3 && f.auth_sequence == 1 && f.status == 76);
    assert(f.sequence == 0x123);
    memcpy(p + 10, p + 16, 6);
    assert(frame_parse(p, 30, &f) && f.from_ap);
    p[1] |= 0x40; /* Protected body must not be decoded. */
    assert(frame_parse(p, 30, &f) && f.protected_frame && f.status == 0);
    header(p, 0x0010); p[26] = 17;
    assert(!frame_parse(p, 29, &f));
    assert(frame_parse(p, 30, &f) && f.kind == FRAME_ASSOC_RESP && f.status == 17);
    header(p, 0x00c0); p[24] = 2;
    assert(!frame_parse(p, 25, &f));
    assert(frame_parse(p, 26, &f) && f.kind == FRAME_DEAUTH && f.reason == 2);
    header(p, 0x00d4);
    assert(!frame_parse(p, 9, &f));
    assert(frame_parse(p, 10, &f) && f.kind == FRAME_ACK);
    assert(f.ta[0] == 0 && f.bssid[0] == 0);

    const uint8_t llc[] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e, 2, 3, 0, 95};
    header(p, 0x0108); /* To DS, ordinary data */
    memcpy(p + 24, llc, sizeof(llc));
    assert(!frame_parse(p, 35, &f));
    assert(frame_parse(p, 36, &f) && f.kind == FRAME_EAPOL && f.eapol_type == 3);
    assert(memcmp(f.bssid, p + 4, 6) == 0);
    p[1] |= 0x40;
    assert(!frame_parse(p, 36, &f)); /* Encrypted data isn't LLC. */
    header(p, 0x8288); /* From DS QoS + HT control */
    p[24] = 0; p[25] = 0;
    memcpy(p + 30, llc, sizeof(llc));
    assert(frame_parse(p, 42, &f) && f.from_ap);
    assert(memcmp(f.bssid, p + 10, 6) == 0);
    p[24] = 0x80;
    assert(!frame_parse(p, 42, &f)); /* A-MSDU needs a different parser. */
    header(p, 0x0308); memcpy(p + 30, llc, sizeof(llc));
    assert(!frame_parse(p, 42, &f)); /* WDS not mislabelled as infrastructure. */
    header(p, 0x04b0);
    assert(!frame_parse(p, 30, &f)); /* Fragmented auth not interpreted. */
    header(p, 0x00b1);
    assert(!frame_parse(p, 30, &f)); /* Unsupported protocol version. */
    assert(!frame_parse(NULL, 30, &f));
    assert(!frame_parse(p, 30, NULL));
    puts("frame parser tests passed");
    return 0;
}
