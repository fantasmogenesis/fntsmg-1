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

/* el bloque blk en buf y, en after, la primera muestra del siguiente: se decodifica de a 64 (leer la flash muestra por
 * muestra, con el código corriendo de la misma flash, llenaba la caché del núcleo 0: con la percusión en SAMPLE se
 * perdían bloques de audio) */
static void load(fg_sampv_t *v, int32_t blk)
{
    uint32_t nb = (uint32_t)blk + 1u;
    fg_smp_block(v->s, (uint32_t)blk, v->buf);
    v->after = nb * 64u < v->s->len ? fg_smp_first(v->s, nb) : 0;
    v->blk = blk;
}

void fg_sampv_trig(fg_sampv_t *v, const uint8_t *p, unsigned vel, int synth, int32_t note_q8)
{
    const fg_sample_t *s = fg_samp_at(p[1]);
    v->on = 0;
    if (!s || !s->len) return;
    v->s = s;
    v->mode = (uint8_t)(p[6] * 3u / 128u);
    int32_t d = synth ? note_q8 - (int32_t)s->root * 256 : ((int32_t)(p[0] < 40u ? 40u : p[0] > 88u ? 88u : p[0]) - 64) * 256;
    if (d > 36 * 256) d = 36 * 256; else if (d < -48 * 256) d = -48 * 256;
    uint32_t r = d >= 0 ? fg_exp2_q16((uint32_t)(d * 65536 / (12 * 256))) : (uint32_t)(((uint64_t)65536u << 16) / fg_exp2_q16((uint32_t)(-d * 65536 / (12 * 256))));
    uint64_t st = ((uint64_t)r * s->rate) / 44100u;
    v->step = (uint32_t)(st > (16u << 16) ? 16u << 16 : st);   /* (más rápido, el decodificador no alcanza) */
    uint32_t start = (uint32_t)((uint64_t)s->len * p[2] / 128u);
    v->lp = 0;
    if (v->mode == FGSV_LAZO) {
        v->lp = 1; v->lps = start; v->lpe = s->len;
        if (s->loop && start < s->loop_e) { v->lps = s->loop_s; v->lpe = s->loop_e; }
        if (v->lpe < v->lps + 2u) v->lp = 0;
        else { load(v, (int32_t)(v->lps >> 6)); v->loopv = v->buf[v->lps & 63u]; }
    }
    v->pos = (int32_t)(v->mode == FGSV_REVERSA ? s->len - 1u - start : start); v->frac = 0;
    load(v, v->pos >> 6);
    v->amp = (int32_t)((int64_t)FG_ONE30 * vel / 127);
    v->kd = p[3] >= 120u ? FG_ONE30 : fg_decay(p[3], 30, 110);
    v->kill = 0; v->rel = 0; v->seg = 0;
    v->att = start || v->mode == FGSV_REVERSA ? 0 : 32767;   /* ~1.5 ms de entrada si no empieza en 0 */
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

/* la siguiente muestra de la cabeza (interpolada) y avanza; 0 al terminar */
static int32_t head_next(fg_sampv_t *v)
{
    int32_t j = v->pos & 63, a = v->buf[j], b, f;
    if (v->mode == FGSV_REVERSA) {
        b = j == 63 ? v->after : v->buf[j + 1];
        int32_t x = a + (((b - a) * (int32_t)(v->frac >> 2)) >> 14);
        f = (int32_t)v->frac - (int32_t)v->step;
        while (f < 0) {
            f += 65536;
            if (--v->pos < 0) { v->on = 0; break; }
            if ((v->pos & 63) == 63) { v->after = v->buf[0]; fg_smp_block(v->s, (uint32_t)v->pos >> 6, v->buf); v->blk = v->pos >> 6; }
        }
        v->frac = (uint32_t)f;
        return x;
    }
    b = v->lp && (uint32_t)v->pos + 1u >= v->lpe ? v->loopv : j == 63 ? v->after : v->buf[j + 1];
    int32_t x = a + (((b - a) * (int32_t)(v->frac >> 2)) >> 14);
    v->frac += v->step;
    while (v->frac >= 65536u) {
        v->frac -= 65536u;
        v->pos++;
        if (v->lp && (uint32_t)v->pos >= v->lpe) { v->pos = (int32_t)v->lps; if ((v->pos >> 6) != v->blk) load(v, v->pos >> 6); }
        else if ((uint32_t)v->pos >= v->s->len) { v->on = 0; break; }
        else if ((v->pos & 63) == 0) load(v, v->pos >> 6);
    }
    return x;
}

void fg_sampv_render(fg_sampv_t *v, int32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n && v->on; i++) {
        if (!v->seg) {                                    /* la envolvente, de 16 en 16 muestras y en línea recta */
            int32_t k = v->kd != FG_ONE30 ? fg_pow16(v->kd) : FG_ONE30, a0 = v->amp, a1;
            if (v->rel) k = fg_mul30(k, fg_pow16(FG_DECAY[66]));   /* LAZO soltado: ~16 ms */
            if (v->kill) k = fg_mul30(k, fg_pow16(v->kill));
            a1 = k == FG_ONE30 ? a0 : fg_mul30(a0, k);
            v->amp = a1;
            int32_t t0 = v->att, t1 = t0 + 16 * 512 > 32767 ? 32767 : t0 + 16 * 512;
            v->att = t1;
            int32_t m0 = (((a0 >> 15) * t0) >> 15) * v->gain >> 14, m1 = (((a1 >> 15) * t1) >> 15) * v->gain >> 14;
            v->m = m0; v->dm = (m1 - m0) / 16; v->seg = 16;
            if (a1 < 3000) { v->on = 0; break; }
        }
        int32_t x = head_next(v);
        if (v->g < 32000) { v->y0 += (v->g * ((x - v->y0) >> 1)) >> 14; v->y1 += (v->g * ((v->y0 - v->y1) >> 1)) >> 14; x = v->hp ? x - v->y1 : v->y1; }
        if (v->crush_n) { if (v->crush_k++ == 0) v->crush_y = x & v->crush_m; if (v->crush_k >= v->crush_n) v->crush_k = 0; x = v->crush_y; }
        int32_t z = (x * v->m) >> 13;                    /* la envolvente y el NIVEL; arriba de ~6000 satura suave */
        out[i] += z > 6000 || z < -6000 ? fg_soft(z) : z;
        v->m += v->dm; v->seg--;
    }
}
