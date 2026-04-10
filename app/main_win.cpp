#include "main_win.h"

#include <iostream>
#include <string>

#include "../tools/win.h"
#include "../src/widget.h"

namespace {


} // namespace

void app_main_win_register(const WidgetsRuntimeWidget* const* widget_list,
                            const WidgetsRuntimeWidget* widget,
                            void* e,
                            void* user_data)
{
    auto* win = static_cast<WinManager*>(user_data);
    auto* ev = static_cast<lv_event_t*>(e);

    switch (lv_event_get_code(ev)) {
    case APP_LV_EVENT_CREATED:
        std::cout << "app window1: CREATED\n";
        break;
    case APP_LV_EVENT_DESTROYED:
        std::cout << "app window1: DESTROYED\n";
        break;
    case APP_LV_EVENT_UPDATE:
        break;
    case LV_EVENT_CLICKED:
        if (!widget) break;
        switch (widget->id) {
        case 2: {
            std::string err;
            if (win && !win->push_win(2, err) && !err.empty())
                std::cerr << "window1: switch_to(2) failed: " << err << std::endl;
        } break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}
