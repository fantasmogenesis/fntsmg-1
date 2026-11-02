/* Cada texto de la pantalla con la captura donde sale (para la página de revisión de textos).
 * Recorre pistas, motores, páginas, perillas de punta a punta, presets y las vistas de patrones, cadena y ranuras;
 * la primera vez que se dibuja un texto guarda la pantalla y el rectángulo. Escribe out/textos/NNN.ppm y
 * out/textos/textos.tsv (clave, como se ve, captura, x, y, w, h, qué se estaba haciendo).
 *   cc -DFG_TEXT_LOG ... textos.c (ver textos.sh) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fg_box.h"
#include "fg_lang.h"

static fg_box_t B;
static uint8_t VMEM[100000];
static uint8_t SLOT[FBX_UBANK_SLOT + 2][16384];
static uint32_t SLOTN[FBX_UBANK_SLOT + 2];
static uint32_t now_ms;
static double audio_acc;
static const char *outdir = "out/textos";
static FILE *tsv;
static char ctx[96];
static int caps;

int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int s_write(unsigned slot, const void *d, uint32_t n) { if (n > sizeof SLOT[0]) return -1; memcpy(SLOT[slot], d, n); SLOTN[slot] = n; return 0; }
static int s_read(unsigned slot, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[slot] || SLOTN[slot] > cap) return -1; memcpy(d, SLOT[slot], SLOTN[slot]); *n = SLOTN[slot]; return 0; }
static int s_has(unsigned slot) { return SLOTN[slot] != 0; }

/* ---- el registro: textos vistos (clave = el texto en español con los números como #) ---- */
#define NSEEN 8192
static char seen[NSEEN][112];
static char scope[48];                     /* las perillas y los presets se juntan por motor y perilla; lo demás, una vez */
static int nseen;
typedef struct { char key[112], shown[64]; int x, y, w, h; } hit_t;
static hit_t fr[256];
static int nfr, logging;

static void keyof(const char *s, char *k, unsigned cap)
{
    unsigned n = 0;
    while (*s && n + 1 < cap) {
        if (*s >= '0' && *s <= '9') { while (*s >= '0' && *s <= '9') s++; k[n++] = '#'; }
        else k[n++] = *s++;
    }
    k[n] = 0;
}
void fg_text_log(const char *s0, const char *s, int x, int y, int w, int h)
{
    if (!logging || nfr >= 256) return;
    hit_t *e = &fr[nfr];
    char k[64];
    keyof(s0, k, sizeof k);
    if (!k[0] || !strcmp(k, "#")) return;
    snprintf(e->key, sizeof e->key, "%s|%s", scope, k);
    snprintf(e->shown, sizeof e->shown, "%s", s);
    e->x = x; e->y = y; e->w = w; e->h = h;
    nfr++;
}
static int is_seen(const char *k) { for (int i = 0; i < nseen; i++) if (!strcmp(seen[i], k)) return 1; return 0; }

static void save_screen(int n)
{
    static uint16_t px[240 * 16];
    static uint8_t rgb[240 * 240 * 3];
    for (unsigned y = 0; y < 240; y += 16) {
        fg_box_band(&B, y, px);
        for (unsigned i = 0; i < 240u * 16u; i++) {
            uint16_t c = (uint16_t)(px[i] >> 8 | px[i] << 8);
            uint8_t *o = rgb + ((y + i / 240u) * 240u + i % 240u) * 3u;
            o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 63u) << 2); o[2] = (uint8_t)((c & 31u) << 3);
        }
    }
    char fn[256];
    snprintf(fn, sizeof fn, "%s/%03d.ppm", outdir, n);
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n240 240\n255\n"); fwrite(rgb, 1, sizeof rgb, f); fclose(f);
}
/* un cuadro: si salió algún texto nuevo, guarda la pantalla y cada texto nuevo con su rectángulo */
static void frame(void)
{
    nfr = 0; logging = 1;
    memset(B.band_sum, 0, sizeof B.band_sum);
    fg_box_frame(&B);
    logging = 0;
    int nuevo = 0;
    for (int i = 0; i < nfr; i++) if (!is_seen(fr[i].key)) nuevo = 1;
    if (!nuevo) { static uint16_t px[240 * 16]; for (unsigned y = 0; y < 240; y += 16) fg_box_band(&B, y, px); return; }
    int n = caps++;
    save_screen(n);
    for (int i = 0; i < nfr; i++) {
        if (is_seen(fr[i].key) || nseen >= NSEEN) continue;
        snprintf(seen[nseen++], sizeof seen[0], "%s", fr[i].key);
        fprintf(tsv, "%s\t%s\t%d\t%d\t%d\t%d\t%d\t%s\n", fr[i].key, fr[i].shown, n, fr[i].x, fr[i].y, fr[i].w, fr[i].h, ctx);
    }
}

