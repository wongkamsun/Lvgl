#include "widget.h"
#include "tr.h"
#include "lvgl.h"
// #include "params.h"
#include "../../app/e100/app.h"

#define LOG_TAG "widget"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_DEBUG
#include "log.h"

#define LV_STYLE_PROP_CUSTOMER (1 << 16)
#define LV_STYLE_PROP_INNER_OBJ_MASK (0x000E0000)
#define LV_STYLE_PROP_BUILT_IN_MASK (0x0000FFFF)

enum
{
    LV_STYLE_CUSTOM_IMAGE_INDEX = 1,
    LV_STYLE_CUSTOM_TR_INDEX,
    LV_STYLE_CUSTOM_FONT_WEIGHT,
    LV_STYLE_CUSTOM_PARENT,
    LV_STYLE_CUSTOM_ALIGN_TOP_LEFT,
    LV_STYLE_CUSTOM_ALIGN_TOP_MID,
    LV_STYLE_CUSTOM_ALIGN_TOP_RIGHT,
    LV_STYLE_CUSTOM_ALIGN_BOTTOM_LEFT,
    LV_STYLE_CUSTOM_ALIGN_BOTTOM_MID,
    LV_STYLE_CUSTOM_ALIGN_BOTTOM_RIGHT,
    LV_STYLE_CUSTOM_ALIGN_LEFT_TOP,
    LV_STYLE_CUSTOM_ALIGN_LEFT_MID,
    LV_STYLE_CUSTOM_ALIGN_LEFT_BOTTOM,
    LV_STYLE_CUSTOM_ALIGN_RIGHT_TOP,
    LV_STYLE_CUSTOM_ALIGN_RIGHT_MID,
    LV_STYLE_CUSTOM_ALIGN_RIGHT_BOTTOM,
};

#define WIDGET_TYPE_CONTAINER 0
#define WIDGET_TYPE_BTN 1
#define WIDGET_TYPE_IMAGE 2
#define WIDGET_TYPE_LABEL 3
#define WIDGET_TYPE_BAR 4
#define WIDGET_TYPE_SLIDER 5
#define WIDGET_TYPE_TEMPLATE 6
#define WIDGET_TYPE_TEXT_AREA 7
#define WIDGET_TYPE_ARC 8

static FILE *widget_fp = NULL;
static widget_bin_header_t widget_bin_header;
static widget_t **widget_list = NULL;
static widget_t **sys_widget_list = NULL;
static window_header_t *window_list = NULL;
static window_callback_t *window_callback_list = NULL;

static widget_t *widget_alloc(widget_header_t *h, lv_obj_t *prent);
static void widget_destroy(widget_t *widget);
static void widget_set_built_in_flag(lv_obj_t *obj, widget_flag_header_t *header);
static void widget_set_built_in_style(lv_obj_t *obj, widget_style_header_t *header);
static void widget_refresh_custom_style(widget_t *widget);
static void widget_event(lv_event_t *e);

int widget_init(void)
{
    int ret = 0;
    widget_fp = fopen(WIDGET_RESOURCES_PATH, "rb");
    if (widget_fp == NULL)
        return -1;

    if (fread(&widget_bin_header, 1, sizeof(widget_bin_header_t), widget_fp) != sizeof(widget_bin_header_t))
    {
        LOG_I("read bin failed\n");
        return -2;
    }

    if (widget_bin_header.magic != WIDGET_MAGIC)
    {
        LOG_I("bin magic error\n");
        return -3;
    }

    //分配窗口内存
    LOG_I("widget_bin_header.window_numbers = %d\n", widget_bin_header.window_numbers);
    window_list = (window_header_t *)malloc(sizeof(window_header_t) * widget_bin_header.window_numbers);
    if (window_list == NULL)
    {
        LOG_I("can't allocate memory for window_list\n");
        return -4;
    }
    memset(window_list, 0, sizeof(window_header_t) * widget_bin_header.window_numbers);
    //分配窗口回调内存
    window_callback_list = (window_callback_t *)malloc(sizeof(window_callback_t) * widget_bin_header.window_numbers);
    if (window_callback_list == NULL)
    {
        LOG_I("can't allocate memory for window_callback_list\n");
        return -4;
    }
    memset(window_callback_list, 0, sizeof(window_callback_t) * widget_bin_header.window_numbers);

    //分配空间内存
    widget_list = (widget_t **)malloc(sizeof(widget_t *) * widget_bin_header.max_widget_numbers);
    sys_widget_list = (widget_t **)malloc(sizeof(widget_t *) * widget_bin_header.max_widget_numbers);
    if (widget_list == NULL)
    {
        LOG_I("can't allocate memory for widget_list\n");
        return -5;
    }
    if (sys_widget_list == NULL)
    {
        LOG_I("can't allocate memory for sys_widget_list\n");
        return -5;
    }
    memset(widget_list, 0, sizeof(widget_t *) * widget_bin_header.max_widget_numbers);
    memset(sys_widget_list, 0, sizeof(widget_t *) * widget_bin_header.max_widget_numbers);

    //读取窗口信息
    if (fread(window_list, 1, sizeof(window_header_t) * widget_bin_header.window_numbers, widget_fp) != sizeof(window_header_t) * widget_bin_header.window_numbers)
    {
        free(sys_widget_list);
        free(widget_list);
        free(window_callback_list);
        free(window_list);
        LOG_I("read window_list failed\n");
        return -6;
    }
    //创建系统层界面
    window_create(0);
    return 0;
}

