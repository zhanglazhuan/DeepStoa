#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flash_control.h"
#include "source_sync.h"
#include "storage.h"
#include "trash.h"
#include "time_service.h"

static const char *TAG = "anki_trash";

#define TRASH_KEY "anki_trash"
#define TRASH_MAX_ENTRIES ANKI_MAX_DECK_CAPACITY
#define TRASH_RETENTION_SECONDS (ANKI_TRASH_RETENTION_DAYS * UINT32_C(86400))
#define TRASH_DIRECTORY_SCAN_MAX 1024U

typedef struct {
    char path[sizeof(((AnkiDeck *)0)->sd_file_path)];
    uint64_t hash;
    uint32_t size;
    bool used;
} ActiveSource;

static bool fingerprint_file(const char *path, uint64_t *hash, uint32_t *size)
{
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0 ||
        (uint64_t)info.st_size > UINT32_MAX) return false;
    if ((uint64_t)info.st_size > ANKI_MAX_SOURCE_BYTES) {
        *hash = 0;
        *size = (uint32_t)info.st_size;
        return true;
    }
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    uint64_t value = UINT64_C(14695981039346656037);
    uint64_t total = 0;
    uint8_t buffer[512];
    size_t read;
    unsigned chunks = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (total + read > UINT32_MAX) {
            fclose(file);
            return false;
        }
        total += read;
        for (size_t i = 0; i < read; i++) {
            value ^= buffer[i];
            value *= UINT64_C(1099511628211);
        }
        if ((++chunks & 0x7FU) == 0) vTaskDelay(1);
    }
    bool ok = !ferror(file);
    fclose(file);
    if (!ok) return false;
    *hash = value;
    *size = (uint32_t)total;
    return true;
}

static AnkiTrashEntry *load_entries(uint16_t *count, bool *ok)
{
    *ok = false;
    *count = 0;
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, TRASH_KEY, fdb_blob_make(&blob, NULL, 0));
    size_t len = blob.saved.len;
    if (len == 0) {
        *ok = true;
        return NULL;
    }
    if (len % sizeof(AnkiTrashEntry) != 0 ||
        len > sizeof(AnkiTrashEntry) * TRASH_MAX_ENTRIES) {
        ESP_LOGE(TAG, "invalid recycle-bin metadata length: %u", (unsigned)len);
        return NULL;
    }

    AnkiTrashEntry *entries = malloc(len);
    if (!entries) return NULL;
    if (fdb_kv_get_blob(&g_kvdb, TRASH_KEY,
                        fdb_blob_make(&blob, entries, len)) != len) {
        free(entries);
        return NULL;
    }
    *count = (uint16_t)(len / sizeof(*entries));
    *ok = true;
    return entries;
}

static bool save_entries(const AnkiTrashEntry *entries, uint16_t count)
{
    if (count == 0) return fdb_kv_del(&g_kvdb, TRASH_KEY) == FDB_NO_ERR;
    struct fdb_blob blob;
    return fdb_kv_set_blob(&g_kvdb, TRASH_KEY,
                           fdb_blob_make(&blob, entries,
                                         count * sizeof(*entries))) == FDB_NO_ERR;
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) return true;
    ESP_LOGE(TAG, "mkdir failed for %s: errno=%d", path, errno);
    return false;
}

