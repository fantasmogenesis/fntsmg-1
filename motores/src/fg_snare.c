#include "fg_snare.h"

static const uint8_t DEF[FGS_MODELS][FGS_NP] = {
    {40, 80, 90, 50, 70, 60, 70, 105},    /* caja: 909 */
    {55, 70, 45, 50, 30, 55, 40, 105},    /* aplauso */
    {64, 24, 0, 127, 64, 0, 0, 100},      /* sample: tal cual (fg_samp.h) */
};
static int32_t sq2(unsigned v) { return (int32_t)(v * v * 2u); }

/* coeficiente de un filtro de estado variable: 2 sin(pi fc / SR) en Q15 */
static int32_t svf_f(uint32_t fc_hz)
{
    return 2 * fg_sin(fg_inc(fc_hz << 8) >> 1);
}

/* ------------------------------------------------------------- caja --- */
/* La caja de las máquinas (909 / 808): dos tonos del parche (el segundo a ×1.6, como la 909) que caen de afinación al
 * golpe, ruido brillante con su propia caída y un chasquido de ruido al principio. Lo que la hace pegar: el cuerpo
 * sostenido unos ms y la saturación (PEGADA), y que ya no sale 6 dB abajo. */
static void voice_trig(fgs_voice_t *v, const uint8_t *p, unsigned vel)
{
    uint32_t f_q8 = (uint32_t)(((uint64_t)(120u * 256u) * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 1737u / 127000u))) >> 16);   /* TONO 120..400 Hz */
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    unsigned idx = 50u + p[1] * 42u / 127u;                          /* CUERPO: suena 50 .. 600 ms (60 dB) */
    v->inc0[0] = fg_inc(f_q8);
    v->inc0[1] = (uint32_t)(((uint64_t)v->inc0[0] * 104858u) >> 16);
    v->inc[0] = v->inc0[0]; v->inc[1] = v->inc0[1];
    v->ph[0] = 0x40000000u; v->ph[1] = 0;
    v->amp[0] = a; v->amp[1] = a / 5 * 3;
    v->k[0] = FG_DECAY[idx]; v->k[1] = FG_DECAY[idx - 8u];
    v->g = 9000 + (int32_t)p[1] * 190;                              /* y cuánto se oye */
    v->tenv = FG_ONE30; v->kt16 = fg_pow16(FG_DECAY[38]);              /* cae de afinación en ~25 ms */
    v->depth = 80000000 + (int32_t)p[5] * 700000;                    /* de +30 % a +63 % al golpe: más chasquido, más caída */
    v->wire = a; v->kw = fg_decay(p[3], 62, 108);                    /* CAÍDA del ruido: 90 ms .. 1.3 s */
    v->wlvl = fg_amount(p[2], 60000);                                /* RUIDO */
    v->snap = a; v->ksnap = FG_DECAY[30];                              /* chasquido: ~15 ms */
    v->slvl = fg_amount(p[5] > 90u ? 90u : p[5], 50000);
    uint32_t fhp = 300u + p[4] * 3700u / 127u;                       /* BRILLO: el ruido desde 0.3 .. 4 kHz y hasta 6 .. 14 kHz */
    v->chp = (int32_t)(fhp * 467u / 100u);
    v->clp = 20000 + (int32_t)p[4] * 80;
    v->hpz = v->lpz = 0;
    v->click = p[5] > 90u ? a : 0; v->kc = FG_DECAY[22]; v->clvl = (int32_t)(p[5] > 90u ? (p[5] - 90u) * 700u : 0u);   /* el aro */
    v->rph[0] = v->rph[1] = 0;
    v->rinc[0] = fg_inc(640u << 8); v->rinc[1] = fg_inc(1520u << 8);
    v->hold = (int32_t)(p[6] * 9u);                                    /* PEGADA: el cuerpo se sostiene hasta ~26 ms */
    v->drive = 4096 + (int32_t)(p[6] * 4096u * 3u / 127u);             /* y satura x1..x4 */
    v->kill = 0;
    v->cnt = 0; v->on = 1;
}

