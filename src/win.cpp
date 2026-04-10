#include "win.h"

#include <functional>
#include <sstream>

#include "widget.h"
#include "widgets_bin.h"

namespace {

struct LvFnBox {
    std::function<void()> fn;
};

void lv_timer_cpp_trampoline(lv_timer_t* t)
{
    auto* box = static_cast<LvFnBox*>(t->user_data);
    if (box && box->fn)
        box->fn();
}

} // namespace

lv_timer_t* ui_lv_timer_create_periodic(uint32_t period_ms, std::function<void()> fn)
{
    auto* box = new LvFnBox{std::move(fn)};
    lv_timer_t* timer = lv_timer_create(lv_timer_cpp_trampoline, period_ms, box);
    if (!timer) {
        delete box;
        return nullptr;
    }
    return timer;
}

void ui_lv_timer_del(lv_timer_t* timer)
{
    if (!timer)
        return;
    auto* box = static_cast<LvFnBox*>(timer->user_data);
    timer->user_data = nullptr;
    lv_timer_del(timer);
    delete box;
}

bool WinManager::init(lv_obj_t* root, std::vector<std::string> binPaths, std::string& err)
{
    err.clear();
    root_ = root;
    binPaths_ = std::move(binPaths);
    trBinPaths_.clear();
    trBinPaths_.resize(binPaths_.size());
    callbacks_.clear();
    callbacks_.resize(binPaths_.size());
    destroy_callbacks_.clear();
    destroy_callbacks_.resize(binPaths_.size());
    event_sink_cb_.clear();
    event_sink_ud_.clear();
    event_sink_cb_.resize(binPaths_.size(), nullptr);
    event_sink_ud_.resize(binPaths_.size(), nullptr);
    sys_update_build_ = nullptr;
    sys_update_cb_ = nullptr;
    sys_update_ud_ = nullptr;
    clear_build();
    current_ = -1;
    last_ = -1;
    screenRoot_ = nullptr;

    if (!root_) {
        err = "root is null";
        return false;
    }
    if (binPaths_.empty()) {
        err = "binPaths is empty";
        return false;
    }
    return true;
}

void WinManager::clear_build()
{
    build_.by_id.clear();
    build_.list.clear();
    build_.runtime_by_id.clear();
    hasBuild_ = false;
}

bool WinManager::register_window_callback(int windowIndex, WindowCallback cb, std::string& err)
{
    err.clear();
    if (windowIndex < 0) {
        err = "windowIndex < 0";
        return false;
    }
    const size_t idx = (size_t)windowIndex;
    if (idx >= callbacks_.size()) {
        std::ostringstream ss;
        ss << "windowIndex out of range: " << windowIndex;
        err = ss.str();
        return false;
    }
    callbacks_[idx] = std::move(cb);
    return true;
}

bool WinManager::register_window_event_sink(int windowIndex, widgets_window_callback_t cb, void* user_data, std::string& err)
{
    err.clear();
    if (windowIndex < 0) {
        err = "windowIndex < 0";
        return false;
    }
    const size_t idx = (size_t)windowIndex;
    if (idx >= event_sink_cb_.size()) {
        std::ostringstream ss;
        ss << "windowIndex out of range: " << windowIndex;
        err = ss.str();
        return false;
    }
    event_sink_cb_[idx] = cb;
    event_sink_ud_[idx] = user_data;
    return true;
}

void WinManager::set_sys_update_sink(const WidgetsBuildResult* build, widgets_window_callback_t cb, void* user_data)
{
    sys_update_build_ = build;
    sys_update_cb_ = cb;
    sys_update_ud_ = user_data;
}

void WinManager::clear_sys_update_sink()
{
    sys_update_build_ = nullptr;
    sys_update_cb_ = nullptr;
    sys_update_ud_ = nullptr;
}

void WinManager::dispatch_periodic_ui_updates()
{
    if (hasBuild_ && current_ >= 0) {
        const size_t i = (size_t)current_;
        if (i < event_sink_cb_.size() && event_sink_cb_[i])
            widgets_window_dispatch_synthetic(build_, event_sink_cb_[i], event_sink_ud_[i], APP_LV_EVENT_UPDATE);
    }
    if (sys_update_build_ && sys_update_cb_)
        widgets_window_dispatch_synthetic(*sys_update_build_, sys_update_cb_, sys_update_ud_, APP_LV_EVENT_UPDATE);
}

