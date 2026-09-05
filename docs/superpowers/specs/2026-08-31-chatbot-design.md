# AI ChatBot 设计（2026-08-31）

墨水屏联网设备上的语音对话应用。本文覆盖：交互模型、数据模型、驱动接口边界、
异常处理规范、mock 策略、分阶段落地。

**当前阶段约束**：物理按键电路和 I2S 麦克风电路都还没有。本阶段只调 UI 交互，
音频与云端全部走 mock，但接口按真实驱动的语义定义好，硬件到位后只补实现、
不改 controller 和 view。

---

## 1. 现状盘点

`apps/chatbot/` 共 684 行，是个可运行的骨架。已经有的：

- `view_main.c` — 聊天页、左右气泡、header 汉堡菜单 + 底部弹窗（New / History / Cancel）
- `view_history.c` — 历史页壳子
- `model.c` — `chatbot_model_add_message()` / `clear_history()`
- `controller.c` — 录音按钮的 PRESSED/RELEASED、免提开关

必须正视的问题（这些直接决定了改造范围）：

| 问题 | 位置 | 说明 |
|---|---|---|
| 消息不进 model | `controller.c:15` | `process_audio_and_fetch_ai_reply()` 直接调 `chatbot_view_add_chat_bubble()`，`chatbot_model_add_message()` 全项目无人调用 → 消息只存在于 LVGL 对象树里，切页就没了 |
| 无多会话 | `model.h:29` | `ChatbotModel` 就是一个会话，History 页没有可展示的数据结构 |
| 无持久化 | — | 没有 `storage.c`，其他 app 都有 |
| 消息无状态无时间 | `model.h:24` | `ChatMessage` 只有 `role` + `text`，无法表达"识别中/失败/可重试"，也没有时间戳 |
| 菜单空实现 | `controller.c:61` | `chatbot_controller_menu_handler()` 是空的 |
| New Chatbot 被注释 | `view_main.c:25` | `chatbot_controller_clear_history(app)` 注释掉了 |
| 免提模式语义不清 | `controller.c:65` | `RECORD_STATE_HANDS_FREE` 与按住说话互斥，但没有实际行为 |

可用的底层（都已在构建里）：

- `wifi_manager`（连接/扫描/状态回调）、`app_event` 事件总线
- FlashDB `g_kvdb`、`lv_page` / `lv_toast` / `lv_bottom_sheet` / `lv_keyboard` / `lv_tab`
- IDF 组件 `esp_http_client`、`json`（cJSON）、`esp_driver_i2s` 已编译进来，尚未使用

**不可用的**（本阶段刻意留空）：

- 音频：全项目 grep `i2s_|microphone|opus|adpcm` → **零命中**，没有任何音频代码
- 物理按键：`system/input/` 没有加进根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS`，
  而且它 `#include "leisound_v1.h"` —— 这个头文件在本仓库不存在（是另一个产品的板级定义）。
  即使加进构建也编不过。

---

## 2. 分层与接口边界

```
  view (LVGL)          只画，不认识音频和网络
      ↕ 事件 / 刷新
  controller           唯一的状态机，驱动 model 和 view
      ↓ 抽象接口（本文档的核心）
  ┌────────────────┬──────────────────┐
  │ chatbot_audio  │  chatbot_svc     │  ← 各有 mock / real 两份实现
  └────────────────┴──────────────────┘
      ↓ 后期接入          ↓ 后期接入
   I2S mic driver     esp_http_client + cJSON
```

建议目录：

```
apps/chatbot/service/
  chatbot_audio.h          接口（现在就定稿）
  chatbot_audio_mock.c     本阶段实现
  chatbot_audio_i2s.c      阶段 2 补
  chatbot_svc.h            接口（现在就定稿）
  chatbot_svc_mock.c       本阶段实现
  chatbot_svc_http.c       阶段 1 补
```

用 Kconfig 或一个 `chatbot_config.h` 的宏在 mock / real 之间二选一编译，
**两份实现共用同一个头文件，controller 不感知区别。**

### 2.1 全局契约（两个接口都适用）

