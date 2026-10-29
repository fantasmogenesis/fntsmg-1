/* Visuales del FM-1 (ver fg_vis.h). Cada bloque lleva el nombre de su par en caja/index.html.
 * Convenciones: coordenadas de diseño en Q4 (1/16 de pixel de 240), parámetros y fracciones en Q8 (0..256),
 * tiempos en segundos Q16, ángulos en vueltas de 32 bits (fg_sin), intensidad 0..255 (guardada en 4 bits: dos pixeles por
 * byte; la pantalla sale a 1 bit con un Bayer de 16 niveles, así que no se pierde nada que se vea). */
#include "fg_vis.h"
#include "fg_dsp.h"
#include "fg_mix.h"
#include "fg_synth.h"

#define TURN(n) (0xFFFFFFFFu / (uint32_t)(n))
#define RAD 683565276u                                    /* 1 radián en vueltas de 32 bits */

static const uint8_t BAYER[16] = {7, 135, 39, 167, 199, 71, 231, 103, 55, 183, 23, 151, 247, 119, 215, 87};   /* (2b+1)·255/32 */
static const uint8_t DIG[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
                                   {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};
static const uint16_t POW06[17] = {0, 12417, 18820, 24003, 28526, 32612, 36382, 39908, 43237, 46403, 49431, 52340, 55145, 57859, 60489, 63046, 65535};
static const uint16_t RATIO[9] = {128, 256, 384, 512, 768, 1024, 1280, 1792, 2304};   /* FM: .5 1 1.5 2 3 4 5 7 9 */

static inline int32_t mul8(int32_t a, int32_t b) { return (a * b) >> 8; }
static inline int32_t SIN(uint32_t ph) { return fg_sin(ph); }
static inline int32_t COS(uint32_t ph) { return fg_sin(ph + 0x40000000u); }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline uint32_t R16(fg_vis_t *V) { return fg_rand(&V->rng) >> 16; }
static uint32_t h16(uint32_t a, uint32_t b)               /* en lugar de fract(sin(n)·43758): cualquier hash parejo sirve */
{
    uint32_t x = a ^ (b * 0x9E3779B9u);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x >> 16;
}
/* 2^-x y e^-x, x en Q16 → Q16 */
static uint32_t exp2neg(uint32_t x)
{
    uint32_t ip = x >> 16;
    if (ip >= 16) return 0;
    return (fg_exp2_q16(65536u - (x & 0xFFFFu)) >> 1) >> ip;
}
static uint32_t expneg(uint32_t x) { return exp2neg((uint32_t)(((uint64_t)x * 94548u) >> 16)); }
static uint32_t isqrt(uint32_t n)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > n) b >>= 2;
    while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}

/* ---- pixeles: el diseño es de 240, la pantalla de w×h ---- */

static void pxd(fg_vis_t *V, int x, int y, int v)
{
    if ((unsigned)x >= FG_VIS_D || (unsigned)y >= FG_VIS_D || v <= 0) return;
    unsigned q = v > 255 ? 15u : (unsigned)v >> 4;             /* 4 bits */
    if (!q) return;
    unsigned x0 = V->xm[x], x1 = V->xm[x + 1], y0 = V->ym[y], y1 = V->ym[y + 1];
    if (x1 == x0) x1++;
    if (y1 == y0) y1++;
    for (unsigned yy = y0; yy < y1; yy++) {
        uint8_t *r = V->buf + yy * (V->w >> 1);
        for (unsigned xx = x0; xx < x1; xx++) {
            uint8_t *b = r + (xx >> 1);
            if (xx & 1u) { if ((*b & 15u) < q) *b = (uint8_t)((*b & 0xF0u) | q); }
            else if ((*b >> 4) < q) *b = (uint8_t)((*b & 15u) | q << 4);
        }
    }
}
/* una fila de la intensidad a 8 bits (0..255) */
static void unpack(const fg_vis_t *V, unsigned y, uint8_t *d)
{
    const uint8_t *s = V->buf + y * (V->w >> 1);
    for (unsigned x = 0; x < V->w; x += 2) { unsigned b = *s++; d[x] = (uint8_t)((b >> 4) * 17u); d[x + 1] = (uint8_t)((b & 15u) * 17u); }
}
static inline void px4(fg_vis_t *V, int x4, int y4, int v) { pxd(V, x4 >> 4, y4 >> 4, v); }

