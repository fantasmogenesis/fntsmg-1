#include "fg_kick.h"

static const uint8_t DEF[FGK_MODELS][FGK_NP] = {
    {58, 60, 55, 10, 25, 50, 45, 110},    /* barrido (TONO 58 = los 38 Hz de antes; el rango ahora baja a 20 Hz) */
    {48, 55, 60, 50, 70, 25, 45, 110},    /* membrana (TONO 48 = los 34 Hz de antes; baja a 22 Hz) */
    {64, 0, 0, 127, 64, 0, 0, 100},       /* sample: tal cual (fg_samp.h) */
};
/* modos de una membrana circular: (0,1) (1,1) (2,1) (0,2), en Q16 */
static const uint32_t MODE_RATIO[4] = {65536, 104464, 139985, 150405};

static int32_t sq2(unsigned v) { return (int32_t)(v * v * 2u); }   /* 0..127 -> 0..32258, curva cuadrada */

/* PEGADA, igual en los dos modelos: sostiene el cuerpo hasta ~50 ms y lo empuja a la saturación (x1..x3.5) */
static int32_t peg_hold(unsigned p) { return (int32_t)(p * 17u); }
static int32_t peg_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }

/* ---------------------------------------------------------- barrido --- */
static void sweep_trig(fgk_sweep_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(20u * 256u) * fg_exp2_q16(p[0] * 65536u * 2285u / 127000u)) >> 16);   /* 20..97 Hz */
    v->inc_end = fg_inc(f_q8);
    v->sweep = (int32_t)(32768u + p[1] * 65536u * 11u / 2u / 127u);   /* 0.5 .. 6 octavas arriba */
    v->penv = FG_ONE30;
    v->kp16 = fg_pow16(fg_decay(p[1], 45, 95));                 /* y más largo cuanto más barrido */
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[2], 55, 118);                            /* caída ~60 ms .. ~2.8 s */
    v->kill = 0;
    v->fold_g = 4096 + (int32_t)(p[3] * 4096u * 5u / 127u);     /* doblez: ganancia 1..6 antes del plegado */
    v->lvl = sq2(p[7]);
    v->click = v->amp; v->kc = fg_decay(p[5], 12, 32); v->clvl = fg_amount(p[5], 60000);   /* click: ruido brillante y un blip, 6..17 ms */
    v->att = 0; v->att_inc = 32767 / (int32_t)((127u - p[5]) * 130u / 127u + 1u);           /* sin click, el golpe entra suave */
    v->cph = 0; v->cinc = fg_inc(1800u << 8); v->chp = 0;
    v->hold = peg_hold(p[6]);
    v->drive = peg_drive(p[6]);
    v->ph = 0x40000000u;                                        /* arranca en coseno: el primer ciclo es un escalón */
    v->cnt = 0; v->on = 1;
}

static void sweep_render(fgk_sweep_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t oct = (uint32_t)fg_mul30(v->sweep, v->penv);
            v->inc = (uint32_t)(((uint64_t)v->inc_end * fg_exp2_q16(oct)) >> 16);
            v->penv = fg_mul30(v->penv, v->kp16);
        }
        int32_t x = fg_mul30(fg_sin(v->ph), v->amp);
        v->ph += v->inc;
        x = fg_fold((int32_t)(((int64_t)x * v->fold_g) >> 12));
        x = fg_soft((int32_t)(((int64_t)x * v->drive) >> 12));      /* el cuerpo a escala completa: el bombo manda */
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; x = fg_mul15(x, v->att); }
        if (v->click > 2000 && v->clvl) {                       /* el click va encima de la saturación: no se aplasta */
            int32_t nz = fg_noise(&v->seed), hp = nz - v->chp;
            v->chp += (nz - v->chp) >> 2;
            x += fg_mul15(fg_mul30((hp >> 1) + fg_sin(v->cph), v->click), v->clvl);
            v->cph += v->cinc;
            v->click = fg_mul30(v->click, v->kill ? v->kill : v->kc);
        }
        out[i] += fg_mul15(x, v->lvl) * 2;
        if (v->hold > 0 && !v->kill) v->hold--;
        else v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}

/* --------------------------------------------------------- membrana --- */
static void memb_trig(fgk_memb_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(22u * 256u) * fg_exp2_q16(p[0] * 65536u * 2447u / 127000u)) >> 16);   /* 22..120 Hz */
    uint32_t inc = fg_inc(f_q8), m = p[3], base = 50u + p[2] * 65u / 127u;
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    /* MATERIAL: piel blanda = solo el fundamental; dura = los tres modos altos fuertes y largos */
    static const int32_t GH[4] = {32767, 30000, 26000, 22000};
    for (int i = 0; i < 4; i++) {
        v->inc0[i] = (uint32_t)(((uint64_t)inc * MODE_RATIO[i]) >> 16);
        v->inc[i] = v->inc0[i];
        v->ph[i] = 0;
        v->amp[i] = a;
        v->g[i] = i == 0 ? GH[0] : fg_amount(m, GH[i]);
        int32_t idx = i == 0 ? (int32_t)base : (int32_t)base - 4 * i - (int32_t)((127u - m) * 40u / 127u);
        v->k[i] = FG_DECAY[idx < 4 ? 4 : idx];
    }
    {   /* la suma de modos no debe saturar sola: si no, MATERIAL no se oye */
        int32_t sum = v->g[0] + v->g[1] + v->g[2] + v->g[3];
        v->norm = (int32_t)(((int64_t)32767 * 36000) / sum);
        if (v->norm > 32767) v->norm = 32767;
    }
    /* TENSIÓN: la altura arranca hasta 4x arriba (2 octavas) y se relaja en ~6..130 ms */
    v->tens = (int32_t)((3u << 28) / 127u * p[1]);
    v->tenv = FG_ONE30;
    v->kt16 = fg_pow16(fg_decay(p[1], 30, 85));
    /* GOLPE: mazo blando = ataque de hasta 15 ms y nada de chasquido; palo duro = ataque inmediato y chasquido */
    unsigned golpe = p[4];
    unsigned att_smp = (127u - golpe) * 660u / 127u;
    v->att = 0;
    v->att_inc = 32767 / (int32_t)(att_smp + 1u);
    v->click = a;
    v->kc = fg_decay(golpe, 6, 32);                             /* el chasquido dura 4..17 ms */
    v->clvl = fg_amount(golpe, 40000);
    v->chp = 0;
    v->kill = 0;
    v->lvl = sq2(p[7]);
    v->hold = peg_hold(p[6]);
    v->drive = peg_drive(p[6]);
    v->cnt = 0; v->on = 1;
}