void widget_deinit(void)
{
    free(widget_list);
    free(window_list);
    fclose(widget_fp);
}

int window_create(int window_index)
{
    widget_header_t widget_header;
    widget_flag_header_t widget_flag_header;
    widget_style_header_t widget_style_header;
    widget_t *widget;
    lv_event_t e;

    int i;
    for (i = 0; i < widget_bin_header.window_numbers; i++)
    {
        if (window_list[i].window_index == window_index)
        {
            window_index = i;
            break;
        }
    }

    if (widget_bin_header.window_numbers == i)
    {
        LOG_I("can't find window %d\n", window_index);
        return -1;
    }

    fseek(widget_fp, window_list[window_index].widget_offset, SEEK_SET);
    for (int i = 0; i < window_list[window_index].widget_numbers; i++)
    {
        //读取头部
        if (fread(&widget_header, 1, sizeof(widget_header_t), widget_fp) != sizeof(widget_header_t))
        {
            LOG_I("window_index %d widget_lsit_index %d read header failed\n", window_list[window_index].window_index, i);
            continue;
        }

        //创建控件
        widget = widget_alloc(&widget_header, lv_scr_act());
        if (widget == NULL)
        {
            LOG_I("window_index %d widget_index %d alloc failed\n", window_list[window_index].window_index, widget_header.index);
            continue;
        }

        widget->window_index = window_index;
        widget->win = NULL; //区别于动态界面
        memcpy(&widget->header, &widget_header, sizeof(widget_header_t));

        //初始化控件
        lv_obj_set_user_data(widget->obj_container[0], widget);

        lv_obj_set_pos(widget->obj_container[0], widget_header.x, widget_header.y);
        if (widget_header.w > 0 && widget_header.h > 0)
            lv_obj_set_size(widget->obj_container[0], widget_header.w, widget_header.h);
        lv_obj_set_style_pad_all(widget->obj_container[0], 0, 0);
        lv_obj_set_style_radius(widget->obj_container[0], 0, 0);
        lv_obj_set_style_shadow_width(widget->obj_container[0], 0, 0);
        lv_obj_add_event_cb(widget->obj_container[0], widget_event, 0, widget);

        //设置控件FLAG
        for (int j = 0; j < widget_header.widget_flag_numbers; j++)
        {
            if (fread(&widget_flag_header, 1, sizeof(widget_flag_header), widget_fp) != sizeof(widget_flag_header))
            {
                LOG_I("window_index %d widget_index %d read flag failed\n", window_list[i].window_index, widget_header.index);
                continue;
            }
            widget_set_built_in_flag(widget->obj_container[0], &widget_flag_header);
        }

        //设置控件STYLE
        for (int j = 0; j < widget_header.widget_style_numbers; j++)
        {
            int customer = 0;
            int obj_index = 0;
            uint32_t style_offset = ftell(widget_fp);
            if (fread(&widget_style_header, 1, sizeof(widget_style_header), widget_fp) != sizeof(widget_style_header))
            {
                LOG_I("window_index %d widget_index %d read style failed\n", window_list[i].window_index, widget_header.index);
                continue;
            }
            customer = widget_style_header.prop & LV_STYLE_PROP_CUSTOMER;
            obj_index = (widget_style_header.prop & LV_STYLE_PROP_INNER_OBJ_MASK) >> 17;
            widget_style_header.prop &= (~LV_STYLE_PROP_CUSTOMER);
            widget_style_header.prop &= (~LV_STYLE_PROP_INNER_OBJ_MASK);

            if (!customer)
            {
                widget_set_built_in_style(widget->obj_container[obj_index], &widget_style_header);
            }
            else
            {
                //自定义的style需要分配内存管理,非自定义的style由lvgl管理
                widget->custom_style_numbers[obj_index]++;
                widget_style_header_t *rep = realloc(widget->custom_style[obj_index], widget->custom_style_numbers[obj_index] * sizeof(widget_style_header_t));
                if (rep == NULL)
                {
                    LOG_I("window_index %d widget_index %d alloc for custom style failed\n", window_list[i].window_index, widget_header.index);
                    widget->custom_style_numbers[obj_index]--;
                    continue;
                }
                widget->custom_style[obj_index] = rep;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].prop = widget_style_header.prop;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].value = widget_style_header.value;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].selector = widget_style_header.selector;
            }
        }

        if (window_index == 0)
        {
            sys_widget_list[widget_header.index] = widget;
        }
        else
        {
            widget_list[widget_header.index] = widget;
        }

        if (widget->custom_style_numbers[0] > 0)
            lv_event_send(widget->obj_container[0], LV_EVENT_CUSTOM_STYLE_CHANGED, NULL);
    }

    return 0;
}

