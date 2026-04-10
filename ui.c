#include "ui.h"
#include "tr.h"
#include "widget.h"
#include "lvgl.h"

#define LOG_TAG "ui"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#include "log.h"

typedef struct
{
    int last_window_index;
    int current_window_index;
    int request_window_index;
    void *request_params;
    void (*update_callback)(void);
    int update_counter;
} ui_t;

ui_t ui = {0};

int ui_init(void)
{
    memset(&ui, 0, sizeof(ui));
    if (tr_init() < 0)
    {
        LOG_I("tr_init failed\n");
        return -1;
    }
    if (widget_init() < 0)
    {
        LOG_I("widget_init failed\n");
        return -1;
    }
    //启动调度
    ui.current_window_index = -1;
    ui.last_window_index = -1;
    ui.request_window_index = 1;
    return 0;
}

void ui_deinit(void)
{
    window_destory(ui.current_window_index);
    widget_deinit();
    tr_deinit();
}

void ui_loop(void)
{
    int request = ui.request_window_index;
    if (ui.current_window_index != request)
    {
        if (ui.current_window_index > 0)
        {
            window_send_destroyed_event(ui.current_window_index);
            window_destory(ui.current_window_index);
        }
        window_create(request);
        ui.last_window_index = ui.current_window_index;
        ui.current_window_index = request;
        window_send_created_event(request, ui.request_params);
    }
    if (ui.update_callback)
        ui.update_callback();
    
    // // 每25次循环发送一次update事件
    // if (++ui.update_counter >= 25)
    // {
        window_send_update_event(ui.current_window_index);
        window_send_update_to_sys_event();
        // ui.update_counter = 0;
    // }
}

void ui_update_window_immediately(void)
{
    int request = ui.request_window_index;
    if (ui.current_window_index != request)
    {
        if (ui.current_window_index > 0)
        {
            window_send_destroyed_event(ui.current_window_index);
            window_destory(ui.current_window_index);
        }
        window_create(request);
        ui.last_window_index = ui.current_window_index;
        ui.current_window_index = request;
        window_send_created_event(request, ui.request_params);
    }
}

int ui_register_window_callback(int window_index, window_callback_t callback)
{
    return window_register_callback(window_index, callback);
}

int ui_register_update_callback(void (*update_callback)(void))
{
    ui.update_callback = update_callback;
}

void ui_refresh_window(void *request_params)
{
    window_send_destroyed_event(ui.current_window_index);
    window_destory(ui.current_window_index);
    window_create(ui.current_window_index);
    window_send_created_event(ui.current_window_index, request_params);
}

void ui_set_window_index(int request, void *request_params)
{
    LOG_D("[%s] id = %d", __FUNCTION__, request);

    ui.request_window_index = request;
    ui.request_params = request_params;
}

int ui_get_window_index(void)
{
    return ui.current_window_index;
}

int ui_get_window_last_index(void)
{
    return ui.last_window_index;
}

int ui_get_window_next_index(void)
{
    return ui.request_window_index;
}

int ui_set_window_last_index(int request)
{
    ui.last_window_index = request;
}

const char *ui_get_image_src(int index)
{
    static char path[PATH_MAX_LEN + 1];
    snprintf(path, PATH_MAX_LEN, IMAGE_RESOURCES_PATH "%d.png", index);
    return path;
}
