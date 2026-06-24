#include "lv_my_demo.h"
#define TAG "font_ui"
//#define USE_TTF_FONT
#ifdef _USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "mjpeg_frame.h"
#include "img_converters.h"
#include "audio_player.h"
#include "xiaozhi_client.h"
static QueueHandle_t text_queue = NULL;

#ifdef USE_TTF_FONT

// 全局字体指针
static lv_font_t* g_my_font = NULL;

// 一次性初始化 FreeType 并加载字体
void font_init(void) {
    // 	char font_path[270] = "S:/myfont.ttf";
    //读取字体文件
    lv_fs_file_t fd;
    lv_fs_res_t res = lv_fs_open(&fd, "S:/myfont.ttf", LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK)
    {
        printf("打开字体文件失败，错误码：%d\n", res);
        return;
    }

    //获取文件大小
    uint32_t file_size = 0;
    lv_fs_seek(&fd, 0, LV_FS_SEEK_END);
    lv_fs_tell(&fd, &file_size);
    lv_fs_seek(&fd, 0, LV_FS_SEEK_SET);

    printf("字体文件大小：%lu 字节\n", file_size);

    //分配内存空间读取字体文件
    void* font_data = heap_caps_malloc(file_size, MALLOC_CAP_8BIT);
    if (font_data == NULL)
    {
        printf("内存分配失败\n");
        lv_fs_close(&fd);
        return;
    }

    printf("内存分配成功\n");

    //读取字体文件到内存
    uint32_t bytes_read = 0;
    printf("开始读取\n");
    res = lv_fs_read(&fd, font_data, file_size, &bytes_read);
    lv_fs_close(&fd);
    printf("结束读取\n");

    if (res != LV_FS_RES_OK || bytes_read != file_size)
    {
        printf("读取字体文件失败，已读：%lu,应读：%lu\n", bytes_read, file_size);
        heap_caps_free(font_data);
        return;

    }

    printf("font_data addr:%p,size:%lu,read:%lu\n", font_data, file_size, bytes_read);

    printf("初始化freetype\n");
    if (!lv_freetype_init(4, 1, 0))
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
    if (!lv_ft_font_init(&info))
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



#endif
typedef struct
{
    /* data */
    char* str;
    size_t len;
}text_data_t;

typedef enum {
    RECORD_STATE_IDLE,    // 空闲（初始）
    RECORD_STATE_RECORDING, // 录音中
    RECORD_STATE_STOPPED   // 已停止（可再次开始）
} record_state_t;

static record_state_t record_state = RECORD_STATE_IDLE;
//连接按钮状态
static bool web_client_connected = false;
static lv_obj_t* web_switch = NULL;
static lv_timer_t* check_timer = NULL;

static lv_obj_t* text_label = NULL;
static lv_timer_t* s_player_timer = NULL;
static lv_obj_t* player_img = NULL;



void start_mjpeg_show(const char* img_path)
{
#ifdef _USE_ESP32
    jpeg_frame_start(img_path);
#endif
}

void stop_mjpeg_show(void)
{
#ifdef _USE_ESP32
    jpeg_frame_stop();
#endif
}

bool get_status_mjpeg_show(void)
{
#ifdef _USE_ESP32
    return jpeg_frame_running_status();
#else
    return true;
#endif
}



void write_text_to_label(const char* str)
{
#ifdef _USE_ESP32
    size_t len = strlen(str);
    if (len <= 0 || s_player_timer == NULL)return;
    text_data_t text_data = { 0 };
    text_data.len = len + 1;
    text_data.str = (char*)malloc(text_data.len);
    if (text_data.str)
    {
        snprintf(text_data.str, text_data.len, str);
#ifdef _USE_ESP32
        xQueueSend(text_queue, &text_data, portMAX_DELAY);
#endif
    }
#endif
}


void font_init(void)
{
#ifdef _USE_ESP32
#ifdef USE_TTF_FONT
    //从sd卡中加载字体ttf文件到psam。但ttf文件过大，7MB以上
    font_init();

    if (g_my_font == NULL) {
        printf("Font not loaded\n");
        return;
    }
#else
    //从sd卡中加载字体bin文件到psam。几百kb左右但字体大小固定  如果不加载的话，lcd刷屏会很慢，默认已经注释了
    extern bool load_font_to_psram(const char* path);
    // 3. 加载字体文件到 PSRAM
    if (load_font_to_psram("/sdcard/myfont.bin")) {
        ESP_LOGI(TAG, "字体加载成功");
    }
    else {
        ESP_LOGE(TAG, "字体加载失败，请检查 SD 卡");
        return;
    }

#endif
#endif
}
#ifdef _USE_ESP32
// 解码请求结构体
typedef struct {
    uint8_t* rgb565_out;       // 输出缓冲区（由解码任务分配）
    uint16_t width;
    uint16_t height;
} decode_req_t;
QueueHandle_t decode_queue = NULL;

static void decoder_task(void* arg)
{
    while (1)
    {
        static jpeg_frame_data_t frame_data = { 0 };
        if (frame_data.frame)
        {
            free(frame_data.frame);
            frame_data.frame = NULL;
            frame_data.len = 0;
        }
        jpeg_frame_get_one(&frame_data);

        if (frame_data.len && decode_queue)
        {
            decode_req_t img_data = { 0 };
            if (jpg2rgb565(frame_data.frame, frame_data.len, &img_data.rgb565_out, &img_data.width, &img_data.height, JPG_SCALE_NONE))
            {
                if (xQueueSend(decode_queue, &img_data, portMAX_DELAY) != pdTRUE) {
                    // 队列满，释放 JPEG 数据避免泄漏
                    free(img_data.rgb565_out);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}



#endif

void start_decoder_task(void) {
#ifdef _USE_ESP32
    if (!decode_queue)
    {
        decode_queue = xQueueCreate(2, sizeof(decode_req_t));  // 队列深度 2
        xTaskCreate(decoder_task, "decoder", 4096, NULL, 5, NULL); // 优先级 5，低于 LVGL（通常为 7~8）
    }
#endif
}

static void lv_timer_text_cb(struct _lv_timer_t* t)
{
#ifdef _USE_ESP32
    //文本解码显示
    static text_data_t text_data = { 0 };
    if (text_data.str)
    {
        free(text_data.str);
        text_data.str = NULL;
        text_data.len = 0;
    }
    if (pdTRUE == xQueueReceive(text_queue, &text_data, pdMS_TO_TICKS(0)))
    {
        if (text_label && text_data.len)
        {
            lv_label_set_text(text_label, text_data.str);
        }
    }

    //图片解码显示
    static decode_req_t img_data = { 0 };
    if (img_data.rgb565_out)
    {
        free(img_data.rgb565_out);
        img_data.rgb565_out = NULL;
        img_data.height = 0;
        img_data.width = 0;
    }
    if (xQueueReceive(decode_queue, &img_data, 0) == pdTRUE)
    {
        static lv_img_dsc_t img_dsc;
        memset(&img_dsc, 0, sizeof(lv_img_dsc_t));
        img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        img_dsc.header.w = img_data.width;
        img_dsc.header.h = img_data.height;
        img_dsc.data = img_data.rgb565_out;
        img_dsc.data_size = (uint32_t)img_data.width * (uint32_t)img_data.height * 2;

        lv_img_set_src(player_img, &img_dsc);
    }

    if (get_status_mjpeg_show() == false && record_state == RECORD_STATE_RECORDING)
    {
        start_mjpeg_show("/emotion/listen.mjpeg");
        write_text_to_label("聆听中......");
    }
#endif

}

// 音量滑块容器及相关对象
static lv_obj_t* vol_panel = NULL;    // 容器
static lv_obj_t* vol_slider = NULL;   // 滑块
static lv_obj_t* vol_label = NULL;    // 数值标签
static lv_timer_t* hide_timer = NULL; // 隐藏定时器

#define VOL_PANEL_WIDTH  50          // 容器宽度
#define VOL_PANEL_HEIGHT 200          // 容器高度
#define VOL_PANEL_X_HIDE (scr_act_width())      // 隐藏时的 X 坐标（屏幕右侧外）
#define VOL_PANEL_X_SHOW (scr_act_width() - VOL_PANEL_WIDTH) // 显示时的 X 坐标
#define VOL_STEP         5             // 每次加减的步长
#define HIDE_DELAY_MS    3000          // 3秒无操作自动隐藏
//滑块拖动事件回调
static void vol_slider_event_cb(lv_event_t* e)
{
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    // 更新标签
    char buf[8];
    if (val > 0)lv_snprintf(buf, sizeof(buf), "%d\n"LV_SYMBOL_VOLUME_MAX, val);
    else lv_snprintf(buf, sizeof(buf), "%d\n"LV_SYMBOL_VOLUME_MID, val);
    lv_label_set_text(vol_label, buf);
#ifdef _USE_ESP32
    audio_player_set_volume(val);
#endif
    // 重置隐藏定时器（只要有操作就重置）
    if (hide_timer) {
        lv_timer_reset(hide_timer);
    }
}
//加减按钮的事件回调
static void vol_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    int32_t delta = (int32_t)(intptr_t)lv_event_get_user_data(e); // +5 或 -5

    // 确保滑块已存在
    if (!vol_slider) return;

    // 获取当前值并调整
    int32_t val = lv_slider_get_value(vol_slider);
    val += delta;
    if (val < 0) val = 0;
    if (val > 100) val = 100;
    lv_slider_set_value(vol_slider, val, LV_ANIM_ON); // 带动画改变
#ifdef _USE_ESP32
    audio_player_set_volume(val);
#endif
    // 同步更新标签
    char buf[8];
    if (val > 0)lv_snprintf(buf, sizeof(buf), "%d\n"LV_SYMBOL_VOLUME_MAX, val);
    else lv_snprintf(buf, sizeof(buf), "%d\n"LV_SYMBOL_VOLUME_MID, val);
    lv_label_set_text(vol_label, buf);

    // ---- 面板滑出逻辑 ----
    if (vol_panel) {
        // 如果面板当前是隐藏状态（x 在隐藏位置），则滑入显示
        lv_coord_t cur_x = lv_obj_get_x(vol_panel);
        if (cur_x >= lv_disp_get_hor_res(NULL) - 1) { // 隐藏或接近隐藏
            // 使用动画从当前位置移动到显示位置
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, vol_panel);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_set_values(&a, cur_x, lv_disp_get_hor_res(NULL) - VOL_PANEL_WIDTH);
            lv_anim_set_time(&a, 300);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_start(&a);
        }
        // 如果已经显示，则不用重复滑入
    }

    // 重置定时器
    if (hide_timer) {
        lv_timer_reset(hide_timer);
        lv_timer_resume(hide_timer); // 确保定时器正在运行
    }
}
//隐藏定时器回调
static void hide_timer_cb(lv_timer_t* timer)
{
    // 超时，隐藏面板：从当前位置滑出到右侧外部
    if (!vol_panel) return;

    lv_coord_t cur_x = lv_obj_get_x(vol_panel);
    if (cur_x < lv_disp_get_hor_res(NULL)) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, vol_panel);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_set_values(&a, cur_x, lv_disp_get_hor_res(NULL));
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
        lv_anim_start(&a);
    }
    // 定时器自动暂停（隐藏后不再触发）
    lv_timer_pause(timer);
}
//创建音量面板（初始化时调用）
static void create_volume_panel(lv_obj_t* parent)
{
    lv_coord_t scr_w = lv_disp_get_hor_res(NULL);
    // 创建容器（父对象为当前活动屏幕，或你的 main_screen）
    vol_panel = lv_obj_create(parent);
    lv_obj_set_size(vol_panel, VOL_PANEL_WIDTH, VOL_PANEL_HEIGHT);
    lv_obj_set_pos(vol_panel, scr_w, 50); // 初始在右侧外部
    lv_obj_set_style_bg_color(vol_panel, lv_color_hex(0x333333), 0);
    lv_obj_set_style_bg_opa(vol_panel, LV_OPA_40, 0);
    lv_obj_set_style_radius(vol_panel, 10, 0);
    lv_obj_set_style_pad_all(vol_panel, 10, 0);


    // 创建滑块（垂直方向）
    vol_slider = lv_slider_create(vol_panel);
    lv_obj_set_size(vol_slider, 20, 150);
    lv_obj_center(vol_slider);
    lv_slider_set_range(vol_slider, 0, 100);
    lv_slider_set_value(vol_slider, 50, LV_ANIM_OFF);
    // 将滑块手柄的透明度设置为 0，实现“隐藏”效果
    lv_obj_set_style_bg_opa(vol_slider, LV_OPA_0, LV_PART_KNOB);
    lv_obj_set_style_border_opa(vol_slider, LV_OPA_0, LV_PART_KNOB);
    // 如果手柄有阴影等，也需要一并设置
    //lv_obj_set_style_shadow_opa(vol_slider, LV_OPA_0, LV_PART_KNOB);
    // 将填充部分设置为蓝色
    lv_obj_set_style_bg_color(vol_slider, lv_color_hex(0xF0F0F0), LV_PART_INDICATOR);

    // 创建数值标签（显示在滑块上方或下方）
    vol_label = lv_label_create(vol_panel);
    lv_label_set_text(vol_label, "50\n"LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_style_text_color(vol_label, lv_color_black(), 0);
    lv_obj_align(vol_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    // 添加滑块拖动事件，更新标签并重置定时器
    lv_obj_add_event_cb(vol_slider, vol_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 创建隐藏定时器（初始暂停）
    hide_timer = lv_timer_create(hide_timer_cb, HIDE_DELAY_MS, NULL);
    lv_timer_pause(hide_timer);

    lv_obj_move_foreground(vol_panel); // 确保在最上层
}

static void cleanup_volume_panel(void)
{
    // 删除定时器（如果有）
    if (hide_timer) {
        lv_timer_del(hide_timer);
        hide_timer = NULL;
    }

    // 清空全局指针（对象会被自动删除，无需手动删除）
    vol_panel = NULL;
    vol_slider = NULL;
    vol_label = NULL;
}





// UI 显示函数，只负责创建控件
void ui_show_hz(void) {
#ifdef _USE_ESP32
    if (!text_queue)text_queue = xQueueCreate(5, sizeof(text_data_t));
#endif
    // 创建样式（可改为静态全局，仅初始化一次）
    static lv_style_t style;
    static bool style_inited = false;
    if (!style_inited) {
        lv_style_init(&style);
#ifdef _USE_ESP32
#ifdef USE_TTF_FONT
        lv_style_set_text_font(&style, g_my_font);
#else
        lv_style_set_text_font(&style, &myfont);
#endif
#endif
        lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
        style_inited = true;
    }

    lv_obj_t* obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, 240, 320);
    //lv_obj_set_style_bg_color(obj,lv_color_black(),0);

    player_img = lv_img_create(obj);
    lv_obj_align(player_img, LV_ALIGN_TOP_MID, 0, 30);

    text_label = lv_label_create(obj);
    lv_obj_add_style(text_label, &style, 0);
    lv_label_set_text(text_label, "字体显示LVGL\n你好");
    lv_obj_set_width(text_label, lv_pct(90));     // 限制宽度，开启换行
    lv_obj_set_style_pad_all(text_label, 5, 0);   // 可选内边距
    lv_obj_align_to(text_label, player_img, LV_ALIGN_OUT_BOTTOM_MID, 0, 150);


#ifdef _USE_ESP32
    jpeg_frame_cfg_t cfg =
    {
        .buff_size = 100 * 1024,
    };
    jpeg_frame_config(&cfg);
#endif
    if (!s_player_timer)
    {
        s_player_timer = lv_timer_create(lv_timer_text_cb, 100, NULL);
    }
}

static void control_record_status(record_state_t status)
{
    // 根据当前状态切换
    switch (status) {
    case RECORD_STATE_IDLE:
        break;
    case RECORD_STATE_STOPPED:
        record_state = RECORD_STATE_STOPPED;
        // 在此处调用实际的录音停止函数
        // stop_recording();
#ifdef _USE_ESP32
        xiaozhi_client_send_opuspcm_stop();
        if (get_status_mjpeg_show())stop_mjpeg_show();
#endif
        //write_text_to_label("语音识别中,请稍后.");
        break;
    case RECORD_STATE_RECORDING:
        record_state = RECORD_STATE_RECORDING;
        // 在此处调用实际的录音启动函数
        // start_recording();
#ifdef _USE_ESP32
        if (get_status_mjpeg_show() == false && record_state == RECORD_STATE_RECORDING)
        {
            xiaozhi_client_send_opuspcm_start(30);
            start_mjpeg_show("/emotion/listen.mjpeg");
            //write_text_to_label("聆听中......");
        }
#endif
        break;
    default:
        break;
    }
}

static void clear_app_data_timer_cb(lv_timer_t* timer)
{
	
    
#ifdef _USE_ESP32
    // 执行你需要的操作（例如更新 UI、释放资源等）
    // ...
    if (decode_queue) {
        decode_req_t msg;
        // 取出并释放所有残留消息
        while (xQueueReceive(decode_queue, &msg, 0) == pdTRUE) {
            if (msg.rgb565_out) {
                free(msg.rgb565_out);
                msg.rgb565_out = NULL;
            }
        }
        // vQueueDelete(decode_queue);
        // decode_queue = NULL;
    }

    if (text_queue) {
        text_data_t msg;
        while (xQueueReceive(text_queue, &msg, 0) == pdTRUE) {
            if (msg.str) {
                free(msg.str);
                msg.str = NULL;
            }
        }
        // vQueueDelete(text_queue);
        // text_queue = NULL;
    }


    // 不需要手动删除，定时器执行完这一次后会自动销毁
#endif
    

    
}

static void back_mainpage_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        control_record_status(RECORD_STATE_STOPPED);
        #ifdef _USE_ESP32
		if(web_client_connected)
		{
			xiaozhi_client_clear_and_stop();
			web_client_connected = false;
		}
		#endif
		// 停止定时器（可选）
		if (check_timer) {
			lv_timer_del(check_timer);
			check_timer = NULL;
		}
		// 先清理音量面板相关资源
        cleanup_volume_panel();

		if (s_player_timer) 
		{
			lv_timer_del(s_player_timer);
			s_player_timer = NULL;
    	}

        lv_timer_t* timer = lv_timer_create(clear_app_data_timer_cb, 200, NULL); // 1000ms 后触发
        lv_timer_set_repeat_count(timer, 1);  // 只执行一次，执行后自动删除

        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_OUT_TOP, 1000, 0, true);


    }

}