bool WinManager::register_window_destroy_callback(int windowIndex, WindowDestroyCallback cb, std::string& err)
{
    err.clear();
    if (windowIndex < 0) {
        err = "windowIndex < 0";
        return false;
    }
    const size_t idx = (size_t)windowIndex;
    if (idx >= destroy_callbacks_.size()) {
        std::ostringstream ss;
        ss << "windowIndex out of range: " << windowIndex;
        err = ss.str();
        return false;
    }
    destroy_callbacks_[idx] = std::move(cb);
    return true;
}

bool WinManager::set_translation_bins(std::vector<std::string> trBinPaths, std::string& err)
{
    err.clear();
    if (trBinPaths.size() != binPaths_.size()) {
        err = "trBinPaths size must match binPaths size";
        return false;
    }
    trBinPaths_ = std::move(trBinPaths);
    return true;
}

const char* WinManager::bin_path_for(int windowIndex) const
{
    if (windowIndex < 0) return nullptr;
    const size_t idx = (size_t)windowIndex;
    if (idx >= binPaths_.size()) return nullptr;
    if (binPaths_[idx].empty()) return nullptr;
    return binPaths_[idx].c_str();
}

const char* WinManager::tr_path_for(int windowIndex) const
{
    if (windowIndex < 0) return nullptr;
    const size_t idx = (size_t)windowIndex;
    if (idx >= trBinPaths_.size()) return nullptr;
    if (trBinPaths_[idx].empty()) return nullptr;
    return trBinPaths_[idx].c_str();
}

bool WinManager::apply_translation_for(int windowIndex, std::string& err)
{
    err.clear();
    const char* tp = tr_path_for(windowIndex);
    if (!tp) return true; // 不切换
    if (!widgets_translation_load(tp, err)) return false;
    widgets_translation_set_language(curTrLang_);
    curTrPath_ = tp;
    return true;
}

void WinManager::destroy_current()
{
    if (screenRoot_) {
        lv_obj_del(screenRoot_);
        screenRoot_ = nullptr;
    }
    clear_build();
}

void WinManager::destroy_stack_all()
{
    // 栈里的窗口也需要释放（防止 push 后再 switch 造成泄漏）
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
        if (it->hasBuild && it->windowIndex >= 0) {
            const size_t idx = (size_t)it->windowIndex;
            if (idx < destroy_callbacks_.size() && destroy_callbacks_[idx])
                destroy_callbacks_[idx]();
        }
        if (it->screenRoot) {
            lv_obj_del(it->screenRoot);
            it->screenRoot = nullptr;
        }
    }
    stack_.clear();
}

