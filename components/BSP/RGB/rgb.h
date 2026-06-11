#ifndef __RGB_H_
#define __RGB_H_

#include <driver/rmt.h>


void ws2812_set_color(uint8_t r, uint8_t g, uint8_t b) ;
void rgb_init(void);
#endif