static void play_random_music_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if(web_client_connected)
        { 
        LV_LOG_USER("play random music");
#ifdef _USE_ESP32
        xiaozhi_client_send_text("播放随机音乐");
#endif
        }
        else warning_msgbox("NOTICE", "Please connect to the web server first.");
    }
}

static void stop_ai_talk_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (web_client_connected) {
            LV_LOG_USER("stop AI talk");
			#ifdef _USE_ESP32
			xiaozhi_client_stop_tts();
			#endif
        } else {
            warning_msgbox("NOTICE", "Please connect to the web server first.");
        }
    }
}


static void test_btn_event_cb(lv_event_t* e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);
	static bool led_open_sta = false;
	
    if (code == LV_EVENT_CLICKED) {
		if(web_client_connected)
		{
			if(led_open_sta)
			{
				xiaozhi_client_send_text("设置led状态为0");
				if (label)lv_label_set_text(label, "OPEN");
			}
			else 
			{
				xiaozhi_client_send_text("设置led状态为1");
				if (label)lv_label_set_text(label, "CLOSE");
			}
			led_open_sta = !led_open_sta;
		}
		else warning_msgbox("NOTICE", "Please connect to the web server first.");
	}
}
static void record_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* label = lv_obj_get_child(btn, 0);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // 根据当前状态切换
        switch (record_state) {
        case RECORD_STATE_IDLE:
        case RECORD_STATE_STOPPED:
            // 进入录音状态
            if(web_client_connected)
            { 
                write_text_to_label("聆听中......");
                control_record_status(RECORD_STATE_RECORDING);
                if (label)lv_label_set_text(label, "STOP");
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF0000), 0); // 红色表示录音中
                // 在此处调用实际的录音启动函数
                // start_recording();
            }
            else warning_msgbox("NOTICE", "Please connect to the web server first.");
            break;

        case RECORD_STATE_RECORDING:
            // 停止录音

            write_text_to_label("语音识别中,请稍后.");
            control_record_status(RECORD_STATE_STOPPED);
            if (label)lv_label_set_text(label, "START");
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2196F3), 0); // 蓝色恢复
            // 在此处调用实际的录音停止函数
            // stop_recording();
            break;

        default:
            break;
        }
    }
}
bool get_actual_connection_status(void)
{
	#ifdef _USE_ESP32
	return xiaozhi_client_is_connected();
	#else
    // 替换为你的真实检测逻辑，例如读取 WiFi/MQTT 状态
    // 这里模拟：随机返回 true/false 用于测试
    static int counter = 0;
    counter++;
    if (counter % 5 == 0) return false; // 每5次检测返回 false（断开）
    return true;
	#endif
}
static void check_connection_cb(lv_timer_t* timer)
{
    // 如果开关处于开启状态但实际连接已丢失
    web_client_connected = get_actual_connection_status();
    if (!web_client_connected) {
        // 自动关闭开关
        lv_obj_clear_state(web_switch, LV_STATE_CHECKED); 

        // 弹出提示框
        //lv_obj_t* mbox = lv_msgbox_create(NULL, "NOTICE", "Web Server Disconnected", NULL, true);
        //lv_obj_center(mbox);
        //if(record_state== RECORD_STATE_RECORDING)control_record_status(RECORD_STATE_STOPPED);
        warning_msgbox("NOTICE", "Web Server Disconnected");
        // 可设置按钮文字，这里使用默认的 "OK"

        // 停止定时器（可选）
        lv_timer_del(timer);
        check_timer = NULL;
    }
}

