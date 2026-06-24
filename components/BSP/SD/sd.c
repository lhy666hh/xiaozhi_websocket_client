#include "sd.h"
#include "dirent.h"

void sd_init(void)
{
	// --- SD 卡初始化逻辑 ---
	esp_err_t ret;
    // 1. 配置挂载参数
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // 若挂载失败则尝试格式化（谨慎使用）
        .max_files = 3,                  // 同时打开的最大文件数
        .allocation_unit_size = 16 * 1024
    };

    // 2. 配置SDMMC主机，使用默认配置，并可调整速度
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 可选: 将总线频率设置为 40MHz 以提升速度
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 或 40000
    // 可选: 启用DMA以提高数据传输效率
    // host.flags = SDMMC_HOST_FLAG_DMA;

    // 3. 配置SDMMC插槽，设置自定义引脚和总线宽度
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = PIN_CLK;
    slot_config.cmd = PIN_CMD;
    slot_config.d0 = PIN_D0;
    slot_config.d1 = PIN_D1;
    slot_config.d2 = PIN_D2;
    slot_config.d3 = PIN_D3;
    slot_config.width = 4;  // 使用4-bit模式以获得更高速度[reference:1]
    // 开启内部上拉电阻，有助于增强信号稳定性
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // 声明一个卡片指针
    sdmmc_card_t* card;

    // 4. 执行挂载操作
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        ESP_LOGE("SD_CARD", "Failed to mount SD card (0x%x)", ret);
        // 根据你的项目情况决定是停止运行还是继续
        return;
    }

    // 5. 打印SD卡信息到控制台
    sdmmc_card_print_info(stdout, card);
    ESP_LOGI("SD_CARD", "SD card mounted successfully!");

	// --- SD 卡操作示例 ---
    // 这里可以编写创建、写入、读取文件的测试代码
    // ...

    // 注意：如果项目需要长时间运行，通常不会在此处卸载SD卡，而是保持挂载状态以供后续使用。
    // 仅在需要安全移除卡片时调用下方函数
    // esp_vfs_fat_sdmmc_unmount();

	// 示例：写入文件
	FILE* f = fopen("/sdcard/test.txt", "w");
	if (f == NULL) {
		ESP_LOGE("FILE", "Failed to open file for writing");
	} else {
		fprintf(f, "Hello, ESP32-S3!\n");
		fclose(f);
		ESP_LOGI("FILE", "File written");
	}

	// 示例：读取文件
	f = fopen("/sdcard/test.txt", "r");
	if (f == NULL) {
		ESP_LOGE("FILE", "Failed to open file for reading");
	} else {
		char line[128];
		if (fgets(line, sizeof(line), f) != NULL) {
			ESP_LOGI("FILE", "Read from file: %s", line);
		}
		fclose(f);
	}
}

int sdcard_filelist(const char(**file)[256])
{
	DIR *dir;//目录
	struct dirent *entry;//文件入口

	static char filename[20][256] = {};

	dir = opendir(MOUNT_POINT);
	if(!dir)return 0;

	int file_cnt = 0;
	while((entry = readdir(dir))!=NULL)
	{
		snprintf(&filename[file_cnt][0],256,"%s",entry->d_name);
		printf("file:%s\n",entry->d_name);
		file_cnt++;
		if(file_cnt>=20)break;
		
	}
	*file = filename;
	return file_cnt;
}