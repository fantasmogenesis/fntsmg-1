/* Secuenciador del FM-1: puerto 1:1 de caja/worklet.js, en enteros y dentro de un struct.
 * 8 pistas con largo y velocidad propios, locks de motor (0–7) y de mezcla (8–11), automatización,
 * condición / probabilidad / ratchet / microtiming por paso, LFO por pista, AZAR por golpe, 16 patrones,
 * cadenas y mutes al compás.
 *
 * El reloj corre en 1/32 y cada paso se decide 2 ticks antes. Para que el swing y los redondeos salgan
 * idénticos al worklet sin float, el tiempo es exacto: muestra entera + fracción con denominador
 * D = bpm·20000 (el swing es b·swing·0.66/100, así que cada tick cabe en múltiplos de 1/D).
 *
 * El host no le da al secuenciador acceso a los motores: por cada bloque pide fg_seq_process() y recibe
 * eventos ordenados (golpe, parámetro, mezcla, mute…) con su muestra dentro del bloque; renderiza hasta
 * cada uno y lo aplica. Así el mismo C sirve para el FM-1, la web, un VST o el Move.
 *
 * Una diferencia a propósito con el worklet: el AZAR del LFO (S&H) saca de su propio generador. En el
 * worklet comparte Math.random() con la probabilidad y el AZAR de los pasos, y como sortea en cada vuelta
 * del LFO (aunque la forma no sea AZAR), mover la velocidad del LFO cambiaba qué pasos sonaban.
 * test/seq_compare.mjs prueba que el resto sale idéntico, muestra por muestra. */
#pragma once
#include <stdint.h>

#define FG_SEQ_T 8                    /* pistas: 0 bombo, 1 caja, 2 metal, 3 perc, 4..7 sintes */
#define FG_SEQ_NS 32                  /* pasos por pista */
#ifndef FG_SEQ_NP
#define FG_SEQ_NP 16                  /* patrones */
#endif
#define FG_SEQ_NM 11                  /* modelos por pista (los sintes tienen 11) */
#define FG_SEQ_QMAX 128               /* eventos pendientes (decididos y aún no sonados) */
#ifndef FG_SEQ_CHAIN
#define FG_SEQ_CHAIN 64                 /* (la vista CADENA: 16 filas con repeticiones, desplegadas) */
#endif
#define FG_SEQ_PRE 65
#define FG_NONE 255u                  /* sin lock / sin automatización */

enum { FG_COND_ALWAYS = 0, FG_COND_FILL = 10, FG_COND_NOFILL = 11 };
/* automatización de un paso: el valor de la perilla i (FG_NONE si no tiene); poner una tercera reemplaza la segunda */
#define FG_AU_GET(st, i) ((st)->au[0] == (i) ? (st)->au[1] : (st)->au[2] == (i) ? (st)->au[3] : FG_NONE)   /* 1..9: 1:2 2:2 1:3 2:3 3:3 1:4 2:4 3:4 4:4 */
enum { FG_SPD_HALF, FG_SPD_1, FG_SPD_2 };
enum { FG_SEQ_REP4 = 1u << 0, FG_SEQ_REP8 = 1u << 1, FG_SEQ_SOLOPERC = 1u << 2, FG_SEQ_NOKICK = 1u << 3 };
enum { FG_LFO_SIN, FG_LFO_TRI, FG_LFO_SAW, FG_LFO_SQR, FG_LFO_SH };

typedef struct {
    uint8_t v;                        /* 0 apagado, 1 golpe, 2 acento */
    uint8_t c, p, r;                  /* condición 0..11, probabilidad 0..100 (100 = siempre), ratchet 1..4 */
    int8_t u;                         /* micro −8..+7 dieciseisavos del paso */
    uint8_t lk[12];                   /* locks: 0..7 motor (0 de sinte = NOTA relativa, 64 = sin mover), 8..11 mezcla */
    uint8_t au[4];                    /* automatización grabada (hasta 2 perillas por paso): índice, valor, índice, valor */
} fg_step_t;

typedef struct {
    fg_step_t st[FG_SEQ_NS];
    uint8_t len, spd;                 /* largo 1..32; velocidad FG_SPD_* */
} fg_track_t;

typedef struct { fg_track_t tr[FG_SEQ_T]; } fg_pattern_t;

/* ---- eventos que el host consume ---- */
enum {
    FG_EV_TRIG,                       /* golpe: t, vel, mask + lk[] ya en valores del motor, len = gate en muestras */
    FG_EV_PRE,                        /* la caja empieza a crecer: len muestras antes del golpe */
    FG_EV_PARAM,                      /* parámetro del motor: t, a = modelo, b = índice, v = valor */
    FG_EV_MIX,                        /* tira de mezcla: t, b = índice (FGX_*), v = valor */
    FG_EV_MUTE,                       /* mutes: a = máscara de mute, b = máscara de solo */
    FG_EV_STEP,                       /* para la interfaz: la pista t pasó por el paso s */
    FG_EV_PAT,                        /* para la interfaz: a = patrón actual, b = en cola (255 = nada), v = posición en la cadena */
};
enum { FG_EVF_FIRST = 1u << 0, FG_EVF_MANUAL = 1u << 1, FG_EVF_LFO = 1u << 2, FG_EVF_AUTO = 1u << 3 };

typedef struct {
    uint16_t at;                      /* muestra dentro del bloque (0..n) */
    uint8_t kind, t, s, a, b, v, flags, vel;
    uint16_t mask;
    uint8_t lk[8];
    int32_t len;
} fg_seq_ev_t;

