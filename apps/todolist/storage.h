#ifndef TODOLIST_STORAGE_H
#define TODOLIST_STORAGE_H

#include "model.h"

bool todolist_app_factory_reset(void);

/* 建立延迟写入定时器。必须在 model 分配好之后调用。 */
void todolist_storage_init(TodoListModel* model);
/* 立即落盘并释放定时器。 */
void todolist_storage_deinit(void);

/* 从 FlashDB 加载所有持久化数据（格式头、Meta、Todo、Done） */
void todolist_storage_load_all(TodoListModel* model);

/* 标脏 —— 不立即写 Flash，由 2 秒去抖定时器合并成一次写入。
 * 连续操作（比如拖拽排序、连点 ± 调时长）因此只产生一次擦写。 */
void todolist_storage_mark_todo_dirty(void);
void todolist_storage_mark_done_dirty(void);
void todolist_storage_mark_meta_dirty(void);

/* 立即把所有脏数据写进 Flash（退出 App、息屏、工厂重置前调用） */
void todolist_storage_flush(void);

#endif // TODOLIST_STORAGE_H
