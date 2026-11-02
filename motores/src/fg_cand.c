/* Motores candidatos: ver fg_cand.h */
#include "fg_cand.h"

const char *const FGC_NAME[FGC_N] = {"IMPACTO", "CAJA 909", "PARCHE", "PALMAS", "METÁLICO", "CUATRO", "LYRA", "PLIEGUE", "TABLA", "ÓXIDO", "ENJAMBRE", "FASE", "SYNC", "ÓRGANO"};
const char *const FGC_LAB[FGC_N][FGC_NP] = {
    {"TONO", "BAJADA", "CURVA", "ATAQUE", "CAÍDA", "SATURACIÓN", "ARMÓNICOS", "NIVEL"},
    {"TONO", "CUERPO", "RUIDO", "CAÍDA", "COLOR", "ATAQUE", "SATURACIÓN", "NIVEL"},
    {"TONO", "TIMBRE", "CUERPO", "RUIDO", "CAÍDA", "COLOR", "SATURACIÓN", "NIVEL"},
    {"TONO", "PALMAS", "SEPARACIÓN", "CAÍDA", "SALA", "COLOR", "SATURACIÓN", "NIVEL"},
    {"TONO", "CAÍDA", "COLOR", "RUIDO", "RESONANCIA", "GOLPE", "SATURACIÓN", "NIVEL"},
    {"NOTA", "ALGORITMO", "RAZÓN", "ÍNDICE", "GOLPE", "FEEDBACK", "CAÍDA", "NIVEL"},
    {"NOTA", "FILO", "MODULACIÓN", "REALIMENTACIÓN", "DELAY", "TIEMPO", "SATURACIÓN", "NIVEL"},
    {"NOTA", "PLIEGUE", "SIMETRÍA", "FM", "RAZÓN", "CAÍDA", "COMPUERTA", "NIVEL"},
    {"NOTA", "POSICIÓN", "TORCER", "MOVIMIENTO", "FILTRO", "RESONANCIA", "FORMA", "NIVEL"},
    {"NOTA", "RAZÓN", "CRUCE", "ANILLO", "FILTRO", "RESONANCIA", "CAÍDA", "NIVEL"},
    {"NOTA", "VOCES", "DESAFINACIÓN", "DERIVA", "FORMA", "BAJADA", "FILTRO", "NIVEL"},
    {"NOTA", "FORMA", "DISTORSIÓN", "ENVOLVENTE", "CAÍDA", "SUB", "SATURACIÓN", "NIVEL"},
    {"NOTA", "SYNC", "BARRIDO", "CAÍDA", "CAOS", "FILTRO", "RESONANCIA", "NIVEL"},
    {"NOTA", "GRAVES", "AGUDOS", "ESTIRAR", "PERCUSIÓN", "CAÍDA", "GIRO", "NIVEL"}};
const uint8_t FGC_DEF[FGC_N][FGC_NP] = {
    {40, 70, 50, 50, 70, 50, 20, 100}, {50, 50, 50, 60, 60, 60, 40, 100}, {25, 64, 110, 60, 100, 100, 60, 100},
    {60, 64, 50, 60, 40, 60, 30, 100}, {64, 60, 70, 20, 40, 40, 20, 100}, {40, 0, 40, 60, 50, 20, 60, 100},
    {36, 20, 30, 15, 60, 70, 50, 100}, {40, 60, 64, 40, 40, 70, 90, 100}, {40, 40, 64, 30, 80, 30, 30, 100},
    {40, 60, 30, 50, 50, 80, 50, 100},
    {30, 64, 40, 50, 20, 0, 70, 100}, {36, 0, 40, 70, 50, 30, 30, 100}, {50, 40, 60, 50, 30, 90, 30, 100}, {40, 80, 40, 0, 60, 127, 40, 100}};
const uint8_t FGC_ROLE[FGC_N] = {FGC_R_BOMBO, FGC_R_CAJA, FGC_R_PERC, FGC_R_CAJA, FGC_R_METAL,
                                 FGC_R_SINTE, FGC_R_SINTE, FGC_R_SINTE, FGC_R_SINTE, FGC_R_SINTE,
                                 FGC_R_SINTE, FGC_R_SINTE, FGC_R_SINTE, FGC_R_SINTE};
/* AZAR: un sonido al azar que todavía sirve (la NOTA y el NIVEL no se tocan) */
const uint8_t FGC_RND[FGC_N][FGC_NP][2] = {
    {{20, 70}, {30, 110}, {20, 90}, {0, 110}, {40, 110}, {0, 100}, {0, 70}, {100, 100}},
    {{20, 90}, {20, 90}, {30, 110}, {30, 100}, {20, 110}, {0, 110}, {0, 90}, {100, 100}},
    {{0, 100}, {0, 127}, {40, 127}, {20, 110}, {40, 127}, {30, 127}, {0, 127}, {100, 100}},
    {{20, 110}, {0, 127}, {10, 110}, {20, 100}, {0, 90}, {10, 110}, {0, 80}, {100, 100}},
    {{20, 110}, {5, 110}, {30, 120}, {0, 80}, {10, 100}, {0, 110}, {0, 80}, {100, 100}},
    {{40, 40}, {0, 127}, {0, 127}, {20, 110}, {10, 110}, {0, 80}, {20, 110}, {100, 100}},
    {{36, 36}, {0, 127}, {10, 100}, {0, 90}, {0, 110}, {10, 127}, {20, 110}, {100, 100}},
    {{40, 40}, {10, 120}, {20, 108}, {0, 110}, {0, 127}, {10, 100}, {40, 127}, {100, 100}},
    {{40, 40}, {0, 127}, {20, 108}, {0, 90}, {40, 120}, {0, 100}, {0, 110}, {100, 100}},
    {{40, 40}, {0, 127}, {0, 90}, {0, 127}, {10, 110}, {0, 110}, {10, 110}, {100, 100}},
    {{30, 30}, {20, 127}, {10, 100}, {0, 110}, {0, 127}, {0, 90}, {30, 120}, {100, 100}},
    {{36, 36}, {0, 127}, {0, 110}, {20, 127}, {10, 110}, {0, 90}, {0, 90}, {100, 100}},
    {{50, 50}, {0, 100}, {20, 127}, {10, 100}, {0, 127}, {50, 127}, {0, 90}, {100, 100}},
    {{40, 40}, {20, 127}, {0, 127}, {0, 127}, {0, 127}, {30, 127}, {0, 127}, {100, 100}}};

