#include <string.h>
#include <stdlib.h>
#include <lvgl.h>
#include "esp_log.h"

#include "audio_service.h"
#include "app_event.h"
#include "flash_control.h"   /* g_kvdb */

extern struct fdb_kvdb g_kvdb;

static const char *TAG = "audio_service";

/* ── 持久化 ───────────────────────────────────────────────────────────
 * 写入时机是关键：绝不能每秒写一次 flash（会磨损，而且这个 128KB 分区是
 * 和 todolist / anki / chatbot 等共用的）。策略是
 *   - 状态变化时写：暂停、停止、切歌、队列增删、音量变化。这些本来就不频繁。
 *   - 播放中每 RESUME_CHECKPOINT_MS 兜底写一次，防止硬断电丢太多进度。
 * 代价是硬断电最多丢这个间隔的进度，换来写入频率降到可以忽略。 */
#define KV_AUDIO_STATE          "au_state"
#define AUDIO_STORE_VERSION     2
#define RESUME_CHECKPOINT_MS    120000u

typedef struct {
    uint16_t      version;
    uint16_t      count;
    int16_t       pos;
    uint8_t       volume;
    uint8_t       _pad;
    uint32_t      position_ms;
    uint32_t      duration_ms;
    uint8_t       mode;
    uint8_t       _pad2[3];
    audio_track_t queue[AUDIO_QUEUE_MAX];
} audio_store_blob_t;

#define MAX_OBSERVERS 4

static audio_track_t s_queue[AUDIO_QUEUE_MAX];
static uint16_t      s_count;
static int16_t       s_pos = -1;
static audio_state_t s_state = AUDIO_STATE_STOPPED;
static bool          s_inited;

/* 连续打开失败的次数。用来在"整个队列都是坏文件"时停下来，
 * 而不是无限地跳到下一首。 */
static uint16_t      s_fail_streak;

/* 从 flash 恢复出来、等着在下一次 open 成功后应用的位置。
 * 配套的时长也要留着，这样进度条在按播放之前就能显示正确的比例。 */
static uint32_t      s_pending_resume_ms;
static uint32_t      s_pending_duration_ms;

static audio_mode_t  s_mode = AUDIO_MODE_SEQUENTIAL;

/* 随机模式用一个"队列下标的排列"来保证一轮之内不重复播到同一首，
 * 而不是每次随机抽一个（那样会明显地重复）。一轮放完重新洗牌。 */
static uint8_t       s_shuffle[AUDIO_QUEUE_MAX];
static uint16_t      s_shuffle_idx;
static lv_timer_t   *s_ckpt_timer;

static struct {
    audio_observer_t cb;
    void            *user_data;
} s_obs[MAX_OBSERVERS];

/* ── 随机序 ───────────────────────────────────────────────────────── */

static void shuffle_rebuild(void)
{
    for (uint16_t i = 0; i < s_count; i++) s_shuffle[i] = (uint8_t)i;
    /* Fisher-Yates */
    for (uint16_t i = s_count; i > 1; i--) {
        uint16_t j = (uint16_t)(rand() % i);
        uint8_t t = s_shuffle[i - 1];
        s_shuffle[i - 1] = s_shuffle[j];
        s_shuffle[j] = t;
    }
    /* 让当前曲目对齐到排列里的位置，下一首才接得上 */
    s_shuffle_idx = 0;
    for (uint16_t i = 0; i < s_count; i++) {
        if (s_shuffle[i] == (uint8_t)s_pos) { s_shuffle_idx = i; break; }
    }
}

/* ── 持久化实现 ───────────────────────────────────────────────────── */