static void disc(fg_vis_t *V, int cx4, int cy4, int r4, int v)
{
    if (r4 <= 0 || v <= 0) return;
    int ya = (cy4 - r4) >> 4, yb = (cy4 + r4) >> 4, xa = (cx4 - r4) >> 4, xb = (cx4 + r4) >> 4, r2 = r4 * r4;
    if (ya < 0) ya = 0;
    if (xa < 0) xa = 0;
    if (yb > FG_VIS_D - 1) yb = FG_VIS_D - 1;
    if (xb > FG_VIS_D - 1) xb = FG_VIS_D - 1;
    for (int y = ya; y <= yb; y++) {
        int dy = y * 16 - cy4;
        for (int x = xa; x <= xb; x++) { int dx = x * 16 - cx4; if (dx * dx + dy * dy <= r2) pxd(V, x, y, v); }
    }
}
/* anillo con bamboleo: wob8 en pixeles Q8, sd = fase extra del bamboleo */
static void ring(fg_vis_t *V, int cx4, int cy4, int r4, int w4, int v, int wob8, uint32_t sd)
{
    int n = (r4 * 7) >> 4;
    if (n < 30) n = 30;
    uint32_t step = TURN(n), mul = 1280u + (uint32_t)wob8;          /* sin(an·(5 + wob) + sd) */
    for (int j = 0; j < n; j++) {
        uint32_t ph = step * (uint32_t)j;
        int rr = r4 + (((wob8 >> 4) * SIN((uint32_t)(((uint64_t)ph * mul) >> 8) + sd)) >> 15);
        int32_t c = COS(ph), s = SIN(ph);
        for (int q = -w4 / 2; q <= w4 / 2; q += 11) {             /* cada 0.7 px */
            int rq = rr + q;
            px4(V, cx4 + ((c * rq) >> 15), cy4 + ((s * rq) >> 15), v);
        }
    }
}
static void digit(fg_vis_t *V, unsigned d, int cx4, int cy4, int v, int s)
{
    for (int y = 0; y < 5; y++) for (int x = 0; x < 3; x++) {
        if (!((DIG[d][y] >> (2 - x)) & 1u)) continue;
        for (int a = 0; a < s; a++) for (int b = 0; b < s; b++) px4(V, cx4 - 24 * s + (x * s + a) * 16, cy4 + (y * s + b) * 16, v);
    }
}
static void number(fg_vis_t *V, unsigned n, int cx4, int cy4, int v, int s)
{
    unsigned d[3], len = 0;
    do { d[len++] = n % 10u; n /= 10u; } while (n && len < 3);
    for (unsigned i = 0; i < len; i++) digit(V, d[len - 1 - i], cx4 + (int)(2 * i + 1 - len) * s * 4 * 8, cy4, v, s);
}

/* ---- golpes ---- */

static uint32_t lifeOf(const fg_vis_ent_t *e)                 /* segundos Q16 */
{
    const uint16_t *q = e->q;
    switch (e->t) {
    case 0: return 16384u + q[2] * 333u;
    case 1: return e->m == 0 ? 16384u + q[3] * 154u : 13107u + q[3] * 230u + q[4] * 154u;
    case 2: return e->m == 0 ? 3932u + q[1] * 307u : 7864u + q[1] * 666u;
    case 3: return e->m == 0 ? 9830u + q[2] * 358u + q[4] * 307u : 7864u + q[3] * 282u;
    default:
        if (e->m == FGY_CUERDAS || e->m == FGY_DRON) return 32768u + q[5] * 700u;   /* pads: viven lo que su SALIDA */
        if (e->m == FGY_TEXTURA) return 32768u + q[6] * 700u;
        if (e->m == FGY_DUB) return 45000u;
        if (e->m == FGY_SH) return 16384u + q[6] * 500u;      /* lo que su FORMA */
        return 16384u + q[5] * 358u;
    }
}