static bool is_in_tree(const AnkiModel *model, int candidate, int root)
{
    if (candidate == root) return true;
    uint32_t parent_id = model->decks[candidate].parent_id;
    for (int depth = 0; depth < ANKI_MAX_DECK_DEPTH + 2 && parent_id != UINT32_MAX; depth++) {
        bool found = false;
        for (int i = 0; i < model->deck_count; i++) {
            if (model->decks[i].deck_id == parent_id) {
                if (i == root) return true;
                parent_id = model->decks[i].parent_id;
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    return false;
}

static uint32_t current_valid_time(void)
{
    if (time_service_get_state() != TIME_STATE_TRUSTED) return 0;
    uint32_t now = 0;
    return time_service_now(&now) ? now : 0;
}

static void remove_group_directory(const AnkiTrashEntry *entries, uint16_t count,
                                   uint32_t group_id)
{
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id != group_id || !entries[i].trash_path[0]) continue;
        char path[sizeof(entries[i].trash_path)];
        snprintf(path, sizeof(path), "%s", entries[i].trash_path);
        char *slash = strrchr(path, '/');
        if (slash) {
            *slash = '\0';
            rmdir(path);
        }
        return;
    }
}

static bool delete_key_if_present(const char *key)
{
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    return blob.saved.len == 0 || fdb_kv_del(&g_kvdb, key) == FDB_NO_ERR;
}

static bool delete_card_data(uint32_t deck_id)
{
    char key[32];
    snprintf(key, sizeof(key), "anki_cards_%u", (unsigned)deck_id);

    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    size_t len = blob.saved.len;
    if (len > 0 && len % sizeof(CardIndex) == 0 &&
        len <= sizeof(CardIndex) * ANKI_MAX_CARDS_PER_DECK) {
        CardIndex *cards = malloc(len);
        if (!cards) return false;
        if (fdb_kv_get_blob(&g_kvdb, key,
                            fdb_blob_make(&blob, cards, len)) != len) {
            free(cards);
            return false;
        }
        size_t count = len / sizeof(*cards);
        for (size_t i = 0; i < count; i++) {
            char payload_key[32];
            snprintf(payload_key, sizeof(payload_key), "anki_payload_%u",
                     (unsigned)cards[i].card_id);
            if (!delete_key_if_present(payload_key) ||
                !anki_storage_delete_schedule(cards[i].card_id)) {
                free(cards);
                return false;
            }
        }
        free(cards);
    }

    if (!delete_key_if_present(key)) return false;
    snprintf(key, sizeof(key), "anki_src_%u", (unsigned)deck_id);
    return delete_key_if_present(key);
}

bool anki_trash_recycle_deck(AnkiModel *model, int deck_idx)
{
    if (!model || deck_idx < 0 || deck_idx >= model->deck_count) return false;

    bool selected[ANKI_MAX_DECK_CAPACITY] = {0};
    uint16_t selected_count = 0;
    for (int i = 0; i < model->deck_count; i++) {
        selected[i] = is_in_tree(model, i, deck_idx);
        if (selected[i]) selected_count++;
    }

    uint16_t old_count = 0;
    bool entries_ok;
    AnkiTrashEntry *old_entries = load_entries(&old_count, &entries_ok);
    if (!entries_ok) return false;
    if (old_count + selected_count > TRASH_MAX_ENTRIES) {
        free(old_entries);
        ESP_LOGE(TAG, "recycle bin metadata is full");
        return false;
    }

    AnkiTrashEntry *next = calloc(old_count + selected_count, sizeof(*next));
    if (!next) {
        free(old_entries);
        return false;
    }
    if (old_count) memcpy(next, old_entries, old_count * sizeof(*next));

    uint32_t deleted_at = current_valid_time();
    uint32_t group_id = model->decks[deck_idx].deck_id;
    char group_dir[96];
    snprintf(group_dir, sizeof(group_dir), "%s/%u_%u", ANKI_TRASH_ROOT,
             (unsigned)deleted_at, (unsigned)group_id);

    bool needs_directory = false;
    for (int i = 0; i < model->deck_count; i++) {
        if (selected[i] && model->decks[i].sd_file_path[0]) needs_directory = true;
    }
    if (needs_directory &&
        (!ensure_directory("/sdcard/anki") ||
         !ensure_directory(ANKI_TRASH_ROOT) ||
         mkdir(group_dir, 0775) != 0)) {
        ESP_LOGE(TAG, "cannot create recycle directory %s: errno=%d", group_dir, errno);
        free(old_entries);
        free(next);
        return false;
    }

    uint16_t write = old_count;
    for (int i = 0; i < model->deck_count; i++) {
        if (!selected[i]) continue;
        AnkiTrashEntry *entry = &next[write++];
        entry->group_id = group_id;
        entry->deleted_at = deleted_at;
        entry->deck = model->decks[i];

        if (!entry->deck.sd_file_path[0]) continue;
        if (!fingerprint_file(entry->deck.sd_file_path,
                              &entry->source_hash, &entry->source_size)) {
            ESP_LOGE(TAG, "cannot fingerprint %s", entry->deck.sd_file_path);
            rmdir(group_dir);
            free(old_entries);
            free(next);
            return false;
        }
        const char *base = strrchr(entry->deck.sd_file_path, '/');
        base = base ? base + 1 : entry->deck.sd_file_path;
        int n = snprintf(entry->trash_path, sizeof(entry->trash_path),
                         "%s/%u_%s", group_dir,
                         (unsigned)entry->deck.deck_id, base);
        if (n <= 0 || (size_t)n >= sizeof(entry->trash_path)) {
            rmdir(group_dir);
            free(old_entries);
            free(next);
            return false;
        }
    }

    size_t snapshot_size = model->deck_count * sizeof(*model->decks);
    AnkiDeck *deck_snapshot = malloc(snapshot_size);
    if (!deck_snapshot) {
        rmdir(group_dir);
        free(old_entries);
        free(next);
        return false;
    }
    memcpy(deck_snapshot, model->decks, snapshot_size);
    uint8_t previous_deck_count = model->deck_count;
    int previous_active_deck = model->active_deck_idx;
    int previous_active_card = model->active_card_idx;

    /* Persist intent before moving files so boot-time recovery can roll back
     * an interrupted recycle operation. */
    if (!save_entries(next, write)) {
        rmdir(group_dir);
        free(deck_snapshot);
        free(old_entries);
        free(next);
        return false;
    }

    for (uint16_t r = old_count; r < write; r++) {
        if (!next[r].trash_path[0]) continue;
        if (rename(next[r].deck.sd_file_path, next[r].trash_path) != 0) {
            ESP_LOGE(TAG, "cannot move %s to recycle bin: errno=%d",
                     next[r].deck.sd_file_path, errno);
            for (uint16_t undo = old_count; undo < r; undo++) {
                if (next[undo].trash_path[0]) {
                    rename(next[undo].trash_path,
                           next[undo].deck.sd_file_path);
                }
            }
            save_entries(old_entries, old_count);
            rmdir(group_dir);
            free(deck_snapshot);
            free(old_entries);
            free(next);
            return false;
        }
    }

    if (!anki_model_delete_deck(model, deck_idx)) {
        save_entries(old_entries, old_count);
        for (uint16_t r = old_count; r < write; r++) {
            if (next[r].trash_path[0]) {
                rename(next[r].trash_path, next[r].deck.sd_file_path);
            }
        }
        rmdir(group_dir);
        free(deck_snapshot);
        free(old_entries);
        free(next);
        return false;
    }
    model->active_deck_idx = -1;
    model->active_card_idx = 0;
    if (!anki_storage_sync_decks(model) || !anki_storage_sync_meta(model)) {
        memcpy(model->decks, deck_snapshot, snapshot_size);
        model->deck_count = previous_deck_count;
        model->active_deck_idx = previous_active_deck;
        model->active_card_idx = previous_active_card;
        save_entries(old_entries, old_count);
        for (uint16_t r = old_count; r < write; r++) {
            if (next[r].trash_path[0]) {
                rename(next[r].trash_path, next[r].deck.sd_file_path);
            }
        }
        anki_storage_sync_decks(model);
        anki_storage_sync_meta(model);
        rmdir(group_dir);
        free(deck_snapshot);
        free(old_entries);
        free(next);
        return false;
    }

    ESP_LOGI(TAG, "moved deck group %u (%u decks) to recycle bin",
             (unsigned)group_id, (unsigned)selected_count);
    free(deck_snapshot);
    free(old_entries);
    free(next);
    return true;
}

size_t anki_trash_group_count(void)
{
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return 0;
    size_t groups = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].deck.deck_id == entries[i].group_id) groups++;
    }
    free(entries);
    return groups;
}

