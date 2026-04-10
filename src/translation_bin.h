#pragma once

#include <cstdint>
#include <string>
#include <vector>

static_assert(sizeof(uint32_t) == 4, "u32 size");

struct _lv_font_t;

/** 将 TextSize(73) 映射到 LVGL 内置字体（最接近的字号）；size<=0 返回 nullptr。 */
const _lv_font_t* translation_builtin_font_for_size(int size);

/**
 * translation.bin 的轻量 C++ 读取器（格式与 tr.c/tr.h 一致）。
 *
 * 目标：
 * - PC/模拟器侧可直接加载 `tools/translation.bin`
 * - 不依赖 tr.c 的 FILE* 全局状态，改为一次读入内存并索引
 *
 * 说明：
 * - 字符串内容按 UTF-8 存储；返回值为 std::string_view 更合适，但这里用 const char* 方便直接喂给 LVGL。
 */
class TranslationBin {
public:
    TranslationBin() = default;

    /** 加载 translation.bin 到内存；成功返回 true，失败返回 false 并填充 err。 */
    bool load(const char* path, std::string& err);

    /** 选择语言（0..lang_len-1）。越界会被钳制到 0。 */
    void set_language(uint8_t lang);
    uint8_t language() const { return lang_; }

    /** 翻译条目总数（tr_len）。 */
    uint16_t tr_len() const { return tr_len_; }
    uint8_t lang_len() const { return lang_len_; }

    /** 获取翻译字符串。tr_index 从 1 开始；0 或越界返回空串。 */
    const char* get(uint16_t tr_index) const;

private:
    struct __attribute__((packed)) TrHeader {
        uint32_t offset;
        uint16_t len;
        uint8_t weight;
        uint8_t weight_index;
    };
    struct __attribute__((packed)) BinHeader {
        uint32_t magic;
        uint32_t size;
        uint16_t crc;
        uint8_t font_len;
        uint8_t lang_len;
        uint16_t tr_len;
    };

    static_assert(sizeof(BinHeader) == 14, "BinHeader packing mismatch");
    static_assert(sizeof(TrHeader) == 8, "TrHeader packing mismatch");

    const TrHeader* header_ptr(uint8_t lang, uint16_t tr_index) const;

private:
    std::vector<uint8_t> data_;
    std::vector<uint32_t> lang_offsets_;
    uint8_t lang_ = 0;
    uint8_t lang_len_ = 0;
    uint16_t tr_len_ = 0;
};

