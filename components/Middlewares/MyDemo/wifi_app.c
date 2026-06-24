/* 定义文件路径 - 根据实际情况修改 */
#include "lv_my_demo.h"
#ifdef _WIN32
#include "lv_my_demo.h"

#include <string.h>
#include <stdlib.h>


#include <stdio.h>
#include <string.h>
#define MAX_SSID_LEN 32

#elif defined(ESP32)
#define MUSIC_DIR "/sdcard/Music"
#elif defined(__linux__)
#define MUSIC_DIR "/home/user/Music"
#else
#include "lvgl.h"
#include "bsp_esp8266_test.h"
#include "mymalloc.h"
#endif



typedef struct {
    lv_obj_t* label;//标签对象
    char ssid_copy[MAX_SSID_LEN];//ssid拷贝指针
    bool connecting;//连接状态
}wifi_info_t;

wifi_info_t* cnc_wifi_info = NULL;




/* 静态变量，用于在回调中访问列表对象 */
static lv_obj_t* wifi_list = NULL;
static load_data_t* data = NULL;//动画加载定时器部件
static load_data_t* joinAp_data = NULL;//动画加载定时器部件
static bool wifi_open_sta = false;
static char* ssid_copy = NULL;//wifi名拷贝

/* 事件回调函数声明 */
static void wifi_switch_event_cb(lv_event_t* e);
static void scan_btn_event_cb(lv_event_t* e);

/* 函数声明 */
static void wifi_item_click_cb(lv_event_t* e);          /* 列表项点击回调 */
static void password_dialog_event_cb(lv_event_t* e);   /* 对话框按钮事件 */


/* 退出按钮回调：删除主容器（模拟返回上一级） */
static void exit_btn_event_cb(lv_event_t* e) {
    //lv_obj_t* btn = lv_event_get_target(e);
    //lv_obj_t* main_card = lv_obj_get_parent(btn);   // 获取按钮所在的主卡片
    //lv_obj_del(main_card);                            // 删除卡片（实际应用中可改为隐藏或切换页面）
    general_del_load_data_t(&data);
    general_del_load_data_t(&joinAp_data);
    if (ssid_copy != NULL)
    {
        lv_mem_free(ssid_copy);
        ssid_copy = NULL;
    }
    //调用通用退出函数
    exit_app_cb(e);//清空APP内所有部件

    wifi_list = NULL;//部件内存释放后需要将全局指针置空

}

lv_obj_t* find_label_by_text(lv_obj_t* list, const char* target)
{
    if (!list || !target) return NULL;

    uint32_t child_cnt = lv_obj_get_child_cnt(list);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* item_container = lv_obj_get_child(list, i);

        // 方法1：直接遍历所有子对象，查找标签
        lv_obj_t* label = NULL;
        uint32_t total_child_cnt = lv_obj_get_child_cnt(item_container);

        for (uint32_t j = 0; j < total_child_cnt; j++) {
            lv_obj_t* child = lv_obj_get_child(item_container, j);

            // 检查当前对象是否为标签
            if (lv_obj_check_type(child, &lv_label_class)) {
                label = child;
            }
            // 如果不是标签，检查其子对象中是否有标签
            else if (lv_obj_get_child_cnt(child) > 0) {
                uint32_t sub_cnt = lv_obj_get_child_cnt(child);
                for (uint32_t k = 0; k < sub_cnt; k++) {
                    lv_obj_t* sub = lv_obj_get_child(child, k);
                    if (lv_obj_check_type(sub, &lv_label_class)) {
                        label = sub;
                        break;
                    }
                }
            }

            if (label) {
                const char* text = lv_label_get_text(label);
                if (text && strstr(text, target) != NULL) {
                    return label;
                }
                label = NULL; // 重置，继续查找
            }
        }
    }

    return NULL;
}

