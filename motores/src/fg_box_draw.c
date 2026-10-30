/* Pantalla de la caja (frame() de caja/index.html): el visual de fg_vis por bandas de 16 filas y, encima, los
 * avisos de texto sobre franjas sólidas. Cada cuadro arma una lista corta de rectángulos y textos; cada banda
 * pinta lo que le toca. Sale en RGB565 con los bytes al revés (como lo pide el ST7789). */
#include "fg_box.h"
#include "fg_dsp.h"
#include "fg_font.h"
#include "fg_lang.h"

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
static const uint16_t C_FG = RGB(242, 238, 228), C_BG = RGB(11, 11, 10), C_DIM = RGB(150, 146, 138), C_LINE = RGB(70, 68, 64),
                      C_RED = RGB(214, 64, 46);

enum { D_RECT, D_TEXT, D_DOT };
typedef struct { uint8_t kind, align; int16_t x, y, w, h; uint16_t col; const fg_font_t *f; char s[44]; } dcmd_t;
#define NDL 72
static dcmd_t DL[NDL];
static unsigned nDL;
static uint32_t dl_hash;                                   /* los textos de este cuadro: si cambian, se mandan todas las bandas */

static uint32_t utf8(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t c = *s++;
    if (c >= 0xC0 && c < 0xE0 && *s) { c = (c & 0x1Fu) << 6 | (*s++ & 0x3Fu); }
    else if (c >= 0xE0 && c < 0xF0 && s[0] && s[1]) { c = (c & 0x0Fu) << 12 | (uint32_t)(s[0] & 0x3Fu) << 6 | (s[1] & 0x3Fu); s += 2; }
    *p = (const char *)s;
    return c;
}
static const fg_glyph_t *glyph(const fg_font_t *f, uint32_t c)
{
    if (c >= 'a' && c <= 'z') c -= 32;                   /* (solo hay mayúsculas) */
    for (unsigned i = 0; i < f->n; i++) if (f->g[i].cp == c) return &f->g[i];
    return 0;
}
static int adv(const fg_font_t *f, const fg_glyph_t *g) { return g ? g->adv + (g->cp == ' ' ? f->size / 6 : 0) : f->size / 3; }   /* (espacios un poco más anchos) */
static int text_w(const fg_font_t *f, const char *s)
{
    int w = 0;
    while (*s) w += adv(f, glyph(f, utf8(&s)));
    return w;
}
static void rect(int x, int y, int w, int h, uint16_t c)
{
    if (nDL >= NDL) return;
    dcmd_t *d = &DL[nDL++];
    d->kind = D_RECT; d->x = (int16_t)x; d->y = (int16_t)y; d->w = (int16_t)w; d->h = (int16_t)h; d->col = c;
}
/* fit(): la letra más grande (hasta px) con la que la palabra cabe en maxw; si ni la chica cabe, la chica */
static void text(const char *s0, int x, int y, int px, int maxw, uint16_t c, int right)
{
    static const fg_font_t *const F[4] = {&FG_FONT_XL, &FG_FONT_L, &FG_FONT_M, &FG_FONT_S};
    if (nDL >= NDL) return;
    dcmd_t *d = &DL[nDL++];
    fg_tr(s0, d->s, sizeof d->s);                          /* en el idioma del aparato (fg_lang.c) */
    int i = px >= 36 ? 0 : px >= 20 ? 1 : px >= 13 ? 2 : 3;
    while (i < 3 && text_w(F[i], d->s) > maxw) i++;
    d->kind = D_TEXT; d->f = F[i]; d->x = (int16_t)x; d->y = (int16_t)y; d->col = c; d->align = (uint8_t)right;
}
static void number(const char *s, int x, int y, uint16_t c)
{
    if (nDL >= NDL) return;
    dcmd_t *d = &DL[nDL++];
    d->kind = D_TEXT; d->f = &FG_FONT_N; d->x = (int16_t)x; d->y = (int16_t)y; d->col = c; d->align = 0;
    unsigned k = 0; while (s[k] && k + 1 < sizeof d->s) { d->s[k] = s[k]; k++; } d->s[k] = 0;
}
static void dot(int x, int y, int r, uint16_t c)
{
    if (nDL >= NDL) return;
    dcmd_t *d = &DL[nDL++];
    d->kind = D_DOT; d->x = (int16_t)x; d->y = (int16_t)y; d->w = (int16_t)r; d->col = c;
}
static void band(int h) { rect(0, 240 - h, 240, h, C_BG); rect(0, 240 - h, 240, 1, C_LINE); }

