#ifndef UI_H
#define UI_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "utils.h"
#include "widget.h"
#include "tr.h"
    int ui_init(void);
    void ui_deinit(void);
    void ui_loop(void);

    void ui_set_window_index(int request, void *request_params);
    void ui_refresh_window(void *request_params);
    int ui_get_window_index(void);
    int ui_get_window_last_index(void);
    int ui_get_window_next_index(void);
    int ui_set_window_last_index(int request);
    void ui_update_window_immediately(void);
    int ui_register_window_callback(int window_index, window_callback_t callback);
    const char *ui_get_image_src(int index);
    int ui_register_update_callback(void (*update_callback)(void));
#ifdef __cplusplus
}
#endif

#endif