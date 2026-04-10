#pragma once

#include "../src/widget.h"

/** 窗口 2：业务统一回调（注册在 `src/main.cpp` 里完成）。 */
void app_set_window_register(const WidgetsRuntimeWidget* const* widget_list,
                            const WidgetsRuntimeWidget* widget,
                            void* e,
                            void* user_data);
