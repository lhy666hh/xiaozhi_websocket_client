/**
 * @file ft6x36.h
 * @brief Independent FT6x36 touch controller driver (no LVGL)
 *        支持中断触发和轮询两种模式
 */

#ifndef FT6X36_H
#define FT6X36_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#ifdef __cplusplus
extern "C" {
#endif

/* I2C 地址 */
#define FT6X36_I2C_ADDR         0x38

/* 最大触摸点数 */
#define FT6X36_MAX_TOUCH        2

/* 寄存器定义 */
#define FT6X36_DEV_MODE_REG     0x00
#define FT6X36_GEST_ID_REG      0x01
#define FT6X36_TD_STAT_REG      0x02
#define FT6X36_P1_XH_REG        0x03
#define FT6X36_P1_XL_REG        0x04
#define FT6X36_P1_YH_REG        0x05
#define FT6X36_P1_YL_REG        0x06
#define FT6X36_P2_XH_REG        0x09
#define FT6X36_P2_XL_REG        0x0A
#define FT6X36_P2_YH_REG        0x0B
#define FT6X36_P2_YL_REG        0x0C
#define FT6X36_CHIPSELECT_REG   0xA3
#define FT6X36_FIRMWARE_ID_REG  0xA6
#define FT6X36_PANEL_ID_REG     0xA8


typedef struct {
    i2c_master_bus_handle_t bus_handle;  // 新版 I2C 总线句柄（外部已创建）
    uint8_t i2c_addr;                    // 设备地址 (0x38)
    uint16_t screen_width;               // 屏幕宽度
    uint16_t screen_height;              // 屏幕高度
    bool swap_xy;
    bool invert_x;
    bool invert_y;
    uint8_t rst_gpio;                    // 复位 GPIO（低电平复位，0或-1表示不用）
    uint8_t intr_gpio;                   // 中断 GPIO（下降沿触发，0或-1表示轮询模式）
} ft6x36_config_t;

/* 触摸状态 */
typedef enum {
    TOUCH_RELEASED = 0,
    TOUCH_PRESSED  = 1,
    TOUCH_CONTACT  = 2
} touch_state_t;

/* 单点触摸数据 */
typedef struct {
    uint16_t x;
    uint16_t y;
    touch_state_t state;
} ft6x36_point_t;

/* 多点触摸数据（最多2点） */
typedef struct {
    uint8_t touch_count;                 // 实际触摸点数 (0,1,2)
    ft6x36_point_t points[FT6X36_MAX_TOUCH];
} ft6x36_data_t;



/**
 * @brief 获取触摸数据（轮询模式或从中断缓存中读取最新数据）
 * @param out_data 输出触摸数据
 * @return ESP_OK 成功
 */
esp_err_t ft6x36_read(ft6x36_data_t *out_data);

/**
 * @brief 动态修改坐标映射方向 (运行时)
 */
void ft6x36_set_orientation(bool swap, bool invert_x, bool invert_y);

/**
 * @brief 获取芯片 ID 字符串 (用于调试)
 */
const char* ft6x36_get_chip_id(void);

esp_err_t ft6x36_init(const ft6x36_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // FT6X36_H