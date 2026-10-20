# 离线卡片文件的身份与重导入设计

调研日期：2026-10-19

## 结论

DeepStoa 不必要求用户先建牌组再导入。更自然的模型是：`/anki` 下的每个文本文件就是一个“源牌组”，首次发现时自动创建内部牌组，文件名默认作为牌组名；之后每次扫描只做增量对账。复习进度、难度和到期时间保存在设备内部，并绑定到设备生成的稳定 `card_id`，文本文件只负责卡片内容。

纯文本如果只有正反面、没有稳定键，那么在用户同时修改题面、移动行或删除再新增时，任何软件都不可能总能判断“这是原卡修改”还是“删掉旧卡后新建一张”。因此应采用两层策略：普通 TXT 提供低摩擦的启发式对账；需要无歧义更新时，再支持可选的显式 ID，而不是把 ID 强加给所有用户。

## 官方产品怎么做

### Anki

Anki 将“内容身份”和“学习状态”分开。官方源码中的 `Note` 有内部 `id`、`guid` 和字段内容；`Card` 有自己的 `id`、`note_id`，并单独保存 `due`、`interval`、`ease_factor`、复习次数等调度状态。这意味着内容更新可以保持原 card 不变，从而保留学习记录。[Anki Note 源码](https://github.com/ankitects/anki/blob/main/rslib/src/notes/mod.rs)；[Anki Card 源码](https://github.com/ankitects/anki/blob/main/rslib/src/card/mod.rs)

对于 TXT/CSV，Anki 默认用“同笔记类型下的第一字段”判断是否为已有 note；匹配时原地更新其他字段，并明确保留已有卡片的调度信息。用户也可以导出并重导入 Anki 生成的 GUID；只要 GUID 不变，即使内容改变仍能精确更新。官方文档还明确建议：外部系统自建的 ID 放在第一字段，不要冒充 Anki GUID。[Anki 文本导入文档](https://docs.ankiweb.net/importing/text-files.html)

Anki 的导入文件还能声明 `deck` 或 `deck column`；目标牌组不存在时会自动创建，所以“先手工建牌组”并非必要步骤。同一文档也说明，更新已有 note 时保留它当前所在牌组，而不是强行移动到导入界面选定的牌组。[Anki 文本导入文档](https://docs.ankiweb.net/importing/text-files.html)

Anki 把导入理解为新增或更新，而不是把外部文件当作持续同步的镜像。共享牌组重新导入时会加入新卡和更新已有卡，但新包里缺失的卡不会自动从用户集合中删除，以避免丢失学习数据。[Anki 共享牌组官方说明](https://docs.ankiweb.net/contrib.html#sharing-decks-publicly)

### Mochi

Mochi 的原生 `.mochi` 格式为 deck 和 card 提供全局唯一 ID，card 还可携带 reviews；更新既有 deck 时，顶层 card 必须带目标 `deck-id`。这与 Anki 的方向相同：可靠更新依赖稳定 ID，学习历史独立于可见内容。[Mochi 原生格式规范](https://www.mochi.cards/docs/import-and-export/mochi-format-reference/)

Mochi 也允许直接导入 Markdown 文件夹或 CSV，但官方明确说 Markdown 导入不保留复习历史、卡片顺序等元数据；完整往返应使用带 ID 和 reviews 的原生格式。[Mochi 导入说明](https://www.mochi.cards/docs/import-and-export/importing/)；[Mochi 导出说明](https://mochi.cards/docs/import-and-export/exporting/)

### RemNote

RemNote 对缺少稳定身份的普通导入采取保守策略：相同位置、相同内容的 bullet 跳过；内容改变而无法确定哪一版正确时，保留两版让用户处理，而不是猜测并覆盖。这说明纯文本模糊匹配的风险应显式控制。[RemNote 导入说明](https://help.remnote.com/en/articles/7898005-importing-notes)

## 建议的 DeepStoa 方案

### 用户体验

1. 用户把 `xx.txt` 放进 `/anki`，牌组列表自动出现 `xx`，无需先建牌组或执行一次性导入。
2. 打开 Anki 或点击“刷新”时扫描文件变化并增量对账；文件没变时只比较大小、修改时间或摘要，不重复解析。
3. 用户修改文件后再次扫描，已有卡内容更新但学习进度不变；新增行成为新卡；调换行顺序不影响身份。
4. 文件中消失的行先标记为 `missing/archived`，不立即删除学习记录。界面可提示“新增 N、更新 N、缺失 N”，再提供清理入口。

### 内部数据模型

- `SourceDeck`：`source_id`（设备生成）、`deck_id`、规范化路径、文件摘要、显示名、上次同步信息。
- `Card`：设备生成的 `card_id`、`source_id`、内容摘要、规范化首字段、源文件位置提示，以及现有的评分/到期/难度状态。
- 文件路径不是 card 身份；`file_offset` 只能作为读取优化，因为前面插入一行就会使后续 offset 全部变化。
- 复习状态只以 `card_id` 为主键。内容和文件位置变化只更新内容索引，不新建调度记录。

### 普通 TXT 的对账顺序

在同一个 `source_id` 范围内：

1. 完整规范化行摘要相同：认作同一卡，适用于重排。
2. 完整摘要不同，但规范化首字段唯一匹配：认作内容更新，保留 `card_id` 和调度状态。这对应 Anki 的默认策略。
3. 无匹配：分配新 `card_id`。
4. 旧卡未被匹配：标为 `missing`，保留历史，不自动硬删除。
5. 首字段重复或多个候选时不要猜；记录冲突并把这些行视作新卡，或要求用户补稳定 ID。

规范化应尽量保守，只统一 UTF-8 BOM、行尾和字段外围空白；不要随意改大小写或标点，否则不同题目可能被错误合并。

### 可选的精确身份格式

后续可兼容第三字段或文件头声明的稳定 ID，例如：

```text
#separator:Pipe
#id-column:3
question|answer|DS-7F3A91C2
```

ID 由 DeepStoa 在导出或“升级为可同步格式”时生成。普通用户仍可只写 `front|back`；高级用户保留第三字段后，可自由修改正反面而不丢学习进度。也可以把映射写到隐藏 sidecar 文件，但 sidecar 与 TXT 分离后仍有被遗漏的风险，因此它适合作为设备迁移辅助，不能消除纯文本本身的身份歧义。

### 文件改名、删除与恢复

- 文件内容摘要完全相同但路径改变：可自动识别为改名，沿用 `source_id`，并更新牌组显示名。
- 文件改名且内容同时大改：无法可靠自动判断，应显示“关联到已有牌组 / 作为新牌组”一次性选择。
- 文件暂时不存在或 SD 卡拔出：牌组标记为离线，不删除牌组和学习记录。
- 文件重新出现：按 `source_id`、摘要和卡片重合度恢复关联。

## 对当前实现的直接影响

当前 `CardIndex.file_offset`、`file_len` 可以继续作为懒加载缓存，但不能充当身份；`card_id` 才是调度主键。`AnkiDeck.sd_file_path` 应扩展为稳定的 source 记录，而不是把路径本身当牌组身份。导入页面可以逐步变成“扫描结果/同步状态”页面，手动创建牌组保留给设备内建牌组，不再成为读取离线 TXT 的前置步骤。

最小可行版本可以先实现“文件名自动建牌组 + 完整行摘要/唯一首字段对账 + 缺失归档”。这已经解决用户最关心的繁琐导入和文件更新问题；显式 ID、改名识别和冲突处理可在数据结构预留后迭代。