/* ---- comunes ---- */
static int32_t note_q8(unsigned v) { return (int32_t)((24u + v * 61u / 128u) << 8); }   /* (la misma NOTA que los sintes del aparato) */
/* una perilla exponencial: lo .. lo·2^oct8/8 (cualquier unidad, p. ej. Hz Q8) */
static uint32_t kexp(unsigned v, uint32_t lo, unsigned oct8) { return (uint32_t)(((uint64_t)lo * fg_exp2_q16(v * oct8 * 8192u / 127u)) >> 16); }
static uint32_t svf_hz(uint32_t hz) { return (uint32_t)fg_svf2_f(hz < 20 ? 20 : hz > 12000 ? 12000 : hz); }
static int32_t ms(unsigned m) { return (int32_t)(m * (unsigned)FG_SR / 1000u); }
static int32_t amp_top(const fg_cand_t *c) { return (FG_ONE30 / 127) * c->vel; }   /* (sin divisiones de 64 bits: el aparato no las tiene) */
static int32_t drive(int32_t x, unsigned k) { return fg_soft((int32_t)(((int64_t)x * (4096 + (int32_t)k * 200)) >> 12)); }
static int32_t level(const fg_cand_t *c, int32_t x) { return (x * (int32_t)c->p[7]) / 100; }
static int32_t scale(int32_t x, unsigned k) { return x * (int32_t)k / 127; }
/* ataque lineal, caída hacia el sostén, salida al soltar */
static int32_t env_step(fg_cand_t *c)
{
    if (c->gated && c->t >= c->gate) c->gated = 0;
    if (!c->gated) { c->amp = fg_mul30(c->amp, c->kr); if (c->amp < 2000) c->on = 0; }
    else if (c->att_inc) { c->amp += c->att_inc; if (c->amp >= amp_top(c)) { c->amp = amp_top(c); c->att_inc = 0; } }
    else { int32_t s = fg_mul30(amp_top(c), c->sus); c->amp = s + fg_mul30(c->amp - s, c->kd); }
    c->t++;
    return c->amp;
}

/* ---- TABLA: 16 ondas de 256 puntos. En la Mac y la web se hacen al arrancar; en el aparato van en la flash
 * (fg_cand_tab.c, que sale de fg_cand_wt() en la Mac: ver firmware/tools/gen_cand_tab.sh) ---- */
#ifdef FG_DEVICE
extern const int16_t FGC_WT[16][257];
#define W FGC_WT
static void wt_make(void) { }
static const int wt_ready = 1;
#else
static int16_t W[16][257];
static int wt_ready;
static void wt_make(void)
{
    static const int8_t H[16][16] = {
        {100}, {100, 0, -11, 0, 4, 0, -2, 0, 1}, {100, 50, 33, 25, 20, 17, 14, 12, 11, 10, 9, 8, 8, 7, 7, 6},
        {100, 0, 33, 0, 20, 0, 14, 0, 11, 0, 9, 0, 8, 0, 7}, {0}, {0}, {0}, {0},
        {100, 70, 50, 40, 0, 30, 0, 20}, {100, 0, 60, 0, 0, 0, 50, 0, 0, 0, 40, 0, 30}, {50, 0, 0, 0, 60, 90, 50, 0, 20},
        {30, 0, 0, 0, 0, 0, 0, 70, 100, 60}, {100, 0, 0, 0, 70, 0, 0, 0, 0, 0, 60, 0, 50}, {0},
        {100, 25, 33, 12, 20, 8, 14, 6, 11, 5, 9, 4, 8, 3, 7, 3}, {0}};
    uint32_t seed = 12345u;
    for (int k = 0; k < 16; k++) {
        int32_t buf[256], mx = 1;
        int8_t h[16];
        for (int i = 0; i < 16; i++) h[i] = H[k][i];
        if (k == 13 || k == 15) for (int i = 0; i < 16; i++) h[i] = (int8_t)((fg_rand(&seed) % 100u) / (unsigned)(k == 13 ? 1 : i + 1));
        for (int j = 0; j < 256; j++) {
            int64_t s = 0;
            if (k == 4 || k == 5) {                                   /* pulsos de 25 y 12 % */
                int w = k == 4 ? 64 : 32; s = (j < w ? 100 : -100) * 30000LL / 100;
                buf[j] = (int32_t)s; goto put;
            }
            if (k == 6 || k == 7) {                                   /* sierra con sync a 2 y 3.5 veces */
                uint32_t r = k == 6 ? 512u : 896u, ph = ((uint32_t)j * r) & 0xFFFFu;
                buf[j] = (int32_t)(ph >> 1) - 16384; buf[j] = buf[j] * 3 / 2 * (256 - j) / 256 + buf[j] / 2; goto put;
            }
            for (int q = 1; q <= 16; q++) {
                int a = h[q - 1];
                if (!a) continue;
                s += (int64_t)a * fg_sin((uint32_t)(j * q) << 24);
            }
            buf[j] = (int32_t)(s / 100);
        put:;
            int32_t m = buf[j] < 0 ? -buf[j] : buf[j]; if (m > mx) mx = m;
        }
        int64_t mean = 0; for (int j = 0; j < 256; j++) mean += buf[j];
        mean /= 256; mx = 1;                                                   /* (sin DC: los pulsos la tienen) */
        for (int j = 0; j < 256; j++) { buf[j] -= (int32_t)mean; int32_t m = buf[j] < 0 ? -buf[j] : buf[j]; if (m > mx) mx = m; }
        for (int j = 0; j < 256; j++) W[k][j] = (int16_t)((int64_t)buf[j] * 30000 / mx);
        W[k][256] = W[k][0];
    }
    wt_ready = 1;
}
const int16_t *fg_cand_wt(void) { if (!wt_ready) wt_make(); return &W[0][0]; }
#endif
static int32_t wt_read(int k, uint32_t ph)
{
    uint32_t i = ph >> 24, f = (ph >> 8) & 0xFFFF;
    int32_t a = W[k][i], b = W[k][i + 1];
    return a + (int32_t)(((int64_t)(b - a) * f) >> 16);
}

