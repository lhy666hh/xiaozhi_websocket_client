#include "esptim.h"
//系统定时器配置
void esptim_int_init(void (*timer_callback)(void* arg),uint64_t tps)
{
	esp_timer_handle_t esp_tim_handle;
	esp_timer_create_args_t tim_periodic_arg = {
		.callback = timer_callback,
		.arg = NULL,
	};

	esp_timer_create(&tim_periodic_arg,&esp_tim_handle);
	esp_timer_start_periodic(esp_tim_handle,tps);

}
