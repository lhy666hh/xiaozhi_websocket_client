#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

#define TAG "font_ui"
//#define USE_TTF_FONT
#ifdef USE_TTF_FONT

// 全局字体指针
static lv_font_t *g_my_font = NULL;

// 一次性初始化 FreeType 并加载字体
void font_init(void) {
    // 	char font_path[270] = "S:/myfont.ttf";
	//读取字体文件
	lv_fs_file_t fd;
	lv_fs_res_t res = lv_fs_open(&fd,"S:/myfont.ttf",LV_FS_MODE_RD);
	if(res != LV_FS_RES_OK)
	{
		printf("打开字体文件失败，错误码：%d\n",res);
		return;
	}

	//获取文件大小
	uint32_t file_size = 0;
	lv_fs_seek(&fd,0,LV_FS_SEEK_END);
	lv_fs_tell(&fd,&file_size);
	lv_fs_seek(&fd,0,LV_FS_SEEK_SET);

	printf("字体文件大小：%lu 字节\n",file_size);

	//分配内存空间读取字体文件
	void *font_data = heap_caps_malloc(file_size,MALLOC_CAP_8BIT);
	if(font_data == NULL)
	{
		printf("内存分配失败\n");
		lv_fs_close(&fd);
		return;
	}

	printf("内存分配成功\n");

	//读取字体文件到内存
	uint32_t bytes_read = 0;
	printf("开始读取\n");
	res = lv_fs_read(&fd,font_data,file_size,&bytes_read);
	lv_fs_close(&fd);
	printf("结束读取\n");

	if(res != LV_FS_RES_OK || bytes_read != file_size)
	{
		printf("读取字体文件失败，已读：%lu,应读：%lu\n",bytes_read,file_size);
		heap_caps_free(font_data);
		return;

	}

	printf("font_data addr:%p,size:%lu,read:%lu\n",font_data,file_size,bytes_read);

	printf("初始化freetype\n");
	if(!lv_freetype_init(4,1,0))
	{
		printf("FreeType初始化失败\n");
		heap_caps_free(font_data);
		return;
	}
	printf("结束初始化freetype\n");

	//加载字体
	static lv_ft_info_t info;
	info.name = "ft";
	info.mem = font_data;
	info.mem_size = file_size;
	info.weight = 20;
	info.style = FT_FONT_STYLE_NORMAL;

	printf("字体开始加载");
	if(!lv_ft_font_init(&info))
	{
		printf("FreeType字体加载失败\n");
		heap_caps_free(font_data);
		return;
	}

	printf("FreeType字体加载成功\n");
    g_my_font = info.font; // 保存字体指针
    printf("Font loaded successfully\n");
}
#else

LV_FONT_DECLARE(myfont);   // 声明外部字体
#endif


static lv_obj_t* label = NULL;
static QueueHandle_t text_queue;
typedef struct 
{
	/* data */
	char* str;
	size_t len;
}text_data_t;

static void lv_timer_text_cb(struct _lv_timer_t *t)
{
	static text_data_t text_data = {0};
	if(text_data.str)
	{
		free(text_data.str);
		text_data.str = NULL;
		text_data.len = 0;
	}
	if(pdTRUE == xQueueReceive(text_queue,&text_data,pdMS_TO_TICKS(500)))
	{
		if(label&&text_data.len)
		{
			lv_label_set_text(label, text_data.str);
		}
	}

}

void write_text_to_label(const char* str)
{
	size_t len = strlen(str);
	if(len<=0)return;
	text_data_t text_data = {0};
	text_data.len = len+1;
	text_data.str = (char*)malloc(text_data.len);
	if(text_data.str)
	{
		snprintf(text_data.str,text_data.len,str);
		xQueueSend(text_queue,&text_data,portMAX_DELAY);
	}

}



