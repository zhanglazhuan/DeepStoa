#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flash_control.h"
#include "source_sync.h"
#include "storage.h"

static const char *TAG = "anki_source";

#define SOURCE_LINE_MAX 1100
#define SOURCE_KEY_MAX  32
#define SOURCE_DIRECTORY_SCAN_MAX 1024U

typedef struct {
    uint64_t line_hash;
    uint64_t front_hash;
    uint32_t file_offset;
    uint16_t file_len;
} ParsedCard;

typedef struct {
    uint32_t card_id;
    uint64_t line_hash;
    uint64_t front_hash;
} SourceCard;

static uint64_t hash_bytes(uint64_t hash, const char *text)
{
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t content_hash(const char *front, const char *back)
{
    uint64_t hash = hash_bytes(UINT64_C(14695981039346656037), front);
    hash ^= UINT8_C(0x1f);
    hash *= UINT64_C(1099511628211);
    return hash_bytes(hash, back);
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) text++;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static bool valid_utf8(const char *text)
{
    const uint8_t *p = (const uint8_t *)text;
    while (*p) {
        if (*p < 0x80) {
            if (*p < 0x20 && *p != '\t') return false;
            p++;
        } else if (*p >= 0xC2 && *p <= 0xDF &&
                   p[1] >= 0x80 && p[1] <= 0xBF) {
            p += 2;
        } else if (*p == 0xE0 && p[1] >= 0xA0 && p[1] <= 0xBF &&
                   p[2] >= 0x80 && p[2] <= 0xBF) {
            p += 3;
        } else if (((*p >= 0xE1 && *p <= 0xEC) ||
                    (*p >= 0xEE && *p <= 0xEF)) &&
                   p[1] >= 0x80 && p[1] <= 0xBF &&
                   p[2] >= 0x80 && p[2] <= 0xBF) {
            p += 3;
        } else if (*p == 0xED && p[1] >= 0x80 && p[1] <= 0x9F &&
                   p[2] >= 0x80 && p[2] <= 0xBF) {
            p += 3;
        } else if (*p == 0xF0 && p[1] >= 0x90 && p[1] <= 0xBF &&
                   p[2] >= 0x80 && p[2] <= 0xBF &&
                   p[3] >= 0x80 && p[3] <= 0xBF) {
            p += 4;
        } else if (*p >= 0xF1 && *p <= 0xF3 &&
                   p[1] >= 0x80 && p[1] <= 0xBF &&
                   p[2] >= 0x80 && p[2] <= 0xBF &&
                   p[3] >= 0x80 && p[3] <= 0xBF) {
            p += 4;
        } else if (*p == 0xF4 && p[1] >= 0x80 && p[1] <= 0x8F &&
                   p[2] >= 0x80 && p[2] <= 0xBF &&
                   p[3] >= 0x80 && p[3] <= 0xBF) {
            p += 4;
        } else {
            return false;
        }
    }
    return true;
}

typedef enum {
    PARSE_LINE_IGNORED,
    PARSE_LINE_CARD,
    PARSE_LINE_INVALID,
} ParseLineResult;

static ParseLineResult parse_fields(char *line, char **front, char **back)
{
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (len >= 3 && (uint8_t)line[0] == 0xEF && (uint8_t)line[1] == 0xBB &&
        (uint8_t)line[2] == 0xBF) {
        memmove(line, line + 3, strlen(line + 3) + 1);
    }

    char *first = trim(line);
    if (!first[0] || first[0] == '#') return PARSE_LINE_IGNORED;
    char *separator = strchr(first, '|');
    if (!separator) return PARSE_LINE_INVALID;
    *separator = '\0';
    *front = trim(first);
    *back = trim(separator + 1);
    return (*front)[0] && (*back)[0] &&
           valid_utf8(*front) && valid_utf8(*back) &&
           strlen(*front) < sizeof(((CardPayload *)0)->front) &&
           strlen(*back) < sizeof(((CardPayload *)0)->back)
        ? PARSE_LINE_CARD : PARSE_LINE_INVALID;
}

static AnkiSyncError parse_source_file(const char *path, ParsedCard *cards,
                                       uint16_t *count, uint16_t *skipped)
{
    struct stat info;
    if (stat(path, &info) != 0) return ANKI_SYNC_ERR_IO;
    if (info.st_size < 0 || (uint64_t)info.st_size > ANKI_MAX_SOURCE_BYTES) {
        return ANKI_SYNC_ERR_TOO_LARGE;
    }

    FILE *file = fopen(path, "rb");
    if (!file) return ANKI_SYNC_ERR_IO;

    char line[SOURCE_LINE_MAX];
    *count = 0;
    *skipped = 0;
    uint32_t line_count = 0;
    uint32_t skipped_count = 0;
    AnkiSyncError error = ANKI_SYNC_OK;
    while (fgets(line, sizeof(line), file)) {
        if (++line_count > ANKI_MAX_SOURCE_LINES) {
            error = ANKI_SYNC_ERR_TOO_MANY_LINES;
            break;
        }
        if ((line_count & 0x7FU) == 0) vTaskDelay(1);
        long offset = ftell(file);
        size_t raw_len = strlen(line);
        long line_offset = offset >= 0 ? offset - (long)raw_len : -1;

        if (raw_len > 0 && line[raw_len - 1] != '\n' && !feof(file)) {
            char discard[256];
            while (fgets(discard, sizeof(discard), file)) {
                if (strchr(discard, '\n')) break;
            }
            skipped_count++;
            continue;
        }

        char *front;
        char *back;
        ParseLineResult line_result = parse_fields(line, &front, &back);
        if (line_result == PARSE_LINE_IGNORED) continue;
        if (line_result != PARSE_LINE_CARD || line_offset < 0 ||
            raw_len > UINT16_MAX || (uint64_t)line_offset > UINT32_MAX) {
            skipped_count++;
            continue;
        }

        if (*count >= ANKI_MAX_CARDS_PER_DECK) {
            error = ANKI_SYNC_ERR_TOO_MANY_CARDS;
            break;
        }

        ParsedCard *parsed = &cards[(*count)++];
        parsed->front_hash = hash_bytes(UINT64_C(14695981039346656037), front);
        parsed->line_hash = content_hash(front, back);
        parsed->file_offset = (uint32_t)line_offset;
        parsed->file_len = (uint16_t)raw_len;
    }

    if (error == ANKI_SYNC_OK && ferror(file)) error = ANKI_SYNC_ERR_IO;
    if (error == ANKI_SYNC_OK && *count == 0 && skipped_count > 0) {
        error = ANKI_SYNC_ERR_FORMAT;
    }
    *skipped = skipped_count > UINT16_MAX ? UINT16_MAX
                                          : (uint16_t)skipped_count;
    fclose(file);
    struct stat after;
    if (error == ANKI_SYNC_OK &&
        (stat(path, &after) != 0 || after.st_size != info.st_size ||
         after.st_mtime != info.st_mtime)) {
        error = ANKI_SYNC_ERR_IO;
    }
    return error;
}

static int find_deck_by_path(const AnkiModel *model, const char *path)
{
    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].sd_file_path[0] &&
            strcmp(model->decks[i].sd_file_path, path) == 0) return i;
    }
    return -1;
}