static void run(uint32_t ms)
{
    static int32_t out[2 * FG_BLOCK];
    for (uint32_t k = 0; k < ms; k++) {
        now_ms++;
        fg_box_tick(&B, now_ms);
        audio_acc += FG_SR / 1000.0;
        while (audio_acc >= FG_BLOCK) { audio_acc -= FG_BLOCK; fg_box_audio(&B, out, FG_BLOCK); }
        if (now_ms % 33u == 0) frame();
        fg_box_idle(&B);
    }
}
static const uint8_t W2K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t B2K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static void btn(unsigned id, int down) { fg_box_button(&B, id, down); run(40); }
static void tap(unsigned id) { btn(id, 1); btn(id, 0); }
static void white(unsigned i, int down) { fg_box_key(&B, W2K[i], down); run(40); }
static void black(unsigned i, int down) { fg_box_key(&B, B2K[i], down); run(40); }
static void enc(unsigned r, int d) { for (int k = 0; k < (d < 0 ? -d : d); k++) { fg_box_enc(&B, r, d < 0 ? -1 : 1); run(100); } }
static void step(unsigned i) { white(i, 1); white(i, 0); }

static const char *const PGN[PG_N] = {"EDIT", "ENV", "FX", "SCL", "LFO", "GLO", "CADENA", "ARP", "PASO", "PATRONES", "RANURAS"};
static const unsigned NPN[PG_N] = {2, 2, 2, 2, 2, 2, 1, 2, 1, 1, 1};
static const unsigned PGB[PG_N] = {FB_EDIT, FB_ENV, FB_FX, FB_SCL, FB_LFO, FB_GLO, 0, FB_ARP, 0, 0, 0};
static void setctx(const char *what)
{
    snprintf(ctx, sizeof ctx, "%s\t%u\t%u\t%u\t%u", what, B.page, B.pn, B.sel, B.s.model[B.sel]);
}
/* cada perilla de la página de ahora, de abajo hasta arriba */
static void sweep_knobs(const char *what)
{
    for (unsigned k = 0; k < 4; k++) {
        char w[64]; snprintf(w, sizeof w, "%s · gira K%u", what, k + 1); setctx(w);
        int per = B.page == PG_EDIT || B.page == PG_ARP;
        snprintf(scope, sizeof scope, "%u.%u.%u.%u.K%u", B.page, B.pn, per ? (B.sel < 4 ? B.sel : 4u) : 9u, per ? B.s.model[B.sel] : 0u, k + 1);
        enc(FE_K1 + k, -70); enc(FE_K1 + k, 70);
        scope[0] = 0;
        fg_lang = FG_ES;
    }
}
/* abre la página (con sus dos vueltas) y gira todo */
static void page(unsigned pg)
{
    for (unsigned sub = 0; sub < 2; sub++) {
        if (B.page != pg || B.pn != sub) { tap(PGB[pg]); run(100); }
        if (B.page != pg) { tap(PGB[pg]); run(100); }
        char w[64]; snprintf(w, sizeof w, "%s pág. %u", PGN[pg], sub + 1); setctx(w);
        run(300);
        sweep_knobs(w);
        if (NPN[pg] < 2) break;
    }
    tap(FB_HOME); run(200);
}
static void track(unsigned t) { while (B.sel != t) enc(FE_SELECT, 1); run(200); }

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    char fn[256];
    snprintf(fn, sizeof fn, "%s/textos.tsv", outdir);
    tsv = fopen(fn, "w");
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    setctx("arranque");
    fg_box_boot(&B);
    run(3000);
    setctx("PLAY"); tap(FB_PLAY); run(1500);

    /* motores: cada pista, cada motor, EDIT (las 2 vueltas), PRESET de punta a punta y ARP en los sintes */
    for (unsigned t = 0; t < 8; t++) {
        if (t > 4) break;                                  /* (las 4 pistas de sinte dicen lo mismo) */
        track(t);
        unsigned nm = t < 4 ? 3 : FGY_MODELS;
        for (unsigned m = 0; m < nm; m++) {
            while (B.s.model[t] != m) { setctx("gira ALGO (motor)"); enc(FE_ALGO, 1); }
            run(300);
            page(PG_EDIT);
            setctx("gira PRESET"); snprintf(scope, sizeof scope, "PRESET.%u.%u", t < 4 ? t : 4u, m);
            enc(FE_PRESET, 30); enc(FE_PRESET, -30); run(300); scope[0] = 0;
            if (t >= 4) page(PG_ARP);
        }
        btn(FB_HOME, 1); tap(FB_EDIT); btn(FB_HOME, 0); setctx("HOME + EDIT (sonido al azar)"); run(600);
    }
    /* páginas que no dependen del motor, en un tambor y en un sinte */
    unsigned tt[2] = {0, 4};
    for (unsigned i = 0; i < 2; i++) {
        track(tt[i]);
        page(PG_ENV); page(PG_FX); page(PG_SCL); page(PG_LFO);
    }
    page(PG_GLO);
    /* SEQ: pasos y la página PASO con un paso sostenido */
    track(0);
    setctx("SEQ (pasos)"); tap(FB_SEQ); run(600);
    for (unsigned i = 0; i < 16; i += 4) step(i);
    setctx("SEQ otra vez (PASO)"); tap(FB_SEQ); run(600);
    setctx("PASO sin paso sostenido"); enc(FE_K1, 2); run(300);
    white(4, 1); run(200); setctx("PASO con un paso sostenido"); sweep_knobs("PASO"); white(4, 0); run(600);
    tap(FB_SEQ); run(400);
    /* patrones, cadena, ranuras, fx, copiar/pegar/mute, rec */
    tap(FB_HOME); run(200); setctx("HOME otra vez (PATRONES)"); tap(FB_HOME); run(600);
    step(2); run(400); run(4000);
    tap(FB_HOME); run(600);
    setctx("PATRONES: PEGAR sin copiar"); black(10, 1); run(200); step(5); run(300); black(10, 0); run(1500);
    setctx("PATRONES: COPIAR + patrón"); black(9, 1); run(200); step(1); run(300); black(9, 0); run(1500);
    setctx("PATRONES: PEGAR + patrón"); black(10, 1); run(200); step(6); run(300); black(10, 0); run(1500);
    tap(FB_HOME); run(600);
    btn(FB_HOME, 1); tap(FB_SEQ); btn(FB_HOME, 0); setctx("HOME + SEQ (CADENA)"); run(600);
    sweep_knobs("CADENA");
    tap(FB_HOME); run(400);
    setctx("FX: teclas de efectos"); tap(FB_FX); run(300);
    for (unsigned i = 0; i < 16; i++) {
        white(i, 1); run(300);
        for (unsigned k = 0; k < 4; k++) {
            char w[64]; snprintf(w, sizeof w, "FX: sostener la tecla %u y girar K%u", i + 1, k + 1); setctx(w);
            snprintf(scope, sizeof scope, "PERF.%u.K%u", i, k + 1);
            enc(FE_K1 + k, -3); enc(FE_K1 + k, 3); scope[0] = 0;
        }
        white(i, 0); run(200);
    }
    tap(FB_HOME); run(300);
    tap(FB_SEQ); run(400);
    setctx("COPIAR + paso"); black(9, 1); run(300); step(3); run(300); black(9, 0); run(1500);
    setctx("PEGAR"); black(10, 1); run(300); black(10, 0); run(300);
    setctx("MUTE"); black(8, 1); run(300); black(8, 0); run(300);
    setctx("COPIAR sin nada"); black(9, 1); run(300); black(9, 0); run(1500);
    tap(FB_SEQ); run(300);
    setctx("ENV: teclas (silencio y solo)"); tap(FB_ENV); run(300);
    for (unsigned i = 0; i < 2; i++) { white(i, 1); white(i, 0); run(300); white(i, 1); white(i, 0); run(300); }
    for (unsigned i = 8; i < 10; i++) { white(i, 1); white(i, 0); run(300); white(i, 1); white(i, 0); run(300); }
    tap(FB_HOME); run(300);
    setctx("ARP en un tambor"); tap(FB_ARP); run(600); enc(FE_K1, 1); run(600); tap(FB_HOME); run(300);
    setctx("SAVE (RANURAS)"); tap(FB_SAVE); run(1500);
    setctx("RANURAS: tocar una ocupada"); step(0); run(300); step(0); run(1500);
    setctx("RANURAS: vacía"); step(5); run(1500);
    tap(FB_SAVE); run(300);
    setctx("REC sostenido"); btn(FB_REC, 1); run(1200); btn(FB_REC, 0); run(400);
    setctx("mantener EDIT (sonido inicial)"); btn(FB_EDIT, 1); run(1200); btn(FB_EDIT, 0); run(600);
    setctx("mantener SCL (borrar pista)"); btn(FB_SCL, 1); run(1200); btn(FB_SCL, 0); run(600);
    setctx("mantener ENV"); btn(FB_ENV, 1); run(1200); btn(FB_ENV, 0); run(600);
    setctx("mantener FX"); btn(FB_FX, 1); run(1200); btn(FB_FX, 0); run(600);
    setctx("mantener LFO"); btn(FB_LFO, 1); run(1200); btn(FB_LFO, 0); run(600);
    setctx("mantener GLO"); btn(FB_GLO, 1); run(1200); btn(FB_GLO, 0); run(600);
    track(4);
    setctx("mantener ARP"); btn(FB_ARP, 1); run(1200); btn(FB_ARP, 0); run(600);
    setctx("HOME + SAVE (guardar sonido)"); btn(FB_HOME, 1); tap(FB_SAVE); btn(FB_HOME, 0); run(800);
    setctx("HOME + PRESET (guardar/borrar sonido)"); btn(FB_HOME, 1); enc(FE_PRESET, 1); enc(FE_PRESET, -1); btn(FB_HOME, 0); run(800);
    setctx("HOME + OCT- / OCT+ (deshacer/rehacer)"); btn(FB_HOME, 1); tap(FB_OCTDN); tap(FB_OCTUP); btn(FB_HOME, 0); run(800);
    setctx("HOME + SCL (ritmo al azar)"); btn(FB_HOME, 1); tap(FB_SCL); btn(FB_HOME, 0); run(800);
    setctx("OCT- / OCT+"); tap(FB_OCTDN); run(300); tap(FB_OCTUP); run(300);
    setctx("PLAY (parar)"); tap(FB_PLAY); run(800);
    fclose(tsv);
    printf("%d textos, %d capturas\n", nseen, caps);
    return 0;
}
