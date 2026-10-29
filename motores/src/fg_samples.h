/* Samples en la flash (fg_samples.c lo genera firmware/tools/gen_samples.py): ADPCM IMA en bloques de 64 muestras,
 * cada bloque con 4 bytes de cabecera (la muestra anterior y el índice del paso), así se salta a cualquier bloque.
 * fg_smpv_*: una cabeza de lectura con velocidad fraccionaria, lazo e interpolación lineal. */
#pragma once
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    uint32_t len, loop_s, loop_e;            /* muestras; el lazo (texturas) */
    uint32_t rate;
    uint8_t root, loop;
    const char *name;
} fg_sample_t;

extern const fg_sample_t FG_SMP_KICK[], FG_SMP_TEX[], FG_SMP_SNR[];
extern const unsigned FG_SMP_KICK_N, FG_SMP_TEX_N, FG_SMP_SNR_N;

typedef struct {
    const fg_sample_t *s;
    uint32_t pos;                            /* la muestra que ya está en 'a' */
    uint32_t frac;                           /* entre a y b, Q16 */
    int32_t a, b, pred;
    uint8_t idx, done;
} fg_smpv_t;

void fg_smpv_start(fg_smpv_t *v, const fg_sample_t *s, uint32_t pos);
/* la siguiente muestra (Q15) avanzando step (Q16: 65536 = la tasa del sample a 44.1 kHz) */
int32_t fg_smpv_next(fg_smpv_t *v, uint32_t step);