static void voice_render(fgs_voice_t *v, int32_t lvl, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if ((v->cnt++ & 15u) == 0) {
            uint32_t m = (1u << 28) + (uint32_t)(((int64_t)v->depth * v->tenv) >> 30);
            v->inc[0] = (uint32_t)(((uint64_t)v->inc0[0] * m) >> 28);
            v->inc[1] = (uint32_t)(((uint64_t)v->inc0[1] * m) >> 28);
            v->tenv = fg_mul30(v->tenv, v->kt16);
        }
        int32_t kill = v->kill;
        int32_t body = fg_mul15(fg_mul30(fg_sin(v->ph[0]), v->amp[0]) + fg_mul30(fg_sin(v->ph[1]), v->amp[1]), v->g);
        v->ph[0] += v->inc[0]; v->ph[1] += v->inc[1];
        if (v->hold > 0 && !kill) v->hold--; else { v->amp[0] = fg_mul30(v->amp[0], kill ? kill : v->k[0]); v->amp[1] = fg_mul30(v->amp[1], kill ? kill : v->k[1]); }
        int32_t nz = fg_noise(&v->seed);
        v->lpz += fg_mul15(nz - v->lpz, v->clp);
        v->hpz += fg_mul15(v->lpz - v->hpz, v->chp);
        int32_t w = v->lpz - v->hpz;
        int32_t s = body + fg_mul15(fg_mul30(w, v->wire), v->wlvl) + fg_mul15(fg_mul30(w, v->snap), v->slvl);
        v->wire = fg_mul30(v->wire, kill ? kill : v->kw);
        v->snap = fg_mul30(v->snap, kill ? kill : v->ksnap);
        s = fg_soft((int32_t)(((int64_t)s * v->drive) >> 12));
        if (v->click > 2000) {                                        /* palo contra el aro, encima de la saturación */
            int32_t r = (fg_sin(v->rph[0]) + (fg_sin(v->rph[1]) >> 1)) >> 1;
            s += fg_mul15(fg_mul30(r + (fg_noise(&v->seed) >> 3), v->click), v->clvl);
            v->rph[0] += v->rinc[0]; v->rph[1] += v->rinc[1];
            v->click = fg_mul30(v->click, kill ? kill : v->kc);
        }
        out[i] += fg_mul15(s, lvl) * 2;
        if (v->amp[0] < 2000 && v->wire < 2000 && v->snap < 2000 && v->click <= 2000) v->on = 0;
    }
}

/* ------------------------------------------------------------ aplauso --- */
/* Ruido por una banda (700..2.6 kHz). Cada ráfaga entra de golpe y se apaga en ~5 ms; las palmas llegan
 * con separación despareja y fuerza al azar. La última deja la cola (CAÍDA); SALA es otra cola, oscura y
 * más larga, mezclada aparte para que se oiga aunque la cola principal sea corta. */
static void clap_trig(fgs_clap_t *c, const uint8_t *p, unsigned vel)
{
    int32_t a = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    uint32_t fc = (uint32_t)((700ull * fg_exp2_q16((uint32_t)((uint64_t)p[0] * 65536u * 1893u / 127000u))) >> 16);   /* 700 Hz .. 2.6 kHz */
    c->sf = svf_f(fc);
    c->sq = 40000 - (int32_t)(p[5] * 260u);                       /* COLOR: banda ancha y crujiente .. angosta y con tono (Q 0.8..5) */
    c->gain = c->sq;                                               /* la banda angosta suena más fuerte: se compensa */
    c->left = (uint8_t)(p[1] * 5u / 127u);                         /* PALMAS: 1..6 ráfagas */
    c->gap = 176u + p[2] * 1146u / 127u;                           /* SEPARA: 4..30 ms */
    c->a = a; c->t = 0; c->next = 0;
    c->env = 0; c->kb = FG_DECAY[12];
    c->tail = 0; c->kt = fg_decay(p[3], 50, 104);                  /* CAÍDA: ~50 ms .. ~1.2 s */
    c->room = 0; c->kr = fg_decay(p[3] / 2u + 64u, 70, 116);       /* la sala dura más que la cola */
    c->rlvl = fg_amount(p[4], 30000);
    c->hold = (int32_t)(p[6] * 8u);                                /* PEGADA: sostiene la cola hasta ~23 ms */
    c->drive = 4096 + (int32_t)(p[6] * 4096u * 3u / 127u);         /* y satura x1..x4 */
    c->lo = c->bp = c->lo2 = c->bp2 = c->rlp = 0; c->kill = 0; c->on = 1;
}