window_t *window_copy(int window_index, window_callback_t callback, lv_obj_t *parent, void *user_data)
{
    widget_header_t widget_header;
    widget_flag_header_t widget_flag_header;
    widget_style_header_t widget_style_header;
    widget_t *widget;
    lv_event_t e;

    //寻找界面
    int i;
    for (i = 0; i < widget_bin_header.window_numbers; i++)
    {
        if (window_list[i].window_index == window_index)
        {
            window_index = i;
            break;
        }
    }
    if (widget_bin_header.window_numbers == i)
    {
        LOG_I("can't find window %d\n", window_index);
        return NULL;
    }

    //分配界面内存
    window_t *win = (window_t *)malloc(sizeof(window_t));
    if (win == NULL)
        return NULL;
    win->user_data = user_data; //设置用户指针
    win->callback = callback;
    //拷贝界面头部
    memcpy(&win->header, &window_list[window_index], sizeof(window_header_t));
    win->widget_list = malloc(sizeof(widget_t *) * window_list[window_index].widget_numbers);
    if (win->widget_list == NULL)
    {
        free(win);
        return NULL;
    }

    //初始化控件
    fseek(widget_fp, window_list[window_index].widget_offset, SEEK_SET);
    for (int i = 0; i < window_list[window_index].widget_numbers; i++)
    {
        //读取头部
        if (fread(&widget_header, 1, sizeof(widget_header_t), widget_fp) != sizeof(widget_header_t))
        {
            LOG_I("window_index %d widget_lsit_index %d read header failed\n", window_list[window_index].window_index, i);
            continue;
        }

        //创建控件
        widget = widget_alloc(&widget_header, parent);
        if (widget == NULL)
        {
            LOG_I("window_index %d widget_index %d alloc failed\n", window_list[window_index].window_index, widget_header.index);
            continue;
        }

        widget->window_index = window_index;
        widget->win = win;
        memcpy(&widget->header, &widget_header, sizeof(widget_header_t));

        //初始化控件
        lv_obj_set_user_data(widget->obj_container[0], widget);
        lv_obj_set_pos(widget->obj_container[0], widget_header.x, widget_header.y);
        if (widget_header.w > 0 && widget_header.h > 0)
            lv_obj_set_size(widget->obj_container[0], widget_header.w, widget_header.h);
        lv_obj_set_style_pad_all(widget->obj_container[0], 0, 0);
        lv_obj_set_style_radius(widget->obj_container[0], 0, 0);
        lv_obj_set_style_shadow_width(widget->obj_container[0], 0, 0);
        lv_obj_add_event_cb(widget->obj_container[0], widget_event, 0, widget);

        //设置控件FLAG
        for (int i = 0; i < widget_header.widget_flag_numbers; i++)
        {
            if (fread(&widget_flag_header, 1, sizeof(widget_flag_header), widget_fp) != sizeof(widget_flag_header))
            {
                LOG_I("window_index %d widget_index %d read flag failed\n", window_list[i].window_index, widget_header.index);
                continue;
            }
            widget_set_built_in_flag(widget->obj_container[0], &widget_flag_header);
        }

        //设置控件STYLE
        for (int i = 0; i < widget_header.widget_style_numbers; i++)
        {
            int customer = 0;
            int obj_index = 0;
            if (fread(&widget_style_header, 1, sizeof(widget_style_header), widget_fp) != sizeof(widget_style_header))
            {
                LOG_I("window_index %d widget_index %d read style failed\n", window_list[i].window_index, widget_header.index);
                continue;
            }
            customer = widget_style_header.prop & LV_STYLE_PROP_CUSTOMER;
            obj_index = (widget_style_header.prop & LV_STYLE_PROP_INNER_OBJ_MASK) >> 17;
            widget_style_header.prop &= (~LV_STYLE_PROP_CUSTOMER);
            widget_style_header.prop &= (~LV_STYLE_PROP_INNER_OBJ_MASK);
            if (!customer)
            {
                widget_set_built_in_style(widget->obj_container[obj_index], &widget_style_header);
            }
            else
            {
                //自定义的style需要分配内存管理,非自定义的style由lvgl管理
                widget->custom_style_numbers[obj_index]++;
                widget_style_header_t *rep = realloc(widget->custom_style[obj_index], widget->custom_style_numbers[obj_index] * sizeof(widget_style_header_t));
                if (rep == NULL)
                {
                    LOG_I("window_index %d widget_index %d alloc for custom style failed\n", window_list[i].window_index, widget_header.index);
                    widget->custom_style_numbers[obj_index]--;
                    continue;
                }
                widget->custom_style[obj_index] = rep;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].prop = widget_style_header.prop;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].value = widget_style_header.value;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].selector = widget_style_header.selector;
            }
        }

        win->widget_list[widget_header.index] = widget;
        if (widget->custom_style_numbers[0] > 0)
            lv_event_send(widget->obj_container[0], LV_EVENT_CUSTOM_STYLE_CHANGED, NULL);
    }
    e.code = LV_EVENT_CREATED;
    e.param = user_data;
    if (widget->win->callback)
        widget->win->callback(widget->win->widget_list, NULL, &e);
    return win;
}