static void deck_name_from_path(const char *path, char *name, size_t size)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    strncpy(name, base, size - 1);
    name[size - 1] = '\0';
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
}

static void source_key(char *key, size_t size, uint32_t deck_id)
{
    snprintf(key, size, "anki_src_%u", (unsigned)deck_id);
}

static void cards_key(char *key, size_t size, uint32_t deck_id)
{
    snprintf(key, size, "anki_cards_%u", (unsigned)deck_id);
}

static bool load_blob(const char *key, size_t item_size, uint16_t max_count,
                      void **data, uint16_t *count)
{
    *data = NULL;
    *count = 0;
    struct fdb_blob blob;
    fdb_kv_get_blob(&g_kvdb, key, fdb_blob_make(&blob, NULL, 0));
    size_t len = blob.saved.len;
    if (len == 0) return true;
    if (len % item_size != 0 || len > item_size * max_count) {
        ESP_LOGE(TAG, "invalid blob %s length %u", key, (unsigned)len);
        return false;
    }
    *data = malloc(len);
    if (!*data) return false;
    if (fdb_kv_get_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, *data, len)) != len) {
        free(*data);
        *data = NULL;
        return false;
    }
    *count = (uint16_t)(len / item_size);
    return true;
}

static int find_exact(const ParsedCard *parsed, const SourceCard *old_sources,
                      const bool *matched, uint16_t old_count)
{
    for (uint16_t i = 0; i < old_count; i++) {
        if (!matched[i] && old_sources[i].line_hash == parsed->line_hash) return i;
    }
    return -1;
}

