#include "spi.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "board_config.h"

static spi_device_handle_t spi2_handle;
static bool s_spi_ready = false;
#define SPI_HOST ((spi_host_device_t)BOARD_SPI_HOST_NUM)

void spi2_init(void)
{
    if (s_spi_ready) {
        return;
    }

    spi_bus_config_t bus_cfg = {
        .flags = SPICOMMON_BUSFLAG_MASTER,
        .intr_flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .max_transfer_sz = 64,
        .miso_io_num = (gpio_num_t)BOARD_SPI_MISO,
        .mosi_io_num = (gpio_num_t)BOARD_SPI_MOSI,
        .quadhd_io_num = -1,
        .quadwp_io_num = -1,
        .sclk_io_num = (gpio_num_t)BOARD_SPI_SCLK,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .clock_source = SPI_CLK_SRC_DEFAULT,
        .clock_speed_hz = BOARD_SPI_BME280_FREQ_HZ,
        .mode = 0,
        .queue_size = 3,
        .spics_io_num = (gpio_num_t)BOARD_SPI_CS_BME280,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_HOST, &dev_cfg, &spi2_handle));

    s_spi_ready = true;
}

esp_err_t spi2_write_bytes(uint8_t reg, const uint8_t *data, size_t len)
{
    if (!s_spi_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || len == 0 || len > 31) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[32];                 //发送缓冲区域
    tx_buf[0] = reg & 0x7F;             // 第1个字节放寄存器地址，强制写
    memcpy(&tx_buf[1], data, len);      // 把data指向的len个字节，复制到tx_buf[1]开始的位置

    spi_transaction_t trans = {
        .length = (len + 1) * 8,        //发送的是字节，结构体里的时bit，所以要*8
        .tx_buffer = tx_buf,
    };

    return spi_device_polling_transmit(spi2_handle, &trans);
}

esp_err_t spi2_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_spi_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == NULL || len == 0 || len > 31) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buf[32] = {0};
    uint8_t rx_buf[32] = {0};
    tx_buf[0] = reg | 0x80;             //强制读

    spi_transaction_t trans = {
        .length = (len + 1) * 8,
        .tx_buffer = tx_buf,
        .rx_buffer = rx_buf,
    };

    esp_err_t ret = spi_device_polling_transmit(spi2_handle, &trans);
    if (ret != ESP_OK) {
        return ret;
    }

    memcpy(data, &rx_buf[1], len);
    return ESP_OK;
}
