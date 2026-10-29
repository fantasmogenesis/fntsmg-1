/* Visuales del FM-1: puerto de la pantalla de caja/index.html (draw, lifeOf, spawn, post2, renderMixer,
 * renderPatterns) a enteros, sobre una pantalla de 1 bit de W×H cualquiera (240×240 en el FM-1).
 *
 * Cada pista dibuja lo que suena; su tamaño sigue a su volumen. Se dibuja en coordenadas de diseño de
 * 240×240 (las de la caja) sobre un buffer de intensidad de 8 bits del tamaño real de la pantalla: las
 * estelas viven ahí, se apagan un poco cada cuadro. Luego, por bandas de 16 filas, se aplican los efectos
 * de performance y el dither Bayer 4×4 a 1 bit: así el host sólo necesita un buffer de banda para la LCD.
 *
 * Memoria (la da el host, sin malloc): fg_vis_size(w, h, hist). En 240×240 son 57.6 KB de intensidad, 8.4 KB
 * de bandas y 7.2 KB por cuadro de historia (repite, stutter y reversa leen de ahí). Sin historia esos
 * efectos se ven en vivo.
 *
 * Lo que NO está aquí: el texto de la pantalla (perillas, nombres, condición del paso). Eso lo dibuja la
 * plataforma con su propia letra encima de la banda. */
#pragma once
#include <stdint.h>

#define FG_VIS_ENTS 160
#define FG_VIS_BAND 16
#define FG_VIS_D 240                   /* lado del espacio de diseño */

enum { FG_VIEW_VIS, FG_VIEW_MIX, FG_VIEW_PAT };
/* efectos: los mismos bits que FGP_* de fg_mix.h, más los de repite del secuenciador */
enum { FG_VIS_REP4 = 1u << 12, FG_VIS_REP8 = 1u << 13 };

typedef struct {
    uint8_t t, m, p[8];
    uint16_t q[8], vq;                 /* p y velocidad como fracción Q8 (0..256) */
    uint32_t seed, born;
} fg_vis_ent_t;

/* lo que la pantalla necesita saber de la caja en cada cuadro */
typedef struct {
    uint32_t now_ms, perf;             /* perf: FGP_* | FG_VIS_REP4/8 */
    uint8_t view, bpm, sel, cur, queued;   /* queued: 255 = nada */
    uint16_t used;                     /* patrones con pasos */
    uint8_t mix[8][4];
    uint8_t mute, solo, pend, has_pend;    /* pend: mutes esperando al compás */
    uint16_t kmov[4];                  /* las perillas: K1 ondula, K2 alarga las estelas, K3 pixelea, K4 hace brillar (0..256) */
} fg_vis_ui_t;

typedef struct {
    uint16_t w, h, hist_n, stride;
    uint16_t xm[FG_VIS_D + 1], ym[FG_VIS_D + 1];   /* dónde empieza en pantalla cada pixel de diseño */
    uint8_t *buf, *hist, *sa, *sb, *row;

    fg_vis_ent_t ents[FG_VIS_ENTS];
    uint16_t nents;
    int32_t level[8];                  /* Q15, con caída */
    uint8_t mute, solo;

    uint32_t rng, last_ms, brk, beat_ph;

    /* historia y lo que el cuadro le deja dicho a las bandas */
    uint16_t hp, hn, loop_n, loop_fr, loop_base;
    uint8_t loop_id, view;
    int16_t push_slot, src_slot;
    uint32_t perf, riser_seed;
    uint8_t gate_off;
    uint16_t kmov[4];
    uint32_t kph;
} fg_vis_t;

uint32_t fg_vis_size(unsigned w, unsigned h, unsigned hist);
void fg_vis_init(fg_vis_t *v, void *mem, unsigned w, unsigned h, unsigned hist, uint32_t seed);

/* un golpe (FG_EV_TRIG con FG_EVF_FIRST o MANUAL): p son las 8 perillas ya con locks (fg_seq_params) */
void fg_vis_hit(fg_vis_t *v, unsigned t, unsigned m, unsigned vel, const uint8_t p[8], uint32_t now_ms);
void fg_vis_level(fg_vis_t *v, unsigned t, int32_t peak);      /* pico posfader de la pista, Q15; cada ~4 bloques */

/* un cuadro: estelas, golpes vivos, vista; luego fg_vis_band() para y0 = 0, 16, 32… */
void fg_vis_frame(fg_vis_t *v, const fg_vis_ui_t *ui);
/* rows ≤ 16 filas desde y0 (múltiplo de 8) a 1 bit, MSB primero, (w + 7) / 8 bytes por fila */
void fg_vis_band(fg_vis_t *v, unsigned y0, unsigned rows, uint8_t *out);
