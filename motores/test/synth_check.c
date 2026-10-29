/* Revisión sin oídos de un motor de sinte: para cada perilla, render con 0 / 64 / 127 y cuánto cambia el espectro
 * (energía en 6 bandas), el pico y el RMS. Una perilla que no mueve nada sale con dif < 3 dB.
 *   cc -O2 -Isrc test/synth_check.c src/fg_synth.c src/fg_tables.c && ./a.out MODELO [wav] */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fg_synth.h"

static fg_synth_t S;
#define N (44118 * 2)
static float buf[N];

static void render(unsigned m, const uint8_t *p)
{
    fg_synth_init(&S, 0, m);
    for (unsigned i = 0; i < 8; i++) fg_synth_param(&S, m, i, p[i]);
    int32_t out[FG_BLOCK];
    for (unsigned k = 0, at = 0; at < N; k++) {
        if (k % 64 == 0) fg_synth_trig(&S, 100, 0, 0, 44118 / 4);   /* una nota cada 186 ms, de 250 ms */
        for (int i = 0; i < FG_BLOCK; i++) out[i] = 0;
        fg_synth_render(&S, out, FG_BLOCK);
        for (int i = 0; i < FG_BLOCK && at < N; i++) buf[at++] = out[i] / 32768.0f;
    }
}
/* energía por banda con filtros de un polo en cascada (barato y suficiente para comparar) */
static void bands(double e[6])
{
    static const double fc[6] = {80, 250, 800, 2500, 6000, 12000};
    for (int b = 0; b < 6; b++) {
        double a = 1 - exp(-2 * M_PI * fc[b] / 44118), lp = 0, lp2 = 0, acc = 0;
        for (int i = 0; i < N; i++) { lp += a * (buf[i] - lp); lp2 += a * (lp - lp2); double h = buf[i] - lp2; acc += h * h; }
        e[b] = 10 * log10(acc / N + 1e-12);
    }
}
int main(int argc, char **argv)
{
    unsigned m = argc > 1 ? (unsigned)atoi(argv[1]) : 0;
    static const uint8_t DEFS[6][8] = {{30, 70, 40, 49, 30, 80, 45, 100}, {20, 35, 95, 85, 45, 0, 25, 100}, {40, 0, 50, 85, 0, 70, 0, 92},
                                       {30, 40, 70, 30, 20, 70, 0, 100}, {30, 49, 60, 50, 70, 50, 50, 95}, {40, 7, 30, 20, 60, 70, 35, 90}};
    uint8_t p[8];
    for (int i = 0; i < 8; i++) p[i] = DEFS[m][i];
    render(m, p);
    double pk = 0, rms = 0, dc = 0; for (int i = 0; i < N; i++) { double a = fabs(buf[i]); if (a > pk) pk = a; rms += buf[i] * buf[i]; dc += buf[i]; }
    printf("modelo %u: pico %.1f dBFS  rms %.1f dBFS  dc %.4f\n", m, 20 * log10(pk + 1e-9), 10 * log10(rms / N + 1e-12), dc / N);
    if (argc > 2) { FILE *f = fopen(argv[2], "wb"); for (int i = 0; i < N; i++) { short s = (short)(buf[i] * 32767); fwrite(&s, 2, 1, f); } fclose(f); }
    for (int k = 0; k < 7; k++) {
        double e0[6], e1[6]; uint8_t q[8];
        for (int i = 0; i < 8; i++) q[i] = p[i];
        q[k] = 5; render(m, q); bands(e0);
        q[k] = 122; render(m, q); bands(e1);
        double d = 0; for (int b = 0; b < 6; b++) d = fmax(d, fabs(e1[b] - e0[b]));
        printf("  perilla %d: dif máx %.1f dB %s\n", k + 1, d, d < 3 ? "<- casi no mueve" : "");
    }
    return 0;
}
