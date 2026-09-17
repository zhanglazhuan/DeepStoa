/**
 * @file news_svc_mock.c
 * @brief news_svc.h 的 mock 实现 —— 本地假数据，不需要服务端
 *
 * 刻意做成"时序模拟器"而不是同步返回一坨数据：
 *   - lv_timer 延迟 700/1100ms 回调 → 验证 Loading 占位、加载更多的忙态
 *   - 3 页共 18 条，第 3 页之后 has_more = false → 验证"没有更多"的收尾
 *   - 故障注入 → 逐条走通错误码表里的每个 UI 分支（列表页长按刷新按钮触发）
 *
 * 换真实服务端：idf.py -DNEWS_SVC=http build，见 CMakeLists.txt。
 *
 * 注意：固件里只编了 Montserrat 字库，没有 CJK 字形，
 *       所以假数据一律用 ASCII，中文会渲染成方框。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl.h>
#include "esp_log.h"

#include "news_svc.h"
#include "../model.h"

static const char *TAG = "news_svc_mock";

#define MOCK_LIST_DELAY_MS    700
#define MOCK_DETAIL_DELAY_MS  1100
#define MOCK_TOTAL            18    /* 3 页 x NEWS_PAGE_SIZE */

/* ── 假数据 ───────────────────────────────────────────────────────── */

typedef struct {
    const char *title;
    const char *source;
    const char *date;
    const char *summary;
    const char *body;
} mock_item_t;

