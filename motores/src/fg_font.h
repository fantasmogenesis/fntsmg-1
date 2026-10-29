/* generado por firmware/tools/gen_font.py: no editar */
#pragma once
#include <stdint.h>
typedef struct { uint16_t cp; uint8_t w, h; int8_t x, y; uint8_t adv; uint32_t off; } fg_glyph_t;
typedef struct { uint8_t size, asc, desc; uint16_t n; const fg_glyph_t *g; const uint8_t *bits; } fg_font_t;
extern const fg_font_t FG_FONT_S;   /* Manrope 700, 12 px */
extern const fg_font_t FG_FONT_M;   /* Manrope 800, 16 px */
extern const fg_font_t FG_FONT_L;   /* Manrope 800, 26 px */
extern const fg_font_t FG_FONT_XL;   /* Manrope 700, 40 px */
extern const fg_font_t FG_FONT_N;   /* Manrope 700, 64 px */