static void store_save(void)
{
    static audio_store_blob_t blob;   /* 7KB，别放栈上 */
    memset(&blob, 0, sizeof(blob));

    blob.version = AUDIO_STORE_VERSION;
    blob.count   = s_count;
    blob.pos     = s_pos;
    blob.volume  = audio_out_volume();
    blob.mode    = (uint8_t)s_mode;
    /* 播放中记当前位置；停止态则把还没用掉的续播位置原样留住，
     * 否则用户没按播放就关机，下次进度会被清成 0。 */
    if (s_state == AUDIO_STATE_STOPPED && s_pending_resume_ms) {
        blob.position_ms = s_pending_resume_ms;
        blob.duration_ms = s_pending_duration_ms;
    } else {
        blob.position_ms = audio_out_position_ms();
        blob.duration_ms = audio_out_duration_ms();
    }
    memcpy(blob.queue, s_queue, sizeof(blob.queue));

    struct fdb_blob b;
    fdb_err_t err = fdb_kv_set_blob(&g_kvdb, KV_AUDIO_STATE,
                                    fdb_blob_make(&b, &blob, sizeof(blob)));
    if (err != FDB_NO_ERR) {
        ESP_LOGE(TAG, "checkpoint failed: %d", (int)err);
        return;
    }
    ESP_LOGD(TAG, "checkpoint: %u tracks pos=%d %lums",
             blob.count, (int)blob.pos, (unsigned long)blob.position_ms);
}

static void store_load(void)
{
    static audio_store_blob_t blob;
    memset(&blob, 0, sizeof(blob));

    struct fdb_blob b;
    size_t read = fdb_kv_get_blob(&g_kvdb, KV_AUDIO_STATE,
                                  fdb_blob_make(&b, &blob, sizeof(blob)));
    if (read != sizeof(blob) || blob.version != AUDIO_STORE_VERSION) {
        ESP_LOGI(TAG, "no saved session (first run or format change)");
        return;
    }

    if (blob.count > AUDIO_QUEUE_MAX) blob.count = AUDIO_QUEUE_MAX;
    memcpy(s_queue, blob.queue, sizeof(s_queue));
    s_count = blob.count;
    s_pos   = (blob.pos >= 0 && blob.pos < (int16_t)s_count)
              ? blob.pos : (s_count ? 0 : -1);
    audio_out_set_volume(blob.volume ? blob.volume : 60);
    s_mode = (blob.mode < AUDIO_MODE_COUNT) ? (audio_mode_t)blob.mode
                                            : AUDIO_MODE_SEQUENTIAL;

    /* 恢复成停止态 + 一个待应用的位置，不自动开始播放。
     * 文件可能已经不在了（换过 SD 卡），所以不在这里打开 ——
     * 等用户按播放时 open 失败会走"坏文件自动跳过"那条路。 */
    s_state = AUDIO_STATE_STOPPED;
    s_pending_resume_ms   = blob.position_ms;
    s_pending_duration_ms = blob.duration_ms;

    ESP_LOGI(TAG, "restored %u tracks, pos=%d, resume@%lu/%lums",
             s_count, (int)s_pos,
             (unsigned long)s_pending_resume_ms,
             (unsigned long)s_pending_duration_ms);
}

void audio_service_checkpoint(void)
{
    if (s_inited) store_save();
}

/* 播放中的兜底存点。暂停 / 停止 / 切歌本来就已经写过了，这里只管 PLAYING。 */
static void ckpt_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state == AUDIO_STATE_PLAYING) store_save();
}

/* ── 观察者 ───────────────────────────────────────────────────────── */

void audio_service_subscribe(audio_observer_t cb, void *user_data)
{
    if (!cb) return;
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s_obs[i].cb == cb) { s_obs[i].user_data = user_data; return; }
    }
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (!s_obs[i].cb) {
            s_obs[i].cb = cb;
            s_obs[i].user_data = user_data;
            return;
        }
    }
    ESP_LOGW(TAG, "observer table full");
}

void audio_service_unsubscribe(audio_observer_t cb)
{
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s_obs[i].cb == cb) { s_obs[i].cb = NULL; s_obs[i].user_data = NULL; }
    }
}

static void notify(audio_event_t ev, audio_err_t err)
{
    /* 快照一份再分发：回调里可能会 unsubscribe */
    audio_observer_t cbs[MAX_OBSERVERS];
    void            *uds[MAX_OBSERVERS];
    int n = 0;
    for (int i = 0; i < MAX_OBSERVERS; i++) {
        if (s_obs[i].cb) { cbs[n] = s_obs[i].cb; uds[n] = s_obs[i].user_data; n++; }
    }
    for (int i = 0; i < n; i++) cbs[i](ev, err, uds[i]);
}

