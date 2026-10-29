/* Hoja de contacto de fg_vis (nativo): cada motor, la mezcla, los patrones y los efectos, en un PGM.
 *   cc -O2 -Isrc test/vis_sheet.c src/fg_vis.c -o /tmp/vis_sheet && /tmp/vis_sheet hoja.pgm */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fg_vis.h"
#include "fg_mix.h"

#define W 240
#define COLS 6
static unsigned char sheet[5 * W][COLS * W];
static int16_t tab[64 * 256], smp[44100];

static void shot(fg_vis_t *V, int cell)
{
    uint8_t band[FG_VIS_BAND * (W / 8)];
    int cx = (cell % COLS) * W, cy = (cell / COLS) * W;
    for (int y0 = 0; y0 < W; y0 += FG_VIS_BAND) {
        fg_vis_band(V, y0, FG_VIS_BAND, band);
        for (int r = 0; r < FG_VIS_BAND; r++) for (int x = 0; x < W; x++)
            sheet[cy + y0 + r][cx + x] = (band[r * (W / 8) + x / 8] >> (7 - x % 8)) & 1 ? 235 : (x == 0 || y0 + r == 0 ? 90 : 12);
    }
}
/* un cuadro completo, como en el aparato: frame + todas las bandas (la historia se graba en las bandas) */
static void frame(fg_vis_t *V, fg_vis_ui_t *ui)
{
    static uint8_t band[FG_VIS_BAND * 128];
    fg_vis_frame(V, ui);
    for (unsigned y0 = 0; y0 < V->h; y0 += FG_VIS_BAND) fg_vis_band(V, y0, FG_VIS_BAND, band);
}
static void run(fg_vis_t *V, fg_vis_ui_t *ui, int t, int m, const uint8_t *p, int ms)
{
    for (int i = 0; i < 8; i++) fg_vis_level(V, i, 14000);
    fg_vis_hit(V, t, m, 120, p, ui->now_ms);
    for (int k = 0; k < ms / 16; k++) { ui->now_ms += 16; frame(V, ui); }
}

int main(int argc, char **argv)
{
    for (int r = 0; r < 64; r++) for (int k = 0; k < 256; k++) tab[r * 256 + k] = (int16_t)(30000 * sin(k * 6.2832 / 256 * (1 + r / 16)) * (k < 128 ? 1 : .4));
    for (int i = 0; i < 44100; i++) smp[i] = (int16_t)((rand() % 20000 - 10000) * exp(-(i % 2756) / 400.0));
    uint32_t sz = fg_vis_size(W, W, 8);
    void *mem = malloc(sz);
    static fg_vis_t V;
    fg_vis_ui_t ui = {0};
    ui.bpm = 124; ui.queued = 255;
    uint8_t p[8] = {64, 64, 64, 64, 64, 64, 64, 0};
    const int TM[][3] = {{0, 0, 200}, {0, 1, 120}, {1, 0, 150}, {1, 1, 200}, {2, 0, 60}, {2, 1, 250}, {3, 0, 300}, {3, 1, 160},
                         {4, 0, 150}, {4, 1, 150}, {4, 2, 150}, {5, 3, 150}, {6, 4, 150}, {7, 5, 150}};
    int cell = 0;
    for (int i = 0; i < 14; i++) {
        fg_vis_init(&V, mem, W, W, 8, 1234 + i);
        uint8_t pp[8]; for (int k = 0; k < 8; k++) pp[k] = p[k];
        if (TM[i][0] == 0 && TM[i][1] == 0) { pp[4] = 90; pp[5] = 100; }
        if (TM[i][0] == 1 && TM[i][1] == 1) { pp[4] = 80; }
        run(&V, &ui, TM[i][0], TM[i][1], pp, TM[i][2]);
        shot(&V, cell++);
    }
    /* un compás con todo sonando, sin efectos y luego con cada efecto */
    const uint32_t FX[] = {0, FGP_LP, FGP_HP, FGP_BITS, FGP_THROW, FGP_RISER, FGP_STUT16, FGP_REV};
    for (int f = 0; f < 8; f++) {
        fg_vis_init(&V, mem, W, W, 8, 99);
        ui.perf = 0;
        for (int k = 0; k < 40; k++) {
            if (k % 8 == 0) { run(&V, &ui, 0, 0, p, 0); run(&V, &ui, 4, 1, p, 0); }
            if (k % 8 == 4) { run(&V, &ui, 1, 0, p, 0); run(&V, &ui, 6, 4, p, 0); }
            if (k % 2 == 1) run(&V, &ui, 2, 0, p, 0);
            if (k > 30) ui.perf = FX[f];
            ui.now_ms += 30; frame(&V, &ui);
        }
        shot(&V, cell++);
    }
    ui.perf = 0;
    ui.view = FG_VIEW_MIX; ui.sel = 2; ui.mute = 1 << 3; ui.solo = 0;
    for (int t = 0; t < 8; t++) { ui.mix[t][0] = 60 + t * 9; ui.mix[t][1] = t * 16; ui.mix[t][2] = t * 15; ui.mix[t][3] = (t & 1) * 90; fg_vis_level(&V, t, 4000 * t); }
    fg_vis_frame(&V, &ui); shot(&V, cell++);
    ui.view = FG_VIEW_PAT; ui.cur = 5; ui.queued = 9; ui.used = 0x3F; ui.now_ms = 180;
    for (int k = 0; k < 10; k++) fg_vis_frame(&V, &ui);
    shot(&V, cell++);
    /* lo mismo a 128×64 (el OLED del Move): que el tamaño configurable no se salga de la memoria */
    {
        static fg_vis_t M;
        void *mm = malloc(fg_vis_size(128, 64, 4));
        fg_vis_init(&M, mm, 128, 64, 4, 5);
        fg_vis_ui_t u2 = ui; u2.view = FG_VIEW_VIS; u2.perf = FGP_LP | FGP_HP | FGP_BITS | FGP_THROW | FGP_RISER | FGP_REV;
        uint8_t band[FG_VIS_BAND * 16];
        for (int k = 0; k < 30; k++) {
            if (k % 4 == 0) for (int t = 0; t < 8; t++) fg_vis_hit(&M, t, t < 4 ? k & 1 : k % 6, 110, p, u2.now_ms);
            u2.now_ms += 30; fg_vis_frame(&M, &u2);
            for (int y0 = 0; y0 < 64; y0 += 16) {
                fg_vis_band(&M, y0, 16, band);
                if (k == 29) for (int r = 0; r < 16; r++) for (int x = 0; x < 128; x++)
                    sheet[4 * W + 40 + y0 + r][40 + x] = (band[r * 16 + x / 8] >> (7 - x % 8)) & 1 ? 235 : 40;
            }
        }
        printf("fg_vis 128x64 con 4 cuadros: %u bytes\n", fg_vis_size(128, 64, 4));
    }
    FILE *o = fopen(argv[1], "wb");
    fprintf(o, "P5 %d %d 255\n", COLS * W, 5 * W);
    fwrite(sheet, 1, sizeof sheet, o);
    fclose(o);
    printf("fg_vis 240x240 con 8 cuadros de historia: %u bytes; struct %zu bytes\n", sz, sizeof V);
    return 0;
}
