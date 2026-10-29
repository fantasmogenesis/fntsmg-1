#include "fg_mix.h"

static const uint16_t COMB[4] = {1116, 1188, 1277, 1356}, AP[2] = {556, 441};
/* DELAY TIEMPO: 8 divisiones, en medios dieciseisavos: 1/32 1/16 3/32 1/8 3/16 1/4 3/8 1/2 */
static const uint8_t DIV[8] = {1, 2, 3, 4, 6, 8, 12, 16};

void fg_mix_init(fg_mix_t *m)
{
    fg_zero(m, sizeof *m);
    for (int t = 0; t < FG_TRACKS; t++) { m->st[t].v[FGX_VOL] = 100; m->st[t].v[FGX_PAN] = 64; m->st[t].v[FGX_FILT] = 64; m->st[t].v[FGX_DUCK] = 64; }
    for (int c = 0; c < 4; c++) { m->cl[c].len = COMB[c] / FG_REV_DIV; m->cr[c].len = (uint16_t)((COMB[c] + 23u) / FG_REV_DIV); }
    for (int a = 0; a < 2; a++) { m->al[a].len = AP[a] / FG_REV_DIV; m->ar[a].len = (uint16_t)((AP[a] + 23u) / FG_REV_DIV); }
    static const uint8_t FX0[FGF_N] = {4, 50, 70, 70, 60, 64, 0, 30};
    for (int i = 0; i < FGF_N; i++) m->fx[i] = FX0[i];
    m->comp_gain = 4096;
    for (int i = 0; i < 12; i++) m->pk[i][0] = m->pk[i][1] = 64;
    m->seed = 0xfacade1u;
    fg_mix_tempo(m, 5337);
}

void fg_mix_xrev(fg_mix_t *m, unsigned t, int32_t v) { if (t < FG_TRACKS) m->st[t].xrev = v; }
void fg_mix_perfk(fg_mix_t *m, unsigned bit, unsigned i, unsigned v)
{
    if (bit < 12u && i < 2u) m->pk[bit][i] = (uint8_t)(v > 127u ? 127u : v);
    if (bit == 3u) m->gate_len = m->step * (m->pk[3][0] < 43u ? 2u : m->pk[3][0] < 86u ? 1u : 0u) + (m->pk[3][0] >= 86u ? m->step / 2u : 0u);
}
void fg_mix_strip(fg_mix_t *m, unsigned t, unsigned which, unsigned v)
{
    if (t >= FG_TRACKS || which >= FGX_STRIP) return;
    fg_strip_t *s = &m->st[t];
    s->v[which] = (uint8_t)(v > 127u ? 127u : v);
    if (which == FGX_FILT) {                                /* abajo de 64: graves (18 kHz .. 60 Hz); arriba: agudos (20 Hz .. 8 kHz) */
        unsigned f = s->v[FGX_FILT];
        uint32_t hz = f < 64u ? (uint32_t)(((uint64_t)60u * fg_exp2_q16(f * 65536u * 82u / 640u)) >> 16)
                              : (uint32_t)(((uint64_t)20u * fg_exp2_q16((f - 64u) * 65536u * 86u / 630u)) >> 16);
        if (hz > 15000u) hz = 15000u;
        s->ff = fg_svf2_f(hz);
        s->fq = 30000;                                      /* (un poco de resonancia fija: la perilla es ANCHO) */
    }
}
void fg_mix_mute(fg_mix_t *m, unsigned t, unsigned mute, unsigned solo)
{
    if (t < FG_TRACKS) { m->st[t].mute = (uint8_t)mute; m->st[t].solo = (uint8_t)solo; }
}
void fg_mix_fx(fg_mix_t *m, unsigned which, unsigned v)
{
    if (which >= FGF_N) return;
    m->fx[which] = (uint8_t)(v > 127u ? 127u : v);
    if (which == FGF_DTIME) fg_mix_tempo(m, m->step);
}
void fg_mix_tempo(fg_mix_t *m, uint32_t step)
{
    m->step = step;
    uint32_t d = step * DIV[m->fx[FGF_DTIME] * 8u / 128u] / 2u;
    uint32_t top = (FG_DLEN - 1u) << FG_DLY_HALF;               /* (dlen sigue en muestras de 44 kHz) */
    m->dlen = d >= top ? top : d < 16u ? 16u : d;
    m->gate_len = step;
    fg_mix_perfk(m, 3, 0, m->pk[3][0]);
}
void fg_mix_perf(fg_mix_t *m, uint32_t flags)
{
    if ((flags & FGP_RISER) && !(m->perf & FGP_RISER)) { m->riser_t = 0; m->rs.lo = m->rs.bp = 0; }
    if ((flags & (FGP_LP | FGP_HP)) && !(m->perf & (FGP_LP | FGP_HP))) m->fsweep = 0;
    m->perf = flags;
}