const char *audio_err_text(audio_err_t err)
{
    switch (err) {
    case AUDIO_ERR_INIT:      return "Speaker unavailable";
    case AUDIO_ERR_NOT_FOUND: return "File not found - it may have been deleted or the SD card removed";
    case AUDIO_ERR_FORMAT:    return "Unsupported audio format";
    case AUDIO_ERR_CORRUPT:   return "File is damaged and cannot be decoded";
    case AUDIO_ERR_IO:        return "Error reading file";
    default:                  return "Playback error";
    }
}

/* ── 内部：打开队列当前位置 ───────────────────────────────────────── */

static audio_err_t open_current(void)
{
    if (s_pos < 0 || s_pos >= (int16_t)s_count) {
        s_state = AUDIO_STATE_STOPPED;
        return AUDIO_ERR_NOT_FOUND;
    }

    uint32_t dur = 0;
    audio_err_t err = audio_out_open(s_queue[s_pos].path, &dur);
    if (err != AUDIO_OK) {
        s_state = AUDIO_STATE_STOPPED;
        return err;
    }
    /* 续播：恢复出来的位置在第一次成功打开后应用一次 */
    if (s_pending_resume_ms > 0) {
        uint32_t target = s_pending_resume_ms;
        s_pending_resume_ms   = 0;
        s_pending_duration_ms = 0;
        if (target < dur) {
            audio_out_seek(target);
            ESP_LOGI(TAG, "resumed at %lums", (unsigned long)target);
        }
    }

    s_state = AUDIO_STATE_PLAYING;
    s_fail_streak = 0;
    return AUDIO_OK;
}

/* 播放指定队列位置。坏文件自动跳过，但整队都坏时会停下来。
 * dir 只用于"跳过坏文件时往哪个方向继续"。 */
static void play_at(int16_t pos, int dir)
{
    if (s_count == 0) return;
    if (pos < 0) pos = (int16_t)(s_count - 1);
    if (pos >= (int16_t)s_count) pos = 0;
    s_pos = pos;
    /* 换曲从头放，不沿用上一首的续播位置 */
    s_pending_resume_ms   = 0;
    s_pending_duration_ms = 0;

    audio_out_stop();
    audio_err_t err = open_current();

    if (err != AUDIO_OK) {
        s_fail_streak++;
        if (s_fail_streak < s_count) {
            ESP_LOGW(TAG, "open failed (%d), skipping to next", (int)err);
            play_at((int16_t)(s_pos + dir), dir);   /* 自动跳过坏文件 */
            return;
        }
        /* 整个队列都打不开：停下并报错，避免无限跳 */
        ESP_LOGE(TAG, "all %u tracks failed to open", s_count);
        s_fail_streak = 0;
        notify(AUDIO_EV_ERROR, err);
    }
    store_save();
    notify(AUDIO_EV_TRACK_CHANGED, AUDIO_OK);
}

/* 用户手动切歌：任何模式下都老老实实换一首（单曲循环也不例外），
 * 只有随机模式按洗好的顺序走。 */
static void step_manual(int delta)
{
    if (s_count == 0) return;

    if (s_mode == AUDIO_MODE_SHUFFLE && s_count > 1) {
        if (s_shuffle[s_shuffle_idx] != (uint8_t)s_pos) shuffle_rebuild();
        int32_t i = (int32_t)s_shuffle_idx + delta;
        if (i < 0)                     i = (int32_t)s_count - 1;
        if (i >= (int32_t)s_count)   { shuffle_rebuild(); i = 0; }
        s_shuffle_idx = (uint16_t)i;
        play_at((int16_t)s_shuffle[s_shuffle_idx], delta >= 0 ? 1 : -1);
        return;
    }
    play_at((int16_t)((s_pos < 0 ? 0 : s_pos) + delta), delta >= 0 ? 1 : -1);
}

