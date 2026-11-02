/* cuánto cambia cada FX de performance el audio del patrón de fábrica (diferencia contra sin FX, en dB) */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static const uint8_t W2K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
#define NS (FG_SR * 3)
static int16_t ref[NS * 2], cur[NS * 2];
static void render(int16_t *o) { static int32_t x[2 * FG_BLOCK]; for (unsigned i = 0; i + FG_BLOCK <= NS; i += FG_BLOCK) { B.now += 3; fg_box_tick(&B, B.now); fg_box_audio(&B, x, FG_BLOCK); for (unsigned k = 0; k < 2 * FG_BLOCK; k++) o[2 * i + k] = (int16_t)x[k]; } }
static void setup(void) { fg_box_init(&B, VMEM, 240, 240, 4, 7); B.now = 0; fg_box_tick(&B, 1); fg_box_button(&B, FB_FX, 1); fg_box_button(&B, FB_FX, 0); }
int main(void)
{
    static const char *N[16] = {"REPITE 1/4", "REPITE 1/8", "STUTTER 16", "STUTTER 32", "REVERSA", "MEDIO TIEMPO", "FRENO", "GATE", "FILTRO GRAVE", "FILTRO AGUDO", "BITS", "ECO", "CONGELA", "SOLO PERC", "SIN BOMBO", "FILL"};
    setup(); fg_box_button(&B, FB_PLAY, 1); fg_box_button(&B, FB_PLAY, 0); render(ref);
    double e0 = 0; for (unsigned i = 0; i < NS * 2; i++) e0 += (double)ref[i] * ref[i];
    for (unsigned f = 0; f < 16; f++) {
        setup(); fg_box_button(&B, FB_PLAY, 1); fg_box_button(&B, FB_PLAY, 0);
        static int16_t pre[FG_SR / 2 * 2];
        { static int32_t x[2 * FG_BLOCK]; for (unsigned i = 0; i + FG_BLOCK <= FG_SR / 2; i += FG_BLOCK) { B.now += 3; fg_box_tick(&B, B.now); fg_box_audio(&B, x, FG_BLOCK); } (void)pre; }
        fg_box_key(&B, W2K[f], 1);                        /* sostenido todo el rato */
        render(cur);
        fg_box_key(&B, W2K[f], 0);
        /* (la referencia también empieza medio segundo después) */
        static int16_t r2[NS * 2];
        setup(); fg_box_button(&B, FB_PLAY, 1); fg_box_button(&B, FB_PLAY, 0);
        { static int32_t x[2 * FG_BLOCK]; for (unsigned i = 0; i + FG_BLOCK <= FG_SR / 2; i += FG_BLOCK) { B.now += 3; fg_box_tick(&B, B.now); fg_box_audio(&B, x, FG_BLOCK); } }
        render(r2);
        double ed = 0, er = 0;
        for (unsigned i = 0; i < NS * 2; i++) { double d = (double)cur[i] - r2[i]; ed += d * d; er += (double)r2[i] * r2[i]; }
        printf("%2u %-14s diferencia %6.1f dB respecto a la señal\n", f + 1, N[f], 10 * log10((ed + 1) / (er + 1)));
    }
    (void)e0;
    return 0;
}