bool anki_trash_get_group(size_t index, AnkiTrashEntry *entry)
{
    if (!entry) return false;
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return false;
    size_t group_index = 0;
    bool found = false;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].deck.deck_id != entries[i].group_id) continue;
        if (group_index++ == index) {
            *entry = entries[i];
            found = true;
            break;
        }
    }
    free(entries);
    return found;
}

static bool make_restored_path(const char *original, char *destination, size_t size)
{
    const char *slash = strrchr(original, '/');
    const char *dot = strrchr(original, '.');
    if (dot && slash && dot < slash) dot = NULL;
    size_t stem_len = dot ? (size_t)(dot - original) : strlen(original);
    const char *extension = dot ? dot : "";

    for (unsigned attempt = 0; attempt < 100; attempt++) {
        int n = attempt == 0
            ? snprintf(destination, size, "%.*s_restored%s",
                       (int)stem_len, original, extension)
            : snprintf(destination, size, "%.*s_restored_%u%s",
                       (int)stem_len, original, attempt + 1, extension);
        if (n <= 0 || n >= size) return false;
        struct stat st;
        if (stat(destination, &st) != 0 && errno == ENOENT) return true;
    }
    return false;
}

static void update_deck_name_from_path(AnkiDeck *deck)
{
    const char *base = strrchr(deck->sd_file_path, '/');
    base = base ? base + 1 : deck->sd_file_path;
    snprintf(deck->name, sizeof(deck->name), "%s", base);
    char *extension = strrchr(deck->name, '.');
    if (extension) *extension = '\0';
}