/* 一首自然放完之后去哪 —— 这里才是播放模式真正起作用的地方 */
static void advance_auto(void)
{
    if (s_count == 0) return;

    switch (s_mode) {
    case AUDIO_MODE_REPEAT_ONE:
        s_pending_resume_ms = 0;
        play_at(s_pos, 1);              /* 原地重放 */
        return;

    case AUDIO_MODE_SHUFFLE:
        step_manual(+1);
        return;

    case AUDIO_MODE_REPEAT_ALL:
        step_manual(+1);
        return;

    case AUDIO_MODE_SEQUENTIAL:
    default:
        if (s_pos + 1 >= (int16_t)s_count) {
            /* 顺序模式放完最后一首就停，不绕回开头 */
            ESP_LOGI(TAG, "queue finished (sequential)");
            audio_out_stop();
            s_state = AUDIO_STATE_STOPPED;
            store_save();
            notify(AUDIO_EV_STATE_CHANGED, AUDIO_OK);
            return;
        }
        step_manual(+1);
        return;
    }
}

/* ── audio_out 回调（保证在 LVGL 线程）────────────────────────────── */

static void on_eot(void *user_data)
{
    (void)user_data;
    ESP_LOGI(TAG, "track finished");
    advance_auto();
}

static void on_err(audio_err_t err, void *user_data)
{
    (void)user_data;
    ESP_LOGW(TAG, "playback error %d", (int)err);
    s_state = AUDIO_STATE_STOPPED;
    notify(AUDIO_EV_STATE_CHANGED, AUDIO_OK);
    notify(AUDIO_EV_ERROR, err);
}

/* ── 物理媒体键 ───────────────────────────────────────────────────────
 * 播放会话现在是系统级的，所以媒体键理应在任何界面下都生效，
 * 而不是只有播放器 app 打开时才管用。
 *
 * app_event 只有 register 没有 unregister —— 对 UI 来说这是坑，
 * 但对一个永不销毁的系统服务来说正合适。
 *
 * 目前 system/input 还没进构建（它 include 的 leisound_v1.h 不在本仓库），
 * 所以这三个事件暂时没人 fire。按键电路到位后只要让 input 层
 * app_event_fire(APP_EVENT_KEY_*) 即可，本文件不用改。 */
static void on_app_event(app_event_t event, const void *data)
{
    (void)data;
    switch (event) {
    case APP_EVENT_KEY_PLAY_PAUSE: audio_service_toggle();      break;
    case APP_EVENT_KEY_VOL_UP:     audio_service_volume_up();   break;
    case APP_EVENT_KEY_VOL_DOWN:   audio_service_volume_down(); break;
    default: break;
    }
}

/* ── 生命周期 ─────────────────────────────────────────────────────── */

esp_err_t audio_service_init(void)
{
    if (s_inited) return ESP_OK;

    memset(s_queue, 0, sizeof(s_queue));
    memset(s_obs, 0, sizeof(s_obs));
    s_count = 0;
    s_pos   = -1;
    s_state = AUDIO_STATE_STOPPED;
    s_fail_streak = 0;

    esp_err_t ret = audio_out_init();
    audio_out_set_callbacks(on_eot, on_err, NULL);

    s_inited = true;
    store_load();
    app_event_register(on_app_event);
    s_ckpt_timer = lv_timer_create(ckpt_timer_cb, RESUME_CHECKPOINT_MS, NULL);
    ESP_LOGI(TAG, "audio service ready (out=%s)",
             audio_out_is_available() ? "available" : "unavailable");
    return ret;
}

void audio_service_deinit(void)
{
    store_save();
    if (s_ckpt_timer) { lv_timer_delete(s_ckpt_timer); s_ckpt_timer = NULL; }
    audio_out_stop();
    audio_out_deinit();
    memset(s_obs, 0, sizeof(s_obs));
    s_inited = false;
}

/* ── 队列 ─────────────────────────────────────────────────────────── */

void audio_service_clear_queue(void)
{
    audio_out_stop();
    memset(s_queue, 0, sizeof(s_queue));
    s_count = 0;
    s_pos   = -1;
    s_state = AUDIO_STATE_STOPPED;
    s_pending_resume_ms   = 0;
    s_pending_duration_ms = 0;
    store_save();
    notify(AUDIO_EV_QUEUE_CHANGED, AUDIO_OK);
}

