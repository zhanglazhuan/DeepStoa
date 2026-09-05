/**
 * @file ota.h
 * @brief WiFi 固件升级 —— 查更新 / 下载刷写 / 结果回报。
 *
 * 流程:
 *   ota_check_async()  ── 拉 manifest，比版本，回调回 LVGL 线程
 *        ↓ 用户二次确认（UI 层负责，见 apps/settings/subpages/view_update.c）
 *   ota_install_async() ── 后台任务下载 + 写 flash + 切启动分区
 *        ↓ UI 轮询 ota_get_state() / ota_get_progress()
 *   ota_apply_and_reboot() ── 用户确认后重启生效
 *
 * manifest 格式（服务端见 server/ota/api.py）:
 *   {"version":"0.2.0","url":"http://host/api/ota/download?version=0.2.0",
 *    "size":1043712,"sha256":"…","date":"2026-09-01","changelog":"…",
 *    "mandatory":false,"min_battery":30}
 *
 * 线程约定:
 *   - 所有 *_async 函数从 LVGL 线程调用，立刻返回，真正的网络 IO 在后台任务里。
 *   - ota_check_async 的回调经 lv_async_call 弹回 LVGL 线程，可以直接碰 UI。
 *   - 下载进度不走回调（墨水屏刷不动那么快），UI 自己按需轮询状态。
 *
 * 注意: manifest 里的 sha256 是发给工具链核对用的，端侧不做逐字节校验 ——
 *       镜像完整性由 esp_ota_end() 的镜像头 + 校验和兜底。
 */

#ifndef SYS_OTA_H
#define SYS_OTA_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 没配 "upd_url" 时用的默认 manifest 地址。
 *  指向 PC 上的 server/run.py —— 它把 news / ota / logs 聚在同一个端口，
 *  192.168.137.1 是 Windows 移动热点的固定网关。详见 server/README.md。
 *  （server/ota/app.py 也能单独跑在 8010，那时改成 :8010。） */
#define OTA_DEFAULT_MANIFEST_URL "http://192.168.137.1:8000/api/ota/check"

#define OTA_VERSION_LEN    24
#define OTA_URL_LEN        256
#define OTA_CHANGELOG_LEN  512
#define OTA_DATE_LEN       16

/* ── 查更新 ─────────────────────────────────────────────────────────── */

typedef enum {
    OTA_CHECK_UP_TO_DATE = 0,     /* 远端版本不比本机新 */
    OTA_CHECK_UPDATE_AVAILABLE,   /* 有新版本，info 有效 */
    OTA_CHECK_ERR_OFFLINE,        /* WiFi 没连上，请求根本没发出去 */
    OTA_CHECK_ERR_NO_URL,         /* manifest 地址没配或不是 http(s) */
    OTA_CHECK_ERR_NETWORK,        /* DNS / TCP / TLS / HTTP 状态码失败 */
    OTA_CHECK_ERR_PARSE,          /* 响应不是合法 manifest */
    OTA_CHECK_ERR_BUSY,           /* 已经有一次检查或升级在跑 */
} ota_check_result_t;

/** 一次可用更新的全部信息。回调里的指针只在回调内有效，要留请自己拷贝。 */
typedef struct {
    char version[OTA_VERSION_LEN];
    char url[OTA_URL_LEN];
    char changelog[OTA_CHANGELOG_LEN];
    char date[OTA_DATE_LEN];
    int  size;         /* 字节，0 表示服务端没给 */
    bool mandatory;    /* 服务端标记的必装更新 */
    int  min_battery;  /* 允许升级的最低电量百分比 */
} ota_update_info_t;

typedef void (*ota_check_cb_t)(ota_check_result_t result,
                               const ota_update_info_t *info,
                               void *user_data);

/* ── 升级状态机 ─────────────────────────────────────────────────────── */

typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_READY,     /* 已写入并切好启动分区，等用户确认重启 */
    OTA_FAILED,
} ota_state_t;

/* ── 生命周期 ───────────────────────────────────────────────────────── */

/**
 * @brief 开机调一次。确认当前固件可用（取消回滚），并把上一次升级的结果
 *        排队回报给服务端（联网后自动发，没网就静默放弃）。
 */
void ota_init(void);

/** 当前运行固件的版本号（来自 version.txt → PROJECT_VER）。 */
const char *ota_running_version(void);

/** 设备标识（WiFi MAC，形如 "a0b1c2d3e4f5"），回报和查更新时带给服务端。 */
const char *ota_device_id(void);

/* ── 查更新 ─────────────────────────────────────────────────────────── */

/**
 * @brief 异步查更新。
 *
 * @param manifest_url  manifest 地址，NULL 用 OTA_DEFAULT_MANIFEST_URL。
 * @param cb            结果回调，在 LVGL 线程执行。
 * @param user_data     透传给 cb。
 * @return false 表示任务没起来（正忙 / 没内存），此时 cb 不会被调用。
 */
bool ota_check_async(const char *manifest_url, ota_check_cb_t cb, void *user_data);

/** 最近一次查到的可用更新；没有就返回 NULL。开机自动检查的结果也存在这里，
 *  用户进设置页时可以直接拿来渲染，不用重查。 */
const ota_update_info_t *ota_get_pending(void);

/** 丢弃缓存的可用更新（用户点了"忽略"或升级已完成时调）。 */
void ota_clear_pending(void);

/**
 * @brief 开机后台静默查一次更新，结果存进 ota_get_pending()。
 * @param manifest_url  同 ota_check_async。
 * @param delay_ms      起任务后先等多久再查，给 WiFi 留连接时间。
 */
void ota_boot_check(const char *manifest_url, int delay_ms);

/* ── 下载 & 刷写 ────────────────────────────────────────────────────── */

/**
 * @brief 异步下载并刷写固件。立刻返回，进度轮询 ota_get_progress()。
 *
 * 成功后状态变成 OTA_READY —— 不会自动重启，什么时候生效由 UI 让用户决定。
 *
 * @return false 表示任务没起来（正忙 / 参数不合法 / 没内存）。
 */
bool ota_install_async(const ota_update_info_t *info);

ota_state_t ota_get_state(void);

/** 下载进度 0..100；不在下载时返回最后一次的值。 */
int ota_get_progress(void);

/** 失败原因（人能看的短句），没失败过返回空串。 */
const char *ota_get_error(void);

/** 重启生效。状态不是 OTA_READY 时什么也不做。 */
void ota_apply_and_reboot(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_OTA_H */
