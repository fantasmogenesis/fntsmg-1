/* Pistas 5..8 · SINTES: ocho motores, cada pista elige uno con ALGORITHM.
 * SH:        NOTA ONDA SUB CORTE | RESO ENV FORMA NIVEL
 *   el SH-101 / S-1: sierra -> cuadrada -> pulso -> PWM, sub −1/−2 oct, filtro 4 polos y una ADSR (FORMA: pluck,
 *   bajo, lead, pad). Bajos de electro, leads, bleeps, stabs.
 * ÁCIDO:     NOTA CORTE RESO ENV | CAÍDA RUGOSIDAD GLIDE NIVEL
 *   sierra o cuadrada (o sync áspero) por un filtro escalera de 24 dB que satura por dentro; acento y glide.
 * FM:        NOTA RAZÓN ÍNDICE FEEDBACK | GOLPE CAÍDA RUGIDO NIVEL
 *   FM de techno: realimentación hasta el caos, salto de altura e índice al golpe, plegado y saturación.
 * CUERDAS:   NOTA DESAFINA BRILLO CORO | ATAQUE SALIDA SUB NIVEL
 *   pad de tres sierras desafinadas y un sub, paso bajo suave y coro: llena la mezcla.
 * Los racks (un motor con sus efectos; las 8 perillas son sus macros):
 * TEXTURA:   NOTA MUESTRA POSICIÓN MOVIMIENTO | BRILLO LAVADO SALIDA NIVEL
 *   texturas hechas afuera y guardadas en la flash (fg_samples), afinadas, onduladas, filtradas, a la reverb.
 * DRON:      NOTA LATIDO FORMA MOVIMIENTO | BRILLO ENSAMBLE SATURA NIVEL
 *   osciladores que laten y derivan despacio, coro y saturación; entra y se va lento.
 * DUB:       NOTA TIPO CORTE CAÍDA | ECO TIEMPO SUCIO NIVEL
 *   stab de acorde con su eco de cinta (uno por máquina: el primer sinte en DUB; los demás, sin eco propio).
 * RESONADOR: NOTA MATERIAL CAÍDA ARCO | BRILLO SIMPATÍA MOVIMIENTO NIVEL
 *   seis modos afinados (cuerda, barra, campana, vidrio) excitados por golpe y arco; simpatía que los sostiene.
 * Parafonía: en CUERDAS, TEXTURA y DRON las teclas sostenidas entran a la misma voz (hasta 4 notas por un filtro
 * y una envolvente): suena a acorde grande y cuesta casi lo de una voz.
 * Polifonía: hasta FGY_VOICES notas por pista (el secuenciador y el teclado se enciman; la nota del teclado o del
 * MIDI suena mientras se sostiene). ÁCIDO (glide) sigue en mono con dos ranuras. */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"
#include "fg_samples.h"

enum { FGY_SH, FGY_ACIDO, FGY_FM, FGY_CUERDAS, FGY_TEXTURA, FGY_DRON, FGY_DUB, FGY_RESONADOR, FGY_MODELS };
#define FGY_GRAINS 8
#define FGY_VOICES 4
#define FGY_CHO 256                          /* el coro de CUERDAS y DRON: 5.8 ms de línea por pista */
#define FGY_HOLD 0x7FFFFFFFu                 /* compuerta de una nota sostenida (hasta que se suelte) */
#define FGY_NP 8

typedef struct { uint8_t unused; } fg_synth_shared_t;      /* (antes la foto y el sample: ya no hay) */

typedef struct {
    uint8_t on, model, cnt, accent;
    uint8_t p[FGY_NP];
    uint16_t locked;                     /* parámetros lockeados en este paso: las perillas no los mueven */
    int32_t note_q8, target_q8, gstep;   /* altura (con glide) */
    uint32_t gate;                       /* muestras hasta soltar la nota */
    int32_t amp, att, att_inc, kd, kr, rel;
    int32_t env, ke;                     /* envolvente de modulación (filtro, compuerta, dcw, tensión) Q30 */
    int32_t tens;                        /* SH: el nivel de sostén de su ADSR (Q30) */
    uint32_t ph[3], inc[3], sph;
    int32_t y[4], dc;                    /* filtro escalera */
    fg_svf_t sv;
    int32_t hold;
    uint32_t gpos[FGY_GRAINS], gwinc[FGY_GRAINS], subph;   /* acorde: fases e incrementos de sus 8 osciladores; DRON: su sub */
    int32_t fbz;
    uint8_t key;                         /* la tecla que la sostiene (255: ninguna); para soltarla */
    uint8_t nn, keys[4];                 /* parafonía (pads): las notas sostenidas que suenan en esta voz */
    int32_t nq[4];
    uint32_t age, rng;
    union {                              /* (una voz toca un solo modelo: comparten lugar) */
        fg_smpv_t hd[3];                 /* TEXTURA: una cabeza por nota */
        struct { int32_t rz[6][2], rc[6][2]; };   /* RESONADOR: estado y coeficientes de sus modos */
    };
} fgy_voice_t;
#define FGY_ECO 8192                         /* el eco de DUB: 0.74 s a 11 kHz, 8 bits (sucio a propósito) */
typedef struct { int8_t b[FGY_ECO]; uint32_t w, acc, ph; int32_t lp, sum, last, prev; uint8_t sub; } fgy_eco_t;

typedef struct {
    uint8_t model, cur;
    uint8_t p[FGY_MODELS][FGY_NP];
    fgy_voice_t v[FGY_VOICES];
    int16_t cho[FGY_CHO];                /* coro / vibrato de la pista */
    uint32_t chw, chph;
    const fg_synth_shared_t *sh;
    int32_t last_q8;
    int32_t peak;
    uint8_t maxv;                        /* voces permitidas ahora (la plataforma baja a 2 si el núcleo no da) */
    uint32_t age;
    uint32_t step;                       /* muestras por dieciseisavo (el eco de DUB va a tempo) */
    fgy_eco_t *eco;                      /* el eco de DUB, si esta pista lo tiene (lo da la plataforma) */
    int32_t xrev;                        /* TEXTURA: cuánto pide de reverb su LAVADO (Q15; la mezcla lo suma al envío) */
} fg_synth_t;

void fg_synth_init(fg_synth_t *s, const fg_synth_shared_t *sh, unsigned model);
void fg_synth_param(fg_synth_t *s, unsigned model, unsigned i, unsigned v);
void fg_synth_model(fg_synth_t *s, unsigned model);
/* key: la tecla que la sostiene (gate = FGY_HOLD hasta fg_synth_release), 255 = nota del secuenciador */
void fg_synth_trig(fg_synth_t *s, unsigned vel, const uint8_t *ov, unsigned mask, uint32_t gate, unsigned key);
void fg_synth_release(fg_synth_t *s, unsigned key);
/* que no suenen más de n voces: las más viejas que sobren se apagan rápido (no esperan su SALIDA) */
void fg_synth_limit(fg_synth_t *s, unsigned n);
unsigned fg_synth_voices(const fg_synth_t *s);
void fg_synth_render(fg_synth_t *s, int32_t *out, unsigned n);
