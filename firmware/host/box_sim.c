/* La caja en la Mac: el mismo C del FM-1 con un guion de botones, perillas y teclas; guarda capturas de la
 * pantalla (PPM 240×240) y el audio (WAV), y revisa el guardado (empacar -> abrir -> mismo proyecto).
 *   firmware/host/build.sh   (escribe firmware/host/out/) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fg_box.h"
#include "fg_lang.h"

static fg_box_t B;
static uint8_t VMEM[100000];
static uint8_t SLOT[FBX_UBANK_SLOT + 2][16384];
static uint32_t SLOTN[FBX_UBANK_SLOT + 2];
static FILE *wav;
static uint32_t frames, now_ms;
static double audio_acc;
static int shots;
static const char *outdir = "out";

int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int s_write(unsigned slot, const void *d, uint32_t n) { if (n > sizeof SLOT[0]) return -1; memcpy(SLOT[slot], d, n); SLOTN[slot] = n; return 0; }
static int s_read(unsigned slot, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[slot] || SLOTN[slot] > cap) return -1; memcpy(d, SLOT[slot], SLOTN[slot]); *n = SLOTN[slot]; return 0; }
static int s_has(unsigned slot) { return SLOTN[slot] != 0; }

static void wav_hdr(uint32_t n)
{
    uint32_t sr = FG_SR, br = sr * 4, ds = n * 4, rs = 36 + ds, fl = 16; uint16_t ch = 2, bits = 16, ba = 4, fmt = 1;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav); fwrite(&rs, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav); fwrite(&fl, 4, 1, wav);
    fwrite(&fmt, 2, 1, wav); fwrite(&ch, 2, 1, wav); fwrite(&sr, 4, 1, wav); fwrite(&br, 4, 1, wav);
    fwrite(&ba, 2, 1, wav); fwrite(&bits, 2, 1, wav); fwrite("data", 1, 4, wav); fwrite(&ds, 4, 1, wav);
    fseek(wav, 0, SEEK_END);
}

/* avanza el tiempo: audio cada 128 muestras, interfaz cada ms, pantalla cada 33 ms */
static void run(uint32_t ms)
{
    static int32_t out[2 * FG_BLOCK];
    static uint16_t px[240 * 16];
    for (uint32_t k = 0; k < ms; k++) {
        now_ms++;
        fg_box_tick(&B, now_ms);
        audio_acc += FG_SR / 1000.0;
        while (audio_acc >= FG_BLOCK) {
            audio_acc -= FG_BLOCK;
            fg_box_audio(&B, out, FG_BLOCK);
            int16_t s[2 * FG_BLOCK];
            for (int i = 0; i < 2 * FG_BLOCK; i++) s[i] = (int16_t)out[i];
            fwrite(s, 2, 2 * FG_BLOCK, wav);
            frames += FG_BLOCK;
        }
        if (now_ms % 33u == 0) { fg_box_frame(&B); for (unsigned y = 0; y < 240; y += 16) fg_box_band(&B, y, px); }
        fg_box_idle(&B);
    }
}
static void shot(const char *name)
{
    static uint16_t px[240 * 16];
    static uint8_t rgb[240 * 240 * 3];
    memset(B.band_sum, 0, sizeof B.band_sum);            /* (todas las bandas: las que no cambiaron no se pintan) */
    fg_box_frame(&B);
    for (unsigned y = 0; y < 240; y += 16) {
        fg_box_band(&B, y, px);
        for (unsigned i = 0; i < 240u * 16u; i++) {
            uint16_t c = (uint16_t)(px[i] >> 8 | px[i] << 8);
            uint8_t *o = rgb + ((y + i / 240u) * 240u + i % 240u) * 3u;
            o[0] = (uint8_t)((c >> 11) << 3); o[1] = (uint8_t)(((c >> 5) & 63u) << 2); o[2] = (uint8_t)((c & 31u) << 3);
        }
    }
    char fn[256];
    snprintf(fn, sizeof fn, "%s/%02d-%s.ppm", outdir, shots++, name);
    FILE *f = fopen(fn, "wb");
    fprintf(f, "P6\n240 240\n255\n"); fwrite(rgb, 1, sizeof rgb, f); fclose(f);
    uint32_t bt, ky;
    fg_box_leds(&B, &bt, &ky);
    printf("%-14s botones %04x teclas %07x  página %d  sel %d  patrón %d\n", name, bt, ky, B.page, B.sel, B.s.cur);
}

