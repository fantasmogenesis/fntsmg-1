#include "fg_metal.h"

static const uint8_t DEF[FGM_MODELS][FGM_NP] = {
    {80, 30, 80, 40, 0, 40, 30, 100},     /* granizo: hat cerrado */
    {50, 70, 64, 95, 70, 40, 20, 95},     /* placa */
    {64, 0, 0, 127, 64, 0, 0, 100},       /* sample */
};
static int32_t sq2(unsigned v) { return (int32_t)(v * v * 2u); }
static int32_t svf_f2(uint32_t fc_hz) { return 2 * fg_sin(fg_inc(fc_hz << 8) >> 2); }   /* para correr el filtro dos veces por muestra */
static int32_t peg_hold(unsigned p) { return (int32_t)(p * 12u); }
static int32_t peg_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }

/* ---------------------------------------------------------- granizo --- */
static void hail_trig(fgm_hail_t *v, const uint8_t *p, unsigned vel)
{
    /* DENSIDAD: de 150 a 30 000 granos por segundo (7.6 octavas, parejo en el oído) */
    uint32_t dens = (uint32_t)(((uint64_t)150u * fg_exp2_q16(p[0] * 65536u * 76u / 1270u)) >> 16);
    v->thresh = (uint32_t)(((uint64_t)dens << 32) / FG_SR);
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[1], 10, 100);                            /* CAÍDA: hat cerrado ~5 ms .. abierto ~1.2 s */
    /* COLOR: 1.5 .. 13 kHz; RESO: Q 0.7 .. 12, compensando la ganancia para que suene igual de fuerte */
    uint32_t fc = (uint32_t)(((uint64_t)1500u * fg_exp2_q16(p[2] * 65536u * 31u / 1270u)) >> 16);
    v->sf = svf_f2(fc > 13000u ? 13000u : fc);                  /* el filtro corre al doble: estable hasta 13 kHz */
    v->sq = 46000 - (int32_t)(p[3] * 350u);                     /* 1/Q: 1.4 .. 0.05: de soplo a silbido */
    v->norm = v->sq < 1500 ? 1500 : v->sq;
    /* AGITA: 0 = golpe seco; 127 = shaker que entra en ~60 ms */
    v->att = 0; v->att_inc = 32767 / (int32_t)(p[4] * 2650u / 127u + 1u);
    /* GRANO: de chispa de ruido de 1 muestra a gota con tono de ~8 ms */
    v->glen = 1 + (int32_t)(((uint32_t)p[5] * p[5]) * 350u / 16129u);
    v->tone = (int32_t)p[5] * 258; v->cinc = fg_inc((fc > 13000u ? 13000u : fc) << 8); v->gph = 0;
    v->gpos = v->glen; v->ginv = 65536 / v->glen;
    v->hold = peg_hold(p[6]); v->drive = peg_drive(p[6]);
    v->lvl = sq2(p[7]);
    v->lo = v->bp = v->hpz = 0; v->kill = 0; v->on = 1;
}

static void hail_render(fgm_hail_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        uint32_t r = fg_rand(&v->seed);
        if (r < v->thresh) {                                     /* nace un grano, con su altura al azar cerca del COLOR */
            v->gpos = 0; v->gamp = (int32_t)(fg_rand(&v->seed) >> 17) + 16384;
            v->ginc = (uint32_t)(((uint64_t)v->cinc * (45000u + (fg_rand(&v->seed) >> 16))) >> 16);
        }
        int32_t x = 0;
        if (v->gpos < v->glen) {
            int32_t e = v->gamp - (int32_t)(((int64_t)v->gamp * v->gpos * v->ginv) >> 16);   /* sin dividir por muestra */
            int32_t g = fg_noise(&v->seed) + fg_mul15(fg_sin(v->gph) - fg_noise(&v->seed), v->tone);   /* ruido -> gota */
            v->gph += v->ginc;
            x = fg_mul15(g, e);
            v->gpos++;
        }
        int32_t y = x - v->hpz; v->hpz += (x - v->hpz) >> 3;     /* sin graves */
        int32_t hi = 0;
        for (int k = 0; k < 2; k++) {
            v->lo += fg_mul15(v->sf, v->bp);
            hi = y - v->lo - fg_mul15(v->sq, v->bp);
            v->bp += fg_mul15(v->sf, hi);
            v->bp = fg_clamp16(v->bp); v->lo = fg_clamp16(v->lo);
        }
        int32_t s = fg_mul15(v->bp, v->norm) * 2 + (hi >> 2);
        s = fg_mul30(s, v->amp);
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; s = fg_mul15(s, v->att); }
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12)) >> 1;
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->hold > 0 && !v->kill) v->hold--;
        else v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------ placa --- */
