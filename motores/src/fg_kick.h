/* Carril 1 · BOMBO: tres modelos (barrido, membrana, muestra) y una saturación compartida que lo empuja al frente.
 * Punto fijo, sin float. Monofónico con dos ranuras: el golpe nuevo entra en una y el anterior
 * se apaga en ~3 ms en la otra, sin clic. */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"
#include "fg_samples.h"

enum { FGK_BARRIDO, FGK_MEMBRANA, FGK_MUESTRA, FGK_MODELS };
#define FGK_NP 8
/* BARRIDO:  TONO BARRIDO CAÍDA DOBLEZ | SATURA CLICK PEGADA NIVEL
 * MEMBRANA: TONO TENSIÓN CAÍDA MATERIAL | GOLPE SATURA PEGADA NIVEL
 * MUESTRA:  TONO MUESTRA CAÍDA FILTRO | SATURA ARRANQUE PEGADA NIVEL  (bombos sampleados: firmware/samples)
 * TONO: barrido 20..97 Hz, membrana 22..120 Hz. SATURA: drive y realce para que corte en la mezcla.
 * PEGADA: el cuerpo se sostiene unos ms antes de caer y el golpe entra a una saturación */

typedef struct {
    uint8_t on, cnt;
    uint32_t ph, inc, inc_end;
    int32_t amp, ka, kill;           /* envolvente de amplitud Q30 */
    int32_t penv, kp16, sweep;       /* envolvente de altura Q30, octavas de barrido Q16 */
    int32_t fold_g, lvl;             /* doblez Q12, nivel Q15 */
    int32_t hold, drive;             /* pegada: muestras sin caer, ganancia Q12 a la saturación */
    int32_t click, kc, clvl, chp;    /* CLICK: 0 = ataque suave de ~3 ms, 127 = chasquido fuerte */
    int32_t att, att_inc;
    uint32_t cph, cinc, seed;
} fgk_sweep_t;

typedef struct {
    uint8_t on, cnt;
    uint32_t ph[4], inc0[4], inc[4];
    int32_t amp[4], k[4], g[4], kill;
    int32_t tenv, kt16, tens;        /* tensión: la altura arranca arriba y cae (Q30 / Q28) */
    int32_t att, att_inc;            /* ataque del mazo, Q15 */
    int32_t click, kc, clvl, chp;    /* chasquido de ruido */
    int32_t lvl, norm;
    int32_t hold, drive;
    uint32_t seed;
} fgk_memb_t;


typedef struct {
    uint8_t on;
    fg_smpv_t r;
    uint32_t step;
    int32_t amp, ka, kill, lp, g, hold, drive;
} fgk_smp_t;

typedef struct {
    uint8_t model, cur;
    uint8_t p[FGK_MODELS][FGK_NP];
    fgk_sweep_t sw[2];
    fgk_memb_t mb[2];
    fgk_smp_t sm[2];
    int32_t sat_lp;                  /* SATURA: paso bajo para el realce */
    int32_t peak;                    /* pico del último bloque, para los visuales */
} fg_kick_t;

void fg_kick_init(fg_kick_t *k);
void fg_kick_param(fg_kick_t *k, unsigned model, unsigned i, unsigned v);
void fg_kick_model(fg_kick_t *k, unsigned model);
void fg_kick_trig(fg_kick_t *k, unsigned vel, const uint8_t *ov, unsigned mask);   /* ov/mask: locks del paso */
void fg_kick_render(fg_kick_t *k, int32_t *out, unsigned n);   /* suma en out, n <= FG_BLOCK */
