#include "display_service.h"

#include <stdio.h>

#include "oled.h"

/* 将任意字符串格式化为 16 字符宽（OLED 一行 16 字符） */
static void display_pad16(char *out, const char *in)
{
    snprintf(out, 17, "%-16.16s", in == NULL ? "" : in);
}

/* 初始化 OLED 硬件并清屏 */
void display_service_init(void)
{
    OLED_Init();
    OLED_Clear();
}

/* 唯一显示入口：根据 UI 层提供的四行文本进行渲染 */
void display_service_render_ui(const local_ui_view_t *view)
{
    if (view == NULL) {
        return;
    }

    char line[17];
    display_pad16(line, view->line1);
    OLED_ShowString(1, 1, line);
    display_pad16(line, view->line2);
    OLED_ShowString(2, 1, line);
    display_pad16(line, view->line3);
    OLED_ShowString(3, 1, line);
    display_pad16(line, view->line4);
    OLED_ShowString(4, 1, line);
}

/* 启动期或致命错误时的兜底错误显示 */
void display_service_show_error(const char *message)
{
    char line[17];
    display_pad16(line, message == NULL ? "display error" : message);
    OLED_ShowString(1, 1, line);
}