void fg_cand_init(fg_cand_t *c, unsigned model)
{
    fg_zero(c, sizeof *c);
    if (!wt_ready) wt_make();
    c->model = (uint8_t)(model % FGC_N);
    c->rng = 0x2545F491u + model * 977u;
    for (int i = 0; i < FGC_NP; i++) c->p[i] = c->ep[i] = FGC_DEF[c->model][i];
}
void fg_cand_param(fg_cand_t *c, unsigned i, unsigned v) { if (i < FGC_NP) c->p[i] = c->ep[i] = (uint8_t)(v > 127 ? 127 : v); }
void fg_cand_model(fg_cand_t *c, unsigned model, const uint8_t *src)
{
    if (model >= FGC_N) return;
    if (model != c->model) c->on = 0;
    c->model = (uint8_t)model;
    for (unsigned i = 0; i < FGC_NP; i++) c->p[i] = c->ep[i] = src ? (src[i] > 127 ? 127 : src[i]) : FGC_DEF[model][i];
}
void fg_cand_param_m(fg_cand_t *c, unsigned model, unsigned i, unsigned v)
{
    if (model != c->model || i >= FGC_NP) return;
    c->p[i] = c->ep[i] = (uint8_t)(v > 127 ? 127 : v);
}
void fg_cand_trig_lk(fg_cand_t *c, unsigned vel, uint32_t gate, const uint8_t *ov, unsigned mask)
{
    for (unsigned i = 0; i < FGC_NP; i++) c->p[i] = (ov && ((mask >> i) & 1u)) ? ov[i] : c->ep[i];
    fg_cand_trig(c, vel, gate);
}
void fg_cand_release(fg_cand_t *c) { if (c->on && c->gated) c->gate = c->t; }

