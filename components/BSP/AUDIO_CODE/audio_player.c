/**
 * @file audio_player.c
 * @brief WAV 音频播放器实现（支持暂停/继续、进度控制）
 */

#include "audio_player.h"
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "audio_code.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include <string.h>

static const char *TAG = "AUDIO_PLAYER";

//#define USE_I2S_HANDLE
//--------------------------- 外部句柄 ------------------------------------
#ifdef USE_I2S_HANDLE
extern i2s_chan_handle_t tx_handle;   // 定义在 audio_code.c 中
#endif
//--------------------------- 播放状态 ------------------------------------
typedef enum {
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_PAUSED
} player_state_t;

// 播放源类型
typedef enum {
    SOURCE_NONE,
    SOURCE_FILE,
    SOURCE_PCM_QUEUE
} source_type_t;

static source_type_t s_source = SOURCE_NONE;
static QueueHandle_t s_pcm_queue = NULL;          // PCM 数据队列
static const size_t PCM_QUEUE_LEN = 8;            // 队列长度

// PCM 数据块
typedef struct {
    uint8_t *data;   // 动态分配的数据
    size_t len;      // 字节数
} pcm_block_t;

static player_state_t s_state = STATE_STOPPED;
static TaskHandle_t s_play_task = NULL;
static SemaphoreHandle_t s_task_stop_sem = NULL;
static SemaphoreHandle_t s_file_mutex = NULL;   // 保护文件操作和 seek 请求

static bool s_loop_play = false;
static FILE *s_audio_file = NULL;

// 音频参数（用于进度计算）
static uint32_t s_sample_rate = 0;
static uint16_t s_channels = 0;
static uint16_t s_bits_per_sample = 0;
static uint32_t s_bytes_per_sec = 0;
static uint32_t s_total_data_bytes = 0;   // data chunk 总字节数
static uint32_t s_data_start_offset = 0;  // 文件中 data 数据起始偏移
static uint32_t s_played_bytes = 0;       // 已写入 I2S 的字节数（累计）

// Seek 请求
static bool s_seek_pending = false;
static uint32_t s_seek_byte = 0;          // 目标字节偏移（相对于 data 起始）

//--------------------------- WAV 文件结构解析 -----------------------------
#pragma pack(push, 1)
typedef struct {
    char     chunkID[4];      // "RIFF"
    uint32_t chunkSize;
    char     format[4];       // "WAVE"
    char     subchunk1ID[4];  // "fmt "
    uint32_t subchunk1Size;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char     subchunk2ID[4];  // "data"
    uint32_t subchunk2Size;
} wav_header_t;
#pragma pack(pop)





/**
 * @brief 解析 WAV 文件头，获取音频参数
 */
static bool parse_wav_header(FILE *fp, wav_header_t *header)
{
    if (fread(header, sizeof(wav_header_t), 1, fp) != 1) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        return false;
    }

    if (memcmp(header->chunkID, "RIFF", 4) != 0 ||
        memcmp(header->format, "WAVE", 4) != 0 ||
        memcmp(header->subchunk1ID, "fmt ", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV file format");
        return false;
    }

    if (header->audioFormat != 1) {
        ESP_LOGE(TAG, "Only PCM format is supported, audioFormat=%d", header->audioFormat);
        return false;
    }

    if (header->bitsPerSample != 16 && header->bitsPerSample != 32) {
        ESP_LOGE(TAG, "Only 16/32-bit PCM supported, bitsPerSample=%d", header->bitsPerSample);
        return false;
    }

    // 查找 data chunk（跳过可能的附加 chunk）
    while (memcmp(header->subchunk2ID, "data", 4) != 0) {
        if (fseek(fp, header->subchunk2Size, SEEK_CUR) != 0) {
            ESP_LOGE(TAG, "Failed to seek to next chunk");
            return false;
        }
        if (fread(header->subchunk2ID, 4, 1, fp) != 1 ||
            fread(&header->subchunk2Size, 4, 1, fp) != 1) {
            ESP_LOGE(TAG, "Failed to read next chunk header");
            return false;
        }
    }

    ESP_LOGI(TAG, "WAV: %dch %dHz %dbit, data size=%lu",
             header->numChannels, header->sampleRate,
             header->bitsPerSample, header->subchunk2Size);
    return true;
}

