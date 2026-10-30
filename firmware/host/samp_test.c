/* SAMPLE: reversa = adelante al revés, LAZO sigue, AFINA +12 dura la mitad, las 8 pistas suenan a la vez */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fg_box.h"
#include "fg_samp.h"
static fg_box_t B;
static uint8_t VMEM[100000], BUF[16384];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int32_t A[400000], R[400000];
static long render(const uint8_t *p, int32_t *o, long max, int synth, int32_t nq)
{
    static fg_sampv_t v;
    fg_sampv_trig(&v, p, 127, synth, nq);
    long n = 0;
    while (v.on && n < max) { int32_t t[FG_BLOCK] = {0}; fg_sampv_render(&v, t, FG_BLOCK); memcpy(o + n, t, sizeof t); n += FG_BLOCK; }
    while (n > 0 && o[n - 1] == 0) n--;
    return n;
}
int main(void)
{
    int bad = 0;
    for (unsigned s = 0; s < 128; s++) if (fg_samp_valid(s)) printf("%u:%s ", s, fg_samp_at(s)->name);
    printf("\n");
    uint8_t p[8] = {64, 0, 0, 127, 64, 0, FGSV_GOLPE * 43, 64};
    long nf = render(p, A, 400000, 0, 0);
    p[6] = FGSV_REVERSA * 43;
    long nr = render(p, R, 400000, 0, 0);
    double sxy = 0, sxx = 0, syy = 0;
    long m = nf < nr ? nf : nr;
    for (long i = 64; i < m - 64; i++) { double x = A[nf - 1 - i], y = R[i]; sxy += x * y; sxx += x * x; syy += y * y; }
    double c = sxy / sqrt(sxx * syy + 1);
    printf("adelante %ld, reversa %ld muestras, parecido %.4f %s\n", nf, nr, c, c > 0.99 ? "bien" : "MAL"); bad += c <= 0.99;
    p[6] = FGSV_LAZO * 43;
    long nl = render(p, A, 300000, 0, 0);
    printf("lazo: %ld muestras (el sample: %u a %u Hz) %s\n", nl, fg_samp_at(0)->len, fg_samp_at(0)->rate, nl >= 299000 ? "sigue = bien" : "MAL"); bad += nl < 299000;
    p[6] = 0; p[0] = 76;
    long n12 = render(p, A, 400000, 0, 0);
    printf("AFINA +12: %ld muestras (%.2f de la original) %s\n", n12, (double)n12 / nf, fabs((double)n12 / nf - 0.5) < 0.03 ? "bien" : "MAL"); bad += fabs((double)n12 / nf - 0.5) >= 0.03;
    p[0] = 64; p[5] = 100;
    long nc = render(p, A, 400000, 0, 0);
    int steps = 0; for (long i = 1; i < 2000 && i < nc; i++) steps += A[i] != A[i - 1];
    printf("CRUJIDO 100: %d cambios en 2000 muestras (sin: ~2000) %s\n", steps, steps < 400 ? "bien" : "MAL"); bad += steps >= 400;
    /* las 8 pistas en SAMPLE, un golpe en cada paso */
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    fg_box_boot(&B);
    for (int t = 0; t < 8; t++) {
        B.s.model[t] = t < 4 ? 2 : FGY_SAMPLE;
        B.s.p[t][B.s.model[t]][1] = (uint8_t)(t < 2 ? t * 24 : t < 4 ? 2 + t : 48 + t);
        for (int i = 0; i < 16; i++) B.Q.pat[0].tr[t].st[i].v = 1;
    }
    uint32_t n = fg_box_pack(&B, BUF, sizeof BUF);
    printf("unpack %d\n", fg_box_unpack(&B, BUF, n));
    fg_box_button(&B, FB_PLAY, 1); fg_box_button(&B, FB_PLAY, 0);
    static int32_t out[2 * FG_BLOCK];
    double pk[8] = {0}; double acc = 0;
    for (uint32_t now = 0; now < 1500; now++) {
        fg_box_tick(&B, now); acc += FG_SR / 1000.0;
        while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, out, FG_BLOCK); for (int t = 0; t < 8; t++) for (int i = 0; i < FG_BLOCK; i++) if (fabs(B.trk[t][i]) > pk[t]) pk[t] = fabs(B.trk[t][i]); }
        fg_box_idle(&B);
    }
    for (int t = 0; t < 8; t++) { printf("pista %d pico %.0f%s\n", t + 1, pk[t], pk[t] > 1000 ? "" : "  MAL"); bad += pk[t] <= 1000; }
    printf(bad ? "FALLAS: %d\n" : "todo bien\n", bad);
    return bad;
}
