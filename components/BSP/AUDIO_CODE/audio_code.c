#include "audio_code.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "es8311.h"

#include "driver/i2c_master.h"


#define USE_ESP_CODEC_DEV  1
static const char *TAG = "i2s_es8311";

i2s_chan_handle_t tx_handle = NULL;
i2s_chan_handle_t rx_handle = NULL;   // 接收通道句柄


es8311_handle_t es_handle;
static uint32_t cur_sample_rate = 44100;
static uint16_t cur_bits_per_sample = 16;
static uint16_t cur_channels = 2;


// 初始化I2S外设
static esp_err_t i2s_driver_init(void)
{
    /* 配置i2s发送通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));
    /* 初始化i2s为std模式 并打开i2s发送通道 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(cur_sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG((cur_bits_per_sample == 16) ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
        												(cur_channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
	ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    return ESP_OK;
}


#if USE_ESP_CODEC_DEV
// 当前配置参数（用于重配时重建）

i2c_master_bus_handle_t i2c_bus_handle = NULL;
esp_codec_dev_handle_t codec_dev = NULL;
//如使用旧版i2c配置,需在menuconfig启用Enable backward compatibility for the i2c driver,注意esp不支持混用旧版i2c和新版i2c
esp_err_t bsp_i2c_init(void)
{
    if (i2c_bus_handle != NULL)
    {
        return ESP_OK;
    }
    i2c_master_bus_config_t i2c_bus_config = {0};
    i2c_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_config.i2c_port = BSP_I2C_NUM;
    i2c_bus_config.scl_io_num = BSP_I2C_SCL;
    i2c_bus_config.sda_io_num = BSP_I2C_SDA;
    i2c_bus_config.glitch_ignore_cnt = 7;
    i2c_bus_config.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
#if 0
    // #include "driver/i2c.h"
    // i2c_config_t conf = {
    //     .mode = I2C_MODE_MASTER,
    //     .sda_io_num = I2C_SDA_IO,
    //     .scl_io_num = I2C_SCL_IO,
    //     .sda_pullup_en = GPIO_PULLUP_ENABLE,
    //     .scl_pullup_en = GPIO_PULLUP_ENABLE,
    //     .master.clk_speed = 100000,
    // };
    // esp_err_t ret = i2c_param_config(I2S_NUM, &conf);
    // if (ret != ESP_OK)
    // {
    //     return ret;
    // }
    // return i2c_driver_install(I2S_NUM, conf.mode, 0, 0, 0);
#endif    
}

//总体流程 
//1.创建i2s数据接口
//2.创建i2c控制接口
//3.创建gpio接口
//4.创建es8311 codec接口
//5.创建esp codec dev接口
//注意移植需要传入初始化时使用的handle,确保正确链接
esp_err_t es8311_codec_init(void)
{
    //1.创建i2s数据接口
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
	if(data_if==NULL)
	{
		ESP_LOGE(TAG, "data_if is NULL!");
		return ESP_FAIL;
	}
    //2.创建i2c控制接口
    audio_codec_i2c_cfg_t i2c_cfg = {
        .bus_handle = i2c_bus_handle,
        .addr = ES8311_CODEC_DEFAULT_ADDR};
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH, // ADC和DAC工作
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = PA_EN_GPIO_NUM,
		.pa_reverted = true,
        .use_mclk = true,
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,              // es8311_codec_new 获取到的接口实现
        .data_if = data_if,                    // audio_codec_new_i2s_data 获取到的数据接口实现
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, // 设备同时支持录制和播放
    };

    codec_dev = esp_codec_dev_new(&dev_cfg);
	if(codec_dev==NULL)return ESP_ERR_INVALID_RESPONSE;
	else 
	{
		esp_codec_dev_sample_info_t open_cfg = {
		.sample_rate = cur_sample_rate,
		.channel = cur_channels,
		.bits_per_sample = cur_bits_per_sample,
		};
		esp_err_t ret = esp_codec_dev_open(codec_dev, &open_cfg);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "open codec dev failed: %d", ret);
			return ret;
		}
		return ESP_OK;
	}
}

// 对外接口：设置播放音量 (0-100)
esp_err_t audio_code_set_volume(uint8_t volume)
{
    if (!codec_dev) return ESP_FAIL;
    return esp_codec_dev_set_out_vol(codec_dev, volume);
}

static esp_err_t audio_code_mute(bool mute)
{
    if (!codec_dev) return ESP_FAIL;
    return esp_codec_dev_set_out_mute(codec_dev, mute);
}


// 销毁当前 codec 设备并释放 I2S 资源
// static void destroy_codec_device(void)
// {
//     if (codec_dev) {
//         esp_codec_dev_close(codec_dev);
//         esp_codec_dev_delete(codec_dev);
//         codec_dev = NULL;
//     }
//     if (tx_handle) {
//         i2s_channel_disable(tx_handle);
//         i2s_del_channel(tx_handle);
//         tx_handle = NULL;
//     }
//     if (rx_handle) {
//         i2s_channel_disable(rx_handle);
//         i2s_del_channel(rx_handle);
//         rx_handle = NULL;
//     }
// }

// 对外接口：动态重配采样率、位宽、通道数
esp_err_t audio_code_reconfigi2s(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
{
    // 参数合法性检查
    if (sample_rate == 0 || (bits_per_sample != 16 && bits_per_sample != 32) ||
        (channels != 1 && channels != 2)) {
        ESP_LOGE(TAG, "Invalid reconfig params: %u Hz, %u bit, %u ch",
                 sample_rate, bits_per_sample, channels);
        return ESP_ERR_INVALID_ARG;
    }

    // 如果参数与当前相同，直接返回成功
    if (sample_rate == cur_sample_rate &&
        bits_per_sample == cur_bits_per_sample &&
        channels == cur_channels) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reconfiguring audio: %u Hz, %u bit, %u ch",
             sample_rate, bits_per_sample, channels);
	
	// 1. 静音
    audio_code_mute(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 1. 关闭 codec 设备（不删除设备本身）
    if (codec_dev) {
        esp_err_t ret = esp_codec_dev_close(codec_dev);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "codec close failed: %d", ret);
        }
    }

    // 2. 禁用 I2S 通道（允许“未启用”错误，因为可能已经被 codec 内部禁用）
    // if (tx_handle) {
    //     esp_err_t ret = i2s_channel_disable(tx_handle);
    //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    //         ESP_LOGE(TAG, "disable tx failed: %d", ret);
    //         return ret;
    //     }
    // }
    // if (rx_handle) {
    //     esp_err_t ret = i2s_channel_disable(rx_handle);
    //     if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    //         ESP_LOGE(TAG, "disable rx failed: %d", ret);
    //         return ret;
    //     }
    // }

    // 3. 重新配置 I2S 时钟和槽位
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        (bits_per_sample == 16) ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
        (channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);

    if (tx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg),
                            TAG, "reconfig tx clock failed");
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(tx_handle, &slot_cfg),
                            TAG, "reconfig tx slot failed");
    }
    if (rx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(rx_handle, &clk_cfg),
                            TAG, "reconfig rx clock failed");
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(rx_handle, &slot_cfg),
                            TAG, "reconfig rx slot failed");
    }

    // 4. 重新启用 I2S 通道
    if (tx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "enable tx failed");
    }
    if (rx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "enable rx failed");
    }

    // 5. 更新全局参数
    cur_sample_rate = sample_rate;
    cur_bits_per_sample = bits_per_sample;
    cur_channels = channels;

    // 6. 重新打开 codec 设备，传入新的采样参数
    if (codec_dev) {
        esp_codec_dev_sample_info_t open_cfg = {
            .sample_rate = cur_sample_rate,
            .channel = cur_channels,
            .bits_per_sample = cur_bits_per_sample,
        };
        esp_err_t ret = esp_codec_dev_open(codec_dev, &open_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "reopen codec dev failed: %d", ret);
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "codec device not initialized");
        return ESP_FAIL;
    }

	// 8. 延迟等待新配置稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 9. 取消静音
    audio_code_mute(false);
    ESP_LOGI(TAG, "Audio reconfigured successfully");
    return ESP_OK;
}
// esp_err_t audio_code_reconfigi2s(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
// {
//     // 参数合法性检查
//     if (sample_rate == 0 || (bits_per_sample != 16 && bits_per_sample != 32) ||
//         (channels != 1 && channels != 2)) {
//         ESP_LOGE(TAG, "Invalid reconfig params: %u Hz, %u bit, %u ch",
//                  sample_rate, bits_per_sample, channels);
//         return ESP_ERR_INVALID_ARG;
//     }

//     // 如果参数与当前相同，直接返回成功
//     if (sample_rate == cur_sample_rate &&
//         bits_per_sample == cur_bits_per_sample &&
//         channels == cur_channels) {
//         return ESP_OK;
//     }

//     ESP_LOGI(TAG, "Reconfiguring audio: %u Hz, %u bit, %u ch",
//              sample_rate, bits_per_sample, channels);

//     // 销毁现有设备
//     destroy_codec_device();

//     // 更新全局参数
//     cur_sample_rate = sample_rate;
//     cur_bits_per_sample = bits_per_sample;
//     cur_channels = channels;
	
// 	i2s_driver_init();

//     // 重新创建设备
//     esp_err_t ret = es8311_codec_init();
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "Reconfiguration failed, fallback to previous params?");
//         return ESP_FAIL;
//     }

//     return ESP_OK;
// }

#else
// static esp_err_t bsp_i2c_init(void)
// {
//     i2c_config_t i2c_conf = {
//         .mode = I2C_MODE_MASTER,
//         .sda_io_num = BSP_I2C_SDA,
//         .sda_pullup_en = GPIO_PULLUP_ENABLE,
//         .scl_io_num = BSP_I2C_SCL,
//         .scl_pullup_en = GPIO_PULLUP_ENABLE,
//         .master.clk_speed = BSP_I2C_FREQ_HZ
//     };
//     i2c_param_config(BSP_I2C_NUM, &i2c_conf);

//     return i2c_driver_install(BSP_I2C_NUM, i2c_conf.mode, 0, 0, 0);
// }

// 初始化I2C接口 并初始化es8311芯片
static esp_err_t es8311_codec_init(void)
{
    /* 初始化I2C接口 */
    ESP_ERROR_CHECK(bsp_i2c_init());

    /* 初始化es8311芯片 */
    es_handle = es8311_create(BSP_I2C_NUM, ES8311_ADD_0);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE,
        .sample_frequency = EXAMPLE_SAMPLE_RATE
    };

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, EXAMPLE_SAMPLE_RATE * EXAMPLE_MCLK_MULTIPLE, EXAMPLE_SAMPLE_RATE), TAG, "set es8311 sample frequency failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL), TAG, "set es8311 volume failed");
	// 1. 配置为模拟麦克风（禁用 PDM）
    ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), TAG, "set es8311 microphone failed");
	
	// 可选：调整麦克风增益
	ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, 6)); // 根据实际驱动 API


	// 打印所有寄存器值 (0x00 ~ 0x49)
    es8311_register_dump(es_handle);
    return ESP_OK;
}
esp_err_t audio_code_set_volume(uint8_t volume)
{
    static es8311_handle_t es_handle = NULL;
    if (es_handle == NULL) {
        es_handle = es8311_create(BSP_I2C_NUM, ES8311_ADD_0);
        if (!es_handle) return ESP_FAIL;
    }
    return es8311_voice_volume_set(es_handle, volume, NULL);
}

