/* Cada pantalla del aparato, una captura por estado, con los textos que tiene (para la página de revisión de pantallas).
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
static const char *outdir = "out/pantallas2";
static FILE *tsv, *tsv2;
static char ctx[96];
static int caps;

int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int s_write(unsigned slot, const void *d, uint32_t n) { if (n > sizeof SLOT[0]) return -1; memcpy(SLOT[slot], d, n); SLOTN[slot] = n; return 0; }
static int s_read(unsigned slot, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[slot] || SLOTN[slot] > cap) return -1; memcpy(d, SLOT[slot], SLOTN[slot]); *n = SLOTN[slot]; return 0; }
static int s_has(unsigned slot) { return SLOTN[slot] != 0; }


/* ---- el registro: los textos del cuadro que se fotografía ---- */
typedef struct { char key[112], shown[64]; int x, y, w, h; } hit_t;
static hit_t fr[256];
static int nfr, logging;
static char grupo[64], pant[96];
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
    keyof(s0, e->key, sizeof e->key);
    snprintf(e->shown, sizeof e->shown, "%s", s);
    e->x = x; e->y = y; e->w = w; e->h = h;
    nfr++;
}
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
static void frame(void) { static uint16_t px[240 * 16]; memset(B.band_sum, 0, sizeof B.band_sum); fg_box_frame(&B); for (unsigned y = 0; y < 240; y += 16) fg_box_band(&B, y, px); }
/* la foto: grupo | pantalla | estado, con cada texto y su rectángulo */
static void snap(const char *estado)
{
    nfr = 0; logging = 1;
    memset(B.band_sum, 0, sizeof B.band_sum);
    fg_box_frame(&B);
    logging = 0;
    int n = caps++;
    save_screen(n);
    fprintf(tsv, "%d\t%s\t%s\t%s\t\t\t0\t0\t0\t0\n", n, grupo, pant, estado);
    for (int i = 0; i < nfr; i++)
        fprintf(tsv, "%d\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\t%d\n", n, grupo, pant, estado, fr[i].key, fr[i].shown, fr[i].x, fr[i].y, fr[i].w, fr[i].h);
}
static void at(const char *g, const char *p) { snprintf(grupo, sizeof grupo, "%s", g); snprintf(pant, sizeof pant, "%s", p); }

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
static void enc(unsigned r, int d) { for (int k = 0; k < (d < 0 ? -d : d); k++) { fg_box_enc(&B, r, d < 0 ? -1 : 1); run(60); } }
static void step(unsigned i) { white(i, 1); white(i, 0); }
static void calm(void) { run(3000); }
static const char *const TRN[8] = {"BOMBO", "CAJA", "METAL", "PERC", "SINTE 1", "SINTE 2", "SINTE 3", "SINTE 4"};
static const unsigned PGB[PG_N] = {FB_EDIT, FB_ENV, FB_FX, FB_SCL, FB_LFO, FB_GLO, 0, FB_ARP, 0, 0, 0};
static const unsigned NPN[PG_N] = {2, 2, 2, 2, 2, 3, 1, 2, 1, 1, 1};
static const char *const PGN[PG_N] = {"EDIT", "ENV", "FX", "SCL", "LFO", "GLO", "CADENA", "ARP", "PASO", "PATRONES", "RANURAS"};
static void track(unsigned t) { while (B.sel != t) enc(FE_SELECT, 1); run(200); }
/* las perillas de la página de ahora: una foto con cada una girada */
static void knobs(int amt)
{
    for (unsigned k = 0; k < 4; k++) {
        enc(FE_K1 + k, amt); run(150);
        char e[48]; snprintf(e, sizeof e, "gira K%u", k + 1); snap(e);
        enc(FE_K1 + k, -amt); run(1800);
    }
}
static void page(unsigned pg, const char *g, const char *extra)
{
    for (unsigned sub = 0; sub < NPN[pg]; sub++) {
        if (B.page != pg || B.pn != sub) { tap(PGB[pg]); run(100); }
        if (B.page != pg) { tap(PGB[pg]); run(100); }
        char p[96]; snprintf(p, sizeof p, "%s pág. %u%s", PGN[pg], sub + 1, extra);
        at(g, p); run(1500); snap("abierta");
        knobs(pg == PG_GLO ? 1 : 3);
    }
    tap(FB_HOME); run(1500);
}
static void ov(const char *g, const char *p, const char *e) { at(g, p); run(120); snap(e); }

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    char fn[256];
    snprintf(fn, sizeof fn, "%s/pantallas.tsv", outdir);
    tsv = fopen(fn, "w");
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    fg_box_boot(&B);
    at("Arranque y reposo", "Arranque");
    run(700); snap("escribiendo el nombre"); run(1800); snap("completo");
    run(1500); at("Arranque y reposo", "Aviso de los gestos"); snap("después del arranque");
    calm();
    tap(FB_PLAY); run(1500);
    at("Inicio", "Visual (HOME)"); snap("tocando");

    /* motores: cada pista, cada motor, EDIT con sus dos vueltas y el PRESET */
    for (unsigned t = 0; t < 5; t++) {
        track(t);
        unsigned m0 = B.s.model[t];
        for (unsigned k = 0; k < 20; k++) {
            if (k) { enc(FE_ALGO, 1); run(100); if (B.s.model[t] == m0) break; }
            char p[96]; snprintf(p, sizeof p, "%s · %s", t < 4 ? TRN[t] : "SINTE", fbx_model_name(t, B.s.model[t]) + 1);
            at("Motores", p); run(120); snap("ALGO: elige el motor"); run(1500);
            for (unsigned sub = 0; sub < 2; sub++) {
                if (B.page != PG_EDIT || B.pn != sub) { tap(FB_EDIT); run(100); }
                if (B.page != PG_EDIT) { tap(FB_EDIT); run(100); }
                run(1500); snap(sub ? "EDIT pág. 2" : "EDIT pág. 1");
                for (unsigned q = 0; q < 4; q++) {
                    enc(FE_K1 + q, 3); run(150);
                    char e[48]; snprintf(e, sizeof e, "pág. %u · gira K%u", sub + 1, q + 1); snap(e);
                    enc(FE_K1 + q, -3); run(1800);
                }
            }
            enc(FE_PRESET, 2); run(150); snap("gira PRESET"); enc(FE_PRESET, -2); run(1800);
            tap(FB_HOME); run(1500);
        }
    }
    btn(FB_HOME, 1); tap(FB_EDIT); btn(FB_HOME, 0); ov("Botones y combinaciones", "HOME + EDIT", "sonido al azar"); calm();
    /* páginas: en el bombo y en un sinte */
    track(0);
    page(PG_ENV, "Páginas", ""); page(PG_FX, "Páginas", ""); page(PG_SCL, "Páginas", " · tambor"); page(PG_LFO, "Páginas", "");
    page(PG_GLO, "Páginas", "");
    track(4);
    page(PG_SCL, "Páginas", " · sinte"); page(PG_ARP, "Páginas", "");
    track(0); tap(FB_ARP); ov("Avisos", "ARP en un tambor", "aviso"); calm(); tap(FB_HOME); run(300);
    /* SEQ y PASO */
    tap(FB_SEQ); run(300); for (unsigned i = 0; i < 16; i += 4) step(i);
    at("Secuenciador", "SEQ: pasos"); run(300); snap("las blancas son pasos");
    tap(FB_SEQ); run(1500); at("Secuenciador", "PASO"); snap("sin paso sostenido");
    white(4, 1); run(300); snap("sosteniendo un paso");
    for (unsigned q = 0; q < 4; q++) { enc(FE_K1 + q, 2); run(150); char e[48]; snprintf(e, sizeof e, "paso sostenido · gira K%u", q + 1); snap(e); enc(FE_K1 + q, -2); run(600); }
    white(4, 0); run(1500);
    black(9, 1); step(3); ov("Secuenciador", "COPIAR + paso", "copia el paso"); black(9, 0); run(1400);
    black(10, 1); run(100); ov("Secuenciador", "PEGAR", "pega"); black(10, 0); run(1500);
    black(8, 1); run(100); ov("Secuenciador", "MUTE", "sosteniendo"); black(8, 0); run(1500);
    black(9, 1); run(100); ov("Secuenciador", "COPIAR sin nada", "sosteniendo"); black(9, 0); run(1500);
    tap(FB_SEQ); run(300); tap(FB_HOME); run(1500);
    /* FX de las 16 teclas */
    btn(FB_FX, 1);
    for (unsigned i = 0; i < 16; i++) {
        if (!(B.down >> FB_FX & 1u)) btn(FB_FX, 1);
        white(i, 1); run(300);
        char p[64]; snprintf(p, sizeof p, "Tecla %u", i + 1); at("Efectos de las 16 teclas", p); snap("sosteniendo la tecla");
        for (unsigned q = 0; q < 4; q++) { enc(FE_K1 + q, 3); run(150); char e[48]; snprintf(e, sizeof e, "gira K%u", q + 1); snap(e); enc(FE_K1 + q, -3); run(300); }
        white(i, 0); run(300);
    }
    btn(FB_FX, 0); run(1500); tap(FB_HOME); run(1500);
    /* patrones */
    tap(FB_HOME); run(1500); at("Patrones y cadena", "PATRONES"); snap("abierta");
    step(2); run(300); snap("patrón 3 en la cola"); run(4000); snap("ya suena el 3");
    black(9, 1); step(1); ov("Patrones y cadena", "PATRONES", "COPIAR + patrón"); black(9, 0); run(1500);
    black(10, 1); step(6); ov("Patrones y cadena", "PATRONES", "PEGAR + patrón"); black(10, 0); run(2500);
    btn(FB_REC, 1); step(6); ov("Patrones y cadena", "PATRONES", "REC + blanca: pregunta"); step(6); ov("Patrones y cadena", "PATRONES", "REC + blanca otra vez: vacío"); btn(FB_REC, 0); run(2500);
    tap(FB_HOME); run(1500);
    btn(FB_HOME, 1); tap(FB_SEQ); btn(FB_HOME, 0); run(1500); at("Patrones y cadena", "CADENA"); snap("abierta");
    knobs(1);
    tap(FB_HOME); run(1500);
    /* ranuras */
    tap(FB_SAVE); ov("Ranuras (guardar)", "SAVE tocando", "autoguardado al parar"); run(2500); at("Ranuras (guardar)", "RANURAS"); snap("abierta");
    btn(FB_SAVE, 1); step(3); btn(FB_SAVE, 0); ov("Ranuras (guardar)", "SAVE + blanca tocando", "se guarda al parar"); run(2500);
    tap(FB_PLAY); ov("Ranuras (guardar)", "PLAY (parar)", "guarda lo pendiente"); run(2500);
    step(5); ov("Ranuras (guardar)", "Tocar una ranura vacía", "aviso"); run(2500);
    btn(FB_SAVE, 1); step(3); ov("Ranuras (guardar)", "SAVE + blanca en una ocupada", "pregunta"); step(3); btn(FB_SAVE, 0); ov("Ranuras (guardar)", "SAVE + blanca en una ocupada", "otra vez: guardado"); run(2500);
    step(3); ov("Ranuras (guardar)", "Tocar una ocupada", "pregunta"); step(3); ov("Ranuras (guardar)", "Tocar una ocupada", "otra vez: abierta"); run(2500);
    enc(FE_K1, 3); run(1500); step(3); ov("Ranuras (guardar)", "Tocar una ocupada con cambios", "cambios sin guardar"); run(4500);
    black(9, 1); step(3); ov("Ranuras (guardar)", "COPIAR + ranura", "copiada"); black(9, 0); run(1500);
    black(10, 1); step(6); ov("Ranuras (guardar)", "PEGAR + ranura", "pega"); black(10, 0); run(2500);
    btn(FB_REC, 1); step(6); ov("Ranuras (guardar)", "REC + blanca", "pregunta"); step(6); ov("Ranuras (guardar)", "REC + blanca", "otra vez: borrada"); btn(FB_REC, 0); run(2500);
    tap(FB_SAVE); run(1500); tap(FB_HOME); run(1500);
    tap(FB_SAVE); ov("Ranuras (guardar)", "SAVE parado", "autoguardado"); run(1500); tap(FB_SAVE); run(1500);
    tap(FB_SAVE); run(1500); step(7); ov("Ranuras (guardar)", "Proyecto nuevo", "ranura vacía: pregunta"); step(7); ov("Ranuras (guardar)", "Proyecto nuevo", "otra vez: de cero"); run(2500);
    tap(FB_HOME); run(1500);
    /* botones sostenidos y combinaciones */
    btn(FB_REC, 1); run(1100); ov("Botones y combinaciones", "Mantener REC", "1 segundo"); btn(FB_REC, 0); calm();
    unsigned lp[7] = {FB_EDIT, FB_SCL, FB_ENV, FB_FX, FB_LFO, FB_GLO, FB_ARP};
    const char *ln[7] = {"Mantener EDIT", "Mantener SCL", "Mantener ENV", "Mantener FX", "Mantener LFO", "Mantener GLO", "Mantener ARP"};
    for (unsigned i = 0; i < 7; i++) { if (lp[i] == FB_ARP) track(4); btn(lp[i], 1); run(1100); ov("Botones y combinaciones", ln[i], "1 segundo"); btn(lp[i], 0); calm(); tap(FB_HOME); run(300); }
    btn(FB_HOME, 1); tap(FB_SAVE); btn(FB_HOME, 0); ov("Botones y combinaciones", "HOME + SAVE", "guarda el sonido"); calm();
    btn(FB_HOME, 1); enc(FE_PRESET, 1); ov("Botones y combinaciones", "HOME + PRESET", "a la derecha"); enc(FE_PRESET, -1); ov("Botones y combinaciones", "HOME + PRESET", "a la izquierda"); btn(FB_HOME, 0); calm();
    btn(FB_HOME, 1); tap(FB_OCTDN); ov("Botones y combinaciones", "HOME + OCT−", "deshacer"); tap(FB_OCTUP); ov("Botones y combinaciones", "HOME + OCT+", "rehacer"); btn(FB_HOME, 0); calm();
    btn(FB_HOME, 1); tap(FB_SCL); ov("Botones y combinaciones", "HOME + SCL", "ritmo al azar"); btn(FB_HOME, 0); calm();
    tap(FB_OCTDN); ov("Botones y combinaciones", "OCT− / OCT+", "octava"); tap(FB_OCTUP); calm();
    /* parado: reposo y batería */
    tap(FB_HOME); run(300); track(0); tap(FB_EDIT); run(3000); at("Inicio", "Franja de ubicación"); snap("en EDIT");
    tap(FB_HOME); run(300);
    at("Arranque y reposo", "Reposo"); run(6000); snap("parado sin tocar"); run(2100); snap("otro golpe");
    B.batt = 538; run(6000); at("Avisos", "Batería baja"); snap("aviso"); run(2500);
    B.batt = 520; run(1500); at("Avisos", "Batería vacía"); snap("aviso"); B.batt = 0; run(3000);
    fclose(tsv);
    printf("%d capturas\n", caps);
    return 0;
}
