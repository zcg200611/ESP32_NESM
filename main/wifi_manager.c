#include "wifi_manager.h"

#include <string.h>

#include "board_config.h"
#include "config_manager.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

/* Wi-Fi 连接成功事件位 */
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI";

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_wifi_connected = false;
static esp_netif_t *s_sta_netif = NULL;

/* 处理 Wi-Fi 事件：启动连接、掉线重连 */
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    }
}

/* 获取 IP 事件，标记连接成功 */
static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    (void)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* 初始化 Wi-Fi STA，使用配置层中的 SSID/密码 */
esp_err_t wifi_manager_init(void)
{
    const app_config_t *cfg = config_manager_get();

    if (s_wifi_event_group == NULL) {
        s_wifi_event_group = xEventGroupCreate();
        if (s_wifi_event_group == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &ip_event_handler,
        NULL,
        NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    memcpy(wifi_cfg.sta.ssid, cfg->wifi_ssid, strlen(cfg->wifi_ssid));
    memcpy(wifi_cfg.sta.password, cfg->wifi_password, strlen(cfg->wifi_password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    return ESP_OK;
}

/* 启动 Wi-Fi 驱动 */
esp_err_t wifi_manager_start(void)
{
    return esp_wifi_start();
}

/* 手动触发一次重连 */
esp_err_t wifi_manager_reconnect(void)
{
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_CONNECT) {
        return ret;
    }
    return esp_wifi_connect();
}

/* 查询当前是否已连接 */
bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

/* 在指定超时时间内等待连接成功 */
bool wifi_manager_wait_connected(TickType_t timeout_ticks)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout_ticks);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}
