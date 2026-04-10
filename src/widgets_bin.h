#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 当前 widgets.bin 二进制格式版本（仅保留最新版本）。
constexpr uint32_t kWidgetsBinMagic = 0x4E424A57u;          // 'W''J''B''N' little-endian
constexpr uint32_t kWidgetsBinFormatVersion = 11u;          // v11: 增加 flags/styles ops_pool（参考 widget.c 的 flag/style 列表能力）
constexpr uint32_t kWidgetsBinRecordBytesV11 = 59u * 4u;    // v11 record_size（v10 + flags/styles offset+len）

// v11 header: WJBN(4) + version(4) + record_count(4) + record_bytes(4) + string_pool_len(4) + attr_pool_len(4) + ops_pool_len(4)
constexpr size_t kWidgetsBinHeaderBytesV11 = 28u;

// === 统一默认值/继承语义（建议所有可选样式字段默认“不覆盖”） ===
// 颜色：当字段为 kWidgetsBinColorInherit 时表示“不设置/不覆盖”
constexpr uint32_t kWidgetsBinColorInherit = 0xFFFFFFFFu;
// 圆角：当字段为 kWidgetsBinRadiusInherit 时表示“不设置/不覆盖”
constexpr int32_t kWidgetsBinRadiusInherit = -1;
// 文本/图片偏移：kWidgetsBinOffsetUnset（-1）表示未配置，运行时不同步到 LVGL
constexpr int32_t kWidgetsBinOffsetUnset = -1;


// 控件类型（对应 widgets[].base.type）
// 说明：这里的数值是“工程内约定”。
// 请把下面的枚举值调整为与你现有 type 数字一一对应。
enum class WidgetsType : int32_t {
    Unknown   = 0,
    Container = 1, // 容器
    Button    = 2, // 按键
    Image     = 3, // 图片
    Text      = 4, // 文本
    Progress  = 5, // 进度条
    Slider    = 6, // 滑条
    Table     = 7, // 表格
    TextArea  = 8, // 输入框（lv_textarea）
    Arc       = 9, // 圆弧
};

// 对齐方式（对应 widgets[].base.align、image.align：JSON 用整数 0~20，与下列一致）
enum class WidgetsAlign : int32_t {
    Center     = 0, // 居中
    LeftTop    = 1, // 左上
    LeftMid    = 2, // 左中
    LeftBottom = 3, // 左下
    RightTop   = 4, // 右上
    RightMid   = 5, // 右中
    RightBottom= 6, // 右下
    TopMid     = 7, // 上中
    BottomMid  = 8, // 下中
    // 边框外对齐（out）：相对目标对象“外侧”对齐
    OutLeftTop     = 9,  // 外-左上
    OutLeftMid     = 10, // 外-左中
    OutLeftBottom  = 11, // 外-左下
    OutRightTop    = 12, // 外-右上
    OutRightMid    = 13, // 外-右中
    OutRightBottom = 14, // 外-右下
    OutTopLeft     = 15, // 外-上左
    OutTopMid      = 16, // 外-上中
    OutTopRight    = 17, // 外-上右
    OutBottomLeft  = 18, // 外-下左
    OutBottomMid   = 19, // 外-下中
    OutBottomRight = 20, // 外-下右
};

// 文本对齐（例如 text.align）
enum class WidgetsTextAlign : int32_t {
    Auto   = 0, // 自动
    Left   = 1, // 左
    Center = 2, // 中
    Right  = 3, // 右
};

// 对象控件 Part（参考 LVGL：main/scrollbar/indicator/knob...）
// 用途：当你需要给某个 style/状态指定作用的“部位”时使用（例如只给 scrollbar 上色）。
enum class WidgetsObjPart : int32_t {
    Main        = 0,  // main
    Scrollbar   = 1,  // scrollbar
    Indicator   = 2,  // indicator
    Knob        = 3,  // knob
    Selected    = 4,  // selected
    Items       = 5,  // items
    Ticks       = 6,  // ticks
    Cursor      = 7,  // cursor
    CustomFirst = 8,  // custom_first（自定义起始）
    Any         = 9,  // any（任意 part）
};

// 控件状态（bitflag，可组合）。数值与 LVGL 的 LV_STATE_* 保持一致（1<<n）。
enum class WidgetsState : uint16_t {
    Default  = 0,
    Checked  = 1u << 0,
    Focused  = 1u << 1,
    FocusKey = 1u << 2,
    Edited   = 1u << 3,
    Hovered  = 1u << 4,
    Pressed  = 1u << 5,
    Scrolled = 1u << 6,
    Disabled = 1u << 7,
    User1    = 1u << 8,
    User2    = 1u << 9,
    User3    = 1u << 10,
    User4    = 1u << 11,
    Any      = 0xFFFFu,
};

