/**
 * @file news_svc.h
 * @brief 新闻数据源抽象层 —— 列表分页 + 正文按需拉取
 *
 * 实现有两份，共用本头文件，由 CMakeLists 的 NEWS_SVC 选项二选一编译：
 *   news_svc_mock.c   默认。本地假数据 + lv_timer 模拟时延，不需要服务端
 *   news_svc_http.c   接自建 news 服务端（server/news，esp_http_client + cJSON）
 *
 * ── 实现方必须遵守的三条契约 ──────────────────────────────────────────
 *
 * 1. 回调必须在 LVGL 线程被调用（真实实现跑在 worker task 里，用 lv_async_call 弹回）。
 * 2. 每个请求必定回调一次，超时由本层内部计时并主动回 ERR_TIMEOUT，
 *    controller 不再自己起 timer。唯一的例外是被 news_svc_abort() 取消的请求。
 * 3. 回调里的 items / content 指针只在回调期间有效，调用方当场拷走。
 */

#ifndef NEWS_SVC_H
#define NEWS_SVC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 超时（本层内部实现，不要求调用方计时） */
#define NEWS_SVC_TIMEOUT_MS  10000u

typedef enum {
    NEWS_SVC_OK = 0,
    NEWS_SVC_ERR_OFFLINE,     /* wifi 未连接，请求根本没发出去 */
    NEWS_SVC_ERR_TIMEOUT,
    NEWS_SVC_ERR_NETWORK,     /* DNS / 连接失败 */
    NEWS_SVC_ERR_SERVER,      /* HTTP 4xx / 5xx */
    NEWS_SVC_ERR_BAD_REPLY,   /* 响应 JSON 解析失败 */
    NEWS_SVC_ERR_BUSY,        /* 本层在途请求已满 */
} news_svc_err_t;

/* 前置声明：service 层不反向依赖 model.h 的全部内容 */
struct NewsArticle;

/**
 * 列表回调。
 * @param items     count 条列表元数据；err != OK 时为 NULL
 * @param has_more  服务端在本页之后还有内容
 */
typedef void (*news_svc_list_cb_t)(uint32_t req_id,
                                   const struct NewsArticle *items, uint16_t count,
                                   bool has_more, news_svc_err_t err, void *user_data);

/** 正文回调。err != OK 时 content 为 NULL。 */
typedef void (*news_svc_detail_cb_t)(uint32_t req_id, const char *content,
                                     news_svc_err_t err, void *user_data);

void news_svc_init(void);
void news_svc_deinit(void);

/** 拉第 page 页（1 起）列表。 */
void news_svc_fetch_list(uint32_t req_id, uint16_t page, uint16_t page_size,
                         news_svc_list_cb_t cb, void *user_data);

/** 拉某篇正文。article_id 由本层在调用期间读完，不持有。 */
void news_svc_fetch_detail(uint32_t req_id, const char *article_id,
                           news_svc_detail_cb_t cb, void *user_data);

/** 取消。已发出的请求丢弃结果，不再回调。 */
void news_svc_abort(uint32_t req_id);

/* ── 仅 mock 实现提供：故障注入 ────────────────────────────────────── */
/** 让下一个请求以 err 失败。传 NEWS_SVC_OK 撤销。http 实现里是空函数。 */
void news_svc_mock_inject(news_svc_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* NEWS_SVC_H */