/* FORMA mezcla tres juegos de razones: campana (casi armónica) -> placa -> platillo (racimo apretado) */
static const uint32_t RATIO[3][FGM_MODES] = {   /* Q16 */
    {65536, 73400, 98304, 110100, 146801, 163840},     /* gong: racimo grave, 1 1.12 1.5 1.68 2.24 2.5 */
    {65536, 131072, 157286, 196608, 266732, 353894},   /* campana: casi armónica, 1 2 2.4 3 4.07 5.4 */
    {65536, 112067, 189399, 290325, 391250, 543949},   /* platillo: repartido hacia arriba, 1 1.71 2.89 4.43 5.97 8.3 */
};
static void plate_trig(fgm_plate_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(150u * 256u) * fg_exp2_q16(p[0] * 65536u * 36u / 1270u)) >> 16);   /* 150..1800 Hz */
    uint32_t base = fg_inc(f_q8), forma = p[2], bri = p[3], det = p[5];
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127), sum = 0;
    unsigned kidx = 52u + p[1] * 60u / 127u;                    /* CAÍDA: ~55 ms .. ~2.6 s (abajo de eso era un clic) */
    static const int32_t GH[FGM_MODES] = {32767, 30000, 28000, 26000, 24000, 22000};
    for (int i = 0; i < FGM_MODES; i++) {
        uint32_t r0, r1, t;
        if (forma < 64u) { r0 = RATIO[0][i]; r1 = RATIO[1][i]; t = forma * 2u; }
        else { r0 = RATIO[1][i]; r1 = RATIO[2][i]; t = (forma - 64u) * 2u; }
        uint32_t ratio = r0 + (uint32_t)(((int64_t)((int32_t)r1 - (int32_t)r0) * (int32_t)t) / 126);
        uint32_t inc = (uint32_t)(((uint64_t)base * ratio) >> 16);
        v->inc[i] = inc;
        v->inc[i + FGM_MODES] = inc + (uint32_t)(((uint64_t)inc * det) / 2100u);   /* gemelo: hasta +6 %: de brillo a clang */
        v->ph[i] = (uint32_t)i * 0x2AAAAAAAu; v->ph[i + FGM_MODES] = (uint32_t)i * 0x15555555u;
        v->amp[i] = a;
        v->g[i] = i == 0 ? GH[0] : fg_amount(bri, GH[i]);       /* BRILLO: cuánto suenan los modos altos */
        int32_t idx = (int32_t)kidx - (int32_t)((127u - bri) * 6u / 127u) * i;
        v->k[i] = FG_DECAY[idx < 6 ? 6 : idx];
        sum += v->g[i];
    }
    v->twin = fg_amount(det, 32767);                            /* BATIDO: cuánto suena el gemelo */
    sum += (sum * (v->twin >> 4)) >> 11;
    v->norm = (int32_t)(((int64_t)32767 * 30000) / sum); if (v->norm > 32767) v->norm = 32767;
    unsigned golpe = p[4];                                      /* GOLPE: mazo blando (entra en 10 ms) .. baqueta con chasquido */
    v->att = 0; v->att_inc = 32767 / (int32_t)((127u - golpe) * 440u / 127u + 1u);
    v->click = a; v->kc = fg_decay(golpe, 6, 24); v->clvl = fg_amount(golpe, 50000); v->chp = 0;
    v->hold = peg_hold(p[6]); v->drive = peg_drive(p[6]);
    v->lvl = sq2(p[7]);
    v->kill = 0; v->on = 1;
}

