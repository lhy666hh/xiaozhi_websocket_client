// ili9341.c
#include "ili9341.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
// 在文件开头添加宏定义
#define SPI_MAX_TRANSFER_BYTES  2048   // 单次SPI传输最大字节数，需与 max_transfer_sz 一致
static const char *TAG = "ILI9341";

static spi_device_handle_t spi_handle = NULL;
static uint8_t rotation = 0;

/* 内部函数声明 */
static void ili9341_send_cmd(uint8_t cmd);
static void ili9341_send_data(uint8_t *data, size_t len);
static void ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
//static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b);

/* 初始化命令序列（来自数据手册和稳定配置） */
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint8_t delay_ms;
} init_cmd_t;

static const init_cmd_t init_cmds[] = {
    {0xCF, {0x00, 0xC1, 0x30}, 3, 0},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4, 0},
    {0xE8, {0x85, 0x00, 0x78}, 3, 0},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, {0x20}, 1, 0},
    {0xEA, {0x00, 0x00}, 2, 0},
    {0xC0, {0x13}, 1, 0},          // Power control
    {0xC1, {0x13}, 1, 0},          // Power control
    {0xC5, {0x22, 0x35}, 2, 0},    // VCOM control
    {0xC7, {0xBD}, 1, 0},          // VCOM control
    {0x21, {0}, 0, 0},             // Display Inversion OFF
    {0x36, {0x08}, 1, 0},          // Memory Access Control (default)
    {0xB6, {0x0A, 0x82}, 2, 0},    // Display Function Control
    {0x3A, {0x55}, 1, 0},          // Pixel Format: 16bit/pixel
    {0xF6, {0x01, 0x30}, 2, 0},    // Interface Control
    {0xB1, {0x00, 0x1B}, 2, 0},    // Frame Rate Control
    {0xF2, {0x00}, 1, 0},          // Enable 3G
    {0x26, {0x01}, 1, 0},          // Gamma Set
    {0xE0, {0x0F, 0x35, 0x31, 0x0B, 0x0F, 0x06, 0x49, 0xA7, 0x33, 0x07, 0x0F, 0x03, 0x0C, 0x0A, 0x00}, 15, 0},
    {0xE1, {0x00, 0x0A, 0x0F, 0x04, 0x11, 0x08, 0x36, 0x58, 0x4D, 0x07, 0x10, 0x0C, 0x32, 0x34, 0x0F}, 15, 0},
    {0x11, {0}, 0x80, 120},           // Sleep Out, delay 120ms
    {0x29, {0}, 0x80, 120},           // Display ON
    {0, {0}, 0xff, 0xFF}              // 结束标志
};

/* 初始化SPI总线 */
static esp_err_t spi_bus_init(void)
{
     spi_bus_config_t bus_cfg = {
    .mosi_io_num = ILI9341_MOSI,
    .miso_io_num = ILI9341_MISO,
    .sclk_io_num = ILI9341_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = SPI_MAX_TRANSFER_BYTES,   // 必须与分块大小匹配
	};
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return ret;
    }
	

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 40 * 1000 * 1000,  // 40MHz
        .mode = 0,                // SPI mode 0
        .spics_io_num = ILI9341_CS,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Add SPI device failed");
        spi_bus_free(SPI2_HOST);
    }
    return ret;
}

/* 发送命令字节 */
static void ili9341_send_cmd(uint8_t cmd)
{
    gpio_set_level(ILI9341_DC, 0);  // 命令模式
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(spi_handle, &trans);
}

/* 发送数据 */
static void ili9341_send_data(uint8_t *data, size_t len)
{
    gpio_set_level(ILI9341_DC, 1);  // 数据模式
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(spi_handle, &trans);
}

/* 设置显示窗口（用于局部刷新） */
static void ili9341_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t buf[4];
    // 列地址
    ili9341_send_cmd(0x2A);
    buf[0] = x1 >> 8;
    buf[1] = x1 & 0xFF;
    buf[2] = x2 >> 8;
    buf[3] = x2 & 0xFF;
    ili9341_send_data(buf, 4);
    // 行地址
    ili9341_send_cmd(0x2B);
    buf[0] = y1 >> 8;
    buf[1] = y1 & 0xFF;
    buf[2] = y2 >> 8;
    buf[3] = y2 & 0xFF;
    ili9341_send_data(buf, 4);
    // 准备写内存
    ili9341_send_cmd(0x2C);
}

/* RGB888 -> RGB565 */
// static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
// {
//     return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
// }

