#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef esp_err_t (*rec_procees_pcm_cb)(const int16_t *, size_t);
void record_to_pcmfile(uint32_t rec_time);
void record_callback(uint32_t rec_time);

void record_pcm_to_queue(uint32_t rec_time, uint32_t rate, uint16_t bits, uint16_t channels,rec_procees_pcm_cb cb);
void recorder_frame_stop(void);
