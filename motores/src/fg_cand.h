/* Motores candidatos (fase 2 de la 0.10), segunda vuelta: lo que Cris votó y el benchmark de motores abiertos
 * (revision/benchmark-motores.md). Se oyen en la página de motores junto a los de ahora; todavía no están en el
 * aparato. Punto fijo, sin float, monofónicos.
 * IMPACTO (bombo): seno con fase que arranca en 90°, barrido exponencial, cuerpo sostenido, ataque de ruido,
 *   saturación después del VCA y armónicos Chebyshev (a la DS Kick).    TONO BAJADA CURVA ATAQUE | CAÍDA SATURACIÓN ARMÓNICOS NIVEL
 * CAJA 909 (caja): dos senos distorsionados (1 y 1.47) que caen de afinación, ruido entre un pasabajos y un pasaaltos
 *   atados al tono, que se sostiene antes de caer (Plaits synthetic_snare_drum).   TONO CUERPO RUIDO CAÍDA | COLOR ATAQUE SATURACIÓN NIVEL
 * PARCHE (perc; nació como la caja 808 de Plaits): cinco modos (1, 2, 3.18, 4.16, 5.62) por un pulso; TIMBRE abre de
 *   2 a 5; ruido rectificado por un pasabanda. Toms y timbales análogos.   TONO TIMBRE CUERPO RUIDO | CAÍDA COLOR SATURACIÓN NIVEL
 * PALMAS (caja): ráfagas de ruido separadas por un pasabanda, la última con cola, y una cola oscura de sala.
 *   TONO PALMAS SEPARACIÓN CAÍDA | SALA COLOR SATURACIÓN NIVEL
 * METÁLICO (metal): seis cuadradas a las razones del hat 808 de Plaits, pasabanda, pasaaltos y envolvente de dos
 *   tramos (hat a platillo).                                   TONO CAÍDA COLOR RUIDO | RESONANCIA GOLPE SATURACIÓN NIVEL
 * CUATRO (sinte): FM de cuatro operadores con tres algoritmos (a la Digitone).
 *   NOTA ALGORITMO RAZÓN ÍNDICE | GOLPE FEEDBACK CAÍDA NIVEL
 * LYRA (sinte): cuatro voces en anillo que se modulan muestra a muestra, realimentación de la salida saturada y dos
 *   delays cruzados afinados a la nota (TIEMPO: 1 a 8 vueltas), a la Soma Lyra-8.                 NOTA FILO MODULACIÓN REALIMENTACIÓN | DELAY TIEMPO SATURACIÓN NIVEL
 * PLIEGUE (sinte): seno con FM, plegador y compuerta de paso bajo con golpe (costa oeste, Buchla).
 *   NOTA PLIEGUE SIMETRÍA FM | RAZÓN CAÍDA COMPUERTA NIVEL
 * TABLA (sinte): 16 ondas en fila (analógicas, sync, órgano, campanas, digitales), fase torcida, filtro y envolvente.
 *   NOTA POSICIÓN TORCER MOVIMIENTO | FILTRO RESONANCIA FORMA NIVEL
 * ÓXIDO (sinte): dos cuadradas en modulación cruzada y anillo, por un pasabajos resonante que sigue a la nota:
 *   bajos y leads industriales.                                NOTA RAZÓN CRUCE ANILLO | FILTRO RESONANCIA CAÍDA NIVEL
 * Tercera tanda: lo que tenían los setups de techno, electro y breaks y aquí faltaba.
 * ENJAMBRE (sinte): hasta 6 sierras o pulsos desafinados que derivan, con bajada de tono: reese y hoover.
 *   NOTA VOCES DESAFINACIÓN DERIVA | FORMA BAJADA FILTRO NIVEL
 * FASE (sinte): distorsión de fase a la Casio CZ (sierra, cuadrada y resonante) con su envolvente: bajos y leads de electro.
 *   NOTA FORMA DISTORSIÓN ENVOLVENTE | CAÍDA SUB SATURACIÓN NIVEL
 * SYNC (sinte): oscilador sincronizado a otro, con barrido: el lead del Prophet en el electro y el Detroit.
 *   NOTA SYNC BARRIDO CAÍDA | CAOS FILTRO RESONANCIA NIVEL   (CAOS: el esclavo empuja al maestro; arriba de 64, anillo)
 * ÓRGANO (sinte): seis registros con percusión y giro: el bajo de órgano del house (Korg M1) y el Hammond.
 *   NOTA GRAVES AGUDOS ESTIRAR | PERCUSIÓN CAÍDA GIRO NIVEL   (ESTIRAR: registros inarmónicos; GIRO llega a anillo) */
#pragma once
#include <stdint.h>
#include "fg_dsp.h"

enum { FGC_IMPACTO, FGC_CAJA909, FGC_PARCHE, FGC_PALMAS, FGC_METALICO, FGC_CUATRO, FGC_LYRA, FGC_PLIEGUE, FGC_TABLA, FGC_OXIDO,
       FGC_ENJAMBRE, FGC_FASE, FGC_SYNC, FGC_ORGANO, FGC_N };
#define FGC_NP 8
#define FGC_DL 2048
enum { FGC_R_BOMBO, FGC_R_CAJA, FGC_R_METAL, FGC_R_PERC, FGC_R_SINTE };

typedef struct {
    uint8_t model, p[FGC_NP], on, gated;
    uint32_t gate, rng, t;
    int32_t amp, att_inc, kd, kr, sus, vel;  /* envolvente Q30 */
    int32_t env2, k2, env3, k3;              /* envolventes de barrido, ruido, golpe */
    uint32_t ph[6], inc[6], lph;
    int32_t note_q8, y[6], fb, hold, hcnt, lp;
#ifndef FG_DEVICE
    int32_t lp2;                             /* (LYRA) */
#endif
    fg_svf_t f[6];
#ifndef FG_DEVICE
    int16_t dl[2][FGC_DL];                   /* LYRA: los dos delays (LYRA quedó fuera: no va al aparato) */
    uint32_t dw;
#endif
    uint8_t ep[FGC_NP];                      /* las perillas del motor (p es la voz: con los locks del paso) */
    int32_t peak;
} fg_cand_t;

void fg_cand_init(fg_cand_t *c, unsigned model);
void fg_cand_param(fg_cand_t *c, unsigned i, unsigned v);                 /* (la página de motores: el motor que suena) */
/* el aparato: cambia de motor (la voz se corta) con sus perillas (src: las de ese motor; 0: las de fábrica) */
void fg_cand_model(fg_cand_t *c, unsigned model, const uint8_t *src);
void fg_cand_param_m(fg_cand_t *c, unsigned model, unsigned i, unsigned v); /* una perilla (si es de otro motor, no hace nada) */
void fg_cand_trig(fg_cand_t *c, unsigned vel, uint32_t gate);
/* con los locks del paso (ov/mask, como los demás motores); gate: muestras hasta soltar (FGY_HOLD: hasta release) */
void fg_cand_trig_lk(fg_cand_t *c, unsigned vel, uint32_t gate, const uint8_t *ov, unsigned mask);
void fg_cand_release(fg_cand_t *c);
void fg_cand_render(fg_cand_t *c, int32_t *out, unsigned n);
extern const char *const FGC_NAME[FGC_N];
extern const char *const FGC_LAB[FGC_N][FGC_NP];
extern const uint8_t FGC_DEF[FGC_N][FGC_NP];
extern const uint8_t FGC_ROLE[FGC_N];
extern const uint8_t FGC_RND[FGC_N][FGC_NP][2];   /* el rango de AZAR de cada perilla: donde suena bien */
