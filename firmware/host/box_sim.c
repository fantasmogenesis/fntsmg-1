/* Las pantallas de la 0.10 en la Mac (las mismas funciones de dibujo del FM-1): capturas PNG de cada vista y página.
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
static void step(unsigned i) { white(i, 1); white(i, 0); }

int main(int argc, char **argv)
{
    if (argc > 1) outdir = argv[1];
    char fn[256];
    snprintf(fn, sizeof fn, "%s/pantallas.wav", outdir);
    wav = fopen(fn, "wb");
    wav_hdr(0);
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, VMEM, 240, 240, 4, 1234);
    fg_box_boot(&B);
    run(3000);
    tap(FB_PLAY); run(1500); shot("visual");
    tap(FB_EDIT); run(100); shot("edit-nombres");
    tap(FB_ENV); run(100); shot("env-mezcla");
    tap(FB_ENV); run(100); shot("env-pag2");
    tap(FB_SEQ); run(1500);
    enc(FE_SELECT, 4); run(1500);
    tap(FB_ARP); run(100); shot("arp-pag1");
    tap(FB_ARP); run(100); shot("arp-pag2");
    tap(FB_SEQ); run(1500); tap(FB_SEQ); run(100); shot("paso-nombres");
    white(4, 1); enc(FE_K1, 3); run(50); shot("paso-condicion"); white(4, 0); run(1500);
    tap(FB_SEQ); run(1500);
    tap(FB_HOME); run(100); tap(FB_HOME); run(100);
    step(2); run(300); shot("patrones");                                /* (el 3 en la cola, parpadeando) */
    run(4000); shot("patrones-2");
    tap(FB_HOME); run(1500);
    btn(FB_HOME, 1); tap(FB_SEQ); btn(FB_HOME, 0); run(100); shot("cadena");
    tap(FB_HOME); run(100);
    tap(FB_FX); run(100); shot("fx-nombres");
    step(15); run(400); shot("fx-fill");
    tap(FB_SEQ); run(1500); shot("fill-pasos");
    tap(FB_FX); step(15); run(100);
    tap(FB_SEQ); run(1500);
    black(9, 1); step(3); shot("copiar"); black(9, 0); run(1400);
    black(10, 1); run(100); shot("pegar"); black(10, 0); run(100);
    black(8, 1); run(100); shot("mute"); black(8, 0); run(100);
    tap(FB_SAVE); run(100); shot("ranuras-guardado"); run(1500);
    step(3); run(100); step(3); run(100); shot("ranuras-abrir"); run(1500);
    shot("ranuras");
    tap(FB_SAVE); run(100);
    btn(FB_REC, 1); run(1100); shot("rec-1s"); btn(FB_REC, 0); run(100);
    wav_hdr(frames); fclose(wav);
    return 0;
}
