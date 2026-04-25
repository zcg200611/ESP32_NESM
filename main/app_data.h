#ifndef APP_DATA_H
#define APP_DATA_H

/* 统一的数据模型定义（跨模块共享） */
typedef struct {
    float temperature_c; /* 摄氏温度 */
    float pressure_hpa;  /* 气压（hPa） */
    float humidity_rh;   /* 相对湿度（%RH） */
} sensor_data_t;

#endif
