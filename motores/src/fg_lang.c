#include "fg_lang.h"
#include "fg_lang_tab.h"                         /* FG_LANG_N, FG_LH (los textos en español, por su hash), FG_LO, FG_LS */

uint8_t fg_lang;
const char *const FG_LANG_NAME[FG_LANGS] = {"\1ESPAÑOL", "\1ENGLISH", "\1PORTUGUÊS"};

static uint32_t hash(const char *s, unsigned n)  /* FNV-1a */
{
    uint32_t h = 2166136261u;
    while (n--) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}
static const char *find(const char *s, unsigned n)
{
    if (!fg_lang || !n) return 0;
    uint32_t h = hash(s, n);
    int lo = 0, hi = FG_LANG_N - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (FG_LH[m] == h) return FG_LS + FG_LO[m][fg_lang - 1u];
        if (FG_LH[m] < h) lo = m + 1; else hi = m - 1;
    }
    return 0;
}
const char *fg_tr_word(const char *s) { unsigned n = 0; while (s[n]) n++; return find(s, n); }

typedef struct { char *o; unsigned k, cap; } out_t;
static void put(out_t *w, const char *s, unsigned n) { while (n-- && w->k + 1u < w->cap) w->o[w->k++] = *s++; }
static void puts0(out_t *w, const char *s) { while (*s && w->k + 1u < w->cap) w->o[w->k++] = *s++; }
static int dig(char c) { return c >= '0' && c <= '9'; }
/* s[0..n): el pedazo con sus espacios de los lados; se busca sin ellos */
static int word(out_t *w, const char *s, unsigned n)
{
    unsigned a = 0, z = n;
    while (a < z && s[a] == ' ') a++;
    while (z > a && s[z - 1u] == ' ') z--;
    const char *t = find(s + a, z - a);
    if (!t) return 0;
    put(w, s, a); puts0(w, t); put(w, s + z, n - z);
    return 1;
}
static void seg(out_t *w, const char *s, unsigned n)
{
    unsigned a = 0;
    while (a < n && s[a] == ' ') a++;
    if (a < n && s[a] == 1) { put(w, s, a); put(w, s + a + 1u, n - a - 1u); return; }   /* no se traduce */
    if (word(w, s, n)) return;
    for (unsigned i = 0; i < n;) {                    /* los números se quedan; lo de en medio, por su lado */
        unsigned j = i;
        if (dig(s[i])) { while (j < n && dig(s[j])) j++; put(w, s + i, j - i); }
        else { while (j < n && !dig(s[j])) j++; if (!word(w, s + i, j - i)) put(w, s + i, j - i); }
        i = j;
    }
}
void fg_tr(const char *s, char *out, unsigned cap)
{
    out_t w = {out, 0, cap};
    unsigned n = 0, raw = 0;
    while (s[n]) raw |= s[n++] == 1;
    if (!fg_lang) { for (unsigned i = 0; i < n; i++) if (s[i] != 1) put(&w, s + i, 1); }
    else if (raw || !word(&w, s, n)) {
        unsigned i = 0;
        for (unsigned j = 0; j + 1u < n; j++)             /* los tramos entre "·" (C2 B7) */
            if ((uint8_t)s[j] == 0xC2u && (uint8_t)s[j + 1u] == 0xB7u) { seg(&w, s + i, j - i); put(&w, s + j, 2); i = j + 2u; j++; }
        seg(&w, s + i, n - i);
    }
    out[w.k < cap ? w.k : cap - 1u] = 0;
}
