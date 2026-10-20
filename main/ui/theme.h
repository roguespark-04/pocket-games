/* Design tokens. Dark, AMOLED black canvas; iOS dark system colors; one family (Montserrat) on a ~1.2 ratio scale. */
#pragma once
#include "lvgl.h"
#define SCR 466
#define C_BG        lv_color_hex(0x000000)
#define C_SURFACE   lv_color_hex(0x1C1C1E)  /* grouped background */
#define C_SURFACE2  lv_color_hex(0x2C2C2E)
#define C_SEP       lv_color_hex(0x38383A)
#define C_LABEL     lv_color_hex(0xFFFFFF)
#define C_LABEL2    lv_color_hex(0x98989F)  /* secondary label, 7.3:1 on black, 6.2:1 on #1C1C1E */
#define C_BLUE      lv_color_hex(0x0A84FF)
#define C_GREEN     lv_color_hex(0x30D158)
#define C_RED       lv_color_hex(0xFF453A)
#define C_ORANGE    lv_color_hex(0xFF9F0A)
#define C_YELLOW    lv_color_hex(0xFFD60A)
#define C_PURPLE    lv_color_hex(0xBF5AF2)
#define C_TEAL      lv_color_hex(0x64D2FF)
#define F_CAPTION   (&lv_font_montserrat_16)   /* footnote / caption */
#define F_BODY      (&lv_font_montserrat_20)   /* body */
#define F_HEAD      (&lv_font_montserrat_24)   /* headline */
#define F_TITLE     (&lv_font_montserrat_32)   /* title */
#define F_DISPLAY   (&lv_font_montserrat_48)   /* large numbers */
/* Spacing scale (px): 4 8 12 16 24 32 48. Radius vocabulary: 12 inner, 20 outer (concentric with 8 padding), 36 app icon. */
