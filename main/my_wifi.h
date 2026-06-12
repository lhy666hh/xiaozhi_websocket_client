#ifndef __MY_WIFI_H_
#define __MY_WIFI_H_
#include <stdbool.h>
void user_wifi_sta_config(void);
void user_wifi_smart_config(void);
bool user_wait_got_ip(void);
void mqtt_start(void);

#endif