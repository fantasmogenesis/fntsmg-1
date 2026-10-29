#include "fg_perc.h"

static const uint8_t DEF[FGP_MODELS][FGP_NP] = {
    {60, 40, 60, 70, 0, 50, 30, 100},     /* gota: madera afinada, sin rebotes */
    {40, 60, 60, 40, 60, 30, 30, 100},    /* chatarra */
};
static int32_t sq2(unsigned v) { return (int32_t)(v * v * 2u); }
static int32_t peg_hold(unsigned p) { return (int32_t)(p * 10u); }
static int32_t peg_drive(unsigned p) { return 4096 + (int32_t)(p * 4096u * 25u / 10u / 127u); }

/* ------------------------------------------------------------- gota --- */
static void drop_start(fgp_drop_t *v, int32_t a)
{
    v->ph[0] = 0x40000000u; v->ph[1] = 0;
    v->amp[0] = a; v->amp[1] = a;
    v->penv = FG_ONE30;
    v->click = a; v->att = 0;
}
static void drop_trig(fgp_drop_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(80u * 256u) * fg_exp2_q16(p[0] * 65536u * 52u / 1270u)) >> 16);   /* 80..3000 Hz */
    v->inc0[0] = fg_inc(f_q8);
    /* MATERIAL: el segundo modo va de 1.5x (tom) a 2.7x (madera) a 4.2x (metal) a 6.3x (vidrio), y dura más */
    uint32_t mat = p[5], ratio = mat < 64u ? 98304u + mat * (177340u - 98304u) / 64u : 177340u + (mat - 64u) * (412877u - 177340u) / 63u;
    v->inc0[1] = (uint32_t)(((uint64_t)v->inc0[0] * ratio) >> 16);
    /* GOTA: 64 = altura quieta; abajo la altura CAE al golpe (tambor); arriba SUBE (gota de agua) */
    int32_t gd = (int32_t)p[1] - 64;
    v->pdir = gd < 0 ? -gd * (3 << 22) : -gd * (1 << 21);       /* cae: arranca hasta 2 octavas arriba; sube: desde 1 abajo (Q28) */
    v->kp16 = fg_pow16(fg_decay((unsigned)(gd < 0 ? -gd : gd) * 2u, 18, 60));
    /* RESONANCIA: de madera seca (~25 ms) a vidrio que canta (~1.5 s) */
    v->k[0] = fg_decay(p[2], 56, 106);   /* (abajo de ~70 ms era un clic) */ v->k[1] = FG_DECAY[(12u + p[2] * 58u / 127u) + mat * 30u / 127u];
    /* DUREZA: mazo blando (entra suave, apagado, sin segundo modo) .. palo duro (chasquido, brillo, segundo modo) */
    unsigned md = p[3];
    v->g2 = fg_amount(md, 30000);
    v->lpa = 2500 + (int32_t)md * 238; v->lpz = 0;            /* blando: paso bajo ~550 Hz; duro: abierto */
    v->att_inc = 32767 / (int32_t)((127u - md) * 200u / 127u + 1u);
    v->kc = FG_DECAY[10]; v->clvl = fg_amount(md, 30000); v->chp = 0;
    /* REBOTE: la pelota cae desde más alto: más rebotes (0..8) y el primero más lejos (40..300 ms) */
    v->bleft = (uint8_t)(p[4] * 9u / 128u);
    v->bgap = 1760u + (uint32_t)p[4] * 90u;
    v->bwait = v->bgap;
    v->bamp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->hold = peg_hold(p[6]); v->drive = peg_drive(p[6]);
    v->lvl = sq2(p[7]);
    v->kill = 0; v->cnt = 0; v->on = 1;
    drop_start(v, v->bamp);
}
static void drop_render(fgp_drop_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if (v->bleft && !v->kill && --v->bwait == 0) {             /* rebote */
            v->bleft--; v->bamp = fg_mul30(v->bamp, 751619277);  /* 0.7 */
            v->bgap = (v->bgap * 184u) >> 8;                        /* x0.72: se acelera */
            v->bwait = v->bgap; drop_start(v, v->bamp);
        }
        if ((v->cnt++ & 15u) == 0) {
            int32_t m = (1 << 28) + (int32_t)(((int64_t)v->pdir * v->penv) >> 30);
            for (int j = 0; j < 2; j++) v->inc[j] = (uint32_t)(((uint64_t)v->inc0[j] * (uint32_t)m) >> 28);
            v->penv = fg_mul30(v->penv, v->kp16);
        }
        int32_t s = fg_mul30(fg_sin(v->ph[0]), v->amp[0]) + fg_mul15(fg_mul30(fg_sin(v->ph[1]), v->amp[1]), v->g2);
        v->ph[0] += v->inc[0]; v->ph[1] += v->inc[1];
        int32_t kill = v->kill;
        if (v->hold > 0 && !kill) v->hold--; else v->amp[0] = fg_mul30(v->amp[0], kill ? kill : v->k[0]);
        v->amp[1] = fg_mul30(v->amp[1], kill ? kill : v->k[1]);
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; s = fg_mul15(s, v->att); }
        v->lpz += fg_mul15(s - v->lpz, v->lpa); s = v->lpz;
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12)) >> 1;
        if (v->click > 2000 && v->clvl) {
            int32_t nz = fg_mul30(fg_noise(&v->seed), v->click), hp = nz - v->chp;
            v->chp += (nz - v->chp) >> 2;
            s += fg_mul15(hp, v->clvl);
            v->click = fg_mul30(v->click, kill ? kill : v->kc);
        }
        out[i] += fg_mul15(s, v->lvl) * 2;
        if (v->amp[0] < 2000 && v->click <= 2000 && (!v->bleft || kill)) v->on = 0;
    }
}

