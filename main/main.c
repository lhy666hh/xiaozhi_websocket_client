#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "rgb.h"
#include "exit.h"
#include "uart.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "mqtt_client.h"

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


#include "xiaozhi_client.h"
#include "audio_recorder.h"

#include "esp_heap_caps.h"//动态堆检测库
static const char *TAG = "AUDIO_TEST";

#define TEST_TAG "DRAW_AREA_TEST"
#define SSID     "CMCC-xnXf"
#define PWD      "6TVFUame"

static  SemaphoreHandle_t wifi_connect_sem = NULL;

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

static void test_ili9341_draw_area(void)
{
    // 1. 测试绘制一个纯色矩形 (绿色, 100x100)
    uint16_t x1 = 70, y1 = 110;
    uint16_t x2 = 169, y2 = 209;  // 宽度 = 100, 高度 = 100
    uint32_t w = x2 - x1 + 1;
    uint32_t h = y2 - y1 + 1;
    uint32_t buf_size = w * h * sizeof(uint16_t);

    uint16_t *buffer = (uint16_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TEST_TAG, "Failed to allocate buffer for rectangle");
        return;
    }

    // 填充绿色 (RGB565)
    for (uint32_t i = 0; i < w * h; i++) {
        buffer[i] = 0x07E0;   // ILI9341_GREEN
    }

    ESP_LOGI(TEST_TAG, "Drawing green rectangle at (%d,%d)-(%d,%d)", x1, y1, x2, y2);
    ili9341_draw_area(x1, y1, x2, y2, buffer);
    free(buffer);

    vTaskDelay(pdMS_TO_TICKS(1000));  // 延时1秒观察

    // 2. 测试绘制一个渐变条 (从左到右红->蓝, 全宽, 高度20)
    x1 = 0; y1 = 250;
    x2 = ILI9341_WIDTH - 1; y2 = 269;
    w = ILI9341_WIDTH;
    h = 20;
    buf_size = w * h * sizeof(uint16_t);

    buffer = (uint16_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TEST_TAG, "Failed to allocate buffer for gradient bar");
        return;
    }

    // 生成渐变: 从红色 (0xF800) 渐变到蓝色 (0x001F)
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint8_t r = (uint8_t)((float)col / w * 31);      // 红色通道从0->31
            uint8_t b = (uint8_t)((float)(w - col) / w * 31); // 蓝色通道从31->0
            uint16_t color = (r << 11) | (0 << 5) | b;        // 绿色通道为0
            buffer[row * w + col] = color;
        }
    }

    ESP_LOGI(TEST_TAG, "Drawing gradient bar at (%d,%d)-(%d,%d)", x1, y1, x2, y2);
    ili9341_draw_area(x1, y1, x2, y2, buffer);
    free(buffer);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. 测试全屏填充白色 (检验分块传输)
    ESP_LOGI(TEST_TAG, "Filling whole screen with white");
    ili9341_clear(ILI9341_WHITE);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 4. 恢复黑屏
    ili9341_clear(ILI9341_BLACK);
    ESP_LOGI(TEST_TAG, "Test finished");
}

void lvgl_task(void *arg) {
    while (1) {
        lv_timer_handler();
        vTaskDelay(5);
    }
}