static int find_unique_front(const ParsedCard *parsed, const ParsedCard *all,
                             uint16_t parsed_count, const SourceCard *old_sources,
                             const bool *matched, uint16_t old_count)
{
    unsigned new_matches = 0;
    for (uint16_t i = 0; i < parsed_count; i++) {
        if (all[i].front_hash == parsed->front_hash) new_matches++;
    }
    if (new_matches != 1) return -1;

    int found = -1;
    for (uint16_t i = 0; i < old_count; i++) {
        if (!matched[i] && old_sources[i].front_hash == parsed->front_hash) {
            if (found >= 0) return -1;
            found = i;
        }
    }
    return found;
}

bool anki_source_sync_file(AnkiModel *model, const char *path, AnkiSyncResult *result)
{
    AnkiSyncResult local = {0};
    if (!result) result = &local;
    memset(result, 0, sizeof(*result));
    if (!model || model->storage_fault || !path ||
        strlen(path) >= sizeof(model->decks[0].sd_file_path)) {
        result->error = ANKI_SYNC_ERR_ARGUMENT;
        return false;
    }
    const char *extension = strrchr(path, '.');
    if (!extension || strcasecmp(extension, ".txt") != 0) {
        result->error = ANKI_SYNC_ERR_ARGUMENT;
        return false;
    }

    ParsedCard *parsed = calloc(ANKI_MAX_CARDS_PER_DECK, sizeof(*parsed));
    if (!parsed) {
        result->error = ANKI_SYNC_ERR_MEMORY;
        return false;
    }
    uint16_t parsed_count = 0;
    result->error = parse_source_file(path, parsed, &parsed_count,
                                      &result->skipped);
    if (result->error != ANKI_SYNC_OK) {
        free(parsed);
        ESP_LOGE(TAG, "cannot parse %s: %d", path, result->error);
        return false;
    }

    uint32_t previous_deck_generator = model->_id_generator;
    uint32_t previous_card_generator = model->_cards_id_generator;
    int deck_idx = find_deck_by_path(model, path);
    bool new_deck = deck_idx < 0;
    char name[sizeof(model->decks[0].name)];
    deck_name_from_path(path, name, sizeof(name));
    if (!name[0] || !valid_utf8(name)) {
        result->error = ANKI_SYNC_ERR_ARGUMENT;
        free(parsed);
        return false;
    }
    if (new_deck) {
        if (!anki_model_add_deck(model, name, -1)) {
            result->error = model->deck_count >= ANKI_MAX_DECK_CAPACITY
                ? ANKI_SYNC_ERR_STORAGE : ANKI_SYNC_ERR_ID_EXHAUSTED;
            free(parsed);
            return false;
        }
        deck_idx = model->deck_count - 1;
    }
    AnkiDeck *deck = &model->decks[deck_idx];
    AnkiDeck previous_deck = *deck;
    uint32_t deck_id = deck->deck_id;

    char src_key[SOURCE_KEY_MAX];
    char card_key[SOURCE_KEY_MAX];
    source_key(src_key, sizeof(src_key), deck_id);
    cards_key(card_key, sizeof(card_key), deck_id);

    uint16_t old_source_count = 0;
    uint16_t old_card_count = 0;
    SourceCard *old_sources = NULL;
    CardIndex *old_cards = NULL;
    bool *matched = NULL;
    CardIndex *next_cards = NULL;
    SourceCard *next_sources = NULL;
    uint32_t *purged_ids = NULL;
    if (!load_blob(src_key, sizeof(SourceCard), ANKI_MAX_CARDS_PER_DECK,
                   (void **)&old_sources, &old_source_count) ||
        !load_blob(card_key, sizeof(CardIndex), ANKI_MAX_CARDS_PER_DECK,
                   (void **)&old_cards, &old_card_count)) {
        result->error = ANKI_SYNC_ERR_STORAGE;
        goto fail;
    }
    if (old_source_count != old_card_count) {
        ESP_LOGE(TAG, "source/card index mismatch for deck %u", (unsigned)deck_id);
        goto fail;
    }
    for (uint16_t i = 0; i < old_source_count; i++) {
        if (old_sources[i].card_id != old_cards[i].card_id) {
            ESP_LOGE(TAG, "source/card identity mismatch for deck %u",
                     (unsigned)deck_id);
            result->error = ANKI_SYNC_ERR_STORAGE;
            goto fail;
        }
    }

    matched = calloc(old_source_count ? old_source_count : 1, sizeof(bool));
    next_cards = calloc(ANKI_MAX_CARDS_PER_DECK, sizeof(CardIndex));
    next_sources = calloc(ANKI_MAX_CARDS_PER_DECK, sizeof(SourceCard));
    purged_ids = calloc(old_source_count ? old_source_count : 1,
                        sizeof(*purged_ids));
    if (!matched || !next_cards || !next_sources || !purged_ids) {
        result->error = ANKI_SYNC_ERR_MEMORY;
        goto fail;
    }

    uint16_t next_count = 0;
    for (uint16_t i = 0; i < parsed_count; i++) {
        if ((i & 0x1FU) == 0x1FU) vTaskDelay(1);
        int old_idx = find_exact(&parsed[i], old_sources, matched, old_source_count);
        if (old_idx < 0) {
            old_idx = find_unique_front(&parsed[i], parsed, parsed_count,
                                        old_sources, matched, old_source_count);
        }

        CardIndex *card = &next_cards[next_count];
        SourceCard *source = &next_sources[next_count];
        if (old_idx >= 0) {
            *card = old_cards[old_idx];
            matched[old_idx] = true;
            card->status &= ANKI_CARD_STATUS_MASK;
            if (old_sources[old_idx].line_hash == parsed[i].line_hash) result->unchanged++;
            else result->updated++;
        } else {
            if (model->_cards_id_generator == 0 ||
                model->_cards_id_generator == UINT32_MAX) {
                result->error = ANKI_SYNC_ERR_ID_EXHAUSTED;
                goto fail;
            }
            card->card_id = model->_cards_id_generator++;
            card->deck_id = deck_id;
            card->status = ANKI_CARD_STATUS_NEW;
            result->added++;
        }
        card->deck_id = deck_id;
        card->file_offset = parsed[i].file_offset;
        card->file_len = parsed[i].file_len;
        source->card_id = card->card_id;
        source->line_hash = parsed[i].line_hash;
        source->front_hash = parsed[i].front_hash;
        next_count++;
    }

    for (uint16_t i = 0; i < old_source_count; i++) {
        if (matched[i]) continue;
        if (next_count >= ANKI_MAX_CARDS_PER_DECK) {
            purged_ids[result->purged++] = old_cards[i].card_id;
            continue;
        }
        next_cards[next_count] = old_cards[i];
        next_cards[next_count].status |= ANKI_CARD_FLAG_MISSING;
        next_sources[next_count] = old_sources[i];
        next_count++;
        result->missing++;
    }

    struct fdb_blob blob;
    size_t source_len = next_count * sizeof(SourceCard);
    size_t card_len = next_count * sizeof(CardIndex);
    fdb_err_t err = source_len
        ? fdb_kv_set_blob(&g_kvdb, src_key,
                          fdb_blob_make(&blob, next_sources, source_len))
        : fdb_kv_del(&g_kvdb, src_key);
    if (err != FDB_NO_ERR) {
        result->error = ANKI_SYNC_ERR_STORAGE;
        goto fail;
    }

    err = card_len
        ? fdb_kv_set_blob(&g_kvdb, card_key,
                          fdb_blob_make(&blob, next_cards, card_len))
        : fdb_kv_del(&g_kvdb, card_key);
    if (err != FDB_NO_ERR) {
        if (old_source_count) {
            fdb_kv_set_blob(&g_kvdb, src_key,
                            fdb_blob_make(&blob, old_sources,
                                          old_source_count * sizeof(SourceCard)));
        } else {
            fdb_kv_del(&g_kvdb, src_key);
        }
        result->error = ANKI_SYNC_ERR_STORAGE;
        goto fail;
    }

    strncpy(deck->name, name, sizeof(deck->name) - 1);
    deck->name[sizeof(deck->name) - 1] = '\0';
    strncpy(deck->sd_file_path, path, sizeof(deck->sd_file_path) - 1);
    deck->sd_file_path[sizeof(deck->sd_file_path) - 1] = '\0';
    if (!anki_storage_sync_decks(model) || !anki_storage_sync_meta(model)) {
        if (old_source_count) {
            fdb_kv_set_blob(&g_kvdb, src_key,
                            fdb_blob_make(&blob, old_sources,
                                          old_source_count * sizeof(*old_sources)));
            fdb_kv_set_blob(&g_kvdb, card_key,
                            fdb_blob_make(&blob, old_cards,
                                          old_card_count * sizeof(*old_cards)));
        } else {
            fdb_kv_del(&g_kvdb, src_key);
            fdb_kv_del(&g_kvdb, card_key);
        }
        if (new_deck) {
            model->deck_count--;
            new_deck = false;
        } else {
            *deck = previous_deck;
        }
        model->_id_generator = previous_deck_generator;
        model->_cards_id_generator = previous_card_generator;
        anki_storage_sync_decks(model);
        anki_storage_sync_meta(model);
        result->error = ANKI_SYNC_ERR_STORAGE;
        goto fail;
    }

    for (uint16_t i = 0; i < result->purged; i++) {
        anki_storage_delete_schedule(purged_ids[i]);
    }

    ESP_LOGI(TAG, "%s: +%u ~%u =%u -%u purged=%u skipped=%u", path,
             result->added, result->updated, result->unchanged,
             result->missing, result->purged, result->skipped);
    free(parsed);
    free(old_sources);
    free(old_cards);
    free(matched);
    free(next_cards);
    free(next_sources);
    free(purged_ids);
    result->error = ANKI_SYNC_OK;
    return true;

fail:
    if (result->error == ANKI_SYNC_OK) result->error = ANKI_SYNC_ERR_STORAGE;
    if (new_deck && deck_idx == model->deck_count - 1) model->deck_count--;
    model->_id_generator = previous_deck_generator;
    model->_cards_id_generator = previous_card_generator;
    free(parsed);
    free(old_sources);
    free(old_cards);
    free(matched);
    free(next_cards);
    free(next_sources);
    free(purged_ids);
    return false;
}