static void plate_render(fgm_plate_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        int32_t s = 0, kill = v->kill;
        for (int j = 0; j < FGM_MODES; j++) {
            int32_t m = fg_sin(v->ph[j]) + (v->twin ? fg_mul15(fg_sin(v->ph[j + FGM_MODES]), v->twin) : 0);
            s += fg_mul15(fg_mul30(m, v->amp[j]), v->g[j]);
            v->ph[j] += v->inc[j]; v->ph[j + FGM_MODES] += v->inc[j + FGM_MODES];
            if (j > 0 || v->hold <= 0 || kill) v->amp[j] = fg_mul30(v->amp[j], kill ? kill : v->k[j]);
        }
        if (v->hold > 0) v->hold--;
        s = fg_mul15(s, v->norm);
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; s = fg_mul15(s, v->att); }
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12)) >> 1;
        if (v->click > 2000 && v->clvl) {                       /* chasquido de la baqueta, encima */
            int32_t nz = fg_mul30(fg_noise(&v->seed), v->click), hp = nz - v->chp;
            v->chp += (nz - v->chp) >> 2;
            s += fg_mul15(hp, v->clvl);
            v->click = fg_mul30(v->click, kill ? kill : v->kc);
        }
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->amp[0] < 2000 && v->click <= 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------ carril --- */
void fg_metal_init(fg_metal_t *m)
{
    fg_zero(m, sizeof *m);
    fg_copy(m->p, DEF, sizeof DEF);
    m->h[0].seed = 0x7a11u; m->h[1].seed = 0x7a22u; m->pl[0].seed = 0x91a7eu; m->pl[1].seed = 0x91a8eu;
}
void fg_metal_param(fg_metal_t *m, unsigned model, unsigned i, unsigned v)
{
    if (model < FGM_MODELS && i < FGM_NP) m->p[model][i] = (uint8_t)(v > 127u ? 127u : v);
}
void fg_metal_model(fg_metal_t *m, unsigned model) { if (model < FGM_MODELS) m->model = (uint8_t)model; }
void fg_metal_trig(fg_metal_t *m, unsigned vel, const uint8_t *ov, unsigned mask)
{
    uint8_t p[FGM_NP];
    fg_apply_locks(p, m->p[m->model], ov, mask, FGM_NP);
    unsigned o = m->cur;
    m->h[o].kill = FG_DECAY[2]; m->pl[o].kill = FG_DECAY[2]; m->sm[o].kill = FG_DECAY[2];     /* el golpe nuevo ahoga al anterior: hat abierto que se cierra */
    m->cur ^= 1u;
    if (m->model == FGM_SAMPLE) fg_sampv_trig(&m->sm[m->cur], p, vel, 0, 0);
    else if (m->model == FGM_GRANIZO) hail_trig(&m->h[m->cur], p, vel);
    else plate_trig(&m->pl[m->cur], p, vel);
}
void fg_metal_render(fg_metal_t *m, int32_t *out, unsigned n)
{
    int32_t tmp[FG_BLOCK];
    fg_zero(tmp, n * sizeof *tmp);
    for (int s = 0; s < 2; s++) { hail_render(&m->h[s], tmp, n); plate_render(&m->pl[s], tmp, n); fg_sampv_render(&m->sm[s], tmp, n); }
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) { int32_t v = tmp[i]; out[i] += v; if (v < 0) v = -v; if (v > pk) pk = v; }
    m->peak = pk;
}