/*动画定时器回调函数*/
static void loading_timer_cb(lv_timer_t* timer)
{
    if (wifi_list != NULL)
    {
        char wifi_text[50] = "\0";
#ifdef _WIN32
        general_del_load_data_t(&data);
        /* 添加模拟的 WiFi 网络，并为每个按钮添加点击回调 */
        sprintf(wifi_text, "HomeWiFi");
        lv_obj_t* btn1 = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, wifi_text);
        lv_obj_add_event_cb(btn1, wifi_item_click_cb, LV_EVENT_CLICKED, NULL);

        sprintf(wifi_text, "OfficeNet");
        lv_obj_t* btn2 = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, wifi_text);
        lv_obj_add_event_cb(btn2, wifi_item_click_cb, LV_EVENT_CLICKED, NULL);

        sprintf(wifi_text, "CoffeeShop \t-60");
        lv_obj_t* btn3 = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, wifi_text);
        lv_obj_add_event_cb(btn3, wifi_item_click_cb, LV_EVENT_CLICKED, NULL);

        sprintf(wifi_text, "EncryptedNet \t-20");
        lv_obj_t* btn4 = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI " ", wifi_text);
        lv_obj_add_event_cb(btn4, wifi_item_click_cb, LV_EVENT_CLICKED, NULL);


        if (cnc_wifi_info != NULL && cnc_wifi_info->connecting == true && cnc_wifi_info->ssid_copy[0] != '\0')
        {
            lv_obj_t* label = find_label_by_text(wifi_list, cnc_wifi_info->ssid_copy);
            if (label)
            {
                lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
                cnc_wifi_info->label = label;
            }
        }

#else




        if (ESP8266_GET_STATE() == WIFI_SCAN_AP)
        {
            general_del_load_data_t(&data);
            wifi_ap_t* temp_list = NULL;
            uint8_t ap_num = ESP8266_GET_APINFO(&temp_list);

            for (int i = 0;i < ap_num;i++)
            {
                /* 添加模拟的 WiFi 网络，并为每个按钮添加点击回调 */
                sprintf(wifi_text, "%s \t-%d", temp_list[i].ssid, temp_list[i].rssi);
                lv_obj_t* btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, wifi_text);
                lv_obj_add_event_cb(btn, wifi_item_click_cb, LV_EVENT_CLICKED, NULL);

                printf("\t%d:%s -- %d\r\n", i, temp_list[i].ssid, temp_list[i].rssi);
            }

            if (temp_list != NULL)
            {
                my_free(temp_list);
                temp_list = NULL;
            }
            if (ESP8266_Get_LinkSTATE(false) == WIFI_CLOSE)
            {
                if (cnc_wifi_info)cnc_wifi_info->connecting = false;
            }
            if (cnc_wifi_info && cnc_wifi_info->connecting == true && cnc_wifi_info->ssid_copy[0] != '\0')
            {
                lv_obj_t* label = find_label_by_text(wifi_list, cnc_wifi_info->ssid_copy);
                if (label)
                {
                    lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
                    cnc_wifi_info->label = label;
                }
            }
        }
#endif
    }
}



/*动画定时器回调函数*/
static void joinAP_timer_cb(lv_timer_t* timer)
{
    if (wifi_list != NULL)
    {
#ifdef _WIN32
        general_del_load_data_t(&joinAp_data);
        /* 添加模拟的 WiFi 网络，并为每个按钮添加点击回调 */
        lv_obj_t* label = find_label_by_text(wifi_list, ssid_copy);

        if (label)
        {
            lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
            LV_LOG_USER("%s :WiF Connect oK!", ssid_copy);
            if (cnc_wifi_info != NULL)
            {
                cnc_wifi_info->label = label;
                cnc_wifi_info->connecting = true;
                memcpy(cnc_wifi_info->ssid_copy, ssid_copy, MAX_SSID_LEN);
                //LV_LOG_USER("%s-------", cnc_wifi_info->ssid_copy);
                //sprintf(cnc_wifi_info->ssid_copy, "%s", ssid_copy);
            }
        }
        else
        {
            LV_LOG_USER("%s :WiF Not Found!", ssid_copy);
        }
#else

        WIFI_STATE esp_state = ESP8266_GET_STATE();
        if (esp_state == WIFI_JOIN_AP || esp_state == WIFI_JOIN_AP_FAIL)
        {
            general_del_load_data_t(&joinAp_data);
            /* 添加模拟的 WiFi 网络，并为每个按钮添加点击回调 */
            lv_obj_t* label = find_label_by_text(wifi_list, ssid_copy);
            if (label)
            {
                if (esp_state == WIFI_JOIN_AP)
                {
                    lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), LV_STATE_DEFAULT);
                    LV_LOG_USER("%s :WiF Connect oK!", ssid_copy);
                    if (cnc_wifi_info != NULL)
                    {
                        cnc_wifi_info->label = label;
                        cnc_wifi_info->connecting = true;
                        memcpy(cnc_wifi_info->ssid_copy, ssid_copy, MAX_SSID_LEN);
                        //LV_LOG_USER("%s-------", cnc_wifi_info->ssid_copy);
                        //sprintf(cnc_wifi_info->ssid_copy, "%s", ssid_copy);
                        //ESP8266_RequestServer(WIFI_GET_LINK_STATUS);
                    }
                }
                else
                {
                    lv_obj_set_style_text_color(label, lv_color_hex(0xFF0000), LV_STATE_DEFAULT);
                    LV_LOG_USER("%s :WiF Connect Fail!", ssid_copy);
                }
            }
            else
            {
                LV_LOG_USER("%s :WiF Not Found!", ssid_copy);
            }
        }
