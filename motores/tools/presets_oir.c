/* Escucha los presets en la Mac: lee de stdin una línea por preset ("K|S|M|P|Y modelo p0..p7"), toca un golpe (percusión)
 * o una nota sostenida 1 s (sintes; con "3" al final, tres notas como acorde) y escribe por preset:
 *   pico_db rms_db (de sus 50 ms más fuertes) largo_ms (hasta −50 dB del pico) brillo_hz
 * Con un directorio de argumento guarda además NNN.wav (mono 44.1 kHz).
 * Lo usa motores/tools/presets.py para nivelar el NIVEL y avisar de presets mudos, que truenan o repetidos.
 *   cc -O2 -Isrc -o /tmp/presets_oir tools/presets_oir.c src/fg_{kick,snare,metal,perc,synth,smp,samples,tables}.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fg_kick.h"
#include "fg_snare.h"
#include "fg_metal.h"
#include "fg_perc.h"
#include "fg_synth.h"

#define SR 44100
#define LEN (SR * 3)
static int32_t buf[LEN];
static fg_kick_t K; static fg_snare_t SN; static fg_metal_t MT; static fg_perc_t PC; static fg_synth_t SY; static fgy_eco_t E;

static void wav(const char *fn, int n)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t ds = (uint32_t)n * 2, rs = 36 + ds, sr = SR, br = SR * 2, fl = 16; uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&rs, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fl, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f);
    for (int i = 0; i < n; i++) { int32_t v = buf[i]; if (v > 32767) v = 32767; if (v < -32767) v = -32767; int16_t s = (int16_t)v; fwrite(&s, 2, 1, f); }
    fclose(f);
}

int main(int argc, char **argv)
{
    char line[256], kind; unsigned m, p[8], idx = 0;
    while (fgets(line, sizeof line, stdin)) {
        int chord = 0;
        int n = sscanf(line, " %c %u %u %u %u %u %u %u %u %u %d", &kind, &m, &p[0], &p[1], &p[2], &p[3], &p[4], &p[5], &p[6], &p[7], &chord);
        if (n < 10) continue;
        memset(buf, 0, sizeof buf);
        uint8_t lk[8]; memset(lk, 255, 8);
        int synth = kind == 'Y', len = synth ? LEN : SR * 2;
        if (kind == 'K') { fg_kick_init(&K); fg_kick_model(&K, m); for (int i = 0; i < 8; i++) fg_kick_param(&K, m, i, p[i]); fg_kick_trig(&K, 110, lk, 0); }
        if (kind == 'S') { fg_snare_init(&SN); fg_snare_model(&SN, m); for (int i = 0; i < 8; i++) fg_snare_param(&SN, m, i, p[i]); fg_snare_trig(&SN, 110, lk, 0); }
        if (kind == 'M') { fg_metal_init(&MT); fg_metal_model(&MT, m); for (int i = 0; i < 8; i++) fg_metal_param(&MT, m, i, p[i]); fg_metal_trig(&MT, 110, lk, 0); }
        if (kind == 'P') { fg_perc_init(&PC); fg_perc_model(&PC, m); for (int i = 0; i < 8; i++) fg_perc_param(&PC, m, i, p[i]); fg_perc_trig(&PC, 110, lk, 0); }
        if (synth) {
            fg_synth_init(&SY, 0, m); SY.maxv = 4; SY.eco = &E; memset(&E, 0, sizeof E); SY.step = 5337;   /* 124 BPM */
            for (int i = 0; i < 8; i++) fg_synth_param(&SY, m, i, p[i]);
            fg_synth_trig(&SY, 110, lk, 0, FGY_HOLD, 1);
        }
        for (int b = 0; b * FG_BLOCK < len; b++) {
            if (synth && chord && b == 8) { lk[0] = (uint8_t)(p[0] + 4 < 128 ? p[0] + 4 : p[0]); fg_synth_trig(&SY, 100, lk, 1, FGY_HOLD, 2); }
            if (synth && chord && b == 16) { lk[0] = (uint8_t)(p[0] + 7 < 128 ? p[0] + 7 : p[0]); fg_synth_trig(&SY, 100, lk, 1, FGY_HOLD, 3); }
            if (synth && b == SR / FG_BLOCK) { fg_synth_release(&SY, 1); fg_synth_release(&SY, 2); fg_synth_release(&SY, 3); }
            int32_t *o = buf + b * FG_BLOCK;
            if (kind == 'K') fg_kick_render(&K, o, FG_BLOCK);
            if (kind == 'S') fg_snare_render(&SN, o, FG_BLOCK);
            if (kind == 'M') fg_metal_render(&MT, o, FG_BLOCK);
            if (kind == 'P') fg_perc_render(&PC, o, FG_BLOCK);
            if (synth) fg_synth_render(&SY, o, FG_BLOCK);
        }
        double pk = 0, best = 0, cross = 0; int W = SR / 20, a0 = 0;            /* lo fuerte = los 50 ms más fuertes */
        for (int i = 0; i < len; i++) { double a = fabs((double)buf[i]); if (a > pk) pk = a; }
        for (int s0 = 0; s0 + W <= len; s0 += W / 2) {
            double ss = 0; for (int i = s0; i < s0 + W; i++) ss += (double)buf[i] * buf[i];
            if (ss > best) { best = ss; a0 = s0; }
        }
        int a1 = a0 + W;
        for (int i = a0 + 1; i < a1; i++) if ((buf[i - 1] < 0) != (buf[i] < 0)) cross++;   /* cruces por cero: idea del brillo */
        int end = 0; double th = pk * 0.00316;                                                 /* −50 dB del pico */
        for (int i = 0; i < len; i++) if (fabs((double)buf[i]) > th) end = i;
        printf("%u %.1f %.1f %d %.0f\n", idx, 20 * log10(pk / 32768 + 1e-9), 10 * log10(best / W / (32768.0 * 32768) + 1e-12),
               end * 1000 / SR, cross / 2 / ((double)(a1 - a0) / SR));
        if (argc > 1) { char fn[512]; snprintf(fn, sizeof fn, "%s/%03u.wav", argv[1], idx); wav(fn, len); }
        idx++;
    }
    return 0;
}
