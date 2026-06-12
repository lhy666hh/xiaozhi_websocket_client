// audio_recorder.c
#include "esp_err.h"
#include "audio_recorder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "audio_code.h"          // 提供 codec_dev 及重配接口
#include "audio_player.h"
#include <string.h>
#include <stdlib.h>
#define START_GET_EV  BIT0
#define STOP_GET_EV   BIT1
#define EXIT_GET_EV   BIT2

// 录音参数 (与 pcm_player 期望一致，通常为 16kHz 单声道 16bit)
#define RECORD_SAMPLE_RATE  16000
#define RECORD_CHANNELS     1
#define RECORD_BITS         16
#define RECORD_FRAME_MS     60                     // 帧时长 (ms)
#define RECORD_FRAME_SAMPLES (RECORD_SAMPLE_RATE * RECORD_FRAME_MS / 1000)  // 960 samples
#define RECORD_FRAME_BYTES   (RECORD_FRAME_SAMPLES * sizeof(int16_t))       // 1920 bytes
static const char *TAG = "AUDIO_RECORDER";
static EventGroupHandle_t recorder_event;

void record_callback(uint32_t rec_time)
{
	ESP_LOGI(TAG, "Start record %" PRIu32 "s", rec_time);
    int flash_wr_size = 0;
    // 每次采样的数据长度（单位：字节）
    const size_t read_size_byte = 4096;
    // 根据录音时间计算出需要录的长度（计算公式：采样率*通道数*位深/8*录音时间 = 录音数据长度）
    // 16000 * 2 * 2 * rec_time
    const int flash_rec_time = RECORD_SAMPLE_RATE * RECORD_CHANNELS * sizeof(int16_t) * rec_time;

    void *record_data = malloc(read_size_byte);
    if (record_data == NULL) {
        ESP_LOGE(TAG, "Failed to malloc record_data");
        return;
    }
	audio_player_start_pcm_stream(RECORD_SAMPLE_RATE, RECORD_BITS, RECORD_CHANNELS);
    esp_codec_dev_set_in_gain(codec_dev, 42.0f);
    while (flash_wr_size < flash_rec_time)
    {
        if (esp_codec_dev_read(codec_dev, record_data, read_size_byte) == ESP_OK) {

			if (audio_player_push_pcm((int16_t*)record_data, read_size_byte/2) != ESP_OK) {
                ESP_LOGW(TAG, "PCM queue full, dropping frame");
				vTaskDelay(pdMS_TO_TICKS(10));
            }
            flash_wr_size += read_size_byte;
        } else {
            ESP_LOGE(TAG, "Codec read failed");
            break;
        }
    }
    
    free(record_data);
    ESP_LOGI(TAG, "Record end, total %d bytes", flash_wr_size);
}


static QueueHandle_t s_pcm_queue = NULL;      // 消息队列
static TaskHandle_t s_pcm_task = NULL;        // 处理任务


typedef struct 
{
	int16_t *pcm_dat;
	size_t    samples;
	rec_procees_pcm_cb cb;
}record_pcm_t;


static void pcm_data_handler_task(void *arg)
{
    record_pcm_t pcm_block;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(60);   // 60ms 周期

    while (1) {
        // 等待下一个周期（如果队列中没有新数据，也会等待到时间点）
		if(recorder_event)
		{
			//若事件组存在，且录音任务没有退出
			if((xEventGroupWaitBits(recorder_event,EXIT_GET_EV,pdFALSE,pdFALSE,0)&EXIT_GET_EV)==0)xEventGroupSetBits(recorder_event,START_GET_EV);
		}
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
        // 尝试从队列接收数据（非阻塞，因为时间到了必须发送一帧，即使丢弃）
        if (xQueueReceive(s_pcm_queue, &pcm_block, 0) == pdTRUE) {
            // 处理数据（编码并发送）
			if(pcm_block.pcm_dat)
			{
				pcm_block.cb(pcm_block.pcm_dat, pcm_block.samples);
				free(pcm_block.pcm_dat);
				pcm_block.pcm_dat = NULL;
			}
        }
    }
    vTaskDelete(s_pcm_task);
}

