#include "tr.h"

#define LOG_TAG "tr"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_DEBUG
#include "log.h"

#define MAX_LINE_SIZE 4096
static uint8_t lang = 0;
static FILE *tr_fp = NULL;
static tr_bin_header_t tr_bin_header;
static uint8_t font_weight_list[MAX_FONT_NUMBERS] = {0};
static uint8_t font_style_list[MAX_FONT_NUMBERS] = {0};
lv_font_t *lv_font[MAX_FONT_NUMBERS] = {};
static uint8_t font_numbers = 0;

static uint32_t lang_offset_list[16];

static int face_table[sizeof(language_infomation) / sizeof(language_infomation[0])][10] = {
    {5, 4, 7, 6, 3, 2, 1, 0}, // SIMPLIFIED_CHINESE
    {7, 6, 5, 4, 3, 2, 1, 0}, // ENGLISH
    {7, 6, 5, 4, 3, 2, 1, 0}, // SPANISH
    {7, 6, 5, 4, 3, 2, 1, 0}, // FRENCH
    {7, 6, 5, 4, 3, 2, 1, 0}, // ITALIANO
    {7, 6, 5, 4, 3, 2, 1, 0}, // RUSSIAN
    {7, 6, 5, 4, 3, 2, 1, 0}, // DEUTSCH
    {1, 0, 7, 6, 5, 4, 3, 2}, // JAPANESE
    {3, 2, 7, 6, 5, 4, 1, 0}, // KOREAN
    {7, 6, 5, 4, 3, 2, 1, 0}, // TURKISH
    {7, 6, 5, 4, 3, 2, 1, 0}, // UKRAINIAN
};

int tr_get_face_via_unicode(uint32_t unicode, int bold)
{
    // 谚文字母
    if ((unicode >= 0x1100 && unicode <= 0x11FF) ||
        (unicode >= 0x3130 && unicode <= 0x318F) ||
        (unicode >= 0xA960 && unicode <= 0xA97F) ||
        (unicode >= 0xAC00 && unicode <= 0xD7AF))
    {
        if (bold)
            return 2;
        else
            return 3;
    }
    // 片假文
    else if ((unicode >= 0x3040 && unicode <= 0x309f) ||
             (unicode >= 0x30A0 && unicode <= 0x30FF) ||
             (unicode >= 0x31F0 && unicode <= 0x31FF))
    {
        if (bold)
            return 0;
        else
            return 1;
    }

    return -1;
}

int tr_get_face(int idx, int bold)
{
    if (bold)
        return face_table[lang][2 * idx + 1];
    else
        return face_table[lang][2 * idx];
}

int tr_init(void)
{
    int ret = 0;
    lv_ft_info_t lv_ft_font;
    uint8_t font_weight_temp_list[MAX_FONT_NUMBERS] = {0};
    uint8_t font_temp_len = 0;
    // 获取字体大小列表
    tr_fp = fopen(TRANSLATION_RESOURCES_PATH, "rb");
    if (tr_fp == NULL)
    {
        ret = -1;
    }
    else
    {
        fseek(tr_fp, 0, SEEK_SET);
        if (fread(&tr_bin_header, 1, sizeof(tr_bin_header), tr_fp) != sizeof(tr_bin_header))
        {
            fclose(tr_fp);
            ret = -2;
        }
        else if (tr_bin_header.magic != TR_MAGIC)
        {
            fclose(tr_fp);
            ret = -3;
        }
        else if (fread(font_weight_temp_list, 1, tr_bin_header.font_len * sizeof(uint8_t), tr_fp) != tr_bin_header.font_len * sizeof(uint8_t))
        {
            fclose(tr_fp);
            ret = -4;
        }
        else if (fread(lang_offset_list, 1, tr_bin_header.lang_len * sizeof(uint32_t), tr_fp) != tr_bin_header.lang_len * sizeof(uint32_t))
        {
            fclose(tr_fp);
            ret = -5;
        }
    }

    if (ret < 0)
    {
        LOG_I("can't find translation");
        return -2;
    }
    return 0;
}

void tr_deinit(void)
{
}

void tr_set_language(uint8_t language)
{
    lang = language;
}

int tr_get_language(void)
{
    return lang;
}

const char *tr(uint16_t tr_index)
{
    tr_header_t tr_header;
    static char str[MAX_LINE_SIZE + 1];
    if (tr_index == 0 || tr_fp == NULL || tr_index > tr_bin_header.tr_len)
        return "";
    else
    {
        fseek(tr_fp, lang_offset_list[lang] + (tr_index - 1) * sizeof(tr_header_t), SEEK_SET);
        fread(&tr_header, 1, sizeof(tr_header), tr_fp);
        fseek(tr_fp, tr_header.offset, SEEK_SET);
        fread(str, 1, tr_header.len, tr_fp);
    }
    return str;
}

const char *tr2(uint16_t tr_index, char *str)
{
    tr_header_t tr_header;
    if (tr_index == 0 || tr_fp == NULL || tr_index > tr_bin_header.tr_len)
        return "";
    else
    {
        fseek(tr_fp, lang_offset_list[lang] + (tr_index - 1) * sizeof(tr_header_t), SEEK_SET);
        fread(&tr_header, 1, sizeof(tr_header), tr_fp);
        fseek(tr_fp, tr_header.offset, SEEK_SET);
        fread(str, 1, tr_header.len, tr_fp);
    }
    return str;
}

const char *tr3(uint16_t tr_index)
{
    tr_header_t tr_header;
    static char str[MAX_LINE_SIZE + 1];
    if (tr_index == 0 || tr_fp == NULL || tr_index > tr_bin_header.tr_len)
        return "";
    else
    {
        fseek(tr_fp, lang_offset_list[lang] + (tr_index - 1) * sizeof(tr_header_t), SEEK_SET);
        fread(&tr_header, 1, sizeof(tr_header), tr_fp);
        fseek(tr_fp, tr_header.offset, SEEK_SET);
        fread(str, 1, tr_header.len, tr_fp);
    }
    return str;
}

lv_font_t *tr_get_font(uint8_t weight, uint8_t style)
{
    for (int i = 0; i < font_numbers; i++)
    {
        if (weight == font_weight_list[i] && style == font_style_list[i])
            return lv_font[i];
    }
    return NULL;
}

int tr_font_new(int font_weight, int font_style)
{
    lv_ft_info_t lv_ft_font;
    // 查看是否已经存在
    if (tr_get_font(font_weight, font_style) != NULL)
        return 0;
    lv_ft_font.name = FONT_RESOURCES_PATH;
    lv_ft_font.weight = font_weight;
    lv_ft_font.style = font_style;
    lv_ft_font.mem = NULL;
    if (lv_ft_font_init(&lv_ft_font) == false)
    {
        LOG_I("can't load font, weight %d\n", lv_ft_font.weight);
        return -2;
    }
    font_weight_list[font_numbers] = font_weight;
    font_style_list[font_numbers] = font_style;
    lv_font[font_numbers++] = lv_ft_font.font;
    return 0;
}
