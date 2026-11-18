#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "frame_parser.h"

#ifndef CONFIG_SNIFFER_ACK
#define CONFIG_SNIFFER_ACK 0
#endif
#ifndef CONFIG_SNIFFER_RAW
#define CONFIG_SNIFFER_RAW 0
#endif
#ifndef CONFIG_SNIFFER_RX_ERRORS
#define CONFIG_SNIFFER_RX_ERRORS 0
#endif

#define QUEUE_DEPTH 64
#define PEER_COUNT 16
#define WINDOW_US ((int64_t)CONFIG_SNIFFER_WINDOW_MS * 1000)

static const char *TAG = "wifi_host";
static QueueHandle_t packets;
static uint8_t sta_filter[6], ap_filter[6];
static bool have_sta, have_ap;
static atomic_uint rx_total, rx_error, queue_drops;
static atomic_uint error_suppressed, error_queue_drops;

typedef struct {
    int64_t time_us;
    uint32_t hw_time_us;
    uint16_t sig_len;
    uint8_t rx_state;
    bool invalid;
    frame_info_t frame;
    uint16_t wire_len, cap_len;
    int8_t rssi;
    uint8_t channel;
#if CONFIG_SNIFFER_RAW
    uint8_t data[CONFIG_SNIFFER_SNAPLEN];
#endif
} packet_t;

typedef struct {
    bool used, active;
    uint8_t sta[6], ap[6];
    int64_t start_us, last_us;
    uint16_t algorithm;
    unsigned sta_auth, ap_auth, assoc_req, assoc_resp, eapol;
    unsigned rounds;
} peer_t;

static peer_t peers[PEER_COUNT];
static unsigned peer_overflows;

static bool parse_mac(const char *text, uint8_t mac[6])
{
    if (strlen(text) != 17) return false;
    for (unsigned i = 0; i < 6; ++i) {
        unsigned byte;
        int count = 0;
        if (!isxdigit((unsigned char)text[i * 3]) ||
            !isxdigit((unsigned char)text[i * 3 + 1]) ||
            sscanf(text + i * 3, "%2x%n", &byte, &count) != 1 || count != 2 ||
            (i < 5 && text[i * 3 + 2] != ':')) return false;
        mac[i] = byte;
    }
    static const uint8_t zero[6];
    return !(mac[0] & 1) && memcmp(mac, zero, 6) != 0;
}

static bool equals(const uint8_t a[6], const uint8_t b[6])
{
    return memcmp(a, b, 6) == 0;
}

static bool selected(const frame_info_t *f)
{
    if (f->kind == FRAME_ACK) {
        /* ACK carries RA only: cannot identify its transmitter. */
        return CONFIG_SNIFFER_ACK &&
               ((have_sta && equals(f->ra, sta_filter)) ||
                (have_ap && equals(f->ra, ap_filter)));
    }
    if (have_ap && !equals(f->bssid, ap_filter)) return false;
    if (have_sta && !equals(f->ta, sta_filter) && !equals(f->ra, sta_filter)) {
        /* Allow the selected AP's beacons as a receiver/channel sanity check. */
        return have_ap && f->kind == FRAME_BEACON;
    }
    return true;
}

static void receive_packet(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!buf || type == WIFI_PKT_MISC) return;
    const wifi_promiscuous_pkt_t *p = buf;
    atomic_fetch_add_explicit(&rx_total, 1, memory_order_relaxed);
    int64_t now = esp_timer_get_time();
    if (p->rx_ctrl.rx_state != 0 || p->rx_ctrl.sig_len < 4) {
        atomic_fetch_add_explicit(&rx_error, 1, memory_order_relaxed);
#if CONFIG_SNIFFER_RX_ERRORS
        /* Payload and MAC addresses of damaged frames are untrusted. Keep
         * only driver metadata, and reserve queue space for valid frames. */
        static int64_t second_start;
        static unsigned in_second;
        if (now - second_start >= 1000000) {
            second_start = now;
            in_second = 0;
        }
        if (in_second++ >= 10) {
            atomic_fetch_add_explicit(&error_suppressed, 1, memory_order_relaxed);
            return;
        }
        packet_t error = {
            .time_us = now, .hw_time_us = p->rx_ctrl.timestamp,
            .sig_len = p->rx_ctrl.sig_len, .rx_state = p->rx_ctrl.rx_state,
            .rssi = p->rx_ctrl.rssi, .channel = p->rx_ctrl.channel,
            .invalid = true,
        };
        if (uxQueueSpacesAvailable(packets) <= QUEUE_DEPTH / 2 ||
            xQueueSend(packets, &error, 0) != pdTRUE) {
            atomic_fetch_add_explicit(&error_queue_drops, 1, memory_order_relaxed);
        }
#endif
        return;
    }
    /* ESP-IDF sig_len includes the four-byte FCS. Never parse/capture it. */
    size_t len = p->rx_ctrl.sig_len - 4;
    packet_t item = {
        .time_us = now,
        .hw_time_us = p->rx_ctrl.timestamp,
        .wire_len = len,
        .cap_len = len < CONFIG_SNIFFER_SNAPLEN ? len : CONFIG_SNIFFER_SNAPLEN,
        .rssi = p->rx_ctrl.rssi,
        .channel = p->rx_ctrl.channel,
    };
    if (!frame_parse(p->payload, item.cap_len, &item.frame) || !selected(&item.frame)) return;
