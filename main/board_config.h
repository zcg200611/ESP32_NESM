#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* -------------------- 硬件引脚配置 -------------------- */
#define BOARD_I2C_PORT_NUM          0
#define BOARD_OLED_I2C_SCL          4
#define BOARD_OLED_I2C_SDA          5
#define BOARD_OLED_I2C_FREQ_HZ      100000

#define BOARD_SPI_HOST_NUM          1
#define BOARD_SPI_SCLK              16
#define BOARD_SPI_MOSI              7
#define BOARD_SPI_MISO              15
#define BOARD_SPI_CS_BME280         6
#define BOARD_SPI_BME280_FREQ_HZ    (5 * 1000 * 1000)

/* -------------------- 应用时序 -------------------- */
#define APP_SENSOR_SAMPLE_PERIOD_MS 1000
#define APP_UI_POLL_PERIOD_MS       100

/* -------------------- Wi-Fi 默认配置 -------------------- */
#define WIFI_STA_SSID               "YOUR_WIFI_SSID"
#define WIFI_STA_PASSWORD           "YOUR_WIFI_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS     15000

/* -------------------- OTA 配置 -------------------- */
/* 本地测试 OTA 包地址（手动触发 OTA 时使用） */
#define OTA_TEST_URL                "http://YOUR_SERVER/ESP32_NEMN.bin"

/* 本地 UI 显示版本号 */
#define APP_VERSION_STRING          "v1.1.1"

/* -------------------- 本地按键（低电平按下） -------------------- */
#define BOARD_KEY_S1_GPIO           10
#define BOARD_KEY_S2_GPIO           11
#define BOARD_KEY_S3_GPIO           12
#define BOARD_KEY_S4_GPIO           13

/* -------------------- OneNET MQTT 账号 -------------------- */
#define MQTT_BROKER_URI             "mqtt://183.230.40.96:1883"
/* TLS 示例：
#define MQTT_BROKER_URI             "mqtts://183.230.40.16:8883"
*/

#define MQTT_PRODUCT_ID             "YOUR_PRODUCT_ID"
#define MQTT_DEVICE_NAME            "YOUR_DEVICE_NAME"
#define MQTT_CLIENT_ID              "YOUR_DEVICE_NAME"
#define MQTT_USERNAME               MQTT_PRODUCT_ID
#define MQTT_PASSWORD               "YOUR_ONENET_TOKEN"

/* -------------------- OneNET 属性相关主题 -------------------- */
#define MQTT_TOPIC_PROPERTY_POST      "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/thing/property/post"
#define MQTT_TOPIC_PROPERTY_REPLY     "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/thing/property/post/reply"
#define MQTT_TOPIC_PROPERTY_SET       "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/thing/property/set"
#define MQTT_TOPIC_PROPERTY_SET_REPLY "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/thing/property/set_reply"

/* -------------------- OneNET OTA 通知主题 -------------------- */
#define MQTT_TOPIC_OTA_INFORM       "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/ota/inform"
#define MQTT_TOPIC_OTA_INFORM_REPLY "$sys/" MQTT_PRODUCT_ID "/" MQTT_DEVICE_NAME "/ota/inform_reply"

#endif
