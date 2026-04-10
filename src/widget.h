#pragma once

#include <string>
#include <vector>

#include "lvgl/lvgl.h"
#include "widgets_bin.h"

/** 与业务层 `switch (lv_event_get_code((lv_event_t*)e))` 对齐的合成事件码（仅 `code` 有效）。 */
#define APP_LV_EVENT_CREATED ((lv_event_code_t)((int)_LV_EVENT_LAST + 1))
#define APP_LV_EVENT_DESTROYED ((lv_event_code_t)((int)_LV_EVENT_LAST + 2))
#define APP_LV_EVENT_UPDATE ((lv_event_code_t)((int)_LV_EVENT_LAST + 3))

class WinManager;

/** 加载 `translation.bin`（建议传入 `ui/translation.bin` 的绝对路径）。 */
bool widgets_translation_load(const char* path, std::string& err);
/** 设置当前语言（0..lang_len-1）。 */
void widgets_translation_set_language(uint8_t lang);
/** 按翻译 id 获取 UTF-8 文本（id 从 1 开始；失败返回空串）。 */
const char* widgets_translation_get(uint16_t tr_index);

/** 初始化图片目录（要求传入 `ui/resources/images` 的绝对路径）。img_id 将按文件名排序后 1-based 映射。 */
bool widgets_images_init(const char* abs_dir, std::string& err);
/** 获取图片路径（返回形如 `A:/abs/path/to.png` 的 LVGL stdio 路径）；找不到返回空串。 */
const char* widgets_image_src(int32_t img_id);
/** 启动诊断：打印已扫描图片路径表，并对 probe_img_id 做 lv_img_decoder_get_info（输出到 stderr） */
void widgets_images_log_diagnostics(int32_t probe_img_id);

// 运行时 widget 封装：用于 window 生命周期管理/查找子对象
struct WidgetsRuntimeWidget {
    // 对应 CSV/base.index（或你定义的唯一 id）
    int32_t id = 0;

    // 主对象（控件本体）
    lv_obj_t* obj = nullptr;

    // 可选：子对象列表（例如 button 的 img/label，slider 的 knob 等）
    // 约定：下标由你后续的 obj_index 规则决定
    std::vector<lv_obj_t*> children;
};

// window 级别回调：按事件分派（CREATED / DESTROYED / UPDATE 为合成事件；CLICKED 等为真实 LVGL 事件）
typedef void (*widgets_window_callback_t)(const WidgetsRuntimeWidget* const* widget_list,
                                          const WidgetsRuntimeWidget* current_widget,
                                          void* e,
                                          void* user_data);

struct WidgetsBuildResult {
    // 通过 id 直接索引对象指针：by_id[id] -> lv_obj_t*
    // 约定：id 来自 widgets[].base.index（或你定义的唯一 id）。
    // 若 id 不连续，该数组可能包含空洞（nullptr）。
    std::vector<lv_obj_t*> by_id;
    std::vector<lv_obj_t*> list;
    /** 与 by_id 同下标：含子对象（如按钮的 img/label），供 `widget_list[id]->children[n]` 使用 */
    std::vector<WidgetsRuntimeWidget> runtime_by_id;
};

// 根据 widgets.bin 解析后的数据创建 LVGL 控件。
// - root: 默认父对象（通常传 lv_scr_act()）
// - out: 可选，返回 key->obj 映射与创建列表
// - err: 失败原因
bool widgets_build_from_bin(lv_obj_t* root,
                            const std::vector<Widgets>& widgets,
                            WidgetsBuildResult* out,
                            std::string& err);

/**
 * 绑定窗口级统一回调（widgets_window_callback_t）：
 * - 立即派发一次 APP_LV_EVENT_CREATED（current_widget 为 nullptr）
 * - 为每个控件注册 LV_EVENT_CLICKED，转发到同一回调（current_widget 为命中的逻辑控件）
 * - screen_root 删除时释放内部上下文（不要在回调里长期持有 widget_list 指针）
 */
bool widgets_window_bind(lv_obj_t* screen_root,
                         WidgetsBuildResult& build,
                         widgets_window_callback_t cb,
                         void* user_data,
                         std::string& err);

/** 手动派发合成事件（如 APP_LV_EVENT_DESTROYED / APP_LV_EVENT_UPDATE）；常与 WinManager 销毁前回调配合 */
void widgets_window_dispatch_synthetic(const WidgetsBuildResult& build,
                                         widgets_window_callback_t cb,
                                         void* user_data,
                                         lv_event_code_t code);

/**
 * 通用窗口注册：
 * - 绑定构建完成回调：内部调用 widgets_window_bind（会派发 APP_LV_EVENT_CREATED + 绑定点击转发）
 * - 绑定离开前回调：内部派发 APP_LV_EVENT_DESTROYED（在 LVGL 删树之前）
 */
bool widgets_register_window_common(WinManager& win,
                                   int windowIndex,
                                   widgets_window_callback_t cb,
                                   void* user_data,
                                   std::string& err);
