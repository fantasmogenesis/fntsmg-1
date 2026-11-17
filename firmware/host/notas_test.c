/* Notas pegadas: una tecla sostenida no se queda sonando si cambias de motor, prendes o apagas el ARP, o sueltas muchas a la vez */
#include <stdio.h>
#include <string.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static const uint8_t W2K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static void run(unsigned ms) { static int32_t o[2 * FG_BLOCK]; double acc = 0; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); acc += FG_SR / 1000.0; while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, o, FG_BLOCK); } fg_box_idle(&B); } }
static unsigned sy_held(unsigned s) { unsigned n = 0; for (int k = 0; k < FGY_VOICES; k++) n += B.SY[s].v[k].on && B.SY[s].v[k].gate != 0; return n; }
static int ca_held(unsigned t) { const fg_cand_t *c = &B.CA[t]; return c->on && c->gated && c->gate > c->t; }
static void model(unsigned m) { B.s.model[4] = (uint8_t)m; fg_box_enc(&B, FE_ALGO, 1); fg_box_enc(&B, FE_ALGO, -1); run(50); }
static void arp(int on)                                  /* K1 en la página ARP: 0 apagado, 1 arriba */
{
    if (B.page != PG_ARP) { fg_box_button(&B, FB_ARP, 1); fg_box_button(&B, FB_ARP, 0); run(50); }
    for (int i = 0; i < 12 && (((B.s.arp_on >> 4) & 1) != on); i++) { fg_box_enc(&B, FE_K1, on ? 1 : -1); run(30); }
}
int main(void)
{
    int bad = 0;
    fg_box_init(&B, VMEM, 240, 240, 4, 1); fg_box_boot(&B); run(3000);
    while (B.sel != 4) { fg_box_enc(&B, FE_SELECT, 1); run(10); }
    fg_box_button(&B, FB_ARP, 1); fg_box_button(&B, FB_ARP, 0); run(50);   /* ARP: las blancas son notas */

    /* 1. ÓRGANO (motor nuevo, una voz) con la tecla abajo, cambio a DRON (de los de antes), suelto */
    model(15); printf("motor: %s\n", fbx_model_name(4, B.s.model[4]));
    fg_box_key(&B, W2K[0], 1); run(200);
    printf("ÓRGANO sostenido: %d (1 = bien)\n", ca_held(4)); bad += !ca_held(4);
    fg_box_enc(&B, FE_ALGO, 1); run(50); printf("cambio a %s con la tecla abajo\n", fbx_model_name(4, B.s.model[4]));
    fg_box_key(&B, W2K[0], 0); run(1500);
    printf("soltada: ÓRGANO sigue sostenido %d (0 = bien)\n", ca_held(4)); bad += ca_held(4);

    /* 2. al revés: SH sostenido, cambio a ÓRGANO, suelto */
    model(FGY_SH);
    fg_box_key(&B, W2K[1], 1); run(200);
    model(15);
    fg_box_key(&B, W2K[1], 0); run(1500);
    printf("SH → ÓRGANO, soltada: %u voces de SH sostenidas (0 = bien)\n", sy_held(0)); bad += sy_held(0) != 0;

    /* 3. tecla abajo sin ARP, prendo el ARP, suelto */
    model(FGY_SH);
    fg_box_key(&B, W2K[2], 1); run(200);
    arp(1); printf("ARP prendido con la tecla abajo (arp %d)\n", (B.s.arp_on >> 4) & 1);
    fg_box_key(&B, W2K[2], 0); run(1500);
    printf("soltada: %u voces sostenidas (0 = bien)\n", sy_held(0)); bad += sy_held(0) != 0;

    /* 4. tecla abajo con ARP, apago el ARP, suelto: el arpegiador no se queda con la tecla */
    fg_box_key(&B, W2K[3], 1); run(300);
    arp(0);
    fg_box_key(&B, W2K[3], 0); run(1500);
    printf("ARP apagado con la tecla abajo, soltada: %u voces, %u teclas en el ARP (0 y 0 = bien)\n", sy_held(0), B.ar[0].n);
    bad += sy_held(0) != 0 || B.ar[0].n != 0;

    /* 5. muchas a la vez: 16 notas MIDI soltadas en el mismo instante en las 4 pistas */
    B.s.mnotes = 3;
    for (unsigned t = 4; t < 8; t++) for (unsigned k = 0; k < 5; k++) fg_box_midi_in(&B, (uint8_t)(0x90 | t), (uint8_t)(48 + k * 3), 100);
    run(200);
    for (unsigned t = 4; t < 8; t++) for (unsigned k = 0; k < 5; k++) fg_box_midi_in(&B, (uint8_t)(0x80 | t), (uint8_t)(48 + k * 3), 0);
    run(1500);
    unsigned left = 0; for (unsigned s = 0; s < 4; s++) left += sy_held(s) + (unsigned)ca_held(4 + s);
    printf("20 notas MIDI soltadas juntas: %u sostenidas (0 = bien)\n", left); bad += left != 0;

    /* 6. MIDI: nota sin ARP, prendo el ARP, la suelto */
    fg_box_midi_in(&B, 0x94, 60, 100); run(200);
    arp(1);
    fg_box_midi_in(&B, 0x84, 60, 0); run(1500);
    printf("MIDI, ARP prendido con la nota abajo, soltada: %u sostenidas (0 = bien)\n", sy_held(0)); bad += sy_held(0) != 0;
    arp(0);

    printf(bad ? "FALLAS %d\n" : "todo bien\n", bad);
    return bad;
}