void window_copy_destory(window_t *win)
{
    if (win)
    {
        lv_event_t e;
        e.code = LV_EVENT_DESTROYED;
        if (win->callback)
            win->callback(win->widget_list, NULL, &e);
        for (int i = win->header.widget_numbers - 1; i >= 0; i--)
        {
            lv_obj_remove_event_cb(win->widget_list[i]->obj_container[0], widget_event);
            for (int j = win->widget_list[i]->obj_numbers - 1; j >= 0; j--)
                free(win->widget_list[i]->custom_style[j]);
            widget_destroy(win->widget_list[i]);
        }
        free(win->widget_list);
        free(win);
        win = NULL;
    }
}

void window_send_created_event(int window_index, void *param)
{
    lv_event_t e;
    e.code = LV_EVENT_CREATED;
    e.param = param;
    if (window_callback_list[window_index])
        window_callback_list[window_index](widget_list, NULL, &e);
}

void window_send_destroyed_event(int window_index)
{
    lv_event_t e;
    e.code = LV_EVENT_DESTROYED;
    if (window_callback_list[window_index])
        window_callback_list[window_index](widget_list, NULL, &e);
}

void window_send_update_event(int window_index)
{
    lv_event_t e;
    e.code = LV_EVENT_UPDATE;
    if (window_callback_list[window_index])
    {
        window_callback_list[window_index](widget_list, NULL, &e);
    }
}

void window_send_update_to_sys_event()
{
    lv_event_t e;
    e.code = LV_EVENT_UPDATE;
    if (window_callback_list[0])
    {
        window_callback_list[0](sys_widget_list, NULL, &e);
    }
}

void window_destory(int window_index)
{
    int i;
    lv_event_t e;
    for (i = 0; i < widget_bin_header.window_numbers; i++)
    {
        if (window_list[i].window_index == window_index)
        {
            window_index = i;
            break;
        }
    }
    if (widget_bin_header.window_numbers == i)
        return;

    for (int i = window_list[window_index].widget_numbers - 1; i >= 0; i--)
    {
        lv_obj_remove_event_cb(widget_list[i]->obj_container[0], widget_event);
        for (int j = widget_list[i]->obj_numbers - 1; j >= 0; j--)
            free(widget_list[i]->custom_style[j]);
        widget_destroy(widget_list[i]);
    }
}