static void draw(fg_vis_t *V, const fg_vis_ent_t *e, uint32_t a16, int k8, int sc8)
{
    const uint16_t *q = e->q;
    const uint8_t *p = e->p;
    int v = mul8(mul8(256 - k8, 128 + e->vq / 2), 154 + mul8(102, sc8));
    if (v > 256) v = 256;
    uint32_t seed = e->seed;
    switch (e->t) {
    case 0:
        if (e->m == 0) {                                        /* onda expansiva */
            int r4 = mul8(96 + mul8(mul8(k8, 1888), 128 + mul8(q[1], 179)), sc8);
            int w4 = mul8(32 + mul8(q[3], 80) + mul8(mul8(q[6], 80), 256 - k8), mini(sc8, 410));
            ring(V, 1920, 1920, r4, w4, v, q[3] * 9, (uint32_t)(((uint64_t)seed * 815u) >> 8));
            if (q[4] > 5) {
                int rk8 = mini(256, (int)((a16 << 8) / (19661u + q[4] * 461u)));
                disc(V, 1920, 1920, mul8(352 + mul8(mul8(q[4], 1120), 256 - mul8(rk8, 102)), sc8), mul8(mul8(77, q[4]), 256 - rk8));
            }
            if (q[5] > 12 && a16 < 1638u) disc(V, 1920, 1920, mul8(48 + mul8(q[5], 192), sc8), 256);
        } else {                                                /* membrana: modos que se apagan */
            int R4 = mul8(544 + mul8(q[0], 480), sc8);
            uint32_t e18 = expneg(a16 * 18u), e6 = expneg(a16 * 6u), e8 = expneg(a16 * 8u);
            uint32_t ten = 65536u + ((q[1] * 358u) * (e18 >> 1) >> 15);
            int amp4 = mul8(mul8(160 + mul8(q[4], 160) + mul8(q[6], 160), v), sc8);
            uint64_t ph = (((uint64_t)a16 * ten) >> 16) * 625830u;   /* a·60·ten radianes */
            int32_t s0 = SIN((uint32_t)ph), s1 = SIN((uint32_t)((ph * 407u) >> 8)), s2 = SIN((uint32_t)((ph * 548u) >> 8));
            int32_t t1 = ((s1 * (51 + mul8(q[3], 154))) >> 8) * (int32_t)(e6 >> 1) >> 15;
            int32_t t2 = ((s2 * (26 + mul8(q[3], 128))) >> 8) * (int32_t)(e8 >> 1) >> 15;
            uint32_t sd6 = (uint32_t)(((uint64_t)seed * 244u) >> 8);
            for (int j = 0; j < 220; j++) {
                uint32_t an = TURN(220) * (uint32_t)j;
                int32_t d = (s0 >> 1) + ((COS(an + sd6) * t1) >> 15) + ((COS(an * 2u) * t2) >> 15);
                int rd = R4 + ((amp4 * d) >> 15);
                px4(V, 1920 + ((COS(an) * rd) >> 15), 1920 + ((SIN(an) * rd) >> 15), v);
            }
        }
        return;
    case 1: {
        int cx4 = 960 + (int)(((seed >> 16) * 1920u) >> 16), cy4 = 800 + (int)((h16(seed, 9) * 1760u) >> 16);
        if (e->m == 1) {                                        /* aplauso: varias manos que estallan desfasadas; SALA deja una nube */
            int nb = 1 + p[1] * 599 / 12700, n = 8 + p[5] * 20 / 127;
            uint32_t gap = 262u + (uint32_t)mul8(q[2], 1704);
            for (int b = 0; b < nb; b++) {
                int32_t tb = (int32_t)a16 - b * (int32_t)gap;
                if (tb < 0) continue;
                int hx4 = cx4 + mul8((((int)h16(seed ^ 7u, (uint32_t)b) - 32768) * 1120) >> 16, sc8);
                int hy4 = cy4 + mul8((((int)h16(seed ^ 3u, (uint32_t)b) - 32768) * 800) >> 16, sc8);
                int kk8 = mini(256, (int)(((uint32_t)tb << 8) / (5243u + q[3] * 128u)));
                int vv = mul8(v, 256 - mul8(kk8, 154));
                for (int i = 0; i < n; i++) {
                    uint32_t an = TURN(n) * (uint32_t)i + (uint32_t)b * RAD;
                    int sp4 = mul8(mul8(480 + (int)((h16(seed, (uint32_t)(i + b + 1000)) * 1440u) >> 16), sc8), kk8);
                    px4(V, hx4 + ((COS(an) * sp4) >> 15), hy4 + ((SIN(an) * sp4) >> 15), vv);
                }
            }
            if (p[4] > 3) {
                int rn = (p[4] * 220 / 127) * (256 - k8) * sc8 >> 16;
                for (int i = 0; i < rn; i++) {
                    uint32_t an = fg_rand(&V->rng);
                    int rr4 = mul8((int)((R16(V) * 1280u) >> 16), sc8);
                    px4(V, cx4 + mul8((COS(an) * rr4) >> 15, 358), cy4 + ((SIN(an) * rr4) >> 15), mul8(v, 90));
                }
            }
            return;
        }
        int n = mul8(2560 + q[2] * 34, mini(sc8, 384)) >> 8;     /* chispas que salen del centro */
        uint32_t sd6 = (uint32_t)(((uint64_t)seed * 244u) >> 8);
        for (int i = 0; i < n; i++) {
            uint32_t an = TURN(n) * (uint32_t)i + sd6;
            int sp4 = mul8(mul8(960 + (int)((h16(seed, (uint32_t)i + 2000u) * 2400u) >> 16), 128 + q[1]), sc8);
            int d4 = (sp4 * (int)(a16 >> 2)) >> 14;
            int x4 = cx4 + ((COS(an) * d4) >> 15), y4 = cy4 + ((SIN(an) * d4) >> 15);
            px4(V, x4, y4, v); px4(V, x4 + 16, y4, v); px4(V, x4, y4 + 16, v);
        }
        int wn = (p[2] * 160 / 127) * (256 - k8) * sc8 >> 16;
        for (int i = 0; i < wn; i++) {
            uint32_t an = fg_rand(&V->rng);
            int rr4 = mul8(mul8((int)((R16(V) * 480u) >> 16), 256 + q[3]), sc8);
            px4(V, cx4 + ((COS(an) * rr4) >> 15), cy4 + ((SIN(an) * rr4) >> 15), mul8(v, 179));
        }
        return; }
    case 2:
        if (e->m == 0) {                                        /* lluvia de rayitas */
            int yc4 = 352 + mul8(256 - q[2], 1120), h4 = mul8(128 + mul8(q[1], 800), sc8);
            int n = ((5120 + q[0] * 320) * (256 - k8) >> 8) * sc8 >> 16, len = 1 + p[5] * 3 / 127;
            int sh4 = (mul8(q[4], 640) * SIN((uint32_t)((uint64_t)a16 * 312906u))) >> 15;
            for (int i = 0; i < n; i++) {
                int x4 = (int)((R16(V) * 3840u) >> 16) + sh4, y4 = yc4 + ((((int)R16(V) - 32768) * h4) >> 16);
                for (int k = 0; k < len; k++) px4(V, x4 + k * 16, y4, v);
            }
        } else {                                                /* platillo: 6 franjas que titilan */
            int base4 = 2400 - mul8(q[0], 1440), gap4 = mul8(96 + mul8(q[2], 224), sc8), scc = mini(sc8, 282);
            uint32_t fr = (uint32_t)(((uint64_t)a16 * (uint32_t)(30 + p[5] * 260 / 127)) >> 16);
            uint32_t thr = 22938u + (uint32_t)(256 - q[5]) * 128u;
            for (int i = 0; i < 6; i++) {
                int amp8 = ((i ? 64 + mul8(q[3], 192) : 256) * (256 - i * 20)) >> 8;
                int L4 = mul8(mul8(mul8(1760, v), amp8), scc), y4 = base4 - i * gap4, vv = mul8(v, amp8);
                for (int x4 = 1920 - L4; x4 < 1920 + L4; x4 += 32)
                    if (h16((uint32_t)(x4 >> 4) * 7919u + fr, (uint32_t)i) < thr) px4(V, x4, y4, vv);
            }
        }
        return;
    case 3:
        if (e->m == 0) {                                        /* gota: un anillo (nb = floor(p4·9/128)+1 en la caja: siempre 1) */
            int cx4 = 640 + (int)(((seed >> 16) * 2560u) >> 16), cy4 = 2720 - mul8(q[0], 1280);
            int r4 = mul8(48 + ((int)((a16 * 1440u) >> 16) * (128 + q[2]) >> 8), sc8);
            ring(V, cx4, cy4, r4, 24, v, 0, 0);
        } else {                                                /* bloques que se pixelean */
            int n = mul8(1536 + q[1] * 26, mini(sc8, 410)) >> 8, blk8 = 256 + q[5] * 6;
            int cx8 = 120 * 256 + ((((int)h16(seed, 3) - 32768) * 80) >> 8), cy8 = 150 * 256 + ((((int)h16(seed, 5) - 32768) * 60) >> 8);
            for (int i = 0; i < n; i++) {
                int w8 = mul8(512 + (int)((R16(V) * 12u * (102u + q[4])) >> 16), sc8);
                int h8 = 256 + (int)((R16(V) * 3u * (uint32_t)blk8) >> 16);
                int x8 = cx8 + mul8(mul8((((int)R16(V) - 32768) * 110) >> 8, sc8), 102 + q[1]);
                int y8 = cy8 + mul8((((int)R16(V) - 32768) * 70) >> 8, sc8);
                for (int yy = 0; yy * 256 < h8; yy++) for (int xx = 0; xx * 256 < w8; xx++) {
                    int X = x8 + xx * 256, Y = y8 + yy * 256;
                    X = (X >= 0 ? X / blk8 : -((-X + blk8 - 1) / blk8)) * blk8;
                    Y = (Y >= 0 ? Y / blk8 : -((-Y + blk8 - 1) / blk8)) * blk8;
                    pxd(V, X >> 8, Y >> 8, v);
                }
            }
        }
        return;
    default: {
        int slot = e->t - 4, note = 24 + p[0] * 61 / 128;
        int cy4 = 3600 - (note - 24) * 54, cx4 = 576 + slot * 896;
        if (cy4 < 320) cy4 = 320;
        if (cy4 > 3600) cy4 = 3600;
        if (e->m == FGY_ACIDO) {                         /* ÁCIDO: seno que corre */
            int A4 = mul8(mul8(64 + mul8(q[3], 320), v), sc8);
            uint32_t inc = (uint32_t)(((uint64_t)(65536u + q[1] * 1280u) * 17895697u) >> 16), ph = (uint32_t)((uint64_t)a16 * 73013u);
            for (int x = 0; x < FG_VIS_D; x++) {
                int y = (cy4 + ((A4 * SIN(ph + inc * (uint32_t)x)) >> 15)) >> 4;
                pxd(V, x, y, v); pxd(V, x, y + 1, v);
            }
        } else if (e->m == FGY_SH) {                            /* SH: dientes de sierra; ONDA los multiplica, CORTE los afila */
            int A4 = mul8(mul8(96 + mul8(q[3], 256), v), sc8), ex = 256 + q[3] * 3, n = ex >> 8, fr8 = ex & 255;
            uint32_t teeth = 196608u + q[1] * 2304u;
            for (int x = 0; x < FG_VIS_D; x++) {
                uint32_t f = ((uint32_t)x * teeth / 240u + a16 * 2u) & 0xFFFFu, pw = 65536u;
                for (int k = 0; k < n; k++) pw = (pw * f) >> 16;
                pw = (uint32_t)(((uint64_t)pw * (65536u - ((uint32_t)fr8 << 8) + ((fr8 * f) >> 8))) >> 16);   /* f^e entre potencias enteras */
                pxd(V, x, (cy4 + ((A4 * ((int)pw - 32768) * 2) >> 16)) >> 4, v);
            }
        } else if (e->m == FGY_FM) {                            /* FM: Lissajous de la razón */
            int A4 = mul8(mul8(160 + mul8(q[2], 416), sc8), v);
            uint32_t R = RATIO[mini(8, p[1] * 9 / 127)], aph = (uint32_t)((uint64_t)a16 * 31291u), p3 = q[3] * 8010487u;
            for (int j = 0; j < 260; j++) {
                uint32_t tt = TURN(260) * (uint32_t)j;
                px4(V, cx4 + ((A4 * SIN((uint32_t)(((uint64_t)tt * R) >> 8) + aph)) >> 15), cy4 + ((A4 * SIN(tt + p3)) >> 15), v);
            }
        } else if (e->m == FGY_CUERDAS) {                       /* CUERDAS: tres hebras desafinadas que laten entre sí */
            int A4 = mul8(mul8(80 + mul8(q[2], 200), v), sc8);
            uint32_t ph = (uint32_t)((uint64_t)a16 * 20000u), det = (uint32_t)q[1] * 90000u;
            for (int k = 0; k < 3; k++) {
                uint32_t inc = TURN(240) * 3u + (uint32_t)(k - 1) * (det >> 4), p0 = ph * (uint32_t)(k + 1) + (seed >> k);
                for (int x = 0; x < FG_VIS_D; x++) pxd(V, x, (cy4 + ((A4 * SIN(p0 + inc * (uint32_t)x)) >> 15) + (k - 1) * 48) >> 4, mul8(v, 200 + k * 28));
            }
        } else if (e->m == FGY_TEXTURA) {                       /* TEXTURA: granos que flotan y se dispersan */
            int nd = 20 + mul8(q[1], 120), spr = 300 + mul8(q[3], 1800), r4 = 8 + mul8(q[2], 40);
            uint32_t drift = (uint32_t)((uint64_t)a16 * 400u);
            for (int i = 0; i < nd; i++) {
                int x4 = 1920 + (int)(((int32_t)h16(seed, (uint32_t)i) - 32768) * spr >> 15) + (int)((drift * (uint32_t)(1 + (i & 3))) >> 16);
                int y4 = cy4 + (int)(((int32_t)h16(seed, (uint32_t)i + 777u) - 32768) * (spr / 2) >> 15) - (int)(drift >> 17);
                disc(V, x4, y4, r4, mul8(v, 120 + (int)(h16(seed, (uint32_t)i + 99u) >> 9)));
            }
        } else if (e->m == FGY_DRON) {                          /* DRON: rachas de líneas que corren */
            int nl = 10 + mul8(q[2], 40), wv = 40 + mul8(q[3], 240);
            uint32_t run = (uint32_t)((uint64_t)a16 * (60000u + q[2] * 900u));
            for (int i = 0; i < nl; i++) {
                int y0 = cy4 + (int)(((int32_t)h16(seed, (uint32_t)i) - 32768) * 900 >> 15);
                int x0 = (int)((h16(seed, (uint32_t)i + 31u) * 240u + (run >> 16) * (uint32_t)(2 + (i & 3))) % 300u) - 30, len = 20 + (int)(h16(seed, (uint32_t)i + 57u) % 60u);
                for (int x = x0; x < x0 + len; x++) pxd(V, x, (y0 + ((wv * SIN((uint32_t)x * 30000000u + run)) >> 15)) >> 4, mul8(v, 90 + (int)((uint32_t)(x - x0) * 166u / (uint32_t)len)));
            }
        } else if (e->m == FGY_DUB) {                           /* DUB: los tiradores, latiendo con el eco */
            int lv[4] = {q[1], q[2], q[3], q[3] / 2}, wob = mul8(q[5], 12);
            for (int k = 0; k < 4; k++) {
                int h4 = mul8(mul8(lv[k] * 6 + 160, v), sc8) + ((wob * SIN((uint32_t)((uint64_t)a16 * 90000u) + (uint32_t)k * 0x40000000u)) >> 15) * 16;
                int x4 = cx4 - 360 + k * 240;
                for (int y4 = cy4 - h4; y4 <= cy4; y4 += 16) for (int xx = 0; xx < 160; xx += 16) px4(V, x4 + xx, y4, mul8(v, 160 + k * 24));
            }
        } else if (e->m == FGY_RESONADOR) {                     /* RESONADOR: anillos a las razones de su material */
            static const uint16_t RR[4][4] = {{256, 512, 768, 1024}, {256, 706, 1383, 2287}, {256, 410, 573, 763}, {256, 594, 1088, 1697}};
            const uint16_t *rr = RR[mini(3, p[1] * 3 / 127)];
            for (int k = 0; k < 4; k++) {
                int r4 = mul8(mul8(mul8(160 + k * 30, 256 + (int)(rr[k] >> 3)), sc8), 128 + mul8(k8, 128));
                ring(V, cx4, cy4, r4, 20, mul8(v, 256 - k * 40), q[6] * 2, seed + (uint32_t)k * 77777u);
            }
        }
        return; }
    }
}

