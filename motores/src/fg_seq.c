/* Secuenciador del FM-1 (ver fg_seq.h). Cada función lleva el nombre de su par en caja/worklet.js. */
#include "fg_seq.h"
#include "fg_dsp.h"

static const uint8_t MULT[3] = {4, 2, 1};                 /* ticks de 1/32 por paso: velocidad ½, 1, 2 */
static const uint8_t CONDAB[10][2] = {{0, 0}, {1, 2}, {2, 2}, {1, 3}, {2, 3}, {3, 3}, {1, 4}, {2, 4}, {3, 4}, {4, 4}};
/* AZAR: qué mueve cada motor en cada golpe — [color/tono, caída]. En los sintes nunca la NOTA. */
static const uint8_t AZ[4][2][2] = {{{0, 2}, {0, 2}}, {{0, 3}, {0, 3}}, {{2, 1}, {0, 1}}, {{0, 2}, {0, 3}}};
static const uint8_t AZS[8][2] = {{3, 6}, {1, 4}, {2, 5}, {2, 5}, {2, 3}, {4, 1}, {2, 3}, {4, 2}};   /* sh ácido fm cuerdas textura dron dub resonador: color, caída */

enum { QK_APPLY, QK_TRIG, QK_MANUAL, QK_PRE, QK_MUTES };

/* ---- aritmética entera ---- */