#endif
    }

}


/**
 * @brief 创建 WiFi 管理界面
 * @param parent 父对象，通常是当前活动屏幕 (lv_scr_act())
 */
lv_obj_t* create_wifi_ui(void)
{

    lv_obj_t* scr = lv_obj_create(main_screen);

    if (scr == NULL)return NULL;
    lv_obj_set_size(scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_align(scr, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x9184EE), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_100, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    /* 1. 创建 WiFi 总开关 */
    lv_obj_t* sw = lv_switch_create(scr);
    lv_obj_set_pos(sw, 10, 10);                         /* 设置位置 (x, y) */
    lv_obj_add_event_cb(sw, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (wifi_open_sta)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }

    /* 开关旁边的文字标签 */
    lv_obj_t* sw_label = lv_label_create(scr);
    lv_label_set_text(sw_label, "WiFi");
    lv_obj_align_to(sw_label, sw, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

    /* 2. 创建“扫描”按钮 */
    lv_obj_t* scan_btn = general_btn_create(scr);
    lv_obj_set_pos(scan_btn, 130, 10);
    lv_obj_add_event_cb(scan_btn, scan_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_color(scan_btn, lv_color_hex(0x83BEEB), 0); // 红色

    /* 按钮上的标签 */
    lv_obj_t* btn_label = lv_label_create(scan_btn);
    lv_label_set_text(btn_label, "Scan");
    lv_obj_center(btn_label);


    /* 退出按钮 */
    lv_obj_t* exit_btn = general_btn_create(scr);
    lv_obj_set_size(exit_btn, 70, 36);
    lv_obj_set_pos(exit_btn, 190, 10);
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(0xE74C3C), 0); // 红色
    lv_obj_set_style_radius(exit_btn, 8, 0);
    lv_obj_add_event_cb(exit_btn, exit_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* exit_label = lv_label_create(exit_btn);
    lv_label_set_text(exit_label, "Exit");
    lv_obj_center(exit_label);

    /* 3. 创建 WiFi 列表 */
    wifi_list = lv_list_create(scr);
    lv_obj_set_pos(wifi_list, 10, 60);
    lv_obj_set_size(wifi_list, 280, 200);                /* 宽度280，高度200，可根据屏幕调整 */

    /* 可选：预置几条演示数据 */
 /*   lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, "HomeWiFi");
    lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, "OfficeNet");*/


    return scr;
}

/* 开关事件回调 */
static void wifi_switch_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* sw = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (is_on) {
            LV_LOG_USER("WiFi turned ON");
            wifi_open_sta = true;
            if (cnc_wifi_info == NULL)
            {
                cnc_wifi_info = (wifi_info_t*)lv_mem_alloc(sizeof(wifi_info_t));
                if (cnc_wifi_info != NULL)
                {
                    cnc_wifi_info->label = NULL;
                    cnc_wifi_info->connecting = false;
                    cnc_wifi_info->ssid_copy[0] = '\0';
                }
            }
            /* 可在此处执行打开 WiFi 硬件的操作 */
#ifdef _WIN32
#else
            ESP8266_RequestServer(WIFI_POWER_ON);
#endif
        }
        else {
            wifi_open_sta = false;
            LV_LOG_USER("WiFi turned OFF");
            /* 关闭 WiFi 时通常清空列表 */
            lv_obj_clean(wifi_list);
            if (cnc_wifi_info)
            {
                lv_mem_free(cnc_wifi_info);
                cnc_wifi_info = NULL;
            }
            /* 可在此处执行关闭 WiFi 硬件的操作 */
#ifdef _WIN32
#else
            ESP8266_RequestServer(WIFI_SLEEP);
#endif
        }
    }
}







/**
 * @brief 显示密码输入对话框
 * @param ssid 要连接的 WiFi 名称
 */
static void show_password_dialog(const char* ssid)
{
    /* 获取当前活动屏幕作为父对象 */
    lv_obj_t* parent = lv_scr_act();

    /* 创建一个简单的遮罩层（可选），这里直接创建对话框 */
    lv_obj_t* dialog = lv_obj_create(parent);
    lv_obj_set_size(dialog, 320, 240);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_radius(dialog, 8, 0);
    lv_obj_set_style_pad_all(dialog, 10, 0);

    /* 标题：显示“连接 [SSID]” */
    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text_fmt(title, "Connect to %s", ssid);
    lv_obj_set_width(title, lv_pct(100));
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    /* 密码输入框 */
    lv_obj_t* pw_ta = lv_textarea_create(dialog);
    lv_obj_set_size(pw_ta, lv_pct(60), 35);
    lv_obj_align(pw_ta, LV_ALIGN_TOP_MID, 0, 16);
    lv_textarea_set_placeholder_text(pw_ta, "Enter password");
    lv_textarea_set_password_mode(pw_ta, true);         /* 密码模式，隐藏输入内容 */
    lv_obj_add_state(pw_ta, LV_STATE_FOCUSED);          /* 自动获取焦点 */

    /* 按钮容器（水平排列） */
    lv_obj_t* btn_container = lv_obj_create(dialog);
    lv_obj_set_size(btn_container, lv_pct(100), 30);
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(btn_container, 0, 0);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(btn_container, 0, 0);


    /* 定义并创建键盘 */
    lv_obj_t* keyboard = lv_keyboard_create(dialog);
    lv_obj_set_size(keyboard, 270, 135);
    lv_obj_align_to(keyboard, btn_container, LV_ALIGN_OUT_TOP_MID, 10, 1);

    /* 为键盘指定一个文本区域 */
    lv_keyboard_set_textarea(keyboard, pw_ta);

    /* 连接按钮 */
    lv_obj_t* connect_btn = lv_btn_create(btn_container);
    lv_obj_set_size(connect_btn, 80, 30);
    lv_obj_t* conn_label = lv_label_create(connect_btn);
    lv_label_set_text(conn_label, "Connect");
    lv_obj_center(conn_label);

    /* 取消按钮 */
    lv_obj_t* cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 80, 30);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    /* 将对话框、密码输入框、SSID 等信息通过用户数据或事件参数传递给回调
       这里我们为两个按钮添加事件回调，并将对话框和 SSID 打包为结构体或分别存储 */
       /* 更简单的方法：使用 lv_obj_set_user_data 将 SSID 字符串和密码框指针附加到对话框上，
          然后在连接按钮回调中获取。但按钮是对话框的子对象，我们可以从按钮的父对象找到对话框。 */
    lv_obj_set_user_data(dialog, (void*)ssid);      /* 将 SSID 存入对话框用户数据 */
    lv_obj_set_user_data(pw_ta, dialog);              /* 便于从密码框找到对话框（可选） */

    /* 为按钮添加事件回调，使用同一个回调函数，通过事件目标区分按钮 */
    lv_obj_add_event_cb(connect_btn, password_dialog_event_cb, LV_EVENT_CLICKED, dialog);
    lv_obj_add_event_cb(cancel_btn, password_dialog_event_cb, LV_EVENT_CLICKED, dialog);

    /* 注意：ssid 字符串必须保持有效，直到对话框关闭。这里假设 ssid 是静态或动态分配的，
       在实际应用中可能需要复制字符串或确保其生命周期。本例中 ssid 来自列表项的标签文本，
       该文本可能随列表更新而改变，因此建议动态复制 ssid。 */
}


