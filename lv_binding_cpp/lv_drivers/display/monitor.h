/**
 * @file monitor.h
 *
 */

#ifndef MONITOR_H
#define MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#ifndef LV_DRV_NO_CONF
#ifdef LV_CONF_INCLUDE_SIMPLE
#include "lv_drv_conf.h"
#else
#include "../../lv_drv_conf.h"
#endif
#endif

#if USE_MONITOR

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/
void monitor_init(void);

/* 运行时设置 monitor 分辨率（用于让 sdl_hal_init(w,h) 生效）
 * 注意：需要在 monitor_init() 之前调用
 * 默认由 lv_drv_conf.h 中的 MONITOR_RUNTIME_RESOLUTION 控制是否启用 */
#if MONITOR_RUNTIME_RESOLUTION
void monitor_set_resolution(int32_t hor_res, int32_t ver_res);
#else
static inline void monitor_set_resolution(int32_t hor_res, int32_t ver_res)
{
    (void)hor_res;
    (void)ver_res;
}
#endif

void monitor_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
void monitor_flush2(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

/**********************
 *      MACROS
 **********************/

#endif /* USE_MONITOR */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MONITOR_H */
