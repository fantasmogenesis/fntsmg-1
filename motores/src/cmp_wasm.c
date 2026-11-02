/* Puente de la página de motores: un motor a prueba (de los de ahora o candidato) y una batería de guía,
 * por la mezcladora del aparato. El mismo C del FM-1, compilado a WebAssembly.
 * clase 0..3: bombo, caja, metal, perc; 4: sinte; 5: candidato (fg_cand.h). */
#include <emscripten.h>
#include "fg_kick.h"
#include "fg_snare.h"
#include "fg_metal.h"
#include "fg_perc.h"
#include "fg_synth.h"
#include "fg_cand.h"
#include "fg_mix.h"

static fg_kick_t K, GK;
static fg_snare_t SN;
static fg_metal_t MT, GM;
static fg_perc_t PC;
static fg_synth_t SY;
static fg_synth_shared_t SH;
static fgy_eco_t ECO;                                   /* el eco de DUB lo da la plataforma (en el aparato, fg_box) */
static fg_cand_t CA;
static fg_mix_t MX;
static int cls, mdl;
static uint8_t P[8];
static int32_t trk[FG_TRACKS][FG_BLOCK], outl[FG_BLOCK], outr[FG_BLOCK];
static float buf[FG_BLOCK * 2];
static uint8_t DEFS[8];

static int track_of(void) { return cls < 4 ? cls : cls == 5 ? (FGC_ROLE[mdl] == FGC_R_SINTE ? 4 : FGC_ROLE[mdl]) : 4; }
static void put(int i, int v)
{
    switch (cls) {
    case 0: fg_kick_param(&K, (unsigned)mdl, (unsigned)i, (unsigned)v); break;
    case 1: fg_snare_param(&SN, (unsigned)mdl, (unsigned)i, (unsigned)v); break;
    case 2: fg_metal_param(&MT, (unsigned)mdl, (unsigned)i, (unsigned)v); break;
    case 3: fg_perc_param(&PC, (unsigned)mdl, (unsigned)i, (unsigned)v); break;
    case 4: fg_synth_param(&SY, (unsigned)mdl, (unsigned)i, (unsigned)v); break;
    default: fg_cand_param(&CA, (unsigned)i, (unsigned)v);
    }
}