bool WinManager::switch_to(int windowIndex, std::string& err)
{
    err.clear();

    const char* path = bin_path_for(windowIndex);
    if (!path) {
        std::ostringstream ss;
        ss << "no bin path for windowIndex=" << windowIndex;
        err = ss.str();
        return false;
    }

    if (current_ == windowIndex && screenRoot_) {
        return true;
    }

    if (!apply_translation_for(windowIndex, err)) return false;

    // 若之前有 push 栈，switch_to 视为“完全切换”，需要清理隐藏栈
    if (!stack_.empty()) {
        destroy_stack_all();
    }

    // 1) 离开当前界面前：业务销毁回调（仍在 lv_obj_del 之前）
    if (screenRoot_ && current_ >= 0) {
        const size_t cur = (size_t)current_;
        if (cur < destroy_callbacks_.size() && destroy_callbacks_[cur])
            destroy_callbacks_[cur]();
    }

    // 2) destroy current screen (LVGL)
    destroy_current();

    // 3) create new screen root container under root_
    screenRoot_ = lv_obj_create(root_);
    lv_obj_set_size(screenRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(screenRoot_, 0, 0);
    lv_obj_set_style_pad_all(screenRoot_, 0, 0);
    lv_obj_set_style_border_width(screenRoot_, 0, 0);
    lv_obj_set_style_radius(screenRoot_, 0, 0);
    lv_obj_set_style_bg_opa(screenRoot_, LV_OPA_TRANSP, 0);

    // 4) load widgets from bin
    std::string load_err;
    std::vector<Widgets> widgets;
    if (!widgets_bin_load(path, widgets, load_err)) {
        destroy_current();
        err = std::string("widgets_bin_load failed: ") + load_err;
        return false;
    }

    // 5) build LVGL objects under screenRoot_
    std::string build_err;
    if (!widgets_build_from_bin(screenRoot_, widgets, &build_, build_err)) {
        destroy_current();
        err = std::string("widgets_build_from_bin failed: ") + build_err;
        return false;
    }

    last_ = current_;
    current_ = windowIndex;
    hasBuild_ = true;

    // 6) notify created callback
    if ((size_t)windowIndex < callbacks_.size() && callbacks_[(size_t)windowIndex]) {
        callbacks_[(size_t)windowIndex](build_);
    }
    return true;
}

bool WinManager::push_win(int windowIndex, std::string& err)
{
    err.clear();

    const char* path = bin_path_for(windowIndex);
    if (!path) {
        std::ostringstream ss;
        ss << "no bin path for windowIndex=" << windowIndex;
        err = ss.str();
        return false;
    }
    if (windowIndex < 0 || (size_t)windowIndex >= callbacks_.size() || !callbacks_[(size_t)windowIndex]) {
        std::ostringstream ss;
        ss << "windowIndex not registered (no created callback): " << windowIndex;
        err = ss.str();
        return false;
    }

    // 先切换到 overlay 对应翻译（保证 CREATED 阶段拿到正确语言/表）
    if (!apply_translation_for(windowIndex, err)) return false;

    // 把当前窗口压栈（不销毁、不隐藏；实现“弹窗覆盖”的效果）
    if (screenRoot_) {
        StackedScreen s{};
        s.windowIndex = current_;
        s.screenRoot = screenRoot_;
        s.build = std::move(build_);
        s.hasBuild = hasBuild_;
        s.trPath = curTrPath_;
        s.trLang = curTrLang_;
        stack_.push_back(std::move(s));
    }

    // 创建 overlay 窗口（覆盖在最上层）
    clear_build();
    screenRoot_ = lv_obj_create(root_);
    lv_obj_set_size(screenRoot_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(screenRoot_, 0, 0);
    lv_obj_set_style_pad_all(screenRoot_, 0, 0);
    lv_obj_set_style_border_width(screenRoot_, 0, 0);
    lv_obj_set_style_radius(screenRoot_, 0, 0);
    lv_obj_set_style_bg_opa(screenRoot_, LV_OPA_TRANSP, 0);
    // 防点穿：让 overlay 根容器可点击以吞掉底层事件（弹窗/页面覆盖常见语义）
    lv_obj_add_flag(screenRoot_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(screenRoot_);

    std::string load_err;
    std::vector<Widgets> widgets;
    if (!widgets_bin_load(path, widgets, load_err)) {
        destroy_current();
        err = std::string("widgets_bin_load failed: ") + load_err;
        return false;
    }
    std::string build_err;
    if (!widgets_build_from_bin(screenRoot_, widgets, &build_, build_err)) {
        destroy_current();
        err = std::string("widgets_build_from_bin failed: ") + build_err;
        return false;
    }

    last_ = current_;
    current_ = windowIndex;
    hasBuild_ = true;
    callbacks_[(size_t)windowIndex](build_);
    return true;
}

bool WinManager::pop_win(std::string& err)
{
    err.clear();
    if (stack_.empty()) {
        err = "pop_win: stack empty";
        return false;
    }

    // 1) 当前窗口销毁前回调（仍在 lv_obj_del 之前）
    if (screenRoot_ && current_ >= 0) {
        const size_t cur = (size_t)current_;
        if (cur < destroy_callbacks_.size() && destroy_callbacks_[cur])
            destroy_callbacks_[cur]();
    }

    // 2) 删除当前窗口对象树
    destroy_current();

    // 3) 恢复上一层
    StackedScreen prev = std::move(stack_.back());
    stack_.pop_back();

    screenRoot_ = prev.screenRoot;
    build_ = std::move(prev.build);
    hasBuild_ = prev.hasBuild;
    last_ = current_;
    current_ = prev.windowIndex;

    // 恢复上一层对应的翻译表
    curTrLang_ = prev.trLang;
    curTrPath_ = prev.trPath;
    if (!curTrPath_.empty()) {
        std::string tr_err;
        if (!widgets_translation_load(curTrPath_.c_str(), tr_err)) {
            err = std::string("restore translation failed: ") + tr_err;
            // 窗口已恢复，翻译失败不阻断 pop；返回 false 便于上层感知
            return false;
        }
        widgets_translation_set_language(curTrLang_);
    }

    // 恢复后不重建、不重复派发 CREATED；需要刷新时业务可自行派发 APP_LV_EVENT_UPDATE
    return true;
}