/* blanca i (0..15) y negra i (0..10) -> tecla física */
static const uint8_t W2K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t B2K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static void btn(unsigned id, int down) { fg_box_button(&B, id, down); run(20); }
static void tap(unsigned id) { btn(id, 1); btn(id, 0); }
static void white(unsigned i, int down) { fg_box_key(&B, W2K[i], down); run(20); }
static void black(unsigned i, int down) { fg_box_key(&B, B2K[i], down); run(20); }
static void enc(unsigned r, int d) { for (int k = 0; k < (d < 0 ? -d : d); k++) { fg_box_enc(&B, r, d < 0 ? -1 : 1); run(80); } }

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    char fn[256];
    snprintf(fn, sizeof fn, "%s/box.wav", outdir);
    wav = fopen(fn, "wb");
    wav_hdr(0);
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    if (fg_vis_size(240, 240, 4) > sizeof VMEM) { printf("VMEM chica: %u\n", fg_vis_size(240, 240, 4)); return 1; }
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    fg_box_boot(&B);
    static const uint32_t IT[7] = {200, 600, 900, 1200, 1500, 2100, 2700};
    for (unsigned k = 0, t = 0; k < 7; k++) { run(IT[k] - t); t = IT[k]; char nm[16]; snprintf(nm, sizeof nm, "arranque-%u", IT[k]); shot(nm); }
    run(300);
    shot("inicio");
    tap(FB_PLAY); run(2000); shot("tocando");
    enc(FE_K1, 6); shot("perilla");
    tap(FB_EDIT); run(100); shot("nombres-pag2");
    tap(FB_EDIT); run(1500);
    enc(FE_SELECT, 4); run(50); shot("pista-grande");
    run(1500);
    white(2, 1); enc(FE_K2, 5); shot("lock"); white(2, 0); run(400);
    tap(FB_ENV); run(1500); shot("mezcla");
    enc(FE_K1, -8); shot("mezcla-vol");
    white(0, 1); white(0, 0); run(300); shot("mezcla-mute");
    tap(FB_EDIT); run(1500);
    btn(FB_HOME, 1); run(500); shot("patrones"); btn(FB_HOME, 0); run(300);
    btn(FB_ARP, 1); white(4, 1); enc(FE_K3, 2); shot("paso-arp"); white(4, 0); btn(FB_ARP, 0); run(300);
    btn(FB_FX, 1); white(8, 1); run(600); shot("fx-filtro"); white(8, 0); btn(FB_FX, 0); run(500);
    enc(FE_ALGO, 1); shot("motor");
    enc(FE_PRESET, 2); shot("preset");
    tap(FB_GLO); run(80); enc(FE_K1, 10); shot("tempo");
    btn(FB_REC, 1); btn(FB_REC, 0); run(200); shot("rec");
    btn(FB_REC, 1); btn(FB_REC, 0);
    run(4000); shot("tocando-2");
    /* paneles de las negras: MUTE, COPIA, FILL */
    black(8, 1); black(1, 1); black(1, 0); black(5, 1); black(5, 0); run(100); shot("panel-mute"); black(8, 0);
    black(1, 1); black(1, 0); black(5, 1); black(5, 0); run(100);
    black(10, 1); run(100); shot("panel-copia"); white(3, 1); white(3, 0); run(100); shot("copiado"); white(6, 1); white(6, 0); run(100); shot("pegado"); black(10, 0);
    black(9, 1); run(100); shot("fill"); black(9, 0); run(100);
    /* CADENA: tocar HOME, filas con K1/K2, prender con K4, mantener HOME vacía el patrón */
    tap(FB_HOME); run(100); shot("cadena");
    white(1, 1); white(1, 0); enc(FE_K1, 4); enc(FE_K2, 2); run(50); shot("cadena-filas");
    white(3, 1); white(3, 0); enc(FE_K1, 2); run(1500); shot("cadena-suena");
    printf("cadena: filas %d %d %d %d, veces %d, sí %d, en el secuenciador %d\n", B.s.crow[0], B.s.crow[1], B.s.crow[2], B.s.crow[3], B.s.crep[1], B.s.chain_on, B.Q.nchain);
    enc(FE_K4, -4); run(50); printf("cadena apagada: sí %d, secuenciador %d\n", B.s.chain_on, B.Q.nchain);
    btn(FB_HOME, 1); run(600); shot("mantener-home"); run(500); shot("patron-vacio"); btn(FB_HOME, 0); run(50);
    printf("después de vaciar: página %d (sigue en CADENA = %d)\n", B.page, B.page == PG_CHAIN);
    tap(FB_HOME); run(100);
    /* azar: SEL + SEQ patrón, SEL + EDIT sonido; SAVE + OCT – lo deshace */
    black(0, 1); black(0, 0);
    btn(FB_SCL, 1); tap(FB_SEQ); run(50); shot("ritmo-azar"); btn(FB_SCL, 0); run(50);
    black(4, 1); black(4, 0); run(1000);
    uint8_t p0[8]; memcpy(p0, B.s.p[4][B.s.model[4]], 8);
    btn(FB_SCL, 1); tap(FB_EDIT); run(50); shot("sonido-azar"); btn(FB_SCL, 0); run(1000);
    printf("sonido al azar: %s · %s\n", B.ov.label, B.ov.value);
    btn(FB_SAVE, 1); tap(FB_OCTDN); btn(FB_SAVE, 0); run(50);
    printf("sonido al azar y deshacer: %s\n", memcmp(p0, B.s.p[4][B.s.model[4]], 8) ? "DISTINTO" : "IGUAL");
    enc(FE_K2, 6); btn(FB_EDIT, 1); run(1100); shot("sonido-inicial"); btn(FB_EDIT, 0); run(50);
    btn(FB_SCL, 1); run(1100); shot("pista-vacia"); btn(FB_SCL, 0); run(50);
    /* MIDI: notas que entran (canal 5 = SINTE A) y que salen */
    B.s.mnotes = 3; fg_box_midi_in(&B, 0x94, 67, 100); run(100);
    btn(FB_HOME, 1); white(0, 1); white(0, 0); btn(FB_HOME, 0);
    B.mnotes_out = 1; if (!B.playing) tap(FB_PLAY); run(1000); { uint32_t pk; int k = 0; while (fg_box_midi_out(&B, &pk)) k++; printf("MIDI salen %d paquetes en 1 s (tocando %d, patrón %d, mutes %02x, solo %02x, pasos bombo %d)\n", k, B.Q.playing, B.Q.cur, B.mute, B.solo, B.Q.pat[B.Q.cur].tr[0].st[0].v); } tap(FB_PLAY);
    /* ---- lo de esta tanda ---- */
    if (B.playing) tap(FB_PLAY);
    btn(FB_HOME, 1); white(11, 1); white(11, 0); btn(FB_HOME, 0); run(100);
    printf("patrón 12: en cola %d (16 = sí hay), aviso %s = %s\n", B.queued + 1, B.ov.label, B.ov.value);
    tap(FB_HOME); run(50); tap(FB_HOME); run(50);                     /* (sale de la vista CADENA si entró) */
    black(5, 1); black(5, 0);                                         /* sinte B */
    tap(FB_ENV); tap(FB_ENV); run(50); enc(FE_K1, -6); shot("env-pag2-filtro"); printf("ENV pág 2: %s = %s\n", B.ov.label, B.ov.value);
    enc(FE_K4, 4); shot("env-pag2-sidechain"); printf("ENV pág 2: %s = %s\n", B.ov.label, B.ov.value);
    tap(FB_SCL); tap(FB_SCL); run(50); enc(FE_K1, 1); printf("SCL pág 2 K1: %s = %s\n", B.ov.label, B.ov.value); enc(FE_K2, 4); printf("SCL pág 2 K2: %s = %s\n", B.ov.label, B.ov.value);
    tap(FB_LFO); tap(FB_LFO); run(50); enc(FE_K3, 6); shot("lfo-pag2"); printf("LFO pág 2: %s = %s\n", B.ov.label, B.ov.value);
    btn(FB_ENV, 1); run(1100); printf("ENV 1 s: %s = %s (filtro %d)\n", B.ov.label, B.ov.value, B.s.mix2[5][0]); btn(FB_ENV, 0); run(50);
    btn(FB_GLO, 1); run(1100); printf("GLO 1 s: %s = %s\n", B.ov.label, B.ov.value); btn(FB_GLO, 0); run(50);
    /* deshacer: mover una perilla, esperar, deshacer, rehacer */
    tap(FB_EDIT); run(50); { int v0 = B.s.p[5][B.s.model[5]][1]; enc(FE_K2, 8); run(900); int v1 = B.s.p[5][B.s.model[5]][1];
      btn(FB_SAVE, 1); tap(FB_OCTDN); int v2 = B.s.p[5][B.s.model[5]][1]; shot("deshacer"); tap(FB_OCTUP); int v3 = B.s.p[5][B.s.model[5]][1]; btn(FB_SAVE, 0); run(50);
      printf("deshacer: %d -> %d, deshacer %d, rehacer %d (%s)\n", v0, v1, v2, v3, v0 == v2 && v1 == v3 ? "BIEN" : "MAL"); }
    /* motor nuevo + polifonía: CUERDAS con tres teclas */
    { unsigned m = B.s.model[5]; while (B.s.model[5] != FGY_CUERDAS) enc(FE_ALGO, 1); (void)m; }
    shot("cuerdas");
    if (B.seqmode) tap(FB_SEQ);
    white(0, 1); white(2, 1); white(4, 1); run(300);
    { int on = 0; for (int k = 0; k < FGY_VOICES; k++) on += B.SY[1].v[k].on; printf("CUERDAS, tres teclas sostenidas: %d voces sonando\n", on); }
    white(0, 0); white(2, 0); white(4, 0); run(200);
    { int g = 0; for (int k = 0; k < FGY_VOICES; k++) g += B.SY[1].v[k].on && B.SY[1].v[k].gate; printf("al soltar: %d voces todavía sostenidas (0 = bien)\n", g); }
    /* arpegiador: tocar ARP, tres teclas, contar notas */
    tap(FB_ARP); printf("ARP: %s = %s\n", B.ov.label, B.ov.value);
    btn(FB_ARP, 1); enc(FE_K2, 2); printf("ARP + K2: %s = %s\n", B.ov.label, B.ov.value); btn(FB_ARP, 0); run(50);
    B.mnotes_out = 1; { uint32_t pk; while (fg_box_midi_out(&B, &pk)) { } }
    white(0, 1); white(2, 1); white(4, 1); run(2000);
    { uint32_t pk; int k = 0; while (fg_box_midi_out(&B, &pk)) if ((pk & 0xF0FF) == 0x9009 && ((pk >> 8) & 15) == 5) k++; printf("arpegio parado, 2 s a %d BPM: %d notas del sinte B\n", B.s.bpm, k); }
    white(0, 0); white(2, 0); white(4, 0); run(100);
    tap(FB_SEQ); B.mnotes_out = 0;
    /* efectos con perillas y perillas que mueven el visual */
    if (!B.playing) tap(FB_PLAY);
    run(1500);
    btn(FB_FX, 1); white(8, 1); btn(FB_FX, 0); white(8, 0); run(100);   /* FILTRO GRAVE enganchado */
    enc(FE_K1, 6); printf("efecto enganchado + K1: %s = %s\n", B.ov.label, B.ov.value); enc(FE_K2, 4); printf("+ K2: %s = %s\n", B.ov.label, B.ov.value);
    btn(FB_FX, 1); btn(FB_FX, 0); btn(FB_FX, 1); btn(FB_FX, 0); run(100);   /* doble FX: se suelta */
    for (int k = 0; k < 6; k++) { fg_box_enc(&B, FE_K1, 1); fg_box_enc(&B, FE_K3, 1); run(15); }
    shot("perillas-visual");
    run(1500);
    /* guardado: empacar, abrir en una caja nueva, comparar */
    btn(FB_SAVE, 1); white(3, 1); white(3, 0); btn(FB_SAVE, 0); run(200); shot("guardado");
    static uint8_t p1[16384], p2[16384];
    uint32_t n1 = fg_box_pack(&B, p1, sizeof p1);
    static fg_box_t C;
    static uint8_t VM2[100000];
    fg_box_init(&C, VM2, 240, 240, 4, 99);
    int rc = fg_box_unpack(&C, p1, n1);
    uint32_t n2 = fg_box_pack(&C, p2, sizeof p2);
    printf("guardado: %u bytes, abrir %d, otra vez %u bytes, %s\n", n1, rc, n2, n1 == n2 && !memcmp(p1, p2, n1) ? "IGUAL" : "DISTINTO");
    /* banco de sonidos: guardar dos, apagar y prender, guardar otro, borrar, y los de un proyecto viejo pasan al banco */
    if (B.playing) tap(FB_PLAY);
    run(100);
