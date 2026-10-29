/* Puente de prueba: fg_seq compilado a WASM para compararlo contra caja/worklet.js (test/seq_compare.mjs).
 * Cada evento sale aplanado en 20 enteros: at kind t s a b v flags vel mask len lk0..lk7 _ */
#include <emscripten.h>
#include "fg_seq.h"

#define EVMAX 1024
static fg_seq_t Q;
static fg_seq_ev_t EV[EVMAX];
static int32_t REC[EVMAX * 20];
static uint8_t BUF[64];

EMSCRIPTEN_KEEPALIVE void t_init(int sr, int seed) { fg_seq_init(&Q, (uint32_t)sr, (uint32_t)seed); }
EMSCRIPTEN_KEEPALIVE uint8_t *t_buf(void) { return BUF; }
EMSCRIPTEN_KEEPALIVE int32_t *t_rec(void) { return REC; }
EMSCRIPTEN_KEEPALIVE int t_clock(void) { return (int)Q.clock; }
EMSCRIPTEN_KEEPALIVE void t_track(int p, int t, int len, int spd) { fg_seq_clear_track(&Q.pat[p].tr[t]); Q.pat[p].tr[t].len = (uint8_t)len; Q.pat[p].tr[t].spd = (uint8_t)spd; }
EMSCRIPTEN_KEEPALIVE void t_step(int p, int t, int s, int v, int c, int pr, int r, int u)
{
    fg_step_t *st = &Q.pat[p].tr[t].st[s];
    st->v = (uint8_t)v; st->c = (uint8_t)c; st->p = (uint8_t)pr; st->r = (uint8_t)r; st->u = (int8_t)u;
}
EMSCRIPTEN_KEEPALIVE void t_lock(int p, int t, int s, int i, int v) { Q.pat[p].tr[t].st[s].lk[i] = (uint8_t)v; }
EMSCRIPTEN_KEEPALIVE void t_auto(int p, int t, int s, int i, int v) { fg_step_t *st = &Q.pat[p].tr[t].st[s]; st->au[0] = (uint8_t)i; st->au[1] = (uint8_t)v; }
EMSCRIPTEN_KEEPALIVE void t_cur(int p) { Q.cur = (uint8_t)p; }
EMSCRIPTEN_KEEPALIVE void t_ahead(int t, int a) { Q.ahead[t] = (uint8_t)a; }

EMSCRIPTEN_KEEPALIVE void t_play(int on) { fg_seq_play(&Q, on); }
EMSCRIPTEN_KEEPALIVE void t_bpm(int v) { fg_seq_tempo(&Q, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void t_swing(int v) { fg_seq_swing(&Q, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void t_fill(int on) { fg_seq_fill(&Q, on); }
EMSCRIPTEN_KEEPALIVE void t_transp(int v) { Q.transp = (int8_t)v; }
EMSCRIPTEN_KEEPALIVE void t_azar(int t, int v) { Q.azar[t] = (uint8_t)v; }
EMSCRIPTEN_KEEPALIVE void t_muteq(int on) { Q.muteq = (uint8_t)on; }
EMSCRIPTEN_KEEPALIVE void t_mutes(int m, int s, int now) { fg_seq_mutes(&Q, (unsigned)m, (unsigned)s, now); }
EMSCRIPTEN_KEEPALIVE void t_perf(int f) { fg_seq_perf(&Q, (unsigned)f); }
EMSCRIPTEN_KEEPALIVE void t_queue(int p) { Q.nchain = 0; fg_seq_queue(&Q, (unsigned)p); }
EMSCRIPTEN_KEEPALIVE void t_chain(int n) { fg_seq_chain(&Q, BUF, (unsigned)n); }
EMSCRIPTEN_KEEPALIVE void t_param(int t, int m, int i, int v) { fg_seq_param(&Q, (unsigned)t, (unsigned)m, (unsigned)i, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void t_model(int t, int m) { fg_seq_model(&Q, (unsigned)t, (unsigned)m); }
EMSCRIPTEN_KEEPALIVE void t_mix(int t, int i, int v) { fg_seq_mix(&Q, (unsigned)t, (unsigned)i, (unsigned)v); }
EMSCRIPTEN_KEEPALIVE void t_lfo(int t) { fg_seq_lfo(&Q, (unsigned)t, BUF); }
EMSCRIPTEN_KEEPALIVE void t_audition(int t, int vel) { fg_seq_audition(&Q, (unsigned)t, (unsigned)vel, BUF, 255u, 0); }

EMSCRIPTEN_KEEPALIVE int t_process(int n)
{
    int c = fg_seq_process(&Q, (unsigned)n, EV, EVMAX);
    for (int k = 0; k < c; k++) {
        const fg_seq_ev_t *e = &EV[k];
        int32_t *r = &REC[k * 20];
        r[0] = e->at; r[1] = e->kind; r[2] = e->t; r[3] = e->s; r[4] = e->a; r[5] = e->b; r[6] = e->v;
        r[7] = e->flags; r[8] = e->vel; r[9] = e->mask; r[10] = e->len;
        for (int i = 0; i < 8; i++) r[11 + i] = e->lk[i];
    }
    return c;
}