static const mock_item_t k_items[MOCK_TOTAL] = {
    { "E-ink panels finally get a usable refresh rate", "The Verge", "2026-09-01",
      "A new driving waveform cuts full-screen ghosting to a single flash, and partial updates now land under 300 ms.",
      "For years the trade-off with electronic paper was simple: you got a screen you could read in daylight for a week on one charge, and in exchange you accepted that anything moving looked like a smear.\n\nThe new waveform changes the arithmetic. By splitting the update into a short clearing pulse and a longer settling phase, panel makers report a full refresh in roughly 600 ms and a partial refresh under 300 ms, with ghosting confined to a single visible flash rather than the three or four most readers are used to." },

    { "Small language models move onto microcontrollers", "IEEE Spectrum", "2026-09-01",
      "Quantised to four bits, a 300M-parameter model now runs on a dual-core MCU with 8 MB of PSRAM.",
      "The demonstration is deliberately unglamorous: a dual-core microcontroller, 8 MB of external PSRAM, and a model small enough to fit in it after four-bit quantisation.\n\nThroughput is about two tokens per second, which is useless for chat and perfectly adequate for what the team actually built: an offline command parser that turns a spoken sentence into a structured intent without ever touching a network." },

    { "Spaced repetition, revisited", "Nature", "2026-08-31",
      "A five-year study finds review intervals matter far less than whether the learner reconstructs the answer from memory.",
      "The study followed roughly 4,000 learners across five years and a range of scheduling algorithms, from fixed intervals to the adaptive schedulers most flashcard software now ships with.\n\nThe headline result is deflationary. Differences between schedulers accounted for a small fraction of the variance in long-term retention. What dominated was whether the learner attempted to reconstruct the answer before revealing it, a behaviour no scheduler can enforce and most interfaces quietly discourage." },

    { "The quiet return of the single-purpose device", "Wired", "2026-08-31",
      "Sales of devices that do one thing are growing for the first time since 2011.",
      "The category was supposed to be extinct. The phone absorbed the camera, the music player, the map, the notebook and the alarm clock, and for a decade the only question was what it would absorb next.\n\nWhat the numbers now show is a small but consistent recovery, concentrated in devices whose selling point is what they cannot do. Buyers are not paying for capability; they are paying for the absence of a notification layer." },

    { "Why battery percentages lie", "Ars Technica", "2026-08-30",
      "Coulomb counting drifts, voltage curves are flat in the middle, and every vendor papers over it differently.",
      "A lithium cell spends most of its useful discharge sitting on a nearly flat voltage plateau, which means voltage alone tells you very little about remaining charge in the range you actually care about.\n\nCoulomb counting solves that by integrating current over time, but the integral accumulates error, so every fuel gauge periodically snaps its estimate back to a voltage-derived anchor. The visible symptom is a percentage that hangs at 40 for an hour and then drops six points at once." },

    { "A field guide to reading in direct sunlight", "The Atlantic", "2026-08-30",
      "Reflective displays win outdoors not because they are brighter but because they stop competing with the sun.",
      "An emissive display outdoors is in an arms race it cannot win: every increase in ambient light demands a matching increase in backlight, and the battery pays for both.\n\nA reflective display opts out of the race entirely. It uses the same light that was drowning the phone screen, which is why contrast on electronic paper improves as conditions get worse for everything else." },

    { "Open-source firmware for e-readers reaches 1.0", "LWN", "2026-08-29",
      "After four years the project ships a stable API for page rendering, storage and power management.",
      "The 1.0 release is less about new features than about a promise: the page rendering, storage and power-management interfaces are now frozen for the 1.x series.\n\nFor the handful of hardware vendors shipping the stack, that turns an ongoing porting cost into a one-time one. For everyone else it makes third-party applications possible for the first time." },

    { "Handwriting recognition on-device, without the cloud", "MIT Technology Review", "2026-08-29",
      "A 2 MB model handles cursive at 96% character accuracy on a mid-range MCU.",
      "The model is small enough to sit in flash and fast enough to keep up with a person writing at normal speed, which are the only two numbers that matter for the use case.\n\nAccuracy on clean print is unremarkable. The interesting result is cursive, where the reported 96% character accuracy comes close to what cloud services were delivering three years ago at several orders of magnitude more compute." },

    { "Students are using flashcards wrong, say researchers", "Science", "2026-08-28",
      "Recognition feels like learning. Recall is learning. The gap explains most failed study sessions.",
      "Recognising a correct answer among alternatives produces a strong feeling of knowing and a weak memory trace. Producing the answer from nothing produces the opposite.\n\nBecause the feeling is what learners use to decide when to stop studying, the more comfortable method reliably wins, and reliably underperforms. The practical suggestion is blunt: hide the answer until you have said yours out loud." },

    { "Deep sleep, shallow wake: the MCU power puzzle", "Hackaday", "2026-08-28",
      "Getting to 20 uA is easy. Getting back to a drawn screen in 200 ms is not.",
      "Datasheet sleep numbers are honest and almost useless, because they describe a chip with everything switched off, including the things you need on to wake up usefully.\n\nThe real budget is dominated by what you must keep in retention RAM to avoid a cold boot, and by how much of the display pipeline you have to re-initialise before the first pixel moves." },

    { "The case against infinite scroll", "The Guardian", "2026-08-27",
      "Pagination gives a reader something an endless feed cannot: an ending.",
      "Infinite scroll was adopted because it measured well. Sessions got longer, and longer sessions were the metric.\n\nThe argument against it is not that it works badly but that it works by removing the moment where a reader would otherwise decide whether to continue. A page that ends returns that decision to the person holding it." },

    { "Typography for 300 ppi reflective displays", "A List Apart", "2026-08-27",
      "Hinting matters more than antialiasing when every pixel is either black or white.",
      "On a one-bit display there is no grey to hide behind. A stem either lands on a pixel boundary or it does not, and the difference between those two cases is the difference between a crisp page and a muddy one.\n\nWhich is why hinting, a technology widely declared obsolete on high-DPI colour screens, turns out to be the single highest-leverage thing you can fix on electronic paper." },

    { "A week with a device that cannot open a browser", "Polygon", "2026-08-26",
      "Notes from seven days on hardware whose most useful feature is a missing one.",
      "The first day is irritating. The reflex to check something arrives roughly every ten minutes and finds nothing to act on.\n\nBy the fourth day the reflex has mostly stopped firing, which is either evidence that the device works or evidence about the reflex. The review does not resolve which." },

    { "Rethinking the review queue", "ACM Queue", "2026-08-26",
      "What happens when you cap the daily queue at what a learner can actually finish.",
      "An uncapped review queue is a debt instrument. Miss two days and it compounds into a backlog large enough that the rational move is to declare bankruptcy and start over.\n\nCapping the queue trades theoretical optimality for the thing that actually determines outcomes: whether the learner opens the application tomorrow." },

    { "Solar charging for always-on sensors", "EE Times", "2026-08-25",
      "Indoor photovoltaics now yield enough at 200 lux to run a duty-cycled sensor indefinitely.",
      "At 200 lux, a normally lit room rather than a window, a palm-sized indoor cell now delivers enough to keep a duty-cycled sensor node running without ever seeing a charger.\n\nThe constraint has moved from harvest to storage: the supercapacitor sized for the overnight gap is now the largest component on the board." },

    { "Why your OTA update bricked 0.3% of the fleet", "The Pragmatic Engineer", "2026-08-25",
      "Partial flash writes, brownouts, and the rollback path nobody tested.",
      "Every over-the-air update is a distributed systems problem wearing a firmware costume. The failure modes are the familiar ones: partial writes, interrupted transactions, and a recovery path that was written but never exercised.\n\nThe most useful line in the post-mortem is about the last of these. The rollback code had been in the tree for two years and had never once run on real hardware." },

    { "Reading speed on paper versus screens: the gap closes", "Psychological Science", "2026-08-24",
      "Once refresh artefacts are removed, comprehension differences fall within noise.",
      "The long-reported comprehension advantage for paper shrinks substantially once the comparison controls for display artefacts rather than for the medium.\n\nWith those controlled, the remaining difference falls inside the noise. The authors are careful to note this says nothing about devices that also deliver notifications." },

    { "Building a study device from spare parts", "Make", "2026-08-24",
      "A weekend project: an e-ink panel, an ESP32, a battery, and about forty lines of glue.",
      "The build is deliberately unambitious. A salvaged panel, a common microcontroller module, a single-cell battery, and enough code to draw a card and read a touch.\n\nWhat the author wants to demonstrate is not the hardware but the ceiling: how much of a usable study device is now available to someone with a soldering iron and a free Saturday." },
};