int window_register_callback(int window_index, window_callback_t callback)
{
    int i;
    for (i = 0; i < widget_bin_header.window_numbers; i++)
    {
        if (window_list[i].window_index == window_index)
        {
            window_index = i;
            break;
        }
    }
    if (widget_bin_header.window_numbers == i)
        return -1;
    window_callback_list[window_index] = callback;
    return 0;
}

static widget_t *widget_alloc(widget_header_t *h, lv_obj_t *parent)
{
    widget_t *widget = (widget_t *)malloc(sizeof(widget_t));

    if (widget == NULL)
    {
        LOG_I("alloc for widget failed\n");
        return NULL;
    }

    switch (h->type)
    {
    case WIDGET_TYPE_CONTAINER:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_BTN:
        widget->obj_numbers = 3;
        break;
    case WIDGET_TYPE_IMAGE:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_LABEL:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_BAR:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_SLIDER:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_TEXT_AREA:
        widget->obj_numbers = 1;
        break;
    case WIDGET_TYPE_ARC:
        widget->obj_numbers = 1;
        break;
    default:
        return NULL;
    }

    widget->obj_container = (lv_obj_t **)malloc(widget->obj_numbers * sizeof(lv_obj_t *));
    if (widget->obj_container == NULL)
    {
        free(widget);
        return NULL;
    }

    switch (h->type)
    {
    case WIDGET_TYPE_CONTAINER:
        widget->obj_container[0] = lv_obj_create(parent);
        lv_obj_set_style_pad_all(widget->obj_container[0], 0, 0);
        lv_obj_set_style_radius(widget->obj_container[0], 0, 0);
        lv_obj_set_style_border_width(widget->obj_container[0], 0, 0);
        break;
    case WIDGET_TYPE_BTN:
        widget->obj_container[0] = lv_btn_create(parent);
        widget->obj_container[1] = lv_img_create(widget->obj_container[0]);
        widget->obj_container[2] = lv_label_create(widget->obj_container[0]);
        if (CONFIG_PROJECT == PROJECT_ELEGO_E100)
            lv_obj_set_style_text_line_space(widget->obj_container[2], 0, LV_PART_MAIN);
        else
            lv_obj_set_style_text_line_space(widget->obj_container[2], -3, LV_PART_MAIN);
        break;
    case WIDGET_TYPE_IMAGE:
        widget->obj_container[0] = lv_img_create(parent);
        break;
    case WIDGET_TYPE_LABEL:
        widget->obj_container[0] = lv_label_create(parent);
        if (CONFIG_PROJECT == PROJECT_ELEGO_E100)
            lv_obj_set_style_text_line_space(widget->obj_container[0], 0, LV_PART_MAIN);
        else
            lv_obj_set_style_text_line_space(widget->obj_container[0], -3, LV_PART_MAIN);
        break;
    case WIDGET_TYPE_BAR:
        widget->obj_container[0] = lv_bar_create(parent);
        break;
    case WIDGET_TYPE_SLIDER:
        widget->obj_container[0] = lv_slider_create(parent);
        break;
    case WIDGET_TYPE_TEXT_AREA:
        widget->obj_container[0] = lv_textarea_create(parent);
        break;
    case WIDGET_TYPE_ARC:
        widget->obj_container[0] = lv_arc_create(parent);
        break;
    default:
        free(widget->obj_container);
        free(widget);
        return NULL;
    }

    widget->custom_style_numbers = malloc(widget->obj_numbers * sizeof(uint32_t));
    widget->custom_style = malloc(widget->obj_numbers * sizeof(widget_style_header_t *));
    memset(widget->custom_style_numbers, 0, widget->obj_numbers * sizeof(uint32_t));
    memset(widget->custom_style, 0, widget->obj_numbers * sizeof(widget_style_header_t *));
    return widget;
}

static void widget_destroy(widget_t *widget)
{
    free(widget->custom_style);
    free(widget->custom_style_numbers);
    lv_obj_del(widget->obj_container[0]);
    free(widget->obj_container);
    free(widget);
}