1. **所有回调必须在 LVGL 线程被调用。** 真实实现跑在 I2S 任务 / HTTP 任务里，
   回调前必须 `lv_async_call()` 弹回 UI 线程 —— 在别的线程改 LVGL 对象会崩。
   这条写死在头文件注释里，接驱动的人才不会踩。
2. **每个异步操作必定回调一次**，成功或失败都要回。内部自带超时兜底，
   不允许出现"不回调"的情况 —— 否则状态机会永久卡在中间态。
3. **回调里传的指针只在回调期间有效**，controller 必须当场拷走。

### 2.2 接口一：音频采集 `chatbot_audio.h`

```c
typedef enum {
    CHATBOT_AUDIO_OK = 0,
    CHATBOT_AUDIO_ERR_INIT,       /* 麦克风/I2S 初始化失败，设备级不可用 */
    CHATBOT_AUDIO_ERR_TOO_SHORT,  /* 时长 < CHATBOT_REC_MIN_MS */
    CHATBOT_AUDIO_ERR_SILENT,     /* 全程能量低于阈值 */
    CHATBOT_AUDIO_ERR_OVERFLOW,   /* 缓冲区溢出 / DMA 丢帧 / 内存不足 */
} chatbot_audio_err_t;

typedef struct {
    const void *pcm;          /* 16kHz 16bit mono；mock 实现返回 NULL */
    size_t      len;
    uint32_t    duration_ms;
    uint8_t     peak_level;   /* 0-100，供 UI 显示音量格 */
} chatbot_audio_clip_t;

/* 录音结束回调（正常或异常）。保证在 LVGL 线程调用。 */
typedef void (*chatbot_audio_done_cb_t)(const chatbot_audio_clip_t *clip,
                                        chatbot_audio_err_t err,
                                        void *user_data);

esp_err_t chatbot_audio_init(void);
bool      chatbot_audio_is_available(void);  /* false → UI 禁用录音 + L3 横幅 */
esp_err_t chatbot_audio_start(void);
esp_err_t chatbot_audio_stop(chatbot_audio_done_cb_t cb, void *user_data);
void      chatbot_audio_cancel(void);        /* 丢弃，不回调 */
uint32_t  chatbot_audio_elapsed_ms(void);    /* UI 1Hz 轮询显示计时 */
uint8_t   chatbot_audio_level(void);         /* 0-100，UI 画离散音量格 */
```

Mock 实现：`is_available()` 返回 true；`start()` 记时间戳；`stop()` 按真实按住时长
构造 clip（`pcm = NULL`）后回调；`level()` 返回一个随时间变化的伪随机值让音量格动起来。
再加一个故障注入开关（见 §6）。

### 2.3 接口二：录音触发（物理按键的替身）

**把"谁触发"和"怎么采集"分开**，这是物理按键能无痛接入的关键。
controller 只暴露三个动作，不关心来源：

```c
void chatbot_controller_on_talk_press(void);
void chatbot_controller_on_talk_release(void);
void chatbot_controller_on_talk_cancel(void);
```

- **现在**：`view_main.c` 的 "Hold to Speak" 按钮把
  `LV_EVENT_PRESSED` / `LV_EVENT_RELEASED` / `LV_EVENT_PRESS_LOST` 绑到这三个。
- **阶段 3**：`system/input` 修好后注册按键回调，调**同样这三个函数**。
  controller 和 view 一行都不用改。

`LV_EVENT_PRESS_LOST` → cancel 这条必须有：手指按住后滑出按钮区域时 LVGL 不发
RELEASED，只发 PRESS_LOST，漏掉会永久卡在 RECORDING。

### 2.4 接口三：云端服务 `chatbot_svc.h`

