/* Carril 4 · PERC: dos modelos.
 * GOTA: TONO GOTA RESONANCIA DUREZA | REBOTE MATERIAL PEGADA NIVEL
 *   un golpe afinado (dos modos) cuya altura cae (tambor) o sube (gota de agua); REBOTE suelta la pelota
 *   desde más alto (más rebotes y más espaciados); MATERIAL mueve el segundo modo de tom a madera a metal a vidrio.
 * CHATARRA: TONO CAOS PEINE CAÍDA | ROTURA BITS RUIDO NIVEL
 *   dos osciladores que se modulan la fase uno al otro (caos), por un peine resonante metálico, plegados,
 *   recortados y reducidos de bits: golpe industrial, rasposo y violento. */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"
#include "fg_samp.h"

enum { FGP_GOTA, FGP_CHATARRA, FGP_SAMPLE, FGP_MODELS };   /* SAMPLE: fg_samp.h */
#define FGP_NP 8

typedef struct {
    uint8_t on, cnt, bleft;
    uint32_t ph[2], inc0[2], inc[2];
    int32_t amp[2], k[2], g2, kill;
    int32_t penv, kp16, pdir;            /* altura: arranca arriba o abajo y regresa (Q30, signo) */
    int32_t click, kc, clvl, chp, att, att_inc;
    uint32_t bwait, bgap; int32_t bamp, bstep;   /* rebotes */
    int32_t hold, drive, lvl, lpa, lpz;
    uint32_t seed;
} fgp_drop_t;

typedef struct {
    uint8_t on, cnt;
    uint32_t pa, pb, ia, ib, ia0, ib0;   /* dos osciladores cruzados */
    int32_t sa, sb, caos, nz;            /* sus salidas, cuánto se modulan, ruido que se mete */
    int32_t amp, ka, kill, penv, kp;
    int16_t comb[520]; uint16_t clen, cpos; int32_t cfb, clp;
    int32_t fold, crush_mask; uint32_t crush_hold, crush_cnt; int32_t crush_v;
    int32_t lvl;
    uint32_t seed;
} fgp_voice_t;

typedef struct {
    uint8_t model, cur;
    uint8_t p[FGP_MODELS][FGP_NP];
    fgp_drop_t d[2];
    fgp_voice_t v[2];
    fg_sampv_t sm[2];
    int32_t peak;
} fg_perc_t;

void fg_perc_init(fg_perc_t *s);
void fg_perc_param(fg_perc_t *s, unsigned model, unsigned i, unsigned v);
void fg_perc_model(fg_perc_t *s, unsigned model);
void fg_perc_trig(fg_perc_t *s, unsigned vel, const uint8_t *ov, unsigned mask);
void fg_perc_render(fg_perc_t *s, int32_t *out, unsigned n);
