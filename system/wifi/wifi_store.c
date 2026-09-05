#include <string.h>
#include <time.h>
#include <flashdb.h>
#include "esp_log.h"

#include "wifi_store.h"

static const char *TAG = "wifi_store";

#define KV_WIFI_APS  "wifi_aps"

extern struct fdb_kvdb g_kvdb;

typedef struct {
    uint16_t    version;
    uint8_t     count;
    uint8_t     _pad;
    wifi_cred_t creds[WIFI_STORE_MAX];
} wifi_store_blob_t;

#define STORE_VERSION 1

static wifi_store_blob_t s_store;
static bool              s_loaded;

static uint32_t now_ts(void) { return (uint32_t)time(NULL); }

static void store_save(void)
{
    s_store.version = STORE_VERSION;

    struct fdb_blob blob;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, KV_WIFI_APS,
                                    fdb_blob_make(&blob, &s_store, sizeof(s_store)));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "save failed: %d", (int)err);
        return;
    }
    ESP_LOGI(TAG, "saved %u network(s)", s_store.count);
}

void wifi_store_load(void)
{
    if (s_loaded) return;

    memset(&s_store, 0, sizeof(s_store));

    struct fdb_blob blob;
    size_t read = fdb_kv_get_blob(&g_kvdb, KV_WIFI_APS,
                                  fdb_blob_make(&blob, &s_store, sizeof(s_store)));
    if (read != sizeof(s_store) || s_store.version != STORE_VERSION) {
        ESP_LOGI(TAG, "no saved networks (first run or format change)");
        memset(&s_store, 0, sizeof(s_store));
    } else {
        if (s_store.count > WIFI_STORE_MAX) s_store.count = WIFI_STORE_MAX;
        ESP_LOGI(TAG, "loaded %u saved network(s)", s_store.count);
    }
    s_loaded = true;
}

uint8_t wifi_store_count(void) { return s_store.count; }

const wifi_cred_t *wifi_store_at(uint8_t idx)
{
    return (idx < s_store.count) ? &s_store.creds[idx] : NULL;
}

const wifi_cred_t *wifi_store_find(const char *ssid)
{
    if (!ssid || ssid[0] == '\0') return NULL;
    for (uint8_t i = 0; i < s_store.count; i++) {
        if (strcmp(s_store.creds[i].ssid, ssid) == 0) return &s_store.creds[i];
    }
    return NULL;
}

const wifi_cred_t *wifi_store_most_recent(void)
{
    if (s_store.count == 0) return NULL;

    uint8_t best = 0;
    for (uint8_t i = 1; i < s_store.count; i++) {
        if (s_store.creds[i].last_used > s_store.creds[best].last_used) best = i;
    }
    return &s_store.creds[best];
}

bool wifi_store_put(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0') return false;

    /* 已存在就更新密码（用户可能改过 AP 密码） */
    for (uint8_t i = 0; i < s_store.count; i++) {
        if (strcmp(s_store.creds[i].ssid, ssid) != 0) continue;

        snprintf(s_store.creds[i].password, WIFI_STORE_PWD_MAX, "%s",
                 password ? password : "");
        s_store.creds[i].last_used    = now_ts();
        s_store.creds[i].auto_connect = 1;
        store_save();
        return true;
    }

    /* 满了：淘汰 last_used 最旧的一条 */
    uint8_t slot;
    if (s_store.count < WIFI_STORE_MAX) {
        slot = s_store.count++;
    } else {
        slot = 0;
        for (uint8_t i = 1; i < s_store.count; i++) {
            if (s_store.creds[i].last_used < s_store.creds[slot].last_used) slot = i;
        }
        ESP_LOGW(TAG, "store full, evicting \"%s\"", s_store.creds[slot].ssid);
    }

    memset(&s_store.creds[slot], 0, sizeof(wifi_cred_t));
    snprintf(s_store.creds[slot].ssid, WIFI_STORE_SSID_MAX, "%s", ssid);
    snprintf(s_store.creds[slot].password, WIFI_STORE_PWD_MAX, "%s",
             password ? password : "");
    s_store.creds[slot].last_used    = now_ts();
    s_store.creds[slot].auto_connect = 1;

    ESP_LOGI(TAG, "remembered \"%s\"", ssid);
    store_save();
    return true;
}

void wifi_store_touch(const char *ssid)
{
    for (uint8_t i = 0; i < s_store.count; i++) {
        if (strcmp(s_store.creds[i].ssid, ssid) != 0) continue;
        s_store.creds[i].last_used = now_ts();
        store_save();
        return;
    }
}

bool wifi_store_remove(const char *ssid)
{
    if (!ssid) return false;

    for (uint8_t i = 0; i < s_store.count; i++) {
        if (strcmp(s_store.creds[i].ssid, ssid) != 0) continue;

        memmove(&s_store.creds[i], &s_store.creds[i + 1],
                sizeof(wifi_cred_t) * (s_store.count - i - 1));
        s_store.count--;
        memset(&s_store.creds[s_store.count], 0, sizeof(wifi_cred_t));

        ESP_LOGI(TAG, "forgot \"%s\"", ssid);
        store_save();
        return true;
    }
    return false;
}

void wifi_store_clear(void)
{
    memset(&s_store, 0, sizeof(s_store));
    store_save();
}
