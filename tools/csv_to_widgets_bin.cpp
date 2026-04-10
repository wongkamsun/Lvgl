#include <cctype>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../src/widgets_bin.h"

namespace {

static std::vector<std::string> split_tab(const std::string &line)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (ch == '\t') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(cur);
    return out;
}

static std::vector<std::string> split_bar(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == '|') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    out.push_back(cur);
    return out;
}

static std::string trim(std::string s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\r' || s[b] == '\n' || s[b] == '\t'))
        ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == '\t'))
        --e;
    return s.substr(b, e - b);
}

static bool looks_hex(const std::string &s)
{
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) return true;
    if (!s.empty() && s[0] == '#') return true;
    // 例如 0020、FF00 等：包含字母或前导 0 且长度>1 时当 hex
    bool has_alpha = false;
    for (char c : s) {
        if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) has_alpha = true;
    }
    if (has_alpha) return true;
    if (s.size() > 1 && s[0] == '0') return true;
    return false;
}

static bool parse_u32_any(const std::string &raw, uint32_t &out)
{
    std::string s = trim(raw);
    if (s.empty()) return false;
    int base = looks_hex(s) ? 16 : 10;
    if (!s.empty() && s[0] == '#') {
        // #AARRGGBB
        s = s.substr(1);
        base = 16;
    } else if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
        s = s.substr(2);
        base = 16;
    }
    try {
        unsigned long v = std::stoul(s, nullptr, base);
        out = (uint32_t)v;
        return true;
    } catch (...) {
        return false;
    }
}

static bool parse_i32_any(const std::string &raw, int32_t &out)
{
    std::string s = trim(raw);
    if (s.empty()) return false;
    // 支持 #AARRGGBB 这种写法：直接当 u32 再转 i32
    if (!s.empty() && s[0] == '#') {
        uint32_t u = 0;
        if (!parse_u32_any(s, u)) return false;
        out = (int32_t)u;
        return true;
    }
    try {
        long long v = std::stoll(s, nullptr, looks_hex(s) ? 16 : 10);
        out = (int32_t)v;
        return true;
    } catch (...) {
        return false;
    }
}

static void write_u32_le(std::ofstream &out, uint32_t v)
{
    const uint8_t b[4] = { (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff), (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff) };
    out.write(reinterpret_cast<const char *>(b), 4);
}

static void write_i32_le(std::ofstream &out, int32_t v)
{
    write_u32_le(out, (uint32_t)v);
}