static int32_t comb(fg_comb_t *c, int32_t x, int32_t fb, int32_t damp)
{
    int32_t y = c->buf[c->pos];
    c->lp = y + fg_mul15(c->lp - y, damp);
    c->buf[c->pos] = (int16_t)fg_clamp16(x + fg_mul15(c->lp, fb));
    if (++c->pos >= c->len) c->pos = 0;
    return y;
}
static int32_t allpass(fg_ap_t *a, int32_t x)
{
    int32_t b = a->buf[a->pos];
    a->buf[a->pos] = (int16_t)fg_clamp16(x + (b >> 1));
    if (++a->pos >= a->len) a->pos = 0;
    return b - x;
}

/* filtro DJ: 64 nada; abajo pasabajos de 18 kHz a 140 Hz; arriba pasaaltos de 20 Hz a 5 kHz */
static void dj_coef(fg_mix_t *m)
{
    int32_t v = m->fx[FGF_FILT], q = 23000;
    if (m->perf & FGP_LP) { if (m->fsweep < 127) m->fsweep += 2; v = m->pk[0][0] != 64u ? m->pk[0][0] / 2 : 64 - m->fsweep / 2; q = 36000 - m->pk[0][1] * 260; }
    if (m->perf & FGP_HP) { if (m->fsweep < 127) m->fsweep += 2; v = m->pk[1][0] != 64u ? 64 + m->pk[1][0] / 2 : 64 + m->fsweep / 2; q = 36000 - m->pk[1][1] * 260; }
    if (v > 60 && v < 68) { m->fmode = 0; return; }
    uint32_t fc;
    if (v <= 60) { m->fmode = -1; fc = (uint32_t)(((uint64_t)140u * fg_exp2_q16((uint32_t)(v * 7 * 65536 / 60))) >> 16); }
    else { m->fmode = 1; fc = (uint32_t)(((uint64_t)20u * fg_exp2_q16((uint32_t)((v - 68) * 8 * 65536 / 59))) >> 16); }
    m->ff = fg_svf2_f(fc > 15000u ? 15000u : fc);
    m->fq = q < 3000 ? 3000 : q;
}