/**
 * @brief 重新配置 I2S 和 ES8311（根据音频参数）
 */
esp_err_t reconfigure_audio_params(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
{
    return audio_code_reconfigi2s(sample_rate, bits_per_sample, channels);
}

/**
 * @brief 播放任务主函数
 */
static void play_task(void *arg)
{
    uint8_t *buffer = NULL;
    bool stop_requested = false;
    const size_t buf_size = 4096;

    // 分配 DMA 缓冲区（两种模式共用）
    buffer = heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
        goto task_exit;
    }

    // 根据源类型执行不同逻辑
    if (s_source == SOURCE_FILE) {
        // ---------- WAV 文件播放模式 ----------
        wav_header_t header;
        size_t bytes_read, bytes_written;
        uint32_t data_remains = 0;

        if (!parse_wav_header(s_audio_file, &header)) {
            ESP_LOGE(TAG, "Invalid WAV file");
            goto task_exit;
        }

        // 保存音频参数用于进度计算
        s_sample_rate = header.sampleRate;
        s_channels = header.numChannels;
        s_bits_per_sample = header.bitsPerSample;
        s_bytes_per_sec = s_sample_rate * s_channels * (s_bits_per_sample / 8);
        s_total_data_bytes = header.subchunk2Size;
        s_data_start_offset = ftell(s_audio_file);
        s_played_bytes = 0;

        if (reconfigure_audio_params(s_sample_rate, s_bits_per_sample, s_channels) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconfigure audio");
            goto task_exit;
        }
		#ifdef USE_I2C_HANDLE
		#else
		if (codec_dev == NULL) {
			ESP_LOGE(TAG, "codec_dev is NULL, cannot write");
			goto task_exit;
		}
		vTaskDelay(pdMS_TO_TICKS(30));  // 确保完全就绪
		#endif

        data_remains = s_total_data_bytes;
        s_state = STATE_PLAYING;
        ESP_LOGI(TAG, "Start playing file... duration = %lu ms", audio_player_get_duration_ms());

        while (!stop_requested && s_state != STATE_STOPPED) {
            // 处理 Seek 请求
            if (s_seek_pending) {
                xSemaphoreTake(s_file_mutex, portMAX_DELAY);
                if (s_seek_byte <= s_total_data_bytes) {
					#ifdef USE_I2S_HANDLE
                    if (tx_handle) i2s_channel_disable(tx_handle);
                    fseek(s_audio_file, s_data_start_offset + s_seek_byte, SEEK_SET);
                    data_remains = s_total_data_bytes - s_seek_byte;
                    s_played_bytes = s_seek_byte;
                    if (tx_handle) i2s_channel_enable(tx_handle);
					#else
					// 直接移动文件指针，不再手动控制 I2S 通道
					fseek(s_audio_file, s_data_start_offset + s_seek_byte, SEEK_SET);
					data_remains = s_total_data_bytes - s_seek_byte;
					s_played_bytes = s_seek_byte;
					#endif
                    ESP_LOGI(TAG, "Seek to byte %lu", s_seek_byte);
                }
                s_seek_pending = false;
                xSemaphoreGive(s_file_mutex);
            }

            // 暂停处理
            if (s_state == STATE_PAUSED) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            if (data_remains == 0) {
                if (s_loop_play) {
                    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
                    fseek(s_audio_file, s_data_start_offset, SEEK_SET);
                    data_remains = s_total_data_bytes;
                    s_played_bytes = 0;
                    xSemaphoreGive(s_file_mutex);
                    ESP_LOGI(TAG, "Loop restart");
                } else {
                    break;
                }
            }

            size_t bytes_to_write = (data_remains < buf_size) ? data_remains : buf_size;
            if (bytes_to_write == 0) break;

            xSemaphoreTake(s_file_mutex, portMAX_DELAY);
            bytes_read = fread(buffer, 1, bytes_to_write, s_audio_file);
            xSemaphoreGive(s_file_mutex);

            if (bytes_read == 0) {
                ESP_LOGW(TAG, "File read EOF");
                break;
            }
			#ifdef USE_I2S_HANDLE
			esp_err_t ret = i2s_channel_write(tx_handle, buffer, bytes_read, &bytes_written, portMAX_DELAY);
            if (ret != ESP_OK || bytes_written != bytes_read) {
                ESP_LOGE(TAG, "I2S write failed");
                break;
            }
			#else

			int ret = esp_codec_dev_write(codec_dev, buffer, bytes_read);
			if (ret != ESP_CODEC_DEV_OK) {
				ESP_LOGE(TAG, "codec write failed, ret=%d", ret);
				break;
			}
			// 注意：返回的 ret 就是实际写入的字节数，可用于更新 s_played_bytes
			size_t bytes_written = (size_t)bytes_read;  // 可选，用于后续进度更新
			#endif
            

            data_remains -= bytes_read;
            s_played_bytes += bytes_written;

            if (xSemaphoreTake(s_task_stop_sem, 0) == pdTRUE) stop_requested = true;
        }

        ESP_LOGI(TAG, "File playback finished");
    }
    else if (s_source == SOURCE_PCM_QUEUE) {
        // ---------- PCM 队列播放模式 ----------
        // 注意：音频参数已在 audio_player_start_pcm_stream 中配置完成
        s_state = STATE_PLAYING;
        ESP_LOGI(TAG, "Start PCM stream playback");

        while (!stop_requested && s_state != STATE_STOPPED) {
            // 暂停处理
            if (s_state == STATE_PAUSED) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            pcm_block_t block;
            // 等待队列数据，超时时间稍长以便检查停止信号
            if (xQueueReceive(s_pcm_queue, &block, pdMS_TO_TICKS(100)) == pdTRUE) {
				#ifdef USE_I2S_HANDLE
				size_t bytes_written;
                esp_err_t ret = i2s_channel_write(tx_handle, block.data, block.len, &bytes_written, portMAX_DELAY);
                free(block.data);   // 释放动态分配的内存
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "I2S write failed in PCM mode");
                    break;
                }
				#else

				int ret = esp_codec_dev_write(codec_dev, block.data, block.len);
				free(block.data);
				if (ret != ESP_CODEC_DEV_OK) {
					ESP_LOGE(TAG, "codec write failed in PCM mode, ret=%d", ret);
					break;
				}
				#endif
                
            }

            // 检查停止信号
            if (xSemaphoreTake(s_task_stop_sem, 0) == pdTRUE) {
                stop_requested = true;
                break;
            }
        }

        // 清空队列中剩余数据
        if (s_pcm_queue) {
            pcm_block_t block;
            while (xQueueReceive(s_pcm_queue, &block, 0) == pdTRUE) {
                free(block.data);
            }
        }
        ESP_LOGI(TAG, "PCM stream playback finished");
    }
    else {
        ESP_LOGE(TAG, "Unknown source type");
        goto task_exit;
    }

