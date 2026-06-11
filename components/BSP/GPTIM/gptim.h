#ifndef __GPTIM_H_
#define __GPTIM_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/timer.h"
#include "esp_clk_tree.h"

typedef struct
{
	timer_src_clk_t clk_src;//时钟源
	int timer_group;//组
	int timer_idx;//对应定时器
	uint64_t timing_time;//定时时间
	uint64_t alarm_value;//报警值
	timer_autoreload_t auto_reload;//是否启用重装载
	uint64_t timer_count_value;//计数器当前值
}timg_config_t;

void timg_init(timg_config_t *timg_config);
#endif