/* ---- vistas fijas ---- */

static void renderMixer(fg_vis_t *V, const fg_vis_ui_t *ui)
{
    fg_zero(V->buf, (unsigned)V->w * V->h / 2u);
    unsigned MU = ui->has_pend ? ui->pend : ui->mute;
    for (int t = 0; t < 8; t++) {
        int x04 = 80 + t * 470, cx4 = x04 + 184, vol = ui->mix[t][0], pan = ui->mix[t][1], dly = ui->mix[t][2], rev = ui->mix[t][3];
        int off = ((MU >> t) & 1u) || (ui->solo && !((ui->solo >> t) & 1u)), dim = off ? 90 : 256;
        int fh = (vol * 320 + 127) / 254, top = 196 - fh;
        if ((MU >> t) & 1u) {
            for (int x4 = x04; x4 < x04 + 368; x4 += 16) { px4(V, x4, 195 * 16, 154); px4(V, x4, 196 * 16, 154); }
        } else {
            for (int y = top; y <= 196; y += 2) { px4(V, x04, y * 16, mul8(141, dim)); px4(V, x04 + 22 * 16, y * 16, mul8(141, dim)); }
            for (int x4 = x04; x4 < x04 + 368; x4 += 32) px4(V, x4, top * 16, mul8(141, dim));
            int lh = mini(fh, (int)((isqrt((uint32_t)V->level[t] * 2u) * 170u + 128u) >> 8));
            for (int y = 196 - lh; y <= 196; y++) for (int x4 = x04 + 48; x4 < x04 + 320; x4 += 16) px4(V, x4, y * 16, mul8(((ui->solo >> t) & 1u) ? 256 : 218, dim));
            int pxm4 = cx4 + (pan - 64) * 144 / 64;
            for (int y = 20; y < 25; y++) { px4(V, pxm4, y * 16, dim); px4(V, pxm4 + 16, y * 16, dim); }
            /* arriba, en puntos: delay (fila de arriba) y reverb (abajo); antes la reverb era un halo gris que se confundía con apagado */
            int nd = (dly * 10 + 127) / 254, nr = (rev * 10 + 127) / 254;
            for (int d = 0; d < nd; d++) for (int yy = 10; yy <= 11; yy++) { px4(V, x04 + (2 + d * 4) * 16, yy * 16, dim); px4(V, x04 + (3 + d * 4) * 16, yy * 16, dim); }
            for (int d = 0; d < nr; d++) for (int yy = 15; yy <= 16; yy++) { px4(V, x04 + (2 + d * 4) * 16, yy * 16, dim); px4(V, x04 + (3 + d * 4) * 16, yy * 16, dim); }
        }
    }   /* (los números de pista, prendida/apagada/solo en color, los pone la caja encima: fg_box_draw) */
}

