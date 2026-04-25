#ifndef SENSOR_SERVICE_H
#define SENSOR_SERVICE_H

#include "app_data.h"
#include "esp_err.h"

/* 初始化传感器服务（底层调用 bme280_init） */
esp_err_t sensor_service_init(void);

/* 触发一次采样并缓存结果 */
esp_err_t sensor_service_sample(void);

/* 读取最近一次缓存的数据 */
esp_err_t sensor_service_get_latest(sensor_data_t *out);

#endif
