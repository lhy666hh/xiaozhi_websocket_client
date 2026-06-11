#ifndef __SD_H_
#define __SD_H_
#include <string.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

// 根据您的实际接线定义引脚
#define PIN_CLK     GPIO_NUM_38
#define PIN_CMD     GPIO_NUM_40
#define PIN_D0      GPIO_NUM_39
#define PIN_D1      GPIO_NUM_41
#define PIN_D2      GPIO_NUM_48
#define PIN_D3      GPIO_NUM_47

#define MOUNT_POINT "/sdcard" // SD卡的挂载路径

void sd_init(void);
int sdcard_filelist(const char(**file)[256]);
#endif