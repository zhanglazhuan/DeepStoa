#ifndef ANKI_BLOB_STORE_H
#define ANKI_BLOB_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Versioned, two-slot storage for fixed-size records. Existing single-key
 * records remain readable and are removed only after a new manifest commits. */
bool anki_blob_load(const char *legacy_key, const char *prefix,
                    size_t item_size, uint16_t capacity, void *items,
                    uint16_t *count);
bool anki_blob_save(const char *legacy_key, const char *prefix,
                    size_t item_size, uint16_t capacity, const void *items,
                    uint16_t count);
bool anki_blob_delete(const char *legacy_key, const char *prefix,
                      size_t item_size, uint16_t capacity);

#endif