esp_err_t audio_code_reconfigi2s(uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
{
    // 1. 禁用发送和接收通道（避免重配过程中数据混乱）
    if (tx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(tx_handle), TAG, "disable tx failed");
    }
    if (rx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(rx_handle), TAG, "disable rx failed");
    }

    // 2. 时钟配置（发送和接收共用）
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;

    // 3. 槽位配置
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        (bits_per_sample == 16) ? I2S_DATA_BIT_WIDTH_16BIT : I2S_DATA_BIT_WIDTH_32BIT,
        (channels == 2) ? I2S_SLOT_MODE_STEREO : I2S_SLOT_MODE_MONO);

    // 4. 同时重配置 tx 和 rx
    if (tx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg), TAG, "reconfig tx clock failed");
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(tx_handle, &slot_cfg), TAG, "reconfig tx slot failed");
    }
    if (rx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(rx_handle, &clk_cfg), TAG, "reconfig rx clock failed");
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(rx_handle, &slot_cfg), TAG, "reconfig rx slot failed");
    }

    // 5. 重新启用通道
    if (tx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "enable tx failed");
    }
    if (rx_handle) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "enable rx failed");
    }

    // 6. 配置 ES8311（只配置一次，因为编解码器同时服务于录音和播放）
    static es8311_handle_t es_handle = NULL;
    if (es_handle == NULL) {
        es_handle = es8311_create(BSP_I2C_NUM, ES8311_ADD_0);
        ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, TAG, "es8311 create failed");
    }

    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = sample_rate * EXAMPLE_MCLK_MULTIPLE,
        .sample_frequency = sample_rate
    };
    ESP_RETURN_ON_ERROR(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "es8311 init failed");
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, sample_rate * EXAMPLE_MCLK_MULTIPLE, sample_rate), TAG, "es8311 sample freq failed");

    ESP_LOGI(TAG, "Audio reconfigured to %dHz, %dbit, %dch", sample_rate, bits_per_sample, channels);
    return ESP_OK;
}
#endif




