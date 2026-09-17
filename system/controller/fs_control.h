/**
 * @file fs_control.h
 * @brief 内部 flash 上的 FAT 文件系统 —— 在没有 SD 卡座时提供"真实文件"
 *
 * 为什么需要它：
 *   ID3 元数据解析、VBR 时长（Xing/Info 头）、seek 索引表 —— 这三件全都是
 *   纯文件格式解析，和有没有 codec、能不能出声完全无关，但都需要**有真实文件
 *   可读**。板子上还没有 SD 卡座，所以在内部 flash 上开一个 FAT 分区顶上。
 *
 * 挂载点故意用 FS_MOUNT_POINT（"/sdcard"）：
 *   - player 的 PLAYER_MUSIC_DIR 和 chatbot 的 FSEL_ROOT 都已经指向这个前缀，
 *     挂上之后它们会自动从 mock 切到真实目录，一行代码都不用改；
 *   - SD 卡座到位后，把 SD 挂到同一个点、不再挂内部分区即可，路径依然稳定。
 *   名字上是有点别扭（内部 flash 却叫 sdcard），但路径稳定比名字重要。
 *
 * 不预烧镜像：首次挂载失败时自动格式化成空 FAT 并建好 /music 目录。
 */

#ifndef FS_CONTROL_H
#define FS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FS_MOUNT_POINT      "/sdcard"
#define FS_PARTITION_LABEL  "storage"

/** 挂载内部 FAT 分区。开机时调一次，要在 audio_service_init() 之前。 */
esp_err_t fs_control_init(void);
void      fs_control_deinit(void);

bool      fs_control_is_mounted(void);

/** 容量，单位字节。未挂载时写 0。 */
void      fs_control_get_usage(uint64_t *total, uint64_t *used);

#ifdef __cplusplus
}
#endif

#endif /* FS_CONTROL_H */