void fg_cand_trig(fg_cand_t *c, unsigned vel, uint32_t gate)
{
    const uint8_t *p = c->p;
    c->on = 1; c->gated = 1; c->gate = gate ? gate : 1; c->t = 0; c->vel = (int32_t)(vel > 127 ? 127 : vel);
    c->note_q8 = note_q8(p[0]);
    c->att_inc = 0; c->sus = 0; c->kr = fg_decay(20, 0, 127);
    int32_t top = amp_top(c);
    switch (c->model) {
    case FGC_IMPACTO:
        c->inc[0] = fg_inc(kexp(p[0], 30u << 8, 16));                       /* 30 .. 120 Hz */
        c->y[0] = 256 + p[1] * 1280 / 127;                                    /* el barrido arranca 1 a 6 veces arriba */
        c->env2 = FG_ONE30; c->k2 = fg_decay(24u + p[2] * 36u / 127u, 0, 127);
        c->ph[0] = 0x40000000u; c->hcnt = ms(1) + ms(1) / 3;                   /* la fase se queda en 90° 1.3 ms: el escalón */
        c->amp = top; c->hold = ms(1) + ms(1) / 2; c->kd = fg_decay(45u + p[4] * 65u / 127u, 0, 127);
        c->env3 = top / 127 * p[3]; c->k3 = fg_decay(6, 0, 127);
        c->f[0].lo = c->f[0].bp = 0;
        break;
    case FGC_CAJA909: {
        uint32_t f0 = kexp(p[0], 150u << 8, 10);                              /* 150 .. 360 Hz */
        c->inc[0] = fg_inc(f0); c->inc[1] = (uint32_t)(((uint64_t)c->inc[0] * 376u) >> 8);
        c->ph[0] = c->ph[1] = 0x40000000u;
        c->env2 = FG_ONE30; c->k2 = fg_decay(29, 0, 127);
        c->amp = top; c->kd = fg_decay(25u + p[1] * 45u / 127u, 0, 127);
        c->env3 = top; c->hold = ms(40) + ms(30) * p[3] / 127; c->k3 = fg_decay(40u + p[3] * 55u / 127u, 0, 127);
        c->y[0] = (int32_t)svf_hz((f0 >> 8) * (15u + p[4] * 35u / 127u));     /* COLOR: pasabajos 15 .. 50 veces el tono */
        c->y[1] = (int32_t)svf_hz((f0 >> 8) * 10u);
        c->fb = top / 127 * p[5];
        break; }
    case FGC_PARCHE: {
        static const uint16_t RT[5] = {256, 512, 814, 1065, 1439};
        uint32_t f0 = kexp(p[0], 60u << 8, 18) >> 8;                          /* 60 .. 285 Hz: de tom a timbal */
        for (int k = 0; k < 5; k++) { c->inc[k] = svf_hz(f0 * RT[k] / 256u); c->f[k].lo = c->f[k].bp = 0; }
        c->hcnt = ms(1);
        c->env3 = top; c->k3 = fg_decay(35u + p[4] * 55u / 127u, 0, 127);
        c->y[0] = (int32_t)svf_hz(f0 * (8u + p[5] * 16u / 127u));
        break; }
    case FGC_PALMAS:
        c->inc[0] = svf_hz(kexp(p[0], 700, 14));                              /* 700 .. 2400 Hz */
        c->y[0] = 2 + p[1] * 3 / 127; c->y[1] = ms(4) + ms(14) * p[2] / 127;   /* cuántas y cada cuánto */
        c->kd = fg_decay(30u + p[3] * 50u / 127u, 0, 127); c->k2 = fg_decay(10, 0, 127);
        c->env3 = top / 127 * p[4]; c->k3 = fg_decay(70u + p[4] * 30u / 127u, 0, 127);
        c->amp = top;
        break;
    case FGC_METALICO: {
        static const uint16_t RT[6] = {256, 334, 375, 457, 495, 649};
        uint32_t sc = fg_exp2_q16((uint32_t)p[0] * 2u * 65536u / 127u);       /* x0.5 .. x2 sobre 414 Hz */
        for (int k = 0; k < 6; k++) c->inc[k] = fg_inc((uint32_t)(((uint64_t)414u * RT[k] * sc) >> 17));
        c->amp = top; c->k2 = fg_decay(18, 0, 127);                           /* el primer tramo, rápido */
        c->env3 = top; c->kd = fg_decay(25u + p[1] * 87u / 127u, 0, 127);     /* el segundo: hat a platillo */
        c->env2 = top / 127 * p[5]; c->k3 = fg_decay(4, 0, 127);
        break; }
    case FGC_CUATRO:
        c->amp = top; c->kd = fg_decay(40u + p[6] * 85u / 127u, 0, 127);
        c->sus = p[6] >= 110 ? FG_ONE30 : 0; c->kr = fg_decay(35, 0, 127);
        c->env2 = FG_ONE30; c->k2 = fg_decay(8u + p[4] * 92u / 127u, 0, 127);
        for (int k = 0; k < 4; k++) c->ph[k] = 0;
        c->y[0] = c->y[1] = 0;
        break;
    case FGC_LYRA: {
        static const uint16_t RT[4] = {256, 257, 384, 385};
        uint32_t base = fg_note_inc(c->note_q8);
        for (int k = 0; k < 4; k++) c->inc[k] = (uint32_t)(((uint64_t)base * RT[k]) >> 8);
        if (c->amp < 2000) c->amp = 0;
        c->att_inc = top / ms(15); c->sus = FG_ONE30; c->kd = FG_ONE30; c->kr = fg_decay(85, 0, 127);
        break; }
    case FGC_PLIEGUE: {
        static const uint16_t RT[8] = {64, 128, 256, 384, 512, 768, 1024, 1792};
        c->inc[0] = fg_note_inc(c->note_q8);
        c->inc[1] = (uint32_t)(((uint64_t)c->inc[0] * RT[p[4] * 8 / 128]) >> 8);
        c->env2 = top; c->k2 = fg_decay(20u + p[5] * 90u / 127u, 0, 127);
        c->amp = top;
        break; }
    case FGC_TABLA: {
        unsigned f = p[6];
        c->att_inc = f > 64 ? (int32_t)(top / (int32_t)(1u + (f - 64u) * (FG_SR / 50u) / 63u)) : top / 40;
        c->kd = fg_decay(70u + f / 2u, 0, 127);
        c->sus = f < 40 ? 0 : f > 100 ? FG_ONE30 : (FG_ONE30 / 60) * (int32_t)(f - 40u);
        c->kr = fg_decay(30u + f / 2u, 0, 127);
        c->env2 = FG_ONE30; c->k2 = fg_decay(50u + f / 3u, 0, 127);
        uint32_t w = 0x80000000u + (uint32_t)((int32_t)(p[2] - 64) * (int32_t)(0x6B000000 / 64));   /* TORCER: el quiebre de la fase */
        c->y[0] = (int32_t)w;
        break; }
    case FGC_ENJAMBRE: {
        unsigned nv = 1u + p[1] * 5u / 127u;
        c->hold = (int32_t)nv;
        for (unsigned k = 0; k < 6; k++) { c->y[k] = (int32_t)(k * 0x2AAAAAAAu); if (!c->ph[k]) c->ph[k] = k * 0x2AAAAAAAu; }
        c->att_inc = top / ms(3); c->sus = FG_ONE30; c->kd = FG_ONE30; c->kr = fg_decay(40, 0, 127);
        c->env2 = FG_ONE30 / 127 * p[5]; c->k2 = fg_decay(50, 0, 127);
        break; }
    case FGC_FASE:
        c->inc[0] = fg_note_inc(c->note_q8);
        c->att_inc = top / ms(2); c->sus = FG_ONE30; c->kd = FG_ONE30; c->kr = fg_decay(35, 0, 127);
        c->env2 = FG_ONE30; c->k2 = fg_decay(20u + p[4] * 90u / 127u, 0, 127);
        break;
    case FGC_SYNC:
        c->inc[0] = fg_note_inc(c->note_q8); c->ph[0] = c->ph[1] = 0;
        c->att_inc = top / ms(2); c->sus = FG_ONE30; c->kd = FG_ONE30; c->kr = fg_decay(40, 0, 127);
        c->env2 = FG_ONE30; c->k2 = fg_decay(20u + p[3] * 90u / 127u, 0, 127);
        break;
    case FGC_ORGANO: {
        static const int32_t L2[6] = {-65536, 0, 65536, 103872, 131072, 196608};   /* log2 de 16' 8' 4' 2 2/3' 2' 1' (Q16) */
        uint32_t base = fg_note_inc(c->note_q8);
        for (int k = 0; k < 6; k++) {                                              /* ESTIRAR: los registros se separan y dejan de ser armónicos */
            int32_t l = L2[k] + L2[k] * (int32_t)p[3] / 180;
            c->inc[k] = (uint32_t)(((uint64_t)base * fg_exp2_q16((uint32_t)(l + 2 * 65536))) >> 18);
        }
        c->amp = c->amp < 2000 ? 0 : c->amp; c->att_inc = top / ms(2);
        c->sus = p[5] >= 120 ? FG_ONE30 : 0; c->kd = fg_decay(30u + p[5] * 80u / 127u, 0, 127); c->kr = fg_decay(25, 0, 127);
        c->env3 = top / 127 * p[4]; c->k3 = fg_decay(40, 0, 127);
        break; }
    case FGC_OXIDO: {                                                           /* ÓXIDO */
        c->inc[0] = fg_note_inc(c->note_q8);
        c->inc[1] = (uint32_t)(((uint64_t)c->inc[0] * (128u + p[1] * 896u / 127u)) >> 8);   /* RAZÓN: 0.5 a 4 veces */
        c->amp = top; c->kd = fg_decay(20u + p[6] * 95u / 127u, 0, 127); c->sus = p[6] > 110 ? FG_ONE30 : 0; c->kr = fg_decay(25, 0, 127);
        c->env2 = FG_ONE30; c->k2 = fg_decay(40u + p[6] / 2u, 0, 127);
        break; }
    }
}

