#pragma once

#include <functional>
#include <string>
#include <vector>

#include "lvgl/lvgl.h"

#include "widget.h"

/**
 * 一个 bin 文件对应一个界面（window）。
 * - windowIndex: 由调用方定义的逻辑界面序号
 * - binPaths[windowIndex]: 对应该界面的 widgets bin 文件路径
 *
 * 切换策略：
 * - 每次切换创建一个 `screenRoot_` 容器，所有控件都挂在它下面
 * - 切换时直接 `lv_obj_del(screenRoot_)`，即可删除整个界面
 */
class WinManager {
public:
    using WindowCallback = std::function<void(WidgetsBuildResult&)>;
    /** 在即将删除当前界面 LVGL 对象树之前调用（用于业务解绑/保存状态）。 */
    using WindowDestroyCallback = std::function<void()>;

    bool init(lv_obj_t* root, std::vector<std::string> binPaths, std::string& err);
    /** 可选：为每个 windowIndex 指定专用的 translation.bin 路径（空串表示不切换翻译）。 */
    bool set_translation_bins(std::vector<std::string> trBinPaths, std::string& err);
    /** 设置翻译语言（0..lang_len-1）。会影响后续 switch_to/push_win/pop_win 的翻译选择。 */
    void set_translation_language(uint8_t lang) { curTrLang_ = lang; }

    /** 注册某个 windowIndex 的“构建完成回调”（每次 switch_to 成功都会调用一次）。 */
    bool register_window_callback(int windowIndex, WindowCallback cb, std::string& err);

    /** 注册某个 windowIndex 的“销毁前回调”（离开该界面前、lv_obj_del 之前调用一次）。 */
    bool register_window_destroy_callback(int windowIndex, WindowDestroyCallback cb, std::string& err);

    /**
     * 与 widgets_register_window_common 配套：保存窗口级 synthetic 回调指针，
     * 供 dispatch_periodic_ui_updates() 派发 APP_LV_EVENT_UPDATE（等同原工程每帧 LV_EVENT_UPDATE）。
     */
    bool register_window_event_sink(int windowIndex, widgets_window_callback_t cb, void* user_data, std::string& err);

    /** 可选：第二路 UPDATE（如原工程 window 0 + sys_widget_list）；未设置则忽略。 */
    void set_sys_update_sink(const WidgetsBuildResult* build, widgets_window_callback_t cb, void* user_data);
    void clear_sys_update_sink();

    /** 主循环每轮调用：对当前界面 + 可选 sys 各派发一次 APP_LV_EVENT_UPDATE。 */
    void dispatch_periodic_ui_updates();

    bool switch_to(int windowIndex, std::string& err);

    /**
     * 弹窗式压栈：不销毁当前窗口，保留在栈里；新窗口作为 overlay 覆盖在当前窗口之上。
     * 要求 windowIndex 已在 main 中注册过回调（register_window_callback / destroy_callback）。
     */
    bool push_win(int windowIndex, std::string& err);

    /**
     * 弹栈：销毁当前 overlay 窗口（会触发 destroy_callback + lv_obj_del），恢复上一层窗口（不重建）。
     */
    bool pop_win(std::string& err);

    int current() const { return current_; }
    int last() const { return last_; }

    lv_obj_t* screen_root() const { return screenRoot_; }
    const WidgetsBuildResult* current_build() const { return hasBuild_ ? &build_ : nullptr; }

private:
    const char* bin_path_for(int windowIndex) const;
    const char* tr_path_for(int windowIndex) const;
    bool apply_translation_for(int windowIndex, std::string& err);
    void destroy_current();
    void destroy_stack_all();
    void clear_build();

private:
    struct StackedScreen {
        int windowIndex = -1;
        lv_obj_t* screenRoot = nullptr;
        WidgetsBuildResult build{};
        bool hasBuild = false;
        std::string trPath;
        uint8_t trLang = 0;
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* screenRoot_ = nullptr;
    std::vector<std::string> binPaths_; // index -> path
    std::vector<std::string> trBinPaths_; // index -> translation.bin path
    std::vector<WindowCallback> callbacks_; // index -> created
    std::vector<WindowDestroyCallback> destroy_callbacks_; // index -> will_destroy
    std::vector<widgets_window_callback_t> event_sink_cb_;
    std::vector<void*> event_sink_ud_;
    const WidgetsBuildResult* sys_update_build_ = nullptr;
    widgets_window_callback_t sys_update_cb_ = nullptr;
    void* sys_update_ud_ = nullptr;
    WidgetsBuildResult build_{};
    bool hasBuild_ = false;
    int current_ = -1;
    int last_ = -1;
    std::vector<StackedScreen> stack_;
    std::string curTrPath_{};
    uint8_t curTrLang_ = 0;
};

/**
 * 主线程节拍里「业务 ui_loop」阶段（先于 lv_timer_handler）：
 * 1) 可选 pre_tick（等同 app_update）
 * 2) WinManager::dispatch_periodic_ui_updates（等同 window_send_update_event + sys）
 */
class UiThreadTick {
public:
    explicit UiThreadTick(WinManager& win) : win_(win) {}

    void set_pre_tick(std::function<void()> cb) { pre_tick_ = std::move(cb); }

    void run_ui_loop_phase()
    {
        if (pre_tick_)
            pre_tick_();
        win_.dispatch_periodic_ui_updates();
    }

private:
    WinManager& win_;
    std::function<void()> pre_tick_;
};

/** 用户输入时调用 mark_activity；配合周期 lv_timer 可做「空闲多久」判断（类原 UPDATE_IDLE_TIMER 场景）。 */
class UiIdleActivity {
public:
    UiIdleActivity() { mark_activity(); }

    void mark_activity() { last_activity_tick_ = lv_tick_get(); }

    uint32_t idle_ms() const { return lv_tick_elaps(last_activity_tick_); }

private:
    uint32_t last_activity_tick_ = 0;
};

/** 用 std::function 包一层 lv_timer；删除请用 ui_lv_timer_del，否则会泄漏 FnBox。 */
lv_timer_t* ui_lv_timer_create_periodic(uint32_t period_ms, std::function<void()> fn);
void ui_lv_timer_del(lv_timer_t* timer);

