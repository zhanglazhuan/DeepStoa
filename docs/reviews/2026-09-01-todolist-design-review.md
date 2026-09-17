# apps/todolist 设计与实现评审

- 日期：2026-09-01
- 范围：`apps/todolist`，39 个源文件 / 3,002 行
- 基线：commit `db99d4d`
- 面板：GDEM0397T81P 480×800 1bpp
- 在线版：https://claude.ai/code/artifact/3ef53987-39ae-4419-8b4c-54e3ef89f71d

---

## 总评

**骨架是对的，帧预算还没有进入设计。** MVC 分层、page_navigator、hardcore 主题这一套底子清楚，
交互路径也想过。真正的问题是这个 App 还在按「一块随时可以重画的屏」来设计，而它面对的是一块每次刷新
都要花掉数百毫秒、消耗一次电流脉冲、并且留下残影的电子纸。Timer 页是矛盾最集中的地方。

三句话：

1. **体系有，但没被遵守。** 主题定义了 2px 硬边、3 档字号、统一按钮预设；App 里却出现了纯黑圆形按钮、
   蓝色文字、三套图标字体，以及靠 `lv_obj_get_child(header, 2)` 拿槽位。
2. **刷新没有预算。** 计时中每 5 秒一次整屏差分刷新，一个 25 分钟任务约 300 帧。项目里已有现成的局部窗口
   方案（`lv_epd_region`，player 页在用），todolist 没有接。
3. **离量产差一层。** test.c 无条件进固件、存储无版本号、设置不落盘、教程文案与实际手势方向相反、
   指针当 ID 用。

---

## 01 刷新预算：最该改的一件事

`timer_tick_cb` 每次都会写倒计时文本和进度条，最终走到 `EPD_FLUSH_MODE_PARTIAL_ALL` →
`EPD_Dis_PartAll_Async()`，即**整屏**差分刷新。前 5 拍 1 秒一次，之后 5 秒一次，只要任务在跑就一直跑。

| 25 分钟任务 | 面板刷新次数 | 每帧范围 | 倒计时呈现 |
| --- | --- | --- | --- |
| 当前实现 | ≈ 304 | 480 × 800 整屏 | HH:MM:SS 秒级 |
| 分钟级 + 局部窗口 | 25 | 倒计时区 ≈ 480 × 180 | MM 分钟级 |
| 差值 | 12× | ≈ 4.4× | — |

> 推算依据：`controller_timer.c` 的 FAST=1s / SLOW=5s / FAST_MAX=5。**帧数为静态推算，非实测。**

**建议先做的一件事：** 把「单帧耗时（ms）」和「单帧电流积分（mAs）」测出来写进 README 当硬约束。
有了这个数，「倒计时要不要显示秒」就不再是审美争论，而是一道算术题。

### 四个具体动作

1. **倒计时降到分钟级。** 番茄钟不需要秒。显示「剩余 24 分」，只在分钟数真的变化时才 set_text + invalidate。
   进度条按 4%（=1 分钟）量化，值没变就不重绘。
2. **接 `lv_epd_region`。** Timer 页正好是「整页只有一块反复在变」，直接
   `epd_region_begin_focus(page.screen, countdown_zone)` 把窗口钉在倒计时+进度条矩形上，之后连
   `flush_obj()` 都不用再调；离开页面 `epd_region_end()`。参考 `apps/player/subpages/view_play.c:112-166`。
3. **定时器该停就停。** 现在无论是否在计时、是否在 Timer tab，`lv_timer` 都在 1–5 秒醒一次。改成
   `lv_timer_pause()`，恢复时用 `esp_timer_get_time()` 差值补齐 —— 顺手修掉「按 tick 计数当墙钟」的漂移。
4. **补去残影策略。** 全项目只有 `launcher_gesture.c:134` 用过一次 `EPD_FLUSH_MODE_FULL`。建议在
   「进入 App / 退出 App / 连续 N 次局刷后」强制一次全刷，规则收进 `display_refresh_policy`。

### 黑面积也是成本

