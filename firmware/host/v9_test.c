/* 0.10 con los motores nuevos (formato 9, 16 motores por pista): abre proyectos v8 (y v7, v6 y respaldos viejos), revisa
 * campo por campo que la canción quedó igual (los motores que salieron pasan a su reemplazo), y que vuelve a guardarse
 * igual. Luego toca cada motor de cada pista y revisa que suene.
 *   ./pruebas.sh v9_test   (lee los archivos de la línea de comandos) */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000], buf[16384], buf2[16384], again[16384];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int bad;
static void run(unsigned ms) { static int32_t o[2 * FG_BLOCK]; double acc = 0; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); acc += FG_SR / 1000.0; while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, o, FG_BLOCK); } } }
static double energy(unsigned ms) { static int32_t o[2 * FG_BLOCK]; double acc = 0, e = 0; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); acc += FG_SR / 1000.0; while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, o, FG_BLOCK); for (int k = 0; k < 2 * FG_BLOCK; k++) e += (double)o[k] * o[k]; } } return e; }
static const char *NM8[8] = {"BOMBO", "CAJA", "METAL", "PERC", "SINTE A", "SINTE B", "SINTE C", "SINTE D"};
int main(int argc, char **argv)
{
    fg_box_init(&B, VMEM, 240, 240, 4, 1); run(100);
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb"); if (!f) { printf("no está %s\n", argv[a]); bad++; continue; }
        uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f);
        const uint8_t *p = buf;
        if (n > 12 && (!memcmp(buf, "FGS1", 4) || !memcmp(buf, "FGS0", 4))) { p += 12; n -= 12; }
        unsigned ver = p[4];
        int r = fg_box_unpack(&B, p, n);
        int same = 1;
        if (ver >= 6 && ver <= 8) {                      /* la canción v8 cruda: lo que debió quedar */
            const uint8_t *s8 = p + 8, *z8 = s8 + offsetof(fbx_song_v8_t, hand);
            fbx_song_t *s = &B.s;
            unsigned cur = s->cur;                       /* (la canción tiene el sonido del patrón que se ve: model y p de ese) */
            same &= s8[offsetof(fbx_song_v8_t, bpm)] == s->bpm; if (!same) printf("      bpm %u/%u\n", s8[offsetof(fbx_song_v8_t, bpm)], s->bpm);
            same &= !memcmp(z8 + offsetof(fbx_song_v8_t, crow) - offsetof(fbx_song_v8_t, edited), s->crow, 16); if (!same) printf("      crow\n");
            same &= !memcmp(s8 + offsetof(fbx_song_v8_t, mix), s->mix, 32) || cur;
            if (ver >= 7) for (unsigned t = 0; t < 8; t++) for (unsigned m = 0; m < FBX_NM8; m++)   /* (la v6 cambia los de SAMPLE: migrate_v6) */
                if (memcmp(z8 + (t * FBX_NM8 + m) * 8, s->edited[t][m], 8)) { same = 0; printf("      edited %u %u\n", t, m); }
            if (memcmp(z8 + offsetof(fbx_song_v8_t, has_edited) - offsetof(fbx_song_v8_t, edited), s->has_edited, 16)) { same = 0; printf("      has_edited\n"); }
            if (ver == 8) same &= !memcmp(z8 + offsetof(fbx_song_v8_t, arp2) - offsetof(fbx_song_v8_t, edited), s->arp2, 32) || cur;
        }
        uint32_t n2 = fg_box_pack(&B, buf2, sizeof buf2);
        int r2 = fg_box_unpack(&B, buf2, n2);
        uint32_t n3 = fg_box_pack(&B, again, sizeof again);
        int okk = r == 0 && same && n2 && n2 <= FBX_PROJ_MAX && r2 == 0 && n3 == n2 && !memcmp(again, buf2, n2);
        printf("%s  %-62s v%u %5u B -> v9 %5u B  modelos", okk ? "bien " : "FALLA", argv[a], ver, n, n2);
        for (unsigned t = 0; t < 8; t++) printf(" %u", B.s.model[t]);
        printf("\n");
        bad += !okk;
        run(200);
    }
    /* cada motor de cada pista suena: ALGO los recorre en su orden y cada vuelta toca la nota (parado, audition) */
    fg_box_init(&B, VMEM, 240, 240, 4, 7); run(300);
    for (unsigned t = 0; t < 8; t++) {
        while (B.sel != t) { fg_box_enc(&B, FE_SELECT, 1); run(100); }
        unsigned first = B.s.model[t], cnt = 0, mud = 0;
        do {
            fg_box_enc(&B, FE_ALGO, 1); run(30);
            double e = energy(600);
            cnt++;
            if (e < 1e6) { mud++; printf("      MUDO: %s · %s (%.3g)\n", NM8[t], fbx_model_name(t, B.s.model[t]) + 1, e); }
        } while (B.s.model[t] != first && cnt < 20);
        printf("%s  %s: %u motores, %u mudos\n", mud ? "FALLA" : "bien ", NM8[t], cnt, mud);
        bad += mud != 0;
    }
    printf(bad ? "FALLAS %d\n" : "todo bien\n", bad);
    return bad;
}
