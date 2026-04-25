#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "board_config.h"
#include "config_manager.h"
#include "display_service.h"
#include "local_ui.h"
#include "mqtt_manager.h"
#include "ota_manager.h"
#include "sensor_service.h"
#include "wifi_manager.h"

/* 系统主入口：负责模块初始化与主循环调度 */
static const char *TAG = "APP";

void app_main(void)
{
    sensor_data_t latest_data = {0};
    local_ui_view_t ui_view = {0};
    TickType_t last_sample_tick = 0;
    uint32_t sample_period_ms = APP_SENSOR_SAMPLE_PERIOD_MS;

    /* 初始化显示与配置（配置会优先从 NVS 读取） */
    display_service_init();

    esp_err_t ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config init failed: %s", esp_err_to_name(ret));
        display_service_show_error("CFG init err    ");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    sample_period_ms = config_manager_get()->sample_period_ms;
    local_ui_init();

    /* 初始化传感器 */
    ret = sensor_service_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sensor init failed: %s", esp_err_to_name(ret));
        display_service_show_error("BME280 init err  ");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* 初始化并启动 Wi-Fi */
    ret = wifi_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(ret));
        display_service_show_error("WiFi init err");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(ret));
        display_service_show_error("WiFi start err");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    if (!wifi_manager_wait_connected(pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS))) {
        ESP_LOGE(TAG, "wifi connect timeout");
        display_service_show_error("WiFi timeout");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* 若是 OTA 新固件首次启动，标记为有效 */
    ota_manager_mark_app_valid_if_needed();

    /* 初始化并启动 MQTT */
    ret = mqtt_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt init failed: %s", esp_err_to_name(ret));
    }

    ret = mqtt_manager_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(ret));
    }

    while (1) {
        TickType_t now = xTaskGetTickCount();

        /* 采样周期支持云端下发动态修改，循环中实时读取配置 */
        sample_period_ms = config_manager_get()->sample_period_ms;
        if ((now - last_sample_tick) >= pdMS_TO_TICKS(sample_period_ms)) {
            last_sample_tick = now;

            ret = sensor_service_sample();
            if (ret == ESP_OK) {
                ret = sensor_service_get_latest(&latest_data);
            }

            if (ret == ESP_OK) {
                if (wifi_manager_is_connected() && mqtt_manager_is_connected()) {
                    (void)mqtt_manager_publish_sensor(&latest_data);
                }
            } else {
                ESP_LOGW(TAG, "BME280 read failed: %s", esp_err_to_name(ret));
                local_ui_notify_error("BME280 read err");
            }
        }

        /* 本地 UI 逻辑更新 -> 取视图 -> 统一交给显示层渲染 */
        local_ui_update(&latest_data);
        local_ui_get_view(&ui_view);
        display_service_render_ui(&ui_view);

        vTaskDelay(pdMS_TO_TICKS(APP_UI_POLL_PERIOD_MS));
    }
}