```c
typedef enum {
    CHATBOT_SVC_OK = 0,
    CHATBOT_SVC_ERR_OFFLINE,     /* wifi_is_connected() == false，请求未发出 */
    CHATBOT_SVC_ERR_TIMEOUT,
    CHATBOT_SVC_ERR_NETWORK,     /* DNS / TLS / 连接失败 */
    CHATBOT_SVC_ERR_ASR_EMPTY,   /* 转写结果为空串 */
    CHATBOT_SVC_ERR_SERVER,      /* HTTP 4xx / 5xx */
    CHATBOT_SVC_ERR_RATE_LIMIT,  /* HTTP 429 */
    CHATBOT_SVC_ERR_BAD_REPLY,   /* 响应 JSON 解析失败 */
} chatbot_svc_err_t;

typedef void (*chatbot_svc_text_cb_t)(uint32_t msg_id, const char *text,
                                      chatbot_svc_err_t err, void *user_data);

/* 语音转文本 */
void chatbot_svc_transcribe(uint32_t msg_id,
                            const chatbot_audio_clip_t *clip,
                            chatbot_svc_text_cb_t cb, void *user_data);

/* 文本 → 大模型回复。ctx 是要带上的上下文消息（见 §5 上下文窗口）。 */
void chatbot_svc_complete(uint32_t session_id, uint32_t msg_id,
                          const ChatMessage *ctx, uint16_t ctx_count,
                          chatbot_svc_text_cb_t cb, void *user_data);

/* 用户取消。已发出的请求丢弃结果，不再回调。 */
void chatbot_svc_abort(uint32_t msg_id);
```

超时建议：ASR 15s，LLM 30s。两者都由 service 层内部计时并主动回
`CHATBOT_SVC_ERR_TIMEOUT`，controller 不再自己起 timer。

---

## 3. 交互状态机

这是整个应用唯一的状态机，放在 controller。

```
        ┌──────────────────────── IDLE ◀───────────────────────┐
        │                          │                           │
        │                       press                          │
        │                          ▼                           │
        │                     RECORDING                        │
        │                          │                           │
        │      ┌───────────────────┼──────────────────┐        │
        │   cancel /          release, 时长≥MIN     到 MAX      │
        │  press_lost /            │              自动 release  │
        │  时长<MIN                 │                  │        │
        └──────┴───────────────────┼──────────────────┘        │
                                   ▼                           │
                             TRANSCRIBING ───── err ───────────┤
                                   │                           │
                              文本就绪                          │
                                   ▼                           │
                              THINKING ─────── err ────────────┤
                                   │                           │
                               回复就绪 ─────────────────────────┘
```

参数：

```c
#define CHATBOT_REC_MIN_MS   300     /* 短于此判为误触 */
#define CHATBOT_REC_MAX_MS   60000   /* 到点自动发送，不丢弃 */
```

不变量（实现时按这几条自检）：

1. **任何状态都有出口。** TRANSCRIBING / THINKING 的出口由 service 层超时保证。
2. **RECORDING 期间禁用** 汉堡菜单、New Chat、返回、切页 —— 避免状态撕裂。
3. **TRANSCRIBING / THINKING 期间**，录音按钮变成"取消"，点击调 `chatbot_svc_abort()`
   并回 IDLE，对应消息标记为已取消（不是失败，不提供重试）。
4. 进入 IDLE 时必须清干净：`chatbot_audio_cancel()`、停掉计时 timer、恢复按钮文案。

---

## 4. 数据模型

### 4.1 消息

```c
typedef enum {
    MSG_STATE_OK = 0,     /* 正常 */
    MSG_STATE_PENDING,    /* 用户消息：识别中；AI 消息：思考中 */
    MSG_STATE_FAILED,     /* 失败，可重试 */
    MSG_STATE_CANCELLED,  /* 用户主动取消 */
} MsgState;

typedef struct ChatMessage {
    uint32_t id;
    MsgRole  role;
    MsgState state;
    uint8_t  err_code;      /* FAILED 时有效，见 §7 错误码表 */
    uint32_t timestamp;     /* unix 秒，气泡时间戳 + 时间分组 */
    uint16_t audio_ms;      /* 用户语音时长，0 表示文字输入 */
    char     text[MAX_MSG_LENGTH];
} ChatMessage;
```

