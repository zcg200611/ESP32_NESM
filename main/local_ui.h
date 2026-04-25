#ifndef LOCAL_UI_H
#define LOCAL_UI_H

#include "app_data.h"

/* UI 层输出给显示层的四行文本 */
typedef struct {
    char line1[17];
    char line2[17];
    char line3[17];
    char line4[17];
} local_ui_view_t;

/* 初始化按键和 UI 状态机 */
void local_ui_init(void);

/* 轮询按键、更新页面状态和动作结果 */
void local_ui_update(const sensor_data_t *data);

/* 获取当前 UI 文本快照，供 display_service 渲染 */
void local_ui_get_view(local_ui_view_t *view);

/* 显示错误提示（短时覆盖） */
void local_ui_notify_error(const char *message);

/* 显示成功提示（短时覆盖） */
void local_ui_notify_success(const char *message);

#endif
