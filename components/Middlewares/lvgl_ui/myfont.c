// /*
// *---------------------------------------------------------------
// *                        Lvgl Font Tool                         
// *                                                               
// * 注:使用unicode编码                                           
// * 注:本字体文件由Lvgl Font Tool V0.4 生成                       
// * 作者:阿里(qq:617622104)                                      
// *---------------------------------------------------------------
// */

// #include "lvgl.h"
// #include <stdio.h>          // 用于文件操作
// #include <string.h>
// #include "esp_log.h"
// #define TAG "myfont"        // 用于日志输出

// typedef struct{
//     uint16_t min;
//     uint16_t max;
//     uint8_t  bpp;
//     uint8_t  reserved[3];
// } x_header_t;

// typedef struct{
//     uint32_t pos;
// } x_table_t;

// typedef struct{
//     uint8_t adv_w;
//     uint8_t box_w;
//     uint8_t box_h;
//     int8_t  ofs_x;
//     int8_t  ofs_y;
//     uint8_t r;
// } glyph_dsc_t;

// static x_header_t __g_xbf_hd = {
//     .min = 0x0020,
//     .max = 0xf2e0,
//     .bpp = 2,
// };

// /* -------------------- 文件读取相关 -------------------- */
// static FILE *s_font_file = NULL;                // 字体文件句柄
// static uint8_t s_read_buf[512];                 // 读取缓冲区（足够容纳大多数字形位图）

// // 打开字体文件（若未打开）
// static bool open_font_file(void) {
//     if (s_font_file != NULL) {
//         return true;    // 已打开
//     }
//     // 修改为你的实际 SD 卡挂载路径，例如 "/sdcard/myfont.bin"
//     const char *path = "/sdcard/myfont.bin";
//     s_font_file = fopen(path, "rb");
//     if (s_font_file == NULL) {
//         ESP_LOGE(TAG, "无法打开字体文件: %s", path);
//         return false;
//     }
//     ESP_LOGI(TAG, "成功打开字体文件: %s", path);
//     return true;
// }

// // 从文件中读取指定偏移和长度的数据到静态缓冲区，并返回该缓冲区指针
// static uint8_t *__user_font_getdata(int offset, int size) {
//     if (!open_font_file()) {
//         return NULL;
//     }

//     // 定位到偏移位置
//     if (fseek(s_font_file, offset, SEEK_SET) != 0) {
//         ESP_LOGE(TAG, "fseek 失败，偏移: %d", offset);
//         return NULL;
//     }

//     // 读取数据
//     size_t read_len = fread(s_read_buf, 1, size, s_font_file);
//     if (read_len != size) {
//         ESP_LOGE(TAG, "fread 读取字节数不符: 期望 %d, 实际 %d", size, read_len);
//         return NULL;
//     }

//     return s_read_buf;
// }

// /* -------------------- LVGL 字体回调函数 -------------------- */
// static const uint8_t *__user_font_get_bitmap(const lv_font_t *font, uint32_t unicode_letter) {
//     if (unicode_letter > __g_xbf_hd.max || unicode_letter < __g_xbf_hd.min) {
//         return NULL;
//     }

//     uint32_t unicode_offset = sizeof(x_header_t) + (unicode_letter - __g_xbf_hd.min) * 4;
//     uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
//     if (p_pos == NULL || p_pos[0] == 0) {
//         return NULL;
//     }

//     uint32_t pos = p_pos[0];
//     glyph_dsc_t *gdsc = (glyph_dsc_t *)__user_font_getdata(pos, sizeof(glyph_dsc_t));
//     if (gdsc == NULL) {
//         return NULL;
//     }

//     uint32_t bitmap_size = gdsc->box_w * gdsc->box_h * __g_xbf_hd.bpp / 8;
//     return __user_font_getdata(pos + sizeof(glyph_dsc_t), bitmap_size);
// }

// static bool __user_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
//                                       uint32_t unicode_letter, uint32_t unicode_letter_next) {
//     if (unicode_letter > __g_xbf_hd.max || unicode_letter < __g_xbf_hd.min) {
//         return false;
//     }

//     uint32_t unicode_offset = sizeof(x_header_t) + (unicode_letter - __g_xbf_hd.min) * 4;
//     uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
//     if (p_pos == NULL || p_pos[0] == 0) {
//         return false;
//     }

//     glyph_dsc_t *gdsc = (glyph_dsc_t *)__user_font_getdata(p_pos[0], sizeof(glyph_dsc_t));
//     if (gdsc == NULL) {
//         return false;
//     }

//     dsc_out->adv_w = gdsc->adv_w;
//     dsc_out->box_h = gdsc->box_h;
//     dsc_out->box_w = gdsc->box_w;
//     dsc_out->ofs_x = gdsc->ofs_x;
//     dsc_out->ofs_y = gdsc->ofs_y;
//     dsc_out->bpp   = __g_xbf_hd.bpp;
//     return true;
// }

// /* -------------------- 导出字体对象 -------------------- */
// // MiSans,,-1
// // 字模高度：27
// // XBF字体,外部bin文件
// lv_font_t myfont = {
//     .get_glyph_bitmap = __user_font_get_bitmap,
//     .get_glyph_dsc = __user_font_get_glyph_dsc,
//     .line_height = 27,
//     .base_line = 0,
// };



/*
*---------------------------------------------------------------
*                        Lvgl Font Tool                         
*                                                               
* 注:使用unicode编码                                           
* 注:本字体文件由Lvgl Font Tool V0.4 生成                       
* 作者:阿里(qq:617622104)                                      
*---------------------------------------------------------------
*/

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"          // 用于 PSRAM 分配