**关键交互设计：用户消息在 ASR 返回前就先上屏。**
松手瞬间插入一条 `role=USER, state=PENDING, audio_ms=3200` 的消息，
气泡显示 `🎤 3.2"  识别中…`；ASR 回来后**原地替换**成文本，`state=OK`。

这样用户立刻有反馈，而墨水屏只需两次局部刷新（插入 + 替换），
比转圈等待再一次性出现要好得多。

AI 消息同理：发出请求时就插入 `role=AI, state=PENDING`，气泡显示"思考中…"。

### 4.2 会话

```c
#define CHATBOT_MAX_SESSIONS  8
#define CHATBOT_TITLE_MAX     32

typedef struct {
    uint32_t id;
    char     title[CHATBOT_TITLE_MAX];  /* 自动取首条用户消息前 12 字 */
    uint32_t created_at;
    uint32_t updated_at;
    uint16_t msg_count;
} ChatSession;
```

### 4.3 存储与配额（需要早决策）

`fdb_kvdb` 分区只有 **0x20000 = 128 KB**，而且是
todolist / anki / clock / reader / settings **共用**的（都在 `g_kvdb` 上）。

算术：

| 项 | 大小 |
|---|---|
| 一条消息 | 256B text + ~24B 元数据 ≈ **280 B** |
| 一个会话 20 条 | ≈ **5.6 KB** |
| 8 个会话全量持久化 | ≈ **45 KB（占分区 35%）** |
| 会话索引 8 × 48B | ≈ 0.4 KB |

**推荐方案**：

- KV 布局：`cb_sess_idx` 存 `ChatSession[8]`；`cb_msg_<session_id>` 各存一个会话的消息数组
- 只有**当前会话**常驻 RAM，历史会话按 key 懒加载（FlashDB 是 KV，天然支持）
- 每会话消息上限 20 条，超出滚动淘汰（`model.c` 已有这个逻辑）
- 会话上限 8 个，超出时删最旧的（删除前提示）

**如果 45 KB 太多**，把 `MAX_MSG_LENGTH` 从 256 降到 192 → 每会话 4.2 KB，
8 会话共 34 KB。AI 回复超长时截断并在尾部加"…（已截断）"。

这个数字需要你和其他 app 的占用一起拍板，建议在动手前先量一下现有 kvdb 用了多少。

---

## 5. 上下文窗口

把 20 条消息全发给 LLM 既超 token 也费钱。建议：

- 只带**最近 6 轮**（12 条）+ 一个 system prompt
- 更早的内容不做摘要（设备上没能力做），直接丢弃
- `chatbot_svc_complete()` 的 `ctx` / `ctx_count` 参数就是为此留的，
  由 controller 负责裁剪，service 层不做策略

---

## 6. Mock 策略

**mock 不是"返回一句固定话"，而是一个能复现所有异常分支的时序模拟器。**
否则 §7 那张表里的 UI 分支永远测不到，等接真实 API 时会集中爆发。

Mock 必须做到三件事：

1. **模拟延迟** — 用 `lv_timer` 延迟 800~2000ms 回调，验证 PENDING 占位气泡
   是否正确显示、是否正确原地替换、等待期间 UI 是否正确禁用。
2. **故障注入** — 一个可切换的开关，让 mock 按序返回错误码表里的每一项。
   建议做法：Settings 里加一个隐藏项，或连点聊天页标题 5 次进入 mock 调试面板，
   列出所有错误码，点一下就在下一次请求返回该错误。
3. **变长回复** — 按输入长度返回不同长度的文本（含一条超过 `MAX_MSG_LENGTH` 的），
   验证长气泡换行、截断标记、滚动到底。

---

## 7. 异常处理规范

### 7.1 三级呈现

墨水屏刷新慢，提示要克制且分级：

| 级别 | 形式 | 用于 | 实现 |
|---|---|---|---|
| **L1 Toast** | 底部浮层，2s 自动消失 | 瞬时、不产生数据的问题 | `lv_toast_show()`（现成） |
| **L2 气泡内联** | 气泡变失败态：虚线边框 + `!` + 一行原因 + "重试"按钮 | **主力形式** — 消息级失败，保留上下文且可重试 | 新增气泡样式 |
| **L3 页面横幅** | 钉在输入区上方的一条，条件恢复后自动消失 | 持续性环境问题 | 新增 |

