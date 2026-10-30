/* MUESTRA (0.8) -> SAMPLE (0.9): el mismo proyecto suena parecido. Se compila dos veces:
 *   con los motores de la 0.8 (-DVIEJO): arma un proyecto con MUESTRA en el bombo y la caja, con locks, lo toca y
 *   escribe out/v6.bin y el nivel de cada golpe; con los de la 0.9: abre out/v6.bin, lo toca y compara. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fg_box.h"

static fg_box_t B;
static uint8_t VMEM[100000], BUF[16384];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }

static double rms[2][16], pk[2][16];
static void play(void)
{
    static int32_t out[2 * FG_BLOCK];
    uint32_t now = 0; double acc = 0; long smp = 0;
    fg_box_button(&B, FB_PLAY, 1); fg_box_button(&B, FB_PLAY, 0);
    long per = (long)(FG_SR * 60.0 / B.s.bpm / 4.0);            /* muestras por paso */
    double sq[2][16] = {{0}};
    long cnt[2][16] = {{0}};
    for (; now < 2000; now++) {
        fg_box_tick(&B, now);
        acc += FG_SR / 1000.0;
        while (acc >= FG_BLOCK) {
            acc -= FG_BLOCK;
            fg_box_audio(&B, out, FG_BLOCK);
            for (int i = 0; i < FG_BLOCK; i++, smp++) {
                int st = (int)((smp / per) % 16);
                for (int t = 0; t < 2; t++) { double x = B.trk[t][i]; sq[t][st] += x * x; cnt[t][st]++; if (fabs(x) > pk[t][st]) pk[t][st] = fabs(x); }
            }
        }
        fg_box_idle(&B);
    }
    for (int t = 0; t < 2; t++) for (int s = 0; s < 16; s++) rms[t][s] = cnt[t][s] ? sqrt(sq[t][s] / cnt[t][s]) : 0;
}

int main(void)
{
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    fg_box_boot(&B);
#ifdef VIEJO
    static const uint8_t K[8] = {70, 40, 90, 100, 40, 20, 40, 110}, S[8] = {64, 50, 110, 127, 20, 0, 40, 105};
    B.s.model[0] = 2; B.s.model[1] = 2; B.s.bpm = 120;
    memcpy(B.s.p[0][2], K, 8); memcpy(B.s.p[1][2], S, 8);
    for (int t = 0; t < 8; t++) for (int i = 0; i < 32; i++) B.Q.pat[0].tr[t].st[i].v = 0;
    for (int i = 0; i < 16; i += 4) B.Q.pat[0].tr[0].st[i].v = 1;
    B.Q.pat[0].tr[1].st[4].v = 1; B.Q.pat[0].tr[1].st[12].v = 1;
    B.Q.pat[0].tr[0].st[8].lk[0] = 50; B.Q.pat[0].tr[0].st[8].lk[2] = 60;    /* TONO y CAÍDA */
    B.Q.pat[0].tr[1].st[12].lk[3] = 60;                                       /* FILTRO */
    B.Q.pat[0].tr[0].len = 16; B.Q.pat[0].tr[1].len = 16;
    uint32_t n = fg_box_pack(&B, BUF, sizeof BUF);
    fg_box_unpack(&B, BUF, n);
    FILE *f = fopen("out/v6.bin", "wb"); fwrite(BUF, 1, n, f); fclose(f);
    play();
    f = fopen("out/v6.lvl", "wb"); fwrite(rms, sizeof rms, 1, f); fwrite(pk, sizeof pk, 1, f); fclose(f);
    printf("0.8: %u bytes, v%u\n", n, BUF[4]);
#else
    FILE *f = fopen("out/v6.bin", "rb"); size_t n = fread(BUF, 1, sizeof BUF, f); fclose(f);
    int r = fg_box_unpack(&B, BUF, (uint32_t)n);
    printf("0.9 abre la v6: %d  modelos %d %d  bombo %d %d %d %d %d %d %d %d  locks paso 9: %d %d %d %d\n", r, B.s.model[0], B.s.model[1],
           B.s.p[0][2][0], B.s.p[0][2][1], B.s.p[0][2][2], B.s.p[0][2][3], B.s.p[0][2][4], B.s.p[0][2][5], B.s.p[0][2][6], B.s.p[0][2][7],
           B.Q.pat[0].tr[0].st[8].lk[0], B.Q.pat[0].tr[0].st[8].lk[1], B.Q.pat[0].tr[0].st[8].lk[2], B.Q.pat[0].tr[0].st[8].lk[3]);
    printf("reloj %d\n", B.s.reloj);
    play();
    double orms[2][16], opk[2][16];
    f = fopen("out/v6.lvl", "rb"); fread(orms, sizeof orms, 1, f); fread(opk, sizeof opk, 1, f); fclose(f);
    for (int t = 0; t < 2; t++) for (int s = 0; s < 16; s++) if (orms[t][s] > 100)
        printf("%s paso %2d: RMS 0.8 %6.0f  0.9 %6.0f (%+.1f dB)  pico %6.0f %6.0f\n", t ? "caja " : "bombo", s + 1, orms[t][s], rms[t][s],
               20 * log10((rms[t][s] + 1) / (orms[t][s] + 1)), opk[t][s], pk[t][s]);
#endif
    return 0;
}