#define UBANK() (!SLOTN[FBX_UBANK_SLOT] ? 0 : (SLOT[FBX_UBANK_SLOT][0] & 0x80) ? SLOT[FBX_UBANK_SLOT][1] : SLOT[FBX_UBANK_SLOT][0])
    btn(FB_SAVE, 1); enc(FE_PRESET, 1); btn(FB_SAVE, 0); run(50); printf("guardar sonido: %s %s, en la memoria %d\n", B.ov.label, B.ov.value, UBANK());
    enc(FE_K2, 9); run(50);
    btn(FB_SAVE, 1); enc(FE_PRESET, 1); btn(FB_SAVE, 0); run(50); shot("sonido-guardado"); printf("otro: %s, en la memoria %d\n", B.ov.value, UBANK());
    fg_box_init(&B, VMEM, 240, 240, 4, 1234); fg_box_boot(&B); run(3500);
    btn(FB_SAVE, 1); enc(FE_PRESET, 1); btn(FB_SAVE, 0); run(50); printf("después de prender, otro: %s, en la memoria %d (3 = bien)\n", B.ov.value, UBANK());
    btn(FB_SAVE, 1); enc(FE_PRESET, -1); btn(FB_SAVE, 0); run(50); shot("sonido-borrado"); printf("borrar el que se ve: %s %s, en la memoria %d (2 = bien)\n", B.ov.label, B.ov.value, UBANK());
    tap(FB_EDIT); run(50); enc(FE_PRESET, 1); enc(FE_PRESET, 1); btn(FB_SAVE, 1); enc(FE_PRESET, -1); btn(FB_SAVE, 0); run(50); printf("borrar uno de fábrica: %s, en la memoria %d (2 = bien)\n", B.ov.value, UBANK());
    B.nupre = 1; B.upre[0] = (fbx_upre_t){4, 0, "VIEJO 1", {10, 20, 30, 40, 50, 60, 70, 80}};
    n1 = fg_box_pack(&B, p1, sizeof p1); rc = fg_box_unpack(&B, p1, n1); run(50);
    printf("proyecto viejo con un sonido adentro: abrir %d, en la memoria %d (3 = bien), en el proyecto %d (0 = bien)\n", rc, UBANK(), B.nupre);
    /* mezcla: pasar de página y mover FILTRO / ANCHO; ¿se queda algo encima? */
    if (!B.seqmode) tap(FB_SEQ);
    if (B.page != PG_ENV) tap(FB_ENV);
    run(1500); shot("mx-1");
    tap(FB_ENV); run(200); shot("mx-pag2-nombres");
    enc(FE_K1, -3); run(100); shot("mx-filtro");
    enc(FE_K2, 3); run(100); shot("mx-ancho");
    run(1500); shot("mx-limpia");
    tap(FB_ENV); run(200); enc(FE_K3, 2); tap(FB_ENV); run(100); enc(FE_K2, 2); run(1500); shot("mx-limpia-2");
    printf("mezcla: ov %d nombres %d grande %d (0 = nada encima)\n", B.ov.until > B.now, B.names_until > B.now, B.big_until > B.now);
    black(3, 1); black(3, 0); run(100); shot("mx-pista");               /* elegir pista en la mezcla: la tarjeta va arriba */
    run(1500);
    tap(FB_SEQ); run(50); printf("SEQ desde la mezcla: modo %s, página %s\n", B.seqmode ? "SEQ" : "TOCAR", B.page == PG_EDIT ? "EDIT (bien)" : "otra");
    tap(FB_ENV); run(50); tap(FB_SEQ); run(50); printf("SEQ otra vez desde la mezcla: modo %s, página %s\n", B.seqmode ? "SEQ" : "TOCAR", B.page == PG_EDIT ? "EDIT (bien)" : "otra");
    if (!B.seqmode) tap(FB_SEQ);
    if (B.page != PG_ENV) tap(FB_ENV);
    run(1500);
    /* SEL + SEQ: ritmo al azar (con el largo de la pista); SEL + EDIT: sonido al azar; EDIT + SAVE: guardar el sonido */
    {
        if (B.playing) tap(FB_PLAY);
        tap(FB_EDIT); run(50);
        unsigned pg0 = B.page, md0 = B.seqmode; int ub0 = UBANK();
        for (int r = 0; r < 3; r++) {
            btn(FB_SCL, 1); btn(FB_SEQ, 1); btn(FB_SEQ, 0); btn(FB_SCL, 0); run(50);
            const fg_track_t *tr = &B.Q.pat[B.s.cur].tr[B.sel]; int n = 0;
            for (unsigned s = 0; s < tr->len; s++) n += tr->st[s].v != 0;
            printf("SEL + SEQ %d: %s %s, largo %d, %d pasos con nota\n", r + 1, B.ov.label, B.ov.value, tr->len, n);
        }
        shot("azar-ritmo");
        printf("  página %s, modo %s (igual = bien)\n", B.page == pg0 ? "igual" : "CAMBIÓ", B.seqmode == md0 ? "igual" : "CAMBIÓ");
        uint8_t p0[8]; for (int j = 0; j < 8; j++) p0[j] = B.s.p[B.sel][B.s.model[B.sel]][j];
        btn(FB_SCL, 1); btn(FB_EDIT, 1); btn(FB_EDIT, 0); btn(FB_SCL, 0); run(50);
        int ch = 0; for (int j = 0; j < 8; j++) ch += p0[j] != B.s.p[B.sel][B.s.model[B.sel]][j];
        printf("SEL + EDIT: %s, %d perillas cambiaron, página %s\n", B.ov.label, ch, B.page == pg0 ? "igual" : "CAMBIÓ");
        shot("azar-sonido");
        btn(FB_EDIT, 1); btn(FB_SAVE, 1); btn(FB_SAVE, 0); btn(FB_EDIT, 0); run(50);
        printf("EDIT + SAVE: %s %s, banco %d -> %d\n", B.ov.label, B.ov.value, ub0, UBANK());
        btn(FB_SAVE, 1); btn(FB_EDIT, 1); btn(FB_EDIT, 0); btn(FB_SAVE, 0); run(50);
        printf("SAVE + EDIT: %s %s, banco %d\n", B.ov.label, B.ov.value, UBANK());
        btn(FB_SCL, 1); run(500); shot("ayuda-sel"); btn(FB_SCL, 0); run(50);
        btn(FB_EDIT, 1); run(500); shot("ayuda-edit-2"); btn(FB_EDIT, 0); run(50);
        printf("LEDs a media luz: %04x (páginas sin elegir)\n", fg_box_leds_half(&B));
    }
    /* SAVE + blanca: vacía guarda; llena con un toque no toca nada, doble abre, mantener guarda encima */
    if (B.playing) tap(FB_PLAY);
    { uint8_t cp[16384]; int p0 = B.s.p[5][B.s.model[5]][1];
      btn(FB_SAVE, 1); white(12, 1); white(12, 0); btn(FB_SAVE, 0); run(100);
      printf("ranura 13 vacía: %s %s\n", B.ov.label, B.ov.value);
      memcpy(cp, SLOT[12], SLOTN[12]); uint32_t n0 = SLOTN[12];
      while (B.sel != 5) enc(FE_SELECT, 1);
      tap(FB_EDIT); enc(FE_K2, 8); int p1 = B.s.p[5][B.s.model[5]][1];
      btn(FB_SAVE, 1); white(12, 1); white(12, 0); run(600); btn(FB_SAVE, 0); run(100);
      printf("un toque en llena: %s · %s, ranura %s (IGUAL = bien)\n", B.ov.label, B.ov.value, n0 == SLOTN[12] && !memcmp(cp, SLOT[12], n0) ? "IGUAL" : "CAMBIÓ");
      btn(FB_SAVE, 1); white(12, 1); white(12, 0); white(12, 1); white(12, 0); btn(FB_SAVE, 0); run(100);
      int p2 = B.s.p[5][B.s.model[5]][1];
      printf("doble toque abre: perilla %d -> %d -> %d (regresa = bien), ranura %s\n", p0, p1, p2, n0 == SLOTN[12] && !memcmp(cp, SLOT[12], n0) ? "IGUAL" : "CAMBIÓ");
      enc(FE_K2, 8);
      btn(FB_SAVE, 1); white(12, 1); run(600); shot("guardar-encima"); run(500); white(12, 0); btn(FB_SAVE, 0); run(100);
      printf("mantener: %s %s, ranura %s (CAMBIÓ = bien)\n", B.ov.label, B.ov.value, n0 == SLOTN[12] && !memcmp(cp, SLOT[12], n0) ? "IGUAL" : "CAMBIÓ");
      btn(FB_SAVE, 1); run(400); shot("ayuda-save-2"); btn(FB_SAVE, 0); run(50); }
    while (B.sel != 1) enc(FE_SELECT, 1);
    { int m = B.s.model[1]; while (B.s.model[1] != 2) enc(FE_ALGO, 1); enc(FE_SELECT, 1); enc(FE_SELECT, -1); run(50); shot("pista-muestra"); while (B.s.model[1] != m) enc(FE_ALGO, 1); }
    /* mantener: los paneles de ayuda */
    btn(FB_EDIT, 1); run(600); shot("ayuda-edit"); btn(FB_EDIT, 0); run(50);
    btn(FB_SAVE, 1); run(400); shot("ayuda-save"); enc(FE_PRESET, -1); btn(FB_SAVE, 0); run(1500);
    /* arpegio grabado: pista 5 en TOCAR, ARP, REC y PLAY; tres teclas un compás */
    if (B.playing) tap(FB_PLAY);
    while (B.sel != 4) enc(FE_SELECT, 1);
    if (!B.seqmode) tap(FB_SEQ);
    btn(FB_SCL, 1); run(1100); btn(FB_SCL, 0); run(50);                /* pista vacía */
    if ((B.s.arp_on >> 4) & 1u) tap(FB_ARP);
    tap(FB_ARP); printf("prender ARP en SEQ: %s, modo %s (TOCAR = bien)\n", B.ov.value, B.seqmode ? "SEQ" : "TOCAR");
    btn(FB_ARP, 1); run(600); shot("ayuda-arp"); run(500); btn(FB_ARP, 0); run(50);
    printf("ARP mantener: %s %s (sostener %d)\n", B.ov.label, B.ov.value, B.s.arp[4][3]);
    btn(FB_ARP, 1); run(1100); btn(FB_ARP, 0); run(50);              /* otra vez: suelto */
    printf("ARP mantener otra vez: %s (sostener %d)\n", B.ov.value, B.s.arp[4][3]);
    tap(FB_REC); tap(FB_PLAY); run(300);
    white(0, 1); white(2, 1); white(4, 1); run(2600); white(0, 0); white(2, 0); white(4, 0);
    run(300); tap(FB_REC); run(200);
    { const fg_track_t *tr = &B.Q.pat[B.s.cur].tr[4]; int nv = 0; printf("arp grabado (len %d):", tr->len);
      for (int s = 0; s < tr->len; s++) { if (tr->st[s].v) nv++; printf(" %d", tr->st[s].v ? tr->st[s].lk[0] : 0); } printf("  -> %d pasos\n", nv); }
    tap(FB_PLAY); run(100);
    tap(FB_PLAY); run(500);
    /* 0.9: SAMPLE en la caja, la perilla SAMPLE y la página 2 de GLO (RELOJ, IDIOMA), en los tres idiomas */
    while (B.sel != 1) enc(FE_SELECT, 1);
    if (B.seqmode) tap(FB_SEQ);
    while (B.s.model[1] != 2) enc(FE_ALGO, 1);
    run(1500); shot("sample-motor");
    tap(FB_EDIT); run(1500); enc(FE_K2, 2); shot("sample-perilla");
    enc(FE_K1, 3); shot("sample-afina");
    for (int lg = 0; lg < 3; lg++) {
        static const char *const LN[3] = {"es", "en", "pt"};
        char nm[32];
        fg_lang = (uint8_t)lg;
        tap(FB_GLO); if (B.pn == 0) tap(FB_GLO); run(1500); enc(FE_K2, 1); snprintf(nm, sizeof nm, "%s-glo-reloj", LN[lg]); shot(nm);
        enc(FE_K3, 0); snprintf(nm, sizeof nm, "%s-glo-nombres", LN[lg]); tap(FB_GLO); tap(FB_GLO); run(100); shot(nm);
        tap(FB_EDIT); run(1500); enc(FE_K2, 1); snprintf(nm, sizeof nm, "%s-sample", LN[lg]); shot(nm);
        btn(FB_SAVE, 1); run(400); snprintf(nm, sizeof nm, "%s-ayuda-save", LN[lg]); shot(nm); btn(FB_SAVE, 0); run(1500);
        btn(FB_EDIT, 1); run(600); snprintf(nm, sizeof nm, "%s-ayuda-edit", LN[lg]); shot(nm); btn(FB_EDIT, 0); run(1500);
        tap(FB_ENV); run(100); snprintf(nm, sizeof nm, "%s-mezcla", LN[lg]); shot(nm); tap(FB_EDIT); run(1500);
        black(10, 1); run(100); snprintf(nm, sizeof nm, "%s-copia", LN[lg]); shot(nm); black(10, 0); run(1500);
    }
    fg_lang = 0;
    wav_hdr(frames); fclose(wav);
    printf("sizeof(fg_box_t) = %zu, vis = %u\n", sizeof(fg_box_t), fg_vis_size(240, 240, 4));
    return 0;
}