static void renderPatterns(fg_vis_t *V, const fg_vis_ui_t *ui)
{
    for (uint32_t i = 0, n = (uint32_t)V->w * V->h / 2u; i < n; i++) V->buf[i] = (uint8_t)((V->buf[i] >> 2) & 0x33u);
    int blink = (int)((ui->now_ms / 180u) & 1u);
    for (int i = 0; i < 16; i++) {
        int x = 22 + (i & 3) * 52, y = 14 + (i >> 2) * 44, cur = i == ui->cur;
        int v = cur ? 256 : (i == ui->queued && blink) ? 256 : ((ui->used >> i) & 1u) ? 141 : 64;
        for (int xx = 0; xx < 40; xx++) { pxd(V, x + xx, y, v); pxd(V, x + xx, y + 33, v); }
        for (int yy = 0; yy < 34; yy++) { pxd(V, x, y + yy, v); pxd(V, x + 39, y + yy, v); }
        if (cur) {
            for (int xx = -3; xx < 43; xx++) { pxd(V, x + xx, y - 3, 256); pxd(V, x + xx, y + 36, 256); }
            for (int yy = -3; yy < 37; yy++) { pxd(V, x - 3, y + yy, 256); pxd(V, x + 42, y + yy, 256); }
        }
        number(V, (unsigned)i + 1, (x + 20) * 16, (y + 10) * 16, v, 3);
    }
}