// static void pcm_data_handler_task(void *arg)
// {
//     record_pcm_t pcm_block;
//     while (1) {
//         // 阻塞等待队列数据
//         if (xQueueReceive(s_pcm_queue, &pcm_block, portMAX_DELAY) == pdTRUE) {
//             //ESP_LOGI("PCM_HANDLER", "Received PCM block, samples: %d", pcm_block.samples);
//             // TODO: 在这里处理 PCM 数据，例如写入文件或 Opus 编码发送
// 			pcm_block.cb(pcm_block.pcm_dat,pcm_block.samples);
// 			vTaskDelay(pdMS_TO_TICKS(60));
//             // 处理完成后释放动态分配的内存
//             free(pcm_block.pcm_dat);
//         }
//     }
//     vTaskDelete(NULL);
// }
static void record_pcm_stop(void)
{
    // if (s_pcm_queue) {
    //     // 清空队列并释放残留数据
    //     record_pcm_t msg;
    //     while (xQueueReceive(s_pcm_queue, &msg, 0) == pdTRUE) {
    //         free(msg.pcm_dat);
    //     }
    //     vQueueDelete(s_pcm_queue);
    //     s_pcm_queue = NULL;
    // }
	// if (recorder_event != NULL) {
	// 	vEventGroupDelete(recorder_event);
	// 	recorder_event = NULL;   // 删除后指针置空，避免野指针
	// }
    // if (s_pcm_task) {
    //     vTaskDelete(s_pcm_task);
    //     s_pcm_task = NULL;
    // }
    ESP_LOGI(TAG, "Record resources cleaned up");
}


void recorder_frame_stop(void)
{
	if(recorder_event)xEventGroupSetBits(recorder_event,STOP_GET_EV);
}

bool recorder_running_status(void)
{
	if(recorder_event)
	{
		EventBits_t ev = xEventGroupWaitBits(recorder_event,EXIT_GET_EV,pdFALSE,pdFALSE,0);
		if(ev&EXIT_GET_EV)return false;
		else return true;
	}
	return false;
}


