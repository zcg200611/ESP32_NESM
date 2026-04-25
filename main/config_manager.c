#include "config_manager.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

/* NVS 命名空间与键名 */
#define CFG_NAMESPACE "app_cfg"
#define CFG_KEY_WIFI_SSID "wifi_ssid"
#define CFG_KEY_WIFI_PWD "wifi_pwd"
#define CFG_KEY_MQTT_URI "mqtt_uri"
#define CFG_KEY_SAMPLE_MS "sample_ms"
#define CFG_KEY_DEV_NAME "dev_name"

/* 采样周期允许范围 */
#define SAMPLE_PERIOD_MIN_MS 100U
#define SAMPLE_PERIOD_MAX_MS 60000U

static const char *TAG = "CFG";
static app_config_t s_cfg;
static bool s_inited = false;

/* 载入编译期默认配置 */
static void config_manager_load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    snprintf(s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid), "%s", WIFI_STA_SSID);
    snprintf(s_cfg.wifi_password, sizeof(s_cfg.wifi_password), "%s", WIFI_STA_PASSWORD);
    snprintf(s_cfg.mqtt_broker_uri, sizeof(s_cfg.mqtt_broker_uri), "%s", MQTT_BROKER_URI);
    s_cfg.sample_period_ms = APP_SENSOR_SAMPLE_PERIOD_MS;
    snprintf(s_cfg.device_name, sizeof(s_cfg.device_name), "%s", MQTT_DEVICE_NAME);
}

/* 防御式校验：将数值钳制到合法区间 */
static void config_manager_clamp_values(void)
{
    if (s_cfg.sample_period_ms < SAMPLE_PERIOD_MIN_MS) {
        s_cfg.sample_period_ms = SAMPLE_PERIOD_MIN_MS;
    } else if (s_cfg.sample_period_ms > SAMPLE_PERIOD_MAX_MS) {
        s_cfg.sample_period_ms = SAMPLE_PERIOD_MAX_MS;
    }
}

/* 从 NVS 读取字符串键（不存在时保留默认值） */
static void config_manager_try_load_str(nvs_handle_t nvs, const char *key, char *out, size_t out_size)
{
    size_t len = out_size;
    esp_err_t ret = nvs_get_str(nvs, key, out, &len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "load key %s failed: %s", key, esp_err_to_name(ret));
    }
}

/* 保存字符串键并提交 */
static esp_err_t config_manager_save_str(const char *key, const char *value)
{
    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_str(nvs, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return ret;
}

/* 初始化配置层：NVS 启动 + 读取配置 */
esp_err_t config_manager_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    config_manager_load_defaults();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_handle_t nvs = 0;
    ret = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    config_manager_try_load_str(nvs, CFG_KEY_WIFI_SSID, s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
    config_manager_try_load_str(nvs, CFG_KEY_WIFI_PWD, s_cfg.wifi_password, sizeof(s_cfg.wifi_password));
    config_manager_try_load_str(nvs, CFG_KEY_MQTT_URI, s_cfg.mqtt_broker_uri, sizeof(s_cfg.mqtt_broker_uri));
    config_manager_try_load_str(nvs, CFG_KEY_DEV_NAME, s_cfg.device_name, sizeof(s_cfg.device_name));
    (void)nvs_get_u32(nvs, CFG_KEY_SAMPLE_MS, &s_cfg.sample_period_ms);

    config_manager_clamp_values();
    nvs_close(nvs);

    s_inited = true;
    ESP_LOGI(TAG, "config loaded, sample=%lu ms", (unsigned long)s_cfg.sample_period_ms);
    return ESP_OK;
}

/* 返回当前配置快照（只读） */
const app_config_t *config_manager_get(void)
{
    return &s_cfg;
}

/* 更新采样周期并保存 */
esp_err_t config_manager_set_sample_period_ms(uint32_t period_ms)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    if (period_ms < SAMPLE_PERIOD_MIN_MS || period_ms > SAMPLE_PERIOD_MAX_MS) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs = 0;
    esp_err_t ret = nvs_open(CFG_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_u32(nvs, CFG_KEY_SAMPLE_MS, period_ms);
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (ret != ESP_OK) {
        return ret;
    }

    s_cfg.sample_period_ms = period_ms;
    ESP_LOGI(TAG, "sample period updated: %lu ms", (unsigned long)period_ms);
    return ESP_OK;
}

/* 更新 Wi-Fi 账号并保存 */
esp_err_t config_manager_set_wifi(const char *ssid, const char *password)
{
    if (!s_inited || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(ssid) >= sizeof(s_cfg.wifi_ssid) || strlen(password) >= sizeof(s_cfg.wifi_password)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = config_manager_save_str(CFG_KEY_WIFI_SSID, ssid);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = config_manager_save_str(CFG_KEY_WIFI_PWD, password);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid), "%s", ssid);
    snprintf(s_cfg.wifi_password, sizeof(s_cfg.wifi_password), "%s", password);
    return ESP_OK;
}

/* 更新 MQTT 地址并保存 */
esp_err_t config_manager_set_mqtt_broker_uri(const char *uri)
{
    if (!s_inited || uri == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(uri) >= sizeof(s_cfg.mqtt_broker_uri)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = config_manager_save_str(CFG_KEY_MQTT_URI, uri);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(s_cfg.mqtt_broker_uri, sizeof(s_cfg.mqtt_broker_uri), "%s", uri);
    return ESP_OK;
}

/* 更新设备名并保存 */
esp_err_t config_manager_set_device_name(const char *name)
{
    if (!s_inited || name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(name) >= sizeof(s_cfg.device_name)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = config_manager_save_str(CFG_KEY_DEV_NAME, name);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(s_cfg.device_name, sizeof(s_cfg.device_name), "%s", name);
    return ESP_OK;
}