/* evento pendiente: guarda su tiempo exacto y copia lo que necesita del paso (el patrón puede cambiar antes) */
typedef struct {
    uint32_t s, f;                    /* muestra + fracción en 1/D */
    uint8_t kind, t, st, vel, first, r0;
    uint8_t lk[12], au[8];
    uint8_t mute, solo;
    int32_t len;
} fg_seq_q_t;

typedef struct {
    fg_pattern_t pat[FG_SEQ_NP];

    /* lo que el secuenciador necesita saber del sonido */
    uint32_t sr;
    uint8_t model[FG_SEQ_T];
    uint8_t pbase[FG_SEQ_T][FG_SEQ_NM][8];   /* valores de perilla: base de locks relativos, AZAR y LFO */
    uint8_t mix[FG_SEQ_T][4];
    uint8_t lfo[FG_SEQ_T][4];         /* destino, velocidad, profundidad, forma */
    uint8_t azar[FG_SEQ_T], ahead[FG_SEQ_T];
    int8_t transp;
    uint8_t bpm, swing, fill, playing, muteq, perf;

    /* patrón y cadena */
    uint8_t cur, chain[FG_SEQ_CHAIN], nchain, chainpos;
    int16_t queued;

    /* reloj */
    uint32_t D, dt_q[2], dt_r[2], sd_q[3], sd_r[3];   /* tick con swing (+, −) y duración de paso por velocidad */
    uint32_t clock, tick, patTick, tickS, tickF;
    int16_t pos[FG_SEQ_T], repA[FG_SEQ_T];
    int32_t repK[FG_SEQ_T];
    uint32_t loops[FG_SEQ_T];
    uint8_t mixLocked[FG_SEQ_T];

    /* mutes */
    uint8_t mute, solo, pend_mute, pend_solo, pend;

    /* LFO */
    uint32_t lfoPh[FG_SEQ_T], lfoInc[FG_SEQ_T];
    int32_t lfoSH[FG_SEQ_T];
    int16_t lfoLastD[FG_SEQ_T], lfoLastV[FG_SEQ_T];
    /* el segundo LFO (si apunta a la misma perilla que el primero, se suman) */
    uint8_t lfo2[FG_SEQ_T][4];
    uint32_t lfo2Ph[FG_SEQ_T], lfo2Inc[FG_SEQ_T];
    int32_t lfo2SH[FG_SEQ_T];
    int16_t lfo2LastD[FG_SEQ_T], lfo2LastV[FG_SEQ_T];
    /* por pista: transponer (semitonos los sintes, unidades de TONO la percusión), humano, densidad */
    int8_t ttr[FG_SEQ_T];
    uint8_t hum[FG_SEQ_T], dens[FG_SEQ_T];

    uint32_t rng, lfo_rng;

    /* cola */
    fg_seq_q_t q[FG_SEQ_QMAX];
    uint16_t nq;
    int32_t pre[FG_SEQ_PRE];
    uint8_t npre;
    fg_seq_ev_t now[24];              /* lo que pasó entre bloques (mute al instante, LFO restaurado…) */
    uint8_t nnow;
} fg_seq_t;

void fg_seq_init(fg_seq_t *q, uint32_t sr, uint32_t seed);
void fg_seq_clear_track(fg_track_t *tr);
/* Un bloque de n muestras: escribe hasta cap eventos ordenados en out y devuelve cuántos. */
int fg_seq_process(fg_seq_t *q, unsigned n, fg_seq_ev_t *out, int cap);
/* Muestras por paso de 1/16 para fg_mix_tempo (redondeadas) */
uint32_t fg_seq_step_samples(const fg_seq_t *q);
/* Valores finales de las 8 perillas para un golpe (base + locks): para los visuales */
void fg_seq_params(const fg_seq_t *q, unsigned t, unsigned mask, const uint8_t *lk, uint8_t *out);

/* lo que en el worklet son mensajes */
void fg_seq_play(fg_seq_t *q, int on);
void fg_seq_tempo(fg_seq_t *q, unsigned bpm);
void fg_seq_swing(fg_seq_t *q, unsigned swing);
void fg_seq_param(fg_seq_t *q, unsigned t, unsigned m, unsigned i, unsigned v);   /* el host ya movió el motor */
void fg_seq_model(fg_seq_t *q, unsigned t, unsigned m);
void fg_seq_mix(fg_seq_t *q, unsigned t, unsigned i, unsigned v);
void fg_seq_lfo(fg_seq_t *q, unsigned t, const uint8_t v[4]);
void fg_seq_mutes(fg_seq_t *q, unsigned mute, unsigned solo, int now);   /* now = 0: al compás si muteq */
void fg_seq_perf(fg_seq_t *q, unsigned seqflags);
void fg_seq_fill(fg_seq_t *q, int on);
void fg_seq_queue(fg_seq_t *q, unsigned p);
void fg_seq_chain(fg_seq_t *q, const uint8_t *list, unsigned n);
/* una nota a mano: key va en el evento (a) para soltarla después; hold > 0: su compuerta en muestras */
void fg_seq_lfo2(fg_seq_t *q, unsigned t, const uint8_t v[4]);
/* una nota a mano en una muestra exacta (el arpegiador) */
void fg_seq_note_at(fg_seq_t *q, unsigned t, unsigned vel, const uint8_t lk[8], int32_t gate, uint32_t at);
/* SCL pág. 2: i 1 TRANSPONER (64 = 0), 2 HUMANO, 3 DENSIDAD (127 = todos los pasos) */
void fg_seq_track2(fg_seq_t *q, unsigned t, unsigned i, unsigned v);
void fg_seq_audition(fg_seq_t *q, unsigned t, unsigned vel, const uint8_t lk[8], unsigned key, int32_t hold);
