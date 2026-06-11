#ifndef __AUDIO_CODE_H_
#define __AUDIO_CODE_H_
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev.h"
#define  ES8311_ADD_0          0x18   // ES8311  I2C地址

#define BSP_I2C_SDA           (GPIO_NUM_16)   // SDA引脚
#define BSP_I2C_SCL           (GPIO_NUM_15)   // SCL引脚

#define BSP_I2C_NUM           (0)            // I2C外设
#define BSP_I2C_FREQ_HZ       400000         // 100kHz

/* Example configurations */
#define EXAMPLE_RECV_BUF_SIZE   (2400)
#define EXAMPLE_SAMPLE_RATE     (44100)
#define EXAMPLE_MCLK_MULTIPLE   (256) // If not using 24-bit data width, 256 should be enough
#define EXAMPLE_MCLK_FREQ_HZ    (EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE)
#define EXAMPLE_VOICE_VOLUME    (50)

/* I2S port and GPIOs */
#define I2S_NUM         (0)
#define I2S_MCK_IO      (GPIO_NUM_4)
#define I2S_BCK_IO      (GPIO_NUM_5)
#define I2S_WS_IO       (GPIO_NUM_7)
#define I2S_DO_IO       (GPIO_NUM_8)
#define I2S_DI_IO       (GPIO_NUM_6)

/***************    PA使能 GPIO ↓   ************************/
#define PA_EN_GPIO_NUM          GPIO_NUM_1   // PA使能引脚
extern esp_codec_dev_handle_t codec_dev;
esp_err_t bsp_i2c_init(void);
esp_err_t audio_code_init(void);
esp_err_t audio_code_set_volume(uint8_t volume);
esp_err_t audio_code_reconfigi2s(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels);
#endif
