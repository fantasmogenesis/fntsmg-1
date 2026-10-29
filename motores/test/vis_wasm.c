/* Puente WASM de fg_vis para la web: 240×240, 8 cuadros de historia, cuadro entero a 1 bit en OUT. */
#include <emscripten.h>
#include "fg_vis.h"

#define VW 240
#define VH 240
#define HIST 8
static fg_vis_t V;
static fg_vis_ui_t UI;
static uint8_t MEM[VW * VH + HIST * (VW / 8) * VH + 35 * VW];
static uint8_t OUT[(VW / 8) * VH], P[8];

EMSCRIPTEN_KEEPALIVE int v_init(int seed) { if (fg_vis_size(VW, VH, HIST) > sizeof MEM) return 0; fg_vis_init(&V, MEM, VW, VH, HIST, (uint32_t)seed); return 1; }
EMSCRIPTEN_KEEPALIVE fg_vis_ui_t *v_ui(void) { return &UI; }
EMSCRIPTEN_KEEPALIVE uint8_t *v_params(void) { return P; }
EMSCRIPTEN_KEEPALIVE uint8_t *v_out(void) { return OUT; }
EMSCRIPTEN_KEEPALIVE void v_hit(int t, int m, int vel) { fg_vis_hit(&V, (unsigned)t, (unsigned)m, (unsigned)vel, P, UI.now_ms); }
EMSCRIPTEN_KEEPALIVE void v_level(int t, int peak) { fg_vis_level(&V, (unsigned)t, peak); }
EMSCRIPTEN_KEEPALIVE void v_row(int slot, int row) { (void)slot; (void)row; }
EMSCRIPTEN_KEEPALIVE void v_photo(const int16_t *tab) { (void)tab; }
EMSCRIPTEN_KEEPALIVE void v_sample(const int16_t *smp, int len) { (void)smp; (void)len; }
EMSCRIPTEN_KEEPALIVE void v_frame(void)
{
    fg_vis_frame(&V, &UI);
    for (unsigned y0 = 0; y0 < VH; y0 += FG_VIS_BAND) fg_vis_band(&V, y0, FG_VIS_BAND, OUT + y0 * (VW / 8));
}