/* n / d con 64 bits sin pedirle nada al runtime: sólo se usa al cambiar tempo, swing o LFO */
static uint32_t divmod64(uint64_t n, uint32_t d, uint32_t *rem)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    if (rem) *rem = (uint32_t)r;
    return (uint32_t)q;
}
/* Math.round(num / den) con den > 0 (redondea .5 hacia arriba, también en negativos) */
static int32_t rdiv(int32_t num, int32_t den)
{
    int32_t x = 2 * num + den, d2 = 2 * den;
    return x >= 0 ? x / d2 : -((-x + d2 - 1) / d2);
}
static uint32_t rdivu(uint32_t num, uint32_t den_even) { return (num + den_even / 2u) / den_even; }
static int32_t fdiv(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* tiempo exacto: muestra + fracción en 1/D */
static void tadd(const fg_seq_t *q, uint32_t *s, uint32_t *f, uint32_t qs, uint32_t qf)
{
    *s += qs; *f += qf;
    if (*f >= q->D) { *f -= q->D; (*s)++; }
}
static int tcmp(uint32_t s1, uint32_t f1, uint32_t s2, uint32_t f2)
{
    int32_t d = (int32_t)(s1 - s2);
    return d ? (d < 0 ? -1 : 1) : (f1 < f2 ? -1 : f1 > f2);
}
static uint32_t sr15(const fg_seq_t *q) { return q->sr * 15u; }

/* los tres deltas de tiempo que usa todo: tick con swing hacia adelante/atrás y duración de paso */
static void retime(fg_seq_t *q)
{
    uint32_t D0 = q->D, D = (uint32_t)q->bpm * 20000u;
    uint64_t b = sr15(q);
    q->dt_q[0] = divmod64(b * (10000u + 66u * q->swing), D, &q->dt_r[0]);
    q->dt_q[1] = divmod64(b * (10000u - 66u * q->swing), D, &q->dt_r[1]);
    for (int i = 0; i < 3; i++) q->sd_q[i] = divmod64(b * MULT[i] * 10000u, D, &q->sd_r[i]);
    if (D0 && D0 != D) {                         /* las fracciones pendientes pasan al nuevo denominador */
        q->tickF = divmod64((uint64_t)q->tickF * D, D0, 0);
        for (int i = 0; i < q->nq; i++) q->q[i].f = divmod64((uint64_t)q->q[i].f * D, D0, 0);
    }
    q->D = D;
}
/* dTick(n): los pares de ticks (un 1/16) se alternan largo/corto */
static void dtick(const fg_seq_t *q, uint32_t n, uint32_t *qs, uint32_t *qf) { *qs = q->dt_q[(n >> 1) & 1u]; *qf = q->dt_r[(n >> 1) & 1u]; }

static uint32_t lfo_inc(const fg_seq_t *q, unsigned rate)
{
    uint32_t e = fg_exp2_q16(rate * 9u * 65536u / 127u);        /* 2^(rate·9/127) en Q16 */
    return divmod64((uint64_t)e * 32768u, q->sr * 10u, 0);     /* 0.05 Hz · e → fase de 32 bits por muestra */
}

/* ---- salida ---- */

static fg_seq_ev_t *emit(fg_seq_ev_t *out, int *no, int cap, unsigned at, unsigned kind, unsigned t)
{
    if (*no >= cap) return 0;
    fg_seq_ev_t *e = &out[(*no)++];
    fg_zero(e, sizeof *e);
    e->at = (uint16_t)at; e->kind = (uint8_t)kind; e->t = (uint8_t)t;
    return e;
}
static fg_seq_ev_t *emit_now(fg_seq_t *q, unsigned kind, unsigned t)
{
    int n = q->nnow;
    fg_seq_ev_t *e = emit(q->now, &n, (int)(sizeof q->now / sizeof q->now[0]), 0, kind, t);
    q->nnow = (uint8_t)n;
    return e;
}
static void ev_param(fg_seq_ev_t *e, unsigned m, unsigned i, unsigned v, unsigned fl)
{
    if (!e) return;
    e->a = (uint8_t)m; e->b = (uint8_t)i; e->v = (uint8_t)v; e->flags = (uint8_t)fl;
}
static void ev_pat(fg_seq_t *q, int queued)
{
    fg_seq_ev_t *e = emit_now(q, FG_EV_PAT, 0);
    if (e) { e->a = q->cur; e->b = (uint8_t)(queued < 0 ? 255 : queued); e->v = q->chainpos; }
}

static fg_seq_q_t *push(fg_seq_t *q, unsigned kind, unsigned t, uint32_t s, uint32_t f)
{
    if (q->nq >= FG_SEQ_QMAX) return 0;
    fg_seq_q_t *e = &q->q[q->nq++];
    fg_zero(e, sizeof *e);
    e->kind = (uint8_t)kind; e->t = (uint8_t)t; e->s = s; e->f = f;
    return e;
}

/* ---- notas y rebanadas relativas ---- */

static int relNote(int base, int lk)
{
    int sb = base * 61 / 128, s = clampi(sb + lk - 64, 0, 60), v = (s * 128 + 60) / 61;
    return v > 127 ? 127 : v;
}

static unsigned loadLocks(const fg_seq_t *q, unsigned t, const uint8_t *lk, uint8_t *buf)
{
    unsigned mask = 0, m = q->model[t];
    const uint8_t *b = q->pbase[t][m];
    for (unsigned n = 0; n < 8; n++) {
        if (lk[n] == FG_NONE) continue;
        int v = lk[n];
        if (t >= 4 && n == 0) v = relNote(b[0], v + q->transp + q->ttr[t]);   /* (el modelo 3 ya no es CHOPS: nota como los demás) */
        else if (t < 4 && n == 0 && q->ttr[t]) v = clampi(v + q->ttr[t] * 2, 0, 127);
        buf[n] = (uint8_t)v; mask |= 1u << n;
    }
    if (t >= 4 && (q->transp || q->ttr[t]) && !(mask & 1u)) {
        buf[0] = (uint8_t)relNote(b[0], 64 + q->transp + q->ttr[t]);
        mask |= 1u;
    }
    if (t < 4 && q->ttr[t] && !(mask & 1u)) { buf[0] = (uint8_t)clampi(b[0] + q->ttr[t] * 2, 0, 127); mask |= 1u; }
    return mask;
}

/* AZAR: Math.round(base + (Math.random()·2 − 1)·(az/127)·S), exacto con r/2^32 */
static int jitter(uint32_t r, int base, unsigned az, unsigned S)
{
    int64_t num = ((int64_t)2 * r - ((int64_t)1 << 32)) * (int64_t)(az * S) * 2 + ((int64_t)127 << 32);
    return base + fdiv((int32_t)(num >> 33), 127);
}
/* Math.round(vel − Math.random()·(az/127)·30) */
static int jitter_vel(uint32_t r, int vel, unsigned az)
{
    int64_t num = ((int64_t)(2 * vel + 1) * 127 << 32) - (int64_t)r * (az * 60u);
    return fdiv((int32_t)(num >> 33), 127);
}

static void fire(fg_seq_t *q, fg_seq_ev_t *e, unsigned t, int vel, const uint8_t *lk0, int32_t gate, unsigned flags)
{
    uint8_t lk[8];
    fg_copy(lk, lk0, 8);
    unsigned az = q->azar[t];
    if (az) {                                     /* cada golpe un poco distinto: color/tono ±19, caída ±16, fuerza -30 */
        unsigned m = q->model[t];
        const uint8_t *tg = t < 4 ? AZ[t][m & 1u] : AZS[m < 8 ? m : 0];
        const uint8_t *b = q->pbase[t][m];
        for (int k = 0; k < 2; k++) {
            unsigned i = tg[k];
            int base = lk[i] != FG_NONE ? lk[i] : b[i];
            lk[i] = (uint8_t)clampi(jitter(fg_rand(&q->rng), base, az, k ? 16u : 19u), 0, 127);
        }
        vel = jitter_vel(fg_rand(&q->rng), vel, az);
        if (vel < 1) vel = 1;
    }
    if (!e) return;
    e->mask = (uint16_t)loadLocks(q, t, lk, e->lk);
    e->vel = (uint8_t)vel; e->len = gate; e->flags = (uint8_t)flags;
}

/* ---- patrón ---- */

static void switchTo(fg_seq_t *q, unsigned p)
{
    q->cur = (uint8_t)p;
    for (int t = 0; t < FG_SEQ_T; t++) { q->pos[t] = -1; q->loops[t] = 0; }
    q->patTick = 0;
    ev_pat(q, -1);
}
void fg_seq_queue(fg_seq_t *q, unsigned p)
{
    if (!q->playing) { switchTo(q, p); q->queued = -1; }
    else { q->queued = (int16_t)p; ev_pat(q, (int)p); }
}
void fg_seq_chain(fg_seq_t *q, const uint8_t *list, unsigned n)
{
    if (n > FG_SEQ_CHAIN) n = FG_SEQ_CHAIN;
    fg_copy(q->chain, list, n); q->nchain = (uint8_t)n; q->chainpos = 0;
    if (!n) return;
    if (q->playing && list[0] == q->cur) { q->queued = -1; ev_pat(q, -1); }
    else fg_seq_queue(q, list[0]);
}
/* largo del patrón en ticks: lo que dura su pista más larga, redondeado a compases */
static uint32_t patTicks(const fg_seq_t *q)
{
    uint32_t m = 16;
    for (int t = 0; t < FG_SEQ_T; t++) {
        const fg_track_t *tr = &q->pat[q->cur].tr[t];
        uint32_t c = ((uint32_t)tr->len * MULT[tr->spd] + 31u) / 32u * 16u;
        if (c > m) m = c;
    }
    return m * 2u;
}

static int passes(fg_seq_t *q, unsigned t, const fg_step_t *st)
{
    unsigned c = st->c;
    if (c >= 1 && c <= 9 && q->loops[t] % CONDAB[c][1] != (uint32_t)(CONDAB[c][0] - 1)) return 0;
    if (c == FG_COND_FILL && !q->fill) return 0;
    if (c == FG_COND_NOFILL && q->fill) return 0;
    if (st->p < 100 && (uint64_t)fg_rand(&q->rng) * 100u >= (uint64_t)st->p << 32) return 0;
    return 1;
}

static int preHas(const fg_seq_t *q, int32_t key)
{
    for (int i = 0; i < q->npre; i++) if (q->pre[i] == key) return 1;
    return 0;
}

/* decide qué pasa en el tick dk (que suena en "at"); se llama 2 ticks antes */
static void decide(fg_seq_t *q, uint32_t dk, uint32_t atS, uint32_t atF)
{
    uint32_t span = patTicks(q);
    if (q->patTick > 0 && q->patTick % span == 0) {
        if (q->queued >= 0) { unsigned p = (unsigned)q->queued; q->queued = -1; switchTo(q, p); }
        else if (q->nchain > 1) { q->chainpos = (uint8_t)((q->chainpos + 1) % q->nchain); switchTo(q, q->chain[q->chainpos]); }
    }
    if (dk % 32u == 0 && q->pend) {
        q->pend = 0;
        fg_seq_q_t *e = push(q, QK_MUTES, 0, atS, atF);
        if (e) { e->mute = q->pend_mute; e->solo = q->pend_solo; }
    }
    uint32_t pk = q->patTick++;
    const fg_pattern_t *pat = &q->pat[q->cur];
    int rep = (q->perf & FG_SEQ_REP8) ? 2 : (q->perf & FG_SEQ_REP4) ? 4 : 0;
    int32_t S = (int32_t)sr15(q), bpm = q->bpm;
    for (unsigned t = 0; t < FG_SEQ_T; t++) {
        const fg_track_t *tr = &pat->tr[t];
        int m = MULT[tr->spd], len = tr->len ? tr->len : 1;
        if (pk % (uint32_t)m) continue;
        int prev = q->pos[t];
        q->pos[t] = (int16_t)((prev + 1) % len);
        if (prev >= 0 && q->pos[t] == 0) q->loops[t]++;
        int s = q->pos[t];
        if (rep && q->repK[t] >= 0) { s = ((q->repA[t] - rep + (int)(q->repK[t] % rep)) % len + len) % len; q->repK[t]++; }
        const fg_step_t *st = &tr->st[s];
        fg_seq_q_t *e = push(q, QK_APPLY, t, atS, atF);
        if (e) { e->st = (uint8_t)s; fg_copy(e->lk, st->lk, 12); for (unsigned i = 0; i < 8; i++) e->au[i] = (uint8_t)FG_AU_GET(st, i); }
        if (!st->v || ((q->perf & FG_SEQ_NOKICK) && t == 0) || ((q->perf & FG_SEQ_SOLOPERC) && t >= 4)) continue;
        if (!passes(q, t, st)) continue;
        if (q->dens[t] < 127u && st->v != 2 && (fg_rand(&q->rng) >> 25) >= q->dens[t]) continue;   /* DENSIDAD: los acentos se quedan */
        int vel = st->v == 2 ? 127 : 100, r = st->r ? st->r : 1;
        int32_t micro = rdiv(st->u * S * m, 32 * bpm);
        if (q->hum[t]) {                                          /* HUMANO: hasta ±1/64 de nota y −30 de fuerza */
            int32_t j = (int32_t)(fg_rand(&q->rng) >> 16) - 32768;
            micro += (int32_t)(((int64_t)j * q->hum[t] * S * m) >> 15) / (127 * 4 * bpm);
            vel -= (int)((fg_rand(&q->rng) >> 16) * q->hum[t] * 30u >> 16) / 127;
            if (micro < 0 && (uint32_t)(-micro) > atS - q->clock) micro = 0;
        }
        int n = 1;
        for (; n < len; n++) if (tr->st[(s + n) % len].v) break;
        int32_t gate = r > 1 ? rdiv(S * m * 9, 20 * bpm * r) : (int32_t)rdivu((uint32_t)(n * m) * (uint32_t)S * 19u, 40u * (uint32_t)bpm);
        for (int k = 0; k < r; k++) {
            fg_seq_q_t *g = push(q, QK_TRIG, t, atS + (uint32_t)(micro + rdiv(k * S * m, 2 * bpm * r)), atF);
            if (!g) break;
            g->st = (uint8_t)s; g->vel = (uint8_t)(k ? (vel * 85 + 50) / 100 : vel);
            fg_copy(g->lk, st->lk, 12); g->len = gate; g->first = k == 0;
        }
    }
    /* la caja con anticipación empieza a crecer antes de sus pasos */
    unsigned ahead = q->ahead[1];
    const fg_track_t *tr = &pat->tr[1];
    if (ahead > 0 && pk % MULT[tr->spd] == 0) {
        int len = tr->len ? tr->len : 1, mi = tr->spd;
        int32_t L = rdiv((int32_t)ahead * S * MULT[mi], 2 * bpm);
        uint32_t hs = atS, hf = atF;
        for (unsigned j = 1; j <= ahead + 1; j++) {
            tadd(q, &hs, &hf, q->sd_q[mi], q->sd_r[mi]);            /* hitAt = at + j·stepDur */
            int s = (q->pos[1] + (int)j) % len;
            if (!tr->st[s].v) continue;
            int32_t key = (int32_t)q->cur * 1000000 + (int32_t)q->loops[1] * 64 + (q->pos[1] + (int)j);
            if (preHas(q, key)) continue;
            uint32_t ss = hs - (uint32_t)L, sf = hf;
            int32_t plen = L;
            if (tcmp(atS, atF, ss, sf) >= 0) {                      /* empieza ya: dura hitAt − at, redondeado */
                uint32_t ds = hs - atS, df = hf;
                if (df < atF) { ds--; df += q->D; }
                df -= atF;
                ss = atS; sf = atF; plen = (int32_t)ds + (2u * df >= q->D ? 1 : 0);
            }
            if (q->npre < FG_SEQ_PRE) q->pre[q->npre++] = key;
            fg_seq_q_t *e = push(q, QK_PRE, 1, ss, sf);
            if (e) e->len = plen;
        }
        if (q->npre > 64) {
            for (int i = 0; i < 32; i++) q->pre[i] = q->pre[q->npre - 32 + i];
            q->npre = 32;
        }
    }
}

/* ---- LFO ---- */

static void restoreLfo2(fg_seq_t *q, unsigned t, fg_seq_ev_t *e);
/* la perilla que movía el LFO regresa a su valor (el que llama ya vio que había LFO sonando) */
static void restoreLfo(fg_seq_t *q, unsigned t, fg_seq_ev_t *e)
{
    unsigned dest = q->lfo[t][0], m = q->model[t];
    ev_param(e, m, dest, q->pbase[t][m][dest], 0);
    q->lfoLastD[t] = -1; q->lfoLastV[t] = -1;
}
static void restore_now(fg_seq_t *q, unsigned t)
{
    if (q->lfo2LastD[t] >= 0) restoreLfo2(q, t, emit_now(q, FG_EV_PARAM, t));
    if (q->lfoLastD[t] < 0) { q->lfoLastV[t] = -1; return; }
    restoreLfo(q, t, emit_now(q, FG_EV_PARAM, t));
}

static int32_t lfo_wave(uint32_t ph, unsigned shape, int32_t sh)
{
    switch (shape) {
    case FG_LFO_SIN: return fg_sin(ph);
    case FG_LFO_TRI: { uint32_t d = ph >= 0x80000000u ? ph - 0x80000000u : 0x80000000u - ph; return 32768 - (int32_t)(d >> 15); }
    case FG_LFO_SAW: return 32768 - (int32_t)(ph >> 16);
    case FG_LFO_SQR: return ph < 0x80000000u ? 32768 : -32768;
    default: return sh;
    }
}
static void restoreLfo2(fg_seq_t *q, unsigned t, fg_seq_ev_t *e)
{
    unsigned dest = (unsigned)q->lfo2LastD[t], m = q->model[t];
    ev_param(e, m, dest, q->pbase[t][m][dest], 0);
    if (e) e->flags = FG_EVF_LFO;
    q->lfo2LastD[t] = -1; q->lfo2LastV[t] = -1;
}
static void runLfo(fg_seq_t *q, unsigned n, fg_seq_ev_t *out, int *no, int cap)
{
    int32_t off2[FG_SEQ_T];
    for (unsigned t = 0; t < FG_SEQ_T; t++) {                 /* el segundo: avanza y dice cuánto mueve */
        off2[t] = 0;
        unsigned d1 = q->lfo[t][0], d2 = q->lfo2[t][0], dep2 = q->lfo2[t][2];
        if (!dep2) { if (q->lfo2LastD[t] >= 0) restoreLfo2(q, t, emit(out, no, cap, 0, FG_EV_PARAM, t)); continue; }
        uint32_t old = q->lfo2Ph[t], ph = old + q->lfo2Inc[t] * n;
        if (ph < old) q->lfo2SH[t] = fg_noise(&q->lfo_rng);
        q->lfo2Ph[t] = ph;
        off2[t] = (lfo_wave(ph, q->lfo2[t][3], q->lfo2SH[t]) * (int32_t)dep2 * 2 + 65536) >> 17;
        if (q->lfo[t][2] && d1 == d2) {                       /* la misma perilla: se suma en el primero */
            if (q->lfo2LastD[t] >= 0 && q->lfo2LastD[t] != (int16_t)d1) restoreLfo2(q, t, emit(out, no, cap, 0, FG_EV_PARAM, t));
            q->lfo2LastD[t] = -1;
            continue;
        }
        unsigned m = q->model[t];
        if (q->lfo2LastD[t] >= 0 && q->lfo2LastD[t] != (int16_t)d2) restoreLfo2(q, t, emit(out, no, cap, 0, FG_EV_PARAM, t));
        int v = clampi(q->pbase[t][m][d2] + off2[t], 0, 127);
        if (v != q->lfo2LastV[t]) {
            ev_param(emit(out, no, cap, 0, FG_EV_PARAM, t), m, d2, (unsigned)v, FG_EVF_LFO);
            q->lfo2LastD[t] = (int16_t)d2; q->lfo2LastV[t] = (int16_t)v;
        }
        off2[t] = 0;                                          /* (ya se mandó solo) */
    }
    for (unsigned t = 0; t < FG_SEQ_T; t++) {
        if (q->lfo2[t][2] && q->lfo[t][2] && q->lfo2[t][0] == q->lfo[t][0])
            off2[t] = (lfo_wave(q->lfo2Ph[t], q->lfo2[t][3], q->lfo2SH[t]) * (int32_t)q->lfo2[t][2] * 2 + 65536) >> 17;
        unsigned dest = q->lfo[t][0], depth = q->lfo[t][2], shape = q->lfo[t][3];
        if (!depth) {
            if (q->lfoLastD[t] >= 0) {
                fg_seq_ev_t *e = emit(out, no, cap, 0, FG_EV_PARAM, t);
                restoreLfo(q, t, e);
                if (e) e->flags = FG_EVF_LFO;
            }
            continue;
        }
        uint32_t old = q->lfoPh[t], ph = old + q->lfoInc[t] * n;
        if (ph < old) q->lfoSH[t] = fg_noise(&q->lfo_rng);     /* su propio azar: no mueve el de los pasos */
        q->lfoPh[t] = ph;
        int32_t w = lfo_wave(ph, shape, q->lfoSH[t]);
        unsigned m = q->model[t];
        int v = clampi(q->pbase[t][m][dest] + ((w * (int32_t)depth * 2 + 65536) >> 17) + off2[t], 0, 127);
        if (v != q->lfoLastV[t]) {
            ev_param(emit(out, no, cap, 0, FG_EV_PARAM, t), m, dest, (unsigned)v, FG_EVF_LFO);
            q->lfoLastD[t] = (int16_t)dest; q->lfoLastV[t] = (int16_t)v;
        }
    }
}

/* ---- eventos ---- */

static void runEvent(fg_seq_t *q, const fg_seq_q_t *e, unsigned at, fg_seq_ev_t *out, int *no, int cap)
{
    unsigned t = e->t;
    switch (e->kind) {
    case QK_APPLY: {
        unsigned m = q->model[t], any = 0;
        for (unsigned i = 0; i < 8; i++) {
            if (e->au[i] == FG_NONE) continue;
            ev_param(emit(out, no, cap, at, FG_EV_PARAM, t), m, i, e->au[i], FG_EVF_AUTO);
            q->pbase[t][m][i] = e->au[i]; any = 1;
        }
        for (unsigned i = 0; i < 4; i++) {
            unsigned v = e->lk[8 + i];
            if (v != FG_NONE) { ev_param(emit(out, no, cap, at, FG_EV_MIX, t), 0, i, v, 0); q->mixLocked[t] |= (uint8_t)(1u << i); }
            else if (q->mixLocked[t] & (1u << i)) { ev_param(emit(out, no, cap, at, FG_EV_MIX, t), 0, i, q->mix[t][i], 0); q->mixLocked[t] &= (uint8_t)~(1u << i); }
        }
        fg_seq_ev_t *s = emit(out, no, cap, at, FG_EV_STEP, t);
        if (s) { s->s = e->st; s->v = (uint8_t)any; }
        break; }
    case QK_TRIG:
    case QK_MANUAL: {
        fg_seq_ev_t *g = emit(out, no, cap, at, FG_EV_TRIG, t);
        if (g) { g->s = e->st; if (e->kind == QK_MANUAL) g->a = e->st; }   /* (la nota a mano lleva su tecla) */
        fire(q, g, t, e->vel, e->lk, e->len, e->kind == QK_MANUAL ? FG_EVF_MANUAL : e->first ? FG_EVF_FIRST : 0);
        break; }
    case QK_PRE: { fg_seq_ev_t *p = emit(out, no, cap, at, FG_EV_PRE, t); if (p) p->len = e->len; break; }
    case QK_MUTES: {
        q->mute = e->mute; q->solo = e->solo;
        fg_seq_ev_t *m = emit(out, no, cap, at, FG_EV_MUTE, 0);
        if (m) { m->a = e->mute; m->b = e->solo; }
        break; }
    }
}

static void drain_now(fg_seq_t *q, fg_seq_ev_t *out, int *no, int cap)
{
    for (int i = 0; i < q->nnow; i++) if (*no < cap) out[(*no)++] = q->now[i];
    q->nnow = 0;
}

int fg_seq_process(fg_seq_t *q, unsigned n, fg_seq_ev_t *out, int cap)
{
    int no = 0;
    uint32_t end = q->clock + n;
    if (q->playing) while ((int32_t)(q->tickS - end) < 0) {
        uint32_t as = q->tickS, af = q->tickF, ds, df;
        dtick(q, q->tick, &ds, &df); tadd(q, &as, &af, ds, df);
        dtick(q, q->tick + 1, &ds, &df); tadd(q, &as, &af, ds, df);
        decide(q, q->tick + 2, as, af);
        dtick(q, q->tick, &ds, &df); tadd(q, &q->tickS, &q->tickF, ds, df);
        q->tick++;
    }
    drain_now(q, out, &no, cap);
    runLfo(q, n, out, &no, cap);

    uint8_t due[FG_SEQ_QMAX];
    int nd = 0;
    for (int i = 0; i < q->nq; i++) if ((int32_t)(q->q[i].s - end) < 0) due[nd++] = (uint8_t)i;
    for (int i = 1; i < nd; i++) {                                /* orden estable por tiempo exacto */
        uint8_t x = due[i]; int j = i - 1;
        while (j >= 0 && tcmp(q->q[due[j]].s, q->q[due[j]].f, q->q[x].s, q->q[x].f) > 0) { due[j + 1] = due[j]; j--; }
        due[j + 1] = x;
    }
    int32_t o = 0;
    for (int i = 0; i < nd; i++) {
        const fg_seq_q_t *e = &q->q[due[i]];
        int32_t at = (int32_t)(e->s - q->clock) + (2u * e->f >= q->D ? 1 : 0);
        if (at < o) at = o;
        if (at > (int32_t)n) at = (int32_t)n;
        o = at;
        runEvent(q, e, (unsigned)at, out, &no, cap);
    }
    if (nd) {                                                     /* saca de la cola lo que ya sonó */
        int w = 0;
        for (int i = 0; i < q->nq; i++) if ((int32_t)(q->q[i].s - end) >= 0) { if (w != i) q->q[w] = q->q[i]; w++; }
        q->nq = (uint16_t)w;
    }
    q->clock = end;
    return no;
}

/* ---- control ---- */

void fg_seq_clear_track(fg_track_t *tr)
{
    for (int s = 0; s < FG_SEQ_NS; s++) {
        fg_step_t *st = &tr->st[s];
        st->v = 0; st->c = 0; st->p = 100; st->r = 1; st->u = 0;
        for (int i = 0; i < 12; i++) st->lk[i] = FG_NONE;
        for (int i = 0; i < 4; i++) st->au[i] = FG_NONE;
    }
    tr->len = 16; tr->spd = FG_SPD_1;
}

void fg_seq_init(fg_seq_t *q, uint32_t sr, uint32_t seed)
{
    fg_zero(q, sizeof *q);
    for (int p = 0; p < FG_SEQ_NP; p++) for (int t = 0; t < FG_SEQ_T; t++) fg_seq_clear_track(&q->pat[p].tr[t]);
    q->sr = sr;
    for (int t = 0; t < FG_SEQ_T; t++) {
        for (int m = 0; m < FG_SEQ_NM; m++) for (int i = 0; i < 8; i++) q->pbase[t][m][i] = 64;
        q->mix[t][0] = 100; q->mix[t][1] = 64;
        q->lfo[t][1] = 40;
        q->pos[t] = -1; q->repK[t] = -1;
        q->lfoLastD[t] = -1; q->lfoLastV[t] = -1;
        q->lfo2[t][1] = 40; q->lfo2LastD[t] = -1; q->lfo2LastV[t] = -1;
        q->dens[t] = 127;
    }
    q->bpm = 124; q->queued = -1;
    q->rng = seed ? seed : 1u;
    q->lfo_rng = (seed ^ 0x9E3779B9u) ? (seed ^ 0x9E3779B9u) : 1u;
    retime(q);
    for (int t = 0; t < FG_SEQ_T; t++) { q->lfoInc[t] = lfo_inc(q, q->lfo[t][1]); q->lfo2Inc[t] = lfo_inc(q, q->lfo2[t][1]); }
}

uint32_t fg_seq_step_samples(const fg_seq_t *q) { return (sr15(q) * 2u + q->bpm) / (2u * q->bpm); }

void fg_seq_params(const fg_seq_t *q, unsigned t, unsigned mask, const uint8_t *lk, uint8_t *out)
{
    const uint8_t *b = q->pbase[t][q->model[t]];
    for (unsigned i = 0; i < 8; i++) out[i] = ((mask >> i) & 1u) ? lk[i] : b[i];
}

void fg_seq_play(fg_seq_t *q, int on)
{
    q->playing = (uint8_t)(on != 0);
    int w = 0;
    for (int i = 0; i < q->nq; i++) if (q->q[i].kind == QK_MANUAL) q->q[w++] = q->q[i];
    q->nq = (uint16_t)w; q->npre = 0;
    if (!on) return;
    for (int t = 0; t < FG_SEQ_T; t++) { q->pos[t] = -1; q->loops[t] = 0; }
    q->patTick = 0; q->tick = 0; q->tickS = q->clock + 64u; q->tickF = 0;
    decide(q, 0, q->tickS, q->tickF);
    uint32_t s = q->tickS, f = q->tickF, ds, df;
    dtick(q, 0, &ds, &df); tadd(q, &s, &f, ds, df);
    decide(q, 1, s, f);
}
void fg_seq_tempo(fg_seq_t *q, unsigned bpm) { q->bpm = (uint8_t)bpm; retime(q); }
void fg_seq_swing(fg_seq_t *q, unsigned swing) { q->swing = (uint8_t)swing; retime(q); }
void fg_seq_param(fg_seq_t *q, unsigned t, unsigned m, unsigned i, unsigned v) { q->pbase[t][m][i] = (uint8_t)v; q->lfoLastV[t] = -1; }
void fg_seq_model(fg_seq_t *q, unsigned t, unsigned m) { restore_now(q, t); q->model[t] = (uint8_t)m; }
void fg_seq_mix(fg_seq_t *q, unsigned t, unsigned i, unsigned v) { q->mix[t][i] = (uint8_t)v; }
void fg_seq_lfo(fg_seq_t *q, unsigned t, const uint8_t v[4])
{
    restore_now(q, t);
    fg_copy(q->lfo[t], v, 4);
    q->lfoInc[t] = lfo_inc(q, v[1]);
}
void fg_seq_lfo2(fg_seq_t *q, unsigned t, const uint8_t v[4])
{
    restore_now(q, t);
    fg_copy(q->lfo2[t], v, 4);
    q->lfo2Inc[t] = lfo_inc(q, v[1]);
}
void fg_seq_note_at(fg_seq_t *q, unsigned t, unsigned vel, const uint8_t lk[8], int32_t gate, uint32_t at)
{
    fg_seq_q_t *e = push(q, QK_MANUAL, t, at, 0);
    if (e) { e->vel = (uint8_t)vel; fg_copy(e->lk, lk, 8); for (int i = 8; i < 12; i++) e->lk[i] = FG_NONE; e->len = gate; e->st = 255; }
}
void fg_seq_track2(fg_seq_t *q, unsigned t, unsigned i, unsigned v)
{
    if (t >= FG_SEQ_T) return;
    if (i == 1) q->ttr[t] = (int8_t)clampi((int)v - 64, -24, 24);
    else if (i == 2) q->hum[t] = (uint8_t)(v > 127u ? 127u : v);
    else if (i == 3) q->dens[t] = (uint8_t)(v > 127u ? 127u : v);
}
void fg_seq_mutes(fg_seq_t *q, unsigned mute, unsigned solo, int now)
{
    if (q->muteq && q->playing && !now) { q->pend = 1; q->pend_mute = (uint8_t)mute; q->pend_solo = (uint8_t)solo; return; }
    q->mute = (uint8_t)mute; q->solo = (uint8_t)solo;
    fg_seq_ev_t *e = emit_now(q, FG_EV_MUTE, 0);
    if (e) { e->a = (uint8_t)mute; e->b = (uint8_t)solo; }
}
void fg_seq_perf(fg_seq_t *q, unsigned seqflags)
{
    for (int t = 0; t < FG_SEQ_T; t++) {
        if (seqflags & (FG_SEQ_REP4 | FG_SEQ_REP8)) { if (q->repK[t] < 0) { q->repA[t] = (int16_t)(q->pos[t] + 1); q->repK[t] = 0; } }
        else q->repK[t] = -1;
    }
    q->perf = (uint8_t)seqflags;
}
void fg_seq_fill(fg_seq_t *q, int on) { q->fill = (uint8_t)(on != 0); }

void fg_seq_audition(fg_seq_t *q, unsigned t, unsigned vel, const uint8_t lk[8], unsigned key, int32_t hold)
{
    int32_t gate = hold > 0 ? hold : rdiv((int32_t)q->sr * 57, q->bpm);
    unsigned ahead = q->ahead[t];
    if (ahead > 0) {
        int32_t L = rdiv((int32_t)(ahead * sr15(q)), q->bpm);
        fg_seq_ev_t *p = emit_now(q, FG_EV_PRE, t);
        if (p) p->len = L;
        fg_seq_q_t *e = push(q, QK_MANUAL, t, q->clock + (uint32_t)L, 0);
        if (e) { e->vel = (uint8_t)vel; fg_copy(e->lk, lk, 8); for (int i = 8; i < 12; i++) e->lk[i] = FG_NONE; e->len = gate; e->st = (uint8_t)key; }
    } else {
        fg_seq_ev_t *g = emit_now(q, FG_EV_TRIG, t);
        fire(q, g, t, (int)vel, lk, gate, FG_EVF_MANUAL);
        if (g) g->a = (uint8_t)key;
    }
}
