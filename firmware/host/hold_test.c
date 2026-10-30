/* SOSTENER sin ARP: lo que se suelta sigue sonando; una nota nueva suelta lo de antes; apagarlo lo calla */
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
static unsigned held_voices(void) { unsigned n = 0; for (int k = 0; k < FGY_VOICES; k++) n += B.SY[0].v[k].on && B.SY[0].v[k].gate != 0; return n; }
int main(void)
{
    int bad = 0;
    fg_box_init(&B, VMEM, 240, 240, 4, 1); fg_box_boot(&B); run(3000);
    while (B.sel != 4) { fg_box_enc(&B, FE_SELECT, 1); run(10); }
    if (B.seqmode) { fg_box_button(&B, FB_SEQ, 1); fg_box_button(&B, FB_SEQ, 0); }
    B.s.model[4] = FGY_SH;                               /* (SH: una voz por nota; CUERDAS junta el acorde en una) */
    { extern void fg_box_enc(fg_box_t *, unsigned, int); }
    fg_box_enc(&B, FE_ALGO, 1); fg_box_enc(&B, FE_ALGO, -1); run(50);
    fg_box_button(&B, FB_ARP, 1); run(1200); fg_box_button(&B, FB_ARP, 0); run(50);
    printf("mantener ARP: %s · %s (sostener %d, arp %d)\n", B.ov.label, B.ov.value, B.s.arp[4][3], (B.s.arp_on >> 4) & 1);
    bad += !B.s.arp[4][3] || ((B.s.arp_on >> 4) & 1);
    fg_box_key(&B, W2K[0], 1); fg_box_key(&B, W2K[2], 1); run(100); fg_box_key(&B, W2K[0], 0); fg_box_key(&B, W2K[2], 0); run(800);
    printf("acorde soltado, 0.8 s después: %u notas sostenidas (2 = bien)\n", held_voices()); bad += held_voices() != 2;
    fg_box_key(&B, W2K[4], 1); run(50); fg_box_key(&B, W2K[4], 0); run(800);
    printf("otra nota: %u sostenidas (1 = bien)\n", held_voices()); bad += held_voices() != 1;
    fg_box_button(&B, FB_ARP, 1); run(1200); fg_box_button(&B, FB_ARP, 0); run(800);
    printf("SOSTENER apagado: %s, %u sostenidas (0 = bien)\n", B.ov.value, held_voices()); bad += held_voices() != 0;
    fg_box_key(&B, W2K[1], 1); run(50); fg_box_key(&B, W2K[1], 0); run(50);
    printf("sin SOSTENER, al soltar: %u (0 = bien)\n", held_voices()); bad += held_voices() != 0;
    printf(bad ? "FALLAS %d\n" : "todo bien\n", bad);
    return bad;
}