void anki_source_sync_all(AnkiModel *model)
{
    if (!model) return;

    /* First refresh every source already known to the app, including files the
     * user selected outside the conventional /sdcard/anki directory. */
    uint8_t known_deck_count = model->deck_count;
    for (uint8_t i = 0; i < known_deck_count; i++) {
        if (!model->decks[i].sd_file_path[0]) continue;
        AnkiSyncResult result;
        anki_source_sync_file(model, model->decks[i].sd_file_path, &result);
    }

    DIR *dir = opendir(ANKI_SOURCE_ROOT);
    if (!dir) {
        ESP_LOGW(TAG, "%s unavailable; keeping existing source decks offline",
                 ANKI_SOURCE_ROOT);
        return;
    }

    struct dirent *entry;
    unsigned inspected = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (++inspected > SOURCE_DIRECTORY_SCAN_MAX) {
            ESP_LOGW(TAG, "%s contains too many directory entries", ANKI_SOURCE_ROOT);
            break;
        }
        const char *dot = strrchr(entry->d_name, '.');
        if (!dot || strcasecmp(dot, ".txt") != 0) continue;
        char path[sizeof(model->decks[0].sd_file_path)];
        int written = snprintf(path, sizeof(path), "%s/%s",
                               ANKI_SOURCE_ROOT, entry->d_name);
        if (written <= 0 || (size_t)written >= sizeof(path)) {
            ESP_LOGW(TAG, "skip long filename: %s", entry->d_name);
            continue;
        }
        if (find_deck_by_path(model, path) >= 0) continue;
        if (model->deck_count >= ANKI_MAX_DECK_CAPACITY) break;
        AnkiSyncResult result;
        anki_source_sync_file(model, path, &result);
    }
    closedir(dir);
}

bool anki_source_load_payload(const AnkiModel *model, const CardIndex *card,
                              CardPayload *payload)
{
    if (!model || !card || !payload || card->file_len == 0 ||
        card->file_len >= SOURCE_LINE_MAX) return false;

    const AnkiDeck *deck = NULL;
    for (int i = 0; i < model->deck_count; i++) {
        if (model->decks[i].deck_id == card->deck_id &&
            model->decks[i].sd_file_path[0]) {
            deck = &model->decks[i];
            break;
        }
    }
    if (!deck) return false;

    FILE *file = fopen(deck->sd_file_path, "rb");
    if (!file) return false;
    if (fseek(file, (long)card->file_offset, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    char line[SOURCE_LINE_MAX] = {0};
    size_t read = fread(line, 1, card->file_len, file);
    fclose(file);
    if (read != card->file_len) return false;
    line[read] = '\0';

    char *front;
    char *back;
    if (parse_fields(line, &front, &back) != PARSE_LINE_CARD) return false;
    memset(payload, 0, sizeof(*payload));
    strncpy(payload->front, front, sizeof(payload->front) - 1);
    strncpy(payload->back, back, sizeof(payload->back) - 1);
    return true;
}