/* ---- API ---- */

uint32_t fg_vis_size(unsigned w, unsigned h, unsigned hist)
{
    uint32_t stride = (w + 7u) / 8u;
    return (uint32_t)w * h / 2u + hist * stride * h + (FG_VIS_BAND + 2u) * w + FG_VIS_BAND * w + w;
}

void fg_vis_init(fg_vis_t *V, void *mem, unsigned w, unsigned h, unsigned hist, uint32_t seed)
{
    fg_zero(V, sizeof *V);
    fg_zero(mem, fg_vis_size(w, h, hist));
    V->w = (uint16_t)w; V->h = (uint16_t)h; V->hist_n = (uint16_t)hist; V->stride = (uint16_t)((w + 7u) / 8u);
    for (unsigned i = 0; i <= FG_VIS_D; i++) { V->xm[i] = (uint16_t)(i * w / FG_VIS_D); V->ym[i] = (uint16_t)(i * h / FG_VIS_D); }
    V->buf = (uint8_t *)mem;
    V->hist = V->buf + (uint32_t)w * h / 2u;
    V->sa = V->hist + (uint32_t)hist * V->stride * h;
    V->sb = V->sa + (FG_VIS_BAND + 2u) * w;
    V->row = V->sb + FG_VIS_BAND * w;
    V->rng = seed ? seed : 1u;
    V->brk = 65536u;
    V->push_slot = V->src_slot = -1;
}