bool audio_service_queue_contains(const char *path)
{
    if (!path) return false;
    for (uint16_t i = 0; i < s_count; i++) {
        if (strcmp(s_queue[i].path, path) == 0) return true;
    }
    return false;
}

bool audio_service_enqueue(const char *path, const char *title)
{
    if (!path || s_count >= AUDIO_QUEUE_MAX) return false;
    if (audio_service_queue_contains(path)) return false;

    audio_track_t *t = &s_queue[s_count];
    strncpy(t->path, path, AUDIO_PATH_MAX - 1);
    t->path[AUDIO_PATH_MAX - 1] = '\0';
    strncpy(t->title, title ? title : path, AUDIO_TITLE_MAX - 1);
    t->title[AUDIO_TITLE_MAX - 1] = '\0';
    s_count++;

    if (s_pos < 0) s_pos = 0;
    if (s_mode == AUDIO_MODE_SHUFFLE) shuffle_rebuild();
    store_save();
    notify(AUDIO_EV_QUEUE_CHANGED, AUDIO_OK);
    return true;
}

uint16_t audio_service_queue_count(void) { return s_count; }
int16_t  audio_service_queue_pos(void)   { return s_pos; }

const audio_track_t *audio_service_queue_at(uint16_t idx)
{
    return (idx < s_count) ? &s_queue[idx] : NULL;
}

bool audio_service_remove_at(uint16_t idx)
{
    if (idx >= s_count) return false;

    bool was_current = (s_pos == (int16_t)idx);

    memmove(&s_queue[idx], &s_queue[idx + 1],
            sizeof(audio_track_t) * (s_count - idx - 1));
    s_count--;
    memset(&s_queue[s_count], 0, sizeof(audio_track_t));

    if (s_pos > (int16_t)idx) s_pos--;
    if (s_pos >= (int16_t)s_count) s_pos = s_count ? (int16_t)(s_count - 1) : -1;

    if (was_current) {
        audio_out_stop();
        s_state = AUDIO_STATE_STOPPED;
        notify(AUDIO_EV_TRACK_CHANGED, AUDIO_OK);
    }
    if (s_mode == AUDIO_MODE_SHUFFLE) shuffle_rebuild();
    store_save();
    notify(AUDIO_EV_QUEUE_CHANGED, AUDIO_OK);
    return true;
}

/* ── 控制 ─────────────────────────────────────────────────────────── */

audio_err_t audio_service_play_index(uint16_t idx)
{
    if (idx >= s_count) return AUDIO_ERR_NOT_FOUND;
    if (!audio_out_is_available()) return AUDIO_ERR_INIT;

    s_pos = (int16_t)idx;
    audio_out_stop();
    audio_err_t err = open_current();

    notify(AUDIO_EV_TRACK_CHANGED, AUDIO_OK);
    if (err != AUDIO_OK) notify(AUDIO_EV_ERROR, err);
    return err;
}

audio_err_t audio_service_play_single(const char *path, const char *title)
{
    if (!path) return AUDIO_ERR_NOT_FOUND;
    if (!audio_out_is_available()) return AUDIO_ERR_INIT;

    audio_out_stop();
    memset(s_queue, 0, sizeof(s_queue));
    s_count = 0;
    s_pos   = -1;

    if (!audio_service_enqueue(path, title)) return AUDIO_ERR_NOT_FOUND;
    s_pos = 0;

    audio_err_t err = open_current();
    notify(AUDIO_EV_TRACK_CHANGED, AUDIO_OK);
    if (err != AUDIO_OK) notify(AUDIO_EV_ERROR, err);
    return err;
}

void audio_service_toggle(void)
{
    if (s_state == AUDIO_STATE_PLAYING) {
        audio_out_pause();
        s_state = AUDIO_STATE_PAUSED;
    } else if (s_state == AUDIO_STATE_PAUSED) {
        audio_out_resume();
        s_state = AUDIO_STATE_PLAYING;
    } else {
        audio_err_t err = open_current();
        if (err != AUDIO_OK) {
            notify(AUDIO_EV_ERROR, err);
            notify(AUDIO_EV_STATE_CHANGED, AUDIO_OK);
            return;
        }
    }
    ESP_LOGI(TAG, "state -> %d", (int)s_state);
    store_save();
    notify(AUDIO_EV_STATE_CHANGED, AUDIO_OK);
}