static bool restore_group(AnkiModel *model, uint32_t group_id, bool as_copy)
{
    if (!model) return false;
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return false;
    uint16_t group_count = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id == group_id) group_count++;
    }
    if (group_count == 0 || model->deck_count + group_count > ANKI_MAX_DECK_CAPACITY) {
        free(entries);
        return false;
    }

    AnkiTrashEntry *group = calloc(group_count, sizeof(*group));
    if (!group) {
        free(entries);
        return false;
    }
    uint16_t group_write = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id == group_id) group[group_write++] = entries[i];
    }

    for (uint16_t i = 0; i < group_count; i++) {
        for (int active = 0; active < model->deck_count; active++) {
            if (model->decks[active].deck_id == group[i].deck.deck_id) {
                ESP_LOGE(TAG, "deck id %u is already active",
                         (unsigned)group[i].deck.deck_id);
                free(group);
                free(entries);
                return false;
            }
        }
    }

    uint16_t remaining_count = count - group_count;
    AnkiTrashEntry *remaining = remaining_count
        ? calloc(remaining_count, sizeof(*remaining)) : NULL;
    if (remaining_count && !remaining) {
        free(group);
        free(entries);
        return false;
    }
    uint16_t remaining_write = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id != group_id) {
            remaining[remaining_write++] = entries[i];
        }
    }

    for (uint16_t i = 0; i < group_count; i++) {
        if (!group[i].trash_path[0]) continue;
        if (as_copy) {
            char restored[sizeof(group[i].deck.sd_file_path)];
            if (!make_restored_path(group[i].deck.sd_file_path,
                                    restored, sizeof(restored))) {
                free(remaining);
                free(group);
                free(entries);
                return false;
            }
            snprintf(group[i].deck.sd_file_path,
                     sizeof(group[i].deck.sd_file_path), "%s", restored);
            update_deck_name_from_path(&group[i].deck);
        } else {
            struct stat st;
            if (stat(group[i].deck.sd_file_path, &st) == 0) {
                ESP_LOGE(TAG, "restore target already exists: %s",
                         group[i].deck.sd_file_path);
                free(remaining);
                free(group);
                free(entries);
                return false;
            }
        }
    }

    for (uint16_t i = 0; i < group_count; i++) {
        if (!group[i].trash_path[0]) continue;
        if (rename(group[i].trash_path, group[i].deck.sd_file_path) != 0) {
            ESP_LOGE(TAG, "restore failed for %s: errno=%d", group[i].trash_path, errno);
            for (uint16_t r = 0; r < i; r++) {
                if (group[r].trash_path[0]) {
                    rename(group[r].deck.sd_file_path, group[r].trash_path);
                }
            }
            free(remaining);
            free(group);
            free(entries);
            return false;
        }
    }

    uint8_t previous_deck_count = model->deck_count;
    for (uint16_t i = 0; i < group_count; i++) {
        model->decks[model->deck_count++] = group[i].deck;
    }
    if (!anki_storage_sync_decks(model)) {
        model->deck_count = previous_deck_count;
        save_entries(entries, count);
        for (uint16_t i = 0; i < group_count; i++) {
            if (group[i].trash_path[0]) {
                rename(group[i].deck.sd_file_path, group[i].trash_path);
            }
        }
        free(remaining);
        free(group);
        free(entries);
        return false;
    }
    if (!save_entries(remaining, remaining_count)) {
        model->deck_count = previous_deck_count;
        anki_storage_sync_decks(model);
        for (uint16_t i = 0; i < group_count; i++) {
            if (group[i].trash_path[0]) {
                rename(group[i].deck.sd_file_path, group[i].trash_path);
            }
        }
        free(remaining);
        free(group);
        free(entries);
        return false;
    }
    remove_group_directory(group, group_count, group_id);
    ESP_LOGI(TAG, "restored deck group %u", (unsigned)group_id);
    free(remaining);
    free(group);
    free(entries);
    return true;
}

