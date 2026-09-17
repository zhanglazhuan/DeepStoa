# 音乐播放器设计（2026-09-01）

墨水屏手机上的音乐播放器。两个页面：曲目列表 + 播放页。

**当前硬件状态**：板子上既没有 SD 卡也没有喇叭。曲目库和音频输出都走 mock，
但接口按真实驱动的语义定稿，硬件到位后只补实现，controller / view 不改。

---

## 1. 分层

```
  apps/player/              纯 UI + 曲目库（app 生命周期）
      ↕ 订阅事件 / 调用控制
  system/audio/
      audio_service.{h,c}   播放会话：队列 + 位置 + 状态 + 模式 + 音量
      audio_out.h           驱动契约：只描述"一个能出声的设备"
      audio_out_mock.c      ← 现在
      audio_out_i2s.c       ← 硬件到位后补
```

### 1.1 为什么播放会话在 system/ 而不是 apps/player/

`app_manager` 关闭一个 app 时会调它的 `stop_func()`。音频引擎如果挂在 player app
的生命周期上，**一退出播放器就断音** —— 而"边看书边听歌"恰恰是墨水屏设备最合理
的用法。所以播放会话必须比 app 活得久：`audio_service_init()` 在 `main/DeepStoa.c`
里和 `clock_init()` / `flash_control_init()` 同级调用，播放器 app 只是它的遥控器。

`player_controller_deinit()` 里明确**不** stop 音频，这一条要一直保持。

### 1.2 队列存路径，不存"曲目库下标"

曲目库（`PlayerModel.tracks[]`）属于 app，app 关掉之后下标就没有意义了。
所以 `audio_track_t` 是 `{path, title}` 的完整拷贝。

### 1.3 观察者用自己的 subscribe/unsubscribe，不用 app_event

`app_event` 只有 `register` 没有 `unregister` —— UI 回调在 app 释放后还会被调，必崩。
所以 `audio_service` 自带一张 4 个槽位的观察者表，两个页面都在 `LV_EVENT_DELETE` 里
`audio_service_unsubscribe()`。

反过来，**永不销毁的订阅者用 app_event 正合适**：物理媒体键就是这么接的（见 §5）。

---

## 2. 墨水屏局部刷新（本项目最关键的部分）

整屏刷新会明显闪一下。进度条每几秒变一次、播放图标随时切换，如果每次都整屏刷，
这一页就没法用。

### 2.1 共享 helper

`system/uilv/utils/lv_epd_region.{h,c}` 把驱动层那套固定的三步顺序封起来：

```c
epd_region_begin(page.screen);        // 静态内容整屏打一帧 + 同步差分基准
epd_region_flush_obj(progress_zone);  // 之后只把这块矩形推给面板
epd_region_flush_obj(btn_play);       // 换一块窗口，不需要取并集
epd_region_end();                     // 退出，恢复整屏
```

三个实现细节：

- **`begin()` 走 `lv_async_call` 再 `lv_refr_now`**。直接在事件回调 / 页面 builder 里
  调 `lv_refr_now()` 会重入 LVGL 自己的刷新流程（`disp_refr` 是全局状态）。
- **`flush_obj()` 会按 `lv_obj_calculate_ext_draw_size()` 外扩**。outline / shadow 画在
  `coords` 之外，不包进窗口就会在面板上留下擦不掉的残影。
- **`begin()` 生效前调 `flush_obj()` 会退化成一次普通整屏刷新**，不会崩。

### 2.2 播放页的三个窗口

| 窗口 | 内容 | 触发 |
|---|---|---|
| `progress_zone` | 已播时间 + 进度条 + 总时长 | tick / 快进 / 点进度条 |
| `btn_play` | 播放暂停图标 | 播放/暂停切换 |
| `opts_zone` | 播放模式 + 音量 | 切模式 / 调音量 |

时间和进度条放在同一个容器里，这样只需要一个窗口，不用在三个对象之间来回切。

### 2.3 进度 tick 1Hz，但面板最多 5 秒推一次

