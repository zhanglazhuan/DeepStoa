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
#include "index_store.h"
#include "source_sync.h"
#include "storage.h"

static const char *TAG = "anki_source";

#define SOURCE_LINE_MAX 1100
#define SOURCE_DIRECTORY_SCAN_MAX 1024U
#define SOURCE_SIGNATURE_MAGIC UINT32_C(0x41534947)

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint64_t hash;
    uint32_t size;
} AnkiSourceSignature;

static bool source_signature(const char *path, AnkiSourceSignature *signature)
{
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0 ||
        (uint64_t)info.st_size > ANKI_MAX_SOURCE_BYTES) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    uint64_t hash = UINT64_C(14695981039346656037);
    uint8_t buffer[1024];
    size_t total = 0;
    size_t read;
    unsigned chunks = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        total += read;
        for (size_t i = 0; i < read; i++) {
            hash ^= buffer[i];
            hash *= UINT64_C(1099511628211);
        }
        if ((++chunks & 127U) == 0) vTaskDelay(1);
    }
    bool ok = !ferror(file) && total == (size_t)info.st_size;
    fclose(file);
    struct stat after;
    ok = ok && stat(path, &after) == 0 &&
         after.st_size == info.st_size && after.st_mtime == info.st_mtime;
    if (!ok) return false;
    memset(signature, 0, sizeof(*signature));
    signature->magic = SOURCE_SIGNATURE_MAGIC;
    signature->version = 1;
    signature->hash = hash;
    signature->size = (uint32_t)total;
    return true;
}

static void signature_key(char *key, size_t size, uint32_t deck_id)
{
    snprintf(key, size, "anki_sig_%u", (unsigned)deck_id);
}

static bool source_unchanged(uint32_t deck_id, const char *path)
{
    if (!anki_index_is_chunked(deck_id)) return false;
    AnkiSourceSignature actual;
    if (!source_signature(path, &actual)) return false;
    char key[32];
    signature_key(key, sizeof(key), deck_id);
    AnkiSourceSignature stored;
    struct fdb_blob blob;
    return fdb_kv_get_blob(&g_kvdb, key,
               fdb_blob_make(&blob, &stored, sizeof(stored))) == sizeof(stored) &&
           stored.magic == SOURCE_SIGNATURE_MAGIC && stored.version == 1 &&
           stored.size == actual.size && stored.hash == actual.hash;
}

static void remember_source(uint32_t deck_id, const char *path)
{
    AnkiSourceSignature signature;
    if (!source_signature(path, &signature)) return;
    char key[32];
    signature_key(key, sizeof(key), deck_id);
    struct fdb_blob blob;
    if (fdb_kv_set_blob(&g_kvdb, key,
                        fdb_blob_make(&blob, &signature,
                                      sizeof(signature))) != FDB_NO_ERR) {
        ESP_LOGW(TAG, "could not cache source signature for deck %u",
                 (unsigned)deck_id);
    }
}

typedef struct {
    uint64_t line_hash;
    uint64_t front_hash;
    uint32_t file_offset;
    uint16_t file_len;
    bool has_id;
} ParsedCard;

typedef struct {
    uint64_t hash;
    uint16_t index;
} HashRef;

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

/* A source record stays on one physical line. Decode only the back field so
 * existing front-field identity remains unchanged. Unknown escapes are literal. */
