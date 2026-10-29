/* Puente del laboratorio: el mismo C del aparato, compilado a WebAssembly.
 * Pistas: 0 bombo, 1 caja, 2 metal, 3 perc, 4..7 sintes. Todas pasan por la mezcladora y los efectos. */
#include <emscripten.h>
#include "fg_kick.h"
#include "fg_snare.h"
#include "fg_metal.h"
#include "fg_perc.h"
#include "fg_synth.h"
#include "fg_mix.h"

#define SMP_MAX 64                                   /* (ya no hay sample ni foto: los dos sintes que los usaban cambiaron) */

static fg_kick_t K;
static fg_snare_t SN;
static fg_metal_t MT;
static fg_perc_t PC;
static fg_synth_t SY[4];
static fg_mix_t MX;
static fg_synth_shared_t SH;
static int16_t TAB[64 * 256];                        /* la página todavía la escribe: no se usa */
static int16_t SMP[SMP_MAX];
static uint8_t LOCKS[FG_TRACKS][8];
static int32_t trk[FG_TRACKS][FG_BLOCK], outl[FG_BLOCK], outr[FG_BLOCK], levels[FG_TRACKS];
static float buf[FG_BLOCK * 2];

EMSCRIPTEN_KEEPALIVE void lab_init(void)
{
    fg_kick_init(&K); fg_snare_init(&SN); fg_metal_init(&MT); fg_perc_init(&PC);
    static const uint8_t M0[4] = {FGY_ACIDO, FGY_SH, FGY_FM, FGY_CUERDAS};
    for (int i = 0; i < 4; i++) fg_synth_init(&SY[i], &SH, M0[i]);
    fg_mix_init(&MX);
}
EMSCRIPTEN_KEEPALIVE void lab_model(int t, int m)
{
    switch (t) {
    case 0: fg_kick_model(&K, (unsigned)m); break;
    case 1: fg_snare_model(&SN, (unsigned)m); break;
    case 2: fg_metal_model(&MT, (unsigned)m); break;
    case 3: fg_perc_model(&PC, (unsigned)m); break;
    default: if (t < 8) fg_synth_model(&SY[t - 4], (unsigned)m);
    }
}
EMSCRIPTEN_KEEPALIVE void lab_param(int t, int m, int i, int v)
{
    switch (t) {
    case 0: fg_kick_param(&K, (unsigned)m, (unsigned)i, (unsigned)v); break;
    case 1: fg_snare_param(&SN, (unsigned)m, (unsigned)i, (unsigned)v); break;
    case 2: fg_metal_param(&MT, (unsigned)m, (unsigned)i, (unsigned)v); break;
    case 3: fg_perc_param(&PC, (unsigned)m, (unsigned)i, (unsigned)v); break;
    default: if (t < 8) fg_synth_param(&SY[t - 4], (unsigned)m, (unsigned)i, (unsigned)v);
    }
}
EMSCRIPTEN_KEEPALIVE uint8_t *lab_locks(int t) { return LOCKS[t & 7]; }
/* mask: qué parámetros del paso vienen lockeados (valores en lab_locks); gate: muestras hasta soltar la nota */
EMSCRIPTEN_KEEPALIVE void lab_trig(int t, int vel, int mask, int gate)
{
    const uint8_t *ov = LOCKS[t & 7];
    switch (t) {
    case 0: fg_kick_trig(&K, (unsigned)vel, ov, (unsigned)mask); break;
    case 1: fg_snare_trig(&SN, (unsigned)vel, ov, (unsigned)mask); break;
    case 2: fg_metal_trig(&MT, (unsigned)vel, ov, (unsigned)mask); break;
    case 3: fg_perc_trig(&PC, (unsigned)vel, ov, (unsigned)mask); break;
    default: if (t < 8) fg_synth_trig(&SY[t - 4], (unsigned)vel, ov, (unsigned)mask, (uint32_t)gate, 255u);
    }
}
EMSCRIPTEN_KEEPALIVE void lab_pre(int t, int samples) { if (t == 1) fg_snare_pre(&SN, (uint32_t)samples); }
EMSCRIPTEN_KEEPALIVE int lab_ahead(int t) { return t == 1 ? (int)fg_snare_ahead(&SN) : 0; }
EMSCRIPTEN_KEEPALIVE void lab_mix(int t, int which, int v) { fg_mix_strip(&MX, (unsigned)t, (unsigned)which, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void lab_mute(int t, int mute, int solo) { fg_mix_mute(&MX, (unsigned)t, (unsigned)mute, (unsigned)solo); }
EMSCRIPTEN_KEEPALIVE void lab_fx(int i, int v) { fg_mix_fx(&MX, (unsigned)i, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void lab_tempo(int step_samples) { fg_mix_tempo(&MX, (uint32_t)step_samples); }
EMSCRIPTEN_KEEPALIVE void lab_perf(int flags) { fg_mix_perf(&MX, (uint32_t)flags); }
EMSCRIPTEN_KEEPALIVE int32_t *lab_levels(void) { return levels; }
EMSCRIPTEN_KEEPALIVE int lab_row(int t) { (void)t; return -1; }
EMSCRIPTEN_KEEPALIVE int16_t *lab_table(void) { return TAB; }
EMSCRIPTEN_KEEPALIVE int16_t *lab_sample(void) { return SMP; }
EMSCRIPTEN_KEEPALIVE int lab_sample_max(void) { return SMP_MAX; }
EMSCRIPTEN_KEEPALIVE void lab_sample_set(int len, int rate) { (void)len; (void)rate; }

EMSCRIPTEN_KEEPALIVE float *lab_render(int n)
{
    if (n > FG_BLOCK) n = FG_BLOCK;
    int32_t *in[FG_TRACKS];
    for (int t = 0; t < FG_TRACKS; t++) { in[t] = trk[t]; for (int i = 0; i < n; i++) trk[t][i] = 0; }
    for (int i = 0; i < n; i++) { outl[i] = 0; outr[i] = 0; }
    fg_kick_render(&K, trk[0], (unsigned)n);
    fg_snare_render(&SN, trk[1], (unsigned)n);
    fg_metal_render(&MT, trk[2], (unsigned)n);
    fg_perc_render(&PC, trk[3], (unsigned)n);
    for (int s = 0; s < 4; s++) fg_synth_render(&SY[s], trk[4 + s], (unsigned)n);
    fg_mix_render(&MX, in, outl, outr, (unsigned)n);
    fg_mix_tape(&MX, outl, outr, (unsigned)n);
    for (int t = 0; t < FG_TRACKS; t++) levels[t] = MX.st[t].peak;
    for (int i = 0; i < n; i++) {
        buf[2 * i] = (float)fg_clamp16(outl[i]) * (1.0f / 32768.0f);
        buf[2 * i + 1] = (float)fg_clamp16(outr[i]) * (1.0f / 32768.0f);
    }
    return buf;
}
