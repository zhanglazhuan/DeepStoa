#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "flash_control.h"
#include "blob_store.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define BLOB_MAGIC UINT32_C(0x41424C42)
#define BLOB_VERSION 1U
#define BLOB_PART_BYTES 3000U

static const char *TAG = "anki_blob";

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t item_size;
    uint8_t slot;
    uint8_t reserved;
} BlobManifest;

static size_t blob_size(const char *key)
{
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    return blob.saved.len;
}

static bool delete_key(const char *key)
{
    fdb_err_t err = fdb_kv_del(&g_kvdb, key);
    return err == FDB_NO_ERR || err == FDB_KV_NAME_ERR;
}

static void make_key(char *key, size_t size, const char *prefix,
                     int slot, unsigned part)
{
    if (slot < 0) snprintf(key, size, "%sM", prefix);
    else snprintf(key, size, "%s%u_%u", prefix, (unsigned)slot, part);
}

static bool read_manifest(const char *prefix, size_t item_size,
                          uint16_t capacity, BlobManifest *manifest,
                          bool *exists)
{
    char key[32];
    make_key(key, sizeof(key), prefix, -1, 0);
    size_t len = blob_size(key);
    *exists = len != 0;
    if (!*exists) return true;
    if (len != sizeof(*manifest)) return false;
    struct fdb_blob blob;
    if (fdb_kv_get_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, manifest, len)) != len) return false;
    return manifest->magic == BLOB_MAGIC &&
           manifest->version == BLOB_VERSION &&
           manifest->item_size == item_size &&
           manifest->count <= capacity && manifest->slot <= 1;
}

static unsigned items_per_part(size_t item_size)
{
    return item_size ? BLOB_PART_BYTES / item_size : 0;
}

static bool delete_slot(const char *prefix, size_t item_size,
                        uint16_t count, unsigned slot)
{
    unsigned step = items_per_part(item_size);
    if (!step) return false;
    unsigned parts = (count + step - 1) / step;
    bool ok = true;
    for (unsigned i = 0; i < parts; i++) {
#ifdef ESP_PLATFORM
        if ((i & 3U) == 0) vTaskDelay(1);
#endif
        char key[32];
        make_key(key, sizeof(key), prefix, (int)slot, i);
        if (!delete_key(key)) ok = false;
    }
    return ok;
}

bool anki_blob_load(const char *legacy_key, const char *prefix,
                    size_t item_size, uint16_t capacity, void *items,
                    uint16_t *count)
{
    if (!legacy_key || !prefix || !items || !count ||
        item_size > UINT16_MAX || !items_per_part(item_size)) {
        return false;
    }
    *count = 0;
    BlobManifest manifest;
    bool exists;
    if (!read_manifest(prefix, item_size, capacity, &manifest, &exists)) {
        ESP_LOGE(TAG, "invalid manifest for %s", prefix);
        return false;
    }
    if (!exists) {
        size_t len = blob_size(legacy_key);
        if (len == 0) return true;
        if (len % item_size || len > (size_t)capacity * item_size) return false;
        struct fdb_blob blob;
        if (fdb_kv_get_blob(&g_kvdb, legacy_key,
                            fdb_blob_make(&blob, items, len)) != len) return false;
        *count = (uint16_t)(len / item_size);
        return true;
    }

    unsigned step = items_per_part(item_size);
    unsigned parts = (manifest.count + step - 1) / step;
    for (unsigned i = 0; i < parts; i++) {
        unsigned start = i * step;
        unsigned entries = manifest.count - start;
        if (entries > step) entries = step;
        size_t len = entries * item_size;
        char key[32];
        make_key(key, sizeof(key), prefix, manifest.slot, i);
        struct fdb_blob blob;
        if (blob_size(key) != len ||
            fdb_kv_get_blob(&g_kvdb, key,
                fdb_blob_make(&blob, (uint8_t *)items + start * item_size,
                              len)) != len) {
            ESP_LOGE(TAG, "missing index part %s", key);
            return false;
        }
    }
    *count = manifest.count;
    return true;
}

bool anki_blob_save(const char *legacy_key, const char *prefix,
                    size_t item_size, uint16_t capacity, const void *items,
                    uint16_t count)
{
    unsigned step = items_per_part(item_size);
    if (!legacy_key || !prefix || !step || item_size > UINT16_MAX ||
        count > capacity ||
        (count && !items)) return false;
    BlobManifest previous;
    bool exists;
    if (!read_manifest(prefix, item_size, capacity, &previous, &exists)) return false;
    unsigned slot = exists ? 1U - previous.slot : 0U;
    unsigned parts = (count + step - 1) / step;
    for (unsigned i = 0; i < parts; i++) {
#ifdef ESP_PLATFORM
        if ((i & 3U) == 0) vTaskDelay(1);
#endif
        unsigned start = i * step;
        unsigned entries = count - start;
        if (entries > step) entries = step;
        char key[32];
        make_key(key, sizeof(key), prefix, (int)slot, i);
        struct fdb_blob blob;
        if (fdb_kv_set_blob(&g_kvdb, key,
                fdb_blob_make(&blob, (const uint8_t *)items + start * item_size,
                              entries * item_size)) != FDB_NO_ERR) return false;
    }
    BlobManifest next = {
        .magic = BLOB_MAGIC,
        .version = BLOB_VERSION,
        .count = count,
        .item_size = (uint16_t)item_size,
        .slot = (uint8_t)slot,
    };
    char key[32];
    make_key(key, sizeof(key), prefix, -1, 0);
    struct fdb_blob blob;
    if (fdb_kv_set_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, &next, sizeof(next))) != FDB_NO_ERR) {
        return false;
    }
    delete_key(legacy_key);
    if (exists) delete_slot(prefix, item_size, previous.count, previous.slot);
    return true;
}

bool anki_blob_delete(const char *legacy_key, const char *prefix,
                      size_t item_size, uint16_t capacity)
{
    if (!legacy_key || !prefix || item_size > UINT16_MAX ||
        !items_per_part(item_size)) return false;
    char key[32];
    make_key(key, sizeof(key), prefix, -1, 0);
    bool ok = delete_key(key);
    if (!delete_slot(prefix, item_size, capacity, 0)) ok = false;
    if (!delete_slot(prefix, item_size, capacity, 1)) ok = false;
    if (!delete_key(legacy_key)) ok = false;
    return ok;
}
