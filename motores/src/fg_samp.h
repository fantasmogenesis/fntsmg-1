/* SAMPLE: el mismo motor en las 8 pistas. Toca un sample de la flash: los de fábrica (BOMBOS, CAJAS, TEXTURAS) o
 * los que mandaste desde la app (TUYOS).
 * AFINA SAMPLE INICIO CAÍDA | CORTE CRUJIDO MODO NIVEL
 *   AFINA: ±24 semitonos en la percusión (64 = como es); en los sintes es la nota (las teclas lo tocan cromático,
 *   C4 = como es). SAMPLE: cuál (los bancos tienen lugares fijos: un proyecto no cambia de sample si agregas uno).
 *   INICIO: desde dónde. CAÍDA: lo acorta (127 = entero). CORTE: abajo de 64 quita agudos, arriba quita graves.
 *   CRUJIDO: menos bits y menos muestras. MODO: GOLPE, LAZO (repite; en los sintes, mientras se sostiene),
 *   REVERSA. NIVEL: el volumen, que satura suave arriba de ~64.
 * Se decodifica un bloque de 64 muestras a la vez (128 bytes por voz); la reversa lo lee al revés. */
#pragma once
#include <stdint.h>
#include "fg_samples.h"

enum { FGSV_GOLPE, FGSV_LAZO, FGSV_REVERSA };
/* los lugares de cada banco en la perilla SAMPLE (0..127) */
enum { FGSB_BOMBOS = 0, FGSB_CAJAS = 24, FGSB_TEXTURAS = 48, FGSB_TUYOS = 96, FGSB_END = 128 };
#define FGS_USR 32                           /* samples del usuario (TUYOS) */

typedef struct {
    uint8_t on, mode, rel, crush_n, crush_k, lp, seg;
    const fg_sample_t *s;
    int16_t buf[64], after, loopv;           /* el bloque que suena (64 muestras), la siguiente, y la del principio del lazo */
    int32_t blk, pos;
    uint32_t frac, step, lps, lpe;           /* lazo: desde lps hasta lpe */
    int32_t amp, kd, kill, att;              /* Q30; la entrada (sin clic al empezar a la mitad) Q15 */
    int32_t m, dm;                           /* ganancia de esta muestra (envolvente × NIVEL, Q13) y su paso */
    int32_t g, y0, y1, hp;                   /* CORTE: dos polos; hp = 1 quita graves */
    int32_t gain, crush_y, crush_m;          /* NIVEL Q12; CRUJIDO: muestra sostenida y máscara de bits */
} fg_sampv_t;

/* los de la app: los llena la plataforma (fbx: desde la flash); name tiene que vivir mientras esté puesto */
extern fg_sample_t fg_usr[FGS_USR];
extern uint32_t fg_usr_mask;

/* el sample de un valor de la perilla (si ese lugar está vacío, el anterior que haya); 0 si no hay ninguno */
const fg_sample_t *fg_samp_at(unsigned v);
int fg_samp_valid(unsigned v);
unsigned fg_samp_bank(unsigned v);           /* 0 BOMBOS, 1 CAJAS, 2 TEXTURAS, 3 TUYOS */
/* el siguiente lugar con sample en esa dirección (para girar la perilla); el mismo si no hay más */
unsigned fg_samp_step(unsigned v, int dir);

/* synth: AFINA es la nota (note_q8 de la pista); si no, ±24 semitonos */
void fg_sampv_trig(fg_sampv_t *v, const uint8_t *p, unsigned vel, int synth, int32_t note_q8);
void fg_sampv_release(fg_sampv_t *v);       /* LAZO en un sinte: soltar la nota */
void fg_sampv_render(fg_sampv_t *v, int32_t *out, unsigned n);   /* suma en out */