// UI 显示函数，只负责创建控件
void ui_show_hz(void) {
	if(!text_queue)text_queue = xQueueCreate(5,sizeof(text_data_t));
	#ifdef USE_TTF_FONT
	//从sd卡中加载字体ttf文件到psam。但ttf文件过大，7MB以上
	font_init();
	
    if (g_my_font == NULL) {
        printf("Font not loaded\n");
        return;
    }
	#else
	//从sd卡中加载字体bin文件到psam。几百kb左右但字体大小固定  如果不加载的话，lcd刷屏会很慢，默认已经注释了
	extern bool load_font_to_psram(const char *path);
	// 3. 加载字体文件到 PSRAM
    if (load_font_to_psram("/sdcard/myfont.bin")) {
        ESP_LOGI(TAG, "字体加载成功");
    } else {
        ESP_LOGE(TAG, "字体加载失败，请检查 SD 卡");
        return;
    }

	#endif

    // 创建样式（可改为静态全局，仅初始化一次）
    static lv_style_t style;
    static bool style_inited = false;
    if (!style_inited) {
        lv_style_init(&style);

		#ifdef USE_TTF_FONT
        lv_style_set_text_font(&style, g_my_font);
		#else
		lv_style_set_text_font(&style, &myfont);
		#endif
        lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
        style_inited = true;
    }

    label = lv_label_create(lv_scr_act());
    lv_obj_add_style(label, &style, 0);
    lv_label_set_text(label, "字体显示LVGL\n你好");
	lv_obj_set_width(label, lv_pct(90));     // 限制宽度，开启换行
	lv_obj_set_style_pad_all(label, 5, 0);   // 可选内边距
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

	lv_timer_t *s_player_timer = lv_timer_create(lv_timer_text_cb,100,NULL);
}


//#include "esp_heap_caps.h"
// void ui_show_hz(void)
// {
// 	char font_path[270] = "S:/myfont.ttf";

// 	//读取字体文件
// 	lv_fs_file_t fd;
// 	lv_fs_res_t res = lv_fs_open(&fd,font_path,LV_FS_MODE_RD);
// 	if(res != LV_FS_RES_OK)
// 	{
// 		printf("打开字体文件失败，错误码：%d\n",res);
// 		return;
// 	}

// 	//获取文件大小
// 	uint32_t file_size = 0;
// 	lv_fs_seek(&fd,0,LV_FS_SEEK_END);
// 	lv_fs_tell(&fd,&file_size);
// 	lv_fs_seek(&fd,0,LV_FS_SEEK_SET);

// 	printf("字体文件大小：%lu 字节\n",file_size);

// 	//分配内存空间读取字体文件
// 	void *font_data = heap_caps_malloc(file_size,MALLOC_CAP_8BIT);
// 	if(font_data == NULL)
// 	{
// 		printf("内存分配失败\n");
// 		lv_fs_close(&fd);
// 		return;
// 	}

// 	printf("内存分配成功\n");

// 	//读取字体文件到内存
// 	uint32_t bytes_read = 0;
// 	printf("开始读取\n");
// 	res = lv_fs_read(&fd,font_data,file_size,&bytes_read);
// 	lv_fs_close(&fd);
// 	printf("结束读取\n");

// 	if(res != LV_FS_RES_OK || bytes_read != file_size)
// 	{
// 		printf("读取字体文件失败，已读：%lu,应读：%lu\n",bytes_read,file_size);
// 		heap_caps_free(font_data);
// 		return;

// 	}

// 	printf("font_data addr:%p,size:%lu,read:%lu\n",font_data,file_size,bytes_read);

// 	printf("初始化freetype\n");
// 	if(!lv_freetype_init(4,1,0))
// 	{
// 		printf("FreeType初始化失败\n");
// 		heap_caps_free(font_data);
// 		return;
// 	}
// 	printf("结束初始化freetype\n");

// 	//加载字体
// 	static lv_ft_info_t info;
// 	info.name = "ft";
// 	info.mem = font_data;
// 	info.mem_size = file_size;
// 	info.weight = 20;
// 	info.style = FT_FONT_STYLE_NORMAL;

// 	printf("字体开始加载");
// 	if(!lv_ft_font_init(&info))
// 	{
// 		printf("FreeType字体加载失败\n");
// 		heap_caps_free(font_data);
// 		return;
// 	}

// 	printf("FreeType字体加载成功\n");

// 	//创建lable标签
// 	static lv_style_t style;
// 	lv_style_init(&style);
// 	lv_style_set_text_font(&style,info.font);
// 	lv_style_set_text_align(&style,LV_TEXT_ALIGN_CENTER);

// 	lv_obj_t* label = lv_label_create(lv_scr_act());
// 	lv_obj_add_style(label,&style,0);
// 	lv_label_set_text(label,"字体显示LVGL\n你好");
// 	lv_obj_align(label,LV_ALIGN_CENTER,0,0);

// }