void record_pcm_to_queue(uint32_t rec_time, uint32_t rate, uint16_t bits, uint16_t channels,rec_procees_pcm_cb cb)
{
    ESP_LOGI(TAG, "Start record %" PRIu32 "s", rec_time);
    
    // 如果队列未创建，则创建
    if (s_pcm_queue == NULL) {
        s_pcm_queue = xQueueCreate(8, sizeof(record_pcm_t));
        if (s_pcm_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create PCM queue");
            return;
        }
    }
    if(!recorder_event)recorder_event = xEventGroupCreate();
	else    xEventGroupClearBits(recorder_event,EXIT_GET_EV|STOP_GET_EV|START_GET_EV);
    // 如果处理任务未创建，则创建（一次性创建后永久运行）
    if (s_pcm_task == NULL) {
        xTaskCreate(pcm_data_handler_task, "pcm_handler", 8192, NULL, 5, &s_pcm_task);
    }
    
    // 每次采样的数据长度（字节）= 采样率 * 帧时长(0.06s) * 字节/样本(2)
    const size_t read_size_byte = rate * 0.06 * (bits / 8);   // 例如 16000*0.06*2 = 1920 bytes
    const size_t frame_samples = read_size_byte / (bits / 8); // 960 samples for 16bit mono
    
    // 总录音数据长度（字节）
    const int total_bytes = rate * channels * (bits / 8) * rec_time;
    
    void *record_data = malloc(read_size_byte);
    if (record_data == NULL) {
        ESP_LOGE(TAG, "Failed to malloc record_data");
        return;
    }
    
    audio_player_start_pcm_stream(rate, bits, channels);
    esp_codec_dev_set_in_gain(codec_dev, 42.0f);
    
    int written_bytes = 0;
    while (written_bytes < total_bytes) {
        if (esp_codec_dev_read(codec_dev, record_data, read_size_byte) == ESP_OK) {
            // 分配消息内存并拷贝数据
            record_pcm_t msg;
            msg.pcm_dat = malloc(read_size_byte);
            if (msg.pcm_dat) {
                memcpy(msg.pcm_dat, record_data, read_size_byte);
                msg.samples = frame_samples;
				msg.cb = cb;

				EventBits_t ev = xEventGroupWaitBits(recorder_event,START_GET_EV|STOP_GET_EV,pdTRUE,pdFALSE,portMAX_DELAY);
				if(ev&START_GET_EV)
				{
					// 发送到队列（若队列满则等待）
					if (xQueueSend(s_pcm_queue, &msg, portMAX_DELAY) != pdTRUE) {
						ESP_LOGW(TAG, "Queue send failed, dropping frame");
						free(msg.pcm_dat);
					}
					msg.pcm_dat = NULL;//发送成功由接收任务销毁，发送失败该逻辑下销毁，必须置空,否则退出逻辑重复销毁。
				}
				if(ev&STOP_GET_EV)
				{
					if (msg.pcm_dat)
					{
						free(msg.pcm_dat);
						msg.pcm_dat = NULL;
					}
					ESP_LOGI(TAG,"STOP_GET_EV!");
					break;
				}

                // // 发送到队列（若队列满则等待）
                // if (xQueueSend(s_pcm_queue, &msg, portMAX_DELAY) != pdTRUE) {
                //     ESP_LOGW(TAG, "Queue send failed, dropping frame");
                //     free(msg.pcm_dat);
                // }
            }
            written_bytes += read_size_byte;
        } else {
            ESP_LOGE(TAG, "Codec read failed");
            break;
        }
    }
    
    free(record_data);
    ESP_LOGI(TAG, "Record end, total %d bytes-----EXIT", written_bytes);
    
    // 注意：录音结束后，队列和任务并未销毁。如需停止，可调用专门的停止函数。
	vTaskDelay(pdMS_TO_TICKS(100));
	xEventGroupSetBits(recorder_event,EXIT_GET_EV);
}


void record_to_pcmfile(uint32_t rec_time)
{
    ESP_LOGI(TAG, "Start record %" PRIu32 "s", rec_time);
    int flash_wr_size = 0;
    // 每次采样的数据长度（单位：字节）
    const size_t read_size_byte = 4096;
    // 根据录音时间计算出需要录的长度（计算公式：采样率*通道数*位深/8*录音时间 = 录音数据长度）
    // 16000 * 2 * 2 * rec_time
    const int flash_rec_time = RECORD_SAMPLE_RATE * RECORD_CHANNELS * sizeof(int16_t) * rec_time;
	reconfigure_audio_params(RECORD_SAMPLE_RATE,RECORD_BITS,RECORD_CHANNELS);
    FILE *fp = fopen("/sdcard/audio.pcm", "w");
    if(fp==NULL){
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }

    void *record_data = malloc(read_size_byte);
    if (record_data == NULL) {
        ESP_LOGE(TAG, "Failed to malloc record_data");
        fclose(fp);
        return;
    }

    esp_codec_dev_set_in_gain(codec_dev, 42.0f);
    while (flash_wr_size < flash_rec_time)
    {
        if (esp_codec_dev_read(codec_dev, record_data, read_size_byte) == ESP_OK) {
            fwrite(record_data, 1, read_size_byte, fp);
            flash_wr_size += read_size_byte;
        } else {
            ESP_LOGE(TAG, "Codec read failed");
            break;
        }
    }
    
    free(record_data);
    fclose(fp);
    ESP_LOGI(TAG, "Record end, total %d bytes", flash_wr_size);
}