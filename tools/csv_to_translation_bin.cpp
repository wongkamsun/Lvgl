#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Keep binary layout identical to runtime tr.h (avoid including tr.h here because it depends on platform headers).
// See: `tr.h` / `tr.c`
#define TR_MAGIC 0x12fd1000

typedef struct __attribute__((packed)) tr_header_tag
{
    uint32_t offset;
    uint16_t len;
    uint8_t weight;
    uint8_t weight_index;
} tr_header_t;

typedef struct __attribute__((packed)) tr_bin_header_tag
{
    uint32_t magic;
    uint32_t size;
    uint16_t crc;
    uint8_t font_len;
    uint8_t lang_len;
    uint16_t tr_len;
} tr_bin_header_t;

static const char* language_infomation[] = {
    "中文",
    "English",
    "Español",
    "Français",
    "Italian",
    "Pусский",
    "Deutsch",
    "日本語",
    "한국어",
    "Türkçe",
    "українська",
};

namespace {

static std::string trim(std::string s)
{
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\r' || s[b] == '\n' || s[b] == '\t')) ++b;
    size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\n' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

static std::vector<std::string> split_tab(const std::string& line)
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

static std::optional<std::string> read_all(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool parse_u16(std::string_view s, uint16_t& out)
{
    std::string t = trim(std::string(s));
    if (t.empty()) return false;
    try {
        unsigned long v = std::stoul(t, nullptr, 10);
        if (v > 0xFFFFu) return false;
        out = (uint16_t)v;
        return true;
    } catch (...) {
        return false;
    }
}

static bool parse_u8(std::string_view s, uint8_t& out)
{
    uint16_t u16 = 0;
    if (!parse_u16(s, u16)) return false;
    if (u16 > 0xFFu) return false;
    out = (uint8_t)u16;
    return true;
}

template <class T>
static void write_le(std::ofstream& out, const T& v)
{
    out.write(reinterpret_cast<const char*>(&v), sizeof(T));
}

struct Row {
    uint16_t id = 0;                 // 1-based translation index
    uint8_t weight = 0;              // optional, currently not used by tr.c
    uint8_t weight_index = 0;        // optional, currently not used by tr.c
    std::vector<std::string> text;   // per language
};

} // namespace

int main(int argc, char** argv)
{
    const char* in_path = "ui/translation.csv";
    const char* out_path = "tools/translation.bin";
    if (argc >= 2 && argv[1] && argv[1][0] != '\0') in_path = argv[1];
    if (argc >= 3 && argv[2] && argv[2][0] != '\0') out_path = argv[2];

    auto csv = read_all(in_path);
    if (!csv) {
        std::cerr << "read csv failed: " << in_path << "\n";
        return 1;
    }

    // Language columns in translation.csv are expected to match language_infomation[] length.
    constexpr uint8_t kLangCount = (uint8_t)(sizeof(language_infomation) / sizeof(language_infomation[0]));

    std::vector<Row> rows;
    rows.reserve(1024);

    std::istringstream ss(*csv);
    std::string line;
    bool first_line = true;
    uint16_t max_id = 0;

    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (first_line) {
            first_line = false; // header
            continue;
        }

        auto cols = split_tab(line);
        // Expected columns:
        // 0: 字符串序号
        // 1: 字号大小 (optional/unused)
        // 2..(2+kLangCount-1): languages
        if (cols.size() < (size_t)(2 + kLangCount)) continue;

        uint16_t id = 0;
        if (!parse_u16(cols[0], id)) continue;
        if (id == 0) continue;
        if (id > max_id) max_id = id;

        Row r{};
        r.id = id;

        // If col[1] is numeric, store as weight (best-effort); otherwise keep 0.
        // This doesn't affect current tr() behavior.
        (void)parse_u8(cols[1], r.weight);
        r.weight_index = 0;

        r.text.resize(kLangCount);
        for (uint8_t li = 0; li < kLangCount; ++li) {
            r.text[li] = cols[2 + li];
        }

        rows.push_back(std::move(r));
    }

    if (max_id == 0) {
        std::cerr << "no valid rows in: " << in_path << "\n";
        return 2;
    }

    // Build dense table [1..max_id] for each language; missing ids become empty strings.
    struct Entry {
        std::string s;
        uint8_t weight = 0;
        uint8_t weight_index = 0;
    };
    std::vector<std::vector<Entry>> table(kLangCount, std::vector<Entry>(max_id + 1));
    for (const auto& r : rows) {
        if (r.id > max_id) continue;
        for (uint8_t li = 0; li < kLangCount; ++li) {
            table[li][r.id].s = r.text[li];
            table[li][r.id].weight = r.weight;
            table[li][r.id].weight_index = r.weight_index;
        }
    }

    // Prepare output stream
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "open out failed: " << out_path << "\n";
        return 3;
    }

