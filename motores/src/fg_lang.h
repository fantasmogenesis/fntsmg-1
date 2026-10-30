/* Los textos de la pantalla en tres idiomas. La interfaz escribe en español y fg_tr traduce justo antes de
 * dibujar (fg_box_draw.c text()): primero el texto entero, si no, cada tramo entre "·" y, dentro de cada tramo, cada
 * pedazo sin números ("RANURA 3" -> "SLOT 3"). Un tramo que empieza con "\1" no se traduce (nombres de motores,
 * de sonidos y de samples); la marca no se dibuja. La tabla sale de motores/tools/lang.tsv (tools/gen_lang.py). */
#pragma once
#include <stdint.h>

enum { FG_ES, FG_EN, FG_PT, FG_LANGS };
extern uint8_t fg_lang;
extern const char *const FG_LANG_NAME[FG_LANGS];
void fg_tr(const char *s, char *out, unsigned cap);
/* el texto de una entrada de la tabla en el idioma de ahora (para la app: META); 0 si no está */
const char *fg_tr_word(const char *s);
