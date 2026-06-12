#include "my_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "esp_log.h"
#include "mqtt_client.h"
#define MQTT_ADDRESS "mqtt://broker-cn.emqx.io"
#define MQTT_CLIENTID   "mqttx_esp3220260603lhy"
#define MQTT_USERNAME   "lhy"
#define MQTT_PASSWORD   "lhy666hh"

#define MQTT_TOPIC1     "/topic/esp32_1234"//ESP32往这个主题推送信息
#define MQTT_TOPIC2     "/topic/mqttx_1234"//mqttx往这个主题推送信息

#define SSID     "CMCC-xnXf"
#define PWD      "6TVFUame"

static esp_mqtt_client_handle_t mqtt_handle = NULL;
static const char *TAG = "MY_WIFI";
static  SemaphoreHandle_t wifi_connect_sem = NULL;
static void wifi_event_handle(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
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
	if(!wifi_connect_sem)wifi_connect_sem = xSemaphoreCreateBinary();
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
	if(!wifi_connect_sem)wifi_connect_sem = xSemaphoreCreateBinary();
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

bool user_wait_got_ip(void)
{
	if(xSemaphoreTake(wifi_connect_sem,portMAX_DELAY)==pdTRUE)return true;
	else return false;
}


static void mqtt_event_callback(void* event_handler_arg,
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