### 7.2 错误码表

| 错误码 | 触发条件 | 呈现 | 文案 | 用户可做 |
|---|---|---|---|---|
| `MIC_INIT` | `chatbot_audio_is_available()==false` | L3 + 禁用录音按钮 | 麦克风不可用，可以用键盘输入 | 切文字输入 |
| `REC_TOO_SHORT` | 时长 < 300ms | L1 | 松手太快了，请按住再说 | 重按 |
| `REC_TOO_LONG` | 到 60s | L1（仍正常发送） | 已达最长录音时间 | — |
| `REC_SILENT` | 全程能量低于阈值 | L1 | 没有听到声音，请靠近麦克风 | 重录 |
| `REC_OVERFLOW` | 缓冲/内存失败 | L2 | 录音出错了，说短一点再试 | 重试 |
| `NET_OFFLINE` | `wifi_is_connected()==false` | L3 + 录音按钮禁用 | 未连接网络 · **去设置** | 跳 Settings |
| `NET_TIMEOUT` | ASR 15s / LLM 30s | L2 | 网络超时 | 重试 |
| `NET_FAIL` | DNS / TLS / 连接失败 | L2 | 连不上服务器 | 重试 |
| `ASR_EMPTY` | 转写空串 | L2，文本置"（没听清）" | 没听清你说什么 | 重录 |
| `ASR_FAIL` | ASR 4xx/5xx | L2 | 语音识别失败 | 重试 |
| `LLM_RATE` | HTTP 429 | L2 | 请求太频繁，稍后再试 | 稍后重试 |
| `LLM_FAIL` | LLM 5xx / `BAD_REPLY` | L2 | 模型没有回应 | 重试 |
| `LLM_TRUNC` | 回复超 `MAX_MSG_LENGTH` | 正常气泡 + 尾部标记 | …（已截断） | — |
| `STORE_FULL` | `fdb_kv_set` 失败 | L1 + 自动删最旧会话 | 存储已满，已清理最早的对话 | 手动删会话 |

### 7.3 三条硬性原则

1. **文案说"发生了什么 + 能做什么"**，不把 HTTP 码暴露给用户 ——
   但完整错误（含 HTTP 码、URL、耗时）必须进 `ESP_LOGE` 和 `system/logs`，
   接真实 API 时靠它定位。
2. **重试要省一步**：失败的用户消息如果 ASR 已经成功（有 text，是 LLM 阶段失败的），
   重试**跳过 ASR 直接重发 LLM**。所以 `ChatMessage` 要同时保留 `text` 和 `audio_ms`。
3. **失败不阻断会话**：失败气泡留在原地，用户可以继续说下一句，
   不强制先处理失败项。

---

## 8. 墨水屏专属取舍

这是本产品和普通屏 chatbot 最大的设计差异，建议直接当成硬约束。

1. **不做流式打字机效果。** 每个 token 刷一次屏是灾难。等完整回复到达后一次性上屏。
   如果服务端只支持流式，在设备侧缓冲完再显示。
2. **不用 spinner / 波形动画。** 用离散文字态（"识别中…" / "思考中…"）。
   录音计时用 1Hz 更新的秒数，且只刷计时那一小块区域。
3. **音量指示用 5 格离散方块**，500ms 更新一次，不要连续波形。
4. **新气泡上屏走局部刷新**，只有滚动/翻页才整屏刷 —— 直接复用
   `epd_display_set_refresh_area()` 那套（参考 anki 的 `view_deck_form.c`）。
5. **气泡不要圆角**：现在 `view_main.c:88` 是 `radius 8`，1bit 屏上会有明显锯齿，
   建议 0 或 2。阴影同理，不要用。
6. **不要 `LV_LABEL_LONG_MODE_SCROLL_CIRCULAR`**（长标题、会话名）——
   那是永不停止的动画，会一直触发局部刷新。用 `DOTS`。

---

## 9. 功能构思