void fg_vis_hit(fg_vis_t *V, unsigned t, unsigned m, unsigned vel, const uint8_t p[8], uint32_t now_ms)
{
    if (((V->mute >> t) & 1u) || (V->solo && !((V->solo >> t) & 1u))) return;
    if (V->nents >= FG_VIS_ENTS) { for (int i = 1; i < V->nents; i++) V->ents[i - 1] = V->ents[i]; V->nents--; }
    fg_vis_ent_t *e = &V->ents[V->nents++];
    e->t = (uint8_t)t; e->m = (uint8_t)m;
    for (int i = 0; i < 8; i++) { e->p[i] = p[i]; e->q[i] = (uint16_t)((p[i] * 256u + 63u) / 127u); }
    e->vq = (uint16_t)(vel * 256u / 127u);
    e->seed = fg_rand(&V->rng); e->born = now_ms;
}
void fg_vis_level(fg_vis_t *V, unsigned t, int32_t peak)
{
    int32_t d = (int32_t)(((int64_t)V->level[t] * 27853) >> 15);
    V->level[t] = peak > d ? peak : d;
}
void fg_vis_frame(fg_vis_t *V, const fg_vis_ui_t *ui)
{
    uint32_t now = ui->now_ms, dt = now - V->last_ms, perf = ui->perf;
    V->last_ms = now; V->mute = ui->mute; V->solo = ui->solo; V->view = ui->view; V->perf = perf;
    for (int k = 0; k < 4; k++) V->kmov[k] = ui->kmov[k];
    V->kph += dt * 6000000u;
    V->beat_ph += dt * ui->bpm * 286331u;                     /* dieciseisavos: bpm/15000 por ms */

    if (perf & FGP_BRAKE) V->brk = V->brk > 786u ? V->brk - 786u : 0; else V->brk = 65536u;
    uint32_t slow = (perf & FGP_HALF) ? V->brk >> 1 : V->brk;
    if (!(perf & FGP_FREEZE)) {                               /* estelas: ×0.86 por cuadro, más lento con MEDIO TIEMPO / FRENO */
        uint32_t d8 = exp2neg((14260u * slow) >> 16) >> 8;
        d8 += ((256u - d8) * V->kmov[1] * 3u) >> 10;          /* K2: las estelas duran más mientras se mueve */
        if (d8 < 256u) {                                      /* (en 4 bits: redondeo al azar por cuadro, en promedio cae igual que en 8) */
            uint8_t lut[16];
            unsigned bias = R16(V) >> 8;
            for (unsigned k = 0; k < 16u; k++) lut[k] = (uint8_t)((k * d8 + bias) >> 8);
            for (uint32_t i = 0, n = (uint32_t)V->w * V->h / 2u; i < n; i++) { unsigned b = V->buf[i]; V->buf[i] = (uint8_t)(lut[b >> 4] << 4 | lut[b & 15u]); }
        }
    }

    int w = 0;
    for (int i = 0; i < V->nents; i++) {
        const fg_vis_ent_t *e = &V->ents[i];
        uint32_t a16 = (uint32_t)((((uint64_t)(now - e->born) * 4294967u) >> 16) * slow >> 16), L = lifeOf(e);
        if (a16 > L) continue;
        if (ui->view == FG_VIEW_VIS) {
            int32_t lv = V->level[e->t], x = lv * 8 / 5;          /* sqrt(min(1, nivel·1.6)) */
            int sc8 = 90 + (int)((3u * isqrt((uint32_t)(x > 32768 ? 32768 : x) * 2u)) >> 1);
            draw(V, e, a16, (int)(((a16 << 14) / ((L >> 2) | 1u)) >> 8), sc8);
        }
        if (w != i) V->ents[w] = *e;
        w++;
    }
    V->nents = (uint16_t)w;
    if (ui->view == FG_VIEW_MIX) renderMixer(V, ui);
    if (ui->view == FG_VIEW_PAT) renderPatterns(V, ui);

    /* lo que harán las bandas: qué cuadro se guarda, de cuál se lee (repite / stutter / reversa) */
    V->push_slot = V->src_slot = -1;
    V->gate_off = (perf & FGP_GATE) && V->beat_ph >= 0x80000000u;
    V->riser_seed = fg_rand(&V->rng) | 1u;
    if (ui->view != FG_VIEW_VIS || !V->hist_n) return;
    unsigned N = V->hist_n, rep2 = (perf & FGP_STUT32) ? 1 : (perf & FGP_STUT16) ? 2 : (perf & FG_VIS_REP8) ? 4 : (perf & FG_VIS_REP4) ? 8 : 0;
    if (!(rep2 && V->loop_id == rep2)) {                      /* mientras repite, la historia se queda quieta */
        V->push_slot = (int16_t)V->hp;
        V->hp = (uint16_t)((V->hp + 1u) % N);
        if (V->hn < N) V->hn++;
    }
    unsigned cur = (V->hp + N - 1u) % N;
    if (rep2) {
        if (V->loop_id != rep2) {
            unsigned fr = (rep2 * 450u + ui->bpm / 2u) / ui->bpm;
            V->loop_id = (uint8_t)rep2; V->loop_n = 0;
            V->loop_fr = (uint16_t)(fr < 2 ? 2 : fr > N ? N : fr); V->loop_base = (uint16_t)cur;
        }
        unsigned k = V->loop_fr - 1u - (V->loop_n++ % V->loop_fr);
        if (k > V->hn - 1u) k = V->hn - 1u;
        V->src_slot = (int16_t)((V->loop_base + N - k) % N);
    } else V->loop_id = 0;
    if (perf & FGP_REV) {
        unsigned k = (V->loop_n++ % 60u) * 2u;
        if (k > V->hn - 1u) k = V->hn - 1u;
        V->src_slot = (int16_t)((cur + N - k) % N);
    }
}

static void dither(const uint8_t *src, unsigned w, unsigned y, uint8_t *out)
{
    const uint8_t *th = BAYER + (y & 3u) * 4u;
    for (unsigned x = 0; x < w; x += 8) {
        uint8_t b = 0;
        for (unsigned k = 0; k < 8 && x + k < w; k++) if (src[x + k] > th[(x + k) & 3u]) b |= (uint8_t)(0x80u >> k);
        out[x >> 3] = b;
    }
}

