#include "fg_synth.h"

static const uint8_t DEF[FGY_MODELS][FGY_NP] = {
    {26, 20, 60, 35, 60, 75, 40, 100},    /* sh: bajo de electro */
    {20, 35, 95, 85, 45, 0, 25, 100},     /* ácido */
    {30, 49, 60, 50, 70, 50, 50, 95},     /* fm */
    {40, 60, 70, 70, 50, 90, 30, 92},     /* cuerdas: pad que entra y se queda */
    {76, 0, 0, 30, 90, 50, 90, 95},       /* textura: la nota de la muestra */
    {40, 50, 30, 40, 60, 60, 30, 95},     /* dron */
    {45, 0, 60, 50, 70, 40, 40, 95},      /* dub */
    {52, 40, 80, 30, 70, 20, 30, 95},     /* resonador */
};
static int32_t sq2(unsigned v) { return (int32_t)(v * v * 2u); }

/* ------------------------------------------------------- envolvente --- */
static int32_t note_q8(const uint8_t *p) { return (24 + (int32_t)p[0] * 61 / 128) << 8; }
static void env_start(fgy_voice_t *v, unsigned vel, unsigned attack_knob, int32_t kd, int32_t kr)
{
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->att = 0;
    v->att_inc = (32767 << 12) / (int32_t)(44u + attack_knob * attack_knob * 4u);   /* 1 ms .. 1.5 s (Q12 más fino: arriba de 90 ya no se queda en 0) */
    v->kd = kd; v->kr = kr; v->rel = 0;
}
/* un paso de la envolvente de amplitud; devuelve la ganancia Q15 */
static int32_t env_step(fgy_voice_t *v)
{
    if (v->gate) v->gate--; else v->rel = 1;
    if (v->att < (32767 << 12)) { v->att += v->att_inc; if (v->att > (32767 << 12)) v->att = 32767 << 12; }
    else {
        int32_t k = v->rel && v->kr < v->kd ? v->kr : v->kd;
        if (v->hold > 0 && !v->rel) v->hold--; else v->amp = fg_mul30(v->amp, k);
    }
    return fg_mul15((int32_t)(v->amp >> 15), v->att >> 12);
}

/* --------------------------------------------------------------- sh --- */
/* El SH-101 / S-1: un oscilador (sierra que pasa a cuadrada, a pulso angosto y a PWM que se mueve sola), un sub una o
 * dos octavas abajo, un filtro de 4 polos resonante y UNA envolvente ADSR para filtro y amplitud. Bajos de electro,
 * leads, bleeps, stabs. Sierra y pulso con PolyBLEP: sin el aliasing que ensucia los agudos.
 * NOTA ONDA SUB CORTE | RESO ENV FORMA NIVEL. FORMA recorre la envolvente: PLUCK (cae a cero) · BAJO (cae a un sostén)
 * · LEAD (sostiene) · PAD (entra lento). Ningún tramo baja de ~250 ms (60 dB): antes las caídas cortas eran clics. */
