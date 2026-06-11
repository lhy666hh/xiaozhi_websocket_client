#include "lvgl.h"
#include "esp_log.h"
#include <string.h>
#include "sd.h"
#include "mjpeg_frame.h"
#include "img_converters.h"
//文件标题
static lv_obj_t* file_title = NULL;

//文件列表
static lv_obj_t* file_list = NULL;


//文件页面
static lv_obj_t* file_page = NULL;

//播放页面
static lv_obj_t* player_page = NULL;
//播放控件
static lv_obj_t* player_img = NULL;
//返回按键
static lv_obj_t* back_img = NULL;
//暂停按键
static lv_obj_t* pause_img = NULL;

//播放定时器
static lv_timer_t *s_player_timer = NULL;

//停止播放标志
static bool pause_flag = false;

//定时器回调
static void lv_timer_player_cb(struct _lv_timer_t *t);

void lv_list_event_cb(lv_event_t * e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t* obj = lv_event_get_target(e);
	switch(code)
	{
		case LV_EVENT_CLICKED:
		{
			if(s_player_timer)
			{
				return;
			}
			const char*text = lv_list_get_btn_text(file_list,obj);
			if(text)
			{
				if(strstr(text,".mjpeg")||strstr(text,".MJPEG")||strstr(text,".MJP"))
				{
					jpeg_frame_start(text);
					pause_flag = false;
					s_player_timer = lv_timer_create(lv_timer_player_cb,10,NULL);
					lv_scr_load(player_page);
				}
			}

			break;
		}
		default:
			break;
	}
}
static void lv_timer_player_cb(struct _lv_timer_t *t)
{
	static jpeg_frame_data_t frame_data = {0};
	if(frame_data.frame)
	{
		free(frame_data.frame);
		frame_data.frame = NULL;
		frame_data.len = 0;
	}
	if(pause_flag)return;

	jpeg_frame_get_one(&frame_data);

	if(frame_data.len)
	{
		static lv_img_dsc_t img_dsc;
		memset(&img_dsc,0,sizeof(lv_img_dsc_t));
		static uint8_t *rgb565_data = NULL;
		uint16_t width = 0;
		uint16_t height = 0;
		if(rgb565_data)
		{
			free(rgb565_data);
			rgb565_data = NULL;
		}
		if(jpg2rgb565(frame_data.frame,frame_data.len,&rgb565_data,&width,&height,JPG_SCALE_NONE))
		{
			img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
			img_dsc.header.w = width;
			img_dsc.header.h = height;
			img_dsc.data = rgb565_data;
			img_dsc.data_size = (uint32_t)width*(uint32_t)height*2;

			lv_img_set_src(player_img,&img_dsc);
		}
	}
	else{
		pause_flag = false;
		lv_timer_del(s_player_timer);
		s_player_timer = NULL;
	}
}

void lv_player_btn_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t* obj = lv_event_get_target(e);

	if(code == LV_EVENT_CLICKED)
	{
		if(obj == back_img)
		{
			if(s_player_timer)
			{
				lv_timer_del(s_player_timer);
				s_player_timer = NULL;
				jpeg_frame_stop();
			}
			
			lv_scr_load(file_page);
		}
		else if(obj == pause_img)
		{
			pause_flag = !pause_flag;
		}
	}
}

void ui_mjpeg_create(void)
{
	lv_coord_t hor_res = lv_disp_get_hor_res(NULL);
	lv_coord_t ver_res = lv_disp_get_ver_res(NULL);

	file_page = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(file_page,lv_color_black(),0);


	file_title = lv_label_create(file_page);
	lv_label_set_text(file_title,"SD File");
	lv_obj_set_size(file_title,hor_res-60,30);
	lv_obj_align(file_title,LV_ALIGN_TOP_MID,0,10);
	lv_obj_set_style_text_font(file_title,&lv_font_montserrat_24,0);
	lv_obj_set_style_text_color(file_title,lv_color_black(),0);
	lv_obj_set_style_bg_color(file_title,lv_color_white(),0);
	lv_obj_set_style_bg_opa(file_title,LV_OPA_COVER,0);

	file_list = lv_list_create(file_page);
	lv_obj_set_size(file_list,hor_res-60,ver_res-90);
	lv_obj_align(file_list,LV_ALIGN_BOTTOM_MID,0,-40);
	lv_obj_set_style_text_font(file_list,&lv_font_montserrat_24,0);
	lv_obj_set_style_text_color(file_list,lv_color_black(),0);
	lv_obj_set_style_bg_color(file_list,lv_color_white(),0);
	lv_obj_set_style_bg_opa(file_list,LV_OPA_COVER,0);

	//读取sd列表
	const char (*filelist)[256] = NULL;
	int file_cnt = sdcard_filelist(&filelist);
	for(int i = 0;i<file_cnt;i++)
	{
		lv_obj_t* btn = lv_list_add_btn(file_list,NULL,*filelist);
		lv_obj_add_event_cb(btn,lv_list_event_cb,LV_EVENT_CLICKED,NULL);
		filelist++;
	}

	player_page = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(file_page,lv_color_black(),0);

	player_img = lv_img_create(player_page);
	lv_obj_align(player_img,LV_ALIGN_TOP_MID,0,35);

	back_img = lv_imgbtn_create(player_page);
	lv_obj_set_size(back_img,48,48);
	lv_obj_align(back_img,LV_ALIGN_BOTTOM_LEFT,30,-20);
	lv_imgbtn_set_src(back_img,LV_IMGBTN_STATE_RELEASED,NULL,"S:/back.png",NULL);

	lv_obj_add_event_cb(back_img,lv_player_btn_event_cb,LV_EVENT_CLICKED,NULL);

	pause_img = lv_imgbtn_create(player_page);
	lv_obj_set_size(pause_img,48,48);
	lv_obj_align(pause_img,LV_ALIGN_BOTTOM_MID,0,-20);
	lv_imgbtn_set_src(pause_img,LV_IMGBTN_STATE_RELEASED,NULL,"S:/pause.png",NULL);

	lv_obj_add_event_cb(pause_img,lv_player_btn_event_cb,LV_EVENT_CLICKED,NULL);

	jpeg_frame_cfg_t cfg =
	{
		.buff_size = 100*1024,
	};
	jpeg_frame_config(&cfg);
	//显示文件页面
	lv_scr_load(file_page);

}
