/**
 * @file ft6x36.c
 * @brief FT6x36 touch driver using new I2C driver (i2c_master_bus)
 */

#include "ft6x36.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include <string.h>

#define TAG "FT6X36"

// 静态变量
static i2c_master_dev_handle_t s_dev_handle = NULL;   // I2C 设备句柄
static uint16_t s_width = 240;
static uint16_t s_height = 320;
static bool s_swap_xy = false;
static bool s_invert_x = false;
static bool s_invert_y = false;

// 中断相关
static uint8_t s_rst_gpio = 0;
static uint8_t s_intr_gpio = 0;
static QueueHandle_t s_event_queue = NULL;
static QueueHandle_t s_touch_queue = NULL;
static ft6x36_data_t s_latest_touch;
static bool s_use_interrupt = true;

// 新版 I2C 读写辅助函数
static esp_err_t i2c_write_reg(uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t write_buf[len + 1];
    write_buf[0] = reg;
    if (len && data) {
        memcpy(write_buf + 1, data, len);
    }
    return i2c_master_transmit(s_dev_handle, write_buf, len + 1, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *buf, size_t len)
{
    // 先发送寄存器地址
    esp_err_t ret = i2c_master_transmit(s_dev_handle, &reg, 1, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) return ret;
    // 再读取数据
    return i2c_master_receive(s_dev_handle, buf, len, pdMS_TO_TICKS(100));
}

// 坐标变换
static void transform_coordinates(uint16_t *x, uint16_t *y)
{
    uint16_t tx = *x, ty = *y;
    if (s_swap_xy) { tx = *y; ty = *x; }
    if (s_invert_x) tx = s_width - tx;
    if (s_invert_y) ty = s_height - ty;
    *x = tx; *y = ty;
}

// 读取单点触摸
static bool read_single_point(uint8_t index, ft6x36_point_t *point)
{
    uint8_t base_reg = (index == 0) ? 0x03 : 0x09;
    uint8_t buf[6];
    esp_err_t ret = i2c_read_reg(base_reg, buf, 6);
    if (ret != ESP_OK) return false;

    uint8_t event = (buf[2] >> 6) & 0x03;
    uint16_t x = ((buf[0] & 0x0F) << 8) | buf[1];
    uint16_t y = ((buf[2] & 0x0F) << 8) | buf[3];

    if (x > s_width) x = s_width;
    if (y > s_height) y = s_height;

    point->x = x;
    point->y = y;

    switch (event) {
        case 0: point->state = TOUCH_PRESSED; break;
        case 1: point->state = TOUCH_RELEASED; break;
        case 2: point->state = TOUCH_CONTACT; break;
        default: point->state = TOUCH_RELEASED; break;
    }

    if (x == 0 && y == 0 && event == 1) {
        point->state = TOUCH_RELEASED;
    }
    return true;
}

// 读取所有触摸点（原始坐标，无变换）
static esp_err_t read_raw_touch(ft6x36_data_t *data)
{
    uint8_t td_stat;
    esp_err_t ret = i2c_read_reg(FT6X36_TD_STAT_REG, &td_stat, 1);
    if (ret != ESP_OK) {
        data->touch_count = 0;
        return ret;
    }

    uint8_t touch_cnt = td_stat & 0x0F;
    if (touch_cnt > FT6X36_MAX_TOUCH) touch_cnt = FT6X36_MAX_TOUCH;
    data->touch_count = touch_cnt;

    for (int i = 0; i < touch_cnt; i++) {
        if (!read_single_point(i, &data->points[i])) {
            data->touch_count = 0;
            return ESP_FAIL;
        }
    }

    for (int i = touch_cnt; i < FT6X36_MAX_TOUCH; i++) {
        data->points[i].state = TOUCH_RELEASED;
        data->points[i].x = 0;
        data->points[i].y = 0;
    }
    return ESP_OK;
}

// 硬件复位
static void hardware_reset(void)
{
    if (s_rst_gpio == 0 || s_rst_gpio == GPIO_NUM_NC) return;
    gpio_set_level(s_rst_gpio, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(s_rst_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// 中断服务函数
static void IRAM_ATTR intr_handler(void *arg)
{
    uint32_t dummy = 0;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_event_queue) {
        xQueueSendFromISR(s_event_queue, &dummy, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// 中断扫描任务
static void touch_scan_task(void *arg)
{
    uint32_t dummy;
    ft6x36_data_t touch_data;
    while (1) {
        if (xQueueReceive(s_event_queue, &dummy, portMAX_DELAY) == pdTRUE) {
            if (read_raw_touch(&touch_data) == ESP_OK) {
                for (int i = 0; i < touch_data.touch_count; i++) {
                    transform_coordinates(&touch_data.points[i].x, &touch_data.points[i].y);
                }
                if (s_touch_queue) {
                    xQueueOverwrite(s_touch_queue, &touch_data);
                }
                memcpy(&s_latest_touch, &touch_data, sizeof(ft6x36_data_t));
            } else {
                ESP_LOGE(TAG, "I2C read error in interrupt task");
            }
        }
    }
}

// ------------------- 公共 API -------------------

esp_err_t ft6x36_init(const ft6x36_config_t *config)
{
    if (!config || !config->bus_handle) return ESP_ERR_INVALID_ARG;

    s_width = config->screen_width;
    s_height = config->screen_height;
    s_swap_xy = config->swap_xy;
    s_invert_x = config->invert_x;
    s_invert_y = config->invert_y;
    s_rst_gpio = config->rst_gpio;
    s_intr_gpio = config->intr_gpio;
    s_use_interrupt = (s_intr_gpio != 0 && s_intr_gpio != GPIO_NUM_NC);

    // 1. 配置复位引脚
    if (s_rst_gpio != 0 && s_rst_gpio != GPIO_NUM_NC) {
        gpio_config_t rst_cfg = {
            .pin_bit_mask = (1ULL << s_rst_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&rst_cfg);
        gpio_set_level(s_rst_gpio, 1);
    }
    hardware_reset();

    // 2. 在新版 I2C 总线上添加设备
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(config->bus_handle, &dev_cfg, &s_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 验证通信
    uint8_t chip_id = 0;
    ret = i2c_read_reg(FT6X36_CHIPSELECT_REG, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C communication failed, check wiring");
        return ret;
    }
    ESP_LOGI(TAG, "Chip ID: 0x%02X", chip_id);

    uint8_t panel_id = 0;
    i2c_read_reg(FT6X36_PANEL_ID_REG, &panel_id, 1);
    ESP_LOGI(TAG, "Panel ID: 0x%02X", panel_id);

    uint8_t fw_id = 0;
    i2c_read_reg(FT6X36_FIRMWARE_ID_REG, &fw_id, 1);
    ESP_LOGI(TAG, "Firmware ID: 0x%02X", fw_id);

    // 设置工作模式
    uint8_t mode = 0x00;
    i2c_write_reg(FT6X36_DEV_MODE_REG, &mode, 1);

    // 4. 初始化中断模式
    if (s_use_interrupt) {
        s_event_queue = xQueueCreate(1, sizeof(uint32_t));
        s_touch_queue = xQueueCreate(1, sizeof(ft6x36_data_t));
        if (!s_event_queue || !s_touch_queue) {
            ESP_LOGE(TAG, "Queue creation failed");
            return ESP_ERR_NO_MEM;
        }

        gpio_config_t intr_cfg = {
            .pin_bit_mask = (1ULL << s_intr_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&intr_cfg);

        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "ISR service install failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = gpio_isr_handler_add(s_intr_gpio, intr_handler, NULL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(ret));
            return ret;
        }

        xTaskCreate(touch_scan_task, "ft6x36_task", 4096, NULL, 10, NULL);
        ESP_LOGI(TAG, "FT6x36 initialized with interrupt on GPIO%d", s_intr_gpio);
    } else {
        ESP_LOGI(TAG, "FT6x36 initialized in polling mode");
    }

    return ESP_OK;
}

esp_err_t ft6x36_read(ft6x36_data_t *out_data)
{
    if (!out_data) return ESP_ERR_INVALID_ARG;

    if (s_use_interrupt) {
        if (s_touch_queue && xQueueReceive(s_touch_queue, out_data, 0) == pdTRUE) {
            return ESP_OK;
        } else {
            memcpy(out_data, &s_latest_touch, sizeof(ft6x36_data_t));
            return ESP_ERR_TIMEOUT;
        }
    } else {
        // 轮询模式
        uint8_t td_stat;
        esp_err_t ret = i2c_read_reg(FT6X36_TD_STAT_REG, &td_stat, 1);
        if (ret != ESP_OK) {
            out_data->touch_count = 0;
            return ret;
        }
        uint8_t touch_cnt = td_stat & 0x0F;
        if (touch_cnt > FT6X36_MAX_TOUCH) touch_cnt = FT6X36_MAX_TOUCH;
        out_data->touch_count = touch_cnt;
        for (int i = 0; i < touch_cnt; i++) {
            if (!read_single_point(i, &out_data->points[i])) {
                out_data->touch_count = 0;
                return ESP_FAIL;
            }
            transform_coordinates(&out_data->points[i].x, &out_data->points[i].y);
        }
        for (int i = touch_cnt; i < FT6X36_MAX_TOUCH; i++) {
            out_data->points[i].state = TOUCH_RELEASED;
            out_data->points[i].x = 0;
            out_data->points[i].y = 0;
        }
        return ESP_OK;
    }
}

void ft6x36_set_orientation(bool swap, bool invert_x, bool invert_y)
{
    s_swap_xy = swap;
    s_invert_x = invert_x;
    s_invert_y = invert_y;
}

const char* ft6x36_get_chip_id(void)
{
    static char id_str[16];
    uint8_t chip = 0;
    if (i2c_read_reg(FT6X36_CHIPSELECT_REG, &chip, 1) == ESP_OK) {
        snprintf(id_str, sizeof(id_str), "FT%02X", chip);
        return id_str;
    }
    return "Unknown";
}

// 删除原来的 ft6x36_config 函数，或改成无操作，避免编译错误
void ft6x36_config(void)
{
    ESP_LOGW(TAG, "ft6x36_config() is deprecated, please use ft6x36_init with bus_handle");
}