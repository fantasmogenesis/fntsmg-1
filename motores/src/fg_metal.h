/* Carril 3 · METAL: dos modelos.
 * GRANIZO: DENSIDAD CAÍDA COLOR RESO | AGITA GRANO PEGADA NIVEL
 *   granos de ruido sueltos por un filtro resonante: de chisporroteo a siseo, de hat cerrado a shaker.
 * PLACA:   TONO CAÍDA FORMA BRILLO | GOLPE BATIDO PEGADA NIVEL
 *   seis modos inarmónicos (cada uno con un gemelo desafinado): FORMA va de gong a campana a platillo. */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"

enum { FGM_GRANIZO, FGM_PLACA, FGM_MODELS };
#define FGM_NP 8
#define FGM_MODES 6

typedef struct {
    uint8_t on;
    int32_t amp, ka, kill, att, att_inc;
    uint32_t thresh;                    /* probabilidad de grano por muestra (de 2^32) */
    int32_t glen, gpos, gamp, ginv;     /* grano en curso: largo, posición, amplitud, 1/largo (Q16) */
    uint32_t gph, ginc, cinc;           /* GRANO alto: cada grano es una gota con tono cerca del COLOR */
    int32_t tone;
    int32_t lo, bp, sf, sq, norm, hpz;
    int32_t hold, drive, lvl;
    uint32_t seed;
} fgm_hail_t;

typedef struct {
    uint8_t on;
    uint32_t ph[FGM_MODES * 2], inc[FGM_MODES * 2];
    int32_t amp[FGM_MODES], k[FGM_MODES], g[FGM_MODES], twin, norm, kill;
    int32_t att, att_inc, click, kc, clvl, chp;
    int32_t hold, drive, lvl;
    uint32_t seed;
} fgm_plate_t;

typedef struct {
    uint8_t model, cur;
    uint8_t p[FGM_MODELS][FGM_NP];
    fgm_hail_t h[2];
    fgm_plate_t pl[2];
    int32_t peak;
} fg_metal_t;

void fg_metal_init(fg_metal_t *m);
void fg_metal_param(fg_metal_t *m, unsigned model, unsigned i, unsigned v);
void fg_metal_model(fg_metal_t *m, unsigned model);
void fg_metal_trig(fg_metal_t *m, unsigned vel, const uint8_t *ov, unsigned mask);
void fg_metal_render(fg_metal_t *m, int32_t *out, unsigned n);