/* --------------------------------------------------------- chatarra --- */
static void scrap_trig(fgp_voice_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(40u * 256u) * fg_exp2_q16(p[0] * 65536u * 43u / 1270u)) >> 16);   /* 40..800 Hz */
    v->ia0 = fg_inc(f_q8); v->ib0 = (uint32_t)(((uint64_t)v->ia0 * 87163u) >> 16);   /* el segundo a 1.33x: inarmónico */
    v->ia = v->ia0; v->ib = v->ib0; v->pa = 0; v->pb = 0x40000000u;
    v->caos = fg_amount(p[1], 32767);                            /* CAOS: de dos ondas limpias a ruido metálico */
    uint32_t fc = (uint32_t)(((uint64_t)90u * fg_exp2_q16(p[2] * 65536u * 55u / 1270u)) >> 16);   /* PEINE: 90 Hz .. 4 kHz */
    uint32_t len = (uint32_t)FG_SR / (fc ? fc : 1u);
    v->clen = (uint16_t)(len < 8u ? 8u : len > 519u ? 519u : len); v->cpos = 0;
    for (int i = 0; i < 520; i++) v->comb[i] = 0;
    v->cfb = 18000 + (int32_t)p[2] * 90;                         /* más agudo, más resuena */
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->ka = fg_decay(p[3], 50, 104);                             /* CAÍDA: 50 ms .. 1.4 s (abajo de eso era un clic) */
    v->penv = FG_ONE30; v->kp = fg_pow16(FG_DECAY[30]);          /* golpe de altura de ~15 ms */
    v->fold = 4096 + (int32_t)p[4] * 230;                        /* ROTURA: plegado x1..x8 */
    unsigned bits = p[5];                                        /* BITS: de 16 bits limpios a 3 bits y tasa /12 */
    v->crush_mask = bits ? ~((1 << (bits * 13u / 127u)) - 1) : ~0;
    v->crush_hold = 1u + bits * 11u / 127u; v->crush_cnt = 0;
    v->nz = fg_amount(p[6], 32767);                              /* RUIDO: arena dentro de la modulación */
    v->lvl = sq2(p[7]);
    v->sa = v->sb = 0; v->clp = 0; v->kill = 0; v->cnt = 0; v->on = 1;
}
static void scrap_render(fgp_voice_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            int32_t m = (1 << 28) + (int32_t)(((int64_t)(1 << 28) * v->penv) >> 30);   /* arranca una octava arriba */
            v->ia = (uint32_t)(((uint64_t)v->ia0 * (uint32_t)m) >> 28); v->ib = (uint32_t)(((uint64_t)v->ib0 * (uint32_t)m) >> 28);
            v->penv = fg_mul30(v->penv, v->kp);
        }
        int32_t nzv = v->nz ? fg_mul15(fg_noise(&v->seed), v->nz) : 0;   /* RUIDO: hasta tanto como los osciladores */
        /* modulación de fase cruzada: cada uno empuja la fase del otro (más ruido, más caos) */
        uint32_t qa = v->pa + ((uint32_t)fg_mul15(v->sb + nzv, v->caos) << 16);
        uint32_t qb = v->pb + ((uint32_t)fg_mul15(v->sa - nzv, v->caos) << 16);
        v->sa = (qa >> 31) ? -32767 : 32767;                     /* cuadrada */
        v->sb = (int32_t)(qb >> 16) - 32768;                     /* sierra */
        v->pa += v->ia; v->pb += v->ib;
        int32_t x = (v->sa * 3 + v->sb * 2) / 5 + nzv;              /* RUIDO también entra directo: arena */
        /* peine resonante: lo vuelve metal */
        int32_t d = v->comb[v->cpos];
        v->clp += (d - v->clp) >> 1;
        v->comb[v->cpos] = (int16_t)fg_clamp16((x >> 1) + fg_mul15(v->clp, v->cfb));
        if (++v->cpos >= v->clen) v->cpos = 0;
        x = (x >> 1) + d;
        x = fg_mul30(x, v->amp);
        x = fg_fold((int32_t)(((int64_t)x * v->fold) >> 12));    /* rotura */
        x = x > 26000 ? 26000 : x < -26000 ? -26000 : x;          /* y recorte duro */
        if (v->crush_hold > 1u || v->crush_mask != ~0) {
            if (v->crush_cnt++ % v->crush_hold == 0) v->crush_v = x & v->crush_mask;
            x = v->crush_v;
        }
        out[i] += fg_mul15(x, v->lvl) * 2;
        v->amp = fg_mul30(v->amp, v->kill ? v->kill : v->ka);
        if (v->amp < 3000) v->on = 0;
    }
}

