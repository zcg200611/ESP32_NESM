#include "local_ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_manager.h"
#include "wifi_manager.h"

/* 页面数量与 UI 行为参数 */
#define UI_PAGE_COUNT 3
#define UI_MSG_MS 1500
#define UI_BLINK_MS 300
#define UI_DEBOUNCE_MS 30

typedef enum {
    UI_PAGE_SENSOR = 0, /* 页面1：传感器 */
    UI_PAGE_NET = 1,    /* 页面2：网络状态与重连 */
    UI_PAGE_OTA = 2,    /* 页面3：版本号与 OTA */
} ui_page_t;

typedef enum {
    UI_KEY_S1 = 0,
    UI_KEY_S2 = 1,
    UI_KEY_S3 = 2,
    UI_KEY_S4 = 3,
    UI_KEY_COUNT = 4,
} ui_key_t;

typedef struct {
    gpio_num_t pin;
    bool last_raw;
    bool stable;
    TickType_t last_change_tick;
} ui_button_t;

static const char *TAG = "LOCAL_UI";

/* UI 状态缓存 */
static ui_page_t s_page = UI_PAGE_SENSOR;
static uint8_t s_selected[UI_PAGE_COUNT] = {0, 0, 0};
static sensor_data_t s_sensor = {0};
static bool s_blink_visible = true;

/* 短时消息（成功/失败）覆盖层 */
static bool s_msg_active = false;
static TickType_t s_msg_deadline = 0;
static char s_msg_line1[17] = {0};
static char s_msg_line2[17] = {0};

/* 当前输出给显示层的视图 */
static local_ui_view_t s_view = {0};

/* 四个按键配置 */
static ui_button_t s_buttons[UI_KEY_COUNT] = {
    {.pin = (gpio_num_t)BOARD_KEY_S1_GPIO},
    {.pin = (gpio_num_t)BOARD_KEY_S2_GPIO},
    {.pin = (gpio_num_t)BOARD_KEY_S3_GPIO},
    {.pin = (gpio_num_t)BOARD_KEY_S4_GPIO},
};

/* 返回当前页面可选择动作数量 */
static uint8_t local_ui_action_count(ui_page_t page)
{
    if (page == UI_PAGE_NET || page == UI_PAGE_OTA) {
        return 1;
    }
    return 0;
}

/* 统一 16 字符宽文本 */
static void local_ui_pad16(char *out, const char *in)
{
    snprintf(out, 17, "%-16.16s", in == NULL ? "" : in);
}

/* 设置四行显示文本（仅写缓存，不直接写屏） */
static void local_ui_set_lines(const char *l1, const char *l2, const char *l3, const char *l4)
{
    local_ui_pad16(s_view.line1, l1);
    local_ui_pad16(s_view.line2, l2);
    local_ui_pad16(s_view.line3, l3);
    local_ui_pad16(s_view.line4, l4);
}

/* 设置提示消息 */
static void local_ui_set_message(bool is_error, const char *message)
{
    char line2[17];
    snprintf(line2, sizeof(line2), "%s", message == NULL ? "" : message);

    local_ui_pad16(s_msg_line1, is_error ? "ERR" : "OK");
    local_ui_pad16(s_msg_line2, line2);
    s_msg_active = true;
    s_msg_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(UI_MSG_MS);
}

void local_ui_notify_error(const char *message)
{
    local_ui_set_message(true, message);
}

void local_ui_notify_success(const char *message)
{
    local_ui_set_message(false, message);
}

/* 渲染消息覆盖层 */
static void local_ui_render_message(void)
{
    local_ui_set_lines(s_msg_line1, s_msg_line2, "", "");
}

/* 渲染页面1：传感器 */
static void local_ui_render_sensor(void)
{
    char l1[17];
    char l2[17];
    char l3[17];

    snprintf(l1, sizeof(l1), "T:%5.1fC", s_sensor.temperature_c);
    snprintf(l2, sizeof(l2), "H:%5.1f%%", s_sensor.humidity_rh);
    snprintf(l3, sizeof(l3), "P:%6.1fhPa", s_sensor.pressure_hpa);

    local_ui_set_lines(l1, l2, l3, "S1:Page Next");
}

/* 渲染页面2：网络 */
static void local_ui_render_net(void)
{
    const char *status = wifi_manager_is_connected() ? "NET: CONNECTED" : "NET: DISCONN";
    const bool selected_hidden = (s_selected[UI_PAGE_NET] == 0) && !s_blink_visible;
    const char *action = selected_hidden ? "                " : "> Reconnect WiFi";

    local_ui_set_lines(status, action, "S2/S3:Select", "S4:OK S1:Next");
}

/* 渲染页面3：版本/OTA */
static void local_ui_render_ota(void)
{
    const bool selected_hidden = (s_selected[UI_PAGE_OTA] == 0) && !s_blink_visible;
    const char *action = selected_hidden ? "                " : "> Start OTA";

    local_ui_set_lines(APP_VERSION_STRING, action, "URL: OTA_TEST", "S4:OK S1:Next");
}

