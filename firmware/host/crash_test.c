/* Busca el crash del issue #3 (FELUCCA CRASH en reposo, 0.10.1): la caja con la memoria del visual del FM-1 (exacta,
 * 1 cuadro de historia, como fgbox.c), horas simuladas en reposo y con botones, teclas y perillas al azar.
 * Con ASan/UBSan (pruebas.sh crash_test): cualquier lectura o escritura fuera de su lugar para la prueba.
 *   ./out/crash_test [semilla] [minutos] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fg_box.h"

static fg_box_t B;
static uint8_t SLOT[FBX_UBANK_SLOT + 2][16384];
static uint32_t SLOTN[FBX_UBANK_SLOT + 2];
static uint32_t now_ms, rng;

int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return now_ms; }
static int s_write(unsigned slot, const void *d, uint32_t n) { if (n > sizeof SLOT[0]) return -1; memcpy(SLOT[slot], d, n); SLOTN[slot] = n; return 0; }
static int s_read(unsigned slot, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[slot] || SLOTN[slot] > cap) return -1; memcpy(d, SLOT[slot], SLOTN[slot]); *n = SLOTN[slot]; return 0; }
static int s_has(unsigned slot) { return SLOTN[slot] != 0; }
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % n; }

static uint16_t *px;
static void run(uint32_t ms)
{
    static int32_t out[2 * FG_BLOCK];
    static double acc;
    for (uint32_t k = 0; k < ms; k++) {
        now_ms++;
        fg_box_tick(&B, now_ms);
        for (acc += FG_SR / 1000.0; acc >= FG_BLOCK; acc -= FG_BLOCK) fg_box_audio(&B, out, FG_BLOCK);
        if (now_ms % 33u == 0) { fg_box_frame(&B); for (unsigned y = 0; y < 240; y += 16) fg_box_band(&B, y, px); }
        fg_box_idle(&B);
    }
}

int main(int argc, char **argv)
{
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 0) : 1u, mins = argc > 2 ? (uint32_t)strtoul(argv[2], 0, 0) : 20u;
    rng = seed * 2654435761u | 1u;
    size_t vn = 28800u + 8640u + 1u * 7200u + 64u;                /* = fgx_vis de fgbox.c (FGX_VIS_HIST 1) */
    uint8_t *vmem = malloc(vn);
    px = malloc(240u * 16u * sizeof *px);                          /* (exacto: ASan ve un pixel de más) */
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, vmem, 240, 240, 1, seed);
    fg_box_boot(&B);
    run(5000);
    printf("reposo sin tocar %u min\n", mins); fflush(stdout);
    run(mins * 60000u);
    fg_box_button(&B, FB_PLAY, 1); run(20); fg_box_button(&B, FB_PLAY, 0);
    printf("reposo tocando %u min\n", mins); fflush(stdout);
    run(mins * 60000u);
    printf("al azar %u min\n", mins); fflush(stdout);
    uint8_t held_b[FB_NBTN] = {0}, held_k[27] = {0};
    for (uint32_t t = 0; t < mins * 60000u;) {
        unsigned w = rnd(10);
        if (w < 3) { unsigned id = rnd(FB_NBTN); if (id == FB_SAVE && rnd(4)) id = FB_FX; held_b[id] ^= 1; fg_box_button(&B, id, held_b[id]); }
        else if (w < 7) { unsigned k = rnd(27); held_k[k] ^= 1; fg_box_key(&B, k, held_k[k]); }
        else fg_box_enc(&B, rnd(FE_NENC), (int)rnd(9) - 4);
        if (!rnd(40)) { for (unsigned i = 0; i < FB_NBTN; i++) if (held_b[i]) { held_b[i] = 0; fg_box_button(&B, i, 0); }
                         for (unsigned i = 0; i < 27; i++) if (held_k[i]) { held_k[i] = 0; fg_box_key(&B, i, 0); } }
        uint32_t d = 5u + rnd(rnd(8) ? 200u : 5000u);
        run(d); t += d;
    }
    printf("ok semilla %u\n", seed);
    free(px); free(vmem);
    return 0;
}
