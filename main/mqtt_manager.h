#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"
#include "sensor_service.h"

/* 初始化 MQTT 客户端并注册事件回调 */
esp_err_t mqtt_manager_init(void);

/* 启动 MQTT 连接 */
esp_err_t mqtt_manager_start(void);

/* 获取当前 MQTT 连接状态 */
bool mqtt_manager_is_connected(void);

/* 上报传感器数据到属性上报主题 */
esp_err_t mqtt_manager_publish_sensor(const sensor_data_t *data);

#endif