static void sh_trig(fgy_voice_t *v, unsigned vel)
{
    unsigned f = v->p[6], seg = f >> 5, u = f & 31u;
    /* índices de FG_DECAY (60 dB en 3 ms · 1333^(i/127)): caída 250 ms–1.2 s · 400 ms–1.5 s · 800 ms · 1.5 s; soltar 150 ms … 4 s */
    static const uint8_t D0[4] = {78, 86, 99, 110}, D1[4] = {106, 110, 99, 110}, R0[4] = {69, 69, 81, 103}, R1[4] = {69, 81, 103, 127};
    unsigned d = D0[seg] + (D1[seg] - D0[seg]) * u / 31u, r = R0[seg] + (R1[seg] - R0[seg]) * u / 31u;
    int32_t sus = seg == 0 ? 0 : seg == 1 ? (int32_t)(u * (FG_ONE30 / 56)) : seg == 2 ? FG_ONE30 / 10 * 7 + (int32_t)(u * (FG_ONE30 / 103)) : FG_ONE30;
    unsigned ak = seg == 2 ? 3u + u * 18u / 31u : seg == 3 ? 25u + u * 102u / 31u : 0u;   /* ataque: 1 ms · 2–40 ms · 60 ms–1.5 s */
    env_start(v, vel, ak, FG_DECAY[d], FG_DECAY[r]);
    v->tens = sus; v->env = 0;
    v->ph[0] = 0; v->ph[1] = 0; v->ph[2] = 0;
    v->y[0] = v->y[1] = v->y[2] = v->y[3] = 0;
}
static int32_t blep(uint32_t t, uint32_t dt, uint32_t rdt)       /* PolyBLEP (Q16): t fase, dt incremento, rdt = 2^32/dt */
{
    if (t < dt) { int32_t x = (int32_t)((t * rdt) >> 16); return 2 * x - (int32_t)(((int64_t)x * x) >> 16) - 65536; }
    if (t > 65536u - dt) { int32_t x = (int32_t)(((int64_t)((int32_t)t - 65536) * (int64_t)rdt) >> 16); return (int32_t)(((int64_t)x * x) >> 16) + 2 * x + 65536; }
    return 0;
}
static void sh_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    const uint8_t *p = v->p;
    unsigned onda = p[1];
    int32_t g = 0, kq = (int32_t)p[4] * 16000 / 127, sub = (int32_t)(p[2] > 95 ? 127 : p[2] * 127 / 95) * 200;
    uint32_t dt = 1, rdt = 65536, w = 32768, subsh = p[2] > 95 ? 2u : 1u;
    int32_t xf = onda <= 42 ? (int32_t)onda * 32767 / 42 : 32767;   /* sierra -> cuadrada */
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            v->inc[0] = fg_note_inc(v->note_q8);
            dt = (v->inc[0] >> 16) + 1u; rdt = 0xFFFFFFFFu / dt;
            if (onda <= 42) w = 32768;
            else if (onda <= 84) w = 32768 - (onda - 42u) * 24900u / 42u;          /* 50 % -> 12 % */
            else {                                               /* PWM sola: más honda y más rápida hacia el final */
                v->sph += 1400u + (onda - 84u) * 90u;
                w = 19660u + (uint32_t)(((int64_t)fg_sin(v->sph << 4) * (int32_t)((onda - 84u) * 300u)) >> 15);
            }
            int32_t e = (int32_t)(v->env >> 15);
            int32_t idx = 150 + (int32_t)p[3] * 800 / 127 + (int32_t)(((int64_t)e * ((int32_t)p[5] * 620 / 127 + (v->accent ? 120 : 0))) >> 15)
                        + ((v->note_q8 >> 8) - 48) * 5;
            g = FG_CUT_G[idx < 0 ? 0 : idx > 1023 ? 1023 : idx];
        }
        uint32_t t = v->ph[0] >> 16;
        int32_t b1 = blep(t, dt, rdt) >> 1, x, saw = 0, pul = 0;        /* (cada parte solo si se oye: en el FM-1 cuesta) */
        if (xf < 32767) saw = (int32_t)t - 32768 - b1;
        if (xf) pul = (t < w ? 32767 : -32768) + b1 - (blep((t - w) & 65535u, dt, rdt) >> 1) - ((int32_t)w - 32768);   /* (sin su DC) */
        x = !xf ? saw : xf >= 32767 ? pul : saw + ((((pul - saw) >> 1) * xf) >> 14);
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[0] >> subsh;
        x = ((x * 3) >> 2) + ((v->ph[1] & 0x80000000u) ? sub : -sub);
        int32_t in = fg_soft(x - (int32_t)(((int64_t)kq * v->y[3]) >> 12));
        v->y[0] += fg_mul15(g, in - v->y[0]);
        v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        v->y[2] += fg_mul15(g, v->y[1] - v->y[2]);
        v->y[3] += fg_mul15(g, v->y[2] - v->y[3]);
        int32_t y = (int32_t)(((int64_t)v->y[3] * (4096 + kq / 2)) >> 12);
        /* la ADSR: ataque lineal, cae hacia el sostén, al soltar cae a cero */
        if (v->gate) v->gate--; else v->rel = 1;
        if (v->rel) v->env = fg_mul30(v->env, v->kr);
        else if (v->att < (32767 << 12)) { v->att += v->att_inc; if (v->att > (32767 << 12)) v->att = 32767 << 12; if ((v->att << 3) > v->env) v->env = v->att << 3; }
        else v->env = v->tens + fg_mul30(v->env - v->tens, v->kd);
        int32_t a = fg_mul15((int32_t)(v->amp >> 15), (int32_t)(v->env >> 15));
        if (v->accent) a += a >> 2;
        out[i] += fg_mul15(fg_mul15(fg_soft(y), a), lvl);
        if ((v->rel || (!v->tens && v->att >= (32767 << 12))) && v->env < (FG_ONE30 >> 11)) v->on = 0;
    }
}

/* ------------------------------------------------------------ ácido --- */
static void acido_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, 0, FG_DECAY[118], FG_DECAY[18]);
    v->env = FG_ONE30;
    v->ke = fg_decay(v->p[4], 30, 100);                         /* CAÍDA del filtro: 15 ms .. 1.1 s */
    if (v->accent) v->ke = FG_DECAY[(v->p[4] * 70u / 127u + 30u) * 3u / 4u];
}
static void acido_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    int32_t g = 0, kq = (int32_t)v->p[2] * 17203 / 127;        /* RESO: 0 .. 4.2: canta solo arriba */
    unsigned rug = v->p[5];
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            if (v->note_q8 != v->target_q8) {                   /* GLIDE */
                v->note_q8 += v->gstep;
                if ((v->gstep > 0 && v->note_q8 > v->target_q8) || (v->gstep < 0 && v->note_q8 < v->target_q8) || !v->gstep) v->note_q8 = v->target_q8;
            }
            v->inc[0] = fg_note_inc(v->note_q8);
            v->inc[1] = (uint32_t)(((uint64_t)v->inc[0] * (65536u + (rug > 64 ? (rug - 64u) * 196608u / 63u : 0u))) >> 16);
            int32_t idx = 120 + (int32_t)v->p[1] * 800 / 127 + (int32_t)(((int64_t)(v->env >> 15) * ((int32_t)v->p[3] * 720 / 127 + (v->accent ? 150 : 0))) >> 15)
                        + ((v->note_q8 >> 8) - 36) * 8;
            g = FG_CUT_G[idx < 0 ? 0 : idx > 1023 ? 1023 : idx];
        }
        uint32_t old = v->ph[0];
        v->ph[0] += v->inc[0];
        int32_t x;
        if (rug <= 64) {
            int32_t saw = (int32_t)(v->ph[0] >> 16) - 32768, saw2 = (int32_t)((v->ph[0] + 0x80000000u) >> 16) - 32768;
            int32_t sq = (saw - saw2) >> 1;
            x = saw + (((sq - saw) * (int32_t)rug) >> 6);          /* RUGOSIDAD 0..64: sierra -> cuadrada */
        } else {
            if (v->ph[0] < old) v->sph = 0;                      /* 64..127: sync duro, áspero */
            v->sph += v->inc[1];
            x = (int32_t)(v->sph >> 16) - 32768;
        }
        int32_t in = fg_soft(x - (int32_t)(((int64_t)kq * v->y[3]) >> 12));
        v->y[0] += fg_mul15(g, in - v->y[0]);
        v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        v->y[2] += fg_mul15(g, v->y[1] - v->y[2]);
        v->y[3] += fg_mul15(g, v->y[2] - v->y[3]);
        int32_t y = (int32_t)(((int64_t)v->y[3] * (4096 + kq / 2)) >> 12);
        v->env = fg_mul30(v->env, v->ke);
        int32_t a = env_step(v);
        if (v->accent) a = a + (a >> 2);
        out[i] += fg_mul15(fg_mul15(fg_soft(y), a), lvl);
        if (v->rel && v->amp < 2000) v->on = 0;
    }
}