void audio_service_next(void) { step_manual(+1); }
void audio_service_prev(void) { step_manual(-1); }

void audio_service_stop(void)
{
    audio_out_stop();
    s_state = AUDIO_STATE_STOPPED;
    store_save();
    notify(AUDIO_EV_STATE_CHANGED, AUDIO_OK);
}

void audio_service_seek_to(uint32_t pos_ms)
{
    uint32_t dur = audio_out_duration_ms();
    if (dur == 0) return;
    if (pos_ms > dur) pos_ms = dur;
    audio_out_seek(pos_ms);
}

void audio_service_seek_by(int32_t delta_ms)
{
    uint32_t dur = audio_out_duration_ms();
    if (dur == 0) return;

    int64_t pos = (int64_t)audio_out_position_ms() + delta_ms;
    if (pos < 0) pos = 0;
    if (pos > (int64_t)dur) pos = dur;
    audio_service_seek_to((uint32_t)pos);
}

/* ── 查询 ─────────────────────────────────────────────────────────── */

audio_state_t audio_service_state(void) { return s_state; }

/* 还没按播放时返回从 flash 恢复出来的值 —— 这样一进播放页就能看到
 * "上次听到 01:23 / 03:47"，而不是从 00:00 开始。 */
uint32_t audio_service_position_ms(void)
{
    if (s_state == AUDIO_STATE_STOPPED && s_pending_resume_ms) return s_pending_resume_ms;
    return audio_out_position_ms();
}

uint32_t audio_service_duration_ms(void)
{
    if (s_state == AUDIO_STATE_STOPPED && s_pending_duration_ms) return s_pending_duration_ms;
    return audio_out_duration_ms();
}
bool          audio_service_is_available(void){ return audio_out_is_available(); }

/* ── 播放模式 ─────────────────────────────────────────────────────── */

const char *audio_mode_text(audio_mode_t mode)
{
    switch (mode) {
    case AUDIO_MODE_REPEAT_ALL: return "Repeat All";
    case AUDIO_MODE_REPEAT_ONE: return "Repeat One";
    case AUDIO_MODE_SHUFFLE:    return "Shuffle";
    default:                    return "In Order";
    }
}

audio_mode_t audio_service_mode(void) { return s_mode; }

void audio_service_set_mode(audio_mode_t mode)
{
    if (mode >= AUDIO_MODE_COUNT) mode = AUDIO_MODE_SEQUENTIAL;
    if (mode == s_mode) return;

    s_mode = mode;
    if (s_mode == AUDIO_MODE_SHUFFLE) shuffle_rebuild();

    ESP_LOGI(TAG, "mode -> %s", audio_mode_text(s_mode));
    store_save();
    notify(AUDIO_EV_OPTION_CHANGED, AUDIO_OK);
}

void audio_service_cycle_mode(void)
{
    audio_service_set_mode((audio_mode_t)((s_mode + 1) % AUDIO_MODE_COUNT));
}

/* ── 音量 ─────────────────────────────────────────────────────────── */

void audio_service_set_volume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    if (vol == audio_out_volume()) return;

    audio_out_set_volume(vol);
    store_save();
    notify(AUDIO_EV_OPTION_CHANGED, AUDIO_OK);
}

uint8_t audio_service_volume(void) { return audio_out_volume(); }

void audio_service_volume_up(void)
{
    uint8_t v = audio_out_volume();
    audio_service_set_volume((uint8_t)(v + AUDIO_VOLUME_STEP > 100
                                       ? 100 : v + AUDIO_VOLUME_STEP));
}

void audio_service_volume_down(void)
{
    uint8_t v = audio_out_volume();
    audio_service_set_volume((uint8_t)(v < AUDIO_VOLUME_STEP
                                       ? 0 : v - AUDIO_VOLUME_STEP));
}

const audio_track_t *audio_service_current(void)
{
    if (s_pos < 0 || s_pos >= (int16_t)s_count) return NULL;
    return &s_queue[s_pos];
}