static void render_impacto(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t click_f = (int32_t)svf_hz(4200);
    for (unsigned i = 0; i < n; i++) {
        if (c->hcnt > 0) c->hcnt--;
        else c->ph[0] += c->inc[0] + (uint32_t)(((int64_t)c->inc[0] * (c->y[0] - 256) * (c->env2 >> 14)) >> 24);
        c->env2 = fg_mul30(c->env2, c->k2);
        int32_t s = fg_sin(c->ph[0]);
        if (p[6]) {                                                             /* ARMÓNICOS: Chebyshev T2 y T3 */
            int32_t x2 = (s * s) >> 15, t2 = 2 * x2 - 32767, t3 = (int32_t)(((int64_t)(4 * x2 - 3 * 32767) * s) >> 15);
            s += (int32_t)((t3 * 2 / 3 + t2 / 3) * (int32_t)p[6] / 127);
        }
        if (c->hold > 0) c->hold--; else c->amp = fg_mul30(c->amp, c->kd);
        int32_t y = fg_mul30(s, c->amp);
        if (c->env3 > 1000) {
            fg_svf2(&c->f[0], fg_mul30(fg_noise(&c->rng), c->env3), click_f, 16000);
            y += c->f[0].bp; c->env3 = fg_mul30(c->env3, c->k3);
        }
        out[i] += level(c, drive(y, p[5]));
        if (c->amp < 2000 && c->env3 <= 1000) { c->on = 0; break; }
    }
}
static void render_caja909(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    for (unsigned i = 0; i < n; i++) {
        uint32_t bend = (uint32_t)(c->env2 >> 15);                              /* la afinación cae en ~7 ms */
        c->ph[0] += c->inc[0] + (uint32_t)(((uint64_t)c->inc[0] * bend) >> 16);
        c->ph[1] += c->inc[1] + (uint32_t)(((uint64_t)c->inc[1] * bend) >> 16);
        c->env2 = fg_mul30(c->env2, c->k2);
        int32_t body = fg_soft(fg_sin(c->ph[0]) * 2) + fg_soft(fg_sin(c->ph[1]) * 2) * 3 / 5;
        body = fg_mul30(body, c->amp); c->amp = fg_mul30(c->amp, c->kd);
        int32_t nz = fg_noise(&c->rng);
        fg_svf2(&c->f[0], nz >> 1, c->y[0], 22000);
        fg_svf2(&c->f[1], c->f[0].lo, c->y[1], 40000);
        int32_t noise = fg_mul30(c->f[1].hi, c->env3);
        if (c->hold > 0) c->hold--; else c->env3 = fg_mul30(c->env3, c->k3);
        int32_t y = scale(body, 127u - p[2]) + scale(noise * 3 / 2, p[2]);
        if (c->fb > 1000) { y += fg_mul30(nz, c->fb) / 2; c->fb = fg_mul30(c->fb, fg_decay(3, 0, 127)); }
        out[i] += level(c, drive(y, p[6]));
        if (c->amp < 2000 && c->env3 < 2000) { c->on = 0; break; }
    }
}
static void render_caja808(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t q = 2400 - p[2] * 2200 / 127;                                       /* CUERPO: menos amortiguado, suena más */
    int32_t g[5];
    for (int k = 0; k < 5; k++) { int32_t o = (int32_t)p[1] * 3 * 256 / 127 - (k - 2) * 256; g[k] = k < 2 ? 256 : o < 0 ? 0 : o > 256 ? 256 : o; }
    int32_t top = amp_top(c);
    for (unsigned i = 0; i < n; i++) {
        int32_t x = 0;
        if (c->hcnt > 0) { c->hcnt--; x = (int32_t)((int64_t)12000 * top >> 30); }
        int32_t body = 0;
        for (int k = 0; k < 5; k++) { fg_svf2(&c->f[k], x, (int32_t)c->inc[k], q); body += (c->f[k].bp * g[k]) >> 8; }
        body = body / 3;
        fg_svf2(&c->f[5], fg_noise(&c->rng) >> 1, c->y[0], 12000);
        int32_t nz = c->f[5].bp; nz = nz > 0 ? nz : -nz / 2;                    /* ruido rectificado, como el 808 */
        int32_t noise = fg_mul30(nz, c->env3); c->env3 = fg_mul30(c->env3, c->k3);
        int32_t y = scale(body, 127u - p[3] / 2) + scale(noise * 2, p[3]);
        out[i] += level(c, drive(y, p[6]));
        c->t++;
        if (c->t > (uint32_t)ms(30) && c->env3 < 2000) {
            int32_t e = c->f[0].bp < 0 ? -c->f[0].bp : c->f[0].bp;
            if (e < 30) { c->on = 0; break; }
        }
    }
}
static void render_palmas(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t q = 20000 - p[5] * 130, room_f = (int32_t)svf_hz(1100), top = amp_top(c);
    uint32_t last = (uint32_t)((c->y[0] - 1) * c->y[1]);
    for (unsigned i = 0; i < n; i++) {
        if (c->t < last) { if (c->t % (uint32_t)c->y[1] == 0) c->amp = top; c->amp = fg_mul30(c->amp, c->k2); }
        else { if (c->t == last) c->amp = top; c->amp = fg_mul30(c->amp, c->kd); }
        int32_t nz = fg_noise(&c->rng) >> 1;
        fg_svf2(&c->f[0], nz, (int32_t)c->inc[0], q);
        int32_t y = fg_mul30(c->f[0].bp * 2, c->amp);
        fg_svf2(&c->f[1], nz, room_f, 30000);
        y += fg_mul30(c->f[1].lo, c->env3); c->env3 = fg_mul30(c->env3, c->k3);
        out[i] += level(c, drive(y, p[6]));
        c->t++;
        if (c->t > last && c->amp < 2000 && c->env3 < 2000) { c->on = 0; break; }
    }
}
static void render_metalico(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t f = (int32_t)svf_hz(kexp(p[2], 3000, 15)), q = 30000 - p[4] * 200;
    for (unsigned i = 0; i < n; i++) {
        int cnt = 0;
        for (int k = 0; k < 6; k++) { c->ph[k] += c->inc[k]; cnt += (int)(c->ph[k] >> 31); }
        int32_t s = (2 * cnt - 6) * 2600;                                       /* la suma de los bits de signo */
        int32_t x = (s * (127 - p[3]) + (fg_noise(&c->rng) >> 1) * p[3]) / 127;
        fg_svf2(&c->f[0], x, f, q);
        int32_t y = c->f[0].bp; c->lp += (y - c->lp) >> 3; y -= c->lp;
        int32_t e = (fg_mul30(c->amp, FG_ONE30 / 20 * 9) + fg_mul30(c->env3, FG_ONE30 / 20 * 11));   /* dos tramos */
        c->amp = fg_mul30(c->amp, c->k2); c->env3 = fg_mul30(c->env3, c->kd);
        y = fg_mul30(y, e);
        if (c->env2 > 1000) { y += fg_mul30(fg_noise(&c->rng), c->env2) / 2; c->env2 = fg_mul30(c->env2, c->k3); }
        out[i] += level(c, drive(y * 2, p[6]));
        if (c->env3 < 2000 && c->amp < 2000) { c->on = 0; break; }
    }
}
static void render_cuatro(fg_cand_t *c, int32_t *out, unsigned n)
{
    static const uint16_t R[8] = {128, 256, 384, 512, 768, 1024, 1280, 1792};
    const uint8_t *p = c->p;
    uint32_t base = fg_note_inc(c->note_q8), rb = R[p[2] * 8 / 128];
    uint32_t ia = base, ib = (uint32_t)(((uint64_t)base * rb) >> 8), ic = base + (base >> 9), id = ib;
    int alg = p[1] * 3 / 128;
    int64_t I = (int32_t)p[3] * 3 * 131072 / 127;
    for (unsigned i = 0; i < n; i++) {
        c->env2 = fg_mul30(c->env2, c->k2);
        int64_t ix = (I * (FG_ONE30 / 4 + fg_mul30(c->env2, FG_ONE30 / 4 * 3))) >> 30;
        c->ph[3] += id;
        int32_t d = fg_sin(c->ph[3] + (uint32_t)((int64_t)c->y[1] * p[5] * 1024));
        c->y[1] = (c->y[1] + d) >> 1;
        int32_t y;
        if (alg == 0) {
            c->ph[1] += ib; c->ph[2] += ic; c->ph[0] += ia;
            int32_t b = fg_sin(c->ph[1]);
            y = (fg_sin(c->ph[0] + (uint32_t)(b * ix)) + fg_sin(c->ph[2] + (uint32_t)(d * ix))) >> 1;
        } else if (alg == 1) {
            c->ph[2] += ic * 2u; c->ph[1] += ib; c->ph[0] += ia;
            int32_t cc = fg_sin(c->ph[2] + (uint32_t)(d * (ix >> 1)));
            int32_t b = fg_sin(c->ph[1] + (uint32_t)(cc * (ix >> 1)));
            y = fg_sin(c->ph[0] + (uint32_t)(b * ix));
        } else {
            c->ph[2] += ic * 3u; c->ph[1] += ib; c->ph[0] += ia;
            int32_t m = (fg_sin(c->ph[1]) + fg_sin(c->ph[2]) + d) / 3;
            y = fg_sin(c->ph[0] + (uint32_t)(m * ix));
        }
        int32_t a = env_step(c);
        out[i] += level(c, fg_mul30(y, a) * 3 / 4);
    }
}
#ifndef FG_DEVICE
static void render_lyra(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t gain = 256 + p[1] * 15 * 256 / 127;                                 /* FILO: triángulo a cuadrada */
    static const uint8_t MUL[6] = {1, 2, 3, 4, 6, 8};                           /* TIEMPO: el delay dura 1 a 8 vueltas de la nota */
    int32_t per = (int32_t)(0xFFFFFFFFu / (c->inc[0] ? c->inc[0] : 1u));
    int32_t T = per * MUL[p[5] * 6 / 128]; while (T > FGC_DL - 96) T >>= 1; if (T < 16) T = 16;
    int32_t T2 = T * 3 / 4;
    int32_t fbk = p[4] * 27000 / 127;
    for (unsigned i = 0; i < n; i++) {
        int32_t sum = 0;
        for (int k = 0; k < 4; k++) {
            int32_t m = c->y[(k + 3) & 3] * (int32_t)p[2] * 4 + c->fb * (int32_t)p[3] * 4;   /* el anillo y la salida */
            c->ph[k] += c->inc[k];
            uint32_t ph = c->ph[k] + (uint32_t)((int64_t)m * 128);
            int32_t saw = (int32_t)(ph >> 16) - 32768, tri = (saw < 0 ? -saw : saw) * 2 - 32767;
            int32_t w = (tri * gain) >> 8; w = w > 32767 ? 32767 : w < -32767 ? -32767 : w;
            c->y[k] = w; sum += w;
        }
        sum >>= 2;
        int32_t tm = T + (c->fb >> 11);                                          /* el delay se mueve con lo que sale */
        tm = tm < 8 ? 8 : tm > FGC_DL - 4 ? FGC_DL - 4 : tm;
        int32_t a = c->dl[0][(c->dw - (uint32_t)tm) & (FGC_DL - 1u)], b = c->dl[1][(c->dw - (uint32_t)T2) & (FGC_DL - 1u)];
        int32_t wa = sum / 2 + ((b * fbk) >> 15), wb = sum / 2 + ((a * fbk) >> 15);
        c->dl[0][c->dw & (FGC_DL - 1u)] = (int16_t)(wa > 32767 ? 32767 : wa < -32767 ? -32767 : wa);
        c->dl[1][c->dw & (FGC_DL - 1u)] = (int16_t)(wb > 32767 ? 32767 : wb < -32767 ? -32767 : wb);
        c->dw++;
        int32_t y = drive(sum + scale((a + b) / 2, p[4]), p[6]) / 2;
        c->fb = y;
        c->lp += (y - c->lp) >> 2; y = c->lp;                                   /* un pasabajos suave: sin él raspa */
        c->lp2 += (y - c->lp2) >> 10; y -= c->lp2;                              /* y sin DC (la realimentación la sube) */
        int32_t e = env_step(c);
        out[i] += level(c, fg_mul30(y, e));
    }
}
#endif
static void render_pliegue(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t g = 4096 + p[1] * 7 * 4096 / 127, off = (p[2] - 64) * 256;
    int32_t top = amp_top(c);
    for (unsigned i = 0; i < n; i++) {
        int32_t e = c->env2;
        if (!(p[5] == 127 && c->gated && c->t < c->gate)) c->env2 = fg_mul30(c->env2, c->k2);
        if (c->t >= c->gate) c->gated = 0;
        if ((c->t & 7u) == 0) {
            int32_t tq = top >> 14, open = tq ? (e >> 14) * 256 / tq : 0;          /* 0..256 */
            int32_t closed = 12000 - (int32_t)((11920LL * (256 - open) * p[6]) >> 15);   /* COMPUERTA: cuánto cierra el filtro */
            c->fb = fg_svf2_f((uint32_t)(closed < 80 ? 80 : closed));
        }
        c->ph[1] += c->inc[1];
        int32_t mod = fg_sin(c->ph[1]);
        int64_t depth = (int32_t)p[3] * 131072 / 127;
        c->ph[0] += c->inc[0];
        int32_t s = fg_sin(c->ph[0] + (uint32_t)(mod * (depth * (FG_ONE30 / 10 * 3 + fg_mul30(e, FG_ONE30 / 10 * 7)) >> 30)));
        int32_t x = (int32_t)(((int64_t)s * g) >> 12) + off;
        int32_t y = fg_fold(x);
        fg_svf2(&c->f[0], y, c->fb, 60000);
        int32_t v = fg_mul30(c->f[0].lo + (y >> 4), e);
        out[i] += level(c, fg_soft(v * 3 / 2));
        c->t++;
        if (e < 2000 && c->t > 64) { c->on = 0; break; }
    }
}
static void render_tabla(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    uint32_t inc = fg_note_inc(c->note_q8), w = (uint32_t)c->y[0];
    uint32_t k1 = 0x80000000u / ((w >> 16) | 1u), k2 = 0x80000000u / (((0u - w) >> 16) | 1u);   /* 2^47 / w, sin dividir en 64 bits */
    int32_t q = 40000 - p[5] * 290;
    for (unsigned i = 0; i < n; i++) {
        if ((c->t & 7u) == 0) {
            c->lph += fg_inc(77) * 8u;
            int32_t pos = p[1] * 15 * 256 / 127 + ((fg_sin(c->lph) * (int32_t)p[3]) >> 12);   /* MOVIMIENTO: hasta 4 ondas */
            c->hold = pos < 0 ? 0 : pos > 15 * 256 - 1 ? 15 * 256 - 1 : pos;
            uint32_t oct = (uint32_t)p[4] * 75u * 65536u / (127u * 10u) + (uint32_t)(((int64_t)c->env2 * 2) >> 14);
            uint32_t fc = (uint32_t)(((uint64_t)60u * fg_exp2_q16(oct)) >> 16);
            c->fb = fg_svf2_f(fc > 12000 ? 12000 : fc);
        }
        c->ph[0] += inc;
        uint32_t x = c->ph[0];
        x = x < w ? (uint32_t)(((uint64_t)x * k1) >> 16) : 0x80000000u + (uint32_t)(((uint64_t)(x - w) * k2) >> 16);   /* TORCER */
        int k = c->hold >> 8, fr = c->hold & 255;
        int32_t s = (wt_read(k, x) * (256 - fr) + wt_read(k + 1, x) * fr) >> 8;
        fg_svf2(&c->f[0], s, c->fb, q);
        c->env2 = fg_mul30(c->env2, c->k2);
        int32_t a = env_step(c);
        out[i] += level(c, fg_mul30(c->f[0].lo, a));
    }
}
static void render_oxido(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    uint32_t d = c->inc[0] / 127u * p[2];
    int32_t q = 40000 - p[5] * 300;
    for (unsigned i = 0; i < n; i++) {
        if ((c->t & 7u) == 0) {                                                  /* FILTRO sigue a la nota, y abre con el golpe */
            uint32_t oct = (uint32_t)p[4] * 6u * 65536u / 127u + (uint32_t)(((int64_t)c->env2 * 2) >> 14);
            uint32_t hz = (uint32_t)(((uint64_t)c->inc[0] * FG_SR) >> 32);
            uint32_t fc = (uint32_t)(((uint64_t)(hz < 20 ? 20 : hz) * fg_exp2_q16(oct)) >> 16);
            c->fb = fg_svf2_f(fc > 12000 ? 12000 : fc);
        }
        c->ph[1] += c->inc[1];
        int s2 = (int)(c->ph[1] >> 31);
        c->ph[0] += s2 ? c->inc[0] + d : c->inc[0] - (d >> 1);                 /* CRUCE: la segunda empuja a la primera */
        int s1 = (int)(c->ph[0] >> 31);
        int32_t sq = s1 ? 14000 : -14000, ring = (s1 ^ s2) ? 14000 : -14000;
        int32_t x = (sq * (127 - p[3]) + ring * p[3]) / 127;                    /* ANILLO: de la cuadrada al anillo */
        fg_svf2(&c->f[0], x, c->fb, q);
        c->env2 = fg_mul30(c->env2, c->k2);
        int32_t a = env_step(c);
        out[i] += level(c, fg_soft(fg_mul30(c->f[0].lo * 2, a)));
    }
}