/* --------------------------------------------------------------- fm --- */
/* FM de techno: un modulador con realimentación profunda sobre una portadora. GOLPE dispara un salto de altura
 * y de índice al ataque; RUGIDO pliega y satura. Razones enteras dan bajos; las raras, metal. */
static const uint32_t FM_RATIO[9] = {32768, 65536, 92406, 131072, 180879, 196608, 229376, 340132, 467272};   /* .5 1 1.41 2 2.76 3 3.5 5.19 7.13 */
static void fm_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, 0, fg_decay(v->p[5], 68, 120), FG_DECAY[60]);   /* CAÍDA: 150 ms .. 6 s (abajo de eso era un clic) */
    v->env = FG_ONE30; v->ke = fg_decay(v->p[4], 18, 58);       /* GOLPE: el salto dura 6..60 ms */
    v->ph[0] = 0x40000000u; v->ph[1] = 0; v->fbz = 0;
}
static void fm_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    int32_t idx = 0, fb = fg_amount(v->p[3], 32767), fold = 4096 + (int32_t)v->p[6] * 230, golpe = v->p[4];
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            int32_t e = v->env >> 15;                            /* 0..32767 */
            int32_t nq = v->note_q8 + (int32_t)(((int64_t)e * golpe * 24 * 256 / 127) >> 15);   /* hasta 2 octavas arriba al golpe */
            v->inc[0] = fg_note_inc(nq);
            v->inc[1] = (uint32_t)(((uint64_t)v->inc[0] * FM_RATIO[v->p[1] * 9u / 128u]) >> 16);
            /* ÍNDICE: hasta ~19 radianes; GOLPE lo multiplica hasta x5 al ataque */
            idx = (int32_t)v->p[2] * 600;
            idx += (int32_t)(((int64_t)idx * e * golpe / 32) >> 15);
        }
        int32_t m = fg_sin(v->ph[1] + (uint32_t)(((int64_t)v->fbz * fb) << 2));   /* realimentación: hasta caos */
        v->fbz = (v->fbz + m) >> 1;
        int32_t s = fg_sin(v->ph[0] + (uint32_t)((int64_t)m * idx));
        v->ph[0] += v->inc[0]; v->ph[1] += v->inc[1];
        s = fg_fold((int32_t)(((int64_t)s * fold) >> 12));         /* RUGIDO: plegado x1..x8 */
        s = fg_soft(s + (s >> 1));                                 /* y saturación */
        v->env = fg_mul30(v->env, v->ke);
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(s, a), lvl);
        if (v->rel && v->amp < 2000) v->on = 0;
    }
}

/* ---------------------------------------------------------- cuerdas --- */
/* Pad de máquina de cuerdas: tres sierras (una afinada, dos desafinadas a los lados) y un sub cuadrado una octava
 * abajo, por un paso bajo de dos polos; el coro de la pista (CORO) lo abre. Entra con ATAQUE, se queda mientras
 * dura la nota y se va con SALIDA (hasta 4 s). */
