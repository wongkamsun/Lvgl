#include "widget.h"

#include <iostream>
#include <cstring>
#include <filesystem>
#include <algorithm>

#include "lvgl/lvgl.h"

#include "win.h"
#include "translation_bin.h"

namespace {

static TranslationBin g_tr{};
static bool g_tr_loaded = false;

static std::vector<std::string> g_img_srcs; // 1-based: g_img_srcs[img_id] -> "A:/abs/path"

static const char* tr_get(uint16_t tr_index)
{
    if (!g_tr_loaded) return "";
    return g_tr.get(tr_index);
}

static void dbg_tr_apply_once(int32_t widget_id, int32_t widget_type, int32_t text_id, const char* got, const char* after)
{
    static int n = 0;
    if (n >= 30) return;
    ++n;
    std::cerr << "[text] widget_id=" << widget_id
              << " type=" << widget_type
              << " text_id=" << text_id
              << " got=\"" << (got ? got : "") << "\""
              << " after=\"" << (after ? after : "") << "\"\n";
}

static void ensure_by_id_size(std::vector<lv_obj_t*>& by_id, int32_t id)
{
    if (id < 0) return;
    const size_t need = (size_t)id + 1u;
    if (by_id.size() < need) by_id.resize(need, nullptr);
}

static void ensure_runtime_by_id_size(std::vector<WidgetsRuntimeWidget>& v, int32_t id)
{
    if (id < 0) return;
    const size_t need = (size_t)id + 1u;
    if (v.size() < need) v.resize(need);
}

static lv_obj_t* widget_create_main_obj(lv_obj_t* parent, int32_t type)
{
    // 对齐参考 widget.c 的 type 编码：
    // 0=container,1=btn,2=image,3=label,4=bar,5=slider,7=textarea,8=arc
    switch (type) {
    case 0:  return lv_obj_create(parent);
    case 1:  return lv_btn_create(parent);
    case 2:  return lv_img_create(parent);
    case 3:  return lv_label_create(parent);
    case 4:  return lv_bar_create(parent);
    case 5:  return lv_slider_create(parent);
    case 7:  return lv_textarea_create(parent);
    case 8:  return lv_arc_create(parent);
    default: return lv_obj_create(parent);
    }
}

static void widget_create_children_if_any(int32_t type, lv_obj_t* main, WidgetsRuntimeWidget& out)
{
    // children 约定：children[0] 不使用（主对象用 out.obj）
    out.children.clear();
    out.children.resize(3, nullptr);

    if (type == 1) { // BTN：仅子 label；有 80|id 时在 sync 里设主按钮 bg_img（不预创建子 lv_img）
        out.children[2] = lv_label_create(main);
        lv_obj_set_size(out.children[2], LV_PCT(100), LV_PCT(100));
        lv_label_set_long_mode(out.children[2], LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(out.children[2], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_opa(out.children[2], LV_OPA_COVER, LV_PART_MAIN);
        /* 主题可能给 label 默认不透明底，会盖住父对象上的 bg_img */
        lv_obj_set_style_bg_opa(out.children[2], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_center(out.children[2]);
    }
}

static lv_obj_t* get_text_obj(int32_t type, const WidgetsRuntimeWidget& w)
{
    // BTN: label 在 children[2]；LABEL / TEXTAREA：文本在主对象上
    if (type == 1 && w.children.size() > 2) return w.children[2];
    if (type == 3 || type == 7) return w.obj;
    return nullptr;
}

static lv_obj_t* get_image_obj(int32_t type, const WidgetsRuntimeWidget& w)
{
    // 仅 type=2 主对象即 lv_img；按钮/容器等图片一律走主对象背景（见 sync 中 bg_img_src）
    if (type == 2) return w.obj;
    return nullptr;
}

static void apply_flag_ops(const WidgetsRuntimeWidget& w, const std::vector<WidgetsFlagOp>& flags)
{
    for (const auto& f : flags) {
        if (f.mask) lv_obj_add_flag(w.obj, (lv_obj_flag_t)f.flag);
        else        lv_obj_clear_flag(w.obj, (lv_obj_flag_t)f.flag);
    }
}

/** `WidgetsObjPart` → LVGL part（仅 90–93 段使用，不由 selector 字符串配置） */
static lv_style_selector_t widgets_obj_part_to_selector(int32_t part)
{
    switch (static_cast<WidgetsObjPart>(part)) {
    case WidgetsObjPart::Main:        return LV_PART_MAIN;
    case WidgetsObjPart::Scrollbar:   return LV_PART_SCROLLBAR;
    case WidgetsObjPart::Indicator:   return LV_PART_INDICATOR;
    case WidgetsObjPart::Knob:        return LV_PART_KNOB;
    case WidgetsObjPart::Selected:    return LV_PART_SELECTED;
    case WidgetsObjPart::Items:       return LV_PART_ITEMS;
    case WidgetsObjPart::Ticks:       return LV_PART_TICKS;
    case WidgetsObjPart::Cursor:      return LV_PART_CURSOR;
    case WidgetsObjPart::CustomFirst: return LV_PART_CUSTOM_FIRST;
    case WidgetsObjPart::Any:         return LV_PART_MAIN;
    default:                          return LV_PART_MAIN;
    }
}

/**
 * 功能码区间 → 作用对象（与 widgets_bin.h WidgetsCode 分段一致）：
 * 40–48：主对象；60–63：边框（LVGL 上为同一主对象的 border_* 属性）；70–76：文本对象；80–84：图片对象；90–93：主对象 + PartType 映射部位。
 */
static lv_obj_t* widgets_code_target_obj(uint32_t code, int32_t widget_type, const WidgetsRuntimeWidget& w)
{
    if (code >= 40u && code <= 48u) return w.obj;
    if (code >= 60u && code <= 63u) return w.obj;
    if (code >= 70u && code <= 76u) return get_text_obj(widget_type, w);
    if (code >= 80u && code <= 84u) return get_image_obj(widget_type, w);
    if (code >= 90u && code <= 93u) return w.obj;
    return w.obj;
}

/** 样式选择器：仅 90–93 使用 PartType 映射；其余段在各自目标对象上用 LV_PART_MAIN（文本/图片控件上 MAIN 即其内容区）。 */
static lv_style_selector_t widgets_code_style_selector(uint32_t code, int32_t part_enum)
{
    if (code >= 90u && code <= 93u) return widgets_obj_part_to_selector(part_enum);
    return LV_PART_MAIN;
}

/** 将 `WidgetsTextStyle` / `WidgetsImageStyle` 同步到 LVGL（文本用 MAIN 的 pad_*；图片用 lv_img 偏移）。
 *  各偏移为 kWidgetsBinOffsetUnset（-1）时跳过，不覆盖 LVGL/主题已有值。 */
static void sync_text_image_styles_to_lvgl(int32_t type,
                                             const WidgetsRuntimeWidget& w,
                                             const WidgetsTextStyle& text,
                                             const WidgetsImageStyle& image)
{
    lv_obj_t* t = get_text_obj(type, w);
    if (t) {
        // TextW/TextH 未设置时给一个默认值，避免“有文本但看不见”
        if (text.text_w > 0) {
            lv_obj_set_width(t, static_cast<lv_coord_t>(text.text_w));
        } else {
            if (type == 1) lv_obj_set_width(t, LV_PCT(100));           // btn-label：填满按钮
            else if (type == 3) lv_obj_set_width(t, LV_SIZE_CONTENT);  // label：随内容
        }

        if (text.text_h > 0) {
            lv_obj_set_height(t, static_cast<lv_coord_t>(text.text_h));
        } else {
            if (type == 1) lv_obj_set_height(t, LV_PCT(100));          // btn-label：填满按钮
            else if (type == 3) lv_obj_set_height(t, LV_SIZE_CONTENT); // label：随内容
        }

        // TextAlign(79): 既设置“文本在 label 内部的对齐”，也（对 BTN）设置 label 在父对象内的对齐
        // 这样 79|1/2/3 的语义更接近“在父控件内部左/中/右对齐显示文字”。
        const WidgetsTextAlign a = static_cast<WidgetsTextAlign>(text.text_align);
        switch (a) {
        case WidgetsTextAlign::Left:
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
            if (type == 1) lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
            break;
        case WidgetsTextAlign::Center:
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            if (type == 1) lv_obj_center(t);
            break;
        case WidgetsTextAlign::Right:
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
            if (type == 1) lv_obj_align(t, LV_ALIGN_RIGHT_MID, 0, 0);
            break;
        case WidgetsTextAlign::Auto:
        default:
            // 默认：BTN label 居中（避免不写 79 时“看不见/偏角落”）
            if (type == 1) lv_obj_center(t);
            break;
        }
        // TextSize: 交给 translation_bin 中的字号映射（最接近的内置字体）
        if (const lv_font_t* f = (const lv_font_t*)translation_builtin_font_for_size(text.text_size)) {
            lv_obj_set_style_text_font(t, f, LV_PART_MAIN);
        }

        if (text.text_offset_left != kWidgetsBinOffsetUnset)
            lv_obj_set_style_pad_left(t, static_cast<lv_coord_t>(text.text_offset_left), LV_PART_MAIN);
        if (text.text_offset_top != kWidgetsBinOffsetUnset)
            lv_obj_set_style_pad_top(t, static_cast<lv_coord_t>(text.text_offset_top), LV_PART_MAIN);
        if (text.text_offset_right != kWidgetsBinOffsetUnset)
            lv_obj_set_style_pad_right(t, static_cast<lv_coord_t>(text.text_offset_right), LV_PART_MAIN);
        if (text.text_offset_bottom != kWidgetsBinOffsetUnset)
            lv_obj_set_style_pad_bottom(t, static_cast<lv_coord_t>(text.text_offset_bottom), LV_PART_MAIN);
    }
    lv_obj_t* im = get_image_obj(type, w);
    if (image.img_id > 0) {
        const char* src = widgets_image_src(image.img_id);
        if (src && src[0] != '\0') {
            if (im) {
                lv_img_set_src(im, src);
                lv_obj_invalidate(im);
            } else {
                lv_obj_set_style_bg_img_src(w.obj, src, LV_PART_MAIN);
                lv_obj_invalidate(w.obj);
            }
        } else {
            static int warn_empty;
            if (warn_empty < 8) {
                ++warn_empty;
                std::cerr << "[widgets] widget id=" << w.id << " ImageId=" << image.img_id
                          << " but widgets_image_src empty (未扫描到图片或 id 超范围；请确认 ui/resources/images 下有 png 且已运行 csv_to_bin_all)\n";
            }
        }
    }
    if (im) {
        const auto* imgd = reinterpret_cast<const lv_img_t*>(im);
        /* lv_img_set_offset_* 内部为 value % w/h；无 src 时 w/h 为 0 会 SIGFPE */
        if (imgd->w > 0 && imgd->h > 0) {
            if (image.img_offset_left != kWidgetsBinOffsetUnset)
                lv_img_set_offset_x(im, static_cast<lv_coord_t>(image.img_offset_left));
            if (image.img_offset_top != kWidgetsBinOffsetUnset)
                lv_img_set_offset_y(im, static_cast<lv_coord_t>(image.img_offset_top));
        }
    }
}

static void apply_attr_ops(int32_t type,
                           const WidgetsRuntimeWidget& w,
                           WidgetsStyle& out_style,
                           WidgetsPartStyle& out_part,
                           WidgetsTextStyle& out_text,
                           WidgetsImageStyle& out_image,
                           const std::vector<WidgetsAttrOp>& attrs)
{
    for (const auto& a : attrs) {
        const uint32_t raw = a.code;
        const auto code = static_cast<WidgetsCode>(a.code);
        switch (code) {
        case WidgetsCode::WidgetBg:
        case WidgetsCode::WidgetBgColor: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            out_style.bg = static_cast<uint32_t>(a.value);
            lv_obj_set_style_bg_color(o, lv_color_hex(static_cast<uint32_t>(a.value) & 0x00FFFFFFu),
                                      widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::WidgetBgOpa: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_bg_opa(o, static_cast<lv_opa_t>(a.value), widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::WidgetRadius: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            out_style.radius = a.value;
            lv_obj_set_style_radius(o, a.value, widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::WidgetPressEn:
            out_style.press_en = a.value;
            if (a.value) lv_obj_add_flag(w.obj, LV_OBJ_FLAG_CLICKABLE);
            else         lv_obj_clear_flag(w.obj, LV_OBJ_FLAG_CLICKABLE);
            break;
        case WidgetsCode::WidgetParentId:
            out_style.parent_id = a.value;
            break;
        case WidgetsCode::WidgetAlign:
            lv_obj_align(w.obj, static_cast<lv_align_t>(a.value), 0, 0);
            break;

        case WidgetsCode::BorderWidth: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_border_width(o, a.value, widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::BorderColor: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_border_color(o, lv_color_hex(static_cast<uint32_t>(a.value) & 0x00FFFFFFu),
                                          widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::BorderOpa: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_border_opa(o, static_cast<lv_opa_t>(a.value), widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::BorderHeight:
            break;

        case WidgetsCode::TextColor: {
            lv_obj_t* t = widgets_code_target_obj(raw, type, w);
            if (!t) break;
            const lv_style_selector_t sel = widgets_code_style_selector(raw, out_part.part);
            lv_obj_set_style_text_color(t, lv_color_hex(static_cast<uint32_t>(a.value) & 0x00FFFFFFu), sel);
        } break;
        case WidgetsCode::TextOpa: {
            lv_obj_t* t = widgets_code_target_obj(raw, type, w);
            if (!t) break;
            lv_obj_set_style_text_opa(t, static_cast<lv_opa_t>(a.value), widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::TextId:
            out_text.text_id = a.value;
            break;
        case WidgetsCode::TextFont:
            break;
        case WidgetsCode::TextSize:
            out_text.text_size = a.value;
            break;
        case WidgetsCode::TextOffsetX:
            out_text.text_offset_left = a.value;
            break;
        case WidgetsCode::TextOffsetY:
            out_text.text_offset_top = a.value;
            break;
        case WidgetsCode::TextW:
            out_text.text_w = a.value;
            break;
        case WidgetsCode::TextH:
            out_text.text_h = a.value;
            break;
        case WidgetsCode::TextAlign:
            out_text.text_align = a.value;
            break;

        case WidgetsCode::ImageId:
            out_image.img_id = a.value;
            break;
        case WidgetsCode::ImageAlign: {
            lv_obj_t* im = widgets_code_target_obj(raw, type, w);
            if (im) lv_obj_align(im, static_cast<lv_align_t>(a.value), 0, 0);
        } break;
        case WidgetsCode::ImageOpa: {
            lv_obj_t* im = widgets_code_target_obj(raw, type, w);
            const lv_opa_t opa = static_cast<lv_opa_t>(a.value);
            if (im) {
                lv_obj_set_style_img_opa(im, opa, widgets_code_style_selector(raw, out_part.part));
            } else {
                // 背景图透明度
                lv_obj_set_style_bg_img_opa(w.obj, opa, LV_PART_MAIN);
            }
        } break;
        case WidgetsCode::ImageOffsetX:
            out_image.img_offset_left = a.value;
            break;
        case WidgetsCode::ImageOffsetY:
            out_image.img_offset_top = a.value;
            break;

        case WidgetsCode::PartType:
            out_part.part = a.value;
            break;
        case WidgetsCode::PartColor: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_bg_color(o, lv_color_hex(static_cast<uint32_t>(a.value) & 0x00FFFFFFu),
                                      widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::PartOpa: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_bg_opa(o, static_cast<lv_opa_t>(a.value), widgets_code_style_selector(raw, out_part.part));
        } break;
        case WidgetsCode::PartRadius: {
            lv_obj_t* o = widgets_code_target_obj(raw, type, w);
            if (!o) break;
            lv_obj_set_style_radius(o, a.value, widgets_code_style_selector(raw, out_part.part));
        } break;

        default:
            break;
        }
    }
    sync_text_image_styles_to_lvgl(type, w, out_text, out_image);

    // 文本翻译：若设置了 text_id（1-based），则把翻译结果写入对应文本对象
    if (out_text.text_id > 0) {
        const char* s = tr_get((uint16_t)out_text.text_id);
        if (s && s[0] != '\0') {
            if (type == 7) { // textarea
                lv_textarea_set_text(w.obj, s);
            } else {
                lv_obj_t* t = get_text_obj(type, w);
                if (t) {
                    lv_label_set_text(t, s);
                    dbg_tr_apply_once(w.id, type, out_text.text_id, s, lv_label_get_text(t));
                }
            }
        } else {
            dbg_tr_apply_once(w.id, type, out_text.text_id, s, "(empty)");
        }
    }
}

} // namespace

bool widgets_build_from_bin(lv_obj_t* root,
                            const std::vector<Widgets>& widgets,
                            WidgetsBuildResult* out,
                            std::string& err)
{
    if (!root) {
        err = "root is null";
        return false;
    }

    if (out) {
        out->by_id.clear();
        out->list.clear();
        out->runtime_by_id.clear();
    }

    // 运行时临时表：用于 parent/offset 等需要二次处理的信息
    struct PendingLayout {
        int32_t id = 0;
        int32_t parent_id = -1;
        int32_t base_x = 0;
        int32_t base_y = 0;
    };
    std::vector<PendingLayout> pending;
    pending.reserve(widgets.size());

    // 1) 创建主对象 + 子对象，应用 flags/styles，保存到 by_id/list
    for (const auto& w : widgets) {
        WidgetsRuntimeWidget rw{};
        rw.id = w.base.index;
        rw.obj = widget_create_main_obj(root, w.base.type);
        if (!rw.obj) {
            err = "lvgl create failed";
            return false;
        }
        widget_create_children_if_any(w.base.type, rw.obj, rw);

        // 基础几何
        lv_obj_set_pos(rw.obj, w.base.x, w.base.y);
        // 宽高允许为 0：按 CSV 原样设置（0 可用于隐藏/占位等）
        lv_obj_set_size(rw.obj, w.base.w, w.base.h);

        // 默认禁止“拖动/滚动”（滚动条）：仅对需要滚动的控件（如 textarea）保留默认行为
        if (w.base.type != 7) { // 7=textarea
            lv_obj_clear_flag(rw.obj, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(rw.obj, LV_SCROLLBAR_MODE_OFF);
        }

        // 对齐参考 widget.c：初始化一些默认样式，避免主题默认值干扰布局观感
        lv_obj_set_style_pad_all(rw.obj, 0, 0);
        lv_obj_set_style_radius(rw.obj, 0, 0);
        lv_obj_set_style_shadow_width(rw.obj, 0, 0);
        lv_obj_set_style_border_width(rw.obj, 0, 0);

        apply_flag_ops(rw, w.flags);
        WidgetsStyle style{};
        WidgetsPartStyle part{};
        WidgetsTextStyle text_style = w.text_style;
        WidgetsImageStyle image_style = w.image_style;
        apply_attr_ops(w.base.type, rw, style, part, text_style, image_style, w.attrs);

        pending.push_back(PendingLayout{
            .id = rw.id,
            .parent_id = style.parent_id,
            .base_x = w.base.x,
            .base_y = w.base.y,
        });

        if (out) {
            ensure_by_id_size(out->by_id, rw.id);
            ensure_runtime_by_id_size(out->runtime_by_id, rw.id);
            if (rw.id >= 0) {
                out->by_id[(size_t)rw.id] = rw.obj;
                out->runtime_by_id[(size_t)rw.id] = rw;
            }
            out->list.push_back(rw.obj);
        }

    }

    // 2) 父子关系与偏移：依赖 by_id（父对象必须已创建）
    if (out) {
        for (const auto& pl : pending) {
            const int32_t id = pl.id;
            if (id < 0) continue;
            if ((size_t)id >= out->by_id.size()) continue;
            lv_obj_t* obj = out->by_id[(size_t)id];
            if (!obj) continue;

            const int32_t pid = pl.parent_id;
            if (pid < 0) continue;
            if (pid == id) continue; // 避免 48|自身序号 导致 set_parent(自己)
            if ((size_t)pid >= out->by_id.size()) continue;
            lv_obj_t* parent = out->by_id[(size_t)pid];
            if (!parent) continue;

            lv_obj_set_parent(obj, parent);
            /* 48 挂父后：位置仅由 CSV 的 X/Y（base）表达，相对父对象内容区 */
            lv_obj_set_pos(obj, pl.base_x, pl.base_y);
        }
    }

    return true;
}

bool widgets_translation_load(const char* path, std::string& err)
{
    const bool ok = g_tr.load(path, err);
    g_tr_loaded = ok;
    return ok;
}

void widgets_translation_set_language(uint8_t lang)
{
    if (!g_tr_loaded) return;
    g_tr.set_language(lang);
}

const char* widgets_translation_get(uint16_t tr_index)
{
    return tr_get(tr_index);
}

bool widgets_images_init(const char* abs_dir, std::string& err)
{
    err.clear();
    g_img_srcs.clear();
    g_img_srcs.resize(1); // index 0 unused

    if (!abs_dir || abs_dir[0] == '\0') {
        err = "abs_dir empty";
        return false;
    }

    std::filesystem::path dir(abs_dir);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
        err = "images dir not found";
        return false;
    }

    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::end(it); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file()) continue;
        auto p = it->path();
        auto ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".gif") {
            files.push_back(p);
        }
    }
    if (ec) {
        err = "iterate images dir failed";
        return false;
    }

    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        // LV_USE_FS_STDIO 默认盘符为 'A'，可使用 "A:" + 绝对路径
        g_img_srcs.push_back(std::string("A:") + p.string());
    }
    if (files.empty()) {
        std::cerr << "[widgets_images] warning: directory has no .png/.jpg/.bmp/.gif: " << dir.string() << std::endl;
    }
    return true;
}

void widgets_images_log_diagnostics(int32_t probe_img_id)
{
    const size_t n = g_img_srcs.size() > 1 ? g_img_srcs.size() - 1 : 0;
    std::cerr << "[widgets_images] image file count=" << n;
    if (n == 0)
        std::cerr << " (将 80|id 映射不到任何文件)";
    std::cerr << std::endl;
    for (size_t i = 1; i < g_img_srcs.size(); ++i)
        std::cerr << "  img_id=" << i << " -> " << g_img_srcs[i] << std::endl;
    if (probe_img_id <= 0)
        return;
    const char* s = widgets_image_src(probe_img_id);
    if (!s || !std::strlen(s)) {
        std::cerr << "[widgets_images] probe img_id=" << probe_img_id << ": NO PATH\n";
        return;
    }
    lv_img_header_t h{};
    lv_res_t r = lv_img_decoder_get_info(s, &h);
    if (r != LV_RES_OK)
        std::cerr << "[widgets_images] probe img_id=" << probe_img_id << " decoder_get_info FAILED path=\"" << s
                  << "\" (检查 A: 盘 stdio 驱动、PNG 解码器、文件是否存在)\n";
    else
        std::cerr << "[widgets_images] probe img_id=" << probe_img_id << " decode header OK w=" << h.w << " h=" << h.h
                  << " cf=" << (int)h.cf << std::endl;
    std::cerr << "[widgets_images] 提示：默认 win.switch_to(1) 加载 ui/ui_1.bin；带 80| 的界面在 ui/ui_2.bin 时需进入窗口 2（如主页按钮 push_win(2)）\n";
}

const char* widgets_image_src(int32_t img_id)
{
    static const char* kEmpty = "";
    if (img_id <= 0) return kEmpty;
    const size_t idx = (size_t)img_id;
    if (idx >= g_img_srcs.size()) return kEmpty;
    return g_img_srcs[idx].c_str();
}

namespace widgets_window_bind_detail {

struct BindCtx {
    WidgetsBuildResult* build = nullptr;
    std::vector<const WidgetsRuntimeWidget*> ptrs;
    widgets_window_callback_t cb = nullptr;
    void* user_data = nullptr;
};

static const WidgetsRuntimeWidget* find_widget_by_target(const WidgetsBuildResult& b, lv_obj_t* target)
{
    if (!target) return nullptr;
    for (lv_obj_t* p = target; p; p = lv_obj_get_parent(p)) {
        for (const auto& rw : b.runtime_by_id) {
            if (rw.obj == p) return &rw;
        }
    }
    return nullptr;
}

static void on_click(lv_event_t* e)
{
    auto* ctx = static_cast<BindCtx*>(lv_event_get_user_data(e));
    if (!ctx || !ctx->cb || !ctx->build) return;
    const WidgetsRuntimeWidget* w = find_widget_by_target(*ctx->build, lv_event_get_target(e));
    if (!w) return;
    ctx->cb(ctx->ptrs.data(), w, e, ctx->user_data);
}

static void on_screen_delete(lv_event_t* e)
{
    auto* ctx = static_cast<BindCtx*>(lv_event_get_user_data(e));
    delete ctx;
}

} // namespace widgets_window_bind_detail

bool widgets_window_bind(lv_obj_t* screen_root,
                         WidgetsBuildResult& build,
                         widgets_window_callback_t cb,
                         void* user_data,
                         std::string& err)
{
    err.clear();
    if (!screen_root || !cb) {
        err = "screen_root/cb null";
        return false;
    }

    using widgets_window_bind_detail::BindCtx;
    auto* ctx = new BindCtx;
    ctx->build = &build;
    ctx->cb = cb;
    ctx->user_data = user_data;
    ctx->ptrs.resize(build.runtime_by_id.size());
    for (size_t i = 0; i < build.runtime_by_id.size(); ++i)
        ctx->ptrs[i] = build.runtime_by_id[i].obj ? &build.runtime_by_id[i] : nullptr;

    lv_event_t syn{};
    syn.code = APP_LV_EVENT_CREATED;
    cb(ctx->ptrs.data(), nullptr, static_cast<void*>(&syn), user_data);

    for (auto& rw : build.runtime_by_id) {
        if (rw.obj)
            lv_obj_add_event_cb(rw.obj, widgets_window_bind_detail::on_click, LV_EVENT_CLICKED, ctx);
    }
    lv_obj_add_event_cb(screen_root, widgets_window_bind_detail::on_screen_delete, LV_EVENT_DELETE, ctx);
    return true;
}

void widgets_window_dispatch_synthetic(const WidgetsBuildResult& build,
                                       widgets_window_callback_t cb,
                                       void* user_data,
                                       lv_event_code_t code)
{
    if (!cb) return;
    std::vector<const WidgetsRuntimeWidget*> ptrs(build.runtime_by_id.size());
    for (size_t i = 0; i < build.runtime_by_id.size(); ++i)
        ptrs[i] = build.runtime_by_id[i].obj ? &build.runtime_by_id[i] : nullptr;
    lv_event_t syn{};
    syn.code = code;
    cb(ptrs.data(), nullptr, static_cast<void*>(&syn), user_data);
}

bool widgets_register_window_common(WinManager& win,
                                    int windowIndex,
                                    widgets_window_callback_t cb,
                                    void* user_data,
                                    std::string& err)
{
    err.clear();
    if (!win.register_window_destroy_callback(
            windowIndex,
            [&win, cb, user_data]() {
                const WidgetsBuildResult* b = win.current_build();
                if (b) widgets_window_dispatch_synthetic(*b, cb, user_data, APP_LV_EVENT_DESTROYED);
            },
            err))
        return false;

    if (!win.register_window_callback(
            windowIndex,
            [&win, cb, user_data](WidgetsBuildResult& build) {
                std::string bind_err;
                if (!widgets_window_bind(win.screen_root(), build, cb, user_data, bind_err) && !bind_err.empty())
                    std::cerr << "widgets_window_bind: " << bind_err << std::endl;
            },
            err))
        return false;

    if (!win.register_window_event_sink(windowIndex, cb, user_data, err))
        return false;

    return true;
}
