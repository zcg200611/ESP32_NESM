#ifndef __MYSPI_H_
#define __MYSPI_H_

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

void spi2_init(void);
esp_err_t spi2_write_bytes(uint8_t reg, const uint8_t *data, size_t len);
esp_err_t spi2_read_bytes(uint8_t reg, uint8_t *data, size_t len);

#endif