    // Header placeholder
    const std::streampos header_pos = out.tellp();
    tr_bin_header_t hdr{};
    hdr.magic = TR_MAGIC;
    hdr.size = 0;
    hdr.crc = 0;
    hdr.font_len = 0;              // current runtime doesn't really use this list
    hdr.lang_len = kLangCount;
    hdr.tr_len = max_id;
    write_le(out, hdr);

    // font_weight_temp_list (font_len bytes) - empty for now

    // lang_offset_list placeholders (lang_len * u32)
    const std::streampos lang_offsets_pos = out.tellp();
    std::vector<uint32_t> lang_offsets(kLangCount, 0);
    out.write(reinterpret_cast<const char*>(lang_offsets.data()), (std::streamsize)(kLangCount * sizeof(uint32_t)));

    // For each language: write tr_header_t[max_id]
    // Then append string pool; each string is stored as UTF-8 bytes plus a trailing '\0'.
    // `tr()` reads len bytes without forcing '\0', so we include it to be safe.
    std::vector<std::vector<tr_header_t>> headers(kLangCount, std::vector<tr_header_t>(max_id));

    for (uint8_t li = 0; li < kLangCount; ++li) {
        lang_offsets[li] = (uint32_t)out.tellp();
        // Reserve space for headers
        out.write(reinterpret_cast<const char*>(headers[li].data()), (std::streamsize)(headers[li].size() * sizeof(tr_header_t)));

        // Write strings, fill header offsets
        for (uint16_t id = 1; id <= max_id; ++id) {
            const auto& e = table[li][id];
            std::string bytes = e.s;
            bytes.push_back('\0');
            const uint32_t off = (uint32_t)out.tellp();
            if (bytes.size() > 0xFFFFu) {
                std::cerr << "string too long (id=" << id << ", lang=" << (int)li << ")\n";
                return 4;
            }
            out.write(bytes.data(), (std::streamsize)bytes.size());

            tr_header_t th{};
            th.offset = off;
            th.len = (uint16_t)bytes.size();
            th.weight = e.weight;
            th.weight_index = e.weight_index;
            headers[li][id - 1] = th;
        }

        // Go back and write headers for this language
        const std::streampos end_lang_pos = out.tellp();
        out.seekp((std::streamoff)lang_offsets[li], std::ios::beg);
        out.write(reinterpret_cast<const char*>(headers[li].data()), (std::streamsize)(headers[li].size() * sizeof(tr_header_t)));
        out.seekp(end_lang_pos, std::ios::beg);
    }

    // Fill lang_offset_list
    const std::streampos end_pos = out.tellp();
    out.seekp(lang_offsets_pos, std::ios::beg);
    out.write(reinterpret_cast<const char*>(lang_offsets.data()), (std::streamsize)(kLangCount * sizeof(uint32_t)));

    // Fill header size
    hdr.size = (uint32_t)end_pos;
    out.seekp(header_pos, std::ios::beg);
    write_le(out, hdr);
    out.seekp(end_pos, std::ios::beg);

    std::cout << "wrote tr_len=" << (unsigned)hdr.tr_len
              << " lang_len=" << (unsigned)hdr.lang_len
              << " -> " << out_path << " (" << hdr.size << " bytes)\n";
    return 0;
}