static void widget_event(lv_event_t *e)
{
    widget_t *widget = (widget_t *)lv_obj_get_user_data(lv_event_get_current_target(e));
    switch (lv_event_get_code(e))
    {
    case LV_EVENT_CUSTOM_STYLE_CHANGED:
    case LV_EVENT_PRESSED:
    case LV_EVENT_PRESSING:
    case LV_EVENT_PRESS_LOST:
    case LV_EVENT_SHORT_CLICKED:
    case LV_EVENT_LONG_PRESSED:
    case LV_EVENT_LONG_PRESSED_REPEAT:
    case LV_EVENT_CLICKED:
    case LV_EVENT_RELEASED:
    case LV_EVENT_SIZE_CHANGED:
    case LV_EVENT_SCROLL_END:
        widget_refresh_custom_style(widget);
        break;
    }

    beeper_ui_handler(e);
    extruder_retime_handler(e);

    if (window_callback_list[widget->window_index])
    {
        if (widget->window_index == 0)
            window_callback_list[widget->window_index](sys_widget_list, widget, e);
        else
            window_callback_list[widget->window_index](widget_list, widget, e);
    }
    else if (widget->win && widget->win->callback)
    {
        widget->win->callback(widget->win->widget_list, widget, e);
    }
}

static void widget_set_built_in_flag(lv_obj_t *obj, widget_flag_header_t *header)
{
    if (header->mask)
        lv_obj_add_flag(obj, header->flag);
    else
        lv_obj_clear_flag(obj, header->flag);
}

static void widget_set_built_in_style(lv_obj_t *obj, widget_style_header_t *header)
{
    lv_style_value_t v = {header->value};
    lv_obj_set_local_style_prop(obj, header->prop, v, header->selector);
}

static void widget_refresh_custom_style(widget_t *widget)
{
    char image_path[64];
    char tr_str[512];
    tr_string_t tr_string;
    uint16_t current_state = lv_obj_get_state(widget->obj_container[0]);
    uint16_t style_numbers = widget->custom_style_numbers[0];
    widget_style_header_t *style_list = widget->custom_style[0];
    widget_style_header_t prop_match[32];
    uint8_t prop_match_numbers = 0;
    uint8_t font_weight;
    uint8_t font_style;
    //扫描属性
    for (int i = 0; i < style_numbers; i++)
    {
        uint16_t selector_state = lv_obj_style_get_selector_state(style_list[i].selector);
        if ((selector_state == LV_STATE_DEFAULT) || (current_state & selector_state))
        {
            //查看该属性是否已经被匹配
            int m = 0;
            for (m = 0; m < prop_match_numbers; m++)
            {
                uint16_t prop_state = lv_obj_style_get_selector_state(prop_match[m].selector);
                //只能覆盖默认状态的值
                if (prop_match[m].prop == style_list[i].prop)
                {
                    if (prop_state == LV_STATE_DEFAULT)
                        memcpy(&prop_match[m], &style_list[i], sizeof(widget_style_header_t));
                    break;
                }
            }
            //找不到则将其添加为新属性
            if (m == prop_match_numbers)
            {
                memcpy(&prop_match[prop_match_numbers++], &style_list[i], sizeof(widget_style_header_t));
            }
        }
    }

    widget_t **pwidget_list;
    if (widget->win != NULL)
    {
        pwidget_list = widget->win->widget_list;
    }
    else
    {
        if (widget->window_index == 0)
            pwidget_list = sys_widget_list;
        else
            pwidget_list = widget_list;
    }
    //应用属性
    for (int i = 0; i < prop_match_numbers; i++)
    {
        switch (prop_match[i].prop)
        {
        case LV_STYLE_CUSTOM_IMAGE_INDEX:
            if (widget->header.type == WIDGET_TYPE_BTN)
            {
                if (prop_match[i].value != 0)
                {
                    snprintf(image_path, sizeof(image_path) - 1, IMAGE_RESOURCES_PATH "%d.png", prop_match[i].value);
                    lv_img_set_src(widget->obj_container[1], image_path);
                }
            }
            else if (widget->header.type == WIDGET_TYPE_IMAGE)
            {
                if (prop_match[i].value != 0)
                {
                    snprintf(image_path, sizeof(image_path) - 1, IMAGE_RESOURCES_PATH "%d.png", prop_match[i].value);
                    lv_img_set_src(widget->obj_container[0], image_path);
                }
            }
            break;
        case LV_STYLE_CUSTOM_TR_INDEX:
            if (widget->header.type == WIDGET_TYPE_BTN)
            {
                if (prop_match[i].value != 0)
                {
                    lv_label_set_text(widget->obj_container[2], tr(prop_match[i].value));
                }
            }
            else if (widget->header.type == WIDGET_TYPE_LABEL)
            {
                if (prop_match[i].value != 0)
                {
                    lv_label_set_text(widget->obj_container[0], tr(prop_match[i].value));
                }
            }
            else if (widget->header.type == WIDGET_TYPE_TEXT_AREA)
            {
                if (prop_match[i].value != 0)
                {
                    lv_textarea_set_text(widget->obj_container[0], tr(prop_match[i].value));
                }
            }
            break;
        case LV_STYLE_CUSTOM_FONT_WEIGHT:
            font_weight = prop_match[i].value & 0xff;
            font_style = (prop_match[i].value & 0xff00) >> 8;
            if (tr_font_new(font_weight, font_style) < 0)
            {
                LOG_I("tr_font_new failed \n");
                return;
            }
            lv_obj_set_style_text_font(widget->obj_container[0], tr_get_font(font_weight, font_style), 0);
            break;
        case LV_STYLE_CUSTOM_PARENT:
            lv_obj_set_parent(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0]);
            // lv_obj_set_pos(widget->obj_container[0], widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_TOP_LEFT:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_TOP_LEFT, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_TOP_MID:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_TOP_MID, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_TOP_RIGHT:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_TOP_RIGHT, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_BOTTOM_LEFT:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_BOTTOM_LEFT, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_BOTTOM_MID:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_BOTTOM_MID, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_BOTTOM_RIGHT:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_BOTTOM_RIGHT, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_LEFT_TOP:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_LEFT_TOP, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_LEFT_MID:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_LEFT_MID, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_LEFT_BOTTOM:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_LEFT_BOTTOM, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_RIGHT_TOP:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_RIGHT_TOP, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_RIGHT_MID:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_RIGHT_MID, widget->header.x, widget->header.y);
            break;
        case LV_STYLE_CUSTOM_ALIGN_RIGHT_BOTTOM:
            lv_obj_align_to(widget->obj_container[0], pwidget_list[prop_match[i].value]->obj_container[0], LV_ALIGN_OUT_RIGHT_BOTTOM, widget->header.x, widget->header.y);
            break;
        }
    }
}