80×80 纯黑圆形 FAB 和 4 个 80×80 圆形加减按钮：黑像素多 = 更长翻转时间 + 更重残影；圆形在 1bpp 上
没有抗锯齿可用，边缘就是锯齿。主题语言是 `radius: 2` 的硬边，改成描边方角，既省帧又回到体系里。

---

## 02 视觉体系

### 字阶只剩两级，而主角用的是最小的那级

`lv_theme_hardcore.h:21-24` 定义 TINY/SMALL/NORMAL/LARGE = 18/24/32/**32** —— LARGE 和 NORMAL 是同一个
字体。Timer 页里标签、任务名、倒计时全是 32px，页面没有主角。

> **建议**：编译一个只含 `0-9` 和 `:` 的 64–96px 数字字库。12 个字形，Flash 成本可忽略，
> 一次解决「远看不清」和「层级不明」。

### 三套图标混在同一行

checkbox 指示符强制 `lv_font_montserrat_14`、编辑/拖拽把手用 `custom_font_normal`、Done 行删除用
`LV_SYMBOL_TRASH`、Home 右上角用 `LV_SYMBOL_BARS`。四个来源、四种光学重量，1bpp 上尤其明显。

### 其他

- About 页小节标题用 `lv_color_make(0, 120, 215)` 蓝色（`view_about.c:72`）—— 1bpp 上要么纯黑要么噪点。
- Home 页用 `lv_obj_get_child(page.header, 2)` 取右槽位（`view_home.c:98`），而 `Page` 已暴露 `header_right`。
- Todo / Done 空列表是一片纯白，没有空状态引导。
- **Todo 行应该显示预计时长**（行内右侧「25m」）。每省掉一次进出编辑页，就省掉两次整屏刷新 ——
  信息密度在墨水屏上直接换算成功耗。

---

## 03 交互

| ID | 问题 | 级别 |
| --- | --- | --- |
| U1 | 教程文案与实际手势方向完全相反 | P0 |
| U2 | 破坏性操作绑在盲滑手势上 | P1 |
| U3 | 行高 ≈40px、右侧控件热区贴在一起 | P1 |
| U4 | FAB 压住最后一行；列表尺寸规则自相矛盾 | P1 |
| U5 | 批量清空无确认，单条删除却有 | P1 |
| U6 | 确认弹窗三个出口 | P2 |
| U7 | 设置页步进为 1，改一次点二十几下 | P1 |
| U8 | Timer tab 直接点进来是空的 | P0 |

**U1**：代码里**左滑 = 删除**、**右滑 = 开始计时**（`controller_todo.c:67,74`），而 About 页写的是
「Swipe LEFT to start timer / Swipe RIGHT to delete」，出厂教程任务文案也是同一个错误方向。
用户照着说明做，第一次滑就删掉任务。

**U3**：行是 `LV_SIZE_CONTENT` + 上下各 8px，实际约 40px，低于 48px 手指目标下限；右侧编辑图标与拖拽
把手间距 12px，各自 `ext_click_area = 10`，热区实际贴着。建议行高 ≥56px、间距 ≥16px。

**U8**：`view_build_timer_tab()` 永远初始化为「None / -- / 00:00:00」，只有从 Todo 行点进去的异步路径
才会调 `todolist_view_timer_update()`。**正在计时时切走再切回来，页面显示没有任务、按钮回到「Start」，
而后台其实还在跑。**

---

## 04 实现缺陷

| ID | 问题 | 级别 | 位置 |
| --- | --- | --- | --- |
| B1 | Finish 之后不会跳到下一个任务 | P0 | `controller_timer.c:360-371` |
| B2 | ±时长把已用时减掉了 | P0 | `controller_timer.c:374-393` |
| B3 | 计时到点后使用已被 memmove 搬移的指针 | P0 | `controller_timer.c:288-299` |
| B4 | 工厂重置会把自己刚写的数据覆盖掉 | P0 | `storage.c:382` |
| B5 | 设置改完不保存 + 两个假开关 | P0 | `controller_settings.c:233,244,255` |
| B6 | 取消勾选 Done 项后列表不刷新 | P1 | `controller_done.c:196-205` |
| B7 | 任务指针当句柄到处传 | P1 | `view_todo.c:46,63,84` |
| B8 | 重新点任务会清零已用时 | P1 | `model.c:231-240` |
| B9 | 计时状态不跨睡眠 / 重启 | P1 | — |
| B10 | 任务满 64 条时静默失败 | P2 | `model.c:80-94` |
| B11 | timer_ctx 泄漏；退出 App 时最后一页 nav_ctx 泄漏 | P2 | `view_timer.c:25` |

**B1**：`controller_on_timer_finish()` 先 `mark_task_done()`（把当前任务移出 todo 数组），再调 skip；
而 `get_next_todo_task()` 是用 `active_task_id` 在 todo 数组里找位置的，此时必然找不到，返回 NULL。
（连带：`controller_on_timer_skip()` 更新了视图却没有更新 `active_task_id`，跳过后计时器仍在跑上一个任务。）

**B2**：`new_estimate = estimate_s + adjust * 60 - elapsed_s` —— 语义上应是 `estimate_s += adjust * 60`。
已跑 3 分钟时按「+5」，预计时长反而少了 3 分钟。下限还钳到 1 *秒*，且不落盘。

**B3**：`mark_task_done()` 内部 `memmove` 压缩数组并清零尾项，随后仍拿旧 `t` 指针调
`refresh_progress(t)`，读到的已是下一条任务或被清零的槽位。

**B4**：`todolist_app_factory_reset()` 写完教程任务后紧接着调
`todolist_storage_sync_todo(g_todolist_app.model)`。App 运行时会用内存里的旧数组盖掉刚写入的
`td_todos`；内存为空时 `sync_todo` 走 `fdb_kv_del()` 分支，教程数据直接被删。

**B5**：三个回调里的 `todolist_model_config_save()` 全被注释掉，配置只活在内存里。而且
`rest_duration_min` 和 `pre_alert_min` 在整个项目里**没有任何地方读取** —— 两个假开关。

**B7**：每行都把 `todolist_task_t*`（指向 model 数组内部）作为 `user_data` 绑给控件，而 model 的增删改
全用 `memmove` 压缩数组，指针在数据变化后指向的是*另一条任务*。最危险的是编辑页：`ctx->edit_task`
跨页面存活，期间后台计时器完成一个任务，保存时就会写到错误的条目上。**修法：全程只传 `uint32_t id`。**

---

## 05 量产工程

- **固件里有测试代码。** `CMakeLists.txt:11` 无条件编译 `test.c`（mock 任务生成器、手势染色测试、
  一堆 `printf`），`controller.c` / `model.c` 还 `#include "test.h"`。用 Kconfig 隔离。
- **死代码。** `splash_timer_cb` / `splash_timer` 定义了从不调用；`PAGE_LAUNCH` 注册了从不进入，而且它
  显示的是 **news 应用的封面图** `app_news_cover`；`inline_edit_commit_and_close()` 是空函数，外面还留着
  一个专门调它的定时器。
- **日志会拖慢触摸。** `controller_on_todo_drag_pressing()` 每帧一条 `ESP_LOGI`。
- **存储写放大。** 任何一次增删改排序都整块重写 `td_todos`；`mark_task_done()` 一次触发三次 blob 写入。
  建议 dirty flag + 息屏/退出时批量落盘。
- **存储无版本号。** 加载时直接 `read_len / sizeof(todolist_task_t)` 算条数。OTA 之后只要结构体布局变了，
  就会静默算出错误条数并加载脏数据。加 `{magic, version, count}` 头 + 迁移分支。
- **结构体偏胖。** `title[128]` × 64 × 2 = 17.9 KB 堆，启动时一次性 malloc。考虑放 PSRAM 或标题降到 64B。
- **分配全部无判空**，且这些结构都是单例 —— 直接静态分配更好。
- **局刷封装刚收敛完**（task_form / deck_form 已改用 `epd_region_begin_focus()`），剩两处没跟上：
  `lv_keyboard.c:105,135` 仍直接操作 flush mode；**Timer tab 一次都没用**，而它是刷新频率最高的页面。
- About 页硬编码 `Version: 1.0.0` / `contact@todolist.com`，应取 `esp_app_get_description()`。
- 字符串硬编码在 view 里，字库只有 Montserrat（无 CJK）。面向中文学习者的产品，建议现在就抽字符串表。
- `todolist_app_start(root, group)` 忽略 `group`，没有编码器/实体键导航路径。

---

## 06 推进顺序

**P0 — 先让它不出错**
B1 Finish 跳转、B2 ±时长算式、B3 悬垂指针、B4 工厂重置自毁、B5 设置落盘、U1 手势文案对齐、
U8 Timer tab 水合视图状态。

**P1 — 再让它省电**
实测单帧耗时与电流定下帧预算；倒计时改分钟级 + 接 `lv_epd_region` + 定时器暂停/RTC 补齐；
B9 计时状态改时间戳持久化（与上一条同一次改动做掉）；补全刷去残影策略。

**P2 — 统一体系**
大号数字字库；图标字体收敛；去掉 About 蓝色；行高 56px、间距、FAB 遮挡；Todo 行加时长；
空状态；批量清空加确认；B7 指针改 id。

**P3 — 收工程债（出厂前）**
test.c 用 Kconfig 隔离；清死代码；热路径日志降级；存储加版本头 + 批量落盘；分配判空或改静态；
`lv_keyboard.c` 收敛到 `lv_epd_region`；字符串抽表 + CJK 字库预留。

---

## 附：做得好、不要改坏的地方

- **MVC 边界清楚**，subpages / modules 划分合理，值得作为其他 App 的模板。
- **切 tab 时清空所有 tab 容器再重建**（`controller.c:79-82`）—— 内存紧张设备上的正确取舍，注释写明了意图。
- **解释 LVGL 坑的注释质量很高。** 比如「focus outline 落在 `lv_obj_get_coords()` 之外，局刷窗口必须外扩
  `ext_draw_size`，否则面板上会留下擦不掉的边框残影」（现已随重构进了 `lv_epd_region.c:62-65`），以及
  「one_line textarea 不要写死高度，否则 `scroll_to_cusor_pos` 判定会翻转导致文字跳动」。这类注释是团队资产。
- **删除确认弹窗把 task id 存进 `user_data` 而不是指针**（`view_todo.c:214`）—— 这正是 B7 应该推广到
  全 App 的写法，作者显然已经意识到了这个问题。

---

## 修复记录

### 2026-09-01 — P0 全部修复（B1–B5、U1、U8）

编译验证：`idf.py build` 通过，改动涉及的 7 个文件无新增警告。

| 项 | 改动 |
| --- | --- |
| B1 | `controller_on_timer_finish()` 改为**先算出下一个任务的 id 再标记完成**；新增 `timer_switch_to_task()` 统一 skip/finish 的停表+切换流程。连带修掉 `controller_on_timer_skip()` 只更新视图不更新 `active_task_id` 的问题（跳过后计时器仍在跑上一个任务）。 |
| B2 | `estimate_s += adjust * 60`，不再减 `elapsed_s`；下限从 1 秒改为 1 分钟；改完调 `todolist_storage_sync_todo()` 落盘；刷新时传入真实 running 状态，不再把「Pause」打回「Start」。 |
| B3 | 计时到点时先用仍有效的指针刷完最后一帧，再 `mark_task_done()`，随后 `set_active_task(0)` 清除悬垂的 `active_task_id`。 |
| B4 | 工厂重置末尾的 `todolist_storage_sync_todo()` 删除，改为「App 在运行则把刚写入的出厂数据重新 load 进内存」；顺带检查 `app_cfg` 写入返回值。教程任务文案方向改正。 |
| B5 | 三个 change 回调只改内存并置 dirty；新增 `settings_flush_config()` 绑在设置页 `LV_EVENT_DELETE` 上，离开页面时一次性落盘 —— 避免步进为 1 时每点一下写一次 Flash。 |
| U1 | About 页与出厂教程文案全部改成「左滑删除 / 右滑或点击开始计时」，与 `controller_todo.c` 的实现一致。 |
| U8 | 新增 `todolist_view_timer_sync(t, running)`，`view_build_timer_tab()` 结尾从 model 水合任务名、预计、**剩余**时间、进度和按钮文案；`todolist_view_timer_update()` 保留为 `sync(t, false)` 的薄封装。 |

附带的小修（同一批改动内顺手做掉）：

- `todolist_view_timer_sync()` / `refresh_progress()` 增加 `estimate_s == 0` 的除零保护。
- `todolist_view_timer_sync()` 增加 `current_page != PAGE_HOME` 与 `timer_ctx == NULL` 的前置判断 ——
  后台计时器结束时若停在表单页，原来会把 `TaskFormContext` 当 `TodoListViewHomeCtx` 解引用。
- `view_build_timer_tab()` 的 malloc 加判空 + memset（原来分配失败直接解引用 NULL）。
- 时长调整按钮的 `(void*)-10` 改为 `(void*)(intptr_t)-10`，读取侧同步。
- 新增 `todolist_model_set_active_task()`：切换当前任务但保留 `elapsed_s`，与
  `todolist_model_active_todo_task()`（从 Todo 列表点进来、清零重来）区分开。

### 2026-09-01 第二批 — P1 / P2 / P3

编译验证：`ninja esp-idf/todolist/all esp-idf/uilv/all` 通过，两个组件零警告
（除框架宏 `PAGE_REGISTE` 的函数指针转型，见下方「保留项」）。

**帧预算（01 节）**

- 倒计时改成变化驱动：`todolist_view_timer_display_key()` 把剩余时间量化成
  「>60 秒按分钟 / 最后一分钟按 10 秒」两档，控制器比对这个值，**值没变一帧都不推**。
  25 分钟任务的面板刷新次数从约 304 次降到约 30 次。
- Timer 页接入 `epd_region_begin_focus()`，窗口钉在倒计时+进度条那一块；
  开始/暂停按钮变化时用 `epd_region_flush_obj()` 把窗口临时切到按钮上（player 页同款用法）。
  离开 tab、离开 Home 页、退出 App 三处都会 `epd_region_end()` —— 窗口不解除的话下一屏根本推不上去。
- 轮询定时器默认暂停，只有「停在 Timer 页 + 正在计时」才 resume，周期 5 秒且只唤醒 CPU。
  原来的快刷/慢刷状态机连同 `ProgressTimerMode` 一起删掉了，变化检测已经覆盖它的职责。

**B9 计时状态跨睡眠/重启**

- 已用时不再靠 tick 累加，改成 `elapsed_s`（累计）+ `run_started_at`（墙钟起点）。
- 实际累加用 `esp_timer_get_time()` 的单调时钟，**墙钟只用于跨重启恢复** ——
  SNTP 同步会把墙钟一次性推进几十年，拿它算增量会瞬间把任务算成已完成。
- 冷启动时 `todolist_model_timer_restore()` 校验墙钟是否可信（单调递增且间隔 ≤ 12 小时），
  可信就把离开期间的时间补进去继续跑，不可信（掉电 RTC 归零等）就丢弃未完成的那一段。

**存储（P3）**

- 新增 `td_fmt` 格式头 `{magic, version, task_size}`。版本或结构体尺寸对不上时丢弃列表并记 ERROR，
  而不是拿 `read_len / sizeof()` 算出错误条数把脏数据当任务加载 —— 迁移分支的位置也留好了。
- 旧数据无缝迁移：没有格式头按 v0 处理，`td_meta` 的 8 字节旧布局单独识别并升级。
- 加载后校验 `id_generator > 所有已存在 id`，避免新任务和旧任务撞号。
- 写入改为标脏 + 2 秒去抖合并（`todolist_storage_mark_*_dirty` / `flush`）。
  原来 `mark_task_done()` 一次操作触发三次整块写入，拖拽排序更是每次都写。
  退出 App、任务完成、Finish 这些节点会立即 `flush()`。

**B7 指针改 id**

行/图标/编辑页绑定的全部改成 `uint32_t id`，用的时候现查。覆盖 Todo 行、Done 行、
删除弹窗、编辑表单（`ctx->edit_task_id`）。顺带消掉了 `view_done.c` 的两个 const 丢弃警告。

**其余**

| 项 | 改动 |
| --- | --- |
| B6 | Done 取消勾选后 `lv_async_call` 刷新列表 |
| B8 | `active_todo_task()` 不再清零 `elapsed_s`，切换任务时先结算上一段 |
| B10 | `add_todo_task()` 失败返回 0，表单侧弹 toast「Task list is full」 |
| B11 | `TodoListViewTimerCtx` 改为内嵌在 `HomeCtx` 里（不再单独 malloc），泄漏和二次释放风险一起消失；退出 App 时 `view_deinit` 补释放当前页 `nav_ctx` |
| U2 | 编辑页加「Delete Task」按钮 —— 左滑删除保留（有确认），但不再是唯一入口 |
| U3 | 行高 ≥56px、行内间距 16px、图标热区外扩 12px |
| U4 | 列表去掉互相打架的 `LV_PCT(70)`，底部留出 FAB 的空间；FAB 从 80px 圆形改成 72px 方角 |
| U5 | 「Clear done list」加二次确认，并显示将删除的条数 |
| U6 | 统一的 `todolist_confirm_dialog()`：只有 Cancel + 危险操作两个出口，危险按钮实心反白 |
| U7 | 设置页步进 1 → 5（提前提醒仍为 1） |
| 视觉 | `LV_FONT_LARGE` 指向新编译的 montserrat_48（原来和 NORMAL 同为 32）；About 页去掉蓝色改用字号分层；Todo 行显示预计时长、Done 行显示实际用时；Todo/Done 空状态文案 |
| 工程 | `test.c` 移出量产固件（`-DTODOLIST_WITH_TEST=1` 才编）；删掉 `splash_timer_cb`/`splash_timer` 死代码；拖拽和手势的热路径日志降到 `LOGD`；`home_ctx`/`controller`/`timer_ctx` 分配全部判空；About 版本号改取 `esp_app_get_description()`；`view_home.c` 改用 `page.header_right` 而不是 `lv_obj_get_child(header, 2)` |
| 收敛 | `lv_keyboard.c` 最后一份手写局刷代码并入 `lv_epd_region`，全项目只剩一处实现 |

### 保留项（有意没改，需要你定或需要硬件）

1. **帧预算的实测数字。** 报告里的刷新次数是静态推算。单帧耗时和电流要上机测，
   测完把数字写进 README 当硬约束。
2. **`rest_duration_min` / `pre_alert_min` 两个假开关。** 现在会正确持久化了，但仍然没有任何地方读它们。
   接进计时流程（休息阶段 + 提前提醒）还是从设置页拿掉，是产品决定。
3. **i18n 字符串表 + CJK 字库。** 字符串仍硬编码在 view 里。抽表本身简单，但要先定支持哪些语言、
   CJK 用哪套子集字库 —— 这两个决定会影响表的结构。
4. **`todolist_task_t` 瘦身 / 放 PSRAM。** `title[128] × 64 × 2 = 17.9 KB` 堆。
   改尺寸需要走存储迁移（格式头已经就位），但收益是省内存、代价是迁移风险，建议等内存真的紧张时再动。
5. **图标字体只统一了字号，没统一来源。** 一行里仍然是 `LV_SYMBOL_*`（montserrat 内置）+
   `custom_font_normal`（自定义图标字体）两个来源 —— 要真正统一，得确认自定义字体里有没有
   trash/plus/bars 这几个字形，需要字体资产层面确认。
6. **`PAGE_REGISTE` 的函数指针转型警告。** 是框架宏的问题，10 个 app 全都触发。
   只改 todolist 会让它和其他 app 的约定不一致，应该在 `page_navigator` 层面统一处理。
