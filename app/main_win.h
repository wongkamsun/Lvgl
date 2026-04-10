#pragma once

#include "../src/widget.h"

/**
 * 窗口 1：业务统一回调（在此函数内用 switch(event)->switch(widget->id) 管理）。
 * 注册动作在 `src/main.cpp` 里完成。
 */
void app_main_win_register(const WidgetsRuntimeWidget* const* widget_list,
                                const WidgetsRuntimeWidget* widget,
                                void* e,
                                void* user_data);