widget_t **window_get_widget_list()
{
    return widget_list;
}

widget_t **window_get_top_widget_list()
{
    return sys_widget_list;
}

window_dynamic_memory_t *window_dynamic_memory_alloc(int window_index)
{
    window_dynamic_memory_t *dm;
    int i = 0;
    for (i = 0; i < widget_bin_header.window_numbers; i++)
    {
        if (window_list[i].window_index == window_index)
        {
            window_index = i;
            break;
        }
    }
    if (widget_bin_header.window_numbers == i)
        return NULL;
    if ((dm = (window_dynamic_memory_t *)malloc(sizeof(window_dynamic_memory_t))) == NULL)
        return NULL;
    memset(dm, 0, sizeof(window_dynamic_memory_t));

    memcpy(&dm->header, &window_list[window_index], sizeof(window_header_t));
    if ((dm->widget_header_list = (widget_header_t *)malloc(sizeof(widget_header_t) * window_list[window_index].widget_numbers)) == NULL)
        return NULL;
    if ((dm->widget_flag_header_list = (widget_flag_header_t **)malloc(sizeof(widget_flag_header_t *) * window_list[window_index].widget_numbers)) == NULL)
        return NULL;
    if ((dm->widget_style_header_list = (widget_style_header_t **)malloc(sizeof(widget_style_header_t *) * window_list[window_index].widget_numbers)) == NULL)
        return NULL;

    fseek(widget_fp, window_list[window_index].widget_offset, SEEK_SET);
    for (int i = 0; i < window_list[window_index].widget_numbers; i++)
    {
        if (fread(&dm->widget_header_list[i], 1, sizeof(widget_header_t), widget_fp) != sizeof(widget_header_t))
            return NULL;
        if ((dm->widget_flag_header_list[i] = (widget_flag_header_t *)malloc(sizeof(widget_flag_header_t) * dm->widget_header_list[i].widget_flag_numbers)) == NULL)
            return NULL;
        if ((dm->widget_style_header_list[i] = (widget_style_header_t *)malloc(sizeof(widget_style_header_t) * dm->widget_header_list[i].widget_style_numbers)) == NULL)
            return NULL;

        for (int j = 0; j < dm->widget_header_list[i].widget_flag_numbers; j++)
        {
            if (fread(&dm->widget_flag_header_list[i][j], 1, sizeof(widget_flag_header_t), widget_fp) != sizeof(widget_flag_header_t))
                continue;
        }
        for (int j = 0; j < dm->widget_header_list[i].widget_style_numbers; j++)
        {
            if (fread(&dm->widget_style_header_list[i][j], 1, sizeof(widget_style_header_t), widget_fp) != sizeof(widget_style_header_t))
                continue;
        }
    }

    return dm;
}

