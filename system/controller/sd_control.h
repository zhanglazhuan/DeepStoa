// system/controller/sd_control.h
// SD card control — filesystem operations
// Ported from D:\Codes\EPOS\epos\drivers\epos_sd_control.h

#ifndef SD_CONTROL_H
#define SD_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_FILE_NAME 255

typedef enum { NODE_TYPE_FILE = 0, NODE_TYPE_DIR } file_node_type_t;

typedef struct {
    char name[MAX_FILE_NAME + 1];
    file_node_type_t type;
    size_t size;
} file_node_t;

typedef struct {
    file_node_t *nodes;
    uint32_t count;
} file_list_t;

typedef struct {
    size_t size;
    file_node_type_t type;
} file_stat_t;

// ── API ────────────────────────────────────────────────────────────────
bool sd_control_init(const char *mount_pt);
bool sd_control_deinit(const char *mount_pt);
bool sd_control_is_ready(const char *mount_pt);
bool sd_control_get_capacity(const char *mount_pt, uint32_t *total_mb, uint32_t *free_mb);

bool sd_control_get_dir_list(const char *dir_path, file_list_t *list, const char *filter_ext);
void sd_control_free_dir_list(file_list_t *list);

bool sd_control_check_exists(const char *path);
bool sd_control_create_dir(const char *path);
bool sd_control_delete(const char *path);
bool sd_control_rename(const char *old_path, const char *new_path);
bool sd_control_file_get_stat(const char *path, file_stat_t *stat);
bool sd_control_file_get_size(const char *path, size_t *out_size);

#endif
