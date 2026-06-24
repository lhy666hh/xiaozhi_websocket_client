/**
 * @file lv_demo_stress.h
 *
 */

#ifndef LV_MY_DEMO_H
#define LV_MY_DEMO_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
//#include "../lv_demos.h"
#include "lvgl.h"
#define _WIN32
#define _USE_ESP32
#define scr_act_width()    lv_obj_get_width(lv_scr_act())
#define scr_act_height()    lv_obj_get_height(lv_scr_act())
/*********************
 *      DEFINES
 *********************/



/**********************
 *      TYPEDEFS
 **********************/
    typedef struct {
        lv_obj_t* btn;//按钮对象
        lv_obj_t* spinner;//加载动画对象
        lv_obj_t* back_obj;
        lv_timer_t* timer;//定时器对象

    }load_data_t;
/**********************
 * GLOBAL PROTOTYPES
 **********************/
void lv_gui_start(void);

//外部函数接口
extern lv_obj_t* general_btn_create(lv_obj_t* father_obj);
extern lv_obj_t* main_screen;
extern void exit_app_cb(lv_event_t* e);

//清除加载器组件恢复按钮点击(需要传入结构体指针的地址才能正确操作该指针)
void general_del_load_data_t(load_data_t** data);

//创建加载器组件恢复按钮点击(需要传入结构体指针的地址才能正确操作该指针)
bool general_create_load_data_t(load_data_t** data, void(*timer_cb)(lv_timer_t* tmier), lv_obj_t* disabled_btn, const char* notice_str, uint32_t x_ms_wait);
/**
 * Check if stress demo has finished one round.
 */
void warning_msgbox(const char* title_text, const char* message_text);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_DEMO_STRESS_H*/
