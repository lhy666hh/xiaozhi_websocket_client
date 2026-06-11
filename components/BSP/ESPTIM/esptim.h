#ifndef __ESPTIM_H_
#define __ESPTIM_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

void esptim_int_init(void (*timer_callback)(void* arg),uint64_t tps);
#endif