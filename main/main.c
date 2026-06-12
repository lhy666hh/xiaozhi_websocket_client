#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "rgb.h"
#include "exit.h"
#include "uart.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"



#include "nvs_flash.h"
#include "sd.h"
#include <math.h>
//#include "audio_code.h"
#include "audio_player.h"
#include "ili9341.h"
#include "ft6x36.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "mjpeg_ui.h"
#include "esp_lv_decoder.h"

#include "my_wifi.h"
#include "xiaozhi_client.h"
#include "audio_recorder.h"

#include "esp_heap_caps.h"//动态堆检测库
static const char *TAG = "AUDIO_TEST";

#define TEST_TAG "DRAW_AREA_TEST"




// 1. 常用的简单快照对比

void debug_memory_leak(void) {
    // 操作前记录空闲内存
    size_t before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    // ... 执行可能泄漏内存的操作 ...
    // 操作后再次记录，计算差值判断是否泄漏
    size_t after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI("MEM", "Before: %d, After: %d, Diff: %d", before, after, before - after);
}

// 在你的 main.c 文件中直接添加这个函数，不需要任何声明
void vApplicationStackOverflowHook( TaskHandle_t xTask, char * pcTaskName ) {
    // 当栈溢出发生时，系统会调用这个函数。
    // 这里会打印出问题的任务名称，帮助你立刻定位是哪个任务栈溢出了！
    ESP_LOGE("STACK_OVERFLOW", "检测到栈溢出！任务名: %s", pcTaskName);
    
    // 处理建议：
    // 1. 打印关键信息后，可以挂起系统或等待看门狗复位。
    // 2. 或者在这里主动调用 esp_restart() 重启设备，防止未知风险。
    while (1);
}

void lvgl_task(void *arg) {
    while (1) {
        lv_timer_handler();
        vTaskDelay(5);
    }
}



void app_main(void) {
	

	esp_err_t ret = nvs_flash_init();//初始化nvs，若不对，进行格式化
	if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		ESP_ERROR_CHECK(nvs_flash_init());
	}

	rgb_init();
    exit_init();
    usart_init(115200);
	printf("hello world!\n");
	// 1. 挂载SD卡
    ESP_LOGI(TAG, "Initializing SD card...");
    sd_init();

	#include "audio_code.h"
	if(bsp_i2c_init()==ESP_OK)
	{
		ESP_LOGI(TAG, "Initializing FT6x36...");
		extern i2c_master_bus_handle_t i2c_bus_handle;
			// 初始化 FT6x36
		ft6x36_config_t cfg = {
			.bus_handle = i2c_bus_handle,
			.i2c_addr = FT6X36_I2C_ADDR,
			.screen_width = 240,
			.screen_height = 320,
			.swap_xy = false,
			.invert_x = false,
			.invert_y = false,
			.rst_gpio = 18,   // 复位引脚 IO18
			.intr_gpio = 17,  // 中断引脚 IO17
		};
		ft6x36_init(&cfg);

		ESP_LOGI(TAG, "Initializing audio_player...");
		//初始化音频播放器
		if(audio_player_init()==ESP_OK)
		{
			if(audio_player_set_volume(40)==ESP_OK)
			{
				//test_pcm_player();
				// 播放 SD 卡中的文件（循环）
				//audio_player_play("/sdcard/demo1.wav", false);

				//record_callback(60);
				//record_pcm_to_queue(60,16000,16,1,audio_player_push_pcm);
				//start_record(30);
			}
		}
	}
	ili9341_init();
	lv_port_init();
	// 必须在 lv_init() 之后调用
	esp_lv_decoder_handle_t decoder_handle = NULL;
    esp_lv_decoder_init(&decoder_handle);
	if (ret != ESP_OK) {
    ESP_LOGE(TAG, "LVGL decoder init failed");
    return;
	}

	//lv_demo_stress();
	//lv_demo_widgets();
	ui_mjpeg_create();

    xTaskCreate(lvgl_task, "lvgl", 8192, NULL, 5, NULL);
	
	ESP_LOGI(TAG,"demo run!");

  	

	

    
	user_wifi_sta_config();
	//user_wifi_smart_config();

	while(user_wait_got_ip()==false)
	{

	}
	
	// // mqtt_start();
	//vTaskDelay(pdMS_TO_TICKS(1000));

	
	
	// 启动 WebSocket 客户端
	xiaozhi_client_config_and_start();


	while(1)
	{
		static bool oldsta = false;
		bool newsta = xiaozhi_client_recorder_running_status();

		if(newsta!=oldsta)
		{
			debug_memory_leak();
			oldsta = newsta;
			if(oldsta)ws2812_set_color(0, 50, 0);
			else ws2812_set_color(0, 0, 0);
		}

		vTaskDelay(pdMS_TO_TICKS(20));
		KEY_STA key_val = get_exit_key_flag();
		if(key_val == BOOT_PRESS)
		{
			
			if(xiaozhi_client_is_connected()==false)
			{
				ws2812_set_color(0, 0, 50);
				ESP_LOGI(TAG, "WebSocket Reconnect!");
				xiaozhi_client_config_and_start();
				vTaskDelay(pdMS_TO_TICKS(2000));
			}
			xiaozhi_client_send_opuspcm_start(5);
			//xiaozhi_client_send_text("播放音乐");
		}
		else if(key_val == BOOT_RELEASE)
		{
			vTaskDelay(pdMS_TO_TICKS(500));
			xiaozhi_client_send_opuspcm_stop();
		}
	}
	
	xiaozhi_client_clear_and_stop();


	// 主任务可以干其他事情，或者直接删除自己
    vTaskDelete(NULL);
}