/* ------------------------------------------------------------ carril --- */
void fg_perc_init(fg_perc_t *s)
{
    fg_zero(s, sizeof *s);
    fg_copy(s->p, DEF, sizeof DEF);
    s->d[0].seed = 0xd201u; s->d[1].seed = 0xd202u; s->v[0].seed = 0x70c1u; s->v[1].seed = 0x70c2u;
}
void fg_perc_param(fg_perc_t *s, unsigned model, unsigned i, unsigned v)
{
    if (model < FGP_MODELS && i < FGP_NP) s->p[model][i] = (uint8_t)(v > 127u ? 127u : v);
}
void fg_perc_model(fg_perc_t *s, unsigned model) { if (model < FGP_MODELS) s->model = (uint8_t)model; }
void fg_perc_trig(fg_perc_t *s, unsigned vel, const uint8_t *ov, unsigned mask)
{
    uint8_t p[FGP_NP];
    fg_apply_locks(p, s->p[s->model], ov, mask, FGP_NP);
    unsigned o = s->cur;
    s->d[o].kill = FG_DECAY[2]; s->v[o].kill = FG_DECAY[2];
    s->cur ^= 1u;
    if (s->model == FGP_GOTA) drop_trig(&s->d[s->cur], p, vel);
    else scrap_trig(&s->v[s->cur], p, vel);
}
void fg_perc_render(fg_perc_t *s, int32_t *out, unsigned n)
{
    int32_t tmp[FG_BLOCK];
    fg_zero(tmp, n * sizeof *tmp);
    for (int k = 0; k < 2; k++) { drop_render(&s->d[k], tmp, n); scrap_render(&s->v[k], tmp, n); }
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) { int32_t v = tmp[i]; out[i] += v; if (v < 0) v = -v; if (v > pk) pk = v; }
    s->peak = pk;
}
