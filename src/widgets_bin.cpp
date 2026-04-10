#include "widgets_bin.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

static bool read_all(const char* path, std::vector<uint8_t>& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    in.seekg(0, std::ios::end);
    std::streampos len = in.tellg();
    if (len <= 0) return false;
    out.resize((size_t)len);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(out.data()), (std::streamsize)out.size());
    return in.good();
}

static uint16_t read_u16_le(const uint8_t* p)
{
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t* p)
{
    return (int32_t)read_u32_le(p);
}

static bool check_range(size_t off, size_t bytes, size_t total)
{
    return off <= total && bytes <= total && off + bytes <= total;
}

} // namespace

bool widgets_bin_load(const char* path, std::vector<Widgets>& out, std::string& err)
{
    out.clear();
    err.clear();

    std::vector<uint8_t> all;
    if (!read_all(path, all)) {
        err = "read bin failed";
        return false;
    }

    // 参考 widget.h 的 widget_bin_header_t
    // magic(uint32) size(uint32) crc(uint16) window_numbers(uint16) total_widget_numbers(uint16) max_widget_numbers(uint16)
    const size_t header_bytes = 4 + 4 + 2 + 2 + 2 + 2;
    if (all.size() < header_bytes) {
        err = "bin too small";
        return false;
    }

    const uint32_t magic = read_u32_le(&all[0]);
    const uint32_t WIDGET_MAGIC = 0x12fd1001u;
    if (magic != WIDGET_MAGIC) {
        err = "bad magic";
        return false;
    }

    const uint32_t size_u32 = read_u32_le(&all[4]);
    if (size_u32 != 0 && size_u32 > all.size()) {
        err = "bad size";
        return false;
    }

    const uint16_t window_numbers = read_u16_le(&all[10]);
    const uint16_t total_widget_numbers = read_u16_le(&all[12]);
    (void)total_widget_numbers;

    // window_header_t: widget_offset(uint32) window_attr(uint16) widget_numbers(uint8) window_index(uint8)
    const size_t window_header_bytes = 4 + 2 + 1 + 1;
    const size_t window_headers_begin = header_bytes;
    const size_t window_headers_bytes = (size_t)window_numbers * window_header_bytes;
    if (!check_range(window_headers_begin, window_headers_bytes, all.size())) {
        err = "bad window headers";
        return false;
    }

    for (uint16_t wi = 0; wi < window_numbers; ++wi) {
        const size_t off = window_headers_begin + (size_t)wi * window_header_bytes;
        const uint32_t widget_offset = read_u32_le(&all[off + 0]);
        const uint8_t widget_numbers = all[off + 6];
        const uint8_t window_index = all[off + 7];

        if (widget_offset == 0 || widget_numbers == 0) continue;
        if (!check_range((size_t)widget_offset, 0, all.size())) continue;

        size_t p = (size_t)widget_offset;
        for (uint8_t i = 0; i < widget_numbers; ++i) {
            // widget_header_t（packed, 10*uint16）
            const size_t wh_bytes = 2 * 10;
            if (!check_range(p, wh_bytes, all.size())) {
                err = "bad widget header range";
                out.clear();
                return false;
            }

            const uint16_t index = read_u16_le(&all[p + 0]);
            const uint16_t type = read_u16_le(&all[p + 2]);
            const uint16_t x = read_u16_le(&all[p + 4]);
            const uint16_t y = read_u16_le(&all[p + 6]);
            const uint16_t w = read_u16_le(&all[p + 8]);
            const uint16_t h = read_u16_le(&all[p + 10]);
            const uint16_t flag_numbers = read_u16_le(&all[p + 16]);
            const uint16_t style_numbers = read_u16_le(&all[p + 18]);
            p += wh_bytes;

            Widgets rec{};
            rec.base.index = (int32_t)index;
            rec.base.type = (int32_t)type;
            rec.base.x = (int32_t)x;
            rec.base.y = (int32_t)y;
            rec.base.w = (int32_t)w;
            rec.base.h = (int32_t)h;

            // flags：widget_flag_header_t（flag(uint32) mask(uint32)）
            const size_t fh_bytes = 8;
            const size_t flags_bytes = (size_t)flag_numbers * fh_bytes;
            if (!check_range(p, flags_bytes, all.size())) {
                err = "bad flags range";
                out.clear();
                return false;
            }
            rec.flags.resize(flag_numbers);
            for (uint16_t fi = 0; fi < flag_numbers; ++fi) {
                const size_t fo = p + (size_t)fi * fh_bytes;
                rec.flags[fi].flag = read_u32_le(&all[fo + 0]);
                rec.flags[fi].mask = read_u32_le(&all[fo + 4]);
            }
            p += flags_bytes;

            // 与 widget_style_header_t 同布局 12 字节：仅使用 code(=prop 字段) + value；selector 忽略
            const size_t sh_bytes = 12;
            const size_t styles_bytes = (size_t)style_numbers * sh_bytes;
            if (!check_range(p, styles_bytes, all.size())) {
                err = "bad styles range";
                out.clear();
                return false;
            }
            rec.attrs.resize(style_numbers);
            rec.styles.clear();
            for (uint16_t si = 0; si < style_numbers; ++si) {
                const size_t so = p + (size_t)si * sh_bytes;
                rec.attrs[si].code = read_u32_le(&all[so + 0]);
                rec.attrs[si].value = read_i32_le(&all[so + 4]);
            }
            p += styles_bytes;

            out.push_back(std::move(rec));
        }
    }

    return true;
}