static void web_switch_event_handler(lv_event_t* e)
{
    lv_obj_t* sw = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);

    // 更新全局状态
    web_client_connected = is_on;

    if (is_on) {
        // 执行连接操作（如启动 WebSocket/MQTT）
        // connect_web_client();
		// 启动 WebSocket 客户端
		#ifdef _USE_ESP32
		xiaozhi_client_config_and_start();
		#endif

        // 成功后可能设置定时器检测
        if (!check_timer) {
            check_timer = lv_timer_create(check_connection_cb, 5000, NULL); // 每x秒检测
        }
    }
    else {
        // 执行断开操作
        // disconnect_web_client();
		#ifdef _USE_ESP32
		xiaozhi_client_clear_and_stop();
		#endif

        // 停止定时器（可选）
        if (check_timer) {
            lv_timer_del(check_timer);
            check_timer = NULL;
        }
    }
}

lv_obj_t* create_talk_page(void)
{
    start_decoder_task();
    LV_IMG_DECLARE(game);
    lv_obj_t* talk_page = lv_obj_create(NULL);
    lv_obj_set_style_bg_img_src(talk_page, &game, 0);
    lv_obj_set_style_bg_img_opa(talk_page, LV_OPA_50, 0);
    lv_obj_set_style_bg_img_tiled(talk_page, true, 0);
#ifdef _USE_ESP32
    if (!text_queue)text_queue = xQueueCreate(5, sizeof(text_data_t));
#endif
    // 创建样式（可改为静态全局，仅初始化一次）
    static lv_style_t style;
    static bool style_inited = false;
    if (!style_inited) {
        lv_style_init(&style);
#ifdef _USE_ESP32
#ifdef USE_TTF_FONT
        lv_style_set_text_font(&style, g_my_font);
#else
        lv_style_set_text_font(&style, &myfont);
#endif
#endif
        lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);
        style_inited = true;
    }

    lv_obj_t* back_btn = general_btn_create(talk_page);
    lv_obj_set_size(back_btn, 60, 30);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 2);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x10C8B1), 0);
    lv_obj_add_event_cb(back_btn, back_mainpage_event_cb, LV_EVENT_CLICKED, NULL);
    /* 按钮上的标签 */
    lv_obj_t* btn_label = lv_label_create(back_btn);
    lv_label_set_text(btn_label, "BACK");
    lv_obj_center(btn_label);

    lv_obj_t* music_btn = general_btn_create(talk_page);
    lv_obj_set_size(music_btn, 30, 30);
    lv_obj_align_to(music_btn, back_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_color(music_btn, lv_color_hex(0xEC79EB), 0);
    lv_obj_add_event_cb(music_btn, play_random_music_event_cb, LV_EVENT_CLICKED, NULL);
    /* 按钮上的标签 */
    lv_obj_t* music_label = lv_label_create(music_btn);
    lv_label_set_text(music_label, LV_SYMBOL_AUDIO);
    lv_obj_center(music_label);

    lv_obj_t* volume_up_btn = general_btn_create(talk_page);
    lv_obj_set_size(volume_up_btn, 30, 30);
    lv_obj_align_to(volume_up_btn, music_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_color(volume_up_btn, lv_color_hex(0xEC79EB), 0);
    lv_obj_add_event_cb(volume_up_btn, vol_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)VOL_STEP);
    /* 按钮上的标签 */
    lv_obj_t* volume_up_label = lv_label_create(volume_up_btn);
    lv_label_set_text(volume_up_label, LV_SYMBOL_VOLUME_MAX "+");
    lv_obj_center(volume_up_label);

    lv_obj_t* volume_down_btn = general_btn_create(talk_page);
    lv_obj_set_size(volume_down_btn, 30, 30);
    lv_obj_align_to(volume_down_btn, volume_up_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_color(volume_down_btn, lv_color_hex(0xEC79EB), 0);
    lv_obj_add_event_cb(volume_down_btn, vol_btn_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(-VOL_STEP));
    /* 按钮上的标签 */
    lv_obj_t* volume_down_label = lv_label_create(volume_down_btn);
    lv_label_set_text(volume_down_label, LV_SYMBOL_VOLUME_MID "-");
    lv_obj_center(volume_down_label);

    lv_obj_t* stop_play_btn = general_btn_create(talk_page);
    lv_obj_set_size(stop_play_btn, 30, 30);
    lv_obj_align_to(stop_play_btn, volume_down_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_bg_color(stop_play_btn, lv_color_hex(0xEC79EB), 0);
    lv_obj_add_event_cb(stop_play_btn, stop_ai_talk_event_cb, LV_EVENT_CLICKED, NULL);
    /* 按钮上的标签 */
    lv_obj_t* stop_play_label = lv_label_create(stop_play_btn);
    lv_label_set_text(stop_play_label, LV_SYMBOL_STOP);
    lv_obj_set_style_text_color(stop_play_label, lv_color_hex(0xF36A77), 0);
    lv_obj_center(stop_play_label);

    lv_obj_t* img_container = lv_obj_create(talk_page);
    lv_obj_set_style_bg_color(img_container, lv_color_hex(0x000000), 0);
    lv_obj_set_size(img_container, scr_act_width(), 140);
    lv_obj_align(img_container, LV_ALIGN_TOP_MID, 0, 35);


    player_img = lv_img_create(img_container);
    lv_obj_align(player_img, LV_ALIGN_CENTER, 0, 10);
    lv_img_set_src(player_img, &game);

    text_label = lv_label_create(talk_page);
    lv_obj_add_style(text_label, &style, 0);
    lv_label_set_text(text_label, "hello,AI\nlvgl");
    //lv_obj_set_width(text_label, lv_pct(100));     // 限制宽度，开启换行
    //lv_obj_set_height(text_label, 90);
    lv_obj_set_size(text_label, scr_act_width(), 90);
    lv_obj_set_style_pad_all(text_label, 0, 0);   // 可选内边距
    lv_obj_set_style_border_width(text_label, 1, 0);
    lv_obj_set_style_border_color(text_label, lv_color_hex(0x10C8B1), 0);
    lv_obj_set_style_bg_color(text_label, lv_color_hex(0x9184EE), 0);
    lv_obj_set_style_bg_opa(text_label, LV_OPA_50, 0);
    lv_obj_set_style_border_opa(text_label, LV_OPA_100, 0);
    lv_obj_align_to(text_label, img_container, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    web_switch = lv_switch_create(talk_page);
    lv_obj_set_size(web_switch, 60, 40);
    lv_obj_align_to(web_switch, text_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_clear_state(web_switch, LV_STATE_CHECKED); // 初始关闭
    // 添加事件回调：用户操作开关时触发
    lv_obj_add_event_cb(web_switch, web_switch_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t* record_btn = general_btn_create(talk_page);
    lv_obj_set_size(record_btn, 80, 40);
    lv_obj_align_to(record_btn, text_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_obj_set_style_bg_color(record_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_add_event_cb(record_btn, record_btn_event_cb, LV_EVENT_CLICKED, NULL);
    /* 按钮上的标签 */
    lv_obj_t* record_label = lv_label_create(record_btn);
    lv_label_set_text(record_label, "START");
    lv_obj_center(record_label);

	lv_obj_t* test_btn = general_btn_create(talk_page);
    lv_obj_set_size(test_btn, 60, 30);
    lv_obj_align_to(test_btn, text_label, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 10);
    lv_obj_set_style_bg_color(test_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_add_event_cb(test_btn, test_btn_event_cb, LV_EVENT_CLICKED, NULL);
    /* 按钮上的标签 */
    lv_obj_t* test_label = lv_label_create(test_btn);
    lv_label_set_text(test_label, "TEST");
    lv_obj_center(test_label);

    if (!s_player_timer)
    {
#ifdef _USE_ESP32
        jpeg_frame_cfg_t cfg =
        {
            .buff_size = 100 * 1024,
        };
        jpeg_frame_config(&cfg);
#endif
        create_volume_panel(talk_page);
        s_player_timer = lv_timer_create(lv_timer_text_cb, 50, NULL);
    }

    return talk_page;
}
