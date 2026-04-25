#ifndef DISPLAY_SERVICE_H
#define DISPLAY_SERVICE_H

#include "local_ui.h"

/* 初始化 OLED 显示硬件 */
void display_service_init(void);

/* 根据 UI 层提供的四行文本进行渲染（唯一写屏入口） */
void display_service_render_ui(const local_ui_view_t *view);

/* 发生致命错误时显示错误信息 */
void display_service_show_error(const char *message);

#endif
