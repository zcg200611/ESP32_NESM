#include "bme280.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi.h"

#define BME280_REG_CHIP_ID 0xD0     //查数据手册得到设备ID等其他地址
#define BME280_REG_RESET 0xE0
#define BME280_REG_CTRL_HUM 0xF2    //配置湿度参数
#define BME280_REG_CTRL_MEAS 0xF4   //配置温度参数
#define BME280_REG_CONFIG 0xF5
#define BME280_REG_PRESS_MSB 0xF7   //大气压

#define BME280_CHIP_ID 0x60

typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;

    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_t;

static const char *TAG = "BME280";
static bme280_calib_t s_calib;
static int32_t s_t_fine;
static bool s_ready = false;

static esp_err_t bme280_read_calibration(void)
{
    uint8_t calib1[26] = {0};
    uint8_t calib2[7] = {0};

    ESP_RETURN_ON_ERROR(spi2_read_bytes(0x88, calib1, sizeof(calib1)), TAG, "read calib1 failed");
    ESP_RETURN_ON_ERROR(spi2_read_bytes(0xE1, calib2, sizeof(calib2)), TAG, "read calib2 failed");

    s_calib.dig_T1 = (uint16_t)(calib1[1] << 8 | calib1[0]);
    s_calib.dig_T2 = (int16_t)(calib1[3] << 8 | calib1[2]);
    s_calib.dig_T3 = (int16_t)(calib1[5] << 8 | calib1[4]);

    s_calib.dig_P1 = (uint16_t)(calib1[7] << 8 | calib1[6]);
    s_calib.dig_P2 = (int16_t)(calib1[9] << 8 | calib1[8]);
    s_calib.dig_P3 = (int16_t)(calib1[11] << 8 | calib1[10]);
    s_calib.dig_P4 = (int16_t)(calib1[13] << 8 | calib1[12]);
    s_calib.dig_P5 = (int16_t)(calib1[15] << 8 | calib1[14]);
    s_calib.dig_P6 = (int16_t)(calib1[17] << 8 | calib1[16]);
    s_calib.dig_P7 = (int16_t)(calib1[19] << 8 | calib1[18]);
    s_calib.dig_P8 = (int16_t)(calib1[21] << 8 | calib1[20]);
    s_calib.dig_P9 = (int16_t)(calib1[23] << 8 | calib1[22]);

    s_calib.dig_H1 = calib1[25];
    s_calib.dig_H2 = (int16_t)(calib2[1] << 8 | calib2[0]);
    s_calib.dig_H3 = calib2[2];
    s_calib.dig_H4 = (int16_t)((calib2[3] << 4) | (calib2[4] & 0x0F));
    s_calib.dig_H5 = (int16_t)((calib2[5] << 4) | (calib2[4] >> 4));
    s_calib.dig_H6 = (int8_t)calib2[6];

    return ESP_OK;
}

//使用数据手册中的标准32位代码
static int32_t bme280_compensate_temp(int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) * ((int32_t)s_calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) * ((int32_t)s_calib.dig_T3)) >> 14;
    s_t_fine = var1 + var2;
    return (s_t_fine * 5 + 128) >> 8;
}

static uint32_t bme280_compensate_press(int32_t adc_P)
{
    int64_t var1 = ((int64_t)s_t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) + ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * ((int64_t)s_calib.dig_P1)) >> 33;

    if (var1 == 0) {
        return 0;
    }

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);

    return (uint32_t)p;
}

static uint32_t bme280_compensate_hum(int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = s_t_fine - ((int32_t)76800);
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)s_calib.dig_H4) << 20) - (((int32_t)s_calib.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) *
                 (((((((v_x1_u32r * ((int32_t)s_calib.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)s_calib.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) *
                   ((int32_t)s_calib.dig_H2) + 8192) >>
                  14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)s_calib.dig_H1)) >> 4);
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);

    return (uint32_t)(v_x1_u32r >> 12);
}

esp_err_t bme280_init(void)
{
    spi2_init();

    uint8_t chip_id = 0;
    ESP_RETURN_ON_ERROR(spi2_read_bytes(BME280_REG_CHIP_ID, &chip_id, 1), TAG, "read chip id failed");
    ESP_RETURN_ON_FALSE(chip_id == BME280_CHIP_ID, ESP_ERR_INVALID_RESPONSE, TAG, "unexpected chip id: 0x%02X", chip_id);

    uint8_t reset_cmd = 0xB6;
    ESP_RETURN_ON_ERROR(spi2_write_bytes(BME280_REG_RESET, &reset_cmd, 1), TAG, "reset failed");
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_RETURN_ON_ERROR(bme280_read_calibration(), TAG, "read calibration failed");

    uint8_t ctrl_hum = 0x01;
    uint8_t ctrl_meas = 0x27;
    uint8_t config = 0xA0;

    ESP_RETURN_ON_ERROR(spi2_write_bytes(BME280_REG_CTRL_HUM, &ctrl_hum, 1), TAG, "write ctrl_hum failed");
    ESP_RETURN_ON_ERROR(spi2_write_bytes(BME280_REG_CTRL_MEAS, &ctrl_meas, 1), TAG, "write ctrl_meas failed");
    ESP_RETURN_ON_ERROR(spi2_write_bytes(BME280_REG_CONFIG, &config, 1), TAG, "write config failed");

    s_ready = true;
    ESP_LOGI(TAG, "BME280 initialized (chip id 0x%02X)", chip_id);
    return ESP_OK;
}

esp_err_t bme280_read_data(bme280_data_t *out)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw[8] = {0};
    ESP_RETURN_ON_ERROR(spi2_read_bytes(BME280_REG_PRESS_MSB, raw, sizeof(raw)), TAG, "read raw data failed");

    int32_t adc_P = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4));
    int32_t adc_T = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | ((uint32_t)raw[5] >> 4));
    int32_t adc_H = (int32_t)(((uint32_t)raw[6] << 8) | (uint32_t)raw[7]);

    int32_t t = bme280_compensate_temp(adc_T);
    uint32_t p = bme280_compensate_press(adc_P);
    uint32_t h = bme280_compensate_hum(adc_H);

    out->temperature_c = (float)t / 100.0f;
    out->pressure_hpa = (float)p / 25600.0f;
    out->humidity_rh = (float)h / 1024.0f;

    return ESP_OK;
}
