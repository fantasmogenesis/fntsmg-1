/* Mezcladora y efectos: 8 pistas mono -> volumen, paneo, envíos a delay y reverb; bus maestro con
 * filtro DJ, drive, compresor y limitador; performance FX que se pueden sostener varios a la vez. */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"

#define FG_TRACKS 8
#ifndef FG_DLY_HALF
#define FG_DLY_HALF 1                     /* 1: el delay corre a 22 kHz: el doble de tiempo por la misma memoria, ecos sin >11 kHz */
#endif
#ifndef FG_DLEN
#define FG_DLEN 16384u                    /* delay por lado (potencia de 2): 0.74 s a 22 kHz (FG_DLY_HALF) */
#endif

enum { FGX_VOL, FGX_PAN, FGX_DLY, FGX_REV, FGX_FILT, FGX_WIDTH, FGX_SAT, FGX_DUCK, FGX_STRIP };   /* por pista, 0..127 */
#define FGX_WD 256                        /* ANCHO: un retardo corto de la pista a un lado (hasta 5.8 ms) */
enum { FGF_DTIME, FGF_DFB, FGF_DTONE, FGF_RSIZE, FGF_RTONE, FGF_FILT, FGF_DRIVE, FGF_COMP, FGF_N };   /* maestro */
enum {                                    /* performance FX (bits): varios a la vez */
    FGP_LP = 1u << 0, FGP_HP = 1u << 1, FGP_BITS = 1u << 2, FGP_GATE = 1u << 3,
    FGP_THROW = 1u << 4, FGP_FREEZE = 1u << 5, FGP_RISER = 1u << 6,
    /* de cinta: leen de lo que acaba de sonar */
    FGP_STUT16 = 1u << 7, FGP_STUT32 = 1u << 8, FGP_REV = 1u << 9, FGP_HALF = 1u << 10, FGP_BRAKE = 1u << 11,
};
#define FGP_TAPE (FGP_STUT16 | FGP_STUT32 | FGP_REV | FGP_HALF | FGP_BRAKE)
#ifndef FG_CAP
#define FG_CAP 16384u                     /* lo que acaba de sonar: 0.37 s (potencia de 2) */
#endif

typedef struct {
    uint8_t v[FGX_STRIP], mute, solo;
    fg_svf_t sv;                          /* FILTRO de la pista (64: abierto); su corte y amortiguación */
    int32_t ff, fq;
    int32_t xrev;                         /* envío extra a la reverb que pide el motor (TEXTURA: LAVADO), Q15 */
    int16_t wd[FGX_WD];
    uint8_t ww;
    int32_t gain;                         /* ganancia actual Q15 (sin saltos) */
    int32_t mgain;                        /* silencio / solo, Q15 sin saltos: se aplica a la pista misma (así sale callada por USB) */
    int32_t peak;                         /* pico posfader del último bloque: para los visuales */
} fg_strip_t;

#ifndef FG_REV_HALF
#define FG_REV_HALF 1                     /* 1: la reverb corre a 22 kHz: la mitad de CPU y de memoria, cola sin >11 kHz */
#endif
#define FG_REV_DIV (FG_REV_HALF ? 2 : 1)

typedef struct {
    int16_t buf[1557 / FG_REV_DIV];
    uint16_t len, pos;
    int32_t lp;
} fg_comb_t;
typedef struct { int16_t buf[579 / FG_REV_DIV]; uint16_t len, pos; } fg_ap_t;

typedef struct {
    fg_strip_t st[FG_TRACKS];
    uint8_t fx[FGF_N];
    uint32_t perf;
    uint8_t pk[12][2];                    /* perillas de cada efecto de performance (bit FGP_*): 64 = como siempre */
    uint32_t dlen, dpos, step;            /* delay: largo en muestras (lo fija el tempo) */
    int16_t dl[FG_DLEN], dr[FG_DLEN];
    int32_t dlpl, dlpr;
    int32_t dacc, dpl, dpr, dcl, dcr;     /* delay a media tasa: entrada acumulada, salida anterior y actual */
    uint8_t dph;
    fg_comb_t cl[4], cr[4];
    fg_ap_t al[2], ar[2];
    int32_t racc, rpl, rpr, rcl, rcr;     /* media tasa: entrada acumulada, salida anterior y actual */
    uint8_t rph;
    fg_svf_t fl, fr;                      /* filtro DJ */
    int32_t ff, fq, fmode, fsweep;        /* coeficiente, 1/Q, -1 pasabajos / 0 nada / 1 pasaaltos */
    int32_t comp_env, comp_gain, comp_thr;
    int32_t pump_env;                     /* envolvente del bombo: agacha a las demás */
    uint32_t gate_len, gate_pos;          /* GATE: medio dieciseisavo encendido, medio apagado */
    uint32_t crush_cnt; int32_t crush_l, crush_r;
    fg_svf_t rs; uint32_t riser_t; int32_t riser_amp, riser_f;
    uint32_t seed;
    int16_t capl[FG_CAP / 2u], capr[FG_CAP / 2u];   /* cinta: a 22 kHz (el promedio de cada par de muestras) */
    int32_t cpl, cpr;
    uint32_t capw, anchor, loop_len;
    uint64_t rpos;                        /* cabeza de lectura Q32.32 (relativa al ancla) */
    int32_t speed, xf;                    /* velocidad Q16, mezcla en vivo -> cinta Q15 */
    uint32_t tape_prev;
} fg_mix_t;

void fg_mix_init(fg_mix_t *m);
void fg_mix_strip(fg_mix_t *m, unsigned track, unsigned which, unsigned v);
void fg_mix_mute(fg_mix_t *m, unsigned track, unsigned mute, unsigned solo);
void fg_mix_fx(fg_mix_t *m, unsigned which, unsigned v);
void fg_mix_tempo(fg_mix_t *m, uint32_t step_samples);   /* muestras por dieciseisavo: fija delay y gate */
void fg_mix_perf(fg_mix_t *m, uint32_t flags);
/* in[t]: FG_BLOCK muestras mono por pista; sale estéreo en outl/outr */
void fg_mix_xrev(fg_mix_t *m, unsigned t, int32_t v);
/* las dos perillas de un efecto de performance (bit: el número de bit de FGP_*) */
void fg_mix_perfk(fg_mix_t *m, unsigned bit, unsigned i, unsigned v);   /* envío extra a la reverb que pide el motor de la pista */
void fg_mix_render(fg_mix_t *m, int32_t *const in[FG_TRACKS], int32_t *outl, int32_t *outr, unsigned n);
void fg_mix_tape(fg_mix_t *m, int32_t *outl, int32_t *outr, unsigned n);   /* después de render */
