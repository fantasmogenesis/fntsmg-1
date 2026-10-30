#include "fg_samples.h"

static const uint16_t STEP[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
    1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* el estado justo antes de la muestra pos: la cabecera de su bloque y lo que hay antes dentro de él */
static int32_t dec1(fg_smpv_t *v, uint32_t n)
{
    const uint8_t *blk = v->s->data + (n >> 6) * 36u;
    if ((n & 63u) == 0) { v->pred = (int16_t)(blk[0] | blk[1] << 8); v->idx = blk[2]; }
    unsigned code = (blk[4 + ((n & 63u) >> 1)] >> ((n & 1u) * 4u)) & 15u, st = STEP[v->idx];
    int32_t dq = (int32_t)(st >> 3);
    if (code & 4u) dq += (int32_t)st;
    if (code & 2u) dq += (int32_t)(st >> 1);
    if (code & 1u) dq += (int32_t)(st >> 2);
    v->pred += code & 8u ? -dq : dq;
    if (v->pred > 32767) v->pred = 32767; else if (v->pred < -32768) v->pred = -32768;
    int ix = (int)v->idx + IDX[code & 7u];
    v->idx = (uint8_t)(ix < 0 ? 0 : ix > 88 ? 88 : ix);
    return v->pred;
}
static uint32_t wrap(const fg_smpv_t *v, uint32_t n) { return v->s->loop && n >= v->s->loop_e ? v->s->loop_s + (n - v->s->loop_e) % (v->s->loop_e - v->s->loop_s) : n; }
static void seek(fg_smpv_t *v, uint32_t n)                  /* decodifica desde el principio del bloque hasta n */
{
    uint32_t b = n & ~63u;
    for (uint32_t k = b; k <= n; k++) v->a = dec1(v, k);
    v->pos = n;
}
void fg_smpv_start(fg_smpv_t *v, const fg_sample_t *s, uint32_t pos)
{
    v->s = s; v->frac = 0; v->done = !s || !s->len;
    if (v->done) { v->a = v->b = 0; return; }
    if (pos >= s->len) pos = s->loop ? s->loop_s : s->len - 1u;
    seek(v, pos);
    uint32_t nx = wrap(v, pos + 1u);
    if (nx >= s->len) v->b = 0;
    else if (nx == pos + 1u) v->b = dec1(v, nx);            /* (el estado queda después de b, como en next) */
    else { int32_t ka = v->a; seek(v, nx); v->b = v->a; seek(v, pos); v->a = ka; }   /* b es el principio del lazo */
}
int32_t fg_smpv_next(fg_smpv_t *v, uint32_t step)
{
    if (v->done) return 0;
    int32_t y = v->a + (int32_t)(((int64_t)(v->b - v->a) * (int32_t)(v->frac >> 1)) >> 15);
    v->frac += step;
    while (v->frac >= 65536u) {                             /* avanza: decodifica en orden (o salta al lazo) */
        v->frac -= 65536u;
        uint32_t n = v->pos + 1u;
        if (n >= v->s->len && !v->s->loop) { v->done = 1; return y; }
        v->a = v->b;
        uint32_t w = wrap(v, n), nx = wrap(v, w + 1u);
        if (w != n) { seek(v, w); }
        v->pos = w;
        if (nx >= v->s->len && !v->s->loop) v->b = 0;
        else if (nx == w + 1u) v->b = dec1(v, nx);
        else { uint32_t keep = v->pos; int32_t ka = v->a; seek(v, nx); v->b = v->a; seek(v, keep); v->a = ka; }
    }
    return y;
}
/* un bloque entero de 64 muestras (la reversa lo lee al revés) */
void fg_smp_block(const fg_sample_t *s, uint32_t blk, int16_t *out)
{
    fg_smpv_t v;
    v.s = s;
    for (uint32_t k = 0; k < 64u; k++) out[k] = (int16_t)dec1(&v, blk * 64u + k);
}
