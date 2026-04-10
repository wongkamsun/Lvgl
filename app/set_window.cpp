#include "set_window.h"

#include <iostream>
#include <string>

#include "../src/win.h"
#include "../src/widget.h"

namespace {

constexpr int kWindow2 = 2;
constexpr int kBackToWindow1 = 1;
constexpr int kBtnOperate1Id = 8;

} // namespace

void app_set_window_register(const WidgetsRuntimeWidget* const* widget_list,
                            const WidgetsRuntimeWidget* widget,
                            void* e,
                            void* user_data)
{
    auto* win = static_cast<WinManager*>(user_data);
    auto* ev = static_cast<lv_event_t*>(e);
    (void)widget_list;

    switch (lv_event_get_code(ev)) {
    case APP_LV_EVENT_CREATED:
        std::cout << "app window2: CREATED\n";
        break;
    case APP_LV_EVENT_DESTROYED:
        std::cout << "app window2: DESTROYED\n";
        break;
    case APP_LV_EVENT_UPDATE:
        break;
    case LV_EVENT_CLICKED:
        if (!widget) break;
        switch (widget->id) {
        case 2: {
            std::string err;
            if (win && !win->pop_win(err) && !err.empty())
                std::cerr << "window2: switch_to(1) failed: " << err << std::endl;
        } break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}
