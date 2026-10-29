/* El visual de FNTSMG-1 en grande: fg_vis (el mismo C del aparato) a 720×720, con 8 cuadros de historia. La página
 * (index.html) le da los golpes y el estado que el FM-1 manda por SysEx, y lee el cuadro a 1 bit de e_out(). */
#include "fg_vis.h"

#define VW 720
#define HIST 8
static fg_vis_t V;
static fg_vis_ui_t UI;
static uint8_t MEM[VW * VW / 2 + HIST * (VW / 8) * VW + 35 * VW + 64];
static uint8_t OUT[(VW / 8) * VW], P[8];

__attribute__((export_name("e_init"))) int e_init(int seed) { if (fg_vis_size(VW, VW, HIST) > sizeof MEM) return 0; fg_vis_init(&V, MEM, VW, VW, HIST, (uint32_t)seed); return VW; }
__attribute__((export_name("e_params"))) uint8_t *e_params(void) { return P; }
__attribute__((export_name("e_out"))) uint8_t *e_out(void) { return OUT; }
__attribute__((export_name("e_hit"))) void e_hit(int t, int m, int vel) { fg_vis_hit(&V, (unsigned)t, (unsigned)m, (unsigned)vel, P, UI.now_ms); }
__attribute__((export_name("e_level"))) void e_level(int t, int peak) { fg_vis_level(&V, (unsigned)t, peak); }
__attribute__((export_name("e_mix"))) void e_mix(int t, int i, int v) { UI.mix[t & 7][i & 3] = (uint8_t)v; }
__attribute__((export_name("e_state"))) void e_state(int view, int bpm, int sel, int cur, int queued, int used, int mute, int solo, int pend, int has_pend, int perf)
{
    UI.view = (uint8_t)view; UI.bpm = (uint8_t)bpm; UI.sel = (uint8_t)sel; UI.cur = (uint8_t)cur; UI.queued = (uint8_t)queued;
    UI.used = (uint16_t)used; UI.mute = (uint8_t)mute; UI.solo = (uint8_t)solo; UI.pend = (uint8_t)pend; UI.has_pend = (uint8_t)has_pend;
    UI.perf = (uint32_t)perf;
}
__attribute__((export_name("e_frame"))) void e_frame(int now_ms)
{
    UI.now_ms = (uint32_t)now_ms;
    fg_vis_frame(&V, &UI);
    for (unsigned y0 = 0; y0 < VW; y0 += FG_VIS_BAND) fg_vis_band(&V, y0, FG_VIS_BAND, OUT + y0 * (VW / 8));
}
