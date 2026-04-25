#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdint.h>

#include "esp_err.h"

/* 设备运行配置（保存在 NVS） */
typedef struct {
    char wifi_ssid[33];        /* Wi-Fi SSID */
    char wifi_password[65];    /* Wi-Fi 密码 */
    char mqtt_broker_uri[128]; /* MQTT 服务器地址 */
    uint32_t sample_period_ms; /* 采样周期（毫秒） */
    char device_name[32];      /* 设备名称 */
} app_config_t;

/* 初始化配置模块：加载 NVS，若没有则使用默认值 */
esp_err_t config_manager_init(void);

/* 获取当前生效配置（只读指针） */
const app_config_t *config_manager_get(void);

/* 更新并持久化采样周期 */
esp_err_t config_manager_set_sample_period_ms(uint32_t period_ms);

/* 更新并持久化 Wi-Fi 账号 */
esp_err_t config_manager_set_wifi(const char *ssid, const char *password);

/* 更新并持久化 MQTT 服务器地址 */
esp_err_t config_manager_set_mqtt_broker_uri(const char *uri);

/* 更新并持久化设备名称 */
esp_err_t config_manager_set_device_name(const char *name);

#endif