`PROGRESS_PUSH_S = 5`。墨水屏一次局部刷新要几百毫秒，1Hz 推面板等于持续刷新，
既晃眼又费电。**用户操作（快进 / 播放暂停 / 切歌）走 `force=true` 立刻推**，
所以手感不受影响 —— 只有"自己往前走"的进度是 5 秒一跳。

### 2.4 局部刷新和弹窗/Toast 互斥

窗口开着时，toast 和弹窗画在窗口之外，驱动层根本不发送那些像素 —— **提示会看不见**。
所以所有提示前先 `epd_region_end()` 退回整屏；`notify_progress` / `notify_state` /
`notify_options` 发现窗口没了会自动 `epd_region_begin()` 重建。整个机制是自愈的。

同理，**局部刷新期间状态栏时钟不会更新**。这是这套方案固有的代价。

### 2.5 换曲走整屏

`notify_track()` 里调的是 `epd_region_begin()` 而不是 `flush_obj()`：
标题长度变了、进度归零、位置指示变了，整页都不一样，这时候局部刷新反而会留残影。

### 2.6 墨水屏上不做的事

- 进度条**不做拖动**，只做"点哪跳哪"。拖动过程中每个中间位置都是一次刷新。
- 音量**不做滑块**，走离散档位（`AUDIO_VOLUME_STEP = 10`）+ `−/+` 按钮。
- `lv_bar_set_value` 一律 `LV_ANIM_OFF`。补间动画每一帧都是一次面板刷新。
- 长标题用 `LV_LABEL_LONG_MODE_DOTS`，**不要 `SCROLL_CIRCULAR`** —— 那是永不停止的动画。
- 圆角统一 2px。1bit 屏上大圆角有明显锯齿。

---

## 3. 驱动契约（`audio_out.h`）

实现方必须遵守，写在头文件注释里：

1. **所有回调必须在 LVGL 线程被调用。** 真实实现跑在解码任务里，回调前必须
   `lv_async_call()` 弹回。在别的线程碰 LVGL 对象会崩，而且崩得很难查。
2. **`position_ms()` 会被 1Hz 轮询**，必须无锁、快速返回，不能阻塞在 I2S 或文件读上。
3. **`open()` 要在真正解码前先校验格式**，格式不对立刻返回 `AUDIO_ERR_FORMAT` ——
   UI 的格式弹窗靠这个，不能等播到一半才报。
4. **`ext_supported()` 返回的集合必须和 `open()` 的实际能力一致。**
   `player_library_ext_supported()` 直接委托给它，避免"列表页说能播、真播时报格式错"。

---

## 4. 持久化与续播

KV key `au_state`，7.2 KB：`{version, count, pos, volume, mode, position_ms, duration_ms, queue[32]}`。

### 4.1 写入时机

绝不能每秒写一次 flash —— 会磨损，而且这个 128 KB 分区是和
todolist / anki / chatbot / reader 共用的。

- **状态变化时写**：暂停、停止、切歌、队列增删、模式和音量变化。本来就不频繁。
- **播放中每 120 秒兜底写一次**（`RESUME_CHECKPOINT_MS`）。硬断电最多丢 2 分钟进度。
- `audio_service_checkpoint()` 已暴露，`system/sleep` 接进来后在进休眠前调一次，
  正常关机就一点不丢。

### 4.2 恢复行为

开机恢复队列和位置，但**不自动开始播放** —— 恢复成停止态 + 一个"待应用的位置"，
按一下播放键才从原位继续。这和主流播放器一致。

**不在 init 里打开文件**：那时 SD 卡不一定挂载好，文件也可能已经不在了（换过卡）。
位置先挂着，等第一次 `open_current()` 成功后再 `audio_out_seek()` 应用一次。

**时长也要存。** 只存 `position_ms` 的话，重启后进度条会显示"已播 01:23"、总时长却是
`00:00`，比例算不出来，看起来像坏了。`audio_service_position_ms()` / `duration_ms()`
在停止态返回恢复出来的值，所以一进播放页就能看到"上次听到 01:23 / 03:47"。

---

## 5. 播放模式