#if CONFIG_SNIFFER_RAW
    memcpy(item.data, p->payload, item.cap_len);
#endif
    if (xQueueSend(packets, &item, 0) != pdTRUE) {
        atomic_fetch_add_explicit(&queue_drops, 1, memory_order_relaxed);
    }
}

static void report_round(peer_t *p, int64_t end_us, const char *end_reason)
{
    const char *result;
    if (p->ap_auth == 0) {
        result = "STA_AUTH_SEEN_NO_AP_AUTH_OBSERVED";
    } else if (p->assoc_req > 0) {
        result = "AP_AUTH_AND_STA_ASSOC_SEEN";
    } else {
        result = "AP_AUTH_SEEN_NO_STA_ASSOC_OBSERVED";
    }
    ESP_LOGW(TAG, "WINDOW sta=" MACSTR " ap=" MACSTR
             " alg=%u window_ms=%d elapsed_ms=%" PRId64 " end=%s sta_auth=%u ap_auth=%u assoc_req=%u"
             " assoc_resp=%u eapol=%u result=%s (observation, not hardware verdict)",
             MAC2STR(p->sta), MAC2STR(p->ap), p->algorithm,
             CONFIG_SNIFFER_WINDOW_MS, (end_us - p->start_us) / 1000,
             end_reason, p->sta_auth, p->ap_auth,
             p->assoc_req, p->assoc_resp, p->eapol, result);
    p->active = false;
}

static void expire_rounds(int64_t now)
{
    for (unsigned i = 0; i < PEER_COUNT; ++i) {
        if (peers[i].active && now - peers[i].start_us >= WINDOW_US) {
            report_round(&peers[i], peers[i].start_us + WINDOW_US, "deadline");
        }
    }
}

static peer_t *get_peer(const frame_info_t *f, int64_t now)
{
    const uint8_t *sta = f->from_ap ? f->ra : f->ta;
    if ((sta[0] & 1) || equals(sta, f->bssid)) return NULL;
    peer_t *slot = NULL;
    for (unsigned i = 0; i < PEER_COUNT; ++i) {
        peer_t *p = &peers[i];
        if (p->used && equals(p->sta, sta) && equals(p->ap, f->bssid)) return p;
        if (!p->active && (!slot || !p->used || (slot->used && p->last_us < slot->last_us))) slot = p;
    }
    if (!slot) {
        ++peer_overflows;
        return NULL;
    }
    *slot = (peer_t){.used = true, .last_us = now};
    memcpy(slot->sta, sta, 6);
    memcpy(slot->ap, f->bssid, 6);
    return slot;
}

static void analyze(const packet_t *item)
{
    const frame_info_t *f = &item->frame;
    if (f->protected_frame || f->kind == FRAME_ACK || f->kind == FRAME_BEACON ||
        f->kind == FRAME_PROBE_REQ || f->kind == FRAME_PROBE_RESP ||
        f->kind == FRAME_DEAUTH || f->kind == FRAME_DISASSOC) return;
    peer_t *p = get_peer(f, item->time_us);
    if (!p) return;
    p->last_us = item->time_us;
    if (f->kind == FRAME_AUTH && !f->from_ap) {
        if (p->active && f->algorithm != p->algorithm) {
            report_round(p, item->time_us, "algorithm_changed");
        }
        if (!p->active) {
            p->active = true;
            p->start_us = item->time_us;
            p->algorithm = f->algorithm;
            p->sta_auth = p->ap_auth = p->assoc_req = p->assoc_resp = p->eapol = 0;
            ++p->rounds;
            ESP_LOGI(TAG, "WINDOW_START sta=" MACSTR " ap=" MACSTR " round=%u",
                     MAC2STR(p->sta), MAC2STR(p->ap), p->rounds);
        }
        ++p->sta_auth;
    } else if (p->active && f->kind == FRAME_AUTH && f->from_ap &&
               f->algorithm == p->algorithm &&
               (f->algorithm != 0 || f->auth_sequence == 2)) {
        ++p->ap_auth;
    } else if (p->active && !f->from_ap &&
               (f->kind == FRAME_ASSOC_REQ || f->kind == FRAME_REASSOC_REQ)) {
        ++p->assoc_req;
    } else if (p->active && f->from_ap &&
               (f->kind == FRAME_ASSOC_RESP || f->kind == FRAME_REASSOC_RESP)) {
        ++p->assoc_resp;
    } else if (p->active && f->kind == FRAME_EAPOL) {
        ++p->eapol;
    }
}