/* 公开API实现 */
void ili9341_init(void)
{
    ESP_LOGI(TAG, "Initializing ILI9341");

    // 初始化DC和背光引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ILI9341_DC) | (1ULL << ILI9341_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // 初始化SPI
    ESP_ERROR_CHECK(spi_bus_init());

    // 软件复位
    ili9341_send_cmd(0x01);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 发送初始化命令
    int idx = 0;
    while (init_cmds[idx].len != 0xFF) {
        ili9341_send_cmd(init_cmds[idx].cmd);
        if (init_cmds[idx].len) {
            ili9341_send_data((uint8_t*)init_cmds[idx].data, init_cmds[idx].len);
        }
        if (init_cmds[idx].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(init_cmds[idx].delay_ms));
        }
        idx++;
    }

    // 设置默认旋转（0度）
    ili9341_set_rotation(0);
    // 清屏黑色
    ili9341_clear(ILI9341_BLACK);
    // 默认背光开启（100%）
    ili9341_set_backlight(100);

    ESP_LOGI(TAG, "ILI9341 initialized");
}

void ili9341_set_backlight(uint8_t level)
{
    if (level > 0) {
        gpio_set_level(ILI9341_BL, 1);
    } else {
        gpio_set_level(ILI9341_BL, 0);
    }
    // 如需PWM调光可在此扩展
}

void ili9341_clear(uint16_t color)
{
    uint32_t total_bytes = ILI9341_WIDTH * ILI9341_HEIGHT * 2; // 2字节/像素
    uint32_t chunk_size = SPI_MAX_TRANSFER_BYTES;
    
    // 分配一块 DMA 缓冲区，大小为 chunk_size
    uint8_t *buf = (uint8_t*)heap_caps_malloc(chunk_size, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate clear buffer");
        return;
    }
    
    // 填充缓冲区为指定颜色
    uint16_t *buf16 = (uint16_t*)buf;
    uint32_t pixels_per_chunk = chunk_size / 2;
    for (uint32_t i = 0; i < pixels_per_chunk; i++) {
        buf16[i] = color;
    }
    
    // 设置全屏窗口
    ili9341_set_window(0, 0, ILI9341_WIDTH - 1, ILI9341_HEIGHT - 1);
    gpio_set_level(ILI9341_DC, 1);
    
    // 分块发送
    uint32_t remaining = total_bytes;
    while (remaining > 0) {
        uint32_t send_len = (remaining > chunk_size) ? chunk_size : remaining;
        spi_transaction_t trans = {
            .length = send_len * 8,
            .tx_buffer = buf,
        };
        spi_device_polling_transmit(spi_handle, &trans);
        remaining -= send_len;
    }
    free(buf);
}

void ili9341_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT) return;
    ili9341_set_window(x, y, x, y);
    uint8_t data[2] = { color >> 8, color & 0xFF };
    ili9341_send_data(data, 2);
}

void ili9341_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= ILI9341_WIDTH || y >= ILI9341_HEIGHT) return;
    if (x + w > ILI9341_WIDTH) w = ILI9341_WIDTH - x;
    if (y + h > ILI9341_HEIGHT) h = ILI9341_HEIGHT - y;
    
    uint32_t pixels = w * h;
    uint32_t total_bytes = pixels * 2;
    uint32_t chunk_size = SPI_MAX_TRANSFER_BYTES;
    
    uint8_t *buf = (uint8_t*)heap_caps_malloc(chunk_size, MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate fill buffer");
        return;
    }
    
    uint16_t *buf16 = (uint16_t*)buf;
    uint32_t pixels_per_chunk = chunk_size / 2;
    for (uint32_t i = 0; i < pixels_per_chunk; i++) {
        buf16[i] = color;
    }
    
    ili9341_set_window(x, y, x + w - 1, y + h - 1);
    gpio_set_level(ILI9341_DC, 1);
    
    uint32_t remaining = total_bytes;
    while (remaining > 0) {
        uint32_t send_len = (remaining > chunk_size) ? chunk_size : remaining;
        spi_transaction_t trans = {
            .length = send_len * 8,
            .tx_buffer = buf,
        };
        spi_device_polling_transmit(spi_handle, &trans);
        remaining -= send_len;
    }
    free(buf);
}

void ili9341_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int e2;
    while (1) {
        ili9341_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void ili9341_draw_circle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (x <= y) {
        ili9341_draw_pixel(x0 + x, y0 + y, color);
        ili9341_draw_pixel(x0 + y, y0 + x, color);
        ili9341_draw_pixel(x0 - x, y0 + y, color);
        ili9341_draw_pixel(x0 - y, y0 + x, color);
        ili9341_draw_pixel(x0 + x, y0 - y, color);
        ili9341_draw_pixel(x0 + y, y0 - x, color);
        ili9341_draw_pixel(x0 - x, y0 - y, color);
        ili9341_draw_pixel(x0 - y, y0 - x, color);
        x++;
        if (d > 0) { y--; d = d + 4 * (x - y) + 10; }
        else d = d + 4 * x + 6;
    }
}