// 初始化GPIO42为PA使能引脚并置低使能
static void pa_gpio_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PA_EN_GPIO_NUM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PA_EN_GPIO_NUM, 1);//拉高关闭
}








// void i2s_init(void)
// {
//     // 1. 配置I2S通道
//     i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
//     ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle));

//     // 2. 配置I2S标准模式
//     i2s_std_config_t std_cfg = {
//         .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(EXAMPLE_SAMPLE_RATE),
//         .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
//         .gpio_cfg = {
//             .mclk = I2S_MCK_IO,
//             .bclk = I2S_BCK_IO,
//             .ws = I2S_WS_IO,
//             .dout = I2S_DO_IO,
//             .din = I2S_DI_IO,
//             .invert_flags = {
//                 .mclk_inv = false,
//                 .bclk_inv = false,
//                 .ws_inv = false,
//             },
//         },
//     };

//     // 3. 初始化和启用I2S通道
//     if (tx_handle != NULL)
//     {
//         ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
//         ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
//     }

//     if (rx_handle != NULL)
//     {
//         ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
//         ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
//     }
// }



esp_err_t audio_code_init(void)
{
	pa_gpio_init();
	ESP_RETURN_ON_ERROR(i2s_driver_init(), TAG, "I2S driver init failed");
	ESP_RETURN_ON_ERROR(es8311_codec_init(), TAG, "ES8311 init failed");
	// 开始静音处理
    audio_code_mute(true);                 // 静音
	vTaskDelay(pdMS_TO_TICKS(500));
	audio_code_mute(false);                // 取消静音，正常播放
	ESP_LOGI(TAG, "Audio init finished, pop-free start");
	return ESP_OK;
}


