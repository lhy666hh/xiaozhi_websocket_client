// ili9341.h
#ifndef ILI9341_H
#define ILI9341_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 硬件引脚配置 */
#define ILI9341_CS       10   // 片选 IO10
#define ILI9341_DC       46   // 命令/数据选择 IO46
#define ILI9341_MOSI     11   // SPI数据输入
#define ILI9341_SCLK     12   // SPI时钟
#define ILI9341_MISO     13   // SPI数据输出（可选，未使用）
#define ILI9341_BL       45   // 背光 IO45

/* 屏幕分辨率 */
#define ILI9341_WIDTH    240
#define ILI9341_HEIGHT   320

/* 颜色定义 (RGB565) */
#define ILI9341_BLACK     0x0000
#define ILI9341_WHITE     0xFFFF
#define ILI9341_RED       0xF800
#define ILI9341_GREEN     0x07E0
#define ILI9341_BLUE      0x001F
#define ILI9341_YELLOW    0xFFE0
#define ILI9341_CYAN      0x07FF
#define ILI9341_MAGENTA   0xF81F

/* API 函数 */
void ili9341_init(void);                     // 初始化显示屏
void ili9341_set_backlight(uint8_t level);   // 背光控制 0~100
void ili9341_clear(uint16_t color);          // 清屏
void ili9341_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ili9341_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ili9341_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ili9341_draw_char(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bgcolor);
void ili9341_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgcolor);
void ili9341_set_rotation(uint8_t rotation); // 0-3: 0°,90°,180°,270°

void ili9341_draw_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void *data);


#ifdef __cplusplus
}
#endif

#endif