/**
 * @brief 从标签字符串中提取WiFi名称（不包含强度信息）
 * @param full_text 完整的标签文本（如 "MyWiFi ▃▃▃"）
 * @param name_buf  用于存储WiFi名称的缓冲区
 * @param buf_size  缓冲区大小
 * @return true: 提取成功; false: 提取失败
 */
bool extract_wifi_name(const char* full_text, char* name_buf, size_t buf_size)
{
    if (!full_text || !name_buf || buf_size == 0) return false;

    // 查找分隔符（这里以空格为例）
    const char* separator = strpbrk(full_text, " \t");
    size_t name_len;

    if (separator) {
        // 有分隔符，截取分隔符前的部分
        name_len = separator - full_text;
    }
    else {
        // 没有分隔符，整个字符串就是名称
        name_len = strlen(full_text);
    }

    // 确保不超出缓冲区大小
    if (name_len >= buf_size) {
        name_len = buf_size - 1;
    }

    // 复制WiFi名称
    strncpy(name_buf, full_text, name_len);
    name_buf[name_len] = '\0';

    return true;
}

/* 密码输入对话框的事件回调 */
static void password_dialog_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);
    lv_obj_t* dialog = lv_event_get_user_data(e);   /* 我们传入的对话框指针 */

    if (code == LV_EVENT_CLICKED) {
        /* 判断点击的是哪个按钮：通过按钮上的文本来区分，或者通过对象指针比较 */
        lv_obj_t* btn_label = lv_obj_get_child(btn, 0);
        if (btn_label != 0 && lv_obj_has_class(btn_label, &lv_label_class))
        {
            const char* btn_text = lv_label_get_text(btn_label);

            if (strcmp(btn_text, "Connect") == 0) {
                LV_LOG_USER("Connecting");
                /* 在对话框中找到密码输入框 */
                lv_obj_t* pw_ta = NULL;
                /* 遍历对话框的子对象找到 lv_textarea 类型 */
                int32_t child_id = 0;
                lv_obj_t* child = lv_obj_get_child(dialog, child_id);
                while (child) {
                    if (lv_obj_check_type(child, &lv_textarea_class)) {
                        pw_ta = child;
                        break;
                    }
                    child_id++;
                    child = lv_obj_get_child(dialog, child_id);
                }
                if (pw_ta) {
                    const char* password = lv_textarea_get_text(pw_ta);
                    const char* ssid = (const char*)lv_obj_get_user_data(dialog);
                    if (password[0] != '\0')
                    {
                        LV_LOG_USER("Connecting to %s with password %s", ssid, password);
                        if (cnc_wifi_info && cnc_wifi_info->label && lv_obj_is_valid(cnc_wifi_info->label))
                        {
                            lv_obj_set_style_text_color(cnc_wifi_info->label, lv_color_hex(0x000000), LV_STATE_DEFAULT);
                            cnc_wifi_info->label = NULL;
                            cnc_wifi_info->connecting = false;
                        }
                        /* 调用实际的 WiFi 连接函数，例如 wifi_connect(ssid, password) */

#ifdef _WIN32
#else
                        ESP8266_SET_WIFI_INFO(ssid, password);
#endif
                        general_create_load_data_t(&joinAp_data, joinAP_timer_cb, NULL, "connect wifi...", 4000);
                    }
                    else LV_LOG_USER("password is NULL");
                }

            }
            else if (strcmp(btn_text, "Cancel") == 0)
            {
                LV_LOG_USER("Cancel");
            }
        }
        /* 无论点击连接还是取消，都关闭对话框 */
        lv_obj_del(dialog);
    }
}