| 模式 | 一首放完之后 |
|---|---|
| 顺序 | 下一首；放完最后一首**停下**（不绕回开头） |
| 列表循环 | 下一首，末尾绕回 |
| 单曲循环 | 原地重放 |
| 随机 | 按洗好的排列取下一个 |

**模式只影响"自然播完之后去哪"，不影响用户手动按上一首/下一首** ——
单曲循环时手动按下一首也应该老老实实换一首。所以拆成两条路径：

```c
static void step_manual(int delta);   /* 用户按键 */
static void advance_auto(void);       /* 自然播完，这里才看模式 */
```

**随机不是每次抽一个**（那样会明显重复）。用 Fisher-Yates 洗一个队列下标的排列，
一轮之内不重样，放完再洗。队列增删后重新洗牌，并把当前曲目对齐到排列里的位置。

---

## 6. 健壮性

**坏文件自动跳过，但整队都坏时会停下。** `play_at()` 打开失败会朝同方向继续跳，
用 `s_fail_streak` 计数，**连续失败次数达到队列长度就停下并报错** —— 否则整队都是
坏文件时会无限跳。

**删除曲目的顺序**：先从播放队列摘掉 → 再删文件 → 最后从曲目库移除。
顺序反了的话队列会指向一个已经不存在的文件。

**不支持的格式仍然列出来**（灰显 + `⚠ 格式不支持`），点开时弹窗说明。
直接隐藏的话"文件明明在却看不到"，很难排查。

---

## 7. 物理媒体键

播放会话是系统级的，所以媒体键理应在任何界面下都生效，而不是只有播放器开着才管用。
`audio_service` 订阅了 `app_event` 的 `APP_EVENT_KEY_PLAY_PAUSE / VOL_UP / VOL_DOWN`。

目前**没人 fire 这三个事件**：`system/input` 没有加进根 `CMakeLists.txt` 的
`EXTRA_COMPONENT_DIRS`，而且它 `#include "leisound_v1.h"`（另一个产品的板级头，
本仓库不存在），即使加进构建也编不过。

按键电路到位后需要做两件事：修好 `system/input` 的板级头依赖并加进构建；
让它 `app_event_fire(APP_EVENT_KEY_*)`。`audio_service.c` 一行都不用改。

---

## 8. 已知限制与下一步

### 8.1 接真实 codec 时一定会碰到的三件事

- **VBR 时长**：不读 Xing/Info/VBRI 头就只能按"文件大小 ÷ 比特率"估，VBR 文件会
  估得很离谱，进度条一路飘。
- **Seek 精度**：MP3 没法按毫秒精确定位。要么读 Xing 的 TOC（100 个点，误差 1%），
  要么扫帧头建索引（准但慢，一首歌几百毫秒）。
  **现在"点进度条跳转"在 mock 下是精确的，接真机后会有可见误差** —— 别以为是 UI 的 bug。
- **ID3 元数据**：现在标题就是文件名。建议只解析 ID3v2 的 TIT2/TPE1/TALB 三个帧，
  和 VBR 头一起读，不做完整索引。

这三件必须一起做，所以放在同一个节点。

### 8.2 等真实 SD 卡才谈得上

- 递归子目录 / 按文件夹浏览（现在只扫 `PLAYER_MUSIC_DIR` 一层）
- 排序（名称/时间/大小）与搜索
- `PLAYER_MAX_TRACKS = 64` 的天花板。64 × `sizeof(TrackInfo)`(≈232B) ≈ 15 KB 已经不小，
  要上量得改成"只把文件名索引常驻，详情按需读"

### 8.3 等其他系统组件

`system/battery` 和 `system/sleep` 目前都没进构建。需要：低电量提示、
播放中抑制深度休眠（但允许息屏 —— 那正是墨水屏最省电的状态）、SD 卡热插拔检测。

### 8.4 刻意不做

- **频谱 / 波形可视化**：每帧一次刷新，墨水屏上物理不成立。
- **专辑封面 / 歌词**：可以做（驱动有 4 灰度模式，封面走 Floyd–Steinberg 抖动；
  歌词只刷当前行正好复用 §2 的机制），但依赖解码器和文件系统都就位，属于锦上添花。
