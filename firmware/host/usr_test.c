/* TUYOS en una flash de mentiras (NOR: programar solo baja bits): escribir, reemplazar, borrar, llenar el registro
 * y cortar la corriente en cada operación de un reemplazo: siempre queda el viejo o el nuevo, entero */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fg_usr.h"
#define BASE 0xBC000u
#define SIZE (140u * 1024u)
static uint8_t F[0x100000]; static long ops, cut = -1;
static int rd(uint32_t o, void *d, uint32_t n) { memcpy(d, F + o, n); return 0; }
static int er(uint32_t o) { if (cut >= 0 && ops++ >= cut) return -9; memset(F + o, 0xFF, 4096); return 0; }
static int pr(uint32_t o, const void *s, uint32_t n) { if (cut >= 0 && ops++ >= cut) return -9; const uint8_t *p = s; for (uint32_t i = 0; i < n; i++) F[o + i] &= p[i]; return 0; }
static const uint8_t *ptr(uint32_t o) { return F + o; }
static const fg_usr_io_t IO = {rd, er, pr, ptr, BASE, SIZE};
static uint8_t D[3][60000];
static uint32_t crc(const uint8_t *p, uint32_t n) { uint32_t c = 0xFFFFFFFFu; while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); } return ~c; }
static int put(unsigned k, int which, uint32_t bytes)
{
    fg_usr_ent_t e; memset(&e, 0, sizeof e);
    e.k = (uint8_t)k; e.bytes = bytes; e.len = bytes / 36u * 64u; e.rate = 44100; e.root = 60; e.crc = crc(D[which], bytes);
    snprintf(e.name, sizeof e.name, "S%d-%d", k, which);
    uint32_t room = 0; int r = fg_usr_begin(&e, &room);
    if (r) return r;
    for (uint32_t i = 0; i < bytes; i += 512) { uint32_t n = bytes - i < 512 ? bytes - i : 512; if ((r = fg_usr_data(i, D[which] + i, n))) { fg_usr_end(0); return r; } }
    return fg_usr_end(1);
}
static int is(unsigned k, int which, uint32_t bytes) { return (fg_usr_mask >> k & 1u) && fg_usr[k].len == bytes / 36u * 64u && !memcmp(fg_usr[k].data, D[which], bytes); }
int main(void)
{
    int bad = 0;
    for (int w = 0; w < 3; w++) for (int i = 0; i < 60000; i++) D[w][i] = (uint8_t)rand();
    memset(F, 0xFF, sizeof F); uint32_t m = 0x31555746u; memcpy(F + BASE, &m, 4);   /* (lo que deja la migración) */
    fg_usr_init(&IO);
    printf("zona %u B, libre %u B\n", fg_usr_total(), fg_usr_free());
    bad += put(0, 0, 36000) != 0; bad += put(5, 1, 20000) != 0;
    fg_usr_init(&IO);
    printf("tras reiniciar: 0 %s, 5 %s, libre %u\n", is(0, 0, 36000) ? "bien" : "MAL", is(5, 1, 20000) ? "bien" : "MAL", fg_usr_free());
    bad += !is(0, 0, 36000) + !is(5, 1, 20000);
    int r = put(9, 2, 140000); printf("uno que no cabe: %d (NO_CABE = %d)\n", r, FGU_NO_CABE); bad += r != FGU_NO_CABE;
    for (int i = 0; i < 100; i++) if (put(3, i & 1, 9000)) { printf("vuelta %d falló\n", i); bad++; break; }   /* llena el registro: se reescribe */
    fg_usr_init(&IO);
    printf("100 reemplazos: 3 %s, 0 %s, 5 %s\n", is(3, 1, 9000) ? "bien" : "MAL", is(0, 0, 36000) ? "bien" : "MAL", is(5, 1, 20000) ? "bien" : "MAL");
    bad += !is(3, 1, 9000) + !is(0, 0, 36000) + !is(5, 1, 20000);
    bad += fg_usr_delete(5) != 0; fg_usr_init(&IO); bad += (fg_usr_mask >> 5 & 1u) != 0;
    /* cortes en un reemplazo del 0 */
    static uint8_t snap[0x100000]; memcpy(snap, F, sizeof F);
    cut = -1; ops = 0; cut = 100000; put(0, 2, 30000); long total = ops; cut = -1;
    int fails = 0;
    for (long c = 0; c < total; c++) {
        memcpy(F, snap, sizeof F); fg_usr_init(&IO);
        ops = 0; cut = c; put(0, 2, 30000); cut = -1;
        fg_usr_init(&IO);
        if (!is(0, 0, 36000) && !is(0, 2, 30000)) { fails++; printf(" corte %ld: el 0 quedó roto\n", c); }
        if (!is(3, 1, 9000)) { fails++; printf(" corte %ld: el 3 se perdió\n", c); }
    }
    printf("cortes en un reemplazo: %d fallas de %ld\n", fails, total);
    bad += fails;
    printf(bad ? "FALLAS: %d\n" : "todo bien\n", bad);
    return bad;
}
