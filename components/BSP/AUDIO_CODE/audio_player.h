/**
 * @file audio_player.h
 * @brief 基于 I2S + ES8311 的 WAV 音频播放器封装库（支持暂停/继续、进度控制）
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化音频播放器（I2S + ES8311 + PA 使能）
 * @return ESP_OK 成功，否则失败
 */
esp_err_t audio_player_init(void);

/**
 * @brief 播放指定路径的 WAV 文件（PCM 格式）
 * @param file_path 文件路径，如 "/sdcard/music.wav"
 * @param loop      是否循环播放（true: 循环，false: 单次）
 * @return ESP_OK 成功开始播放，否则返回错误码
 * @note  若已有正在播放的任务，会先停止并销毁，再启动新播放任务
 */
esp_err_t audio_player_play(const char *file_path, bool loop);

/**
 * @brief 停止当前播放任务，关闭文件
 * @return ESP_OK 成功
 */
esp_err_t audio_player_stop(void);

/**
 * @brief 暂停正在播放的音频
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 如果没有正在播放的任务
 */
esp_err_t audio_player_pause(void);

/**
 * @brief 恢复暂停的音频
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 如果没有暂停的任务
 */
esp_err_t audio_player_resume(void);

/**
 * @brief 获取当前播放进度（毫秒）
 * @return 进度毫秒数，若未播放则返回 -1
 */
int32_t audio_player_get_progress_ms(void);

/**
 * @brief 跳转到指定时间位置（毫秒）
 * @param ms 目标时间（毫秒），范围 [0, 音频总时长]
 * @return ESP_OK 成功，ESP_ERR_INVALID_ARG 参数越界，ESP_ERR_INVALID_STATE 未播放
 */
esp_err_t audio_player_seek_ms(uint32_t ms);

/**
 * @brief 获取音频总时长（毫秒）
 * @return 总时长毫秒数，若未播放则返回 0
 */
uint32_t audio_player_get_duration_ms(void);

/**
 * @brief 设置播放音量
 * @param volume 音量值 0~100
 * @return ESP_OK 成功
 */
esp_err_t audio_player_set_volume(uint8_t volume);


// 在 audio_player.h 末尾，所有函数声明之后添加
bool audio_player_is_playing(void);

esp_err_t reconfigure_audio_params(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels);


/** 
 * @brief 开始 PCM 流式播放（需提前配置好采样率、位深、声道数）
 * @param sample_rate      采样率 (Hz)
 * @param bits_per_sample   位深 (16 或 32)
 * @param channels          声道数 (1 或 2)
 * @return ESP_OK 成功，否则失败
 */
esp_err_t audio_player_start_pcm_stream(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels);

/**
 * @brief 推送 PCM 数据到播放队列（异步播放）
 * @param data      PCM 数据指针（int16_t 类型，若 32 位则用 int32_t）
 * @param samples   样本个数（每个样本为一个声道的数据点）
 * @return ESP_OK 成功，ESP_ERR_INVALID_STATE 未处于 PCM 流模式
 */
esp_err_t audio_player_push_pcm(const int16_t *data, size_t samples);

/**
 * @brief 停止当前 PCM 流播放，释放队列资源
 * @return ESP_OK
 */
esp_err_t audio_player_stop_pcm_stream(void);

void test_pcm_player(void);
#ifdef __cplusplus
}
#endif