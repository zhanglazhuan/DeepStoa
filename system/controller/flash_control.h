// system/controller/flash_control.h
// FlashDB key-value database wrapper for ESP-IDF
//
// 两个独立的库，对应两个分区（见 fal_cfg.h）：
//
//   g_kvdb      用户区（fdb_user）—— Wi-Fi 凭据、各 app 的设置和使用记录。
//               **恢复出厂设置会整块擦掉。**
//               这是绝大多数代码该用的那个，名字保持不变，
//               现有 11 处 `extern struct fdb_kvdb g_kvdb;` 都不用改。
//
//   g_sys_kvdb  厂家区（fdb_sys）—— 机器码、序列号、出厂标定。
//               产线单独刷入，**恢复出厂设置不擦**。
//               只有产线工具和"读机器码"这类场景该碰它。

#ifndef FLASH_CONTROL_H
#define FLASH_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include <flashdb.h>

extern struct fdb_kvdb g_kvdb;       /* 用户区，恢复出厂设置会擦 */
extern struct fdb_kvdb g_sys_kvdb;   /* 厂家区，恢复出厂设置保留 */

// Initialize flashDB (FAL + both KVDBs). Call once at boot, before any fdb_kv_*.
void flash_control_init(void);
void flash_control_deinit(void);

/* ── 厂家区读写 ────────────────────────────────────────────────────────
 * 刻意做成窄接口：厂家区只放少量短字符串，不希望应用代码把它当通用存储用。 */

bool flash_sys_get_str(const char *key, char *out, size_t out_len);
bool flash_sys_set_str(const char *key, const char *val);

/** 机器码。没写过时返回一份由 eFuse MAC 推出的稳定占位值，
 *  这样开发板上也能看到一个固定的 ID，产线刷入后会覆盖它。 */
const char *flash_sys_device_id(void);

/**
 * 恢复出厂设置：整块擦掉用户区，厂家区原样保留。
 * 调用后设备会重启（本函数不返回）。
 */
void flash_control_factory_reset(void);

#endif