/* 追加在每篇正文后面，保证详情页一定长到需要滚动 */
static const char k_tail[] =
    "\n\nThis paragraph is appended by the mock data source to every article so that "
    "the detail page always has enough text to scroll. It exercises line wrapping, "
    "the scroll container, and the end-of-article marker at the bottom of the page. "
    "If you can read this sentence and the marker below it, the detail layout is "
    "doing its job.\n\n"
    "Switch to the real backend with: idf.py -DNEWS_SVC=http build";

/* ── 在途请求 ─────────────────────────────────────────────────────── */

typedef enum { REQ_LIST = 1, REQ_DETAIL } req_kind_t;

typedef struct {
    uint32_t              req_id;
    req_kind_t            kind;
    news_svc_list_cb_t    list_cb;
    news_svc_detail_cb_t  detail_cb;
    void                 *user_data;
    news_svc_err_t        err;
    lv_timer_t           *timer;
    bool                  aborted;

    /* list */
    uint16_t              page;
    uint16_t              page_size;
    /* detail */
    char                  article_id[NEWS_ID_LEN];
} mock_req_t;

/* 同时最多两个在途请求（一次列表 + 一次正文），够用且不用动态分配 */
#define MOCK_MAX_REQ 2
static mock_req_t     s_reqs[MOCK_MAX_REQ];
static news_svc_err_t s_injected = NEWS_SVC_OK;

static mock_req_t *alloc_req(void)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer == NULL) return &s_reqs[i];
    }
    return NULL;
}

static mock_req_t *find_req(uint32_t req_id)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer && s_reqs[i].req_id == req_id) return &s_reqs[i];
    }
    return NULL;
}

static news_svc_err_t take_injected(void)
{
    news_svc_err_t e = s_injected;
    s_injected = NEWS_SVC_OK;
    return e;
}

/* mock 的 id 是 "n01".."n18"，和 server/news 的编号规则保持一致 */
static int id_to_index(const char *id)
{
    int n = 0;
    if (!id || id[0] != 'n') return -1;
    if (sscanf(id + 1, "%d", &n) != 1) return -1;
    if (n < 1 || n > MOCK_TOTAL) return -1;
    return n - 1;
}

static void fill_article(NewsArticle *out, int idx)
{
    const mock_item_t *it = &k_items[idx];
    memset(out, 0, sizeof(*out));
    snprintf(out->id,      sizeof(out->id),      "n%02d", idx + 1);
    snprintf(out->title,   sizeof(out->title),   "%s", it->title);
    snprintf(out->source,  sizeof(out->source),  "%s", it->source);
    snprintf(out->date,    sizeof(out->date),    "%s", it->date);
    snprintf(out->summary, sizeof(out->summary), "%s", it->summary);
}

/* ── 回调派发（lv_timer 回调天然在 LVGL 线程 —— 契约 1 自动满足） ──── */

static void fire_list(mock_req_t *r)
{
    news_svc_list_cb_t cb = r->list_cb;
    uint32_t  req_id = r->req_id;
    void     *ud     = r->user_data;
    news_svc_err_t err = r->err;
    uint16_t  page = r->page, page_size = r->page_size;

    memset(r, 0, sizeof(*r));

    if (err != NEWS_SVC_OK) {
        cb(req_id, NULL, 0, false, err, ud);
        return;
    }

    if (page_size == 0 || page_size > NEWS_PAGE_SIZE) page_size = NEWS_PAGE_SIZE;

    /* 2.5 KB，别放 LVGL 任务栈上；同时只会有一个列表请求在派发 */
    static NewsArticle page_buf[NEWS_PAGE_SIZE];
    uint16_t n = 0;
    int start = (page >= 1) ? (int)(page - 1) * page_size : 0;
    for (int i = start; i < MOCK_TOTAL && n < page_size; i++) {
        fill_article(&page_buf[n++], i);
    }
    bool has_more = (start + n) < MOCK_TOTAL;

    ESP_LOGI(TAG, "list page=%u -> %u items, has_more=%d",
             (unsigned)page, (unsigned)n, (int)has_more);
    cb(req_id, page_buf, n, has_more, NEWS_SVC_OK, ud);
}

