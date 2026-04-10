#include "translation_bin.h"

#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include "lvgl/lvgl.h"

namespace {

constexpr uint32_t kTrMagic = 0x12fd1000u;

template <class T>
static bool read_pod(std::ifstream& in, T& out)
{
    in.read(reinterpret_cast<char*>(&out), (std::streamsize)sizeof(T));
    return (size_t)in.gcount() == sizeof(T);
}

static std::string read_all(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

const lv_font_t* translation_builtin_font_for_size(int size)
{
    if (size <= 0) return nullptr;
    int sz = size;
    if (sz < 12) sz = 12;
    if (sz > 48) sz = 48;

    // 取最接近的字号（仅引用本工程启用的字号集合）
    const int candidates[] = {12,14,16,18,20,24,28,32,40,48};
    int best = candidates[0];
    for (int c : candidates) {
        if (std::abs(c - sz) < std::abs(best - sz)) best = c;
    }

    switch (best) {
    case 12: return &lv_font_montserrat_12;
    case 14: return &lv_font_montserrat_14;
    case 16: return &lv_font_montserrat_16;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    case 24: return &lv_font_montserrat_24;
    case 28: return &lv_font_montserrat_28;
    case 32: return &lv_font_montserrat_32;
    case 40: return &lv_font_montserrat_40;
    case 48: return &lv_font_montserrat_48;
    default: return nullptr;
    }
}

bool TranslationBin::load(const char* path, std::string& err)
{
    err.clear();
    data_.clear();
    lang_offsets_.clear();
    lang_ = 0;
    lang_len_ = 0;
    tr_len_ = 0;

    std::string blob = read_all(path);
    if (blob.empty()) {
        err = "read failed or empty";
        return false;
    }

    data_.assign(blob.begin(), blob.end());
    if (data_.size() < sizeof(BinHeader)) {
        err = "file too small";
        return false;
    }

    const auto* hdr = reinterpret_cast<const BinHeader*>(data_.data());
    if (hdr->magic != kTrMagic) {
        err = "bad magic";
        return false;
    }
    if (hdr->size != (uint32_t)data_.size()) {
        // 允许 size 不严谨，但至少不能超过文件
        if (hdr->size > (uint32_t)data_.size()) {
            err = "size field larger than file";
            return false;
        }
    }
    if (hdr->lang_len == 0 || hdr->tr_len == 0) {
        err = "lang_len/tr_len is zero";
        return false;
    }

    lang_len_ = hdr->lang_len;
    tr_len_ = hdr->tr_len;

    // layout: BinHeader + font_len bytes + lang_len*u32 + (per-lang header+pool...)
    size_t off = sizeof(BinHeader);
    off += (size_t)hdr->font_len * sizeof(uint8_t);
    const size_t need_offsets = off + (size_t)lang_len_ * sizeof(uint32_t);
    if (need_offsets > data_.size()) {
        err = "file truncated (lang_offset_list)";
        return false;
    }

    lang_offsets_.resize(lang_len_);
    std::memcpy(lang_offsets_.data(), data_.data() + off, (size_t)lang_len_ * sizeof(uint32_t));

    // basic sanity: each lang offset points inside file and has room for headers
    const size_t headers_bytes = (size_t)tr_len_ * sizeof(TrHeader);
    for (uint8_t li = 0; li < lang_len_; ++li) {
        const uint32_t lo = lang_offsets_[li];
        if (lo >= data_.size()) {
            err = "lang_offset out of range";
            return false;
        }
        if ((size_t)lo + headers_bytes > data_.size()) {
            err = "lang header table truncated";
            return false;
        }
    }

    return true;
}

void TranslationBin::set_language(uint8_t lang)
{
    if (lang_len_ == 0) {
        lang_ = 0;
        return;
    }
    if (lang >= lang_len_) lang_ = 0;
    else lang_ = lang;
}

const TranslationBin::TrHeader* TranslationBin::header_ptr(uint8_t lang, uint16_t tr_index) const
{
    if (tr_index == 0) return nullptr;
    if (lang_len_ == 0 || tr_len_ == 0) return nullptr;
    if (lang >= lang_len_) return nullptr;
    if (tr_index > tr_len_) return nullptr;

    const uint32_t base = lang_offsets_[lang];
    const size_t off = (size_t)base + (size_t)(tr_index - 1u) * sizeof(TrHeader);
    if (off + sizeof(TrHeader) > data_.size()) return nullptr;
    return reinterpret_cast<const TrHeader*>(data_.data() + off);
}

const char* TranslationBin::get(uint16_t tr_index) const
{
    static const char* kEmpty = "";
    const TrHeader* h = header_ptr(lang_, tr_index);
    if (!h) return kEmpty;
    if ((size_t)h->offset >= data_.size()) return kEmpty;
    if ((size_t)h->offset + (size_t)h->len > data_.size()) return kEmpty;
    return reinterpret_cast<const char*>(data_.data() + h->offset);
}