static void cuerdas_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, v->p[4], FG_ONE30, fg_decay(v->p[5], 40, 127));
    v->y[0] = v->y[1] = 0;
    for (int k = 0; k < 4; k++) v->gpos[k] = (uint32_t)k * 0x3A5F1C27u + v->rng;
}
static void cuerdas_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    int32_t g = 0, sub = (int32_t)v->p[6] * 150;
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            if (v->nn > 1u) {                                    /* parafonía: dos sierras desafinadas por nota */
                for (unsigned k = 0; k < 4; k++) {
                    uint32_t inc = k < v->nn ? fg_note_inc(v->nq[k]) : 0u, dd = (uint32_t)(((uint64_t)inc * ((uint32_t)v->p[1] * 3u + 2u)) >> 14);
                    v->gwinc[2 * k] = inc ? inc + dd : 0u; v->gwinc[2 * k + 1] = inc ? inc - dd : 0u;
                }
                v->sph = fg_note_inc(v->nq[0]) >> 1;
            } else {
                uint32_t inc = fg_note_inc(v->note_q8), dd = (uint32_t)(((uint64_t)inc * ((uint32_t)v->p[1] * 3u + 2u)) >> 14);   /* ±0 .. 40 cents */
                v->gwinc[0] = inc; v->gwinc[1] = inc + dd; v->gwinc[2] = inc - dd; v->gwinc[3] = inc >> 1;
            }
            int32_t idx = 250 + (int32_t)v->p[2] * 650 / 127 + ((v->note_q8 >> 8) - 48) * 3;
            g = FG_CUT_G[idx < 0 ? 0 : idx > 1023 ? 1023 : idx];
        }
        int32_t s;
        if (v->nn > 1u) {
            s = 0;
            for (unsigned k = 0; k < 2u * v->nn; k++) { s += (int32_t)(v->gpos[k] >> 16) - 32768; v->gpos[k] += v->gwinc[k]; }
            s = (int32_t)(((int64_t)s * (v->nn > 2u ? 6554 : 9830)) >> 15) + ((v->subph & 0x80000000u) ? sub : -sub);
            v->subph += v->sph;
        } else {
            s = (int32_t)(v->gpos[0] >> 16) + (int32_t)(v->gpos[1] >> 16) + (int32_t)(v->gpos[2] >> 16) - 98304;
            s = (int32_t)(((int64_t)s * 10923) >> 15) + ((v->gpos[3] & 0x80000000u) ? sub : -sub);
            for (int k = 0; k < 4; k++) v->gpos[k] += v->gwinc[k];
        }
        v->y[0] += fg_mul15(g, s - v->y[0]);
        v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(fg_soft(v->y[1]) * 13 >> 5, a), lvl);   /* (−8 dB: el pad se encima con las otras voces) */
        if (v->rel && v->att >= (32767 << 12) && v->amp < 2000) v->on = 0;
    }
}

/* ---------------------------------------------------------- textura --- */
/* Texturas hechas afuera (coros, cinta, campanas largas, capas con reverbs enormes: firmware/samples) que aquí solo se
 * leen: MUESTRA cuál, POSICIÓN desde dónde, MOVIMIENTO las hace ondular (altura y brillo, lento), BRILLO su paso
 * bajo, LAVADO las manda a la reverb, SALIDA cuánto se quedan al soltar. Con teclas sostenidas, una cabeza por nota. */
static uint32_t tex_step(int32_t nq, const fg_sample_t *sm)
{
    int32_t d = nq - (int32_t)sm->root * 256;                    /* semitonos Q8 desde la nota del sample */
    uint32_t r = d >= 0 ? fg_exp2_q16((uint32_t)(d * 65536 / (12 * 256))) : (uint32_t)(((uint64_t)65536u << 16) / fg_exp2_q16((uint32_t)(-d * 65536 / (12 * 256))));
    return (uint32_t)(((uint64_t)r * sm->rate) / 44100u);
}
static const fg_sample_t *tex_of(const uint8_t *p) { return FG_SMP_TEX_N ? &FG_SMP_TEX[p[1] * FG_SMP_TEX_N / 128u] : 0; }
static void textura_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, 40, FG_ONE30, fg_decay(v->p[6], 40, 127));
    const fg_sample_t *sm = tex_of(v->p);
    for (int k = 0; k < 3; k++) fg_smpv_start(&v->hd[k], sm, sm ? (uint32_t)((uint64_t)sm->len * v->p[2] / 128u) + (uint32_t)k * 2311u : 0);
    v->y[0] = v->y[1] = 0;
}
static void textura_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    const fg_sample_t *sm = tex_of(v->p);
    if (!sm) { v->on = 0; return; }
    int32_t g = 0;
    uint32_t st[3] = {0, 0, 0};
    unsigned heads = v->nn > 1u ? (v->nn < 3u ? v->nn : 3u) : 1u;
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            v->sph += 3000u + (uint32_t)v->p[3] * 120u;            /* MOVIMIENTO: una onda lenta */
            int32_t w = fg_mul15(fg_sin(v->sph), (int32_t)v->p[3] * 258);
            for (unsigned k = 0; k < heads; k++) st[k] = tex_step((heads > 1u ? v->nq[k] : v->note_q8) + (w >> 7), sm);
            int32_t idx = 300 + (int32_t)v->p[4] * 600 / 127 + (w >> 6);
            g = FG_CUT_G[idx < 0 ? 0 : idx > 1023 ? 1023 : idx];
        }
        int32_t x = 0;
        for (unsigned k = 0; k < heads; k++) x += fg_smpv_next(&v->hd[k], st[k]);
        x = (x * (heads > 2u ? 11350 : heads > 1u ? 13900 : 19660)) >> 15;   /* −4.4 dB, y más cabezas no suben el nivel */
        v->y[0] += fg_mul15(g, x - v->y[0]); v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(v->y[1], a), lvl);
        if (v->rel && v->att >= (32767 << 12) && v->amp < 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------- dron --- */
/* Dron: sierras y un pulso que laten entre sí (LATIDO), FORMA de sierra a pulso angosto, MOVIMIENTO que deriva la
 * afinación, el ancho del pulso y el filtro despacio, BRILLO, ENSAMBLE (el coro de la pista) y SATURA. Entra lento
 * (~1 s) y se va lento (~3 s): para dejarlo sonar. Con teclas sostenidas, un par de osciladores por nota. */
static void dron_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, 100, FG_ONE30, FG_DECAY[112]);
    v->y[0] = v->y[1] = 0;
    for (int k = 0; k < 8; k++) v->gpos[k] = (uint32_t)k * 0x2F1A7C93u + v->rng;
}
static void dron_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    const uint8_t *p = v->p;
    int32_t g = 0, forma = p[2], drive = 4096 + p[6] * 170;
    uint32_t pw = 0x80000000u;
    unsigned no = v->nn > 1u ? 2u * v->nn : 3u;
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            v->sph += 1200u + (uint32_t)p[3] * 40u;              /* ~0.05 .. 0.25 Hz */
            int32_t m = fg_mul15(fg_sin(v->sph), (int32_t)p[3] * 258), m2 = fg_mul15(fg_sin(v->sph * 3u + 0x40000000u), (int32_t)p[3] * 258);
            int32_t det = (int32_t)p[1] * 3 + (m2 >> 10);            /* LATIDO: hasta ~25 cents, que respira */
            for (unsigned k = 0; k < no; k++) {
                int32_t nq = v->nn > 1u ? v->nq[k >> 1] : v->note_q8;
                int32_t c = v->nn > 1u ? ((k & 1u) ? det : -det) : (k == 0 ? 0 : k == 1 ? det : -det);
                v->gwinc[k] = fg_note_inc(nq + c / 4);
            }
            pw = 0x80000000u + (uint32_t)(m * 40000);
            int32_t idx = 280 + (int32_t)p[4] * 640 / 127 + (m >> 7) + ((v->note_q8 >> 8) - 48) * 2;
            g = FG_CUT_G[idx < 0 ? 0 : idx > 1023 ? 1023 : idx];
        }
        int32_t s = 0;
        for (unsigned k = 0; k < no; k++) {
            int32_t saw = (int32_t)(v->gpos[k] >> 16) - 32768, saw2 = (int32_t)((v->gpos[k] + pw) >> 16) - 32768;
            int32_t pulse = (saw - saw2) >> 1;
            s += saw + (((pulse - saw) * forma) >> 7);
            v->gpos[k] += v->gwinc[k];
        }
        s = (int32_t)(((int64_t)s * (no > 4u ? 5000 : no > 2u ? 8000 : 10923)) >> 15);
        v->y[0] += fg_mul15(g, s - v->y[0]); v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        int32_t y = p[6] ? fg_soft((int32_t)(((int64_t)v->y[1] * drive) >> 12)) : v->y[1];
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(y, a), lvl);
        if (v->rel && v->att >= (32767 << 12) && v->amp < 2000) v->on = 0;
    }
}

