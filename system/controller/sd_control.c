// system/controller/sd_control.c
// SD card control — ESP-IDF VFS filesystem operations
// Ported from D:\Codes\EPOS\epos\drivers\epos_sd_control.c (Zephyr fs → ESP-IDF VFS)

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sd_control.h"

static const char *TAG = "sd_ctrl";

bool sd_control_init(const char *mount_pt)
{
    struct stat st;
    if (stat(mount_pt, &st) == 0) {
        ESP_LOGI(TAG, "SD already mounted at %s", mount_pt);
        return true;
    }
    ESP_LOGW(TAG, "SD not mounted at %s — call sdmmc init first", mount_pt);
    return false;
}

bool sd_control_deinit(const char *mount_pt)
{
    return true;
}

bool sd_control_is_ready(const char *mount_pt)
{
    struct stat st;
    return stat(mount_pt, &st) == 0 && S_ISDIR(st.st_mode);
}

bool sd_control_get_capacity(const char *mount_pt, uint32_t *total_mb, uint32_t *free_mb)
{
    if (!mount_pt || !total_mb || !free_mb) return false;
    uint64_t total = 0, avail = 0;
    esp_err_t err = esp_vfs_fat_info(mount_pt, &total, &avail);
    if (err != ESP_OK) return false;
    *total_mb = (uint32_t)(total / (1024U * 1024U));
    *free_mb = (uint32_t)(avail / (1024U * 1024U));
    return true;
}

bool sd_control_get_dir_list(const char *dir_path, file_list_t *list, const char *filter_ext)
{
    if (!list) return false;
    memset(list, 0, sizeof(*list));

    DIR *d = opendir(dir_path);
    if (!d) return false;

    // Count entries
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (filter_ext) {
            const char *ext = strrchr(ent->d_name, '.');
            if (!ext || strcasecmp(ext, filter_ext) != 0) continue;
        }
        count++;
    }
    rewinddir(d);

    list->nodes = count ? (file_node_t *)calloc((size_t)count, sizeof(file_node_t)) : NULL;
    if (count > 0 && !list->nodes) { closedir(d); return false; }

    int i = 0;
    while ((ent = readdir(d)) != NULL && i < count) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (filter_ext) {
            const char *ext = strrchr(ent->d_name, '.');
            if (!ext || strcasecmp(ext, filter_ext) != 0) continue;
        }
        strncpy(list->nodes[i].name, ent->d_name, MAX_FILE_NAME - 1);
        list->nodes[i].name[MAX_FILE_NAME - 1] = '\0';
        list->nodes[i].type = (ent->d_type == DT_DIR) ? NODE_TYPE_DIR : NODE_TYPE_FILE;
        list->nodes[i].size = 0;  // stat would be needed for size
        i++;
    }
    list->count = i;
    closedir(d);
    return true;
}

void sd_control_free_dir_list(file_list_t *list)
{
    if (list && list->nodes) { free(list->nodes); list->nodes = NULL; list->count = 0; }
}

bool sd_control_check_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

bool sd_control_create_dir(const char *path)
{
    return mkdir(path, 0755) == 0;
}

bool sd_control_delete(const char *path)
{
    return unlink(path) == 0 || (rmdir(path) == 0);
}

bool sd_control_rename(const char *old_path, const char *new_path)
{
    return rename(old_path, new_path) == 0;
}

bool sd_control_file_get_stat(const char *path, file_stat_t *stat_out)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    stat_out->size = st.st_size;
    stat_out->type = S_ISDIR(st.st_mode) ? NODE_TYPE_DIR : NODE_TYPE_FILE;
    return true;
}

bool sd_control_file_get_size(const char *path, size_t *out_size)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *out_size = st.st_size;
    return true;
}