EMSCRIPTEN_KEEPALIVE void cmp_init(void)
{
    fg_kick_init(&K); fg_snare_init(&SN); fg_metal_init(&MT); fg_perc_init(&PC);
    fg_synth_init(&SY, &SH, FGY_SH); SY.eco = &ECO; SY.step = FG_SR / 8; fg_cand_init(&CA, 0);
    fg_kick_init(&GK); fg_metal_init(&GM);
    fg_kick_model(&GK, FGK_BARRIDO); fg_metal_model(&GM, 0);
    fg_mix_init(&MX);
    for (int t = 0; t < FG_TRACKS; t++) { fg_mix_strip(&MX, (unsigned)t, 2, 0); fg_mix_strip(&MX, (unsigned)t, 3, 10); }
    fg_mix_strip(&MX, 6, 0, 70); fg_mix_strip(&MX, 7, 0, 55);           /* la guía, más baja */
}
/* elige el motor a prueba; devuelve un puntero a sus 8 valores iniciales */
EMSCRIPTEN_KEEPALIVE uint8_t *cmp_engine(int c, int m)
{
    cls = c; mdl = m;
    switch (c) {
    case 0: fg_kick_init(&K); fg_kick_model(&K, (unsigned)m); for (int i = 0; i < 8; i++) DEFS[i] = K.p[m][i]; break;
    case 1: fg_snare_init(&SN); fg_snare_model(&SN, (unsigned)m); for (int i = 0; i < 8; i++) DEFS[i] = SN.p[m][i]; break;
    case 2: fg_metal_init(&MT); fg_metal_model(&MT, (unsigned)m); for (int i = 0; i < 8; i++) DEFS[i] = MT.p[m][i]; break;
    case 3: fg_perc_init(&PC); fg_perc_model(&PC, (unsigned)m); for (int i = 0; i < 8; i++) DEFS[i] = PC.p[m][i]; break;
    case 4: { uint32_t st = SY.step; fg_synth_init(&SY, &SH, (unsigned)m); SY.eco = &ECO; SY.step = st; for (int i = 0; i < 8; i++) DEFS[i] = SY.p[m][i]; break; }
    default: fg_cand_init(&CA, (unsigned)m); for (int i = 0; i < 8; i++) DEFS[i] = CA.p[i];
    }
    for (int i = 0; i < 8; i++) P[i] = DEFS[i];
    return DEFS;
}
EMSCRIPTEN_KEEPALIVE void cmp_param(int i, int v) { if (i >= 0 && i < 8) { P[i] = (uint8_t)v; put(i, v); } }
/* note: -1 = la NOTA de la perilla; si no, el valor de NOTA para este golpe (como un lock del paso) */
EMSCRIPTEN_KEEPALIVE void cmp_trig(int vel, int note, int gate)
{
    uint8_t ov[8] = {0}; unsigned mask = 0;
    if (note >= 0) { ov[0] = (uint8_t)note; mask = 1; }
    switch (cls) {
    case 0: fg_kick_trig(&K, (unsigned)vel, ov, mask); break;
    case 1: fg_snare_trig(&SN, (unsigned)vel, ov, mask); break;
    case 2: fg_metal_trig(&MT, (unsigned)vel, ov, mask); break;
    case 3: fg_perc_trig(&PC, (unsigned)vel, ov, mask); break;
    case 4: fg_synth_trig(&SY, (unsigned)vel, ov, mask, (uint32_t)gate, 255u); break;
    default:
        if (note >= 0) CA.p[0] = (uint8_t)note;
        fg_cand_trig(&CA, (unsigned)vel, (uint32_t)gate);
        CA.p[0] = P[0];
    }
}
/* la batería de guía: 0 bombo, 1 hat */
EMSCRIPTEN_KEEPALIVE void cmp_guide(int which, int vel)
{
    if (which == 0) fg_kick_trig(&GK, (unsigned)vel, 0, 0); else fg_metal_trig(&GM, (unsigned)vel, 0, 0);
}
EMSCRIPTEN_KEEPALIVE void cmp_fx(int i, int v) { fg_mix_fx(&MX, (unsigned)i, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void cmp_tempo(int step_samples) { fg_mix_tempo(&MX, (uint32_t)step_samples); SY.step = (uint32_t)step_samples; }
EMSCRIPTEN_KEEPALIVE int cmp_peak(void) { return MX.st[track_of()].peak; }
EMSCRIPTEN_KEEPALIVE int cmp_rate(void) { return FG_SR; }

EMSCRIPTEN_KEEPALIVE float *cmp_render(int n)
{
    if (n > FG_BLOCK) n = FG_BLOCK;
    int32_t *in[FG_TRACKS];
    for (int t = 0; t < FG_TRACKS; t++) { in[t] = trk[t]; for (int i = 0; i < n; i++) trk[t][i] = 0; }
    for (int i = 0; i < n; i++) { outl[i] = 0; outr[i] = 0; }
    int t = track_of();
    switch (cls) {
    case 0: fg_kick_render(&K, trk[t], (unsigned)n); break;
    case 1: fg_snare_render(&SN, trk[t], (unsigned)n); break;
    case 2: fg_metal_render(&MT, trk[t], (unsigned)n); break;
    case 3: fg_perc_render(&PC, trk[t], (unsigned)n); break;
    case 4: fg_synth_render(&SY, trk[t], (unsigned)n); break;
    default: fg_cand_render(&CA, trk[t], (unsigned)n);
    }
    fg_kick_render(&GK, trk[6], (unsigned)n);
    fg_metal_render(&GM, trk[7], (unsigned)n);
    fg_mix_render(&MX, in, outl, outr, (unsigned)n);
    fg_mix_tape(&MX, outl, outr, (unsigned)n);
    for (int i = 0; i < n; i++) {
        buf[2 * i] = (float)fg_clamp16(outl[i]) * (1.0f / 32768.0f);
        buf[2 * i + 1] = (float)fg_clamp16(outr[i]) * (1.0f / 32768.0f);
    }
    return buf;
}