static bool show_beacon(const packet_t *item)
{
    /* Bound beacon logging to one per BSSID per 10 s, with a bounded AP table. */
    static struct { uint8_t mac[6]; int64_t last; bool used; } aps[16];
    unsigned oldest = 0;
    for (unsigned i = 0; i < 16; ++i) {
        if (aps[i].used && equals(aps[i].mac, item->frame.bssid)) {
            if (item->time_us - aps[i].last < 10000000) return false;
            aps[i].last = item->time_us;
            return true;
        }
        if (!aps[i].used) oldest = i;
        else if (aps[oldest].used && aps[i].last < aps[oldest].last) oldest = i;
    }
    /* Full table: limit new AP messages too, even in a crowded environment. */
    if (aps[oldest].used && item->time_us - aps[oldest].last < 10000000) return false;
    memcpy(aps[oldest].mac, item->frame.bssid, 6);
    aps[oldest].used = true;
    aps[oldest].last = item->time_us;
    return true;
}

static void log_packet(const packet_t *p)
{
    if (p->invalid) {
        ESP_LOGW(TAG, "RX_ERROR t=%" PRId64 "us hw_t=%" PRIu32
                 "us ch=%u rssi=%d rx_state=%u sig_len=%u source=UNKNOWN"
                 " (driver metadata; not proof of target transmission)",
                 p->time_us, p->hw_time_us, p->channel, p->rssi,
                 p->rx_state, p->sig_len);
        return;
    }
    const frame_info_t *f = &p->frame;
    bool show = f->kind != FRAME_BEACON || show_beacon(p);
    if (show) {
        ESP_LOGI(TAG, "RX t=%" PRId64 "us ch=%u rssi=%d %s"
                 " ra=" MACSTR " ta=" MACSTR " bssid=" MACSTR
                 " seq=%u retry=%u protected=%u len=%u",
                 p->time_us, p->channel, p->rssi, frame_kind_name(f->kind),
                 MAC2STR(f->ra), MAC2STR(f->ta), MAC2STR(f->bssid),
                 f->sequence, f->retry, f->protected_frame, p->wire_len);
        if (!f->protected_frame && f->kind == FRAME_AUTH) {
            ESP_LOGI(TAG, "  AUTH direction=%s alg=%u transaction=%u status=%u"
                     " rx_callback_us=%" PRId64 " rx_hw_us=%" PRIu32 " output_delay_us=%" PRId64,
                     f->from_ap ? "AP->STA" : "STA->AP", f->algorithm,
                     f->auth_sequence, f->status, p->time_us, p->hw_time_us,
                     esp_timer_get_time() - p->time_us);
        } else if (!f->protected_frame &&
                   (f->kind == FRAME_ASSOC_RESP || f->kind == FRAME_REASSOC_RESP)) {
            ESP_LOGI(TAG, "  ASSOC status=%u (0=success)", f->status);
        } else if (!f->protected_frame &&
                   (f->kind == FRAME_DEAUTH || f->kind == FRAME_DISASSOC)) {
            ESP_LOGI(TAG, "  DISCONNECT reason=%u", f->reason);
        } else if (f->kind == FRAME_EAPOL) {
            ESP_LOGI(TAG, "  EAPOL direction=%s type=%u (3=Key; not a decoded M1-M4 label)",
                     f->from_ap ? "AP->STA" : "STA->AP", f->eapol_type);
        }
    }
#if CONFIG_SNIFFER_RAW
    /* One complete line; no interleaving of the hex body with other log lines. */
    static char hex[CONFIG_SNIFFER_SNAPLEN * 2 + 1];
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < p->cap_len; ++i) {
        hex[2 * i] = digits[p->data[i] >> 4];
        hex[2 * i + 1] = digits[p->data[i] & 15];
    }
    hex[2 * p->cap_len] = 0;
    printf("CAP %" PRId64 " %u %u %s\n", p->time_us, p->wire_len, p->cap_len, hex);