static void decode_back_escapes(char *text)
{
    char *src = text;
    char *dst = text;
    while (*src) {
        if (src[0] == '\\' && src[1] == 'n') {
            *dst++ = '\n';
            src += 2;
        } else if (src[0] == '\\' && src[1] == '\\') {
            *dst++ = '\\';
            src += 2;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static ParseLineResult parse_fields(char *line, char **front, char **back,
                                   char **identity)
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
    if (identity) *identity = NULL;
    if (strncmp(*front, "@id=", 4) == 0) {
        char *id = *front + 4;
        char *space = strchr(id, ' ');
        if (!space || space == id || (size_t)(space - id) > 64)
            return PARSE_LINE_INVALID;
        for (char *p = id; p < space; p++) {
            if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-')
                return PARSE_LINE_INVALID;
        }
        *space = '\0';
        *front = trim(space + 1);
        if (identity) *identity = id;
    }
    if (!(*front)[0] || !(*back)[0] ||
        !valid_utf8(*front) || !valid_utf8(*back)) return PARSE_LINE_INVALID;
    decode_back_escapes(*back);
    return strlen(*front) < sizeof(((CardPayload *)0)->front) &&
           strlen(*back) < sizeof(((CardPayload *)0)->back)
        ? PARSE_LINE_CARD : PARSE_LINE_INVALID;
}

static AnkiSyncError parse_source_file(const char *path, ParsedCard *cards,
                                       uint16_t *count, uint16_t *skipped,
                                       uint32_t *first_skipped_line)
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
    *first_skipped_line = 0;
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
            if (!*first_skipped_line) *first_skipped_line = line_count;
            continue;
        }

        char *front;
        char *back;
        char *identity = NULL;
        ParseLineResult line_result = parse_fields(line, &front, &back,
                                                  &identity);
        if (line_result == PARSE_LINE_IGNORED) continue;
        if (line_result != PARSE_LINE_CARD || line_offset < 0 ||
            raw_len > UINT16_MAX || (uint64_t)line_offset > UINT32_MAX) {
            skipped_count++;
            if (!*first_skipped_line) *first_skipped_line = line_count;
            continue;
        }

        if (*count >= ANKI_MAX_CARDS_PER_DECK) {
            error = ANKI_SYNC_ERR_TOO_MANY_CARDS;
            break;
        }

        ParsedCard *parsed = &cards[(*count)++];
        parsed->front_hash = identity
            ? hash_bytes(UINT64_C(0x7da71c18f1ad3853), identity)
            : hash_bytes(UINT64_C(14695981039346656037), front);
        parsed->has_id = identity != NULL;
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

static int compare_hash_refs(const void *left, const void *right)
{
    const HashRef *a = left;
    const HashRef *b = right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : a->index > b->index ? 1 : 0;
}

static uint16_t hash_lower_bound(const HashRef *refs, uint16_t count,
                                 uint64_t hash)
{
    uint16_t lo = 0, hi = count;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        if (refs[mid].hash < hash) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int find_exact(uint64_t hash, const HashRef *old_lines,
                      const bool *matched, uint16_t old_count)
{
    for (uint16_t i = hash_lower_bound(old_lines, old_count, hash);
         i < old_count && old_lines[i].hash == hash; i++) {
        if (!matched[old_lines[i].index]) return old_lines[i].index;
    }
    return -1;
}

static int find_unique_front(uint64_t hash, const HashRef *new_fronts,
                             uint16_t parsed_count, const HashRef *old_fronts,
                             const bool *matched, uint16_t old_count)
{
    uint16_t first = hash_lower_bound(new_fronts, parsed_count, hash);
    if (first >= parsed_count || new_fronts[first].hash != hash ||
        (first + 1 < parsed_count && new_fronts[first + 1].hash == hash)) {
        return -1;
    }

    int found = -1;
    for (uint16_t i = hash_lower_bound(old_fronts, old_count, hash);
         i < old_count && old_fronts[i].hash == hash; i++) {
        if (!matched[old_fronts[i].index]) {
            if (found >= 0) return -1;
            found = old_fronts[i].index;
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
                                      &result->skipped,
                                      &result->first_skipped_line);
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

    uint16_t old_source_count = 0;
    uint16_t old_card_count = 0;
    AnkiSourceIndex *old_sources = NULL;
    CardIndex *old_cards = NULL;
    bool *matched = NULL;
    CardIndex *next_cards = NULL;
    AnkiSourceIndex *next_sources = NULL;
    HashRef *old_lines = NULL;
    HashRef *old_fronts = NULL;
    HashRef *new_fronts = NULL;
    if (!anki_index_load_sources(deck_id, &old_sources, &old_source_count) ||
        !anki_index_load_cards(deck_id, &old_cards, &old_card_count)) {
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
    next_sources = calloc(ANKI_MAX_CARDS_PER_DECK, sizeof(AnkiSourceIndex));
    if (old_source_count) {
        old_lines = malloc(old_source_count * sizeof(*old_lines));
        old_fronts = malloc(old_source_count * sizeof(*old_fronts));
    }
    if (parsed_count) new_fronts = malloc(parsed_count * sizeof(*new_fronts));
    if (!matched || !next_cards || !next_sources ||
        (old_source_count && (!old_lines || !old_fronts)) ||
        (parsed_count && !new_fronts)) {
        result->error = ANKI_SYNC_ERR_MEMORY;
        goto fail;
    }
    for (uint16_t i = 0; i < old_source_count; i++) {
        old_lines[i] = (HashRef){old_sources[i].line_hash, i};
        old_fronts[i] = (HashRef){old_sources[i].front_hash, i};
    }
    for (uint16_t i = 0; i < parsed_count; i++) {
        new_fronts[i] = (HashRef){parsed[i].front_hash, i};
    }
    if (parsed_count) {
        qsort(new_fronts, parsed_count, sizeof(*new_fronts),
              compare_hash_refs);
        for (uint16_t i = 1; i < parsed_count; i++) {
            if (new_fronts[i - 1].hash == new_fronts[i].hash &&
                (parsed[new_fronts[i - 1].index].has_id ||
                 parsed[new_fronts[i].index].has_id)) {
                result->error = ANKI_SYNC_ERR_DUPLICATE_ID;
                goto fail;
            }
        }
    }
    if (old_source_count) {
        qsort(old_lines, old_source_count, sizeof(*old_lines), compare_hash_refs);
        qsort(old_fronts, old_source_count, sizeof(*old_fronts), compare_hash_refs);
    }

    uint16_t next_count = 0;
    for (uint16_t i = 0; i < parsed_count; i++) {
        if ((i & 0x1FU) == 0x1FU) vTaskDelay(1);
        int old_idx = -1;
        if (parsed[i].has_id && old_source_count) {
            old_idx = find_unique_front(parsed[i].front_hash, new_fronts,
                                        parsed_count, old_fronts, matched,
                                        old_source_count);
        }
        if (old_idx < 0 && old_source_count) {
            old_idx = find_exact(parsed[i].line_hash, old_lines, matched,
                                 old_source_count);
        }
        if (old_idx < 0 && !parsed[i].has_id && old_source_count) {
            old_idx = find_unique_front(parsed[i].front_hash, new_fronts,
                                        parsed_count, old_fronts, matched,
                                        old_source_count);
        }

        CardIndex *card = &next_cards[next_count];
        AnkiSourceIndex *source = &next_sources[next_count];
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
            result->error = ANKI_SYNC_ERR_HISTORY_CAPACITY;
            goto fail;
        }
        next_cards[next_count] = old_cards[i];
        next_cards[next_count].status |= ANKI_CARD_FLAG_MISSING;
        next_sources[next_count] = old_sources[i];
        next_count++;
        result->missing++;
    }

    bool same_index = next_count == old_card_count &&
        (!next_count ||
         (memcmp(next_cards, old_cards,
                 next_count * sizeof(*next_cards)) == 0 &&
          memcmp(next_sources, old_sources,
                 next_count * sizeof(*next_sources)) == 0));
    bool index_saved = !same_index || !anki_index_is_chunked(deck_id);
    if (index_saved &&
        !anki_index_save(deck_id, next_cards, next_sources, next_count)) {
        result->error = anki_index_last_write_full()
            ? ANKI_SYNC_ERR_NO_SPACE : ANKI_SYNC_ERR_STORAGE;
        goto fail;
    }

    strncpy(deck->name, name, sizeof(deck->name) - 1);
    deck->name[sizeof(deck->name) - 1] = '\0';
    strncpy(deck->sd_file_path, path, sizeof(deck->sd_file_path) - 1);
    deck->sd_file_path[sizeof(deck->sd_file_path) - 1] = '\0';
    if (!anki_storage_sync_decks(model) || !anki_storage_sync_meta(model)) {
        if (index_saved) {
            if (old_source_count) {
                anki_index_save(deck_id, old_cards, old_sources,
                                old_source_count);
            } else {
                anki_index_delete(deck_id);
            }
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

    ESP_LOGI(TAG, "%s: +%u ~%u =%u missing=%u skipped=%u", path,
             result->added, result->updated, result->unchanged,
             result->missing, result->skipped);
    remember_source(deck_id, path);
    free(parsed);
    free(old_sources);
    free(old_cards);
    free(matched);
    free(next_cards);
    free(next_sources);
    free(old_lines);
    free(old_fronts);
    free(new_fronts);
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
    free(old_lines);
    free(old_fronts);
    free(new_fronts);
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
        if (source_unchanged(model->decks[i].deck_id,
                             model->decks[i].sd_file_path)) continue;
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
    if (parse_fields(line, &front, &back, NULL) != PARSE_LINE_CARD) return false;
    memset(payload, 0, sizeof(*payload));
    strncpy(payload->front, front, sizeof(payload->front) - 1);
    strncpy(payload->back, back, sizeof(payload->back) - 1);
    return true;
}