/* 列表项点击事件回调 */
static void wifi_item_click_cb(lv_event_t* e)
{



    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* btn = lv_event_get_target(e);

    if (code == LV_EVENT_CLICKED) {
        /* 获取按钮上的标签文本来得到 SSID */
        /* 注意：lv_list_add_btn 创建的按钮包含一个标签作为子对象，标签文本就是我们传入的字符串 */
        lv_obj_t* label = lv_obj_get_child(btn, 1);  /* 按钮的第一个子对象通常是标签 */
        if (label != 0 && lv_obj_has_class(label, &lv_label_class)) {
            const char* ssid = lv_label_get_text(label);
            /* 弹出密码输入对话框，需要复制 SSID 字符串，因为 ssid 可能指向动态内存，且对话框生命周期长 */
            if (ssid_copy == NULL)ssid_copy = (char*)lv_mem_alloc(MAX_SSID_LEN * sizeof(char));
            if (ssid_copy != NULL)
            {
                if (extract_wifi_name(ssid, ssid_copy, MAX_SSID_LEN))
                {
                    show_password_dialog(ssid_copy);
                }

            }
            /* 注意：ssid_copy 在对话框关闭后应释放，但对话框的回调中无法直接释放，因为不知道何时释放。
               可以在对话框关闭后，在回调中通过用户数据释放。本例将 ssid_copy 作为对话框用户数据，并在对话框删除前释放。 */
        }
    }
}

/* 修改扫描按钮的回调，为每个列表项添加点击事件 */
static void scan_btn_event_cb(lv_event_t* e)
{
    lv_obj_t* btn = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED && wifi_open_sta == true) {
        LV_LOG_USER("Scan button clicked");

        /* 清空列表 */
        lv_obj_clean(wifi_list);
        general_create_load_data_t(&data, loading_timer_cb, btn, "scan wifi ap...", 3000);

        //控制WIFI模块扫描AP
#ifdef _WIN32
#else
        if (ESP8266_GET_STATE() != WIFI_SCAN_AP)ESP8266_RequestServer(WIFI_SCAN_AP);
#endif




    }
}