static void memb_render(fgk_memb_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t m = (1u << 28) + (uint32_t)(((int64_t)v->tens * v->tenv) >> 30);
            for (int j = 0; j < 4; j++) v->inc[j] = (uint32_t)(((uint64_t)v->inc0[j] * m) >> 28);
            v->tenv = fg_mul30(v->tenv, v->kt16);
        }
        int32_t s = 0;
        for (int j = 0; j < 4; j++) {
            s += fg_mul15(fg_mul30(fg_sin(v->ph[j]), v->amp[j]), v->g[j]);
            v->ph[j] += v->inc[j];
            if (v->hold <= 0 || v->kill || j > 0) v->amp[j] = fg_mul30(v->amp[j], v->kill ? v->kill : v->k[j]);
        }
        if (v->hold > 0) v->hold--;
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; }
        s = fg_mul15(fg_mul15(s, v->norm), v->att);
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12));
        if (v->click > 2000 && v->clvl) {                       /* chasquido encima de la saturación */
            int32_t nz = fg_mul30(fg_noise(&v->seed), v->click);
            int32_t hp = nz - v->chp; v->chp += (nz - v->chp) >> 3;
            s += fg_mul15(hp, v->clvl);
            v->click = fg_mul30(v->click, v->kill ? v->kill : v->kc);
        }
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->amp[0] < 2000 && v->click <= 2000) v->on = 0;
    }
}

/* ----------------------------------------------------------- satura --- */
/* SATURA (donde antes estaba RUMBLE): el bombo entero por una ganancia de x1 a x10 hacia una saturación suave, y un
 * realce de lo que pasa de ~700 Hz: armónicos y ataque que se oyen aun en bocinas chicas y cortan la mezcla */
static int32_t satura_amount(const fg_kick_t *k) { return k->p[k->model][k->model == FGK_MEMBRANA ? 5 : 4]; }   /* (SAMPLE no: su NIVEL satura) */
static void satura(fg_kick_t *k, int32_t *x, unsigned n)
{
    int32_t a = satura_amount(k), g = 4096 + a * 290, pres = a * 160, lp = k->sat_lp;
    for (unsigned i = 0; i < n; i++) {
        int32_t y = fg_soft((int32_t)(((int64_t)x[i] * g) >> 12));
        lp += fg_mul15(y - lp, 3113);                            /* ~700 Hz */
        x[i] = y + fg_mul15(y - lp, pres);
    }
    k->sat_lp = lp;
}

/* ------------------------------------------------------------ carril --- */
void fg_kick_init(fg_kick_t *k)
{
    fg_zero(k, sizeof *k);
    fg_copy(k->p, DEF, sizeof DEF);
    k->mb[0].seed = 0x1234567u; k->mb[1].seed = 0x89abcdeu;
    k->sw[0].seed = 0x5eed1u; k->sw[1].seed = 0x5eed2u;
}

void fg_kick_param(fg_kick_t *k, unsigned model, unsigned i, unsigned v)
{
    if (model >= FGK_MODELS || i >= FGK_NP) return;
    k->p[model][i] = (uint8_t)(v > 127u ? 127u : v);
}

void fg_kick_model(fg_kick_t *k, unsigned model)
{
    if (model < FGK_MODELS) k->model = (uint8_t)model;
}

void fg_kick_trig(fg_kick_t *k, unsigned vel, const uint8_t *ov, unsigned mask)
{
    int32_t fast = FG_DECAY[2];                                 /* la ranura anterior se apaga en ~3 ms */
    unsigned o = k->cur;
    k->sw[o].kill = fast; k->mb[o].kill = fast; k->sm[o].kill = fast;
    k->cur ^= 1u;
    uint8_t p[FGK_NP];
    fg_apply_locks(p, k->p[k->model], ov, mask, FGK_NP);
    if (k->model == FGK_BARRIDO) sweep_trig(&k->sw[k->cur], p, vel);
    else if (k->model == FGK_SAMPLE) fg_sampv_trig(&k->sm[k->cur], p, vel, 0, 0);
    else memb_trig(&k->mb[k->cur], p, vel);
}

void fg_kick_render(fg_kick_t *k, int32_t *out, unsigned n)
{
    int32_t tmp[FG_BLOCK];
    fg_zero(tmp, n * sizeof *tmp);
    for (int s = 0; s < 2; s++) {
        sweep_render(&k->sw[s], tmp, n);
        memb_render(&k->mb[s], tmp, n);
        fg_sampv_render(&k->sm[s], tmp, n);
    }
    if (k->model != FGK_SAMPLE) satura(k, tmp, n);
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) {
        int32_t v = tmp[i];
        out[i] += v;
        if (v < 0) v = -v;
        if (v > pk) pk = v;
    }
    k->peak = pk;
}
