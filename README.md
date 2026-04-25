# ESP32_NEMN

ESP32_NEMN 是一个基于 ESP32-S3 的联网环境监测 IoT 终端项目。系统使用 BME280 采集温度、湿度和大气压，通过 OLED 进行本地数据显示，并接入 OneNET 云平台实现属性上报、远程参数配置和 OTA 固件升级。

## 项目功能

- 使用 BME280 采集环境温度、湿度和大气压
- 使用 OLED 显示传感器数据、网络状态和 OTA 页面
- 支持按键切换本地显示界面
- 支持 Wi-Fi STA 模式联网
- 支持 MQTT 接入 OneNET 云平台
- 支持温度、湿度、气压等属性周期上报
- 支持云端下发采样周期，并写入 NVS 持久化保存
- 支持本地按键触发 OTA 升级
- 支持云端 MQTT 指令触发 OTA 升级
- 支持 OTA 分区与新固件有效性确认
- 采用模块化代码结构，便于后续扩展

## 硬件组成

| 模块 | 说明 |
|---|---|
| 主控 | ESP32-S3-N16R8 |
| 传感器 | BME280 温湿度气压传感器 |
| 显示 | OLED 显示屏 |
| 输入 | 4 个本地按键 |
| 云平台 | OneNET |
| 通信方式 | Wi-Fi + MQTT |

## 软件架构

```text
main/
├── main.c              # 系统主入口与调度
├── board_config.h      # 硬件引脚和默认配置
├── app_data.h          # 通用数据结构
├── sensor_service.c    # 传感器采样服务
├── display_service.c   # OLED 显示服务
├── local_ui.c          # 本地按键和页面逻辑
├── wifi_manager.c      # Wi-Fi 连接管理
├── mqtt_manager.c      # MQTT 上报、下发、OTA 指令处理
├── config_manager.c    # NVS 配置管理
└── ota_manager.c       # OTA 升级管理

drivers/
├── bme280.c            # BME280 驱动
├── oled.c              # OLED 驱动
└── spi.c               # SPI 总线封装

## 系统流程

上电启动
  -> 初始化 OLED
  -> 初始化 NVS 配置
  -> 初始化 BME280
  -> 连接 Wi-Fi
  -> 启动 MQTT
  -> 周期采集传感器数据
  -> 上报 OneNET 云平台
  -> 处理云端下发配置 / OTA 指令
  -> 本地 OLED 页面刷新
