#ifndef __EXIT_H_
#define __EXIT_H_
#include "driver/gpio.h"
#include "esp_system.h"

#define BOOT_INT_GPIO_PIN  GPIO_NUM_0
#define BOOT gpio_get_level(BOOT_INT_GPIO_PIN)
//#define BOOT_PRESS     1
typedef enum{
	BOOT_PRESS = 1,
	BOOT_RELEASE =2,
	BOOT_NONE
}KEY_STA;

void exit_init(void);
KEY_STA get_exit_key_flag(void);
#endif