/**
 * @file lv_demo_stress.c
 *
 */

 /*********************
  *      INCLUDES
  *********************/


#include "lv_my_demo.h"
#ifdef _WIN32

#else
#include "lvgl.h"
#include "bsp_esp8266_test.h"
#endif





  //清除加载器组件恢复按钮点击(需要传入结构体指针的地址才能正确操作该指针)
void general_del_load_data_t(load_data_t** data)
{
    if (*data)
    {
        if ((*data)->spinner)
        {
            lv_obj_del((*data)->spinner);
            (*data)->spinner = NULL;
        }
        if ((*data)->back_obj)
        {
            lv_obj_del((*data)->back_obj);
            (*data)->back_obj = NULL;
        }
        if ((*data)->timer)
        {
            lv_timer_del((*data)->timer);
            (*data)->timer = NULL;
        }
        //恢复按钮状态（若按钮未删除）
        if ((*data)->btn && lv_obj_is_valid((*data)->btn))
        {
            lv_obj_clear_state((*data)->btn, LV_STATE_DISABLED);
        }

        //删除数据
        lv_mem_free((*data));
        (*data) = NULL;
    }
}

//创建加载器组件恢复按钮点击(需要传入结构体指针的地址才能正确操作该指针)
bool general_create_load_data_t(load_data_t** data, void(*timer_cb)(lv_timer_t* tmier), lv_obj_t* disabled_btn,const char* notice_str,uint32_t x_ms_wait)
{
    if (timer_cb == NULL || (*data) != NULL)return false;

    (*data) = lv_mem_alloc(sizeof(load_data_t));
    if ((*data) == NULL)return false; //内存分配失败
    (*data)->btn = NULL;
    (*data)->spinner = NULL;
    (*data)->back_obj = NULL;
    (*data)->timer = NULL;

    (*data)->timer = lv_timer_create(timer_cb, x_ms_wait, NULL);
    //禁用按钮
    if (disabled_btn && lv_obj_is_valid(disabled_btn))
    {
        lv_obj_add_state(disabled_btn, LV_STATE_DISABLED);
        (*data)->btn = disabled_btn;
    }

    lv_obj_t* obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, 220, 110);
    lv_obj_set_align(obj, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xFCC31F), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_100, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_center(obj);
    (*data)->back_obj = obj;

    //创建加载部件
    lv_obj_t* spinner = lv_spinner_create(obj, 2000, 60);
    lv_obj_set_size(spinner, 50, 50);
    lv_obj_center(spinner);
    (*data)->spinner = spinner;


    lv_obj_t* notice_label = lv_label_create(obj);
    lv_label_set_text(notice_label, notice_str);
    lv_obj_set_style_text_color(notice_label, lv_color_hex(0x9CDCFE),0);
    lv_obj_align(notice_label,LV_ALIGN_BOTTOM_MID,0,0);

    return true;
}




lv_obj_t* main_screen;


/******************************************开机界面配置***********************************************************/
static const char startup_path[] = "A:/SYSTEM/img/start3.bmp";
lv_obj_t* start_father_obj;
lv_obj_t* start_img;
lv_obj_t* start_bar;
lv_timer_t* start_timer;
lv_obj_t* loading_label;

static void cleanup_startup_screen(lv_timer_t* timer);
static void create_main_ui(void);
static void start_time_cb(lv_timer_t* timer)
{
    static int val = 0;
    if (val < 100)
    {
        if (val < 20)val += 5;
        else if (val < 50)val += 3;
        else if (val < 90)val += 2;
        else val++;
        lv_bar_set_value(start_bar, val, LV_ANIM_ON);
        lv_label_set_text_fmt(loading_label, "LOADING...(%d %%)", lv_bar_get_value(start_bar));
    }
    else
    {
        if (start_timer)
        {
            lv_timer_del(start_timer);
            start_timer = NULL;
        }
        lv_label_set_text(loading_label, "finished!(100 %%)");
        //延迟500ms清除开机画面
        lv_timer_create(cleanup_startup_screen, 500, NULL);
    }

}