static void fire_detail(mock_req_t *r)
{
    news_svc_detail_cb_t cb = r->detail_cb;
    uint32_t  req_id = r->req_id;
    void     *ud     = r->user_data;
    news_svc_err_t err = r->err;
    char      id[NEWS_ID_LEN];
    snprintf(id, sizeof(id), "%s", r->article_id);

    memset(r, 0, sizeof(*r));

    int idx = id_to_index(id);
    if (err == NEWS_SVC_OK && idx < 0) err = NEWS_SVC_ERR_BAD_REPLY;
    if (err != NEWS_SVC_OK) {
        cb(req_id, NULL, err, ud);
        return;
    }

    /* 正文可能接近 4 KB，别放栈上 */
    char *buf = malloc(NEWS_CONTENT_LEN);
    if (!buf) {
        cb(req_id, NULL, NEWS_SVC_ERR_BUSY, ud);
        return;
    }
    snprintf(buf, NEWS_CONTENT_LEN, "%s%s", k_items[idx].body, k_tail);
    cb(req_id, buf, NEWS_SVC_OK, ud);
    free(buf);
}

static void mock_fire_cb(lv_timer_t *t)
{
    mock_req_t *r = lv_timer_get_user_data(t);

    lv_timer_delete(t);
    r->timer = NULL;

    if (r->aborted) {           /* 契约 2 的唯一例外：被 abort 的请求不回调 */
        ESP_LOGI(TAG, "req %lu aborted, dropping result", (unsigned long)r->req_id);
        memset(r, 0, sizeof(*r));
        return;
    }

    if (r->kind == REQ_LIST) fire_list(r);
    else                     fire_detail(r);
}

static void schedule(mock_req_t *r, uint32_t delay_ms)
{
    r->timer = lv_timer_create(mock_fire_cb, delay_ms, r);
    lv_timer_set_repeat_count(r->timer, 1);
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

void news_svc_init(void)
{
    memset(s_reqs, 0, sizeof(s_reqs));
    s_injected = NEWS_SVC_OK;
    ESP_LOGI(TAG, "mock news source ready (%d articles, %d per page)",
             MOCK_TOTAL, NEWS_PAGE_SIZE);
}

void news_svc_deinit(void)
{
    for (int i = 0; i < MOCK_MAX_REQ; i++) {
        if (s_reqs[i].timer) lv_timer_delete(s_reqs[i].timer);
    }
    memset(s_reqs, 0, sizeof(s_reqs));
}

void news_svc_fetch_list(uint32_t req_id, uint16_t page, uint16_t page_size,
                         news_svc_list_cb_t cb, void *user_data)
{
    if (!cb) return;

    mock_req_t *r = alloc_req();
    if (!r) {   /* 契约 2：内部资源不足也必须回调 */
        cb(req_id, NULL, 0, false, NEWS_SVC_ERR_BUSY, user_data);
        return;
    }

    memset(r, 0, sizeof(*r));
    r->req_id    = req_id;
    r->kind      = REQ_LIST;
    r->list_cb   = cb;
    r->user_data = user_data;
    r->page      = page;
    r->page_size = page_size;
    r->err       = take_injected();

    /* mock 刻意不检查 wifi_is_connected()：调试时设备常常没连网，
     * 在这里硬拦就永远看不到列表。想验证离线分支用 news_svc_mock_inject()。
     * 真实实现 news_svc_http.c 里这个检查是必须有的。 */
    schedule(r, MOCK_LIST_DELAY_MS);
}

void news_svc_fetch_detail(uint32_t req_id, const char *article_id,
                           news_svc_detail_cb_t cb, void *user_data)
{
    if (!cb) return;

    mock_req_t *r = alloc_req();
    if (!r) {
        cb(req_id, NULL, NEWS_SVC_ERR_BUSY, user_data);
        return;
    }

    memset(r, 0, sizeof(*r));
    r->req_id    = req_id;
    r->kind      = REQ_DETAIL;
    r->detail_cb = cb;
    r->user_data = user_data;
    r->err       = take_injected();
    snprintf(r->article_id, sizeof(r->article_id), "%s", article_id ? article_id : "");

    schedule(r, MOCK_DETAIL_DELAY_MS);
}

void news_svc_abort(uint32_t req_id)
{
    mock_req_t *r = find_req(req_id);
    if (!r) return;
    r->aborted = true;
    ESP_LOGI(TAG, "abort req=%lu", (unsigned long)req_id);
}

void news_svc_mock_inject(news_svc_err_t err)
{
    s_injected = err;
    ESP_LOGW(TAG, "next request will report err=%d", (int)err);
}