static void render_enjambre(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int nv = c->hold;
    int32_t mixf = p[4];
    for (unsigned i = 0; i < n; i++) {
        if ((c->t & 15u) == 0) {
            int32_t bend = (int32_t)(((int64_t)c->env2 >> 14) * 12 * 256 >> 16);          /* BAJADA: hasta una octava arriba que cae */
            uint32_t b = fg_note_inc(c->note_q8 + bend);
            for (int k = 0; k < nv; k++) {
                c->y[k] += (int32_t)fg_inc((uint32_t)(40 + k * 13)) * 16;                     /* DERIVA: cada voz con su ola lenta */
                int32_t pos = nv > 1 ? (k * 2 - (nv - 1)) * 65536 / (nv - 1) : 0;            /* -1 .. 1 (Q16) */
                int32_t cents = (pos >> 8) * p[2] / 127 * 40 / 256                            /* hasta ±40 cents */
                              + ((fg_sin((uint32_t)c->y[k]) * (int32_t)p[3]) >> 15) / 12;
                c->inc[k] = (uint32_t)(((uint64_t)b * fg_exp2_q16((uint32_t)(65536 * 8 + cents * 65536 / 1200))) >> 24);
            }
            c->env2 = fg_mul30(c->env2, fg_pow16(c->k2));
            uint32_t fc = kexp(p[6], 60, 60);
            c->fb = fg_svf2_f(fc > 12000 ? 12000 : fc);
            c->lph += fg_inc(3 * 256) * 16u;
        }
        int32_t sum = 0;
        uint32_t pw = 0x80000000u + (uint32_t)(fg_sin(c->lph) * 40000);
        for (int k = 0; k < nv; k++) {
            c->ph[k] += c->inc[k];
            int32_t saw = (int32_t)(c->ph[k] >> 16) - 32768, pul = c->ph[k] < pw ? 26000 : -26000;
            sum += (saw * (127 - mixf) + pul * mixf) / 127;
        }
        sum = sum * 3 / (nv + 2);
        fg_svf2(&c->f[0], sum >> 1, c->fb, 26000);
        int32_t a = env_step(c);
        out[i] += level(c, fg_soft(fg_mul30(c->f[0].lo * 2, a)));
    }
}
static void render_fase(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int type = p[1] * 3 / 128;                                                  /* sierra, cuadrada, resonante */
    for (unsigned i = 0; i < n; i++) {
        int32_t dcw = p[2] * 258 + (c->env2 >> 15) * (int32_t)p[3] / 127;   /* Q15 */
        if (dcw > 32700) dcw = 32700;
        c->env2 = fg_mul30(c->env2, c->k2);
        c->ph[0] += c->inc[0];
        uint32_t x = c->ph[0], xp;
        int32_t y;
        if (type == 0) {                                                        /* sierra: el quiebre se corre */
            uint32_t d = 0x80000000u - (uint32_t)(((uint64_t)0x7D70A3D7u * (uint32_t)dcw) >> 15);
            uint32_t r1 = 0x80000000u / ((d >> 16) | 1u), r2 = 0x80000000u / (((0u - d) >> 16) | 1u);
            xp = x < d ? (uint32_t)(((uint64_t)x * r1) >> 16) : 0x80000000u + (uint32_t)(((uint64_t)(x - d) * r2) >> 16);
            y = -fg_sin(xp + 0x40000000u);
        } else if (type == 1) {                                                 /* cuadrada: sube rápido y se queda */
            uint32_t h = x & 0x7FFFFFFFu, lim = 0x7FFFFFFFu - (uint32_t)(((uint64_t)0x7C000000u * (uint32_t)dcw) >> 15);
            uint32_t q = h >= lim ? 0x40000000u : (uint32_t)(((uint64_t)h * (0x40000000u / ((lim >> 16) | 1u))) >> 16);
            xp = (x & 0x80000000u) + q;
            y = -fg_sin(xp + 0x40000000u);
        } else {                                                                /* resonante: un seno más agudo bajo una rampa */
            uint32_t r = 256 + (uint32_t)dcw * 14 / 128;                        /* 1 .. 15 veces (Q8) */
            int32_t win = 32767 - (int32_t)(x >> 17);
            y = (int32_t)(((int64_t)fg_sin((uint32_t)(((uint64_t)x * r) >> 8)) * win) >> 15);
        }
        y /= 2;
        if (x < c->inc[0]) c->hcnt ^= 1;                                         /* SUB: una cuadrada una octava abajo */
        if (p[5]) y += ((c->hcnt ? 9000 : -9000) * (int32_t)p[5]) / 127;
        int32_t a = env_step(c);
        out[i] += level(c, drive(fg_mul30(y, a), p[6]));
    }
}
static void render_sync(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t q = 36000 - p[6] * 260;
    for (unsigned i = 0; i < n; i++) {
        if ((c->t & 7u) == 0) {
            int32_t r = 256 + p[1] * 7 * 256 / 127 + (c->env2 >> 14) * (int32_t)p[2] * 7 / 127 * 256 / 65536;   /* 1 .. 15 veces */
            c->inc[1] = (uint32_t)(((uint64_t)c->inc[0] * (uint32_t)r) >> 8);
            uint32_t fc = kexp(p[5], 80, 56);
            c->fb = fg_svf2_f(fc > 12000 ? 12000 : fc);
        }
        c->env2 = fg_mul30(c->env2, c->k2);
        uint32_t old = c->ph[0];
        int32_t back = (int32_t)(c->ph[1] >> 16) - 32768;                       /* CAOS: el esclavo empuja al maestro */
        c->ph[0] += c->inc[0] + (uint32_t)(((int64_t)c->inc[0] * back * p[4]) >> 22);
        if (c->ph[0] < old) c->ph[1] = 0;                                       /* el maestro da la vuelta: el esclavo empieza otra vez */
        else c->ph[1] += c->inc[1];
        int32_t saw = (int32_t)(c->ph[1] >> 16) - 32768, ring = ((c->ph[1] ^ c->ph[0]) >> 31) ? 22000 : -22000;
        int32_t x = p[4] > 64 ? (saw * (127 - p[4]) + ring * (p[4] - 64)) / 63 : saw;   /* arriba de 64 entra el anillo */
        fg_svf2(&c->f[0], x >> 1, c->fb, q);
        int32_t a = env_step(c);
        out[i] += level(c, fg_soft(fg_mul30(c->f[0].lo * 2, a)));
    }
}
static void render_organo(fg_cand_t *c, int32_t *out, unsigned n)
{
    const uint8_t *p = c->p;
    int32_t w[6] = {p[1], p[1] * 3 / 2, (p[1] + p[2]) / 2, p[2] * 3 / 4, p[2], p[2] / 2};   /* GRAVES y AGUDOS */
    uint32_t spin = fg_inc((uint32_t)(256 + p[6] * p[6] * 40 * 256 / (127 * 127)));     /* GIRO: de 1 Hz (Leslie) a 40 Hz (anillo) */
    int32_t depth = p[6] * 100;
    for (unsigned i = 0; i < n; i++) {
        c->lph += spin;
        int32_t s = 0;
        for (int k = 0; k < 6; k++) { c->ph[k] += c->inc[k]; s += (fg_sin(c->ph[k]) * w[k]) >> 8; }
        if (c->env3 > 1000) { s += fg_mul30(fg_sin(c->ph[3]), c->env3) / 2; c->env3 = fg_mul30(c->env3, c->k3); }   /* PERCUSIÓN */
        int32_t am = 32768 - ((fg_sin(c->lph) + 32767) * depth >> 16);
        s = (s * am) >> 15;
        int32_t a = env_step(c);
        out[i] += level(c, fg_soft(fg_mul30(s, a)));
    }
}

