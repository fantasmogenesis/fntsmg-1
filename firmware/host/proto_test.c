/* La app de mentiras: habla con la caja por PROTOCOLO.md (los mismos bytes que mandaría la companion) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
/* la flash: ranuras en RAM y TUYOS en una NOR de mentiras */
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
/* lo que contesta la caja */
static uint8_t R[64][800]; static unsigned RN[64], nr;
static int out(const uint8_t *p, unsigned n) { if (nr < 64) { memcpy(R[nr], p, n); RN[nr++] = n; } return 0; }
/* armar una pregunta */
static uint8_t Q[800]; static unsigned qn;
static void q7(unsigned v) { Q[qn++] = v & 127; }
static void q14(unsigned v) { q7(v); q7(v >> 7); }
static void qs14(int v) { q14((unsigned)(v + 8192)); }
static void q21(uint32_t v) { q7(v); q7(v >> 7); q7(v >> 14); }
static void q32(uint32_t v) { for (int i = 0; i < 5; i++) q7(v >> (7 * i)); }
static void qpack(const uint8_t *d, unsigned n) { for (unsigned i = 0; i < n; i += 7) { unsigned k = n - i < 7 ? n - i : 7, hi = 0; for (unsigned j = 0; j < k; j++) hi |= (d[i + j] >> 7) << j; q7(hi); for (unsigned j = 0; j < k; j++) q7(d[i + j]); } }
static void qbegin(unsigned cmd, unsigned id) { qn = 0; q7(cmd); q7(id); }
/* leer una respuesta */
static const uint8_t *A; static unsigned an, ap;
static unsigned a7(void) { return ap < an ? A[ap++] : 0; }
static unsigned a14(void) { unsigned x = a7(); return x | a7() << 7; }
static int as14(void) { return (int)a14() - 8192; }
static uint32_t a21(void) { uint32_t x = a7(), y = a7(); return x | y << 7 | (uint32_t)a7() << 14; }
static uint32_t a32(void) { uint32_t v = 0; for (int i = 0; i < 5; i++) v |= (uint32_t)a7() << (7 * i); return v; }
static unsigned apack(uint8_t *d, unsigned cap) { unsigned n = 0; while (ap < an) { unsigned hi = A[ap++], k = an - ap < 7 ? an - ap : 7; for (unsigned j = 0; j < k && n < cap; j++) d[n++] = A[ap++] | ((hi >> j) & 1) << 7; } return n; }
static int bad;
/* manda y deja la respuesta en A; devuelve el estado */
static int ask(void)
{
    nr = 0;
    fg_box_sysex(&B, Q, qn);
    if (nr != 1) { printf("  %u respuestas a cmd %02x\n", nr, Q[0]); bad++; return -1; }
    A = R[0]; an = RN[0] - 1; ap = 4;                    /* F0 7D 46 47 | cmd id estado ... | F7 */
    if (A[0] != 0xF0 || A[1] != 0x7D || A[2] != 0x46 || A[3] != 0x47 || A[an] != 0xF7 || A[4] != Q[0] || A[5] != Q[1]) { printf("  cabecera mala\n"); bad++; }
    for (unsigned i = 1; i < an; i++) if (A[i] & 0x80) { printf("  byte con bit 7 en %u\n", i); bad++; break; }
    if (RN[0] > 640) printf("  (respuesta de %u bytes)\n", RN[0]);
    ap = 7;
    return A[6];
}
static void tick(unsigned ms) { static int32_t o[2 * FG_BLOCK]; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); if (B.now % 3 == 0) fg_box_audio(&B, o, FG_BLOCK); fg_box_idle(&B); } }
#define OK(c, ...) do { if (!(c)) { printf("MAL: " __VA_ARGS__); printf("\n"); bad++; } } while (0)
static uint8_t BIG[20000];
static uint32_t read_obj(unsigned obj, unsigned ix, uint8_t *o)
{
    qbegin(0x20, 10); q7(obj); q7(ix); q7(0);
    int st = ask(); if (st) return 0;
    uint32_t len = a21(), c = a32(), got = 0;
    while (got < len) { qbegin(0x21, 11 + (got / 512) % 100); q21(got); q14(512); if (ask()) return 0; uint32_t f = a21(); got = f + apack(o + f, 20000 - f); }
    qbegin(0x23, 120); q7(0); ask();
    OK(crc(o, len) == c, "CRC del objeto %u", obj);
    return len;
}
static int write_obj(unsigned obj, unsigned ix, const uint8_t *d, uint32_t len, int sample)
{
    qbegin(0x20, 20); q7(obj); q7(ix); q7(1); q21(len); q32(crc(d, len));
    if (sample) { q21(44100); q21(len / 36 * 64); q7(60); q7(0); q21(0); q21(0); qpack((const uint8_t *)"MI GOLPE", 9); }
    int st = ask(); if (st) return st;
    for (uint32_t i = 0, id = 30; i < len; i += 512, id++) { unsigned n = len - i < 512 ? len - i : 512; qbegin(0x22, id % 120 + 1); q21(i); qpack(d + i, n); if ((st = ask())) return st; }
    qbegin(0x23, 121); q7(1); return ask();
}
int main(void)
{
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has; B.store_info = s_info; B.sysex_out = out; B.usr_io = &IO; B.version = "FNTSMG-1 0.9";
    memset(F, 0xFF, sizeof F); uint32_t m = 0x31555746u; memcpy(F + 0xBC000u, &m, 4);
    fg_box_init(&B, VMEM, 240, 240, 4, 1234); fg_box_boot(&B); tick(3000);
    /* INFO */
    qbegin(0x10, 1); OK(ask() == 0, "INFO");
    unsigned pv = a7(), fv = a7(), np = a7(), nt = a7(), ns = a7(), sl = a7(); uint32_t ss = a21(); unsigned nb = a7(), tr = a14(); uint32_t zt = a21(), zf = a21(); unsigned lg = a7(), cnt = a14(), fl = a7();
    uint8_t v[64]; unsigned vn = apack(v, 63); v[vn] = 0;
    printf("INFO: protocolo %u, formato %u, %u patrones, %u pistas, %u pasos, %u ranuras de %u B, banco %u, trozo %u, TUYOS %u (%u libres), idioma %u, contador %u, banderas %u, «%s»\n", pv, fv, np, nt, ns, sl, ss, nb, tr, zt, zf, lg, cnt, fl, v);
    OK(pv == 1 && fv == 7 && np == 16 && nt == 8 && ns == 32 && sl == 16 && ss == 8180 && tr == 512 && !strcmp((char *)v, "FNTSMG-1 0.9"), "INFO");
    /* META */
    qbegin(0x13, 2); q7(0); OK(ask() == 0, "META pistas"); vn = apack(BIG, 600); printf("pistas: "); for (unsigned i = 0; i < vn; i++) putchar(BIG[i] ? BIG[i] : '|'); printf("\n");
    qbegin(0x13, 3); q7(1); q7(0); OK(ask() == 0, "META modelos"); vn = apack(BIG, 600); printf("modelos del bombo (%u): ", BIG[0]); for (unsigned i = 1; i < vn; i++) putchar(BIG[i] ? BIG[i] : '|'); printf("\n");
    for (unsigned pg = 0; pg < 8; pg++) for (unsigned sub = 0; sub < 2; sub++) {
        qbegin(0x13, 70 + pg * 2 + sub); q7(2); q7(pg == 7 ? 4 : 0); q7(pg); q7(sub); if (ask()) { printf("META perillas %u/%u: estado %d\n", pg, sub, A[6]); continue; }
        vn = apack(BIG, 600); printf("pág %u.%u:", pg, sub);
        for (unsigned i = 0, k = 0; k < 4 && i < vn; k++) { printf("  %s", BIG[i] ? (char *)BIG + i : "–"); i += strlen((char *)BIG + i) + 1; int mi = (int16_t)(BIG[i] | BIG[i + 1] << 8), ma = (int16_t)(BIG[i + 2] | BIG[i + 3] << 8); i += 4; printf(" [%d..%d]", mi, ma); }
        printf("\n");
    }
    qbegin(0x13, 5); q7(3); q7(0); q7(5); q7(0); q7(0); qs14(30); OK(ask() == 0, "META valor"); vn = apack(BIG, 100); printf("GLO tempo 30 se lee «%s»\n", BIG);
    qbegin(0x13, 6); q7(5); q7(2); OK(ask() == 0, "META samples"); vn = apack(BIG, 600); printf("TEXTURAS: %u\n", BIG[0]);
    qbegin(0x13, 7); q7(6); q7(1); OK(ask() == 0, "META escalas"); vn = apack(BIG, 600); printf("escalas (%u): ", BIG[0]); for (unsigned i = 1; i < vn; i++) putchar(BIG[i] ? BIG[i] : '|'); printf("\n");
    /* ESCUCHA + CAMBIO: la persona gira una perilla */
    qbegin(0x11, 8); q7(3); OK(ask() == 0, "ESCUCHA");
    nr = 0; tick(150); unsigned n0 = nr;
    fg_box_enc(&B, FE_K1, 3); nr = 0; tick(150);
    int found = 0; for (unsigned i = 0; i < nr; i++) if (R[i][4] == 0x12) { A = R[i]; an = RN[i] - 1; ap = 7; unsigned c = a14(), w = a14(); printf("CAMBIO: contador %u, qué %u\n", c, w); found = w & 1; }
    OK(n0 == 0 && found, "CAMBIO tras girar (antes %u)", n0);
    /* PERILLA: ENV VOLUMEN del bombo */
    qbegin(0x30, 9); q7(0); q7(1); q7(0); q7(0); qs14(100); OK(ask() == 0, "PERILLA"); int got = as14(); unsigned c1 = a14();
    OK(got == 100 && B.s.mix[0][0] == 100, "PERILLA quedó %d, mezcla %d", got, B.s.mix[0][0]);
    qbegin(0x30, 9); q7(0); q7(1); q7(0); q7(0); qs14(20); ask();
    OK(B.s.mix[0][0] == 100, "la pregunta repetida no se aplicó dos veces");
    nr = 0; qbegin(0x11, 12); q7(3); ask(); nr = 0; tick(250); int echo = 0; for (unsigned i = 0; i < nr; i++) echo |= R[i][4] == 0x12;
    OK(!echo, "la app no recibe aviso de su propio cambio (contador %u)", c1);
    qbegin(0x30, 13); q7(0); q7(1); q7(0); q7(0); qs14(400); ask(); OK(as14() == 127, "PERILLA se acota a 127");
    qbegin(0x31, 14); q7(0); q7(1); q7(0); OK(ask() == 0, "PERILLAS"); printf("ENV del bombo: %d %d %d %d\n", as14(), as14(), as14(), as14());
    qbegin(0x30, 15); q7(0); q7(0); q7(0); q7(4); qs14(2); ask(); OK(B.s.model[0] == 2, "PERILLA ALGO -> SAMPLE");
    /* proyecto: leer, cambiar en la caja, volver a escribir lo leído */
    uint32_t pl = read_obj(0, 0, BIG); printf("proyecto leído: %u bytes\n", pl);
    static uint8_t keep[20000]; memcpy(keep, BIG, pl);
    uint8_t bpm0 = B.s.bpm; B.s.bpm = 77;
    OK(write_obj(0, 0, keep, pl, 0) == 0, "escribir proyecto");
    OK(B.s.bpm == bpm0, "el proyecto escrito volvió (bpm %d)", B.s.bpm);
    /* ranura: escribir el proyecto en la 3, LISTA, leerla, borrarla */
    OK(write_obj(2, 3, keep, pl, 0) == 0, "escribir ranura"); OK(B.slots >> 3 & 1, "ranura 3 marcada");
    qbegin(0x25, 40); q7(2); OK(ask() == 0, "LISTA ranuras"); unsigned nsl = a7(); uint32_t l3 = 0; for (unsigned i = 0; i < nsl; i++) { uint32_t l = a21(); a32(); if (i == 3) l3 = l; }
    OK(nsl == 17 && l3 == pl, "LISTA: %u ranuras, la 3 con %u", nsl, l3);
    OK(read_obj(2, 3, BIG) == pl && !memcmp(BIG, keep, pl), "leer ranura 3");
    qbegin(0x24, 41); q7(2); q7(3); OK(ask() == 0, "BORRAR ranura"); OK(!(B.slots >> 3 & 1), "ranura 3 vacía");
    qbegin(0x20, 42); q7(2); q7(3); q7(0); OK(ask() == 11, "leer ranura vacía = VACIO");
    /* sample: escribir uno en TUYOS 0, lista, tocarlo en el bombo */
    static uint8_t S[18000]; for (int i = 0; i < 18000; i += 36) { S[i] = 0; S[i + 1] = 0; S[i + 2] = 40; S[i + 3] = 0; for (int j = 4; j < 36; j++) S[i + j] = (uint8_t)(j & 1 ? 0x17 : 0x9B); }
    int ws = write_obj(4, 0, S, 18000, 1); OK(ws == 0, "escribir sample (estado %d)", ws);
    qbegin(0x25, 43); q7(4); OK(ask() == 0, "LISTA samples"); uint32_t fr = a21(); unsigned nsm = a7(); unsigned k0 = a7(); uint32_t len0 = a21();
    { a21(); a32(); a21(); a7(); a7(); a21(); a21(); unsigned nl = a7(); uint8_t nm[16]; unsigned m0 = apack(nm, 15); nm[m0] = 0; printf("nombre (%u bytes): «%s»\n", nl, nm); OK(nl == 8 && !strcmp((char *)nm, "MI GOLPE"), "nombre en LISTA"); }
    printf("TUYOS: %u, libre %u, el %u con %u muestras\n", nsm, fr, k0, len0); OK(nsm == 1 && k0 == 0 && len0 == 32000, "LISTA samples");
    qbegin(0x13, 44); q7(5); q7(3); ask(); vn = apack(BIG, 100); printf("META TUYOS: %u, lugar %u «%s»\n", BIG[0], BIG[1], BIG + 2); OK(BIG[0] == 1 && BIG[1] == 96, "META TUYOS");
    qbegin(0x30, 45); q7(0); q7(0); q7(0); q7(1); qs14(96); ask(); OK(as14() == 96, "SAMPLE del bombo en TUYOS 1");
    OK(read_obj(4, 0, BIG) == 18000 && !memcmp(BIG, S, 18000), "leer el sample de vuelta");
    qbegin(0x20, 46); q7(4); q7(1); q7(1); q21(200000); q32(0); q21(44100); q21(355000); q7(60); q7(0); q21(0); q21(0); qpack((const uint8_t *)"X", 2);
    OK(ask() == 5, "un sample que no cabe = NO_CABE"); printf("  cabe %u\n", a21());
    tap: B.playing = 1;
    qbegin(0x20, 47); q7(4); q7(2); q7(1); q21(3600); q32(0); q21(44100); q21(6400); q7(60); q7(0); q21(0); q21(0); qpack((const uint8_t *)"X", 2);
    OK(ask() == 4, "escribir tocando = TOCANDO");
    B.playing = 0;
    /* PASO y PISTA */
    uint8_t rec[4] = {1 | 4, 2, 50};                     /* golpe con acento y probabilidad 50 */
    qbegin(0x32, 50); q7(1); q7(4); q7(7); qpack(rec, 3); OK(ask() == 0, "PASO");
    OK(B.Q.pat[1].tr[4].st[7].v == 2 && B.Q.pat[1].tr[4].st[7].p == 50, "PASO quedó v %d p %d", B.Q.pat[1].tr[4].st[7].v, B.Q.pat[1].tr[4].st[7].p);
    qbegin(0x33, 51); q7(1); q7(4); q7(0); OK(ask() == 0, "PISTA leer"); a7(); a7(); vn = apack(BIG, 600);
    qbegin(0x32, 52); q7(1); q7(4); q7(7); { uint8_t z = 0; qpack(&z, 1); } ask(); OK(B.Q.pat[1].tr[4].st[7].v == 0, "PASO 00 lo vacía");
    qbegin(0x33, 53); q7(1); q7(4); q7(1); qpack(BIG, vn); OK(ask() == 0, "PISTA escribir"); OK(B.Q.pat[1].tr[4].st[7].v == 2, "PISTA volvió");
    /* TRANSPORTE */
    qbegin(0x34, 54); q7(0); q7(1); ask(); OK(B.playing == 1, "tocar");
    qbegin(0x34, 55); q7(2); q7(5); q7(0); ask(); OK(B.mute == 5, "mutes");
    qbegin(0x34, 56); q7(0); q7(0); ask(); OK(B.playing == 0, "parar");
    /* AJUSTE */
    qbegin(0x40, 57); q7(0); q7(1); qs14(1); ask(); OK(as14() == 1, "idioma English");
    qbegin(0x13, 58); q7(0); ask(); vn = apack(BIG, 600); printf("pistas en inglés: "); for (unsigned i = 0; i < vn; i++) putchar(BIG[i] ? BIG[i] : '|'); printf("\n");
    qbegin(0x40, 59); q7(0); q7(1); qs14(0); ask();
    /* mal formados */
    qbegin(0x32, 60); q7(1); q7(4); q7(7); q7(0x7F); q7(1); OK(ask() == 1, "empaque malo = NO_ENTIENDO");
    qbegin(0x55, 61); OK(ask() == 1, "comando que no existe = NO_ENTIENDO");
    qbegin(0x21, 62); q21(0); q14(10); OK(ask() == 7, "LEER sin ABRIR = SIN_TRASLADO");
    printf(bad ? "FALLAS: %d\n" : "todo bien\n", bad);
    return bad;
}