/* 根据状态渲染当前页面 */
static void local_ui_render(void)
{
    if (s_msg_active) {
        local_ui_render_message();
        return;
    }

    if (s_page == UI_PAGE_SENSOR) {
        local_ui_render_sensor();
    } else if (s_page == UI_PAGE_NET) {
        local_ui_render_net();
    } else {
        local_ui_render_ota();
    }
}

/* 选择项移动 */
static void local_ui_move_selection(int delta)
{
    uint8_t count = local_ui_action_count(s_page);
    if (count == 0) {
        return;
    }

    int next = (int)s_selected[s_page] + delta;
    if (next < 0) {
        next = count - 1;
    } else if (next >= count) {
        next = 0;
    }
    s_selected[s_page] = (uint8_t)next;
}

/* 执行确认动作：重连 Wi-Fi / 本地 OTA */
static void local_ui_confirm_action(void)
{
    esp_err_t ret;

    if (s_page == UI_PAGE_NET && s_selected[UI_PAGE_NET] == 0) {
        ret = wifi_manager_reconnect();
        if (ret != ESP_OK) {
            local_ui_notify_error("WiFi reconnect");
            ESP_LOGE(TAG, "wifi reconnect failed: %s", esp_err_to_name(ret));
            return;
        }
        local_ui_notify_success("WiFi reconnect");
        return;
    }

    if (s_page == UI_PAGE_OTA && s_selected[UI_PAGE_OTA] == 0) {
        local_ui_notify_success("OTA starting");
        ret = ota_manager_start(OTA_TEST_URL);
        if (ret != ESP_OK) {
            local_ui_notify_error("OTA failed");
            ESP_LOGE(TAG, "ota failed: %s", esp_err_to_name(ret));
            return;
        }
        local_ui_notify_success("OTA done");
    }
}

/* 按键消抖：检测“按下沿” */
static bool local_ui_poll_press(ui_key_t key, TickType_t now_tick)
{
    ui_button_t *btn = &s_buttons[key];
    bool raw_pressed = gpio_get_level(btn->pin) == 0;

    if (raw_pressed != btn->last_raw) {
        btn->last_raw = raw_pressed;
        btn->last_change_tick = now_tick;
    }

    if ((now_tick - btn->last_change_tick) >= pdMS_TO_TICKS(UI_DEBOUNCE_MS) &&
        raw_pressed != btn->stable) {
        btn->stable = raw_pressed;
        if (btn->stable) {
            return true;
        }
    }

    return false;
}

/* 初始化按键输入与 UI 初始状态 */
void local_ui_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BOARD_KEY_S1_GPIO) |
                        (1ULL << BOARD_KEY_S2_GPIO) |
                        (1ULL << BOARD_KEY_S3_GPIO) |
                        (1ULL << BOARD_KEY_S4_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < UI_KEY_COUNT; i++) {
        bool pressed = gpio_get_level(s_buttons[i].pin) == 0;
        s_buttons[i].last_raw = pressed;
        s_buttons[i].stable = pressed;
        s_buttons[i].last_change_tick = now;
    }

    s_page = UI_PAGE_SENSOR;
    s_selected[UI_PAGE_NET] = 0;
    s_selected[UI_PAGE_OTA] = 0;
}

/* 主 UI 更新函数：处理输入、更新状态、渲染文本 */
void local_ui_update(const sensor_data_t *data)
{
    TickType_t now_tick = xTaskGetTickCount();
    TickType_t now_ms = pdTICKS_TO_MS(now_tick);

    if (data != NULL) {
        s_sensor = *data;
    }

    if (s_msg_active && (int32_t)(now_tick - s_msg_deadline) >= 0) {
        s_msg_active = false;
    }

    s_blink_visible = ((now_ms / UI_BLINK_MS) % 2U) == 0U;

    if (!s_msg_active) {
        if (local_ui_poll_press(UI_KEY_S1, now_tick)) {
            s_page = (ui_page_t)(((int)s_page + 1) % UI_PAGE_COUNT);
        }
        if (local_ui_poll_press(UI_KEY_S2, now_tick)) {
            local_ui_move_selection(-1);
        }
        if (local_ui_poll_press(UI_KEY_S3, now_tick)) {
            local_ui_move_selection(1);
        }
        if (local_ui_poll_press(UI_KEY_S4, now_tick)) {
            local_ui_confirm_action();
        }
    } else {
        for (int i = 0; i < UI_KEY_COUNT; i++) {
            (void)local_ui_poll_press((ui_key_t)i, now_tick);
        }
    }

    local_ui_render();
}

/* 供显示层读取当前页面文本 */
void local_ui_get_view(local_ui_view_t *view)
{
    if (view == NULL) {
        return;
    }
    *view = s_view;
}
