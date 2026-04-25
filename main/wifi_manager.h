#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/* 初始化 Wi-Fi STA 模块（加载配置、注册事件） */
esp_err_t wifi_manager_init(void);

/* 启动 Wi-Fi */
esp_err_t wifi_manager_start(void);

/* 主动断开并重连（用于本地 UI 手动重连） */
esp_err_t wifi_manager_reconnect(void);

/* 获取当前连接状态 */
bool wifi_manager_is_connected(void);

/* 阻塞等待连接成功 */
bool wifi_manager_wait_connected(TickType_t timeout_ticks);

#endif
