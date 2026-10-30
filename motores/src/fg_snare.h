/* Carril 2 · CAJA: tres modelos.
 * CAJA:     TONO CUERPO RUIDO CAÍDA | BRILLO CHASQUIDO PEGADA NIVEL
 *   la de las máquinas (909/808): dos tonos del parche que caen de afinación, ruido brillante con su caída, un
 *   chasquido al principio y PEGADA (cuerpo sostenido + saturación). CHASQUIDO arriba de 90 suma el aro (rimshot).
 * APLAUSO:  TONO PALMAS SEPARA CAÍDA | SALA COLOR PEGADA NIVEL
 *   varias ráfagas de ruido muy cortas y desparejas (las manos que no caen juntas) por un filtro de banda;
 *   la última deja la cola, y SALA agrega una cola oscura más larga, como el cuarto.
 * SAMPLE:   AFINA SAMPLE INICIO CAÍDA | CORTE CRUJIDO MODO NIVEL  (el de las 8 pistas: fg_samp.h) */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"
#include "fg_samp.h"

enum { FGS_CAJA, FGS_APLAUSO, FGS_SAMPLE, FGS_MODELS };
#define FGS_NP 8

typedef struct {
    uint8_t on, cnt;
    uint32_t ph[2], inc0[2], inc[2], rph[2], rinc[2];
    int32_t amp[2], k[2], g, kill;
    int32_t tenv, kt16, depth;              /* el parche cae de afinación al golpe */
    int32_t snap, ksnap, slvl, wire, kw, wlvl;   /* ruido: chasquido inicial + cola */
    int32_t hpz, lpz, chp, clp;             /* ruido: paso bajo y paso alto (BRILLO) */
    int32_t click, kc, clvl;                /* el aro (CHASQUIDO arriba de 90) */
    int32_t hold, drive;
    uint32_t seed;
} fgs_voice_t;

typedef struct {
    uint8_t on, left;
    uint32_t t, next, gap;               /* ráfagas: cuándo llega la siguiente y cuántas faltan */
    int32_t env, kb, tail, kt, room, kr, a;
    int32_t lo, bp, lo2, bp2, sf, sq, gain, rlp, rlvl;
    int32_t hold, drive, kill;
    uint32_t seed;
} fgs_clap_t;


typedef struct {
    uint8_t model, cur;
    uint8_t p[FGS_MODELS][FGS_NP];
    fgs_voice_t v[2];
    fgs_clap_t c[2];
    fg_sampv_t sm[2];
    int32_t peak;
} fg_snare_t;

void fg_snare_init(fg_snare_t *s);
void fg_snare_param(fg_snare_t *s, unsigned model, unsigned i, unsigned v);
void fg_snare_model(fg_snare_t *s, unsigned model);
unsigned fg_snare_ahead(const fg_snare_t *s);           /* ya no hay anticipación: siempre 0 (se queda por la API) */
void fg_snare_pre(fg_snare_t *s, uint32_t samples);
void fg_snare_trig(fg_snare_t *s, unsigned vel, const uint8_t *ov, unsigned mask);
void fg_snare_render(fg_snare_t *s, int32_t *out, unsigned n);
