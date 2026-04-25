#include "mqtt_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "config_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "ota_manager.h"

static const char *TAG = "MQTT";

/* MQTT 客户端句柄与连接状态 */
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
/* 防止 OTA 重入（同一时刻仅允许一个 OTA 任务） */
static volatile bool s_ota_task_running = false;

/* 从 JSON 中提取字符串字段（简单文本解析） */
static bool mqtt_extract_json_string(const char *json, const char *key, char *out, size_t out_size)
{
    if (json == NULL || key == NULL || out == NULL || out_size == 0) {
        return false;
    }

    const char *key_pos = strstr(json, key);
    if (key_pos == NULL) {
        return false;
    }

    const char *colon = strchr(key_pos, ':');
    if (colon == NULL) {
        return false;
    }

    const char *p = colon + 1;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }

    if (*p != '"') {
        return false;
    }
    ++p;

    size_t i = 0;
    while (*p != '\0' && *p != '"' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';

    return (*p == '"' && i > 0);
}

/* 从 JSON 中提取整数字段 */
static bool mqtt_extract_json_int(const char *json, const char *key, int *out)
{
    if (json == NULL || key == NULL || out == NULL) {
        return false;
    }

    const char *key_pos = strstr(json, key);
    if (key_pos == NULL) {
        return false;
    }

    const char *colon = strchr(key_pos, ':');
    if (colon == NULL) {
        return false;
    }

    const char *p = colon + 1;
    while (*p != '\0' && !isdigit((unsigned char)*p) && *p != '-') {
        ++p;
    }
    if (*p == '\0') {
        return false;
    }

    char *end = NULL;
    long value = strtol(p, &end, 10);
    if (end == p) {
        return false;
    }

    *out = (int)value;
    return true;
}

/* 发布属性设置回复（thing/property/set_reply） */
static esp_err_t mqtt_publish_property_set_reply(bool ok, int period_ms)
{
    if (s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[160];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"code\":%d,\"msg\":\"%s\",\"sample_period_ms\":%d}",
                       ok ? 0 : 1,
                       ok ? "succ" : "failed",
                       period_ms);
    if (len <= 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int msg_id = esp_mqtt_client_publish(
        s_mqtt_client,
        MQTT_TOPIC_PROPERTY_SET_REPLY,
        payload,
        0,
        1,
        0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "property set reply: %s", payload);
    return ESP_OK;
}

/* 处理云端下发配置（当前支持采样周期） */
static void mqtt_handle_remote_config(const char *topic, int topic_len, const char *data, int data_len)
{
    size_t set_topic_len = strlen(MQTT_TOPIC_PROPERTY_SET);
    if (topic_len != (int)set_topic_len || strncmp(topic, MQTT_TOPIC_PROPERTY_SET, set_topic_len) != 0) {
        return;
    }

    char json_buf[512];
    int copy_len = data_len < (int)(sizeof(json_buf) - 1) ? data_len : (int)(sizeof(json_buf) - 1);
    memcpy(json_buf, data, (size_t)copy_len);
    json_buf[copy_len] = '\0';

    int period_ms = 0;
    bool found = mqtt_extract_json_int(json_buf, "\"sample_period_ms\"", &period_ms);
    if (!found) {
        found = mqtt_extract_json_int(json_buf, "\"sample_period\"", &period_ms);
    }
    if (!found) {
        (void)mqtt_publish_property_set_reply(false, 0);
        return;
    }

    esp_err_t ret = config_manager_set_sample_period_ms((uint32_t)period_ms);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "remote config updated sample_period_ms=%d", period_ms);
        (void)mqtt_publish_property_set_reply(true, period_ms);
    } else {
        ESP_LOGW(TAG, "remote config update failed: %s", esp_err_to_name(ret));
        (void)mqtt_publish_property_set_reply(false, period_ms);
    }
}

