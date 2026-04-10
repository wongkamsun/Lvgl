#include "hal.h"

#include "lv_drv_conf.h"

#if LV_USE_FS_STDIO
#include <stdio.h>
#endif
#if LV_USE_LODEPNG
#include "lvgl/src/extra/libs/png/lv_png.h"
#endif

#if USE_MONITOR
#include "lv_drivers/display/monitor.h"
#include "lv_drivers/indev/mouse.h"
#if USE_MOUSEWHEEL
#include "lv_drivers/indev/mousewheel.h"
#endif
#if USE_KEYBOARD
#include "lv_drivers/indev/keyboard.h"
#endif
#endif

#if LV_USE_FS_STDIO
static void * hal_fs_open_cb(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    (void)drv;
    const char * smode = "rb";
    if (mode == LV_FS_MODE_WR)
        smode = "wb";
    else if ((mode & LV_FS_MODE_RD) && (mode & LV_FS_MODE_WR))
        smode = "rb+";
    return (void *)fopen(path, smode);
}

static lv_fs_res_t hal_fs_close_cb(lv_fs_drv_t * drv, void * file_p)
{
    (void)drv;
    fclose((FILE *)file_p);
    return LV_FS_RES_OK;
}

static lv_fs_res_t hal_fs_read_cb(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    (void)drv;
    size_t n = fread(buf, 1, btr, (FILE *)file_p);
    if (br)
        *br = (uint32_t)n;
    return ferror((FILE *)file_p) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t hal_fs_write_cb(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    (void)drv;
    size_t n = fwrite(buf, 1, btw, (FILE *)file_p);
    if (bw)
        *bw = (uint32_t)n;
    return ferror((FILE *)file_p) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

static lv_fs_res_t hal_fs_seek_cb(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    (void)drv;
    int w = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR)
        w = SEEK_CUR;
    else if (whence == LV_FS_SEEK_END)
        w = SEEK_END;
    return fseek((FILE *)file_p, (long)pos, w) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

static lv_fs_res_t hal_fs_tell_cb(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    (void)drv;
    long p = ftell((FILE *)file_p);
    if (p < 0)
        return LV_FS_RES_UNKNOWN;
    *pos_p = (uint32_t)p;
    return LV_FS_RES_OK;
}

static void hal_fs_stdio_register_once(void)
{
    static int done;
    if (done)
        return;
    done = 1;

    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = LV_FS_STDIO_LETTER;
    drv.open_cb = hal_fs_open_cb;
    drv.close_cb = hal_fs_close_cb;
    drv.read_cb = hal_fs_read_cb;
    drv.write_cb = hal_fs_write_cb;
    drv.seek_cb = hal_fs_seek_cb;
    drv.tell_cb = hal_fs_tell_cb;
    lv_fs_drv_register(&drv);
}
#endif

#if LV_USE_FS_STDIO || LV_USE_LODEPNG
static void hal_lvgl_fs_and_png_init(void)
{
#if LV_USE_FS_STDIO
    hal_fs_stdio_register_once();
#endif
#if LV_USE_LODEPNG
    lv_png_init();
#endif
}
#endif

lv_disp_t * sdl_hal_init(int32_t w, int32_t h)
{
#if LV_USE_FS_STDIO || LV_USE_LODEPNG
    hal_lvgl_fs_and_png_init();
#endif

#if !USE_MONITOR
    (void)w;
    (void)h;
    /* 如果你关闭了 USE_MONITOR，就不会创建 SDL 窗口，也不会注册显示驱动 */
    return NULL;
#else
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[MONITOR_HOR_RES * 100];
    static lv_color_t buf2[MONITOR_HOR_RES * 100];
    static lv_disp_drv_t disp_drv;

    /* 如果没开运行时分辨率，这两个参数不生效 */
    (void)w;
    (void)h;

    monitor_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, MONITOR_HOR_RES * 100);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = MONITOR_HOR_RES;
    disp_drv.ver_res = MONITOR_VER_RES;
    disp_drv.flush_cb = monitor_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
    lv_disp_set_default(disp);

    lv_group_t * g = lv_group_create();
    lv_group_set_default(g);

    mouse_init();
    static lv_indev_drv_t mouse_drv;
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = mouse_read;
    lv_indev_t * mouse = lv_indev_drv_register(&mouse_drv);
    lv_indev_set_group(mouse, lv_group_get_default());

    LV_IMG_DECLARE(mouse_cursor_icon);
    lv_obj_t * cursor_obj = lv_img_create(lv_scr_act());
    lv_img_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse, cursor_obj);

#if USE_MOUSEWHEEL
    mousewheel_init();
    static lv_indev_drv_t mousewheel_drv;
    lv_indev_drv_init(&mousewheel_drv);
    mousewheel_drv.type = LV_INDEV_TYPE_ENCODER;
    mousewheel_drv.read_cb = mousewheel_read;
    lv_indev_t * mousewheel = lv_indev_drv_register(&mousewheel_drv);
    lv_indev_set_group(mousewheel, lv_group_get_default());
#endif

#if USE_KEYBOARD
    keyboard_init();
    static lv_indev_drv_t kb_drv;
    lv_indev_drv_init(&kb_drv);
    kb_drv.type = LV_INDEV_TYPE_KEYPAD;
    kb_drv.read_cb = keyboard_read;
    lv_indev_t * kb = lv_indev_drv_register(&kb_drv);
    lv_indev_set_group(kb, lv_group_get_default());
#endif

    return disp;
#endif
}
