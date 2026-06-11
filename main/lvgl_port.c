

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"
#include "esp_log.h"
#include "ili9341.h"
#include "ft6x36.h"
#include "esp_timer.h"

#define LCD_WIDTH   240
#define LCD_HEIGHT  320
#define TAG         "lv_port"

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t disp_buf;   // 移到文件作用域，避免被优化

void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    // 将缓冲区数据发送到屏幕（同步传输）
    ili9341_draw_area(area->x1, area->y1, area->x2, area->y2, color_p);
    // 必须通知 LVGL 刷新完成
    lv_disp_flush_ready(disp_drv);
}

void lv_disp_init(void)
{
    const size_t disp_buf_size = LCD_WIDTH * 40;   // 一行 * 20 行
    lv_color_t * disp1 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), 
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
	
	lv_color_t * disp2 = heap_caps_malloc(disp_buf_size * sizeof(lv_color_t), 
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!disp1||!disp2) {
        ESP_LOGE(TAG, "disp buff malloc fail!");
        return;
    }

    // 初始化绘制缓冲区（单缓冲模式，第二个参数为 NULL）
    lv_disp_draw_buf_init(&disp_buf, disp1,disp2, disp_buf_size);

    // 初始化显示驱动
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res    = LCD_WIDTH;
    disp_drv.ver_res    = LCD_HEIGHT;
    disp_drv.flush_cb   = disp_flush;
    disp_drv.draw_buf   = &disp_buf;     // ⚠️ 关键：关联缓冲区！

    lv_disp_drv_register(&disp_drv);
}

void indev_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    int16_t x = 0, y = 0;
    uint8_t state = 0;
    ft6x36_data_t touch;

    if (ft6x36_read(&touch) == ESP_OK && touch.touch_count > 0) {
        if (touch.points[0].state == TOUCH_PRESSED) {
            state = 1;
            x = (int16_t)touch.points[0].x;
            y = (int16_t)touch.points[0].y;
			//printf("Touch %d: (%d, %d)\n", 0, touch.points[0].x, touch.points[0].y);
        }
    }

    data->point.x = x;
    data->point.y = y;
    data->state = state;
}

void lv_indev_init(void)
{
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = indev_read;
    lv_indev_drv_register(&indev_drv);
}

void lv_timer_cb(void *arg)
{
    uint32_t tick_interval = *(uint32_t*)arg;
    //static uint32_t count = 0;
    // if (count++ % 100 == 0) {
    //     ESP_LOGI("LV_TICK", "tick_interval=%lu", tick_interval);
    // }
    lv_tick_inc(tick_interval);
}

void lv_tick_init(void)
{
    static uint32_t tick_interval = 5;
    const esp_timer_create_args_t arg = {
        .arg = &tick_interval,
        .callback = lv_timer_cb,
        .name = "lv_tick",
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer_handle;
    esp_timer_create(&arg, &timer_handle);
    esp_timer_start_periodic(timer_handle, tick_interval * 1000);
}

void lv_port_init(void)
{
    ESP_LOGI(TAG, "lv_init!");
    lv_init();

    ESP_LOGI(TAG, "lv_disp_init");
    lv_disp_init();

    ESP_LOGI(TAG, "lv_indev_init!");
    lv_indev_init();

    ESP_LOGI(TAG, "lv_tick_init");
    lv_tick_init();     // ⚠️ 取消注释，必须提供心跳

    ESP_LOGI(TAG, "lv_port_init ok!");
}