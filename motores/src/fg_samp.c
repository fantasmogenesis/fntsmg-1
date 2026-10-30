#include "fg_samp.h"
#include "fg_dsp.h"
#include "fg_tables.h"

fg_sample_t fg_usr[FGS_USR] FG_POOL;                  /* (vale lo que diga fg_usr_mask, que sí arranca en 0) */
uint32_t fg_usr_mask;

static const fg_sample_t *slot(unsigned v)
{
    if (v < FGSB_CAJAS) return v < FG_SMP_KICK_N ? &FG_SMP_KICK[v] : 0;
    if (v < FGSB_TEXTURAS) return v - FGSB_CAJAS < FG_SMP_SNR_N ? &FG_SMP_SNR[v - FGSB_CAJAS] : 0;
    if (v < FGSB_TUYOS) return v - FGSB_TEXTURAS < FG_SMP_TEX_N ? &FG_SMP_TEX[v - FGSB_TEXTURAS] : 0;
    if (v < FGSB_END) return (fg_usr_mask >> (v - FGSB_TUYOS)) & 1u ? &fg_usr[v - FGSB_TUYOS] : 0;
    return 0;
}
int fg_samp_valid(unsigned v) { const fg_sample_t *s = slot(v); return s && s->len; }
const fg_sample_t *fg_samp_at(unsigned v)
{
    if (v >= FGSB_END) v = FGSB_END - 1u;
    for (int k = (int)v; k >= 0; k--) if (fg_samp_valid((unsigned)k)) return slot((unsigned)k);
    for (unsigned k = v + 1u; k < FGSB_END; k++) if (fg_samp_valid(k)) return slot(k);
    return 0;
}
unsigned fg_samp_bank(unsigned v) { return v < FGSB_CAJAS ? 0u : v < FGSB_TEXTURAS ? 1u : v < FGSB_TUYOS ? 2u : 3u; }
unsigned fg_samp_step(unsigned v, int dir)
{
    for (int k = (int)v + dir; k >= 0 && k < FGSB_END; k += dir) if (fg_samp_valid((unsigned)k)) return (unsigned)k;
    return v;
}

/* la reversa: el bloque blk en buf y, en after, la muestra que le sigue */
static void rv_load(fg_sampv_t *v, int32_t blk)
{
    if ((uint32_t)(blk + 1) * 64u < v->s->len) { fg_smp_block(v->s, (uint32_t)blk + 1u, v->buf); v->after = v->buf[0]; }
    else v->after = 0;
    fg_smp_block(v->s, (uint32_t)blk, v->buf);
    v->blk = blk;
}

void fg_sampv_trig(fg_sampv_t *v, const uint8_t *p, unsigned vel, int synth, int32_t note_q8)
{
    const fg_sample_t *s = fg_samp_at(p[1]);
    v->on = 0;
    if (!s || !s->len) return;
    v->s = s; v->ls = *s;
    v->mode = (uint8_t)(p[6] * 3u / 128u);
    int32_t d = synth ? note_q8 - (int32_t)s->root * 256 : ((int32_t)(p[0] < 40u ? 40u : p[0] > 88u ? 88u : p[0]) - 64) * 256;
    if (d > 36 * 256) d = 36 * 256; else if (d < -48 * 256) d = -48 * 256;
    uint32_t r = d >= 0 ? fg_exp2_q16((uint32_t)(d * 65536 / (12 * 256))) : (uint32_t)(((uint64_t)65536u << 16) / fg_exp2_q16((uint32_t)(-d * 65536 / (12 * 256))));
    uint64_t st = ((uint64_t)r * s->rate) / 44100u;
    v->step = (uint32_t)(st > (16u << 16) ? 16u << 16 : st);   /* (más rápido, el decodificador no alcanza) */
    uint32_t start = (uint32_t)((uint64_t)s->len * p[2] / 128u);
    if (v->mode == FGSV_REVERSA) {
        v->rpos = (int32_t)(s->len - 1u - start); v->rfrac = 0;
        rv_load(v, v->rpos >> 6);
    } else {
        if (v->mode == FGSV_GOLPE) v->ls.loop = 0;
        else if (!s->loop || start >= s->loop_e) { v->ls.loop = 1; v->ls.loop_s = start; v->ls.loop_e = s->len; }
        if (v->ls.loop && v->ls.loop_e < v->ls.loop_s + 2u) v->ls.loop = 0;
        fg_smpv_start(&v->r, &v->ls, start);
    }
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->kd = p[3] >= 120u ? FG_ONE30 : fg_decay(p[3], 30, 110);
    v->kill = 0; v->rel = 0;
    v->att = start || v->mode == FGSV_REVERSA ? 0 : 32767; v->att_inc = 512;   /* ~1.5 ms de entrada si no empieza en 0 */
    unsigned c = p[4];
    v->hp = c > 65u;
    v->g = c >= 63u && c <= 65u ? 32767 : FG_CUT_G[c < 63u ? 300u + c * 700u / 64u : 100u + (c - 66u) * 700u / 61u];
    v->y0 = v->y1 = 0;
    unsigned k = p[5];
    v->crush_n = (uint8_t)(k ? 1u + k * k / 1100u : 0u); v->crush_k = 0;
    v->crush_m = (int32_t)(~0u << (k * 11u / 127u));
    v->crush_y = 0;
    v->gain = (int32_t)(p[7] * p[7]);
    v->on = 1;
}
void fg_sampv_release(fg_sampv_t *v) { if (v->mode == FGSV_LAZO) v->rel = 1; }

void fg_sampv_render(fg_sampv_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        int32_t x;
        if (v->mode == FGSV_REVERSA) {
            int32_t j = v->rpos & 63, a = v->buf[j], b = j == 63 ? v->after : v->buf[j + 1];
            x = a + (int32_t)(((int64_t)(b - a) * (int32_t)v->rfrac) >> 16);
            int32_t f = (int32_t)v->rfrac - (int32_t)v->step;
            while (f < 0) {
                f += 65536;
                if (--v->rpos < 0) { v->on = 0; break; }
                if ((v->rpos & 63) == 63) { v->after = v->buf[0]; fg_smp_block(v->s, (uint32_t)v->rpos >> 6, v->buf); v->blk = v->rpos >> 6; }
            }
            v->rfrac = (uint32_t)f;
        } else {
            x = fg_smpv_next(&v->r, v->step);
            if (v->r.done) v->on = 0;
        }
        if (v->g < 32000) { v->y0 += fg_mul15(v->g, x - v->y0); v->y1 += fg_mul15(v->g, v->y0 - v->y1); x = v->hp ? x - v->y1 : v->y1; }
        if (v->crush_n) { if (v->crush_k++ == 0) v->crush_y = x & v->crush_m; if (v->crush_k >= v->crush_n) v->crush_k = 0; x = v->crush_y; }
        if (v->att < 32767) { v->att += v->att_inc; if (v->att > 32767) v->att = 32767; }
        if (v->kd != FG_ONE30) v->amp = fg_mul30(v->amp, v->kd);
        if (v->rel) v->amp = fg_mul30(v->amp, FG_DECAY[66]);         /* LAZO soltado: ~16 ms */
        if (v->kill) v->amp = fg_mul30(v->amp, v->kill);
        x = fg_mul15(fg_mul15(x, (int32_t)(v->amp >> 15)), v->att);   /* la envolvente antes del NIVEL: la saturación */
        out[i] += fg_soft((int32_t)(((int64_t)x * v->gain) >> 12));   /* levanta la cola, como la MUESTRA del bombo */
        if (v->amp < 3000) v->on = 0;
    }
}