void fg_mix_render(fg_mix_t *m, int32_t *const in[FG_TRACKS], int32_t *outl, int32_t *outr, unsigned n)
{
    int32_t L[FG_BLOCK], R[FG_BLOCK], ds[FG_BLOCK], rs[FG_BLOCK];
    fg_zero(L, n * 4u); fg_zero(R, n * 4u); fg_zero(ds, n * 4u); fg_zero(rs, n * 4u);
    unsigned any_solo = 0;
    for (int t = 0; t < FG_TRACKS; t++) any_solo |= m->st[t].solo;
    /* el bombo manda: las demás pistas van 3 dB abajo y su golpe las agacha ~4 dB (ataque inmediato, regreso ~120 ms) */
    int32_t duck[FG_BLOCK];
    unsigned pump = !(m->st[0].mute || (any_solo && !m->st[0].solo));
    if (pump) {
        int32_t e = m->pump_env;
        for (unsigned i = 0; i < n; i++) {
            int32_t a = in[0][i] < 0 ? -in[0][i] : in[0][i];
            if (a > e) e = a; else e -= (e >> 12) + 1;
            if (e < 0) e = 0;
            int32_t d = e * 3; if (d > 32767) d = 32767;
            duck[i] = d;
        }
        m->pump_env = e;
    }

    /* pistas: volumen sin saltos, paneo de potencia constante, envíos posfader */
    for (int t = 0; t < FG_TRACKS; t++) {
        fg_strip_t *s = &m->st[t];
        int32_t target = fg_amount(s->v[FGX_VOL], 32767), mtarget = (s->mute || (any_solo && !s->solo)) ? 0 : 32767;
        int32_t g = s->gain, step = (target - g) / (int32_t)n, mg = s->mgain, mstep = (mtarget - mg) / (int32_t)n;
        uint32_t th = (uint32_t)s->v[FGX_PAN] * (0x40000000u / 127u);
        int32_t gl = fg_sin(th + 0x40000000u), gr = fg_sin(th);
        int32_t sd = fg_amount(s->v[FGX_DLY], 32767), sr = fg_amount(s->v[FGX_REV], 32767) + s->xrev, pk = 0;
        if (sr > 32767) sr = 32767;
        unsigned wid = s->v[FGX_WIDTH], wdel = 20u + wid * 7u / 4u;   /* ANCHO: 0.5 .. 5.5 ms al otro lado */
        int32_t wg = (int32_t)wid * 200;
        unsigned filt = s->v[FGX_FILT] != 64u && s->ff, sat = s->v[FGX_SAT];
        int32_t drive = 4096 + (int32_t)sat * 150, dk = (int32_t)s->v[FGX_DUCK] * 195;   /* SIDECHAIN 64 = los ~4 dB de siempre */
        int32_t *x = in[t];
        unsigned mfull = mg == 32767 && mtarget == 32767;   /* sonando: la pista pasa intacta */
        for (unsigned i = 0; i < n; i++) {
            g += step; mg += mstep;
            int32_t y = mfull ? x[i] : (x[i] = fg_mul15(x[i], mg));   /* la pista callada también sale callada por USB */
            if (filt) { fg_svf2(&s->sv, y, s->ff, s->fq); y = s->v[FGX_FILT] < 64u ? s->sv.lo : s->sv.hi; }
            if (sat) y = fg_soft((int32_t)(((int64_t)y * drive) >> 12));
            y = fg_mul15(y, g);
            if (t > 0) y = fg_mul15(pump && dk ? y - fg_mul15(y, fg_mul15(duck[i], dk)) : y, 23170);
            if (wid) {                                          /* el lado que se retrasa se abre, el otro se queda */
                s->wd[s->ww & (FGX_WD - 1u)] = (int16_t)fg_clamp16(y);
                int32_t dly = s->wd[(uint8_t)(s->ww - wdel)];
                s->ww++;
                L[i] += fg_mul15(y, gl); R[i] += fg_mul15(y - fg_mul15(y, wg) + fg_mul15(dly, wg), gr);
            } else { L[i] += fg_mul15(y, gl); R[i] += fg_mul15(y, gr); }
            if (sd) ds[i] += fg_mul15(y, sd);
            if (sr) rs[i] += fg_mul15(y, sr);
            int32_t a = y < 0 ? -y : y; if (a > pk) pk = a;
        }
        s->gain = target; s->mgain = mtarget; s->peak = pk;
    }

    /* delay ping-pong sincronizado al tempo */
    int32_t fb = (int32_t)m->fx[FGF_DFB] * 245, tone = 30000 - (int32_t)m->fx[FGF_DTONE] * 220;
    if (m->perf & FGP_THROW) {                                  /* K1 cuánto se repite, K2 su tono */
        int32_t f2 = 18000 + m->pk[4][0] * 115;
        if (fb < f2) fb = f2;
        if (m->pk[4][1] != 64u) tone = 30000 - (int32_t)m->pk[4][1] * 220;
    }
    /* reverb: 4 peines + 2 pasatodo por lado */
    int32_t rfb = 22937 + (int32_t)m->fx[FGF_RSIZE] * 72, damp = 26000 - (int32_t)m->fx[FGF_RTONE] * 180;
    unsigned freeze = (m->perf & FGP_FREEZE) != 0;
    if (freeze) { rfb = 32767 - (127 - (int32_t)m->pk[5][0]) / 4; damp = m->pk[5][1] < 64u ? (64 - (int32_t)m->pk[5][1]) * 300 : 0; }   /* K1 tamaño, K2 tono */
    if (FG_REV_HALF) damp = fg_mul15(damp, damp);               /* el mismo corte con la mitad de pasos */
    int32_t dla = 32767 - tone;                                 /* tono del delay: a media tasa, 1 - (1 - a)^2 */
    if (FG_DLY_HALF) dla = 2 * dla - fg_mul15(dla, dla);
    (void)dla;
    dj_coef(m);
    for (unsigned i = 0; i < n; i++) {
        int32_t din = ds[i];
        if (m->perf & FGP_THROW) din += (L[i] + R[i]) >> 1;
#if FG_DLY_HALF
        /* un paso cada dos muestras con el promedio de las dos, la salida interpola (como la reverb) */
        int32_t yl, yr;
        m->dacc += din;
        if (m->dph) {
            uint32_t rp = (m->dpos - (m->dlen >> 1)) & (FG_DLEN - 1u);
            int32_t sl = m->dl[rp], sr = m->dr[rp];
            m->dlpl += fg_mul15(sl - m->dlpl, dla); m->dlpr += fg_mul15(sr - m->dlpr, dla);
            m->dl[m->dpos] = (int16_t)fg_clamp16((m->dacc >> 1) + fg_mul15(m->dlpr, fb));
            m->dr[m->dpos] = (int16_t)fg_clamp16(fg_mul15(m->dlpl, fb));
            m->dpos = (m->dpos + 1u) & (FG_DLEN - 1u);
            m->dacc = 0;
            m->dpl = m->dcl; m->dpr = m->dcr; m->dcl = sl; m->dcr = sr;
            yl = m->dpl; yr = m->dpr;
        } else {
            yl = (m->dpl + m->dcl) >> 1; yr = (m->dpr + m->dcr) >> 1;
        }
        m->dph ^= 1u;
#else
        uint32_t rp = (m->dpos - m->dlen) & (FG_DLEN - 1u);
        int32_t yl = m->dl[rp], yr = m->dr[rp];
        m->dlpl += fg_mul15(yl - m->dlpl, 32767 - tone); m->dlpr += fg_mul15(yr - m->dlpr, 32767 - tone);
        m->dl[m->dpos] = (int16_t)fg_clamp16(din + fg_mul15(m->dlpr, fb));
        m->dr[m->dpos] = (int16_t)fg_clamp16(fg_mul15(m->dlpl, fb));
        m->dpos = (m->dpos + 1u) & (FG_DLEN - 1u);
#endif
        int32_t rin = freeze ? 0 : rs[i] >> 3, wl = 0, wr = 0;
#if FG_REV_HALF
        /* un paso de reverb cada dos muestras con el promedio de las dos; la salida interpola entre los dos
         * últimos pasos (1.5 muestras de retraso) */
        m->racc += rin;
        if (m->rph) {
            int32_t x = m->racc >> 1;
            m->racc = 0;
            for (int c = 0; c < 4; c++) { wl += comb(&m->cl[c], x, rfb, damp); wr += comb(&m->cr[c], x, rfb, damp); }
            for (int a = 0; a < 2; a++) { wl = allpass(&m->al[a], wl); wr = allpass(&m->ar[a], wr); }
            m->rpl = m->rcl; m->rpr = m->rcr; m->rcl = wl; m->rcr = wr;
            wl = m->rpl; wr = m->rpr;
        } else {
            wl = (m->rpl + m->rcl) >> 1; wr = (m->rpr + m->rcr) >> 1;
        }
        m->rph ^= 1u;
#else
        for (int c = 0; c < 4; c++) { wl += comb(&m->cl[c], rin, rfb, damp); wr += comb(&m->cr[c], rin, rfb, damp); }
        for (int a = 0; a < 2; a++) { wl = allpass(&m->al[a], wl); wr = allpass(&m->ar[a], wr); }
#endif
        L[i] += yl + (wl >> 1); R[i] += yr + (wr >> 1);
    }

    /* subida: ruido por una banda que sube de 300 Hz a 9 kHz en 3 s */
    if (m->perf & FGP_RISER) {
        for (unsigned i = 0; i < n; i++) {
            if ((i & 15u) == 0) {
                uint32_t t = m->riser_t < 132354u ? m->riser_t : 132354u;
                uint32_t fc = (uint32_t)(((uint64_t)300u * fg_exp2_q16((uint32_t)(((uint64_t)t * 5u * 65536u) / 132354u))) >> 16);
                m->riser_amp = (int32_t)(t / 6u);
                m->riser_f = fg_svf2_f(fc);
                m->riser_t += 4u + m->pk[6][0] / 5u;                 /* K1: más o menos rápida */
            }
            fg_svf2(&m->rs, fg_noise(&m->seed), m->riser_f, 12000 - (int32_t)m->pk[6][1] * 80);
            int32_t r = fg_mul15(m->rs.bp, m->riser_amp > 22000 ? 22000 : m->riser_amp);
            L[i] += r; R[i] += r;
        }
    }

    /* bus maestro: filtro DJ, drive, bits, gate, compresor, limitador */
    int32_t drive = 4096 + (int32_t)m->fx[FGF_DRIVE] * 160;
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) {
        int32_t l = L[i], r = R[i];
        if (m->fmode) {
            fg_svf2(&m->fl, l, m->ff, m->fq); fg_svf2(&m->fr, r, m->ff, m->fq);
            l = m->fmode < 0 ? m->fl.lo : m->fl.hi; r = m->fmode < 0 ? m->fr.lo : m->fr.hi;
        }
        if (m->fx[FGF_DRIVE]) {
            l = fg_soft((int32_t)(((int64_t)l * drive) >> 12)); r = fg_soft((int32_t)(((int64_t)r * drive) >> 12));
        }
        if (m->perf & FGP_BITS) {
            unsigned rate = 1u + m->pk[2][1] * 10u / 127u, bits = 6u + m->pk[2][0] * 8u / 127u;   /* K1 cuántos bits se van, K2 tasa */
            int32_t keep = ~(int32_t)((1u << bits) - 1u);
            if (m->crush_cnt++ % rate == 0) { m->crush_l = l & keep; m->crush_r = r & keep; }
            l = m->crush_l; r = m->crush_r;
        }
        if (m->perf & FGP_GATE) {
            if (++m->gate_pos >= m->gate_len) m->gate_pos = 0;
            unsigned w = m->pk[3][1] < 16u ? 16u : m->pk[3][1] > 112u ? 112u : m->pk[3][1];   /* K2: cuánto suena (64 = la mitad) */
            if (m->gate_pos >= (uint32_t)(((uint64_t)m->gate_len * w) >> 7)) { l = 0; r = 0; }
        }
        int32_t a = l < 0 ? -l : l, b = r < 0 ? -r : r;
        if (b > a) a = b;
        if (a > pk) pk = a;
        L[i] = l; R[i] = r;
    }
    /* compresor por bloque: ataque inmediato, regreso ~150 ms */
    if (pk > m->comp_env) m->comp_env = pk; else m->comp_env -= (m->comp_env >> 4);
    int32_t thr = (int32_t)(((int64_t)32767 << 16) / (int64_t)fg_exp2_q16(m->fx[FGF_COMP] * 4u * 65536u / 127u));   /* 0 .. -24 dB */
    int32_t want = m->comp_env > thr ? (int32_t)(((int64_t)thr << 12) / m->comp_env) : 4096;
    int32_t makeup = (int32_t)((fg_exp2_q16(m->fx[FGF_COMP] * 2u * 65536u / 127u)) >> 4);   /* hasta +12 dB, Q12 */
    int32_t g0 = m->comp_gain, gstep = (want - g0) / (int32_t)n;
    for (unsigned i = 0; i < n; i++) {
        g0 += gstep;
        int32_t gm = (int32_t)(((int64_t)g0 * makeup) >> 12);
        int32_t l = (int32_t)(((int64_t)L[i] * gm) >> 12), r = (int32_t)(((int64_t)R[i] * gm) >> 12);
        outl[i] += fg_soft(l * 2 / 3); outr[i] += fg_soft(r * 2 / 3);
    }
    m->comp_gain = want;
}