/* ---- lo que dice cada vista (frame() de la caja) ---- */
static void scpy(char *d, const char *s, unsigned cap) { unsigned i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void scat(char *d, const char *s, unsigned cap) { unsigned n = 0; while (d[n]) n++; scpy(d + n, s, cap > n ? cap - n : 0); }
static void cati(char *d, int v, unsigned cap)
{
    char t[12]; int n = 0; unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    char o[14]; unsigned k = 0; if (v < 0) o[k++] = '-'; while (n) o[k++] = t[--n]; o[k] = 0;
    scat(d, o, cap);
}
static const char *const TN[8] = {"BOMBO", "CAJA", "METAL", "PERC", "SINTE A", "SINTE B", "SINTE C", "SINTE D"};
/* los nombres de los motores no se traducen ("\1": fg_lang.c) */
static const char *const MNAME[4][3] = {{"\1BARRIDO", "\1MEMBRANA", "\1SAMPLE"}, {"\1CAJA", "\1APLAUSO", "\1SAMPLE"}, {"\1GRANIZO", "\1PLACA", "\1SAMPLE"},
                                        {"\1GOTA", "\1CHATARRA", "\1SAMPLE"}};
static const char *const YNAME[FGY_MODELS] = {"\1SH", "\1ÁCIDO", "\1FM", "\1CUERDAS", "\1TEXTURA", "\1DRON", "\1DUB", "\1RESONADOR", "\1SAMPLE"};
static const char *const CONDS[12] = {"SIEMPRE", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "SIN FILL"};
static const char *const MIXL[4] = {"VOLUMEN", "PANEO", "DELAY", "REVERB"};

static int held(const fg_box_t *b, unsigned id) { return (b->down >> id) & 1u; }
static int heldb(const fg_box_t *b, unsigned i) { return (b->bdown >> i) & 1u; }

/* mantener un botón solo: a los 0.3 s sale qué hace mantenerlo (con su barra) y qué más se hace con él apretado */
static int hint_btn(const fg_box_t *b)
{
    static const uint8_t ID[7] = {FB_EDIT, FB_SCL, FB_ENV, FB_LFO, FB_GLO, FB_ARP, FB_SAVE};
    if (b->wdown || b->bdown || (b->down & (b->down - 1u))) return -1;
    for (unsigned k = 0; k < 7; k++) {
        unsigned id = ID[k];
        if (held(b, id) && !((b->used_btn | b->long_done) >> id & 1u) && b->now - b->press_ms[id] >= 300u) return (int)id;
    }
    return -1;
}
static void hint(const fg_box_t *b, unsigned id)
{
    if (id == FB_SAVE) {
        band(120);
        text("GUARDAR", 12, 142, 13, 100, C_FG, 0);
        text("SOLTAR = PROYECTO", 228, 142, 11, 120, C_DIM, 1);
        text("BLANCA VACÍA = GUARDA AHÍ", 12, 162, 11, 216, C_DIM, 0);
        text("BLANCA LLENA: DOBLE = ABRIR,", 12, 180, 11, 216, C_DIM, 0);
        text("MANTENER = GUARDAR ENCIMA", 12, 198, 11, 216, C_DIM, 0);
        text("+ EDIT = GUARDA EL SONIDO", 12, 216, 11, 216, C_DIM, 0);
        text("OCT – / + = DESHACER / REHACER", 12, 234, 11, 216, C_DIM, 0);
        return;
    }
    const char *what = "", *L1 = 0, *L2 = 0;
    switch (id) {
    case FB_EDIT: what = "MANTÉN = SONIDO INICIAL"; L1 = "+ SAVE = GUARDA EL SONIDO"; L2 = "SEL + EDIT = SONIDO AL AZAR"; break;
    case FB_SCL: what = "MANTÉN = VACIAR PISTA"; L1 = "+ EDIT = SONIDO AL AZAR"; L2 = "+ SEQ = RITMO AL AZAR"; break;
    case FB_ENV: what = "MANTÉN = MEZCLA INICIAL"; break;
    case FB_LFO: what = "MANTÉN = SIN LFO"; break;
    case FB_GLO: what = "MANTÉN = SWING Y ESCALA"; break;
    case FB_ARP:
        if (b->sel < 4) { what = "ARPEGIADOR: SOLO SINTES"; break; }
        if ((b->s.arp_on >> b->sel) & 1u) what = b->s.arp[b->sel][3] ? "MANTÉN = ARP SUELTO" : "MANTÉN = ARP SOSTENIDO";
        else what = b->s.arp[b->sel][3] ? "MANTÉN = NOTAS SUELTAS" : "MANTÉN = NOTAS SOSTENIDAS";
        L1 = "1 MODO     2 VELOCIDAD"; L2 = "3 OCTAVAS     4 SOSTENER"; break;
    }
    int h = L1 ? 82 : 46, y = 240 - h + 22, w = (int)(b->now - b->press_ms[id] - 300u) * 216 / 700;
    band(h);
    text(what, 12, y, 13, 216, C_FG, 0);
    if (!(id == FB_ARP && b->sel < 4)) { rect(12, y + 8, 216, 4, C_LINE); rect(12, y + 8, w > 216 ? 216 : w, 4, C_FG); }
    if (L1) { text(L1, 12, y + 36, 11, 216, C_DIM, 0); text(L2, 12, y + 54, 11, 216, C_DIM, 0); }
}

/* el arranque: líneas de señal que se enganchan, FNTSMG-1 letra por letra con cortes, el punto rojo; ~2 s */
static uint32_t hsh(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; return x ^ (x >> 16); }
static void intro(uint32_t t)
{
    static const char NAME[] = "FNTSMG-1";
    uint32_t f = t / 45u;                                   /* el ruido cambia cada ~45 ms */
    if (t >= 400u && t < 1300u) {                           /* los visuales de las pistas (fg_box_tick les da golpes) */
        for (unsigned i = 0; i < 2u; i++) {
            uint32_t r = hsh(f * 53u + i);
            if (r & 3u) continue;
            int w = 40 + (int)(r >> 8) % 160;
            rect((int)(r >> 16) % (240 - w), (int)(r >> 4) % 240, w, 1, C_FG);
        }
        return;
    }
    rect(0, 0, 240, 240, C_BG);
    if (t < 400u) {                                         /* la señal: rayas que se van juntando hacia el renglón */
        unsigned n = 9u - t / 50u;
        for (unsigned i = 0; i < n; i++) {
            uint32_t r = hsh(f * 131u + i);
            int y = 132 + (int)(r % 200u) - 100, w = 30 + (int)(r >> 8) % 180;
            y = 132 + (y - 132) * (int)(400u - t) / 400;
            rect((int)(r >> 16) % (240 - w), y, w, 1 + (int)(r >> 24) % 3, i == 0 && (f & 1u) ? C_RED : C_FG);
        }
        return;
    }
    t -= 780u;                                              /* el nombre: como si empezara en 520 */
    unsigned k = t < 1080u ? 1u + (t - 520u) / 70u : 8u;    /* letras escritas */
    if (k > 8u) k = 8u;
    char s[12]; unsigned j = 0;
    for (; j < k; j++) s[j] = NAME[j];
    s[j] = 0;
    int jit = 0;
    if (t < 1250u) { uint32_t r = hsh(f * 7u + 3u); jit = (int)(r % 7u) - 3; }
    text(s, 14 + jit, 140, 40, 212, C_FG, 0);
    if (t < 1250u) {                                        /* cortes: franjas del fondo que atraviesan el nombre */
        for (unsigned i = 0; i < 2u; i++) {
            uint32_t r = hsh(f * 977u + i * 31u);
            if (r & 1u) rect(0, 104 + (int)(r >> 4) % 40, 240, 1 + (int)(r >> 12) % 4, C_BG);
        }
        if (k < 8u && (f & 1u)) rect(16 + jit + text_w(&FG_FONT_XL, s), 110, 3, 31, C_FG);   /* el cursor */
    } else {
        if (t < 1450u || (t / 160u) & 1u) dot(14 + text_w(&FG_FONT_XL, NAME) + 12, 127, 5, C_RED);
        rect(14, 160, t < 1700u ? (int)(t - 1250u) * 212 / 450 : 212, 1, C_LINE);
        text("SOBRE FELUCCA", 14, 186, 11, 200, C_DIM, 0);
    }
}

void fg_box_frame(fg_box_t *b)
{
    uint32_t now = b->now;
    fg_vis_ui_t *u = &b->vui;
    const fg_track_t *tr = &b->Q.pat[b->s.cur].tr[b->sel];
    u->now_ms = now;
    u->perf = 0;
    static const uint16_t PM[16] = {FG_VIS_REP4, FG_VIS_REP8, FGP_STUT16, FGP_STUT32, FGP_REV, FGP_HALF, FGP_BRAKE, FGP_GATE,
                                    FGP_LP, FGP_HP, FGP_BITS, FGP_THROW, FGP_FREEZE, 0, 0, FGP_RISER};
    for (int i = 0; i < 16; i++) if ((b->perf >> i) & 1u) u->perf |= PM[i];
    int chainv = b->page == PG_CHAIN && !held(b, FB_FX);
    u->view = held(b, FB_HOME) && !chainv ? FG_VIEW_PAT : chainv ? FG_VIEW_VIS : (b->page == PG_ENV && b->seqmode && !held(b, FB_FX)) ? FG_VIEW_MIX : FG_VIEW_VIS;
    u->bpm = b->s.bpm; u->sel = b->sel; u->cur = b->s.cur; u->queued = b->queued;
    u->used = 0;
    for (unsigned p = 0; p < FBX_NP; p++)
        for (unsigned t = 0; t < 8; t++) for (unsigned s = 0; s < FG_SEQ_NS; s++) if (b->Q.pat[p].tr[t].st[s].v) { u->used |= (uint16_t)(1u << p); t = 8; break; }
    for (unsigned t = 0; t < 8; t++) for (unsigned i = 0; i < 4; i++) u->mix[t][i] = b->s.mix[t][i];
    u->mute = b->mute; u->solo = b->solo; u->pend = b->pend; u->has_pend = b->has_pend;
    for (int k = 0; k < 4; k++) { u->kmov[k] = b->kmov[k]; b->kmov[k] = (uint16_t)(b->kmov[k] * 7u / 8u); }   /* (se asienta en ~1 s) */
    b->vis.mute = b->intro ? 0 : b->mute; b->vis.solo = b->intro ? 0 : b->solo;
    fg_vis_frame(&b->vis, u);

    nDL = 0;
    if (b->intro && now - b->intro_t0 >= 2780u) b->intro = 0;
    if (b->intro) { intro(now - b->intro_t0); goto hash; }
    char s1[44], s2[44];
    uint8_t hs[16]; unsigned nhs = 0;
    int hk = chainv ? -1 : hint_btn(b);
    for (unsigned k = 0; k < b->nhw; k++) { unsigned s = b->sp * 16u + b->hw[k]; if (s < tr->len) hs[nhs++] = (uint8_t)s; }
    if (chainv) {                                          /* CADENA: 16 filas como en un tracker, la elegida en blanco */
        int on = b->s.chain_on, now_r = fg_box_chain_row(b);
        rect(0, 0, 240, 240, C_BG);
        text("CADENA", 12, 25, 16, 140, C_FG, 0);
        text(on ? "SÍ" : "NO", 228, 25, 16, 80, on ? C_FG : C_DIM, 1);
        rect(0, 35, 240, 1, C_LINE);
        for (int r = 0; r < 16; r++) {
            int x = r < 8 ? 4 : 124, y = 40 + (r & 7) * 22, sel = r == b->crow_cur, has = b->s.crow[r] < FBX_NP;
            uint16_t cn = sel ? C_BG : C_DIM, cp = sel ? C_BG : on ? C_FG : C_DIM;
            if (sel) rect(x, y, 112, 20, C_FG);
            if (r == now_r) rect(x, y, 4, 20, C_RED);
            s1[0] = 0; if (r < 9) scat(s1, "0", sizeof s1); cati(s1, r + 1, sizeof s1);
            text(s1, x + 8, y + 15, 11, 24, cn, 0);
            if (has) {
                scpy(s2, "PATRÓN ", sizeof s2); cati(s2, b->s.crow[r] + 1, sizeof s2);
                text(s2, x + 32, y + 15, 13, 60, cp, 0);
                if (b->s.crep[r] > 1) { scpy(s2, "×", sizeof s2); cati(s2, b->s.crep[r], sizeof s2); text(s2, x + 108, y + 15, 11, 22, cp, 1); }
            } else text("–", x + 32, y + 15, 13, 60, cn, 0);
        }
        text("1 PATRÓN   2 VECES   4 SÍ / NO", 12, 233, 11, 216, C_DIM, 0);
    }
    if (u->view == FG_VIEW_PAT) {
        band(34);
        s1[0] = 0;
        if (b->nhome > 1) { scpy(s1, "CADENA  ", sizeof s1); for (unsigned k = 0; k < b->nhome; k++) { if (k) scat(s1, " › ", sizeof s1); cati(s1, b->home_list[k] + 1, sizeof s1); } }
        else if (b->s.chain_on) {
            int now_r = fg_box_chain_row(b), k = 0;
            scpy(s1, "CADENA  ", sizeof s1);
            for (int r = 0; r < 16; r++) {
                if (b->s.crow[r] >= FBX_NP) continue;
                if (k++) scat(s1, " › ", sizeof s1);
                if (r == now_r) scat(s1, "[", sizeof s1);
                cati(s1, b->s.crow[r] + 1, sizeof s1);
                if (r == now_r) scat(s1, "]", sizeof s1);
            }
        }
        else { scpy(s1, "PATRÓN ", sizeof s1); cati(s1, b->s.cur + 1, sizeof s1); if (b->queued != 255 && b->queued != b->s.cur) { scat(s1, "  ›  ", sizeof s1); cati(s1, b->queued + 1, sizeof s1); } }
        text(s1, 12, 229, 16, 216, C_FG, 0);
    } else if (heldb(b, BK_MUTE) && !held(b, FB_FX)) {     /* MUTE: las 8 pistas, cómo están (y cuál cambia al compás) */
        uint8_t want = b->has_pend ? b->pend : b->mute;
        band(72);
        text("SILENCIOS", 12, 190, 13, 130, C_FG, 0);
        text(b->s.muteq ? "AL COMPÁS" : "AL INSTANTE", 228, 190, 11, 96, C_DIM, 1);
        for (unsigned t = 0; t < 8; t++) {
            int x = 8 + (int)t * 28, so = (b->solo >> t) & 1u, off = ((want >> t) & 1u) || (b->solo && !so);
            int pend = b->has_pend && (((b->pend ^ b->mute) >> t) & 1u) && ((now >> 8) & 1u);
            uint16_t bg = so ? C_RED : off ? C_LINE : C_FG;
            char d[2] = {(char)('1' + t), 0};
            if (!pend) rect(x, 200, 26, 30, bg);
            else { rect(x, 200, 26, 2, C_DIM); rect(x, 228, 26, 2, C_DIM); }
            text(d, x + 8, 221, 13, 20, pend ? C_DIM : so ? C_FG : off ? C_DIM : C_BG, 0);
        }
    } else if (heldb(b, BK_COPY) && !held(b, FB_FX)) {     /* COPIA: qué hacer, qué está copiado, dónde se pegó */
        static const char *const HINT[4] = {"", "HOME + OTRA BLANCA", "OTRA BLANCA", "OTRA NEGRA"};
        band(66);
        if (!b->copy_kind) {
            text("COPIAR", 12, 196, 13, 216, C_FG, 0);
            text("BLANCA = PASO    NEGRA = PISTA", 12, 215, 11, 216, C_DIM, 0);
            text("HOME + BLANCA = PATRÓN", 12, 232, 11, 216, C_DIM, 0);
        } else {
            scpy(s1, "COPIADO  ", sizeof s1);
            if (b->copy_kind == 3) scat(s1, TN[b->copy_idx & 7], sizeof s1);
            else { scat(s1, b->copy_kind == 1 ? "PATRÓN " : "PASO ", sizeof s1); cati(s1, b->copy_idx + 1, sizeof s1); }
            text(s1, 12, 196, 13, 216, C_FG, 0);
            const char *a = b->ov.label, *p = "PEGADO";
            while (*p && *a == *p) { a++; p++; }
            if (now < b->ov.until && !*p && !*a) { scpy(s2, "PEGADO EN ", sizeof s2); scat(s2, b->ov.value, sizeof s2); text(s2, 12, 215, 11, 216, C_FG, 0); }
            else text("TOCA DÓNDE PEGARLO", 12, 215, 11, 216, C_DIM, 0);
            text(HINT[b->copy_kind & 3], 12, 232, 11, 216, C_DIM, 0);
        }
    } else if (held(b, FB_FX)) {
        /* con los efectos, solo el visual */
    } else if (held(b, FB_ARP) && nhs) {
        const fg_step_t *st = &tr->st[hs[0]];
        band(108);
        scpy(s1, "PASO ", sizeof s1); cati(s1, hs[0] + 1, sizeof s1); scat(s1, "  ·  ", sizeof s1); scat(s1, TN[b->sel], sizeof s1);
        text(s1, 12, 152, 13, 216, C_FG, 0);
        static const char *const L[4] = {"CONDICIÓN", "PROBABILIDAD", "RATCHET", "MICRO"};
        char V[4][16];
        scpy(V[0], CONDS[st->c < 12 ? st->c : 0], 16);
        V[1][0] = 0; cati(V[1], st->p, 16); scat(V[1], "%", 16);
        scpy(V[2], "×", 16); cati(V[2], st->r, 16);
        V[3][0] = 0; if (st->u > 0) scat(V[3], "+", 16); cati(V[3], st->u, 16); scat(V[3], "/16", 16);
        for (int k = 0; k < 4; k++) {
            int x = 12 + (k % 2) * 116, y = k < 2 ? 172 : 212;
            text(L[k], x, y, 11, 104, C_DIM, 0);
            text(V[k], x, y + 22, 20, 104, C_FG, 0);
        }
    } else if (hk >= 0) {
        hint(b, (unsigned)hk);
    } else if (now < b->ov.until && u->view == FG_VIEW_MIX) {
        rect(0, 0, 240, 40, C_BG); rect(0, 40, 240, 1, C_LINE);
        int ismix = 0;
        for (int i = 0; i < 4; i++) { const char *a = MIXL[i], *c = b->ov.label; while (*a && *a == *c) { a++; c++; } if (!*a && !*c) ismix = 1; }
        s1[0] = 0;
        if (ismix) { scpy(s1, TN[b->sel], sizeof s1); scat(s1, " · ", sizeof s1); }
        scat(s1, b->ov.label, sizeof s1);
        text(s1, 12, 25, 13, 120, C_DIM, 0);
        text(b->ov.value, 228, 28, 22, 100, C_FG, 1);
    } else if (now < b->ov.until) {
        band(100);
        /* label: "A · B" -> A a la izquierda, B tenue a la derecha */
        const char *l = b->ov.label, *dot = 0;
        for (const char *c = l; *c; c++) if ((uint8_t)c[0] == 0xC2 && (uint8_t)c[1] == 0xB7 && c > l && c[-1] == ' ') { dot = c - 1; break; }
        if (dot) {
            unsigned n = (unsigned)(dot - l); if (n > 43) n = 43;
            for (unsigned k = 0; k < n; k++) s1[k] = l[k];
            s1[n] = 0;
            scpy(s2, dot + 4, sizeof s2);                /* " · " son 4 bytes */
            text(s1, 12, 158, 14, 120, C_FG, 0);
            text(s2, 228, 158, 12, 96, C_DIM, 1);
        } else text(l, 12, 158, 14, 216, C_FG, 0);
        text(b->ov.value, 11, 202, 40, 218, C_FG, 0);
        if (b->ov.bar >= 0) {
            int w = b->ov.bar > 256 ? 216 : b->ov.bar * 216 / 256;
            rect(12, 218, 216, 8, C_LINE); rect(12, 218, w, 8, C_FG);
        }
    } else if (now < b->names_until && u->view == FG_VIEW_MIX) {   /* en la mezcla, arriba: abajo están las pistas */
        rect(0, 0, 240, 44, C_BG); rect(0, 44, 240, 1, C_LINE);
        for (int k = 0; k < 4; k++) {
            int x = 12 + (k % 2) * 116, y = k < 2 ? 19 : 38;
            char d[2] = {(char)('1' + k), 0};
            text(d, x, y, 11, 12, C_DIM, 0);
            text(b->names[k], x + 14, y, 13, 94, C_FG, 0);
        }
    } else if (now < b->names_until) {
        band(78);
        for (int k = 0; k < 4; k++) {
            int x = 12 + (k % 2) * 116, y = k < 2 ? 186 : 222;
            char d[2] = {(char)('1' + k), 0};
            text(d, x, y, 11, 12, C_DIM, 0);
            text(b->names[k], x + 14, y, 15, 94, C_FG, 0);
        }
    } else if (now < b->big_until && u->view == FG_VIEW_MIX) {     /* pista elegida en la mezcla: arriba, como los avisos */
        rect(0, 0, 240, 40, C_BG); rect(0, 40, 240, 1, C_LINE);
        s1[0] = 0; cati(s1, b->sel + 1, sizeof s1); scat(s1, " · ", sizeof s1); scat(s1, TN[b->sel], sizeof s1);
        text(s1, 12, 25, 13, 100, C_DIM, 0);
        text(b->sel >= 4 ? YNAME[b->s.model[b->sel] % FGY_MODELS] : MNAME[b->sel][b->s.model[b->sel] % 3u], 228, 28, 22, 120, C_FG, 1);
    } else if (now < b->big_until) {
        band(96);
        char d[2] = {(char)('1' + b->sel), 0};
        number(d, 10, 226, C_FG);
        text(TN[b->sel], 74, 174, 13, 154, C_DIM, 0);
        text(b->sel >= 4 ? YNAME[b->s.model[b->sel] % FGY_MODELS] : MNAME[b->sel][b->s.model[b->sel] % 3u], 74, 212, 28, 154, C_FG, 0);
    } else if (b->nhw && b->seqmode && nhs) {
        band(40);
        scpy(s1, "LOCK  ·  PASO", sizeof s1);
        for (unsigned k = 0; k < nhs && k < 4; k++) { scat(s1, " ", sizeof s1); cati(s1, hs[k] + 1, sizeof s1); }
        if (nhs == 1 && tr->st[hs[0]].v == 2) scat(s1, "  ·  ACENTO", sizeof s1);
        text(s1, 12, 226, 15, 216, C_FG, 0);
    }
    if (u->view == FG_VIEW_MIX && !heldb(b, BK_MUTE) && hk < 0) {    /* mezcla: cada pista con el código de SILENCIOS (hueso suena, gris apagada, rojo solo) */
        uint8_t want = b->has_pend ? b->pend : b->mute;
        for (unsigned t = 0; t < 8; t++) {
            int x = (80 + (int)t * 470) / 16, so = (b->solo >> t) & 1u, off = ((want >> t) & 1u) || (b->solo && !so);
            int pend = b->has_pend && (((b->pend ^ b->mute) >> t) & 1u) && ((now >> 8) & 1u);
            char d[2] = {(char)('1' + t), 0};
            if (!pend) rect(x, 201, 23, 24, so ? C_RED : off ? C_LINE : C_FG);
            else { rect(x, 201, 23, 2, C_DIM); rect(x, 223, 23, 2, C_DIM); }
            text(d, x + 7, 219, 13, 20, pend ? C_DIM : so ? C_FG : off ? C_DIM : C_BG, 0);
            if (t == b->sel) rect(x, 229, 23, 3, C_FG);
        }
    }
    if (b->rec) dot(226, 14, 5, C_RED);
    if (heldb(b, BK_FILL)) { rect(8, 8, 50, 22, C_FG); text("FILL", 14, 25, 13, 40, C_BG, 0); }
hash:;
    uint32_t h = 2166136261u;
    const uint8_t *q = (const uint8_t *)DL;
    for (unsigned i = 0; i < nDL * sizeof DL[0]; i++) h = (h ^ q[i]) * 16777619u;
    dl_hash = h ^ nDL;
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a)    /* a 0..15 */
{
    unsigned r = ((bg >> 11) * (15 - a) + (fg >> 11) * a) / 15, g = (((bg >> 5) & 63) * (15 - a) + ((fg >> 5) & 63) * a) / 15,
             bl = ((bg & 31) * (15 - a) + (fg & 31) * a) / 15;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static inline uint16_t swp(uint16_t c) { return (uint16_t)(c >> 8 | c << 8); }

static void draw_text(const dcmd_t *d, unsigned y0, uint16_t *px)
{
    const fg_font_t *f = d->f;
    int x = d->x;
    if (d->align) x -= text_w(f, d->s);
    const char *s = d->s;
    while (*s) {
        const fg_glyph_t *g = glyph(f, utf8(&s));
        if (!g) { x += f->size / 3; continue; }
        int gx = x + g->x, gy = d->y + g->y, stride = (g->w + 1) / 2;
        for (int yy = 0; yy < g->h; yy++) {
            int py = gy + yy - (int)y0;
            if (py < 0 || py >= FG_VIS_BAND) continue;
            const uint8_t *row = f->bits + g->off + (uint32_t)yy * (uint32_t)stride;
            for (int xx = 0; xx < g->w; xx++) {
                int pxx = gx + xx;
                if (pxx < 0 || pxx >= 240) continue;
                unsigned a = (row[xx >> 1] >> ((xx & 1) ? 0 : 4)) & 15u;
                if (!a) continue;
                uint16_t *o = &px[py * 240 + pxx];
                *o = swp(a == 15 ? d->col : blend(d->col, swp(*o), a));
            }
        }
        x += adv(f, g);
    }
}

int fg_box_band(fg_box_t *b, unsigned y0, uint16_t *px)
{
    uint8_t bits[FG_VIS_BAND * 30];
    fg_vis_band(&b->vis, y0, FG_VIS_BAND, bits);
    /* ¿cambió? los bits del visual y los textos del cuadro; si no, ni se convierte */
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof bits; i++) h = (h ^ bits[i]) * 16777619u;
    h ^= dl_hash * 0x9E3779B1u;
    unsigned bi = y0 / FG_VIS_BAND;
    if (bi < 16 && b->band_sum[bi] == h) return 0;
    if (bi < 16) b->band_sum[bi] = h;
    const uint16_t fg = swp(C_FG), bg = swp(C_BG);
    for (unsigned y = 0; y < FG_VIS_BAND; y++) {                /* (el visual es de 240×240: 30 bytes por fila) */
        uint16_t *o = px + y * 240;
        const uint8_t *r = bits + y * 30u;
        for (unsigned xb = 0; xb < 30u; xb++, o += 8) {
            unsigned v = r[xb];
            o[0] = v & 0x80u ? fg : bg; o[1] = v & 0x40u ? fg : bg; o[2] = v & 0x20u ? fg : bg; o[3] = v & 0x10u ? fg : bg;
            o[4] = v & 0x08u ? fg : bg; o[5] = v & 0x04u ? fg : bg; o[6] = v & 0x02u ? fg : bg; o[7] = v & 0x01u ? fg : bg;
        }
    }
    for (unsigned k = 0; k < nDL; k++) {
        const dcmd_t *d = &DL[k];
        if (d->kind == D_RECT) {
            int ya = d->y > (int)y0 ? d->y : (int)y0, yb = d->y + d->h < (int)y0 + FG_VIS_BAND ? d->y + d->h : (int)y0 + FG_VIS_BAND;
            uint16_t c = swp(d->col);
            for (int y = ya; y < yb; y++) for (int x = d->x; x < d->x + d->w && x < 240; x++) if (x >= 0) px[(y - (int)y0) * 240 + x] = c;
        } else if (d->kind == D_TEXT) draw_text(d, y0, px);
        else {
            uint16_t c = swp(d->col);
            for (int y = d->y - d->w; y <= d->y + d->w; y++) {
                int py = y - (int)y0;
                if (py < 0 || py >= FG_VIS_BAND) continue;
                for (int x = d->x - d->w; x <= d->x + d->w; x++) if ((x - d->x) * (x - d->x) + (y - d->y) * (y - d->y) <= d->w * d->w) px[py * 240 + x] = c;
            }
        }
    }
    return 1;
}