void fg_vis_band(fg_vis_t *V, unsigned y0, unsigned rows, uint8_t *out)
{
    unsigned W = V->w, H = V->h, F = (unsigned)V->stride * H;
    if (y0 >= H) return;
    if (rows > FG_VIS_BAND) rows = FG_VIS_BAND;
    if (y0 + rows > H) rows = H - y0;
    int vis = V->view == FG_VIEW_VIS;
    uint32_t perf = vis ? V->perf : 0;
    if (!vis) { for (unsigned r = 0; r < rows; r++) { unpack(V, y0 + r, V->row); dither(V->row, W, y0 + r, out + r * V->stride); } return; }

    if (V->push_slot >= 0) for (unsigned r = 0; r < rows; r++) { unpack(V, y0 + r, V->row); dither(V->row, W, y0 + r, V->hist + V->push_slot * F + (y0 + r) * V->stride); }
    for (int r = -1; r <= (int)rows; r++) {                   /* la banda más una fila arriba y abajo (para FILTRO GRAVE) */
        int y = (int)y0 + r;
        y = y < 0 ? 0 : y >= (int)H ? (int)H - 1 : y;
        uint8_t *d = V->sa + (unsigned)(r + 1) * W;
        if (V->src_slot >= 0) {
            const uint8_t *b = V->hist + V->src_slot * F + (unsigned)y * V->stride;
            for (unsigned x = 0; x < W; x++) d[x] = (b[x >> 3] >> (7u - (x & 7u))) & 1u ? 255 : 0;
        } else unpack(V, (unsigned)y, d);
    }
    for (unsigned r = 0; r < rows; r++) {
        const uint8_t *c = V->sa + (r + 1) * W, *u = c - W, *dn = c + W;
        uint8_t *o = V->sb + r * W;
        unsigned y = y0 + r;
        if ((perf & FGP_LP) && y >= 1 && y + 1 < H) {
            o[0] = c[0]; o[W - 1] = c[W - 1];
            for (unsigned x = 1; x + 1 < W; x++) o[x] = (uint8_t)(((2u * c[x] + c[x - 1] + c[x + 1] + u[x] + dn[x]) * 34u) >> 8);
        } else fg_copy(o, c, W);
        if (perf & FGP_HP) {
            fg_copy(V->row, o, W);
            for (unsigned x = 2; x < W; x++) { int d = V->row[x] - V->row[x - 2]; d = (d < 0 ? -d : d) * 5 / 2; o[x] = (uint8_t)(d > 255 ? 255 : d); }
        }
    }
    if (perf & FGP_BITS) for (unsigned r = 0; r < rows; r++) {
        unsigned rb = ((y0 + r) & ~7u) - y0;
        uint8_t *o = V->sb + r * W;
        const uint8_t *b = V->sb + rb * W;
        for (unsigned x = W; x-- > 0;) o[x] = b[x & ~7u];
    }
    if (perf & FGP_THROW) {                                   /* ECO: copia corrida a la derecha */
        unsigned dx = 16u * W / FG_VIS_D;
        if (!dx) dx = 1;
        for (unsigned r = 0; r < rows; r++) {
            uint8_t *o = V->sb + r * W;
            fg_copy(V->row, o, W);
            for (unsigned x = dx; x < W; x++) { unsigned v = (V->row[x - dx] * 141u) >> 8; if (o[x] < v) o[x] = (uint8_t)v; }
        }
    }
    if (V->kmov[2] > 8u) for (unsigned r = 0; r < rows; r++) {   /* K3: se pixelea */
        unsigned bk = 1u + V->kmov[2] * 7u / 256u, rb = ((y0 + r) / bk) * bk;
        uint8_t *o = V->sb + r * W;
        const uint8_t *src = rb >= y0 ? V->sb + (rb - y0) * W : o;
        for (unsigned x = W; x-- > 0;) o[x] = src[(x / bk) * bk];
    }
    if (V->kmov[0] > 8u) for (unsigned r = 0; r < rows; r++) {   /* K1: la imagen ondula de lado a lado */
        int sh = (SIN((y0 + r) * 9000000u + V->kph) * (int)V->kmov[0]) >> 18;
        if (!sh) continue;
        uint8_t *o = V->sb + r * W;
        fg_copy(V->row, o, W);
        for (unsigned x = 0; x < W; x++) { int s = (int)x - sh; o[x] = s >= 0 && s < (int)W ? V->row[s] : 0; }
    }
    if (V->kmov[3] > 8u) for (unsigned r = 0; r < rows; r++) {   /* K4: brilla */
        uint8_t *o = V->sb + r * W;
        unsigned add = V->kmov[3] / 6u;
        for (unsigned x = 0; x < W; x++) { unsigned y = o[x] + (o[x] ? add : 0u); o[x] = (uint8_t)(y > 255u ? 255u : y); }
    }
    if (V->gate_off) fg_zero(V->sb, rows * W);
    if (perf & FGP_RISER) {                                   /* SUBIDA: chispas que se juntan abajo; mismas en todas las bandas */
        uint32_t s = V->riser_seed, n = 600u * W * H / (FG_VIS_D * FG_VIS_D);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t r = fg_rand(&s) >> 16, k = r >> 12, f = r & 0xFFFu;
            uint32_t pw = POW06[k] + (((uint32_t)(POW06[k < 16 ? k + 1 : 16] - POW06[k]) * f) >> 12);
            uint32_t y = H - ((pw * H) >> 16), x = ((fg_rand(&s) >> 16) * W) >> 16, v = 102u + (((fg_rand(&s) >> 16) * 153u) >> 16);
            if (y >= y0 && y < y0 + rows) V->sb[(y - y0) * W + x] = (uint8_t)v;
        }
    }
    for (unsigned r = 0; r < rows; r++) dither(V->sb + r * W, W, y0 + r, out + r * V->stride);
}