/* 发布 OTA 回复（ota/inform_reply） */
static esp_err_t mqtt_publish_ota_reply(const char *status, const char *msg)
{
    if (s_mqtt_client == NULL || status == NULL || msg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char payload[192];
    int len = snprintf(payload, sizeof(payload), "{\"status\":\"%s\",\"msg\":\"%s\"}", status, msg);
    if (len < 0 || len >= (int)sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_TOPIC_OTA_INFORM_REPLY, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "ota inform reply publish failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "ota inform reply: %s", payload);
    return ESP_OK;
}

/* OTA 后台任务：避免阻塞 MQTT 回调线程 */
static void ota_task(void *arg)
{
    char *url = (char *)arg;
    if (url == NULL) {
        s_ota_task_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "start OTA task, url=%s", url);
    esp_err_t ret = ota_manager_start(url);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA task failed: %s", esp_err_to_name(ret));
        (void)mqtt_publish_ota_reply("failed", esp_err_to_name(ret));
    }

    free(url);
    s_ota_task_running = false;
    vTaskDelete(NULL);
}

/* 处理 OTA 通知：解析 URL 并启动 OTA 任务 */
static void mqtt_handle_ota_inform(const char *topic, int topic_len, const char *data, int data_len)
{
    size_t ota_topic_len = strlen(MQTT_TOPIC_OTA_INFORM);
    if (topic_len != (int)ota_topic_len || strncmp(topic, MQTT_TOPIC_OTA_INFORM, ota_topic_len) != 0) {
        return;
    }

    char json_buf[512];
    int copy_len = data_len < (int)(sizeof(json_buf) - 1) ? data_len : (int)(sizeof(json_buf) - 1);
    memcpy(json_buf, data, (size_t)copy_len);
    json_buf[copy_len] = '\0';

    char ota_url[320];
    bool found = mqtt_extract_json_string(json_buf, "\"ota_url\"", ota_url, sizeof(ota_url));
    if (!found) {
        found = mqtt_extract_json_string(json_buf, "\"url\"", ota_url, sizeof(ota_url));
    }
    if (!found) {
        found = mqtt_extract_json_string(json_buf, "\"firmware_url\"", ota_url, sizeof(ota_url));
    }

    /* 兼容脚本透传简化格式，未带 URL 时回退到本地测试地址 */
    if (!found) {
        bool has_params = strstr(json_buf, "params=") != NULL;
        bool has_id = strstr(json_buf, "id=") != NULL;
        if (has_params && has_id) {
            size_t fallback_len = strlen(OTA_TEST_URL);
            if (fallback_len >= sizeof(ota_url)) {
                (void)mqtt_publish_ota_reply("rejected", "ota url too long");
                return;
            }
            memcpy(ota_url, OTA_TEST_URL, fallback_len + 1);
            found = true;
            ESP_LOGI(TAG, "ota inform without url, fallback OTA_TEST_URL");
        } else {
            (void)mqtt_publish_ota_reply("rejected", "missing ota url");
            return;
        }
    }

    if (s_ota_task_running) {
        (void)mqtt_publish_ota_reply("rejected", "ota already running");
        return;
    }

    size_t url_len = strlen(ota_url);
    char *url_copy = (char *)malloc(url_len + 1);
    if (url_copy == NULL) {
        (void)mqtt_publish_ota_reply("rejected", "no memory");
        return;
    }
    memcpy(url_copy, ota_url, url_len + 1);

    BaseType_t ok = xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, NULL);
    if (ok != pdPASS) {
        free(url_copy);
        (void)mqtt_publish_ota_reply("rejected", "create ota task failed");
        return;
    }

    s_ota_task_running = true;
    ESP_LOGI(TAG, "OTA inform accepted");
    (void)mqtt_publish_ota_reply("accepted", "ota task started");
}

/* MQTT 事件分发入口 */
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)handler_args;
    (void)base;

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "mqtt before connect");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "mqtt connected");
        s_mqtt_connected = true;

        /* 订阅属性与 OTA 下行主题 */
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_PROPERTY_REPLY, 0);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_PROPERTY_SET, 0);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_PROPERTY_SET_REPLY, 0);
        esp_mqtt_client_subscribe(s_mqtt_client, MQTT_TOPIC_OTA_INFORM, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "mqtt disconnected");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "mqtt published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "topic=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "data=%.*s", event->data_len, event->data);
        mqtt_handle_remote_config(event->topic, event->topic_len, event->data, event->data_len);
        mqtt_handle_ota_inform(event->topic, event->topic_len, event->data, event->data_len);
        break;

    case MQTT_EVENT_ERROR:
        s_mqtt_connected = false;
        ESP_LOGE(TAG, "mqtt event error");

        if (event->error_handle) {
            ESP_LOGE(TAG, "error_type=%d", event->error_handle->error_type);
            ESP_LOGE(TAG, "esp_tls_last_esp_err=0x%x",
                     event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG, "esp_tls_stack_err=0x%x",
                     event->error_handle->esp_tls_stack_err);
            ESP_LOGE(TAG, "esp_transport_sock_errno=%d",
                     event->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        ESP_LOGI(TAG, "other mqtt event id=%ld", (long)event_id);
        break;
    }
}

/* 初始化 MQTT 客户端 */
esp_err_t mqtt_manager_init(void)
{
    const app_config_t *cfg = config_manager_get();

    if (s_mqtt_client != NULL) {
        return ESP_OK;
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = cfg->mqtt_broker_uri,
            },
        },
        .credentials = {
            .client_id = MQTT_CLIENT_ID,
            .username = MQTT_USERNAME,
            .authentication = {
                .password = MQTT_PASSWORD,
            },
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
        },
        .network = {
            .disable_auto_reconnect = false,
            .timeout_ms = 10000,
            .reconnect_timeout_ms = 5000,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(
        s_mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register mqtt event failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/* 启动 MQTT 连接 */
esp_err_t mqtt_manager_start(void)
{
    if (s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_mqtt_client_start(s_mqtt_client);
}

/* 查询连接状态 */
bool mqtt_manager_is_connected(void)
{
    return s_mqtt_connected;
}

/* 上报传感器与采样周期属性 */
esp_err_t mqtt_manager_publish_sensor(const sensor_data_t *data)
{
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt_connected || s_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char payload[512];
    int len = snprintf(
        payload,
        sizeof(payload),
        "{"
            "\"id\" :\"1\","
            "\"version\":\"1.0\","
            "\"params\": {"
                "\"temperature_c\":{"
                    "\"value\":%.1f"
                "},"
                "\"humidity_rh\":{"
                    "\"value\":%.1f"
                "},"
                "\"pressure_hpa\":{"
                    "\"value\":%.1f"
                "},"
                "\"sample_period_ms\":{"
                    "\"value\":%lu"
                "}"
            "}"
        "}",
        data->temperature_c,
        data->humidity_rh,
        data->pressure_hpa,
        (unsigned long)config_manager_get()->sample_period_ms
    );

    if (len < 0 || len >= (int)sizeof(payload)) {
        ESP_LOGE(TAG, "payload too long");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "publish topic: %s", MQTT_TOPIC_PROPERTY_POST);
    ESP_LOGI(TAG, "publish payload: %s", payload);

    int msg_id = esp_mqtt_client_publish(
        s_mqtt_client,
        MQTT_TOPIC_PROPERTY_POST,
        payload,
        0,
        1,
        0
    );

    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}
