#include "ota_manager.h"

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

static const char *TAG = "OTA";

/* 如果当前固件处于待确认状态，启动后标记为有效，避免回滚 */
void ota_manager_mark_app_valid_if_needed(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK &&
        ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "new app verified, mark valid");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

/* 按 URL 执行 OTA，成功后立即重启 */
esp_err_t ota_manager_start(const char *url)
{
    if (url == NULL || strlen(url) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "start ota: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ota success, reboot now");
        esp_restart();
    }

    ESP_LOGE(TAG, "ota failed: %s", esp_err_to_name(ret));
    return ret;
}