/* Cinta: lo que suena se graba siempre; los FX de cinta lo leen en loop (stutter), hacia atrás (reversa),
 * a media velocidad (medio tiempo) o frenando hasta parar. Se combinan: stutter + reversa = loop al revés. */
void fg_mix_tape(fg_mix_t *m, int32_t *outl, int32_t *outr, unsigned n)
{
    uint32_t tape = m->perf & FGP_TAPE;
    if (tape && !m->tape_prev) {
        m->anchor = m->capw; m->rpos = 0; m->speed = 65536;
        if (tape & (FGP_HALF | FGP_BRAKE)) m->rpos = (uint64_t)(-((int64_t)(m->step * 4u) << 32));   /* arranca un tiempo atrás */
    }
    if ((tape & (FGP_STUT16 | FGP_STUT32)) != (m->tape_prev & (FGP_STUT16 | FGP_STUT32))) {
        m->loop_len = (tape & FGP_STUT32) ? m->step / 2u : m->step;
        { unsigned k = (tape & FGP_STUT32) ? m->pk[8][0] : m->pk[7][0];    /* K1: de un cuarto a dos veces el pedazo */
          m->loop_len = k < 32u ? m->loop_len / 4u : k < 56u ? m->loop_len / 2u : k < 80u ? m->loop_len : m->loop_len * 2u; }
        m->anchor = m->capw; m->rpos = 0;
    }
    m->tape_prev = tape;
    for (unsigned i = 0; i < n; i++) {
        if (m->capw & 1u) {                                     /* cada par: su promedio, a 22 kHz (la mitad de memoria) */
            uint32_t k = (m->capw >> 1) & (FG_CAP / 2u - 1u);
            m->capl[k] = (int16_t)fg_clamp16((m->cpl + outl[i]) >> 1);
            m->capr[k] = (int16_t)fg_clamp16((m->cpr + outr[i]) >> 1);
        } else { m->cpl = outl[i]; m->cpr = outr[i]; }
        m->capw++;
        int32_t target = tape ? 32767 : 0;
        m->xf += (target - m->xf) >> 6;                        /* ~3 ms de cruce: sin clics */
        if (m->xf < 8 && !tape) continue;
        /* velocidad: medio tiempo a la mitad; el freno cae de 1 a 0 en ~1 s */
        int32_t sp = 65536;
        if (tape & FGP_HALF) sp = 16384 + (int32_t)m->pk[10][0] * 258;     /* K1: de un cuarto a tres cuartos */
        if (tape & FGP_REV) sp = 32768 + (int32_t)m->pk[9][0] * 516;       /* K1: reversa a media .. doble velocidad */
        if (tape & FGP_BRAKE) { if (m->speed > 0) m->speed -= 1 + (127 - (int32_t)m->pk[11][0]) / 16; if (m->speed < 0) m->speed = 0; sp = (int32_t)(((int64_t)sp * m->speed) >> 16); }
        else m->speed = 65536;
        int64_t step = (int64_t)sp << 16;                       /* Q32.32 */
        int64_t rp = (int64_t)m->rpos;
        if (tape & FGP_REV) rp -= step; else rp += step;
        uint32_t back;
        if (tape & (FGP_STUT16 | FGP_STUT32)) {                 /* loop del último pedazo */
            int64_t L = (int64_t)m->loop_len << 32;
            while (rp >= 0) rp -= L;
            while (rp < -L) rp += L;
        } else {
            int64_t lim = -((int64_t)(FG_CAP - 256u) << 32);
            if (rp > -(int64_t)(1LL << 32) && !(tape & FGP_REV) && sp >= 65536) rp = -(int64_t)(1LL << 32);
            if (rp < lim) rp = lim;
        }
        back = (uint32_t)(-(rp >> 32));
        if (m->capw - (m->anchor - back) > FG_CAP - 256u) {    /* la lectura quedó más atrás de lo que guarda la cinta */
            m->anchor += FG_CAP / 2u;                           /* (medio tiempo, freno): salta media cinta adelante */
        }
        m->rpos = (uint64_t)rp;
        uint32_t fr = (uint32_t)((uint64_t)rp >> 16) & 0xFFFFu;
        uint32_t pos = m->anchor - back - 2u;                   /* (dos atrás: el par de la cabeza puede no estar completo) */
        fr = ((pos & 1u) << 15) + (fr >> 1);
        uint32_t i0 = (pos >> 1) & (FG_CAP / 2u - 1u), i1 = (i0 + 1u) & (FG_CAP / 2u - 1u);
        int32_t l = m->capl[i0] + (int32_t)(((int64_t)(m->capl[i1] - m->capl[i0]) * (int32_t)fr) >> 16);
        int32_t r = m->capr[i0] + (int32_t)(((int64_t)(m->capr[i1] - m->capr[i0]) * (int32_t)fr) >> 16);
        if (tape & FGP_BRAKE) { l = fg_mul15(l, m->speed >> 1); r = fg_mul15(r, m->speed >> 1); }   /* la cinta se apaga al detenerse */
        outl[i] += fg_mul15(l - outl[i], m->xf);
        outr[i] += fg_mul15(r - outr[i], m->xf);
    }
}