task_exit:
    if (buffer) heap_caps_free(buffer);

    // 关闭文件（如果是文件模式）
    if (s_source == SOURCE_FILE && s_audio_file) {
        fclose(s_audio_file);
        s_audio_file = NULL;
    }

    s_state = STATE_STOPPED;
    s_source = SOURCE_NONE;
    s_play_task = NULL;
    vTaskDelete(NULL);
}

//--------------------------- 对外 API 实现 ----------------------------------
esp_err_t audio_player_init(void)
{
    ESP_RETURN_ON_ERROR(audio_code_init(), TAG, "audio_code init failed");

    if (s_task_stop_sem == NULL) {
        s_task_stop_sem = xSemaphoreCreateBinary();
    }
    if (s_file_mutex == NULL) {
        s_file_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Audio player initialized");
    return ESP_OK;
}

esp_err_t audio_player_play(const char *file_path, bool loop)
{
    if (!file_path) return ESP_ERR_INVALID_ARG;

    audio_player_stop();

    s_audio_file = fopen(file_path, "rb");
    if (!s_audio_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", file_path);
        return ESP_FAIL;
    }

    s_loop_play = loop;
    s_state = STATE_STOPPED;   // 任务启动后会变为 PLAYING
    s_seek_pending = false;
    s_played_bytes = 0;
	s_source = SOURCE_FILE;

    xSemaphoreTake(s_task_stop_sem, 0);   // 清空停止信号
    BaseType_t ret = xTaskCreate(play_task, "wav_player", 8192, NULL, 5, &s_play_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create play task");
        fclose(s_audio_file);
        s_audio_file = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_player_stop(void)
{
    if (s_play_task != NULL) {
        xSemaphoreGive(s_task_stop_sem);
        TickType_t timeout = pdMS_TO_TICKS(2000);
        while (s_play_task != NULL && timeout--) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (s_play_task) {
            vTaskDelete(s_play_task);
            s_play_task = NULL;
        }
    }
    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    if (s_audio_file) {
        fclose(s_audio_file);
        s_audio_file = NULL;
    }
    // 清空 PCM 队列残留
    if (s_pcm_queue) {
        pcm_block_t block;
        while (xQueueReceive(s_pcm_queue, &block, 0) == pdTRUE) {
            free(block.data);
        }
    }
    s_state = STATE_STOPPED;
    s_source = SOURCE_NONE;
    s_seek_pending = false;
    xSemaphoreGive(s_file_mutex);
    return ESP_OK;
}

esp_err_t audio_player_pause(void)
{
    if (s_state == STATE_PLAYING) {
        s_state = STATE_PAUSED;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t audio_player_resume(void)
{
    if (s_state == STATE_PAUSED) {
        s_state = STATE_PLAYING;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}

int32_t audio_player_get_progress_ms(void)
{
    if (s_state == STATE_STOPPED || s_bytes_per_sec == 0) {
        return -1;
    }
    return (int32_t)((uint64_t)s_played_bytes * 1000 / s_bytes_per_sec);
}

uint32_t audio_player_get_duration_ms(void)
{
    if (s_total_data_bytes == 0 || s_bytes_per_sec == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)s_total_data_bytes * 1000 / s_bytes_per_sec);
}

esp_err_t audio_player_seek_ms(uint32_t ms)
{
    if (s_state == STATE_STOPPED || s_total_data_bytes == 0 || s_bytes_per_sec == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t target_byte = (uint32_t)((uint64_t)ms * s_bytes_per_sec / 1000);
    if (target_byte > s_total_data_bytes) {
        target_byte = s_total_data_bytes;
    }

    xSemaphoreTake(s_file_mutex, portMAX_DELAY);
    s_seek_byte = target_byte;
    s_seek_pending = true;
    xSemaphoreGive(s_file_mutex);
    return ESP_OK;
}

esp_err_t audio_player_set_volume(uint8_t volume)
{
    return audio_code_set_volume(volume);
}

bool audio_player_is_playing(void)
{
    return (s_state == STATE_PLAYING || s_play_task != NULL);
}

esp_err_t audio_player_start_pcm_stream(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
{
    if (!sample_rate || !bits_per_sample || !channels) return ESP_ERR_INVALID_ARG;
    
    // 停止当前任何播放
    audio_player_stop();
    
    // 重新配置 I2S 参数
    ESP_RETURN_ON_ERROR(reconfigure_audio_params(sample_rate, bits_per_sample, channels), TAG, "reconfig failed");
    
    // 创建 PCM 队列（若未创建）
    if (!s_pcm_queue) {
        s_pcm_queue = xQueueCreate(PCM_QUEUE_LEN, sizeof(pcm_block_t));
        if (!s_pcm_queue) return ESP_ERR_NO_MEM;
    } else {
        // 清空队列中残留数据
        pcm_block_t block;
        while (xQueueReceive(s_pcm_queue, &block, 0) == pdTRUE) free(block.data);
    }
    
    s_source = SOURCE_PCM_QUEUE;
    s_state = STATE_STOPPED;
    s_seek_pending = false;
    
    xSemaphoreTake(s_task_stop_sem, 0); // 清空停止信号
    BaseType_t ret = xTaskCreate(play_task, "pcm_player", 8192, NULL, 5, &s_play_task);
    if (ret != pdPASS) {
        s_source = SOURCE_NONE;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_player_push_pcm(const int16_t *data, size_t samples)
{
    if (s_source != SOURCE_PCM_QUEUE) return ESP_ERR_INVALID_STATE;
    if (!data || samples == 0) return ESP_ERR_INVALID_ARG;
    
    size_t bytes = samples * sizeof(int16_t);
    uint8_t *copy = malloc(bytes);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, data, bytes);
    
    pcm_block_t block = { .data = copy, .len = bytes };
    if (xQueueSend(s_pcm_queue, &block, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_player_stop_pcm_stream(void)
{
    if (s_source == SOURCE_PCM_QUEUE) {
        audio_player_stop();  // 复用停止逻辑
    }
     // 清空 PCM 队列（如果存在）
    if (s_pcm_queue) {
        pcm_block_t block;
        while (xQueueReceive(s_pcm_queue, &block, 0) == pdTRUE) free(block.data);
    }
    s_source = SOURCE_NONE;
    return ESP_OK;
}




///////////////////////////////////////////////////
#define USE_TEST 1
#ifdef USE_TEST

#include <math.h>
#define TEST_SAMPLE_RATE 16000   // 与服务器期望的采样率一致
#define TEST_CHANNELS     1
#define TEST_BITS         16
#define TEST_DURATION_SEC 5      // 播放 5 秒
#define TEST_VOLUME       50

// 生成正弦波数据
static void generate_sine_wave(int16_t *buffer, int samples, int sample_rate, int freq_hz)
{
    double step = 2.0 * M_PI * freq_hz / sample_rate;
    for (int i = 0; i < samples; i++) {
        double value = sin(step * i);
        buffer[i] = (int16_t)(value * 32767); // 满幅度
    }
}

void test_pcm_player(void)
{
    ESP_LOGI(TAG, "Starting PCM player test...");

    // 3. 设置音频参数（采样率、位深、声道）
    ESP_ERROR_CHECK(audio_player_start_pcm_stream(TEST_SAMPLE_RATE, TEST_BITS, TEST_CHANNELS));

    // 4. 设置音量（可选）
    //audio_player_set_volume(TEST_VOLUME);

    // 5. 准备正弦波数据
    int samples_per_buffer = 1024;           // 每次推送 1024 个样本
    int total_samples = TEST_SAMPLE_RATE * TEST_DURATION_SEC;
    int16_t *buffer = malloc(samples_per_buffer * sizeof(int16_t));
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return;
    }

    ESP_LOGI(TAG, "Generating sine wave %d Hz, duration %d sec", 1000, TEST_DURATION_SEC);
    int sent_samples = 0;
    while (sent_samples < total_samples) {
        int remaining = total_samples - sent_samples;
        int chunk_samples = (remaining > samples_per_buffer) ? samples_per_buffer : remaining;
        generate_sine_wave(buffer, chunk_samples, TEST_SAMPLE_RATE, 1000);
        audio_player_push_pcm(buffer, chunk_samples);
        sent_samples += chunk_samples;
        // 等待约 10ms 避免队列溢出（模拟实时流）
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(buffer);
    ESP_LOGI(TAG, "Finished generating sine wave, waiting for playback...");
    // 等待播放完成（给队列一些处理时间）
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 可选：停止播放并清理
    // pcm_player_stop();
    // ESP_LOGI(TAG, "PCM player test finished");
}
#endif