void fg_cand_render(fg_cand_t *c, int32_t *out, unsigned n)
{
    if (!c->on) { c->peak = 0; return; }
    int32_t tmp[FG_BLOCK];
    if (n > FG_BLOCK) n = FG_BLOCK;
    fg_zero(tmp, n * sizeof *tmp);
    switch (c->model) {
    case FGC_IMPACTO: render_impacto(c, tmp, n); break;
    case FGC_CAJA909: render_caja909(c, tmp, n); break;
    case FGC_PARCHE: render_caja808(c, tmp, n); break;
    case FGC_PALMAS: render_palmas(c, tmp, n); break;
    case FGC_METALICO: render_metalico(c, tmp, n); break;
    case FGC_CUATRO: render_cuatro(c, tmp, n); break;
#ifndef FG_DEVICE
    case FGC_LYRA: render_lyra(c, tmp, n); break;
#endif
    case FGC_PLIEGUE: render_pliegue(c, tmp, n); break;
    case FGC_TABLA: render_tabla(c, tmp, n); break;
    case FGC_OXIDO: render_oxido(c, tmp, n); break;
    case FGC_ENJAMBRE: render_enjambre(c, tmp, n); break;
    case FGC_FASE: render_fase(c, tmp, n); break;
    case FGC_SYNC: render_sync(c, tmp, n); break;
    default: render_organo(c, tmp, n);
    }
    int32_t pk = 0;
    for (unsigned i = 0; i < n; i++) { int32_t v = tmp[i]; out[i] += v; if (v < 0) v = -v; if (v > pk) pk = v; }
    c->peak = pk;
}
