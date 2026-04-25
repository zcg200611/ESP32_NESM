#ifndef __BME280_H_
#define __BME280_H_

#include "esp_err.h"

typedef struct {
    float temperature_c;
    float pressure_hpa;
    float humidity_rh;
} bme280_data_t;

esp_err_t bme280_init(void);
esp_err_t bme280_read_data(bme280_data_t *out);

#endif