// 属性功能码（运行时按区间选对象：40–48 主对象；60–63 主对象上的 border_*；70–76 文本对象；80–84 图片对象；90–93 主对象 + PartType 映射部位）
enum class WidgetsCode : uint16_t {
    WidgetAlign  = 40,
    WidgetBg     = 41,
    WidgetBgOpa  = 42,
    WidgetBgColor = 43,
    WidgetRadius = 44,
    WidgetPressEn = 45,
    WidgetParentId = 46,
    BorderWidth  = 60,
    BorderHeight = 61, // 预留：当前结构体未使用该字段
    BorderColor  = 62,
    BorderOpa    = 63,
    TextColor    = 70,// 文本颜色
    TextOpa      = 71,// 文本透明度
    TextFont     = 72,// 文本字体
    TextSize     = 73,// 文本大小
    TextId       = 74,// 文本ID
    TextOffsetX  = 75,// 文本偏移X
    TextOffsetY  = 76,// 文本偏移Y
    TextW        = 77,// 文本对象宽度（用于 label/btn-label/textarea 的文本对象）
    TextH        = 78,// 文本对象高度
    TextAlign    = 79,// 文本对齐（LV_TEXT_ALIGN_LEFT/CENTER/RIGHT 等）
    ImageId      = 80,// 图片ID
    ImageAlign   = 81,// 图片对齐方式
    ImageOpa     = 82,// 图片透明度
    ImageOffsetX = 83,// 图片偏移X
    ImageOffsetY = 84,// 图片偏移Y
    PartType     = 90,// 部位类型
    PartColor    = 91,// 部位颜色
    PartOpa      = 92,// 部位透明度
    PartRadius   = 93,// 部位圆角
};


// CSV 固定列（index/type/x/y/w/h 等）：用于创建控件与基础几何
struct WidgetsBase {
    int32_t index = 0;
    int32_t type = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
};

// 附加/属性相关（来自 CSV 的属性列或其它配置）：用于样式、父子、可点击等
struct WidgetsStyle {
    uint32_t bg = 0;         // 0xRRGGBB
    uint32_t pressed_bg = 0; // 0xRRGGBB
    int32_t parent_id = -1; // 父控件的id（-1 表示无父控件；0 是合法 id）
    int32_t press_en = 0;    // 是否可按压/可触控（0/1）
    int32_t radius = 0;      // 主对象圆角（LV_PART_MAIN，可被 border_style/part 覆盖）
};

// 对应参考 widget.c 里的 flag header（flag + mask：mask=1 add, mask=0 clear）
struct WidgetsFlagOp {
    uint32_t flag = 0;
    uint32_t mask = 0;
};

// 兼容旧工具：与 widget.c 的 widget_style_header_t 同布局（prop/value/selector），
// 当前工程语义上只使用「功能码 + 值」；selector 在 bin 中可为 0，加载时忽略。
struct WidgetsStyleOp {
    uint32_t prop = 0;
    int32_t value = 0;
    uint32_t selector = 0;
};

// 属性操作：功能码|参数（CSV/bin 主路径；对象由功能码区分，不依赖 selector）
struct WidgetsAttrOp {
    uint32_t code = 0;  // WidgetsCode
    int32_t value = 0;
};
struct WidgetsBorderStyle {
    int32_t width = 0;
    int32_t height = 0;
    uint32_t color = kWidgetsBinColorInherit; // 0xRRGGBB；kWidgetsBinColorInherit=不覆盖
    int32_t opa = 255;
};

// widget 独立 part 样式块（对应 JSON 的 widgets[].part）
struct WidgetsPartStyle {
    int32_t part = 0;           // 对象部位（枚举 WidgetsObjPart）
    uint32_t color = kWidgetsBinColorInherit; // 0xRRGGBB；kWidgetsBinColorInherit=不覆盖
    int32_t opa = 255;          // 透明度（0~255）
    int32_t radius = kWidgetsBinRadiusInherit; // 圆角；kWidgetsBinRadiusInherit=不覆盖 base.radius
    int32_t border_width = 0;   // 边框宽度
    uint32_t border_color = kWidgetsBinColorInherit;  // 边框颜色（0xRRGGBB）；kWidgetsBinColorInherit=不覆盖
    int32_t border_opa = 255;   // 边框透明度（0~255）
};

struct WidgetsTextStyle {
    // text
    int32_t text_w = 0;
    int32_t text_h = 0;
    int32_t text_offset_left = kWidgetsBinOffsetUnset;
    int32_t text_offset_top = kWidgetsBinOffsetUnset;
    int32_t text_offset_right = kWidgetsBinOffsetUnset;
    int32_t text_offset_bottom = kWidgetsBinOffsetUnset;
    int32_t text_opa = 255;
    uint32_t text_color = kWidgetsBinColorInherit; // 0xRRGGBB；kWidgetsBinColorInherit=不覆盖
    int32_t text_id = -1;    // 翻译表号（-1 表示 null）
    int32_t text_font = -1;
    int32_t text_size = 16;  // 字号
    int32_t text_align = -1; // LV_TEXT_ALIGN_*；-1 表示不设置
};

struct WidgetsImageStyle {
    // image
    int32_t img_id = -1;     // 图片号（-1 表示 null）
    int32_t img_align = 0;   // 图片对齐方式（枚举）
    int32_t img_offset_left = kWidgetsBinOffsetUnset;
    int32_t img_offset_top = kWidgetsBinOffsetUnset;
    int32_t img_offset_right = kWidgetsBinOffsetUnset;
    int32_t img_offset_bottom = kWidgetsBinOffsetUnset;
    int32_t img_opa = 255;
};


struct Widgets {
    WidgetsBase base;     // CSV 固定项
    WidgetsStyle style;        // UI/属性项
    WidgetsBorderStyle border_style;
    WidgetsPartStyle part_style;
    WidgetsTextStyle text_style;
    WidgetsImageStyle image_style;

    std::vector<WidgetsFlagOp> flags;
    // 保留字段：若需调试或兼容读旧语义可填充；运行时以 attrs 为准
    std::vector<WidgetsStyleOp> styles;

    // widget.bin 中 style 列表：文件里仍为 12 字节/条，第三字段忽略，只落到 code/value
    std::vector<WidgetsAttrOp> attrs;
};

// 读取 tools/json_to_widgets_bin 生成的二进制文件
bool widgets_bin_load(const char *path, std::vector<Widgets> &out, std::string &err);
