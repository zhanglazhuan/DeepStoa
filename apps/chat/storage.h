/**
 * @file storage.h
 * @brief ChatBot 持久化 —— FlashDB KV
 *
 * KV 布局（分区 fdb_kvdb 共 128 KB，与 todolist/anki/clock/reader/settings 共用）：
 *   "cb_sess_idx"   ChatSession[CHAT_MAX_SESSIONS] + 元数据   ≈ 0.4 KB
 *   "cb_m<id>"      单个会话的 ChatMessage 数组                   ≈ 4.2 KB / 会话
 *
 * 只有当前会话的消息常驻 RAM，历史会话按 key 懒加载。
 */

#ifndef CHAT_STORAGE_H
#define CHAT_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "model.h"

/** 开机恢复：会话索引 + 上次的当前会话消息 */
bool chat_storage_load(ChatModel *model);

bool chat_storage_save_index(ChatModel *model);
bool chat_storage_save_messages(ChatModel *model);
bool chat_storage_load_messages(ChatModel *model, uint32_t session_id);
bool chat_storage_delete_messages(uint32_t session_id);

/** 最近一次写入是否因存储满而失败（UI 用它决定要不要提示 CHAT_ERR_STORE_FULL） */
bool chat_storage_last_write_failed(void);

#endif // CHAT_STORAGE_H
