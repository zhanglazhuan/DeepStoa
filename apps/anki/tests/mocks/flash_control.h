#ifndef TEST_FLASH_CONTROL_H
#define TEST_FLASH_CONTROL_H
#include <stddef.h>

struct fdb_kvdb { int unused; };
extern struct fdb_kvdb g_kvdb;

struct fdb_blob {
    const void *buf;
    size_t size;
    struct { size_t len; } saved;
};
typedef struct fdb_blob *fdb_blob_t;
typedef int fdb_err_t;
#define FDB_NO_ERR 0
#define FDB_KV_NAME_ERR 5
#define FDB_SAVED_FULL 7

fdb_blob_t fdb_blob_make(struct fdb_blob *blob, const void *buf, size_t size);
size_t fdb_kv_get_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob);
fdb_err_t fdb_kv_set_blob(struct fdb_kvdb *db, const char *key, fdb_blob_t blob);
fdb_err_t fdb_kv_del(struct fdb_kvdb *db, const char *key);
#endif
