#ifndef __MYFONT_UI_H_
#define __MYFONT_UI_H_

void font_init(void);
void ui_show_hz(void);
void write_text_to_label(const char* str);
void start_mjpeg_show(const char* img_path);
void stop_mjpeg_show(void);
bool get_status_mjpeg_show(void);
#endif