void wifi_event_handle(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
	if(event_base == WIFI_EVENT)
	{
		switch(event_id)
		{
			case WIFI_EVENT_STA_START:
				esp_wifi_connect();
				break;
			case WIFI_EVENT_STA_CONNECTED:
				ESP_LOGI(TAG,"esp32 connected to ap");
				break;
			case WIFI_EVENT_STA_DISCONNECTED:
				esp_wifi_connect();//推荐重连一定次数 延时一段时间再重连
				ESP_LOGI(TAG,"esp32 connect the ap fail! retry...");
				break;
			default:break;
		}
	}
	else if(event_base == IP_EVENT)
	{
		switch(event_id)
		{
			case IP_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG,"esp32 got ip");
			xSemaphoreGive(wifi_connect_sem);
			break;
		}
	}
	else if(event_base == SC_EVENT)
	{
		switch(event_id)
		{
			case SC_EVENT_SCAN_DONE:
				ESP_LOGI(TAG,"sc scan done");
				break;
			case SC_EVENT_GOT_SSID_PSWD:
				ESP_LOGI(TAG,"sc got ssid pwd");
				smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;//解析smart config获取的AP名和密码
				wifi_config_t wifi_config = {0};
				memset(&wifi_config,0,sizeof(wifi_config));
				snprintf((char *)wifi_config.sta.ssid,sizeof(wifi_config.sta.ssid),"%s",(char*)evt->ssid);
				snprintf((char *)wifi_config.sta.password,sizeof(wifi_config.sta.password),"%s",(char*)evt->password);
				wifi_config.sta.bssid_set = evt->bssid_set;
				if(wifi_config.sta.bssid_set)//设置MAC
				{
					memcpy(wifi_config.sta.bssid,evt->bssid,6);
				}
				esp_wifi_disconnect();
				esp_wifi_set_config(WIFI_IF_STA,&wifi_config);
				esp_wifi_connect();
				break;
			case SC_EVENT_SEND_ACK_DONE:
				ESP_LOGI(TAG,"sc send ack down");
				//告诉手机app，设备已经收到了SSID 和pwd
				esp_smartconfig_stop();
				break;
			default:break;
		}
	}
}	

void user_wifi_sta_config(void)
{
	//wifi配置
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

	esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handle,NULL);
	esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handle,NULL);

	wifi_config_t wifi_config = {
		.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK,//加密模式
		.sta.pmf_cfg.capable = true,//启用保护管理帧
		.sta.pmf_cfg.required = false,//是否与有保护管理帧设备通信
	};
	memset(wifi_config.sta.ssid,0,sizeof(wifi_config.sta.ssid));
	memcpy(wifi_config.sta.ssid,SSID,strlen(SSID));

	memset(wifi_config.sta.password,0,sizeof(wifi_config.sta.password));
	memcpy(wifi_config.sta.password,PWD,strlen(PWD));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
}

void user_wifi_smart_config(void)
{
	//wifi配置
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();
	wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

	esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event_handle,NULL);
	esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event_handle,NULL);
	esp_event_handler_register(SC_EVENT,ESP_EVENT_ANY_ID,wifi_event_handle,NULL);//smart 事件



	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
	smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
	esp_smartconfig_start(&cfg);
}
#define MQTT_ADDRESS "mqtt://broker-cn.emqx.io"
#define MQTT_CLIENTID   "mqttx_esp3220260603lhy"
#define MQTT_USERNAME   "lhy"
#define MQTT_PASSWORD   "lhy666hh"

#define MQTT_TOPIC1     "/topic/esp32_1234"//ESP32往这个主题推送信息
#define MQTT_TOPIC2     "/topic/mqttx_1234"//mqttx往这个主题推送信息

static esp_mqtt_client_handle_t mqtt_handle = NULL;

void mqtt_event_callback(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data)
{
	esp_mqtt_event_handle_t data = (esp_mqtt_event_handle_t )event_data;
	switch(event_id)
	{
		case MQTT_EVENT_CONNECTED:
			ESP_LOGI(TAG,"mqtt connected");
			esp_mqtt_client_subscribe_single(mqtt_handle,MQTT_TOPIC2,1);//订阅主题
			break;
		case MQTT_EVENT_DISCONNECTED:
			ESP_LOGI(TAG,"mqtt disconnected!");
			break;
		case MQTT_EVENT_PUBLISHED:
			ESP_LOGI(TAG,"mqtt published ack");
			break;
		case MQTT_EVENT_SUBSCRIBED:
			ESP_LOGI(TAG,"mqtt subscribed ack");
			break;
		case MQTT_EVENT_DATA:
			ESP_LOGI(TAG,"topic->%s",data->topic);
			ESP_LOGI(TAG,"topic->%s",data->data);
			break;

		default:break;
	}
}