bool anki_trash_restore_group(AnkiModel *model, uint32_t group_id)
{
    return restore_group(model, group_id, false);
}

bool anki_trash_restore_group_as_copy(AnkiModel *model, uint32_t group_id)
{
    return restore_group(model, group_id, true);
}

bool anki_trash_delete_group_permanently(uint32_t group_id)
{
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return false;
    bool found = false;

    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id != group_id) continue;
        found = true;
        if (entries[i].trash_path[0] && unlink(entries[i].trash_path) != 0 &&
            errno != ENOENT) {
            ESP_LOGE(TAG, "cannot delete %s: errno=%d", entries[i].trash_path, errno);
            free(entries);
            return false;
        }
    }
    if (!found) {
        free(entries);
        return false;
    }

    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id == group_id &&
            !delete_card_data(entries[i].deck.deck_id)) {
            free(entries);
            return false;
        }
    }

    remove_group_directory(entries, count, group_id);
    uint16_t write = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].group_id != group_id) entries[write++] = entries[i];
    }
    if (write == count) {
        free(entries);
        return false;
    }
    bool saved = save_entries(entries, write);
    free(entries);
    return saved;
}

bool anki_trash_delete_active_permanently(AnkiModel *model, int deck_idx)
{
    if (!model || deck_idx < 0 || deck_idx >= model->deck_count) return false;
    uint32_t group_id = model->decks[deck_idx].deck_id;
    if (!anki_trash_recycle_deck(model, deck_idx)) return false;
    return anki_trash_delete_group_permanently(group_id);
}