static void del_start_obj(lv_timer_t* t)
{
    if (start_img)
    {
        lv_obj_del(start_img);
        start_img = NULL;
    }

    if (start_bar)
    {
        lv_obj_del(start_bar);
        start_img = NULL;
    }

    if (loading_label)
    {
        lv_obj_del(loading_label);
        loading_label = NULL;
    }

    //删除整个屏幕
    if (start_father_obj)
    {
        lv_obj_del(start_father_obj);
        start_father_obj = NULL;
    }
    /*进入主页面*/
    create_main_ui();
    /**************/


    if (t)lv_timer_del(t);
}


static void cleanup_startup_screen(lv_timer_t* timer)
{
    //渐隐动画效果
    lv_obj_fade_out(start_father_obj, 300, 0);

    //延迟删除
    lv_timer_create(del_start_obj, 350, NULL);

    if (timer) lv_timer_del(timer);
}


static void lv_start_bar(void)
{
    //开机画面父部件
    start_father_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(start_father_obj, scr_act_width(), scr_act_height());
    lv_obj_center(start_father_obj);

    lv_obj_set_style_bg_color(start_father_obj, lv_color_hex(0x1a1a2e), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(start_father_obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(start_father_obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(start_father_obj, 0, LV_STATE_DEFAULT);
    lv_obj_clear_flag(start_father_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(start_father_obj, 0, 0);//内边距设置为0，可让子部件置顶时不会隔着一段距离

    //logo
    lv_obj_t* title_logo = lv_label_create(start_father_obj);
    lv_obj_set_style_text_color(title_logo, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_logo, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_label_set_text(title_logo, "RemainingLife");
    //lv_obj_set_size(title_logo, scr_act_width(), 30);
    //lv_obj_align(title_logo, LV_ALIGN_TOP_MID, 0, 0);
    //lv_label_set_long_mode(title_logo, LV_LABEL_LONG_DOT);

    lv_obj_center(title_logo);



    //背景图
    start_img = lv_img_create(start_father_obj);
    lv_img_set_src(start_img, startup_path);
    lv_obj_set_align(start_img, LV_ALIGN_CENTER);
    lv_obj_move_background(start_img);



    //进度条创建
    start_bar = lv_bar_create(start_father_obj);
    lv_obj_align_to(start_bar, start_father_obj, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_size(start_bar, scr_act_width(), 15);

    lv_obj_set_style_bg_color(start_bar, lv_color_hex(0xE972EE), LV_PART_MAIN);
    //lv_obj_set_style_opa(start_bar, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(start_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_border_width(start_bar, 0, 0);



    lv_obj_set_style_bg_color(start_bar, lv_color_hex(0xFF8023), LV_PART_INDICATOR);
    //lv_obj_set_style_opa(start_bar, LV_OPA_70, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_color(start_bar, lv_color_hex(0x4361ee), LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(start_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_radius(start_bar, 6, LV_PART_INDICATOR);

    //loading tabel
    loading_label = lv_label_create(start_father_obj);
    lv_obj_set_style_text_color(loading_label, lv_color_hex(0x25E8CF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_label_set_text(loading_label, "LOADING...(0 %%)");
    lv_obj_align_to(loading_label, start_bar, LV_ALIGN_OUT_TOP_MID, 0, 0);




    lv_obj_set_style_anim_time(start_bar, 100, LV_STATE_DEFAULT);
    start_timer = lv_timer_create(start_time_cb, 100, NULL);
}



/**********************************APP应用框图********************************************************/


 /*********************
      *      DEFINES
      *********************/
typedef enum {
    APP_MAIN = 0,
    APP_DEMO_1 = 1,
    APP_DEMO_2 = 2,
}app_id_t;


typedef struct {
    const char* name;
    const void* icon_src;
    app_id_t app_id;
    lv_obj_t* (*create_fun)(void);
}app_info_t;


//平铺视图父部件
static lv_obj_t* tileview;

//当前app屏幕指针容器，方便释放缓冲区
typedef struct {
    app_id_t current_app;
    lv_obj_t* app_screen;
}app_context_t;

static app_context_t app_ctx = { 0 };

//外部app函数
//extern lv_obj_t* create_rgbctl_app_ui(void);
//extern lv_obj_t* music_player_init(void);
//extern lv_obj_t* create_clock_ui(void);
//extern lv_obj_t* create_wifi_ui(void);
//extern lv_obj_t* create_wether_ui(void);
/*app未发布时调用*/
//static void warning_app_is_null_msgbox(void);
static lv_obj_t* app_has_not_released(void)
{
    warning_msgbox("Notice", "Sorry,this app hasn't released yet!");
    return NULL;
}

//LV_IMG_DECLARE(ikun);
//LV_IMG_DECLARE(rgb);
LV_IMG_DECLARE(game);
//LV_IMG_DECLARE(clock);
//LV_IMG_DECLARE(wether);
//LV_IMG_DECLARE(wifi);

//app信息列表初始化
static const app_info_t app_list[] = {
    //{"RGB","A:/SYSTEM/img/rgb.bmp",APP_DEMO_1,create_rgbctl_app_ui},
    //{"Game","A:/SYSTEM/img/game.bmp",APP_DEMO_2,app_has_not_released},
    //{"IKUN","A:/SYSTEM/img/ikun.bmp",APP_DEMO_2,app_has_not_released},

    //{"RGB",&rgb,APP_DEMO_1,create_rgbctl_app_ui},
    {"Game",&game,APP_DEMO_2,app_has_not_released},
    //{"IKUN",&ikun,APP_DEMO_2,music_player_init},
    //{"Clock",&clock,APP_DEMO_2,create_clock_ui},
    //{"Wether",&wether,APP_DEMO_2,create_wether_ui},
    //{"WiFi",&wifi,APP_DEMO_2, create_wifi_ui},

};

static const uint8_t app_count = sizeof(app_list) / sizeof(app_info_t);

static lv_obj_t* msgbox = NULL;

static void msgbox_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = lv_event_get_current_target(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        if (lv_msgbox_get_active_btn(target) == 2)//按钮索引
        {
            if (msgbox && lv_obj_is_valid(msgbox))
            {
                lv_obj_t* win = lv_obj_get_parent(msgbox);
                lv_obj_del(win);
                msgbox = NULL;
            }
        }
    }


}

void warning_msgbox(const char*title_text,const char* message_text)
{
    static const char* btns[] = { " "," ","OK","" };

    /*消息框整体*/
    //msgbox = lv_msgbox_create(NULL, LV_SYMBOL_WARNING "Notice", "Sorry,this app hasn't released yet!", btns, false);
    msgbox = lv_msgbox_create(NULL, title_text, message_text, btns, false);
    lv_obj_set_size(msgbox, scr_act_width() * 2 / 3, scr_act_height() * 2 / 3);
    lv_obj_center(msgbox);
    lv_obj_set_style_border_width(msgbox, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(msgbox, 20, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(msgbox, lv_color_hex(0xa9a9a9), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(msgbox, 18, LV_STATE_DEFAULT);//设置顶部填充
    lv_obj_set_style_pad_left(msgbox, 20, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(msgbox, msgbox_event_cb, LV_EVENT_VALUE_CHANGED, NULL);


    /*消息框标题*/
    lv_obj_t* title = lv_msgbox_get_title(msgbox);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xff0000), LV_STATE_DEFAULT);

    /*消息框主题*/
    lv_obj_t* content = lv_msgbox_get_content(msgbox);
    lv_obj_set_style_text_font(content, &lv_font_montserrat_14, LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(content, lv_color_hex(0x6c6c6c), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(content, 15, LV_STATE_DEFAULT);

    /*消息框按钮*/
    lv_obj_t* btn = lv_msgbox_get_btns(msgbox);
    lv_obj_set_style_bg_opa(btn, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x2271df), LV_PART_ITEMS);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xff0000), LV_PART_ITEMS | LV_STATE_PRESSED);


}


//平铺视图中app按钮回调函数
static void app_btn_event_handle(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn_target = lv_event_get_target(e);
    int app_index = 0;

    switch (code)
    {
    case LV_EVENT_PRESSED:
        //lv_obj_set_style_img_recolor(btn_target, lv_color_hex(0x007ffe), LV_STATE_DEFAULT);
        //lv_obj_set_style_img_recolor_opa(btn_target, 150, LV_STATE_DEFAULT);


        break;
    case LV_EVENT_RELEASED:
        //lv_obj_set_style_img_recolor_opa(btn_target, 0, LV_STATE_DEFAULT);
        break;
    case LV_EVENT_CLICKED:
        app_index = (int)(intptr_t)lv_event_get_user_data(e);
        if (app_index >= 0 && app_index < app_count)
        {

            app_ctx.current_app = app_list[app_index].app_id;
            app_ctx.app_screen = app_list[app_index].create_fun();
            if (app_ctx.app_screen != NULL)
            {
                //lv_obj_add_flag(tileview, LV_OBJ_FLAG_HIDDEN);
                lv_obj_del(tileview);
                tileview = NULL;
            }
            else
            {
                //lv_obj_clear_flag(msgbox, LV_OBJ_FLAG_HIDDEN);
                LV_LOG_USER("sorry,app has not released yet!");
            }
        }
        break;
    default:
        break;

    }



}

/**
 * @brief app函数退出键调用回调，由外部APP调用
 * @param e
 */
void exit_app_cb(lv_event_t* e)
{
    if (app_ctx.app_screen)
    {
        lv_obj_del(app_ctx.app_screen);
        app_ctx.app_screen = NULL;
        app_ctx.current_app = APP_MAIN;
    }
    //lv_obj_clear_flag(tileview, LV_OBJ_FLAG_HIDDEN);
    create_main_ui();

}

lv_obj_t* time_label = NULL;
static lv_timer_t* sys_timer = NULL;
static void sys_timer_cb(lv_timer_t* timer);
static void goto_talkpage_event_cb(lv_event_t* e);
static lv_obj_t* usr_create_app_icon_obj(lv_obj_t* parent, const app_info_t* app_data, void* user_data);
// 创建主界面（开机完成后显示）
static void create_main_ui(void)
{
    // 清除之前的屏幕
    //lv_obj_clean(lv_scr_act());


    app_ctx.current_app = APP_MAIN;
    app_ctx.app_screen = NULL;

    // 创建主界面背景
    if (main_screen == NULL)
    {
    //    main_screen = lv_obj_create(lv_scr_act());
    //    lv_obj_set_size(main_screen, scr_act_width(), scr_act_height());
        main_screen = lv_obj_create(NULL);   // 不要传 lv_scr_act()
        lv_obj_set_size(main_screen, scr_act_width(), scr_act_height());
        lv_obj_center(main_screen);
        lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x000000), 0);
        //lv_obj_set_style_bg_opa(main_screen, LV_OPA_COVER, 0);
        //lv_obj_set_style_bg_grad_dir(main_screen, LV_GRAD_DIR_HOR, LV_STATE_DEFAULT);
        //lv_obj_set_style_bg_grad_color(main_screen, lv_color_hex(0x888888), LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(main_screen, 0, 0);
        lv_obj_set_style_radius(main_screen, 0, 0);
        lv_obj_set_style_pad_all(main_screen, 0, 0);//内边距设置为0，可让子部件置顶时不会隔着一段距离

        lv_obj_set_style_bg_img_src(main_screen, &game, 0);
        lv_obj_set_style_bg_img_opa(main_screen, LV_OPA_50, 0);
        lv_obj_set_style_bg_img_tiled(main_screen, true, 0);
        //lv_obj_t* img = lv_img_create(main_screen);
        //lv_img_set_src(img, &wifi);
        //lv_obj_center(img);
        //lv_img_set_zoom(img, 512);

         // ---- 添加背景图片 ----
        //LV_IMG_DECLARE(wifi);
        //lv_obj_t* bg_img = lv_img_create(main_screen);
        //lv_img_set_src(bg_img, &wifi);
        //lv_obj_set_size(bg_img, scr_act_width(), scr_act_height());
        //lv_obj_set_align(bg_img, LV_ALIGN_TOP_LEFT);
        //lv_obj_move_background(bg_img);
        //// 如果需要调整透明度：
        //lv_obj_set_style_opa(bg_img, LV_OPA_80, 0); // 80% 不透明度
        

    }


    if (tileview == NULL)tileview = lv_tileview_create(main_screen);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_0, 0);

    lv_obj_t* title_1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* title_2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_RIGHT | LV_DIR_LEFT);
    lv_obj_t* title_3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

    //lv_obj_t * label1 = lv_label_create(title_1);
    //lv_label_set_text(label1,"Page1");
    //lv_obj_set_style_text_font(label1,&lv_font_montserrat_20,LV_STATE_DEFAULT);
    //lv_obj_center(label1);


    lv_obj_t* label2 = lv_label_create(title_2);
    lv_label_set_text(label2, "Page2");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(label2);

    lv_obj_t* label3 = lv_label_create(title_3);
    lv_label_set_text(label3, "Page3");
    lv_obj_set_style_text_font(label3, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(label3);

    lv_obj_remove_style(tileview, NULL, LV_PART_SCROLLBAR);//移出平铺视图滚动条



    uint16_t app_distance = (scr_act_width() - 20) / 3;
    /********************************创建APP图标按钮***************************************************************/
    for (int i = 0;i < app_count;i++)
    {
        //lv_obj_t* app_btn = usr_create_img_btn(title_1, img_w_h, app_list[i].icon_src);
        ////添加按键事件
        //lv_obj_add_event_cb(app_btn, app_btn_event_handle, LV_EVENT_CLICKED, (void*)(intptr_t)0);
        //lv_obj_align_to(app_btn, sys_info_container, LV_ALIGN_OUT_BOTTOM_LEFT, i*(img_w_h+10), 10);
        ////添加app名称标签
        //lv_obj_t* app_name_label = lv_label_create(title_1);
        //lv_label_set_text(app_name_label, app_list[i].name);
        //lv_obj_set_style_text_color(app_name_label, lv_color_hex(0x000088), LV_STATE_DEFAULT);
        //lv_obj_align_to(app_name_label, app_btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
        if (i < 6)
        {
            lv_obj_t* app_obj = usr_create_app_icon_obj(title_1, &app_list[i], (void*)(intptr_t)i);
            lv_obj_align(app_obj, LV_ALIGN_OUT_BOTTOM_LEFT, (lv_coord_t)(10 + (i % 3) * app_distance), (lv_coord_t)(40 + (i / 3) * 80));
        }
        //else if (i < 12)
        //{
        //    lv_obj_t* app_obj = usr_create_app_icon_obj(title_2, &app_list[i], (void*)(intptr_t)i);
        //    lv_obj_align(app_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 20 + (i % 3) * 70, 40 + ((i-6)/ 3) * 80);
        //}
        //else if (i < 18)
        //{
        //    lv_obj_t* app_obj = usr_create_app_icon_obj(title_3, &app_list[i], (void*)(intptr_t)i);
        //    lv_obj_align(app_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 20 + (i % 3) * 70, 40 + ((i - 12) / 3) * 80);
        //}


        lv_obj_t* sys_info_container = lv_obj_create(main_screen);
        lv_obj_set_size(sys_info_container, scr_act_width(), 25);
        lv_obj_set_style_bg_color(sys_info_container, lv_color_hex(0xF3E8EC), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(sys_info_container, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_dir(sys_info_container, LV_GRAD_DIR_VER, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_color(sys_info_container, lv_color_hex(0x0095B7), LV_STATE_DEFAULT);
        lv_obj_set_style_radius(sys_info_container, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(sys_info_container, 0, 0);
        lv_obj_align(sys_info_container, LV_ALIGN_TOP_MID, 0, 0);



        time_label = lv_label_create(sys_info_container);
        lv_label_set_text(time_label, "00:00:00");
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t* talk_btn = general_btn_create(sys_info_container);
        lv_obj_set_size(talk_btn, 60, 20);
        lv_obj_align(talk_btn, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(talk_btn, lv_color_hex(0x10C8B1), 0); 
        lv_obj_add_event_cb(talk_btn, goto_talkpage_event_cb, LV_EVENT_CLICKED, NULL);
        /* 按钮上的标签 */
        lv_obj_t* btn_label = lv_label_create(talk_btn);
        lv_label_set_text(btn_label, "Talk");
        lv_obj_center(btn_label);


        lv_obj_t* state_symbol_label = lv_label_create(sys_info_container);
        lv_label_set_text(state_symbol_label, LV_SYMBOL_WIFI "   " LV_SYMBOL_BATTERY_3);
        lv_obj_set_style_text_font(state_symbol_label, &lv_font_montserrat_14, LV_STATE_DEFAULT);
        lv_obj_align(state_symbol_label, LV_ALIGN_RIGHT_MID, 0, 0);

    }


    


    /* 创建播放定时器（模拟播放进度） */
    if (sys_timer == NULL)sys_timer = lv_timer_create(sys_timer_cb, 1000, NULL);
    //lv_timer_pause(sys_timer);

}

//static void back_mainpage_event_cb(lv_event_t* e)
//{
//    lv_obj_t* btn = lv_event_get_target(e);
//    lv_event_code_t code = lv_event_get_code(e);
//    if (code == LV_EVENT_CLICKED) {
//
//        lv_scr_load_anim(main_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 300, 0, true);
//    }
//
//}
extern lv_obj_t* create_talk_page(void);
/* 修改扫描按钮的回调，为每个列表项添加点击事件 */
static void goto_talkpage_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        LV_LOG_USER("Go to talk page");

        //lv_obj_t* new_scr = lv_obj_create(NULL);
        //lv_obj_set_style_bg_img_src(new_scr, &game, 0);
        //lv_obj_set_style_bg_img_opa(new_scr, LV_OPA_50, 0);
        //lv_obj_set_style_bg_img_tiled(new_scr, true, 0);

        //lv_obj_t* back_btn = general_btn_create(new_scr);
        //lv_obj_set_size(back_btn, 80, 40);
        //lv_obj_align(back_btn, LV_ALIGN_CENTER, 0, 0);
        //lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x10C8B1), 0);
        //lv_obj_add_event_cb(back_btn, back_mainpage_event_cb, LV_EVENT_CLICKED, NULL);
        ///* 按钮上的标签 */
        //lv_obj_t* btn_label = lv_label_create(back_btn);
        //lv_label_set_text(btn_label, "BACK");
        //lv_obj_center(btn_label);
        lv_obj_t* new_scr = create_talk_page();

        lv_scr_load_anim(new_scr, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 300, 0, false);


    }
}

static void sys_timer_cb(lv_timer_t* timer)
{
    LV_UNUSED(timer);


#ifdef _WIN32
    static uint32_t time_cnt = 0;
    if (time_cnt++ >= 86400)time_cnt = 0;
    lv_label_set_text_fmt(time_label, "%02d:%02d:%02d", time_cnt / 3600, (time_cnt / 60) % 60, time_cnt % 60);

#else
    RTC_TimeTypeDef RTC_TimeStruct;
    RTC_DateTypeDef RTC_DateStruct;
    if (ESP8266_GET_TIME(&RTC_TimeStruct, &RTC_DateStruct) == 0)
    {
        lv_label_set_text_fmt(time_label, "   %02d:%02d:%02d", RTC_TimeStruct.RTC_Hours, RTC_TimeStruct.RTC_Minutes, RTC_TimeStruct.RTC_Seconds);
    }

    //RTC_GetDate(RTC_Format_BIN, &RTC_DateStruct);
    //sprintf((char*)tbuf, "Date:20%02d-%02d-%02d", RTC_DateStruct.RTC_Year, RTC_DateStruct.RTC_Month, RTC_DateStruct.RTC_Date);
    //printf("\t %s \r\n", tbuf);
    //sprintf((char*)tbuf, "Week:%d", RTC_DateStruct.RTC_WeekDay);
    //printf("\t %s \r\n", tbuf);

#endif


}


#define APP_BTN_W_H    60
static lv_style_t style_pressed;
static lv_style_t style_main;
static lv_style_t obj_style;
static lv_style_t label_style;


lv_obj_t* general_btn_create(lv_obj_t* father_obj)
{
    /*创建按钮样式对象*/
    static bool init_btn_flag = false;
    static lv_style_t general_btn_style;
    if (init_btn_flag != true)
    {
        init_btn_flag = true;

        lv_style_init(&general_btn_style);
        lv_style_set_bg_color(&general_btn_style, lv_color_hex(0xFAFAFA));  /* 红色背景 */
        lv_style_set_bg_opa(&general_btn_style, LV_OPA_COVER);              /* 不透明度100% */
        lv_style_set_radius(&general_btn_style, 10);                        /* 圆角半径 */
        lv_style_set_pad_all(&general_btn_style, 10);                       /* 内边距 */
        lv_style_set_bg_grad_dir(&general_btn_style, LV_GRAD_DIR_VER);
        lv_style_set_bg_grad_color(&general_btn_style, lv_color_hex(0xC5C5C5));
    }
    lv_obj_t* btn = lv_btn_create(father_obj);
    if (btn != NULL)lv_obj_add_style(btn, &general_btn_style, 0);

    return btn;
}


/**
 * @brief 对本地样式进行初始化（APP图片按钮样式）
 * @param
 */
static void usr_img_btn_style_init(void)
{
    static bool init_style_flag = false;
    if (init_style_flag != true)
    {
        init_style_flag = true;
        //本地样式
        lv_style_init(&obj_style);
        lv_style_set_bg_color(&obj_style, lv_color_hex(0xffffff));
        lv_style_set_bg_opa(&obj_style, LV_OPA_0);
        lv_style_set_radius(&obj_style, 0);
        lv_style_set_border_width(&obj_style, 0);
        lv_style_set_pad_all(&obj_style, 0);

        lv_style_init(&label_style);
        lv_style_set_text_font(&label_style, &lv_font_montserrat_14);
        lv_style_set_text_color(&label_style, lv_color_hex(0xDCDCAA));



        //按钮默认样式
        lv_style_init(&style_main);
        lv_style_set_bg_color(&style_main, lv_color_hex(0x3498db));
        lv_style_set_bg_opa(&style_main, LV_OPA_50);
        lv_style_set_shadow_width(&style_main, 5);
        lv_style_set_shadow_opa(&style_main, LV_OPA_30);
        lv_style_set_shadow_ofs_x(&style_main, 2);
        lv_style_set_shadow_ofs_y(&style_main, 2);
        lv_style_set_radius(&style_main, APP_BTN_W_H / 2);
        lv_style_set_clip_corner(&style_main, true);//开启裁剪

        //按钮按下样式
        lv_style_init(&style_pressed);
        lv_style_set_radius(&style_pressed, APP_BTN_W_H / 2 - 10);
        lv_style_set_bg_color(&style_pressed, lv_color_hex(0x9CDCFE));
        lv_style_set_shadow_width(&style_pressed, 10);


    }

}

/**
 * @brief     创建一个用户APP图标按钮，其中图片可随按钮形状变化
 * @param parent    父对象
 * @param img_w_h   APP图像高宽（要求正方形
 * @param img_src   图片源
 * @return
 */
static lv_obj_t* usr_create_img_btn(lv_obj_t* parent, int32_t img_w_h, const char* img_src)
{
    lv_obj_t* app_btn = lv_btn_create(parent);
    lv_obj_set_size(app_btn, img_w_h, img_w_h);

    lv_obj_t* btn_img = lv_img_create(app_btn);
    lv_img_set_src(btn_img, img_src);
    lv_obj_align(btn_img, LV_ALIGN_CENTER, 0, 0);

    //添加样式到按钮
    lv_obj_add_style(app_btn, &style_main, LV_STATE_DEFAULT);
    lv_obj_add_style(app_btn, &style_pressed, LV_STATE_PRESSED);



    return app_btn;
}





static lv_obj_t* usr_create_app_icon_obj(lv_obj_t* parent, const app_info_t* app_data, void* user_data)
{

    if (app_data == NULL)return NULL;
    usr_img_btn_style_init();
    ////本地样式初始化
    usr_img_btn_style_init();



    lv_obj_t* app_icon_obj = lv_obj_create(parent);
    //添加样式
    lv_obj_add_style(app_icon_obj, &obj_style, LV_STATE_DEFAULT);
    //设置大小
    lv_obj_set_size(app_icon_obj, 80, 80);






    //添加用户图标按钮
    lv_obj_t* app_btn = usr_create_img_btn(app_icon_obj, APP_BTN_W_H, app_data->icon_src);
    lv_obj_align(app_btn, LV_ALIGN_TOP_MID, 0, 0);
    //添加按键事件
    lv_obj_add_event_cb(app_btn, app_btn_event_handle, LV_EVENT_CLICKED, user_data);

    //添加app名称标签
    lv_obj_t* app_name_label = lv_label_create(app_icon_obj);
    //添加标签样式
    lv_obj_add_style(app_name_label, &label_style, LV_STATE_DEFAULT);

    lv_label_set_text(app_name_label, app_data->name);
    lv_obj_align(app_name_label, LV_ALIGN_BOTTOM_MID, 0, 0);


    return app_icon_obj;


}






void lv_gui_start(void)
{
    //lv_start_bar();
    create_main_ui();
    lv_scr_load(main_screen);
}