/* 简单字符绘制（8x16 ASCII，仅示例，可替换为更完整字体） */
static const uint8_t font8x16[][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    // ... 此处省略完整字体表，实际请从网上获取标准8x16字体
    // 为简洁，仅提供部分字符占位，实际使用时请补充完整ASCII 32~126
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 占位
};

void ili9341_draw_char(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bgcolor)
{
    if (x + 8 > ILI9341_WIDTH || y + 16 > ILI9341_HEIGHT) return;
    uint8_t c = (uint8_t)ch;
    if (c < 32 || c > 126) c = 32; // 空格
    const uint8_t *glyph = font8x16[c - 32];
    for (int row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (line & (1 << (7 - col))) {
                ili9341_draw_pixel(x + col, y + row, color);
            } else {
                ili9341_draw_pixel(x + col, y + row, bgcolor);
            }
        }
    }
}

void ili9341_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bgcolor)
{
    while (*str) {
        ili9341_draw_char(x, y, *str++, color, bgcolor);
        x += 8;
        if (x + 8 > ILI9341_WIDTH) {
            x = 0;
            y += 16;
            if (y + 16 > ILI9341_HEIGHT) break;
        }
    }
}

void ili9341_set_rotation(uint8_t rot)
{
    rotation = rot % 4;
    uint8_t madctl = 0x48; // default: RGB, MX=0, MY=0, MV=0
    switch (rotation) {
        case 0: // 0°
            madctl = 0x48;
            break;
        case 1: // 90°
            madctl = 0xE8;  // MX=1, MV=1, BGR=1 (根据实测调整)
            break;
        case 2: // 180°
            madctl = 0xC8;  // MX=1, MY=1, BGR=1
            break;
        case 3: // 270°
            madctl = 0x68;  // MY=1, MV=1, BGR=1
            break;
    }
    ili9341_send_cmd(0x36);
    ili9341_send_data(&madctl, 1);
    // 重新清屏避免显示异常
    ili9341_clear(ILI9341_BLACK);
}


/**
 * @brief 在指定区域绘制 RGB565 图像数据（分块 DMA 传输）
 * @param x1 起始列坐标
 * @param y1 起始行坐标
 * @param x2 结束列坐标（包含）
 * @param y2 结束行坐标（包含）
 * @param data RGB565 像素数据，按行顺序排列，大小为 (x2-x1+1)*(y2-y1+1)*2 字节
 */
#define SPI_CHUNK_SIZE 2048   // 更保守的值

void ili9341_draw_area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, void *data)
{
    //ESP_LOGI("ILI9341", "Drawing area: %d,%d to %d,%d", x1, y1, x2, y2);
	// 参数检查
    if (x1 >= ILI9341_WIDTH) x1 = ILI9341_WIDTH - 1;
    if (x2 >= ILI9341_WIDTH) x2 = ILI9341_WIDTH - 1;
    if (y1 >= ILI9341_HEIGHT) y1 = ILI9341_HEIGHT - 1;
    if (y2 >= ILI9341_HEIGHT) y2 = ILI9341_HEIGHT - 1;

	if (x1 > x2) {
        uint16_t temp = x1;
        x1 = x2;
        x2 = temp;
    }
    if (y1 > y2) {
        uint16_t temp = y1;
        y1 = y2;
        y2 = temp;
    }

    uint32_t w = x2 - x1 + 1;
    uint32_t h = y2 - y1 + 1;
    uint32_t total_bytes = w * h * sizeof(uint16_t);
    uint32_t chunk_size = SPI_CHUNK_SIZE;   // 改为固定小值

    ili9341_set_window(x1, y1, x2, y2);
    ili9341_send_cmd(0x2C);
    gpio_set_level(ILI9341_DC, 1);

    uint8_t *data_ptr = (uint8_t *)data;
    uint32_t remaining = total_bytes;
    int retry_cnt;

    while (remaining > 0) {
        uint32_t send_len = (remaining > chunk_size) ? chunk_size : remaining;
        spi_transaction_t trans = {
            .length = send_len * 8,
            .tx_buffer = data_ptr,
        };
        esp_err_t ret;
        // 重试最多3次
        for (retry_cnt = 0; retry_cnt < 3; retry_cnt++) {
            ret = spi_device_polling_transmit(spi_handle, &trans);
            if (ret == ESP_OK) break;
            ESP_LOGW("ILI9341", "SPI transmit failed, retry %d, error=%s", retry_cnt, esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (ret != ESP_OK) {
            ESP_LOGE("ILI9341", "SPI transmit fatal error, remaining=%u", remaining);
            return;   // 或者触发系统复位
        }
        data_ptr += send_len;
        remaining -= send_len;
    }
}