### 建议做（本阶段就能做，不受硬件阻塞）

1. **文字输入兜底** —— 优先级最高。用现成的 `lv_keyboard` + textarea，
   输入区加一个"键盘"图标切换语音/文字。
   理由：**麦克风到货之前，这是唯一能端到端跑通完整对话流程的路径**；
   同时它也是麦克风故障 (`MIC_INIT`) 和嘈杂环境下的正式降级方案。成本极低。
2. **取消进行中的请求** —— 等待时按返回键或再点录音按钮取消，消除卡死感。
3. **失败重试** —— 见 §7.2，复用已转写文本。
4. **会话标题自动生成** —— 取首条用户消息前 12 字，比"新对话 3"有用得多。
5. **上下文窗口管理** —— 见 §5。
6. **时间分组** —— 消息列表按"今天 / 昨天 / 更早"插入分隔行，
   气泡角落显示 `HH:MM`。History 页按 `updated_at` 倒序。

### 值得做（第二批）

7. **长按气泡的操作菜单** —— 删除这条 / 从这里重新生成 / 复制。复用 `lv_bottom_sheet`。
8. **删除会话的二次确认** —— 参考 todolist 的 `view_todo_show_delete_dialog()`。
9. **请求耗时与失败率进日志** —— 接真实 API 时排查用。

### 明确建议不做

10. **离线排队录音** —— 没网时把录音存下来等联网再发。
    PCM 太大（16kHz/16bit 单声道 = 32 KB/秒），128 KB 分区放一条 4 秒的都勉强。
    改成：没网时直接禁用录音 + L3 横幅（`NET_OFFLINE`），并引导去文字输入或连 WiFi。
11. **Markdown 渲染** —— LVGL label 不支持，且墨水屏字体资源有限。
    在 system prompt 里要求模型返回纯文本。
12. **流式输出** —— 见 §8.1。

### 需要先确认硬件

13. **TTS 朗读回复** —— 产品叫"语音对话"，但目前只有输入是语音、输出是文字，
    体验是不闭环的。要做需要扬声器 + 解码能力，是独立的硬件依赖，
    建议和麦克风一起在板子定型时确认。
14. **免提模式** —— 现有 `RECORD_STATE_HANDS_FREE` 语义未定。
    真要做需要 VAD（静音检测自动断句），依赖音频能力，本阶段建议**先移除**这个状态，
    避免它和按住说话的状态机纠缠。

---

## 10. 分阶段落地

**阶段 0 — 本阶段，零硬件依赖，可完整调试**

1. 数据模型改造：`ChatMessage` 加状态/时间戳/时长，新增 `ChatSession`
2. 新增 `storage.c`，接 FlashDB，确定配额（§4.3）
3. 定稿 `chatbot_audio.h` / `chatbot_svc.h` 两个接口头文件
4. 写 `chatbot_audio_mock.c` / `chatbot_svc_mock.c`，带故障注入（§6）
5. controller 换成 §3 的状态机，暴露三个 `on_talk_*`，屏幕按钮先接上
6. view：PENDING/FAILED 气泡样式、时间戳、时间分组、L1/L2/L3 三级错误呈现、重试按钮
7. 多会话：New / History / 删除会话跑通
8. **文字输入兜底** → 端到端可跑通完整对话流程

**阶段 1 — 接真实云端**（依赖：WiFi 已配好）
只实现 `chatbot_svc_http.c`。`esp_http_client` 和 cJSON 已在构建里，不用加依赖。

**阶段 2 — 接麦克风**（依赖：I2S 麦克风型号 + 引脚确定）
只实现 `chatbot_audio_i2s.c`。

**阶段 3 — 接物理按键**（依赖：按键电路 + 修 `system/input`）
需要先做两件事：把 `system/input` 加进根 `CMakeLists.txt` 的 `EXTRA_COMPONENT_DIRS`；
把它 `#include` 的 `leisound_v1.h` 换成 DeepStoa 的板级头文件（该文件目前不存在）。
然后注册按键回调，调 §2.3 的三个函数即可，controller / view 零改动。
