#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "esp_err.h"

/* 执行 OTA 升级（成功后会重启） */
esp_err_t ota_manager_start(const char *url);

/* 若当前固件处于待确认状态，则标记为有效，防止回滚 */
void ota_manager_mark_app_valid_if_needed(void);

#endif
