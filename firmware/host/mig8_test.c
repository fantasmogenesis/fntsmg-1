/* 0.9 (formato 7, un solo sonido) -> 0.10 (formato 8, un sonido por patrón). Se compila dos veces:
 *   con el C de la 0.9 (-DVIEJO): arma un proyecto y escribe out/v7.bin;
 *   con el de ahora: abre out/v7.bin y los autoguardados reales que se den, y revisa que todos los patrones
 *   suenen con el sonido que tenía el proyecto, que vuelva a guardarse igual y que quepa */
#include <stdio.h>
#include <string.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000], buf[16384], buf2[16384];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static void run(unsigned ms) { static int32_t o[2 * FG_BLOCK]; double acc = 0; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); acc += FG_SR / 1000.0; while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, o, FG_BLOCK); } } }
int main(int argc, char **argv)
{
    fg_box_init(&B, VMEM, 240, 240, 4, 1); run(100);
#ifdef VIEJO
    while (B.sel != 5) { fg_box_enc(&B, FE_SELECT, 1); run(5); }
    fg_box_enc(&B, FE_ALGO, 1); fg_box_enc(&B, FE_ALGO, 1); fg_box_enc(&B, FE_K2, 9); run(50);
    B.Q.pat[3].tr[2].st[5].v = 1; B.s.arp_on |= 1u << 6; B.s.mix[1][0] = 33;
    uint32_t n = fg_box_pack(&B, buf, sizeof buf);
    FILE *f = fopen("out/v7.bin", "wb"); fwrite(buf, 1, n, f); fclose(f);
    printf("v7: %u bytes (modelo de la pista 6: %u)\n", n, B.s.model[5]);
    return 0;
#else
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { printf("no está %s\n", argv[a]); bad++; continue; }
        uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f);
        const uint8_t *p = buf;
        if (n > 12 && !memcmp(buf, "FGS1", 4)) { p += 12; n -= 12; }     /* (la cabecera de la ranura) */
        unsigned ver = p[4];
        int r = fg_box_unpack(&B, p, n);
        unsigned same = 1; fbx_kit_t k;
        for (unsigned q = 1; q < FBX_NP; q++) same &= !memcmp(&B.kit[q], &B.kit[0], sizeof k);
        uint32_t n2 = fg_box_pack(&B, buf2, sizeof buf2);
        fg_box_t *c = &B;
        static uint8_t again[16384]; uint32_t n3 = 0;
        int r2 = fg_box_unpack(c, buf2, n2); n3 = fg_box_pack(c, again, sizeof again);
        int okk = r == 0 && same && n2 && n2 <= FBX_PROJ_MAX && r2 == 0 && n3 == n2 && !memcmp(again, buf2, n2);
        printf("%s  %-60s v%u %5u B -> v8 %5u B, mismo sonido en los 16: %s, con sonido propio %04x\n", okk ? "bien " : "FALLA", argv[a], ver, n, n2, same ? "sí" : "no", B.kit_own);
        if (!okk) { unsigned d = 0; while (d < n2 && again[d] == buf2[d]) d++; printf("      r %d r2 %d n2 %u n3 %u, primer byte distinto %u (%02x / %02x) cur %u\n", r, r2, n2, n3, d, buf2[d], again[d], B.s.cur); }
        bad += !okk;
        run(200);
    }
    printf(bad ? "FALLAS %d\n" : "todo bien\n", bad);
    return bad;
#endif
}