#endif
}

static void packet_task(void *arg)
{
    packet_t item;
    int64_t last_stats = 0;
    unsigned selected_count = 0, sta_auth_count = 0;
    while (true) {
        bool got = xQueueReceive(packets, &item, pdMS_TO_TICKS(100)) == pdTRUE;
        /* Use capture time while draining the queue, not delayed serial output time. */
        expire_rounds(got ? item.time_us : esp_timer_get_time());
        if (got) {
            log_packet(&item);
            if (!item.invalid) {
                ++selected_count;
                if (item.frame.kind == FRAME_AUTH && !item.frame.from_ap) ++sta_auth_count;
                analyze(&item);
            }
        }
        int64_t now = esp_timer_get_time();
        if (now - last_stats >= 5000000) {
            last_stats = now;
            ESP_LOGI(TAG, "STATS rx=%u selected=%u rx_error=%u queue_drops=%u"
                     " peer_overflows=%u sta_auth_seen=%u queue_pending=%u",
                     atomic_load(&rx_total), selected_count, atomic_load(&rx_error),
                     atomic_load(&queue_drops), peer_overflows, sta_auth_count,
                     (unsigned)uxQueueMessagesWaiting(packets));
            if (have_sta && sta_auth_count == 0) {
                ESP_LOGW(TAG, "No target STA authentication observed yet; verify MAC/channel/timing/coverage.");
            }
            ESP_LOGI(TAG, "ERROR_STATS enabled=%u suppressed=%u error_queue_drops=%u"
                     " source=UNKNOWN rate_limit=10/s (not all RF activity is delivered)",
                     CONFIG_SNIFFER_RX_ERRORS, atomic_load(&error_suppressed),
                     atomic_load(&error_queue_drops));
        }
    }
}

void app_main(void)
{
    have_sta = strlen(CONFIG_SNIFFER_STA_MAC) > 0;
    have_ap = strlen(CONFIG_SNIFFER_AP_MAC) > 0;
    if ((have_sta && !parse_mac(CONFIG_SNIFFER_STA_MAC, sta_filter)) ||
        (have_ap && !parse_mac(CONFIG_SNIFFER_AP_MAC, ap_filter))) {
        ESP_LOGE(TAG, "Invalid MAC filter: expected unicast aa:bb:cc:dd:ee:ff");
        return;
    }
    ESP_LOGI(TAG, "Passive sniffer channel=%d STA=%s AP=%s window=%dms raw=%d",
             CONFIG_SNIFFER_CHANNEL, have_sta ? CONFIG_SNIFFER_STA_MAC : "ALL",
             have_ap ? CONFIG_SNIFFER_AP_MAC : "ALL", CONFIG_SNIFFER_WINDOW_MS,
             CONFIG_SNIFFER_RAW);
    /* Shared board NVS must never be erased automatically by a diagnostic. */
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    /* Permit fixed channels 1-13; passive only, no active scanning/transmitting. */
    wifi_country_t country = {.cc = "CN", .schan = 1, .nchan = 13,
                              .policy = WIFI_COUNTRY_POLICY_MANUAL};
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_SNIFFER_CHANNEL, WIFI_SECOND_CHAN_NONE));
    packets = xQueueCreate(QUEUE_DEPTH, sizeof(packet_t));
    assert(packets);
    BaseType_t created = xTaskCreate(packet_task, "sniffer_output", 6144, NULL, 4, NULL);
    assert(created == pdPASS);
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    if (CONFIG_SNIFFER_RX_ERRORS) {
        filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_FCSFAIL;
    }
    if (CONFIG_SNIFFER_ACK && (have_sta || have_ap)) {
        filter.filter_mask |= WIFI_PROMIS_FILTER_MASK_CTRL;
        wifi_promiscuous_filter_t ctrl = {.filter_mask = WIFI_PROMIS_CTRL_FILTER_MASK_ACK};
        ESP_ERROR_CHECK(esp_wifi_set_promiscuous_ctrl_filter(&ctrl));
    }
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(receive_packet));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_LOGI(TAG, "READY: now trigger the faulty board's Wi-Fi connection.");
}
