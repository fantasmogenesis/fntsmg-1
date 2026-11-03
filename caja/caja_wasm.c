/* La caja en el navegador: el firmware del FM-1 tal cual (fg_box), corriendo en un AudioWorklet.
 * Cada bloque de audio avanza el reloj de la interfaz; la pantalla sale por bandas de 240×16 (RGB565)
 * y solo las que cambiaron. Las ranuras viven en memoria; la página las guarda en el navegador.
 *   caja/build.sh  ->  caja/fntsmg1.wasm */
#include <string.h>
#include "fg_box.h"

#define NSLOT (FBX_DEV_SLOT + 1)
#define SLOT_CAP 16384

static fg_box_t B;
static uint8_t VMEM[100000];
static uint8_t SLOT[NSLOT][SLOT_CAP];
static uint32_t SLOTN[NSLOT];
static uint32_t dirty;
static int32_t OUT[2 * FG_BLOCK];
static float L[FG_BLOCK], R[FG_BLOCK];
static uint16_t BAND[240 * 16];
static uint32_t now_ms, ms_acc;

int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }

static int s_write(unsigned s, const void *d, uint32_t n)
{
    if (s >= NSLOT || n > SLOT_CAP) return -1;
    memcpy(SLOT[s], d, n); SLOTN[s] = n; dirty |= 1u << s;
    return 0;
}
static int s_read(unsigned s, void *d, uint32_t cap, uint32_t *n)
{
    if (s >= NSLOT || !SLOTN[s] || SLOTN[s] > cap) return -1;
    memcpy(d, SLOT[s], SLOTN[s]); *n = SLOTN[s];
    return 0;
}
static int s_has(unsigned s) { return s < NSLOT && SLOTN[s] != 0; }

/* antes de cj_boot: la página copia lo guardado en cj_slot(i) y avisa el largo */
uint8_t *cj_slot(unsigned s) { return s < NSLOT ? SLOT[s] : 0; }
uint32_t cj_slot_len(unsigned s) { return s < NSLOT ? SLOTN[s] : 0; }
void cj_set_slot_len(unsigned s, uint32_t n) { if (s < NSLOT && n <= SLOT_CAP) SLOTN[s] = n; }
unsigned cj_nslots(void) { return NSLOT; }
/* qué ranuras escribió el aparato desde la última vez (y se borra) */
uint32_t cj_dirty(void) { uint32_t d = dirty; dirty = 0; return d; }

void cj_init(uint32_t seed)
{
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, VMEM, 240, 240, 4, seed);
}
void cj_boot(void) { fg_box_boot(&B); }

float *cj_left(void) { return L; }
float *cj_right(void) { return R; }

/* un bloque de 128: interfaz al día (1 ms a la vez, como el loop del FM-1) y audio */
void cj_block(void)
{
    ms_acc += FG_BLOCK * 1000u;
    while (ms_acc >= (uint32_t)FG_SR) {
        ms_acc -= (uint32_t)FG_SR;
        fg_box_tick(&B, ++now_ms);
        fg_box_idle(&B);
    }
    fg_box_audio(&B, OUT, FG_BLOCK);
    for (unsigned i = 0; i < FG_BLOCK; i++) {
        L[i] = (float)OUT[2 * i] * (1.0f / 32768.0f);
        R[i] = (float)OUT[2 * i + 1] * (1.0f / 32768.0f);
    }
}

void cj_button(unsigned id, int down) { fg_box_button(&B, id, down); }
void cj_key(unsigned k, int down) { fg_box_key(&B, k, down); }
void cj_enc(unsigned role, int steps) { fg_box_enc(&B, role, steps); }

/* la pantalla: cj_frame una vez por cuadro y luego cj_band(y) por banda (1 = cambió y quedó en cj_px) */
void cj_frame(void) { fg_box_frame(&B); }
void cj_repaint(void) { memset(B.band_sum, 0, sizeof B.band_sum); }
int cj_band(unsigned y0) { return fg_box_band(&B, y0, BAND); }
uint16_t *cj_px(void) { return BAND; }

uint32_t cj_leds_buttons(void) { uint32_t bt, ky; fg_box_leds(&B, &bt, &ky); return bt; }
uint32_t cj_leds_keys(void) { uint32_t bt, ky; fg_box_leds(&B, &bt, &ky); return ky; }
uint32_t cj_leds_half(void) { return fg_box_leds_half(&B); }
int cj_playing(void) { return B.playing; }
