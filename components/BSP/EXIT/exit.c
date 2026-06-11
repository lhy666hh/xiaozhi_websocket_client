#include "exit.h"
static volatile uint8_t key_press_flag = 0;
//IRAM_ATTR  :放入内部ram运行，中断响应速度更快
void IRAM_ATTR exit_gpio_isr_handle(void *arg)
{
	uint32_t gpio_num = (uint32_t)arg;
	
	if(gpio_num == BOOT_INT_GPIO_PIN)
	{
		key_press_flag =1;
	}
}
void exit_init(void)
{
	gpio_config_t gpio_init_struct = {0};
	gpio_init_struct.intr_type = GPIO_INTR_NEGEDGE;
	gpio_init_struct.mode = GPIO_MODE_INPUT;
	gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
	gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
	gpio_init_struct.pin_bit_mask = 1ull << BOOT_INT_GPIO_PIN;
	gpio_config(&gpio_init_struct);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(BOOT_INT_GPIO_PIN,exit_gpio_isr_handle,BOOT_INT_GPIO_PIN);
}

//按键检测状态机  外部调用间隔》=10ms
KEY_STA get_exit_key_flag(void)
{
	static uint8_t sta = 0;
	KEY_STA key_press = BOOT_NONE;
	if(key_press_flag)
	{
		switch(sta)
		{
			case 0:
				if(BOOT==0)
				{
					sta = 1;
				}
				break;
			case 1:
				if(BOOT==0)
				{
					sta = 2;
					key_press = BOOT_PRESS;
				}
				else
				{
					sta = 0;
					key_press_flag = 0;
				}
				break;
			case 2:
				if(BOOT!=0)//释放
				{
					sta = 0;
					key_press_flag = 0;
					key_press = BOOT_RELEASE;
				}
				break;
			default:break;
		}
	}
	return key_press;
}
