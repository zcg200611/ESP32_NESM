#include "sensor_service.h"

#include <stdbool.h>

#include "bme280.h"

/* 传感器最新缓存值 */
static sensor_data_t s_latest;
/* 记录模块是否完成初始化 */
static bool s_initialized = false;

/* 初始化 BME280，并清空本地缓存 */
esp_err_t sensor_service_init(void)
{
    esp_err_t ret = bme280_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_latest.temperature_c = 0.0f;
    s_latest.humidity_rh = 0.0f;
    s_latest.pressure_hpa = 0.0f;
    s_initialized = true;
    return ESP_OK;
}

/* 触发一次底层采样并更新缓存 */
esp_err_t sensor_service_sample(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bme280_data_t raw;
    esp_err_t ret = bme280_read_data(&raw);
    if (ret != ESP_OK) {
        return ret;
    }

    s_latest.temperature_c = raw.temperature_c;
    s_latest.humidity_rh = raw.humidity_rh;
    s_latest.pressure_hpa = raw.pressure_hpa;
    return ESP_OK;
}

/* 读取最近一次缓存的数据 */
esp_err_t sensor_service_get_latest(sensor_data_t *out)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out = s_latest;
    return ESP_OK;
}
