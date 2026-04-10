#ifndef WIDGET_H
#define WIDGET_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "utils.h"
#include "lvgl.h"

#define WIDGET_RESOURCES_PATH "/app/resources/widget.bin"
#define IMAGE_RESOURCES_PATH "/app/resources/images/"

    enum lv_ext_event_code_t
    {
        LV_EVENT_CUSTOM_STYLE_CHANGED = _LV_EVENT_LAST,
        LV_EVENT_CREATED,
        LV_EVENT_DESTROYED,
        LV_EVENT_CHILD_DESTROYED,
        LV_EVENT_CHILD_VALUE_CHANGE,
        LV_EVENT_CHILD_CLICKED,
        LV_EVENT_UPDATE,
        LV_EVENT_EXIT,
        LV_EVENT_CHILD_LONG_PRESSED,
        LV_EVENT_CHILD_PRESSING,
        LV_EVENT_CHILD_RELEASED,
    };

    typedef struct __attribute__((packed)) widget_header_tag
    {
        uint16_t index;
        uint16_t type;
        uint16_t x;
        uint16_t y;
        uint16_t w;
        uint16_t h;
        uint16_t callback_index;
        uint16_t event_index;
        uint16_t widget_flag_numbers;
        uint16_t widget_style_numbers;
    } widget_header_t;

    typedef struct __attribute__((packed)) widget_flag_header_tag
    {
        uint32_t flag;
        uint32_t mask;
    } widget_flag_header_t;

    typedef struct __attribute__((packed)) widget_style_header_tag
    {
        uint32_t prop;
        int32_t value;
        uint32_t selector;
    } widget_style_header_t;

    typedef struct __attribute__((packed)) window_header_tag
    {
        uint32_t widget_offset;
        uint16_t window_attr;
        uint8_t widget_numbers;
        uint8_t window_index;
    } window_header_t;

    typedef struct __attribute__((packed))
    {
#define WIDGET_MAGIC 0x12fd1001
        uint32_t magic;
        uint32_t size;
        uint16_t crc;
        uint16_t window_numbers;
        uint16_t total_widget_numbers;
        uint16_t max_widget_numbers;
    } widget_bin_header_t;

    typedef struct window_tag window_t;
    typedef struct
    {
        int obj_numbers;
        lv_obj_t **obj_container;
        widget_header_t header;
        widget_style_header_t **custom_style;
        int *custom_style_numbers;
        int window_index;
        void *user_data;
        window_t *win;
    } widget_t;

    typedef void (*window_callback_t)(widget_t **widget_list, widget_t *current_widget, void *e);
    typedef struct window_tag
    {
        window_header_t header;
        widget_t **widget_list;
        window_callback_t callback;
        void *user_data;
    } window_t;

    typedef struct
    {
        window_header_t header;
        widget_header_t *widget_header_list;
        widget_flag_header_t **widget_flag_header_list;
        widget_style_header_t **widget_style_header_list;
    } window_dynamic_memory_t;

    int widget_init(void);
    void widget_deinit(void);

    int window_create(int window_index);
    void window_destory(int window_index);
    int window_register_callback(int window_index, window_callback_t callback);
    void window_send_created_event(int window_index, void *param);
    void window_send_destroyed_event(int window_index);
    void window_send_update_event(int window_index);
    void window_send_update_to_sys_event();

    window_t *window_copy(int window_index, window_callback_t callback, lv_obj_t *parent, void *user_data);
    void window_copy_destory(window_t *win);

    widget_t **window_get_widget_list();
    widget_t **window_get_top_widget_list();

    window_dynamic_memory_t *window_dynamic_memory_alloc(int window_index);
    void window_dynamic_memory_free(window_dynamic_memory_t *dm);
    window_t *window_create_from_memory(window_dynamic_memory_t *dm, window_callback_t callback, lv_obj_t *parent, void *user_data);
#ifdef __cplusplus
}
#endif

#endif