/* -------------------------------------------------------------- dub --- */
/* Stab de dub: un acorde de pulsos por un paso bajo que se cierra con CAÍDA, y el eco de cinta de la pista (en la
 * pista, ver fg_synth_render): ECO cuánto entra y cuánto se repite, TIEMPO 1/8 · 1/8 con punto · 1/4 · 3/8,
 * SUCIO el bamboleo, lo oscuro y la saturación de la cinta. */
static const int8_t DUBCH[8][3] = {{0, 3, 7}, {0, 3, 10}, {0, 7, 10}, {0, 4, 7}, {0, 5, 10}, {0, 7, 14}, {0, 3, 15}, {0, 5, 7}};
static void dub_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, 0, fg_decay(v->p[3], 66, 114), FG_DECAY[60]);   /* 130 ms .. 2.6 s */
    v->env = FG_ONE30; v->ke = fg_decay(v->p[3], 50, 105);
    v->y[0] = v->y[1] = 0;
}
static void dub_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    int32_t g = 0;
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            const int8_t *ch = DUBCH[v->p[1] * 8u / 128u];
            for (int k = 0; k < 3; k++) v->gwinc[k] = fg_note_inc(v->note_q8 + ch[k] * 256);
            int32_t idx = 200 + (int32_t)v->p[2] * 600 / 127 + (int32_t)(((int64_t)(v->env >> 15) * 260) >> 15);
            g = FG_CUT_G[idx > 1023 ? 1023 : idx];
        }
        int32_t s = 0;
        for (int k = 0; k < 3; k++) { s += (v->gpos[k] < 0x60000000u ? 20000 : -12000); v->gpos[k] += v->gwinc[k]; }   /* pulsos de 37 % */
        v->y[0] += fg_mul15(g, s - v->y[0]); v->y[1] += fg_mul15(g, v->y[0] - v->y[1]);
        v->env = fg_mul30(v->env, v->ke);
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(fg_soft(v->y[1]), a), lvl);
        if (v->rel && v->amp < 2000) v->on = 0;
    }
}

/* -------------------------------------------------------- resonador --- */
/* Seis modos (resonadores de dos polos) afinados a la nota con las razones de un MATERIAL (cuerda, barra, campana,
 * vidrio, y entre ellos), excitados por un golpe y por un ARCO de ruido continuo; CAÍDA cuánto suenan, BRILLO cuánto
 * duran y se oyen los modos altos, SIMPATÍA devuelve la salida a la excitación (crece y se sostiene: drones),
 * MOVIMIENTO les mueve la afinación despacio. */
