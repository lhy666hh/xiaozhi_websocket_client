#include <stdio.h>
#include "mjpeg_frame.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#define TAG "mjpeg_frame"

#define JPEG_SOI    0xFFD8//jpeg起始标记
#define JPEG_SOE    0xFFD9//jpeg结束标记

static jpeg_frame_cfg_t s_mjpeg_cfg = {0};

static int mjpeg_inited = 0;//初始化标志
static int mjpeg_stared = 0;//正在工作标志

#define START_GET_EV  BIT0
#define STOP_GET_EV   BIT1
#define EXIT_GET_EV   BIT2

static EventGroupHandle_t mjpeg_event;
static QueueHandle_t mjpeg_queue;

void jpeg_frame_config(jpeg_frame_cfg_t *cfg)
{
	if(cfg->buff_size <4096)
	{
		return;
	}
	memcpy(&s_mjpeg_cfg,cfg,sizeof(jpeg_frame_cfg_t));
	mjpeg_inited = 1;

	if(!mjpeg_event)
	{
		mjpeg_event = xEventGroupCreate();
		if(mjpeg_event)xEventGroupSetBits(mjpeg_event,EXIT_GET_EV);
	}
	if(!mjpeg_queue)mjpeg_queue = xQueueCreate(5,sizeof(jpeg_frame_data_t));
}

static void jpeg_frame_task(void* param)
{
	if(mjpeg_event)xEventGroupClearBits(mjpeg_event,EXIT_GET_EV|STOP_GET_EV|START_GET_EV);
	const char* filename = (const char*)param;
	FILE* f = fopen(filename,"r");
	uint8_t *read_buf = NULL;
	if(!f)
	{
		ESP_LOGI(TAG,"Can't open file:%s",filename);
		goto mjpeg_task_return;
	}

	read_buf = (uint8_t*)malloc(s_mjpeg_cfg.buff_size);
	if(!read_buf)
	{
		ESP_LOGI(TAG,"read_buf malloc failed!");
		goto mjpeg_task_return;
	}

	

	int jpeg_started = 0;//表示当前的检索循环已经找到标头了
	size_t read_bytes = 0;//每次实际读取到的字节数
	uint8_t* frame_buff = NULL; // 截取到的jpg帧数据
	size_t frame_write_index = 0;//当前截取到的jpg数据长度
	size_t frame_buff_total_len = 0;//分配给jpg帧的内存大小

	while((read_bytes = fread(read_buf,1,s_mjpeg_cfg.buff_size,f))>0)
	{
		int soi_index = 0;//soi下标
		for(int i = 0;i<read_bytes-1;i++)
		{
			uint16_t oi_flag = (read_buf[i]<<8)+read_buf[i+1];
			if(!jpeg_started && oi_flag == JPEG_SOI)
			{
				soi_index = i;
				jpeg_started = 1;
			}
			else if(oi_flag == JPEG_SOE && jpeg_started)
			{
				int write_len = (i - soi_index+1)+1;
				if(frame_buff)
				{
					if(frame_write_index+write_len <= frame_buff_total_len)
					{
						memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
					}
					else{
						frame_buff = (uint8_t*)realloc(frame_buff,frame_write_index+write_len);
						frame_buff_total_len = frame_write_index+write_len;
						memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
					}
				}
				else
				{
					if(write_len>frame_buff_total_len)
						frame_buff_total_len = write_len;
					frame_buff = (uint8_t*)malloc(frame_buff_total_len);
					memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
				}
				frame_write_index += write_len;

				bool send_frame_buff_ok = false;
				jpeg_frame_data_t frame_data;
				frame_data.frame = frame_buff;
				frame_data.len = frame_write_index;
				EventBits_t ev = xEventGroupWaitBits(mjpeg_event,START_GET_EV|STOP_GET_EV,pdTRUE,pdFALSE,portMAX_DELAY);
				if(ev&START_GET_EV)
				{
					xQueueSend(mjpeg_queue,&frame_data,portMAX_DELAY);
					send_frame_buff_ok = true;
				}
				if(ev&STOP_GET_EV)
				{
					if(frame_buff&&send_frame_buff_ok==false)
						free(frame_buff);
					ESP_LOGI(TAG,"STOP_GET_EV!");
					goto mjpeg_task_return;
				}

				frame_buff = NULL;

				frame_write_index = 0;
				jpeg_started = 0;

			}
		}
		if(jpeg_started)
		{
			int write_len = s_mjpeg_cfg.buff_size - soi_index;
			if(frame_buff)
			{
				if(frame_write_index+write_len <= frame_buff_total_len)
				{
					memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
				}
				else{
					frame_buff = (uint8_t*)realloc(frame_buff,frame_write_index+write_len);
					frame_buff_total_len = frame_write_index+write_len;
					memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
				}
			}
			else
			{
				if(write_len>frame_buff_total_len)
					frame_buff_total_len = write_len;
				frame_buff = (uint8_t*)malloc(frame_buff_total_len);
				memcpy(&frame_buff[frame_write_index],&read_buf[soi_index],write_len);
			}
			frame_write_index += write_len;
		}
	}

mjpeg_task_return:
	if(read_buf)
	{
		free(read_buf);
		read_buf = NULL;
	}
	if(f)fclose(f);
	mjpeg_stared = 0;
	if(mjpeg_event)xEventGroupSetBits(mjpeg_event,EXIT_GET_EV);
	vTaskDelete(NULL);
}

void jpeg_frame_start(const char* filename)
{
	if(jpeg_frame_running_status()==true)return;
	if(!mjpeg_inited)return;
	if(mjpeg_stared)return;

	static char jpeg_filename[256];
	snprintf(jpeg_filename,256,"/sdcard/%s",filename);
	mjpeg_stared = 1;
	xTaskCreatePinnedToCore(jpeg_frame_task,"mjpeg_frame",4096,jpeg_filename,6,NULL,1);
}

void jpeg_frame_stop(void)
{
	if(mjpeg_stared)xEventGroupSetBits(mjpeg_event,STOP_GET_EV);
}

bool jpeg_frame_running_status(void)
{
	if(mjpeg_event)
	{
		EventBits_t ev = xEventGroupWaitBits(mjpeg_event,EXIT_GET_EV,pdFALSE,pdFALSE,0);
		if(ev&EXIT_GET_EV)return false;
		else return true;
	}
	return false;
}

void jpeg_frame_get_one(jpeg_frame_data_t *data)
{
	jpeg_frame_data_t frame_data = {0};
	if(!mjpeg_stared)return;
	xEventGroupSetBits(mjpeg_event,START_GET_EV);

	if(pdTRUE == xQueueReceive(mjpeg_queue,&frame_data,pdMS_TO_TICKS(1000)))
	{
		memcpy(data,&frame_data,sizeof(jpeg_frame_data_t));
	}
	else
	{
		data->len = 0;
	}
}