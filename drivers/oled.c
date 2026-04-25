#include "oled.h"

#include <stdbool.h>
#include <stddef.h>

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_config.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#include "oled_Font.h"
#pragma GCC diagnostic pop

#define OLED_I2C_PORT ((i2c_port_t)BOARD_I2C_PORT_NUM)
#define OLED_I2C_SCL ((gpio_num_t)BOARD_OLED_I2C_SCL)
#define OLED_I2C_SDA ((gpio_num_t)BOARD_OLED_I2C_SDA)
#define OLED_I2C_FREQ_HZ BOARD_OLED_I2C_FREQ_HZ
#define OLED_ADDR_3C 0x3C
#define OLED_ADDR_3D 0x3D
#define OLED_TIMEOUT_MS 100

static bool s_i2c_ready = false;
static uint8_t s_oled_addr = OLED_ADDR_3C;
static const char *TAG = "OLED";

static void OLED_WriteCommand(uint8_t command);
static void OLED_WriteData(uint8_t data);
static void OLED_WriteDataBuffer(const uint8_t *data, size_t len);
static void OLED_SetCursor(uint8_t y, uint8_t x);
static uint32_t OLED_Pow(uint32_t x, uint32_t y);
static esp_err_t OLED_ProbeAddress(uint8_t addr);

static void OLED_I2C_Init(void)
{
    if (s_i2c_ready) {
        return;
    }

    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = OLED_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(OLED_I2C_PORT, &i2c_cfg));
    esp_err_t ret = i2c_driver_install(OLED_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }

    if (OLED_ProbeAddress(OLED_ADDR_3C) == ESP_OK) {
        s_oled_addr = OLED_ADDR_3C;
    } else if (OLED_ProbeAddress(OLED_ADDR_3D) == ESP_OK) {
        s_oled_addr = OLED_ADDR_3D;
    } else {
        ESP_LOGE(TAG, "No OLED found on I2C. Check wiring/power. Tried 0x3C and 0x3D.");
        ESP_ERROR_CHECK(ESP_ERR_NOT_FOUND);
    }
    ESP_LOGI(TAG, "OLED detected at 0x%02X (SCL=%d SDA=%d)", s_oled_addr, OLED_I2C_SCL, OLED_I2C_SDA);

    s_i2c_ready = true;
}

static esp_err_t OLED_ProbeAddress(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(OLED_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static void OLED_WriteCommand(uint8_t command)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_oled_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_write_byte(cmd, command, true);
    i2c_master_stop(cmd);
    ESP_ERROR_CHECK(i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(OLED_TIMEOUT_MS)));
    i2c_cmd_link_delete(cmd);
}

static void OLED_WriteData(uint8_t data)
{
    OLED_WriteDataBuffer(&data, 1);
}

static void OLED_WriteDataBuffer(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_oled_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x40, true);
    for (size_t i = 0; i < len; i++) {
        i2c_master_write_byte(cmd, data[i], true);
    }
    i2c_master_stop(cmd);
    ESP_ERROR_CHECK(i2c_master_cmd_begin(OLED_I2C_PORT, cmd, pdMS_TO_TICKS(OLED_TIMEOUT_MS)));
    i2c_cmd_link_delete(cmd);
}

static void OLED_SetCursor(uint8_t y, uint8_t x)
{
    OLED_WriteCommand(0xB0 | y);
    OLED_WriteCommand(0x10 | ((x & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (x & 0x0F));
}

void OLED_Clear(void)
{
    uint8_t zero_line[128] = {0};

    for (uint8_t page = 0; page < 8; page++) {
        OLED_SetCursor(page, 0);
        OLED_WriteDataBuffer(zero_line, sizeof(zero_line));
    }
}

void OLED_ShowChar(uint8_t line, uint8_t column, char ch)
{
    uint8_t index = (uint8_t)(ch - ' ');
    OLED_SetCursor((line - 1) * 2, (column - 1) * 8);
    OLED_WriteDataBuffer(&OLED_F8x16[index][0], 8);
    OLED_SetCursor((line - 1) * 2 + 1, (column - 1) * 8);
    OLED_WriteDataBuffer(&OLED_F8x16[index][8], 8);
}

void OLED_ShowString(uint8_t line, uint8_t column, const char *string)
{
    for (uint8_t i = 0; string[i] != '\0'; i++) {
        OLED_ShowChar(line, column + i, string[i]);
    }
}

static uint32_t OLED_Pow(uint32_t x, uint32_t y)
{
    uint32_t result = 1;
    while (y--) {
        result *= x;
    }
    return result;
}

void OLED_ShowNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        OLED_ShowChar(line, column + i, number / OLED_Pow(10, length - i - 1) % 10 + '0');
    }
}

void OLED_ShowSignedNum(uint8_t line, uint8_t column, int32_t number, uint8_t length)
{
    uint32_t abs_num;
    if (number >= 0) {
        OLED_ShowChar(line, column, '+');
        abs_num = (uint32_t)number;
    } else {
        OLED_ShowChar(line, column, '-');
        abs_num = (uint32_t)(-number);
    }

    for (uint8_t i = 0; i < length; i++) {
        OLED_ShowChar(line, column + i + 1, abs_num / OLED_Pow(10, length - i - 1) % 10 + '0');
    }
}

void OLED_ShowHexNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        uint8_t single = number / OLED_Pow(16, length - i - 1) % 16;
        if (single < 10) {
            OLED_ShowChar(line, column + i, single + '0');
        } else {
            OLED_ShowChar(line, column + i, single - 10 + 'A');
        }
    }
}

void OLED_ShowBinNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++) {
        OLED_ShowChar(line, column + i, number / OLED_Pow(2, length - i - 1) % 2 + '0');
    }
}

void OLED_Init(void)
{
    OLED_I2C_Init();
    vTaskDelay(pdMS_TO_TICKS(100));

    OLED_WriteCommand(0xAE);
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);
    OLED_WriteCommand(0x40);
    OLED_WriteCommand(0xA1);
    OLED_WriteCommand(0xC8);
    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);
    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);
    OLED_WriteCommand(0xA4);
    OLED_WriteCommand(0xA6);
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    OLED_WriteCommand(0xAF);

    OLED_Clear();
}