void mqtt_start(void)
{
	esp_mqtt_client_config_t mqtt_cfg = {0};
	mqtt_cfg.broker.address.uri = MQTT_ADDRESS;
	mqtt_cfg.broker.address.port = 1883;
	mqtt_cfg.credentials.client_id = MQTT_CLIENTID;
	mqtt_cfg.credentials.username = MQTT_USERNAME;
	mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
	mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);

	//注册事件回调
	esp_mqtt_client_register_event(mqtt_handle,ESP_EVENT_ANY_ID,mqtt_event_callback,NULL);

	esp_mqtt_client_start(mqtt_handle);
}
#include "esp_websocket_client.h"
#include "xiaozhi_client.h"
#include "cJSON.h"






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
			if(audio_player_set_volume(30)==ESP_OK)
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
	//test_ili9341_draw_area();
    // ili9341_clear(ILI9341_BLACK);
    // //ili9341_draw_string(10, 10, "Hello ESP32-S3!", ILI9341_WHITE, ILI9341_BLACK);
    // ili9341_draw_line(10, 30, 200, 30, ILI9341_RED);
    // ili9341_fill_rect(50, 50, 100, 80, ILI9341_BLUE);
    // ili9341_draw_circle(120, 150, 40, ILI9341_YELLOW);





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

  	wifi_connect_sem = xSemaphoreCreateBinary();

	

    
	user_wifi_sta_config();
	//user_wifi_smart_config();

	xSemaphoreTake(wifi_connect_sem,portMAX_DELAY);
	// // mqtt_start();
	// // 启动 WebSocket 客户端
	vTaskDelay(pdMS_TO_TICKS(1000));

	
	
	
	xiaozhi_client_config_and_start();


	while(1)
	{
		vTaskDelay(pdMS_TO_TICKS(20));
		KEY_STA key_val = get_exit_key_flag();
		if(key_val == BOOT_PRESS)
		{
			debug_memory_leak();
			if(xiaozhi_client_is_connected()==false)
			{
				ws2812_set_color(0, 0, 50);
				ESP_LOGI(TAG, "WebSocket Reconnect!");
				xiaozhi_client_config_and_start();
				vTaskDelay(pdMS_TO_TICKS(2000));
			}
			else	ws2812_set_color(0, 50, 0);
			xiaozhi_client_send_opuspcm_start();
			debug_memory_leak();
			//xiaozhi_client_send_text("查歌单");
		}
		else if(key_val == BOOT_RELEASE)
		{
			vTaskDelay(pdMS_TO_TICKS(500));
			ws2812_set_color(0, 0, 0);
			xiaozhi_client_send_opuspcm_stop();
		}
	}
	
	xiaozhi_client_clear_and_stop();


	// 主任务可以干其他事情，或者直接删除自己
    vTaskDelete(NULL);
	// int count = 0;
	// while(1)
	// {
	// 	char publish_str[32];
	// 	snprintf(publish_str,sizeof(publish_str),"{\"count\":%d}",count);
	// 	//mqtt发布报文
	// 	esp_mqtt_client_publish(mqtt_handle,MQTT_TOPIC1,publish_str,strlen(publish_str),1,0);
	// 	count++;
	// 	vTaskDelay(pdMS_TO_TICKS(5000));
	// }


	

		

    //     // if (uart_get_buffered_data_len(USART_UX, &len) == ESP_OK && len > 0) {
    //     //     if (len >= sizeof(data)) len = sizeof(data) - 1;
    //     //     int rx_len = uart_read_bytes(USART_UX, (uint8_t*)data, len, pdMS_TO_TICKS(100));
    //     //     if (rx_len > 0) {
    //     //         printf("\n你发送的消息为：%.*s\n", rx_len, data);
    //     //         uart_write_bytes(USART_UX, data, rx_len);
    //     //     }
    //     //     memset(data, 0, sizeof(data));
    //     // } else {
    //     //     // time++;
    //     //     // if (time % 500 == 0) {
    //     //     //     printf("\n请输入数据\n");
    //     //     // }
            
    //     // }
		
    // }
}