bool anki_trash_factory_reset(void)
{
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) {
        return delete_key_if_present(TRASH_KEY);
    }

    bool ok = true;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].trash_path[0] &&
            unlink(entries[i].trash_path) != 0 && errno != ENOENT) {
            ok = false;
        }
        if (!delete_card_data(entries[i].deck.deck_id)) ok = false;
    }
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].deck.deck_id == entries[i].group_id) {
            remove_group_directory(entries, count, entries[i].group_id);
        }
    }
    free(entries);
    if (!delete_key_if_present(TRASH_KEY)) ok = false;
    return ok;
}

static uint16_t scan_active_sources(const AnkiModel *model,
                                    ActiveSource sources[ANKI_MAX_DECK_CAPACITY])
{
    DIR *directory = opendir(ANKI_SOURCE_ROOT);
    if (!directory) return 0;

    uint16_t count = 0;
    unsigned inspected = 0;
    struct dirent *item;
    while (count < ANKI_MAX_DECK_CAPACITY && (item = readdir(directory)) != NULL) {
        if (++inspected > TRASH_DIRECTORY_SCAN_MAX) break;
        const char *extension = strrchr(item->d_name, '.');
        if (!extension || strcasecmp(extension, ".txt") != 0) continue;

        ActiveSource *source = &sources[count];
        int n = snprintf(source->path, sizeof(source->path), "%s/%s",
                         ANKI_SOURCE_ROOT, item->d_name);
        if (n <= 0 || (size_t)n >= sizeof(source->path) ||
            !fingerprint_file(source->path, &source->hash, &source->size)) {
            continue;
        }
        for (int i = 0; i < model->deck_count; i++) {
            if (strcmp(model->decks[i].sd_file_path, source->path) == 0) {
                source->used = true;
                break;
            }
        }
        count++;
    }
    closedir(directory);
    return count;
}

static int find_external_source(const AnkiTrashEntry *entry,
                                ActiveSource *sources, uint16_t source_count)
{
    int exact_path = -1;
    int match = -1;
    unsigned matches = 0;
    for (uint16_t i = 0; i < source_count; i++) {
        if (sources[i].used || sources[i].hash != entry->source_hash ||
            sources[i].size != entry->source_size) continue;
        if (strcmp(sources[i].path, entry->deck.sd_file_path) == 0) exact_path = i;
        match = i;
        matches++;
    }
    if (exact_path >= 0) return exact_path;
    return matches == 1 ? match : -1;
}

static bool deck_id_is_active(const AnkiModel *model, uint32_t deck_id)
{
    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].deck_id == deck_id) return true;
    }
    return false;
}

static bool reconcile_incomplete_recycles(AnkiModel *model)
{
    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return false;

    bool changed = false;
    bool remove_entry[TRASH_MAX_ENTRIES] = {0};
    for (uint16_t root = 0; root < count; root++) {
        if (entries[root].deck.deck_id != entries[root].group_id ||
            !deck_id_is_active(model, entries[root].group_id)) continue;

        uint32_t group_id = entries[root].group_id;
        bool recoverable = true;
        for (uint16_t i = 0; i < count; i++) {
            if (entries[i].group_id != group_id) continue;
            if (!deck_id_is_active(model, entries[i].deck.deck_id)) {
                recoverable = false;
                break;
            }
            if (!entries[i].trash_path[0]) continue;
            struct stat original;
            struct stat recycled;
            bool original_exists = stat(entries[i].deck.sd_file_path,
                                        &original) == 0;
            bool recycled_exists = stat(entries[i].trash_path, &recycled) == 0;
            if (original_exists == recycled_exists) {
                recoverable = false;
                break;
            }
            if (recycled_exists &&
                rename(entries[i].trash_path,
                       entries[i].deck.sd_file_path) != 0) {
                recoverable = false;
                break;
            }
        }
        if (!recoverable) {
            ESP_LOGE(TAG, "incomplete recycle group %u needs recovery",
                     (unsigned)group_id);
            free(entries);
            return false;
        }
        for (uint16_t i = 0; i < count; i++) {
            if (entries[i].group_id == group_id) remove_entry[i] = true;
        }
        remove_group_directory(entries, count, group_id);
        changed = true;
    }

    if (changed) {
        uint16_t write = 0;
        for (uint16_t i = 0; i < count; i++) {
            if (!remove_entry[i]) entries[write++] = entries[i];
        }
        if (!save_entries(entries, write)) {
            free(entries);
            return false;
        }
    }
    free(entries);
    return true;
}

