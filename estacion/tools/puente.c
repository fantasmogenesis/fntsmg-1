/* Puente para probar la Estación contra el fg_box real: lee SysEx en hex por stdin (uno por línea),
 * se los da a fg_box_sysex y escribe cada respuesta en hex por stdout. Plataforma igual que host/proto_test.c.
 * «t ms» deja correr el aparato ese tiempo; «k perilla pasos» gira una perilla del aparato; el espejo (golpes y ESTADO) sale igual que las respuestas. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static uint8_t SLOT[20][16384]; static uint32_t SLOTN[20];
static uint32_t crc(const uint8_t *p, uint32_t n) { uint32_t c = 0xFFFFFFFFu; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); } return ~c; }
static int s_write(unsigned s, const void *d, uint32_t n) { memcpy(SLOT[s], d, n); SLOTN[s] = n; return 0; }
static int s_read(unsigned s, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[s] || SLOTN[s] > cap) return -1; memcpy(d, SLOT[s], SLOTN[s]); *n = SLOTN[s]; return 0; }
static int s_has(unsigned s) { return SLOTN[s] != 0; }
static int s_info(unsigned s, uint32_t *l, uint32_t *c) { *l = SLOTN[s]; *c = SLOTN[s] ? crc(SLOT[s], SLOTN[s]) : 0; return 0; }
static uint8_t F[0x100000];
static int rd(uint32_t o, void *d, uint32_t n) { memcpy(d, F + o, n); return 0; }
static int er(uint32_t o) { memset(F + o, 0xFF, 4096); return 0; }
static int pr(uint32_t o, const void *s, uint32_t n) { const uint8_t *p = s; for (uint32_t i = 0; i < n; i++) F[o + i] &= p[i]; return 0; }
static const uint8_t *ptr(uint32_t o) { return F + o; }
static const fg_usr_io_t IO = {rd, er, pr, ptr, 0xBC000u, 140u * 1024u};
static int out(const uint8_t *p, unsigned n) { for (unsigned i = 0; i < n; i++) printf("%02x", p[i]); printf("\n"); return 0; }
static void tick(unsigned ms)
{
    static int32_t o[2 * FG_BLOCK]; static uint8_t m[128];
    for (unsigned i = 0; i < ms; i++) {
        B.now++; fg_box_tick(&B, B.now); if (B.now % 3 == 0) fg_box_audio(&B, o, FG_BLOCK); fg_box_idle(&B);
        unsigned n; while ((n = fg_box_mirror(&B, m, 0))) out(m, n);              /* los golpes */
        if (B.now % 33 == 0 && (n = fg_box_mirror(&B, m, 1))) out(m, n);   /* el estado, uno por cuadro */
    }
}
int main(void)
{
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has; B.store_info = s_info; B.sysex_out = out; B.usr_io = &IO; B.version = "FNTSMG-1 0.9";
    memset(F, 0xFF, sizeof F); uint32_t m = 0x31555746u; memcpy(F + 0xBC000u, &m, 4);
    fg_box_init(&B, VMEM, 240, 240, 4, 1234); fg_box_boot(&B); tick(3000);
    setvbuf(stdout, NULL, _IOLBF, 0);
    static char L[8192]; static uint8_t M[4096];
    while (fgets(L, sizeof L, stdin)) {
        if (L[0] == 't') { tick((unsigned)atoi(L + 2)); printf("--\n"); continue; }
        if (L[0] == 'k') { int e = 0, st = 0; sscanf(L + 2, "%d %d", &e, &st); fg_box_enc(&B, (unsigned)e, st); tick(150); printf("--\n"); continue; }   /* «k perilla pasos»: la persona gira una perilla del aparato */
        unsigned n = 0; for (char *c = L; c[0] && c[1] && c[0] != '\n'; c += 2) { unsigned v; sscanf(c, "%2x", &v); M[n++] = (uint8_t)v; }
        if (n >= 6 && M[0] == 0xF0) fg_box_sysex(&B, M + 4, n - 5);
        tick(2);
        printf("--\n");
    }
    return 0;
}