static const uint16_t MATR[4][6] = {                             /* razones Q8 */
    {256, 512, 768, 1024, 1280, 1536}, {256, 706, 1383, 2287, 3415, 4772}, {256, 410, 573, 763, 1042, 1382}, {256, 594, 1088, 1697, 2401, 3277}};
static void reson_trig(fgy_voice_t *v, unsigned vel)
{
    env_start(v, vel, v->p[3] > 80u ? 70u : 0u, FG_ONE30, FG_DECAY[60]);
    for (int k = 0; k < 6; k++) { v->rz[k][0] = v->rz[k][1] = 0; }
    v->cnt = 0;                                                   /* los coeficientes se calculan en la primera muestra */
    v->hold = 96;                                                 /* el golpe: ~2 ms de ruido */
    v->dc = 0;
}
static void reson_render(fgy_voice_t *v, int32_t *out, int32_t lvl, unsigned n)
{
    const uint8_t *p = v->p;
    int32_t arco = 0, simp = 0, gin = 0;
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            unsigned mi = p[1] * 3u / 128u, fr = (p[1] * 3u * 256u / 128u) & 255u;
            v->sph += 900u + (uint32_t)p[6] * 60u;
            int32_t wob = fg_mul15(fg_sin(v->sph), (int32_t)p[6] * 10);
            uint32_t f0 = fg_note_inc(v->note_q8 + wob);
            /* CAÍDA: 0.1 .. 8 s el modo grave; BRILLO hace durar más los agudos */
            int32_t T = 4410 + (int32_t)p[2] * p[2] * 21;              /* muestras */
            for (int k = 0; k < 6; k++) {
                uint32_t ra = (uint32_t)MATR[mi][k] * (256u - fr) + (uint32_t)MATR[mi < 3 ? mi + 1 : 3][k] * fr;   /* Q16 */
                uint32_t inc = (uint32_t)(((uint64_t)f0 * ra) >> 16);
                if (inc > 0x3C000000u) { v->rc[k][0] = 0; v->rc[k][1] = 0; continue; }   /* arriba de ~10 kHz: fuera */
                int32_t Tk = T / (1 + (k * (127 - (int32_t)p[4])) / 40);
                int32_t r = FG_ONE30 - (int32_t)(((int64_t)FG_ONE30 * 7) / (Tk > 8 ? Tk : 8));   /* ~e^(-7/T): −60 dB en T */
                int32_t c = fg_sin(inc + 0x40000000u);              /* cos(w), Q15 */
                v->rc[k][0] = (int32_t)(((int64_t)2 * r >> 15) * c >> 0);   /* 2 r cos(w), Q30 */
                v->rc[k][1] = fg_mul30(r, r);                        /* r^2, Q30 */
                int32_t sw = fg_sin(inc);                            /* la entrada por sin(w): el golpe suena igual de fuerte en todo el teclado */
                v->gwinc[k] = (uint32_t)fg_mul15(sw, (k ? 9000 : 16000) + (int32_t)p[4] * (k ? 60 : 0));
            }
            /* el ARCO (ruido continuo) se escala con la caída: un modo largo junta mucho más */
            uint32_t q = (uint32_t)(7u * 65536u / (uint32_t)T);        /* (1 − r) Q16 */
            uint32_t rt = 0; for (uint32_t b = 1u << 15; b; b >>= 1) if ((rt + b) * (rt + b) <= q * 65536u) rt += b;   /* √ Q16 */
            arco = (int32_t)(((uint64_t)p[3] * rt * 12u) >> 16);
            simp = (int32_t)(((uint64_t)p[5] * q * 30u) >> 16);           /* SIMPATÍA: la vuelta no pasa de lo que el modo pierde */
            gin = 1;
        }
        int32_t x = fg_mul15(fg_noise(&v->rng), arco >> 1);
        if (v->hold > 0) { v->hold--; x += fg_noise(&v->rng) >> 1; }
        x += fg_mul15(v->dc, simp);                                   /* SIMPATÍA: la salida vuelve a entrar */
        int32_t s = 0;
        (void)gin;
        for (int k = 0; k < 6; k++) {
            int32_t y = fg_mul15(x, (int32_t)v->gwinc[k]) + (int32_t)(((int64_t)v->rc[k][0] * v->rz[k][0] - (int64_t)v->rc[k][1] * v->rz[k][1]) >> 30);
            if (y > 200000) y = 200000; else if (y < -200000) y = -200000;
            v->rz[k][1] = v->rz[k][0]; v->rz[k][0] = y;
            s += k ? y >> 1 : y;
        }
        v->dc = fg_soft(s >> 1);
        int32_t a = env_step(v);
        out[i] += fg_mul15(fg_mul15(v->dc, a), lvl);
        if (v->rel && v->amp < 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------ pista --- */
void fg_synth_init(fg_synth_t *s, const fg_synth_shared_t *sh, unsigned model)
{
    fg_zero(s, sizeof *s);
    fg_copy(s->p, DEF, sizeof DEF);
    s->sh = sh; s->model = (uint8_t)(model < FGY_MODELS ? model : 0);
    s->last_q8 = 36 << 8;
}
void fg_synth_param(fg_synth_t *s, unsigned model, unsigned i, unsigned v)
{
    if (model >= FGY_MODELS || i >= FGY_NP) return;
    s->p[model][i] = (uint8_t)(v > 127u ? 127u : v);
    for (int k = 0; k < FGY_VOICES; k++) {                       /* la perilla también mueve la nota que suena */
        fgy_voice_t *x = &s->v[k];
        if (x->on && x->model == model && !(x->locked & (1u << i))) x->p[i] = s->p[model][i];
    }
}
void fg_synth_model(fg_synth_t *s, unsigned model) { if (model < FGY_MODELS) s->model = (uint8_t)model; }
static int para_model(unsigned m) { return m == FGY_CUERDAS || m == FGY_TEXTURA || m == FGY_DRON; }
void fg_synth_trig(fg_synth_t *s, unsigned vel, const uint8_t *ov, unsigned mask, uint32_t gate, unsigned key)
{
    unsigned k;
    if (key != 255u && para_model(s->model)) {                  /* parafonía: una tecla más en la voz que ya se sostiene */
        for (k = 0; k < FGY_VOICES; k++) {
            fgy_voice_t *v = &s->v[k];
            if (!v->on || v->model != s->model || !v->gate || v->rel || v->key == 255u || v->nn >= 4u) continue;
            uint8_t p[FGY_NP];
            fg_apply_locks(p, s->p[s->model], ov, mask, FGY_NP);
            v->keys[v->nn] = (uint8_t)key; v->nq[v->nn] = note_q8(p); v->nn++;
            return;
        }
    }
    if (s->model == FGY_ACIDO) {                                 /* mono: la nota nueva y la cola de la anterior */
        unsigned o = s->cur & 1u;
        if (s->v[o].on) { s->v[o].rel = 1; s->v[o].gate = 0; s->v[o].kr = FG_DECAY[4]; }
        s->cur = (uint8_t)(o ^ 1u);
        k = s->cur;
    } else {                                                     /* poli: una voz libre o la más vieja */
        unsigned lim = s->maxv && s->maxv < FGY_VOICES ? s->maxv : FGY_VOICES;
        for (k = 0; k < lim && s->v[k].on; k++) { }
        if (k == lim) { k = 0; for (unsigned j = 1; j < lim; j++) if (s->v[j].age < s->v[k].age) k = j; }
    }
    fgy_voice_t *v = &s->v[k];
    fg_zero(v, sizeof *v);
    v->key = (uint8_t)key; v->age = ++s->age;
    v->rng = 0x9E3779B9u ^ (s->age * 2654435761u) ^ key;
    v->model = s->model;
    fg_apply_locks(v->p, s->p[s->model], ov, mask, FGY_NP);
    v->locked = (uint16_t)(ov ? mask : 0u);
    v->gate = gate; v->accent = vel >= 120u;
    v->target_q8 = note_q8(v->p);
    v->note_q8 = v->target_q8;
    v->nn = 1; v->keys[0] = (uint8_t)key; v->nq[0] = v->note_q8;
    if (v->model == FGY_ACIDO && v->p[6]) {                      /* GLIDE: 0 .. 400 ms desde la nota anterior */
        v->note_q8 = s->last_q8;
        int32_t steps = 1 + (int32_t)v->p[6] * 1100 / 127 / 16;
        v->gstep = (v->target_q8 - v->note_q8) / steps;
    }
    s->last_q8 = v->target_q8;
    v->on = 1;
    switch (v->model) {
    case FGY_SH: sh_trig(v, vel); break;
    case FGY_ACIDO: acido_trig(v, vel); break;
    case FGY_FM: fm_trig(v, vel); break;
    case FGY_CUERDAS: cuerdas_trig(v, vel); break;
    case FGY_TEXTURA: textura_trig(v, vel); break;
    case FGY_DRON: dron_trig(v, vel); break;
    case FGY_DUB: dub_trig(v, vel); break;
    case FGY_RESONADOR: reson_trig(v, vel); break;
    default: break;
    }
}
unsigned fg_synth_voices(const fg_synth_t *s)
{
    unsigned n = 0;
    for (int k = 0; k < FGY_VOICES; k++) n += s->v[k].on && !(s->v[k].rel && s->v[k].kr == FG_DECAY[2]);
    return n;
}
void fg_synth_limit(fg_synth_t *s, unsigned n)
{
    while (fg_synth_voices(s) > n) {
        int o = -1;
        for (int k = 0; k < FGY_VOICES; k++)
            if (s->v[k].on && !(s->v[k].rel && s->v[k].kr == FG_DECAY[2]) && (o < 0 || s->v[k].age < s->v[o].age)) o = k;
        if (o < 0) break;
        fgy_voice_t *v = &s->v[o];
        v->gate = 0; v->rel = 1; v->kr = FG_DECAY[2]; v->kd = FG_DECAY[2];   /* ~4 ms: sin clic, pero ya */
        if (v->att < (32767 << 12)) v->att = 32767 << 12;
    }
}
void fg_synth_release(fg_synth_t *s, unsigned key)
{
    for (int k = 0; k < FGY_VOICES; k++) {
        fgy_voice_t *v = &s->v[k];
        if (!v->on || !v->gate) continue;
        unsigned i;
        for (i = 0; i < v->nn && v->keys[i] != key; i++) { }
        if (i == v->nn) continue;
        if (v->nn <= 1u) { v->gate = 0; continue; }              /* la última: suelta la voz */
        for (; i + 1u < v->nn; i++) { v->keys[i] = v->keys[i + 1u]; v->nq[i] = v->nq[i + 1u]; }
        v->nn--;
        v->key = v->keys[0]; v->note_q8 = v->target_q8 = v->nq[0];
    }
}
/* el eco de cinta de DUB: a 11 kHz y 8 bits, con un paso bajo y saturación en la vuelta; SUCIO lo bambolea */
static void dub_eco(fg_synth_t *s, int32_t *x, unsigned n)
{
    fgy_eco_t *e = s->eco;
    const uint8_t *p = s->p[FGY_DUB];
    static const uint8_t MUL[4] = {2, 3, 4, 6};                   /* dieciseisavos: 1/8, 1/8 con punto, 1/4, 3/8 */
    uint32_t d = (s->step ? s->step : 5337u) * MUL[p[5] * 4u / 128u] / 4u;   /* en muestras de 11 kHz */
    if (d > FGY_ECO - 8u) d = FGY_ECO - 8u;
    int32_t send = (int32_t)p[4] * 200, fb = 9000 + (int32_t)p[4] * 170, dark = 9000 - (int32_t)p[6] * 50, sucio = p[6];
    for (unsigned i = 0; i < n; i++) {
        e->sum += x[i];
        if (++e->sub == 4u) {                                       /* cada 4 muestras, un paso de la cinta */
            e->sub = 0;
            e->ph += 600u + (uint32_t)sucio * 6u;
            int32_t wow = fg_mul15(fg_sin(e->ph), sucio * 6);       /* bamboleo: muestras Q8 */
            uint32_t rd = (e->w << 8) - (d << 8) + (uint32_t)wow, i0 = (rd >> 8) & (FGY_ECO - 1u), i1 = (i0 + 1u) & (FGY_ECO - 1u);
            int32_t fr = (int32_t)(rd & 255u), y = ((int32_t)e->b[i0] * 256 + (((int32_t)e->b[i1] - e->b[i0]) * fr)) ;   /* Q8 → ~Q15 */
            e->lp += fg_mul15(y - e->lp, dark);
            int32_t in = fg_mul15(e->sum >> 2, send) + fg_mul15(e->lp, fb);
            if (sucio) in = fg_soft(in + (in >> 2));
            e->b[e->w & (FGY_ECO - 1u)] = (int8_t)(fg_clamp16(in) >> 8);
            e->w++;
            e->sum = 0; e->prev = e->last; e->last = e->lp;
        }
        x[i] += e->prev + (((e->last - e->prev) * (int32_t)e->sub) >> 2);   /* la vuelta, interpolada a 44 kHz */
    }
}

void fg_synth_render(fg_synth_t *s, int32_t *out, unsigned n)
{
    int32_t tmp[FG_BLOCK];
    fg_zero(tmp, n * sizeof *tmp);
    for (int k = 0; k < FGY_VOICES; k++) {
        fgy_voice_t *v = &s->v[k];
        if (!v->on) continue;
        int32_t lvl = sq2(v->p[7]) * 2;
        switch (v->model) {
        case FGY_SH: sh_render(v, tmp, lvl, n); break;
        case FGY_ACIDO: acido_render(v, tmp, lvl, n); break;
        case FGY_FM: fm_render(v, tmp, lvl, n); break;
        case FGY_CUERDAS: cuerdas_render(v, tmp, lvl, n); break;
        case FGY_TEXTURA: textura_render(v, tmp, lvl, n); break;
        case FGY_DRON: dron_render(v, tmp, lvl, n); break;
        case FGY_DUB: dub_render(v, tmp, lvl, n); break;
        case FGY_RESONADOR: reson_render(v, tmp, lvl, n); break;
        default: v->on = 0; break;
        }
    }
    /* coro de la pista: CUERDAS lo abre (CORO), DRON es su ENSAMBLE, más lento */
    unsigned cm = s->model, depth = cm == FGY_CUERDAS ? s->p[cm][3] : cm == FGY_DRON ? s->p[cm][5] : 0u;
    s->xrev = cm == FGY_TEXTURA ? (int32_t)s->p[cm][5] * 258 : 0;
    if (cm == FGY_DUB && s->eco) dub_eco(s, tmp, n);
    if (depth) {
        uint32_t rate = cm == FGY_CUERDAS ? 58000u : 30000u;        /* ~0.6 Hz / ~0.3 Hz */
        int32_t base = 100 << 8, sw = (int32_t)depth * 70;     /* (cabe en la línea: hasta 169 muestras) */
        for (unsigned i = 0; i < n; i++) {
            s->cho[s->chw & (FGY_CHO - 1u)] = (int16_t)fg_clamp16(tmp[i]);
            s->chph += rate;
            int32_t d = base + (int32_t)(((int64_t)(fg_sin(s->chph) + 32768) * sw) >> 16) * 2;   /* Q8 muestras */
            uint32_t rd = (s->chw << 8) - (uint32_t)d, i0 = (rd >> 8) & (FGY_CHO - 1u), i1 = (i0 + 1u) & (FGY_CHO - 1u);
            int32_t fr = (int32_t)(rd & 255u), w = s->cho[i0] + (((s->cho[i1] - s->cho[i0]) * fr) >> 8);
            s->chw++;
            tmp[i] = (tmp[i] + w) >> 1;
        }
    }
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) { int32_t v = tmp[i]; out[i] += v; if (v < 0) v = -v; if (v > pk) pk = v; }
    s->peak = pk;
}