void anki_trash_reconcile_external_restores(AnkiModel *model)
{
    if (!model) return;
    if (!reconcile_incomplete_recycles(model)) {
        model->storage_fault = true;
        return;
    }
    ActiveSource *sources = calloc(ANKI_MAX_DECK_CAPACITY, sizeof(*sources));
    if (!sources) return;
    uint16_t source_count = scan_active_sources(model, sources);
    if (source_count == 0) {
        free(sources);
        return;
    }

    bool restored;
    do {
        restored = false;
        uint16_t count = 0;
        bool entries_ok;
        AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
        if (!entries_ok) break;
        for (uint16_t root = 0; root < count && !restored; root++) {
            if (entries[root].deck.deck_id != entries[root].group_id) continue;
            uint32_t group_id = entries[root].group_id;
            uint16_t group_count = 0;
            uint16_t source_entries = 0;
            uint16_t missing_source_entries = 0;
            bool eligible = true;

            for (uint16_t i = 0; i < count; i++) {
                if (entries[i].group_id != group_id) continue;
                group_count++;
                for (int active = 0; active < model->deck_count; active++) {
                    if (model->decks[active].deck_id == entries[i].deck.deck_id) {
                        eligible = false;
                    }
                }
                if (!entries[i].trash_path[0]) continue;
                source_entries++;
                struct stat st;
                if (stat(entries[i].trash_path, &st) == 0) {
                    if (stat(entries[i].deck.sd_file_path, &st) == 0) {
                        eligible = false;
                    }
                    continue;
                }
                if (errno != ENOENT || entries[i].source_hash == 0) {
                    eligible = false;
                } else {
                    missing_source_entries++;
                }
            }
            if (!eligible || source_entries == 0 || missing_source_entries == 0 ||
                model->deck_count + group_count > ANKI_MAX_DECK_CAPACITY) continue;

            AnkiTrashEntry *group = calloc(group_count, sizeof(*group));
            int *matches = calloc(source_entries, sizeof(*matches));
            if (!group || !matches) {
                free(group);
                free(matches);
                break;
            }

            uint16_t group_write = 0;
            uint16_t source_write = 0;
            for (uint16_t i = 0; i < count; i++) {
                if (entries[i].group_id != group_id) continue;
                group[group_write] = entries[i];
                if (group[group_write].trash_path[0]) {
                    struct stat trash_stat;
                    int source = -1;
                    if (stat(group[group_write].trash_path, &trash_stat) != 0) {
                        source = find_external_source(&group[group_write],
                                                      sources, source_count);
                        if (source < 0) eligible = false;
                    }
                    if (source >= 0) {
                        for (uint16_t used = 0; used < source_write; used++) {
                            if (matches[used] == source) eligible = false;
                        }
                    }
                    matches[source_write] = source;
                    source_write++;
                }
                group_write++;
            }
            if (!eligible) {
                free(group);
                free(matches);
                continue;
            }

            source_write = 0;
            for (uint16_t i = 0; i < group_count; i++) {
                if (!group[i].trash_path[0]) continue;
                int source = matches[source_write++];
                if (source >= 0) {
                    snprintf(group[i].deck.sd_file_path,
                             sizeof(group[i].deck.sd_file_path), "%s",
                             sources[source].path);
                }
            }

            source_write = 0;
            uint16_t moved_from_trash = 0;
            for (uint16_t i = 0; i < group_count; i++) {
                if (!group[i].trash_path[0]) continue;
                int source = matches[source_write++];
                if (source >= 0) continue;
                if (rename(group[i].trash_path,
                           group[i].deck.sd_file_path) != 0) {
                    eligible = false;
                    break;
                }
                moved_from_trash++;
            }
            if (!eligible) {
                source_write = 0;
                uint16_t moved_seen = 0;
                for (uint16_t i = 0; i < group_count; i++) {
                    if (!group[i].trash_path[0]) continue;
                    int source = matches[source_write++];
                    if (source < 0 && moved_seen++ < moved_from_trash) {
                        rename(group[i].deck.sd_file_path, group[i].trash_path);
                    }
                }
                free(group);
                free(matches);
                continue;
            }

            uint16_t remaining_count = count - group_count;
            AnkiTrashEntry *remaining = remaining_count
                ? calloc(remaining_count, sizeof(*remaining)) : NULL;
            if (remaining_count && !remaining) {
                free(group);
                free(matches);
                break;
            }
            uint16_t write = 0;
            for (uint16_t i = 0; i < count; i++) {
                if (entries[i].group_id != group_id) {
                    remaining[write++] = entries[i];
                }
            }

            uint8_t previous_deck_count = model->deck_count;
            for (uint16_t i = 0; i < group_count; i++) {
                model->decks[model->deck_count++] = group[i].deck;
            }
            if (!anki_storage_sync_decks(model)) {
                model->deck_count = previous_deck_count;
                source_write = 0;
                for (uint16_t i = 0; i < group_count; i++) {
                    if (!group[i].trash_path[0]) continue;
                    if (matches[source_write++] < 0) {
                        rename(group[i].deck.sd_file_path, group[i].trash_path);
                    }
                }
                free(remaining);
                free(group);
                free(matches);
                break;
            }
            if (!save_entries(remaining, remaining_count)) {
                model->deck_count = previous_deck_count;
                anki_storage_sync_decks(model);
                source_write = 0;
                for (uint16_t i = 0; i < group_count; i++) {
                    if (!group[i].trash_path[0]) continue;
                    if (matches[source_write++] < 0) {
                        rename(group[i].deck.sd_file_path, group[i].trash_path);
                    }
                }
                free(remaining);
                free(group);
                free(matches);
                break;
            }
            source_write = 0;
            for (uint16_t i = 0; i < group_count; i++) {
                if (group[i].trash_path[0]) {
                    int source = matches[source_write++];
                    if (source >= 0) sources[source].used = true;
                }
            }
            remove_group_directory(group, group_count, group_id);
            ESP_LOGI(TAG, "recognized external restore for deck group %u",
                     (unsigned)group_id);
            restored = true;
            free(remaining);
            free(group);
            free(matches);
        }
        free(entries);
    } while (restored);
    free(sources);
}

void anki_trash_cleanup_expired(AnkiModel *model)
{
    (void)model;
    uint32_t now = current_valid_time();
    if (now == 0) return;

    uint16_t count = 0;
    bool entries_ok;
    AnkiTrashEntry *entries = load_entries(&count, &entries_ok);
    if (!entries_ok) return;
    bool timestamps_changed = false;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].deleted_at == 0) {
            entries[i].deleted_at = now;
            timestamps_changed = true;
        }
    }
    if (timestamps_changed && !save_entries(entries, count)) {
        free(entries);
        return;
    }

    uint32_t expired[ANKI_MAX_DECK_CAPACITY];
    uint16_t expired_count = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].deck.deck_id != entries[i].group_id ||
            now < entries[i].deleted_at ||
            now - entries[i].deleted_at < TRASH_RETENTION_SECONDS) continue;
        expired[expired_count++] = entries[i].group_id;
    }
    free(entries);

    for (uint16_t i = 0; i < expired_count; i++) {
        if (!anki_trash_delete_group_permanently(expired[i])) {
            ESP_LOGW(TAG, "could not purge expired group %u", (unsigned)expired[i]);
        }
    }
}