static std::optional<std::string> read_all(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main(int argc, char **argv)
{
    const char *in_path = "ui/widget.csv";
    // 输出为参考 widget.c/widget.h 兼容的 widget.bin
    const char *out_path = "tools/widget.bin";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        in_path = argv[1];
    if (argc >= 3 && argv[2] && argv[2][0] != '\0')
        out_path = argv[2];

    auto csv = read_all(in_path);
    if (!csv) {
        std::cerr << "read csv failed: " << in_path << std::endl;
        return 1;
    }

    // 参考 widget.h：生成 widget.c 兼容的二进制布局
    // 功能码|参数：写入 widget_style_header_t{prop,value,selector=0}
    // - code 使用 WidgetsCode，避免手写数值不一致
    struct AttrOp { WidgetsCode code = (WidgetsCode)0; int32_t value = 0; };
    struct Rec {
        uint16_t window_index = 0;
        uint16_t index = 0;
        uint16_t type = 0;
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t w = 0;
        uint16_t h = 0;
        uint16_t callback_index = 0;
        uint16_t event_index = 0;
        std::vector<AttrOp> attrs;
    };

    std::vector<Rec> widgets;
    widgets.reserve(512);

    std::istringstream ss(*csv);
    std::string line;
    bool first_line = true;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty())
            continue;
        if (first_line) {
            first_line = false; // 跳过表头
            continue;
        }

        auto cols = split_tab(line);
        // - 0: 备注
        // - 1: 控件序号
        // - 2: 控件类型
        // - 3: X
        // - 4: Y
        // - 5: W
        // - 6: H
        // 属性列（可选）从第 7 列开始：有些行末尾没有 tab 时 split_tab 不会产生空字段
        if (cols.size() < 7)
            continue;

        // 固定列（按 widget.csv 的前 10 列）
        const std::string remark = cols[0];
        int32_t idx = 0, type = 0, x = 0, y = 0, w = 0, h = 0;
        if (!parse_i32_any(cols[1], idx)) continue;
        if (!parse_i32_any(cols[2], type)) continue;
        if (!parse_i32_any(cols[3], x)) continue;
        if (!parse_i32_any(cols[4], y)) continue;
        if (!parse_i32_any(cols[5], w)) continue;
        if (!parse_i32_any(cols[6], h)) continue;

        Rec ww{};
        // 当前 CSV 不再包含界面序号：统一写入 window_index=0
        ww.window_index = 0;
        ww.index = (uint16_t)idx;
        ww.type = (uint16_t)type;
        ww.x = (uint16_t)x;
        ww.y = (uint16_t)y;
        ww.w = (uint16_t)w;
        ww.h = (uint16_t)h;
        ww.callback_index = 0;
        ww.event_index = 0;

        // 属性列：功能码|参数
        for (size_t ci = 7; ci < cols.size(); ++ci) {
            const std::string cell = trim(cols[ci]);
            if (cell.empty())
                continue;
            auto parts = split_bar(cell);
            if (parts.size() == 2) {
                uint32_t code_u32 = 0;
                if (!parse_u32_any(parts[0], code_u32)) continue;
                int32_t value = 0;
                if (!parse_i32_any(parts[1], value)) continue;
                // 这里不强行校验范围：允许未来扩展功能码
                ww.attrs.push_back(AttrOp{(WidgetsCode)code_u32, value});
                continue;
            }
            // 其它格式：忽略

            // 其它格式：当前 widget.c 版本不支持，先忽略（后续可扩展为自定义 style/customer）
        }

        widgets.push_back(std::move(ww));
    }

    if (widgets.size() > 65535u) {
        std::cerr << "error: too many widgets for uint16 total_widget_numbers\n";
        return 1;
    }

    // 写 widget.c/widget.h 兼容的 widget.bin
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "open out failed: " << out_path << std::endl;
        return 1;
    }

    auto write_u16_le = [&](uint16_t v) {
        const uint8_t b[2] = { (uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff) };
        out.write(reinterpret_cast<const char*>(b), 2);
    };

    // widget_bin_header_t 占位
    const uint32_t WIDGET_MAGIC = 0x12fd1001u;
    const std::streampos header_pos = out.tellp();
    write_u32_le(out, WIDGET_MAGIC);
    write_u32_le(out, 0u);   // size（回填）
    write_u16_le(0u);        // crc（暂不算）
    write_u16_le(0u);        // window_numbers（回填）
    write_u16_le(0u);        // total_widget_numbers（回填）
    write_u16_le(0u);        // max_widget_numbers（回填）

    // 收集窗口信息
    struct WindowInfo {
        uint8_t window_index = 0;
        uint16_t window_attr = 0;
        std::vector<size_t> widget_indices; // indices into widgets[]
        uint32_t widget_offset = 0;         // file offset
    };
    std::vector<WindowInfo> windows;
    auto get_window = [&](uint16_t win_idx) -> WindowInfo* {
        for (auto& w : windows) {
            if (w.window_index == (uint8_t)win_idx) return &w;
        }
        WindowInfo nw{};
        nw.window_index = (uint8_t)win_idx;
        windows.push_back(nw);
        return &windows.back();
    };

    uint16_t max_widget_numbers = 0;
    for (size_t i = 0; i < widgets.size(); ++i) {
        auto* win = get_window(widgets[i].window_index);
        win->widget_indices.push_back(i);
        const uint16_t next = (uint16_t)(widgets[i].index + 1u);
        if (next > max_widget_numbers) max_widget_numbers = next;
    }

    const uint16_t window_numbers = (uint16_t)windows.size();
    const uint16_t total_widget_numbers = (uint16_t)widgets.size();

    for (const auto& win : windows) {
        if (win.widget_indices.size() > 255u) {
            std::cerr << "error: window_index=" << (int)win.window_index
                      << " has " << win.widget_indices.size()
                      << " widgets; window_header_t.widget_numbers is uint8_t (max 255)\n";
            return 1;
        }
    }

    // window_header_t 列表占位
    const std::streampos window_headers_pos = out.tellp();
    for (uint16_t i = 0; i < window_numbers; ++i) {
        write_u32_le(out, 0u); // widget_offset
        write_u16_le(0u); // window_attr
        out.put((char)0);      // widget_numbers
        out.put((char)0);      // window_index
    }

    // 写每个窗口的 widget 列表（widget_header_t + flag_headers + style_headers）
    for (auto& win : windows) {
        win.widget_offset = (uint32_t)out.tellp();
        for (size_t wi : win.widget_indices) {
            const auto& r = widgets[wi];
            // widget_header_t
            write_u16_le(r.index);
            write_u16_le(r.type);
            write_u16_le(r.x);
            write_u16_le(r.y);
            write_u16_le(r.w);
            write_u16_le(r.h);
            write_u16_le(r.callback_index);
            write_u16_le(r.event_index);
            write_u16_le(0u); // widget_flag_numbers
            write_u16_le((uint16_t)r.attrs.size()); // widget_style_numbers（复用为 attrs）

            // widget_style_header_t[]：与 widget.c 文件布局兼容（12 字节/条）；
            // 语义上只用 功能码+值，第三字段保留为 0（运行时忽略，不参与对象/部位选择）
            for (const auto& a : r.attrs) {
                write_u32_le(out, (uint32_t)a.code);
                write_i32_le(out, a.value);
                write_u32_le(out, 0u);
            }
        }
    }

    // 回填 window_header_t
    const std::streampos end_pos = out.tellp();
    out.seekp(window_headers_pos, std::ios::beg);
    for (const auto& win : windows) {
        write_u32_le(out, win.widget_offset);
        write_u16_le(win.window_attr);
        out.put((char)win.widget_indices.size());
        out.put((char)win.window_index);
    }

    // 回填 widget_bin_header_t
    out.seekp(header_pos, std::ios::beg);
    write_u32_le(out, WIDGET_MAGIC);
    write_u32_le(out, (uint32_t)end_pos); // size
    write_u16_le(0u); // crc（暂不算）
    write_u16_le(window_numbers);
    write_u16_le(total_widget_numbers);
    write_u16_le(max_widget_numbers);
    out.seekp(end_pos, std::ios::beg);

    std::cout << "wrote " << widgets.size() << " widgets -> " << out_path << std::endl;
    return 0;
}