#define TAG "myfont"

typedef struct{
    uint16_t min;
    uint16_t max;
    uint8_t  bpp;
    uint8_t  reserved[3];
} x_header_t;

typedef struct{
    uint32_t pos;
} x_table_t;

typedef struct{
    uint8_t adv_w;
    uint8_t box_w;
    uint8_t box_h;
    int8_t  ofs_x;
    int8_t  ofs_y;
    uint8_t r;
} glyph_dsc_t;

static x_header_t __g_xbf_hd = {
    .min = 0x0020,
    .max = 0xff5e,
    .bpp = 2,
};

/* -------------------- 内存加载相关 -------------------- */
static uint8_t *s_font_data = NULL;     // 指向已加载到 PSRAM 的字体数据
static size_t   s_font_size = 0;        // 字体文件大小

// 加载字体文件到 PSRAM（在挂载 SD 卡后调用一次）
bool load_font_to_psram(const char *path) {
    if (s_font_data != NULL) {
        ESP_LOGW(TAG, "字体已加载，无需重复加载");
        return true;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "无法打开字体文件: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    s_font_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "字体文件大小: %u 字节", (unsigned int)s_font_size);

    // 优先分配 PSRAM
    s_font_data = heap_caps_malloc(s_font_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_font_data == NULL) {
        ESP_LOGW(TAG, "PSRAM 分配失败，尝试内部 RAM");
        s_font_data = heap_caps_malloc(s_font_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_font_data == NULL) {
        ESP_LOGE(TAG, "内存分配失败，大小 %u", (unsigned int)s_font_size);
        fclose(f);
        return false;
    }
    ESP_LOGI(TAG, "内存分配成功，地址: %p", s_font_data);

    size_t read_len = fread(s_font_data, 1, s_font_size, f);
    fclose(f);

    if (read_len != s_font_size) {
        ESP_LOGE(TAG, "读取文件大小不符: 期望 %u, 实际 %u", (unsigned int)s_font_size, (unsigned int)read_len);
        heap_caps_free(s_font_data);
        s_font_data = NULL;
        return false;
    }

    ESP_LOGI(TAG, "字体文件已成功加载到 PSRAM");
    return true;
}

// 释放字体内存（程序结束时调用）
void unload_font_from_psram(void) {
    if (s_font_data) {
        heap_caps_free(s_font_data);
        s_font_data = NULL;
        s_font_size = 0;
        ESP_LOGI(TAG, "字体内存已释放");
    }
}

/* -------------------- LVGL 字体回调函数（直接访问内存） -------------------- */
static uint8_t *__user_font_getdata(int offset, int size) {
    if (s_font_data == NULL) {
        ESP_LOGE(TAG, "字体尚未加载到内存，请先调用 load_font_to_psram()");
        return NULL;
    }
    if (offset + size > (int)s_font_size) {
        ESP_LOGE(TAG, "读取越界: offset=%d, size=%d, total=%u", offset, size, (unsigned int)s_font_size);
        return NULL;
    }
    return s_font_data + offset;
}

/* -------------------- LVGL 字体回调函数（不变） -------------------- */
static const uint8_t *__user_font_get_bitmap(const lv_font_t *font, uint32_t unicode_letter) {
    if (unicode_letter > __g_xbf_hd.max || unicode_letter < __g_xbf_hd.min) {
        return NULL;
    }

    uint32_t unicode_offset = sizeof(x_header_t) + (unicode_letter - __g_xbf_hd.min) * 4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if (p_pos == NULL || p_pos[0] == 0) {
        return NULL;
    }

    uint32_t pos = p_pos[0];
    glyph_dsc_t *gdsc = (glyph_dsc_t *)__user_font_getdata(pos, sizeof(glyph_dsc_t));
    if (gdsc == NULL) {
        return NULL;
    }

    uint32_t bitmap_size = gdsc->box_w * gdsc->box_h * __g_xbf_hd.bpp / 8;
    return __user_font_getdata(pos + sizeof(glyph_dsc_t), bitmap_size);
}

static bool __user_font_get_glyph_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out,
                                      uint32_t unicode_letter, uint32_t unicode_letter_next) {
    if (unicode_letter > __g_xbf_hd.max || unicode_letter < __g_xbf_hd.min) {
        return false;
    }

    uint32_t unicode_offset = sizeof(x_header_t) + (unicode_letter - __g_xbf_hd.min) * 4;
    uint32_t *p_pos = (uint32_t *)__user_font_getdata(unicode_offset, 4);
    if (p_pos == NULL || p_pos[0] == 0) {
        return false;
    }

    glyph_dsc_t *gdsc = (glyph_dsc_t *)__user_font_getdata(p_pos[0], sizeof(glyph_dsc_t));
    if (gdsc == NULL) {
        return false;
    }

    dsc_out->adv_w = gdsc->adv_w;
    dsc_out->box_h = gdsc->box_h;
    dsc_out->box_w = gdsc->box_w;
    dsc_out->ofs_x = gdsc->ofs_x;
    dsc_out->ofs_y = gdsc->ofs_y;
    dsc_out->bpp   = __g_xbf_hd.bpp;
    return true;
}

/* -------------------- 导出字体对象 -------------------- */
lv_font_t myfont = {
    .get_glyph_bitmap = __user_font_get_bitmap,
    .get_glyph_dsc = __user_font_get_glyph_dsc,
    .line_height = 27,
    .base_line = 0,
};