static void clap_render(fgs_clap_t *c, int32_t lvl, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && c->on; i++) {
        if (c->t == c->next && !c->kill) {                        /* llega una palma */
            uint32_t r = fg_rand(&c->seed);
            int32_t force = 21000 + (int32_t)((r >> 16) % 11768u); /* 0.65 .. 1.0 */
            c->env = fg_mul15(c->a, force);
            if (c->left) { c->left--; c->next = c->t + c->gap * (870u + (r & 255u)) / 1000u; }   /* ±13 % */
            else { c->tail = c->a; c->room = c->a; c->next = 0xFFFFFFFFu; }
        }
        c->t++;
        int32_t kill = c->kill;
        int32_t nz = fg_noise(&c->seed);
        c->lo += fg_mul15(c->sf, c->bp);
        int32_t hi = fg_mul15(nz, c->gain) - c->lo - fg_mul15(c->sq, c->bp);
        c->bp += fg_mul15(c->sf, hi);
        c->bp = fg_clamp16(c->bp); c->lo = fg_clamp16(c->lo);
        c->lo2 += fg_mul15(c->sf, c->bp2);                         /* segunda banda igual: faldas de 12 dB/oct */
        int32_t hi2 = fg_mul15(c->bp, c->gain) - c->lo2 - fg_mul15(c->sq, c->bp2);
        c->bp2 += fg_mul15(c->sf, hi2);
        c->bp2 = fg_clamp16(c->bp2); c->lo2 = fg_clamp16(c->lo2);
        int32_t s = fg_mul30(c->bp2 * 6, c->env + c->tail);                 /* (+6 dB: el aplauso quedaba 12 dB abajo del bombo) */
        c->rlp += fg_mul15(nz - c->rlp, 5000);                     /* sala: ruido oscuro (~1.2 kHz) */
        s += fg_mul15(fg_mul30(c->rlp * 2, c->room), c->rlvl);
        s = fg_soft((int32_t)(((int64_t)s * c->drive) >> 12));
        c->env = fg_mul30(c->env, kill ? kill : c->kb);
        if (c->hold > 0 && c->tail && !kill) c->hold--; else c->tail = fg_mul30(c->tail, kill ? kill : c->kt);
        c->room = fg_mul30(c->room, kill ? kill : c->kr);
        out[i] += fg_mul15(s, lvl) * 2;
        if (c->next == 0xFFFFFFFFu && c->env < 2000 && c->tail < 2000 && c->room < 2000) c->on = 0;
        if (kill && c->env < 2000 && c->tail < 2000 && c->room < 2000) c->on = 0;
    }
}

/* ------------------------------------------------------------ carril --- */
void fg_snare_init(fg_snare_t *s)
{
    fg_zero(s, sizeof *s);
    fg_copy(s->p, DEF, sizeof DEF);
    s->v[0].seed = 0x2468ace1u; s->v[1].seed = 0x13579bdfu; s->c[0].seed = 0x0badf00du; s->c[1].seed = 0x5eed1234u;
}

void fg_snare_param(fg_snare_t *s, unsigned model, unsigned i, unsigned v)
{
    if (model < FGS_MODELS && i < FGS_NP) s->p[model][i] = (uint8_t)(v > 127u ? 127u : v);
}

void fg_snare_model(fg_snare_t *s, unsigned model) { if (model < FGS_MODELS) s->model = (uint8_t)model; }

unsigned fg_snare_ahead(const fg_snare_t *s) { (void)s; return 0u; }
void fg_snare_pre(fg_snare_t *s, uint32_t samples) { (void)s; (void)samples; }

void fg_snare_trig(fg_snare_t *s, unsigned vel, const uint8_t *ov, unsigned mask)
{
    uint8_t p[FGS_NP];
    fg_apply_locks(p, s->p[s->model], ov, mask, FGS_NP);
    unsigned o = s->cur;
    s->v[o].kill = FG_DECAY[2]; s->c[o].kill = FG_DECAY[6]; s->sm[o].kill = FG_DECAY[6];
    s->cur ^= 1u;
    if (s->model == FGS_CAJA) voice_trig(&s->v[s->cur], p, vel);
    else if (s->model == FGS_SAMPLE) fg_sampv_trig(&s->sm[s->cur], p, vel, 0, 0);
    else clap_trig(&s->c[s->cur], p, vel);
}

void fg_snare_render(fg_snare_t *s, int32_t *out, unsigned n)
{
    int32_t tmp[FG_BLOCK];
    fg_zero(tmp, n * sizeof *tmp);
    int32_t lvl = sq2(s->p[s->model][7]);
    voice_render(&s->v[0], lvl, tmp, n);
    voice_render(&s->v[1], lvl, tmp, n);
    clap_render(&s->c[0], lvl, tmp, n);
    clap_render(&s->c[1], lvl, tmp, n);
    fg_sampv_render(&s->sm[0], tmp, n);
    fg_sampv_render(&s->sm[1], tmp, n);
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) { int32_t v = tmp[i]; out[i] += v; if (v < 0) v = -v; if (v > pk) pk = v; }
    s->peak = pk;
}
