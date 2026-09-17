/**
 * @file wifi_store.h
 * @brief 已保存的 Wi-Fi 网络列表 —— FlashDB 持久化
 *
 * 存储选型：全工程统一用 FlashDB，不再让 NVS 参与凭据存储。
 *   esp_wifi 默认是 WIFI_STORAGE_FLASH，会自己往 NVS 写一份 SSID/密码。
 *   那样就有两份"上次连的是谁"，迟早对不上。所以 wifi_manager_init() 里
 *   显式调了 esp_wifi_set_storage(WIFI_STORAGE_RAM) 把它关掉，
 *   **本模块是凭据的唯一事实来源**。
 *
 * 代价：FlashDB 没有加密层，密码在分区里是明文的。
 *   这是明确权衡过的选择（换来只有一套存储机制）。
 *   将来若要加密，方案是给这个 blob 单独做一层对称加密，
 *   而不是把凭据挪回 NVS —— 那会把双份事实来源又请回来。
 *
 * 容量：WIFI_STORE_MAX 条，满了淘汰 last_used 最旧的一条。
 *       一条约 103 字节，10 条约 1.1 KB。
 */

#ifndef WIFI_STORE_H
#define WIFI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_STORE_MAX      10
#define WIFI_STORE_SSID_MAX 33
#define WIFI_STORE_PWD_MAX  65

typedef struct {
    char     ssid[WIFI_STORE_SSID_MAX];
    char     password[WIFI_STORE_PWD_MAX];
    uint32_t last_used;    /* unix 秒；滚动淘汰和"优先连最近用过的"都靠它 */
    uint8_t  auto_connect; /* 0 = 只记密码不自动连 */
} wifi_cred_t;

/** 从 flash 载入。wifi_manager_init() 里调一次。 */
void wifi_store_load(void);

uint8_t wifi_store_count(void);
const wifi_cred_t *wifi_store_at(uint8_t idx);

/** 按 SSID 查；没有返回 NULL */
const wifi_cred_t *wifi_store_find(const char *ssid);

/**
 * 记住一个网络（连接成功后才调，别在输入时就记）。
 * 已存在则更新密码和 last_used；满了淘汰 last_used 最旧的一条。
 */
bool wifi_store_put(const char *ssid, const char *password);

/** 只把 last_used 刷新到当前时间并落盘 */
void wifi_store_touch(const char *ssid);

/** 删除一条。"忘记此网络"用。 */
bool wifi_store_remove(const char *ssid);

void wifi_store_clear(void);

/** 最近用过的那条；列表为空返回 NULL */
const wifi_cred_t *wifi_store_most_recent(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STORE_H */
