#ifndef TR_H
#define TR_H
#ifdef __cplusplus
extern "C"
{
#endif

#include "utils.h"
#include "lvgl.h"

#define MAX_FONT_NUMBERS 64
#define TRANSLATION_RESOURCES_PATH "/tools/translation.bin"
#define FONT_RESOURCES_PATH "/tools/fonts/font.ttc"

    const static char *language_infomation[] = {
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

    extern lv_font_t *lv_font[MAX_FONT_NUMBERS];

    typedef struct __attribute__((packed)) tr_header_tag
    {
        uint32_t offset;
        uint16_t len;
        uint8_t weight;
        uint8_t weight_index;
    } tr_header_t;

    typedef struct __attribute__((packed)) tr_bin_header_tag
    {
#define TR_MAGIC 0x12fd1000
        uint32_t magic;
        uint32_t size;
        uint16_t crc;
        uint8_t font_len;
        uint8_t lang_len;
        uint16_t tr_len;
    } tr_bin_header_t;

    typedef struct tr_string_tag
    {
        char *string;
        uint8_t weight_index;
    } tr_string_t;

    int tr_init(void);
    void tr_deinit(void);
    int tr_get_face_via_unicode(uint32_t unicode, int bold);
    int tr_get_face(int idx, int bold);
    const char *tr(uint16_t tr_index);
    const char *tr2(uint16_t tr_index, char *str);
    const char *tr3(uint16_t tr_index);
    lv_font_t *tr_get_font(uint8_t weight, uint8_t style);
    int tr_font_new(int font_weight, int font_style);
    void tr_set_language(uint8_t language);
    int tr_get_language(void);

#ifdef __cplusplus
}
#endif

#endif