void window_dynamic_memory_free(window_dynamic_memory_t *dm)
{
    for (int i = 0; i < dm->header.widget_numbers; i++)
    {
        free(dm->widget_style_header_list[i]);
        free(dm->widget_flag_header_list[i]);
    }
    free(dm->widget_style_header_list);
    free(dm->widget_flag_header_list);
    free(dm->widget_header_list);
    free(dm);
}

window_t *window_create_from_memory(window_dynamic_memory_t *dm, window_callback_t callback, lv_obj_t *parent, void *user_data)
{
    window_t *win;
    widget_t *widget;

    if ((win = (window_t *)malloc(sizeof(window_t))) == NULL)
        return NULL;
    win->user_data = user_data; //设置用户指针
    win->callback = callback;
    memcpy(&win->header, &dm->header, sizeof(window_header_t));
    if ((win->widget_list = (widget_t **)malloc(sizeof(widget_t *) * dm->header.widget_numbers)) == NULL)
        return NULL;
    for (int i = 0; i < dm->header.widget_numbers; i++)
    {
        widget = widget_alloc(&dm->widget_header_list[i], parent);
        widget->window_index = dm->header.window_index;
        widget->win = win;
        memcpy(&widget->header, &dm->widget_header_list[i], sizeof(widget_header_t));

        //初始化控件
        lv_obj_set_user_data(widget->obj_container[0], widget);
        lv_obj_set_pos(widget->obj_container[0], dm->widget_header_list[i].x, dm->widget_header_list[i].y);
        if (dm->widget_header_list[i].w > 0 && dm->widget_header_list[i].h > 0)
            lv_obj_set_size(widget->obj_container[0], dm->widget_header_list[i].w, dm->widget_header_list[i].h);
        lv_obj_set_style_pad_all(widget->obj_container[0], 0, 0);
        lv_obj_set_style_radius(widget->obj_container[0], 0, 0);
        lv_obj_set_style_shadow_width(widget->obj_container[0], 0, 0);
        lv_obj_add_event_cb(widget->obj_container[0], widget_event, 0, widget);

        //设置控件FLAG
        for (int j = 0; j < dm->widget_header_list[i].widget_flag_numbers; j++)
        {
            widget_set_built_in_flag(widget->obj_container[0], &dm->widget_flag_header_list[i][j]);
        }
        //设置控件STYLE
        for (int j = 0; j < dm->widget_header_list[i].widget_style_numbers; j++)
        {
            int customer = 0;
            int obj_index = 0;
            widget_style_header_t widget_style_header = dm->widget_style_header_list[i][j];

            customer = widget_style_header.prop & LV_STYLE_PROP_CUSTOMER;
            obj_index = (widget_style_header.prop & LV_STYLE_PROP_INNER_OBJ_MASK) >> 17;
            widget_style_header.prop &= (~LV_STYLE_PROP_CUSTOMER);
            widget_style_header.prop &= (~LV_STYLE_PROP_INNER_OBJ_MASK);
            if (!customer)
            {
                widget_set_built_in_style(widget->obj_container[obj_index], &widget_style_header);
            }
            else
            {
                //自定义的style需要分配内存管理,非自定义的style由lvgl管理
                widget->custom_style_numbers[obj_index]++;
                widget_style_header_t *rep = realloc(widget->custom_style[obj_index], widget->custom_style_numbers[obj_index] * sizeof(widget_style_header_t));
                if (rep == NULL)
                {
                    widget->custom_style_numbers[obj_index]--;
                    continue;
                }
                widget->custom_style[obj_index] = rep;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].prop = widget_style_header.prop;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].value = widget_style_header.value;
                widget->custom_style[obj_index][widget->custom_style_numbers[obj_index] - 1].selector = widget_style_header.selector;
            }
        }
        win->widget_list[dm->widget_header_list[i].index] = widget;
        if (widget->custom_style_numbers[0] > 0)
            lv_event_send(widget->obj_container[0], LV_EVENT_CUSTOM_STYLE_CHANGED, NULL);
    }
    return win;
}