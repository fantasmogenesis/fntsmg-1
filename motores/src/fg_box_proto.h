/* La app companion (PROTOCOLO.md en la raíz): SysEx F0 7D 46 47 cmd id datos F7. Se incluye una sola vez, al final
 * de fg_box.c, porque usa sus funciones de adentro: lo que la app escribe pasa por el mismo camino que las perillas,
 * las teclas y los botones (se acota, se deshace y se autoguarda igual). */
#include "fg_usr.h"

enum { PX_BIEN, PX_NO_ENTIENDO, PX_FUERA, PX_OCUPADO, PX_TOCANDO, PX_NO_CABE, PX_CRC, PX_SIN_TRASLADO, PX_FALLO_FLASH,
       PX_SIN_FLASH, PX_FORMATO, PX_VACIO };
enum { PX_INFO = 0x10, PX_ESCUCHA, PX_CAMBIO, PX_META, PX_ABRIR = 0x20, PX_LEER, PX_ESCRIBIR, PX_CERRAR, PX_BORRAR, PX_LISTA,
       PX_PERILLA = 0x30, PX_PERILLAS, PX_PASO, PX_PISTA, PX_TRANSPORTE, PX_NOMBRE, PX_AJUSTE = 0x40 };
enum { PO_PROYECTO, PO_BANCO, PO_RANURA, PO_AUTO, PO_SAMPLE };
#define PX_VERSION 1u
#define PX_TROZO 512u

static struct {
    uint8_t out[720]; unsigned on;                /* la última respuesta (una pregunta repetida la vuelve a mandar) */
    uint8_t last_cmd, last_id, has_last;
    const uint8_t *in; unsigned ip, in_n, bad;    /* la pregunta que se está leyendo */
    uint8_t listen; uint32_t listen_ms;
    uint16_t count;
    uint8_t open, obj, idx, wr; uint32_t len, crc, got, ms;   /* el traslado abierto */
    uint32_t h_song, h_tr[FBX_NP][8], h_tp, h_bank, h_lang, h_usr, h_slots, gen, seen_gen;
    uint32_t last_ms; uint8_t rr, base_ok;
} PX FG_POOL;                                            /* (fg_box_init lo limpia: px_init) */
static void px_init(void) { fg_zero(&PX, sizeof PX); }

/* ------------------------------------------------------------ bytes --- */
static void w7(unsigned v) { if (PX.on < sizeof PX.out - 1u) PX.out[PX.on++] = (uint8_t)(v & 127u); }
static void w14(unsigned v) { w7(v); w7(v >> 7); }
static void ws14(int v) { w14((unsigned)(clampi(v, -8192, 8191) + 8192)); }
static void w21(uint32_t v) { w7(v); w7(v >> 7); w7(v >> 14); }
static void w32(uint32_t v) { w7(v); w7(v >> 7); w7(v >> 14); w7(v >> 21); w7(v >> 28); }
static void wpack(const uint8_t *d, unsigned n)   /* 7 en 8, como ed_unpack7 de Felucca */
{
    for (unsigned i = 0; i < n; i += 7u) {
        unsigned k = n - i < 7u ? n - i : 7u, hi = 0;
        for (unsigned j = 0; j < k; j++) hi |= (unsigned)(d[i + j] >> 7) << j;
        w7(hi);
        for (unsigned j = 0; j < k; j++) w7(d[i + j]);
    }
}
static unsigned r7(void) { if (PX.ip >= PX.in_n) { PX.bad = 1; return 0; } return PX.in[PX.ip++]; }
static unsigned r14(void) { unsigned a = r7(); return a | r7() << 7; }
static int rs14(void) { return (int)r14() - 8192; }
static uint32_t r21(void) { uint32_t a = r7(), c = r7(); return a | c << 7 | (uint32_t)r7() << 14; }
static uint32_t r32(void) { uint32_t v = 0; for (unsigned i = 0; i < 5u; i++) v |= (uint32_t)r7() << (7u * i); return v; }
static unsigned rpack(uint8_t *d, unsigned cap)   /* lo que queda de la pregunta; cap de más: mal formado */
{
    unsigned n = 0;
    while (PX.ip < PX.in_n) {
        unsigned hi = PX.in[PX.ip++], k = PX.in_n - PX.ip < 7u ? PX.in_n - PX.ip : 7u;
        if (!k || hi >> k) { PX.bad = 1; return n; }
        for (unsigned j = 0; j < k; j++) { if (n >= cap) { PX.bad = 1; return n; } d[n++] = (uint8_t)(PX.in[PX.ip++] | ((hi >> j) & 1u) << 7); }
    }
    return n;
}
static uint32_t px_crc(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static void head(unsigned cmd, unsigned id, unsigned st)
{
    PX.on = 0;
    PX.out[PX.on++] = 0xF0; w7(0x7D); w7(0x46); w7(0x47); w7(cmd); w7(id); w7(st);
}
static void send(fg_box_t *b) { if (PX.on < sizeof PX.out) PX.out[PX.on++] = 0xF7; if (b->sysex_out) b->sysex_out(PX.out, PX.on); }
/* un texto: sin la marca "no traducir" y en el idioma del aparato, con su 0 */
static unsigned put_text(uint8_t *o, unsigned cap, const char *s)
{
    char t[48];
    fg_tr(s ? s : "", t, sizeof t);
    unsigned n = 0;
    while (t[n] && n + 1u < cap) { o[n] = (uint8_t)t[n]; n++; }
    o[n++] = 0;
    return n;
}

/* ----------------------------------------- cambios (el aviso CAMBIO) --- */
static uint32_t hsh(uint32_t h, const void *p, unsigned n) { const uint8_t *q = p; while (n--) { h ^= *q++; h *= 16777619u; } return h; }
static uint32_t h_track(const fg_track_t *t) { return hsh(2166136261u, t, sizeof *t); }
static uint32_t h_transport(const fg_box_t *b)
{
    uint8_t t[8] = {b->playing, b->s.cur, b->queued, b->mute, b->solo, b->s.chain_on, b->has_pend, b->pend};
    return hsh(hsh(2166136261u, t, sizeof t), b->s.crow, 32);
}
static uint32_t h_bank(void) { return hsh(hsh(2166136261u, &UBN, 1), UB, UBN * sizeof UB[0]); }
static void px_baseline(fg_box_t *b)
{
    PX.h_song = hsh(2166136261u, &b->s, sizeof b->s);
    for (unsigned p = 0; p < FBX_NP; p++) for (unsigned t = 0; t < 8; t++) PX.h_tr[p][t] = h_track(&b->Q.pat[p].tr[t]);
    PX.h_tp = h_transport(b); PX.h_bank = h_bank(); PX.h_lang = fg_lang; PX.h_usr = fg_usr_gen(); PX.h_slots = b->slots;
    PX.seen_gen = PX.gen; PX.base_ok = 1;
}
/* lo que cambió la app: sube el contador y no se avisa de vuelta */
static unsigned px_own(fg_box_t *b) { PX.count = (uint16_t)((PX.count + 1u) & 16383u); px_baseline(b); return PX.count; }
static void px_tick(fg_box_t *b)
{
    if (PX.listen && b->now - PX.listen_ms > 3000u) PX.listen = 0;
    if (PX.open && b->now - PX.ms > 5000u) { if (PX.obj == PO_SAMPLE && PX.wr) fg_usr_end(0); PX.open = 0; }
    if (!(PX.listen & 2u)) { PX.base_ok = 0; return; }
    if (!PX.base_ok) { px_baseline(b); return; }
    if (b->now - PX.last_ms < 100u) return;
    PX.last_ms = b->now;
    unsigned what = 0, pat = 127, trk = 0;
    uint32_t h = hsh(2166136261u, &b->s, sizeof b->s);
    if (h != PX.h_song) { PX.h_song = h; what |= 1u; }
    for (unsigned k = 0; k < 2u; k++) {                  /* el patrón que suena y otro más, por turnos */
        unsigned p = k ? (PX.rr = (uint8_t)((PX.rr + 1u) % FBX_NP)) : b->s.cur;
        for (unsigned t = 0; t < 8u; t++) {
            uint32_t x = h_track(&b->Q.pat[p].tr[t]);
            if (x == PX.h_tr[p][t]) continue;
            PX.h_tr[p][t] = x; what |= 2u;
            if (pat == 127u || pat == p) { pat = p; trk |= 1u << t; } else pat = 126u;
        }
    }
    if (pat == 126u) { pat = 127; trk = 0; }
    if ((h = h_transport(b)) != PX.h_tp) { PX.h_tp = h; what |= 4u; }
    if (PX.gen != PX.seen_gen) { PX.seen_gen = PX.gen; what |= 8u; }
    if ((h = h_bank()) != PX.h_bank) { PX.h_bank = h; what |= 16u; }
    if (b->slots != PX.h_slots) { PX.h_slots = b->slots; what |= 32u; }
    if (fg_usr_gen() != PX.h_usr) { PX.h_usr = fg_usr_gen(); what |= 64u; }
    if (fg_lang != PX.h_lang) { PX.h_lang = fg_lang; what |= 128u; }
    if (!what) return;
    PX.count = (uint16_t)((PX.count + 1u) & 16383u);
    uint8_t keep[720]; unsigned kn = PX.on;             /* (el aviso no pisa la respuesta guardada) */
    fg_copy(keep, PX.out, kn);
    head(PX_CAMBIO, 0, PX_BIEN); w14(PX.count); w14(what); w7(pat); w7(trk);
    send(b);
    fg_copy(PX.out, keep, kn); PX.on = kn;
}

/* ------------------------------------------ una perilla desde la app --- */
static const uint8_t PX_PAGE[7] = {PG_EDIT, PG_ENV, PG_FX, PG_SCL, PG_LFO, PG_GLO, PG_CHAIN};
typedef struct { uint8_t sel, page, pn, nhw, seqmode, crow; uint16_t down, bdown, wdown; } px_view_t;
/* la vista de esa pista y página, sin nada sostenido (lo que se haría con la perilla); 0 = no existe */
static int px_view(fg_box_t *b, px_view_t *sv, unsigned t, unsigned pg, unsigned sub)
{
    sv->sel = b->sel; sv->page = b->page; sv->pn = b->pn; sv->nhw = b->nhw; sv->seqmode = b->seqmode; sv->crow = b->crow_cur;
    sv->down = b->down; sv->bdown = b->bdown; sv->wdown = b->wdown;
    if (pg > 7u || sub > 1u || (pg == 6u ? t >= 16u : t >= 8u)) return 0;
    b->nhw = 0; b->down = 0; b->bdown = 0; b->wdown = 0;
    if (pg == 6u) { b->crow_cur = (uint8_t)t; b->page = PG_CHAIN; b->pn = 0; }
    else if (pg == 7u) { b->sel = (uint8_t)t; b->down = 1u << FB_ARP; }
    else { b->sel = (uint8_t)t; b->page = PX_PAGE[pg]; b->pn = (uint8_t)sub; }
    return 1;
}
static void px_unview(fg_box_t *b, const px_view_t *sv)
{
    b->sel = sv->sel; b->page = sv->page; b->pn = sv->pn; b->nhw = sv->nhw; b->seqmode = sv->seqmode; b->crow_cur = sv->crow;
    b->down = sv->down; b->bdown = sv->bdown; b->wdown = sv->wdown;
}
static void px_preset(fg_box_t *b, unsigned t, unsigned i)   /* como girar PRESET hasta ahí */
{
    unsigned m = b->s.model[t], n = preset_count(b, t), cur = b->preset_idx[t][m];
    uint8_t pv[8];
    if (cur >= n) cur = n - 1u;
    preset_at(b, t, cur, pv);
    if (!same(P(b, t), pv, 8)) {                         /* tu versión queda primera en la lista */
        fg_copy(b->s.edited[t][m], P(b, t), 8); b->s.has_edited[t] |= (uint16_t)(1u << m);
        n = preset_count(b, t);
    }
    if (i >= n) i = n - 1u;
    b->preset_idx[t][m] = (uint8_t)i;
    preset_at(b, t, i, pv);
    for (unsigned j = 0; j < 8; j++) { b->s.p[t][m][j] = pv[j]; send_param(b, t, m, j); }
    clear_acc(b); mark(b);
}

/* -------------------------------------------------------------- META --- */
static void px_meta(fg_box_t *b, unsigned id)
{
    static uint8_t o[600] FG_POOL;
    unsigned n = 0, ty = r7(), a = 0, bb = 0, c = 0, k = 0;
    int v = 0;
    if (ty == 1u || ty == 4u || ty == 5u || ty == 6u) a = r7();
    if (ty == 2u || ty == 3u) { a = r7(); bb = r7(); c = r7(); }
    if (ty == 3u) { k = r7(); v = rs14(); }
    if (ty == 4u) bb = r7();
    if (ty == 6u && a == 3u) bb = r7();
    if (PX.bad) { head(PX_META, id, PX_NO_ENTIENDO); return; }
    switch (ty) {
    case 0: for (unsigned t = 0; t < 8u; t++) n += put_text(o + n, sizeof o - n, TN[t]); break;
    case 1:
        if (a >= 8u) { head(PX_META, id, PX_FUERA); return; }
        o[n++] = NMODELS[a];
        for (unsigned m = 0; m < NMODELS[a]; m++) n += put_text(o + n, sizeof o - n, is_synth(a) ? YNAME[m] : MNAME[a][m]);
        break;
    case 2: case 3: {
        px_view_t sv;
        if (!px_view(b, &sv, a, bb, c)) { head(PX_META, id, PX_FUERA); return; }
        for (unsigned q = ty == 2u ? 0u : k; q < (ty == 2u ? 4u : k + 1u) && q < 4u; q++) {
            tgt_t g;
            if (!target(b, q, &g)) { if (ty == 2u) { o[n++] = 0; o[n++] = 0; o[n++] = 0; o[n++] = 0; o[n++] = 0; } continue; }
            if (ty == 2u) {
                n += put_text(o + n, sizeof o - n, g.label);
                o[n++] = (uint8_t)g.min; o[n++] = (uint8_t)((uint16_t)g.min >> 8); o[n++] = (uint8_t)g.max; o[n++] = (uint8_t)((uint16_t)g.max >> 8);
            } else { char t[28]; tfmt(b, &g, clampi(v, g.min, g.max), t, sizeof t); n += put_text(o + n, sizeof o - n, t); }
        }
        px_unview(b, &sv);
        if (ty == 3u && k >= 4u) { head(PX_META, id, PX_FUERA); return; }
        break; }
    case 4: {
        if (a >= 8u || bb >= NMODELS[a]) { head(PX_META, id, PX_FUERA); return; }
        uint8_t m0 = b->s.model[a], pv[8];
        b->s.model[a] = (uint8_t)bb;                     /* (solo para leer la lista: no se manda al audio) */
        unsigned cnt = preset_count(b, a), ed = b->s.has_edited[a] >> bb & 1u;
        o[n++] = (uint8_t)cnt;
        for (unsigned i = 0; i < cnt && n + 16u < sizeof o; i++) {
            const char *nm = preset_at(b, a, i, pv);
            n += put_text(o + n, sizeof o - n, i == 0 || (i == 1 && ed) ? nm : raw(nm));
        }
        b->s.model[a] = m0;
        break; }
    case 5: {
        static const uint8_t LO[4] = {FGSB_BOMBOS, FGSB_CAJAS, FGSB_TEXTURAS, FGSB_TUYOS}, HI[4] = {FGSB_CAJAS, FGSB_TEXTURAS, FGSB_TUYOS, FGSB_END};
        if (a >= 4u) { head(PX_META, id, PX_FUERA); return; }
        unsigned at = n++;
        o[at] = 0;
        for (unsigned s = LO[a]; s < HI[a]; s++) if (fg_samp_valid(s)) { o[n++] = (uint8_t)s; n += put_text(o + n, sizeof o - n, raw(fg_samp_at(s)->name)); o[at]++; }
        break; }
    case 6: {
        unsigned cnt = a == 0u ? 12u : a == 1u ? 8u : a == 2u ? 5u : a == 3u ? 8u : a == 4u ? 16u : 0u;
        if (!cnt || (a == 3u && bb >= 8u)) { head(PX_META, id, PX_FUERA); return; }
        o[n++] = (uint8_t)cnt;
        for (unsigned i = 0; i < cnt; i++) {
            px_view_t sv;
            const char *s = a == 0u ? CONDS[i] : a == 1u ? SCALEN[i] : a == 2u ? SHAPES[i] : a == 4u ? PERF[i].name : 0;
            if (a == 3u) { px_view(b, &sv, bb, 0, 0); s = PL(b, bb, i); px_unview(b, &sv); }
            n += put_text(o + n, sizeof o - n, s);
        }
        break; }
    default: head(PX_META, id, PX_NO_ENTIENDO); return;
    }
    head(PX_META, id, PX_BIEN);
    wpack(o, n);
}

/* --------------------------------------------------------- traslados --- */
static int px_flash_ok(fg_box_t *b) { return b->store_write != 0; }
static void px_abrir(fg_box_t *b, unsigned id)
{
    unsigned obj = r7(), ix = r7(), wr = r7();
    uint32_t len = 0, crc = 0;
    if (wr) { len = r21(); crc = r32(); }
    if (PX.bad || obj > PO_SAMPLE || wr > 1u) { head(PX_ABRIR, id, PX_NO_ENTIENDO); return; }
    if (PX.open) { head(PX_ABRIR, id, PX_OCUPADO); return; }
    if ((obj == PO_RANURA && ix >= FBX_SLOTS) || (obj == PO_SAMPLE && ix >= FGS_USR)) { head(PX_ABRIR, id, PX_FUERA); return; }
    if (wr) {
        if (obj == PO_AUTO) { head(PX_ABRIR, id, PX_NO_ENTIENDO); return; }
        if (obj != PO_PROYECTO && !px_flash_ok(b)) { head(PX_ABRIR, id, PX_SIN_FLASH); return; }
        if (obj != PO_PROYECTO && b->playing) { head(PX_ABRIR, id, PX_TOCANDO); return; }
        uint32_t cap = obj == PO_RANURA ? FBX_PROJ_MAX : obj == PO_BANCO ? 2u + FBX_UBANK * (uint32_t)sizeof UB[0] : sizeof pack_buf - 16u;
        if (obj == PO_SAMPLE) {
            fg_usr_ent_t e;
            uint8_t nm[16];
            fg_zero(&e, sizeof e);
            e.k = (uint8_t)ix; e.bytes = len; e.crc = crc;
            e.rate = r21(); e.len = r21(); e.root = (uint8_t)r7(); e.loop = (uint8_t)r7(); e.loop_s = r21(); e.loop_e = r21();
            unsigned nn = rpack(nm, sizeof nm);
            if (PX.bad) { head(PX_ABRIR, id, PX_NO_ENTIENDO); return; }
            for (unsigned i = 0; i < 11u; i++) e.name[i] = i < nn && nm[i] ? (char)nm[i] : 0;
            uint32_t room = 0;
            post(b, M_SAMPOFF, 0, 0, 0, 0); pause_audio(b); resume_audio(b);   /* (lo que suene de TUYOS se calla antes de borrar) */
            int r = fg_usr_begin(&e, &room);
            if (r == FGU_NO_CABE) { head(PX_ABRIR, id, PX_NO_CABE); w21(room); return; }
            if (r) { head(PX_ABRIR, id, r == FGU_FORMATO ? PX_FORMATO : r == FGU_FUERA ? PX_FUERA : PX_FALLO_FLASH); return; }
        } else if (len > cap) { head(PX_ABRIR, id, PX_NO_CABE); w21(cap); return; }
    } else {
        uint32_t n = 0;
        if (obj == PO_PROYECTO) n = fg_box_pack(b, pack_buf, sizeof pack_buf);
        else if (obj == PO_BANCO) { pack_buf[0] = 0x80u | UB_VER; pack_buf[1] = UBN; fg_copy(pack_buf + 2, UB, UBN * sizeof UB[0]); n = 2u + UBN * sizeof UB[0]; }
        else if (obj == PO_RANURA || obj == PO_AUTO) {
            if (!b->store_read || b->store_read(obj == PO_AUTO ? FBX_SLOTS : ix, pack_buf, sizeof pack_buf, &n) || !n) { head(PX_ABRIR, id, PX_VACIO); return; }
        } else {
            const fg_usr_ent_t *e = fg_usr_entry(ix);
            if (!e) { head(PX_ABRIR, id, PX_VACIO); return; }
            len = e->bytes; crc = e->crc;
        }
        if (obj != PO_SAMPLE) { if (!n) { head(PX_ABRIR, id, PX_FORMATO); return; } len = n; crc = px_crc(pack_buf, n); }
    }
    PX.open = 1; PX.obj = (uint8_t)obj; PX.idx = (uint8_t)ix; PX.wr = (uint8_t)wr; PX.len = len; PX.crc = crc; PX.got = 0; PX.ms = b->now;
    head(PX_ABRIR, id, PX_BIEN);
    if (!wr) { w21(len); w32(crc); }
}
static void px_leer(fg_box_t *b, unsigned id)
{
    static uint8_t t[PX_TROZO] FG_POOL;
    uint32_t from = r21(); unsigned n = r14();
    if (PX.bad) { head(PX_LEER, id, PX_NO_ENTIENDO); return; }
    if (!PX.open || PX.wr) { head(PX_LEER, id, PX_SIN_TRASLADO); return; }
    if (from > PX.len) { head(PX_LEER, id, PX_FUERA); return; }
    if (n > PX_TROZO) n = PX_TROZO;
    if (n > PX.len - from) n = PX.len - from;
    PX.ms = b->now;
    if (PX.obj == PO_SAMPLE) { if (fg_usr_read(PX.idx, from, t, n)) { head(PX_LEER, id, PX_FALLO_FLASH); return; } }
    else fg_copy(t, pack_buf + from, n);
    head(PX_LEER, id, PX_BIEN); w21(from); wpack(t, n);
}
static void px_escribir(fg_box_t *b, unsigned id)
{
    static uint8_t t[PX_TROZO + 8u] FG_POOL;
    uint32_t from = r21();
    unsigned n = rpack(t, sizeof t);
    if (PX.bad || n > PX_TROZO) { head(PX_ESCRIBIR, id, PX_NO_ENTIENDO); return; }
    if (!PX.open || !PX.wr) { head(PX_ESCRIBIR, id, PX_SIN_TRASLADO); return; }
    if (from != PX.got || PX.got + n > PX.len) { head(PX_ESCRIBIR, id, PX_FUERA); w21(PX.got); return; }
    PX.ms = b->now;
    if (PX.obj == PO_SAMPLE) {
        int r = fg_usr_data(from, t, n);
        if (r) { fg_usr_end(0); PX.open = 0; head(PX_ESCRIBIR, id, PX_FALLO_FLASH); return; }
    } else fg_copy(pack_buf + from, t, n);
    PX.got += n;
    head(PX_ESCRIBIR, id, PX_BIEN); w21(PX.got);
}
static int ub_from(const uint8_t *q, uint32_t n);
static void px_cerrar(fg_box_t *b, unsigned id)
{
    unsigned apply = r7();
    if (PX.bad) { head(PX_CERRAR, id, PX_NO_ENTIENDO); return; }
    if (!PX.open) { head(PX_CERRAR, id, PX_SIN_TRASLADO); return; }
    PX.open = 0;
    unsigned st = PX_BIEN;
    if (PX.wr && PX.obj == PO_SAMPLE) {
        int r = fg_usr_end((int)apply);
        st = !r ? PX_BIEN : r == FGU_CRC ? PX_CRC : r == FGU_FORMATO ? PX_FORMATO : PX_FALLO_FLASH;
    } else if (PX.wr && apply) {
        if (PX.got != PX.len || px_crc(pack_buf, PX.len) != PX.crc) st = PX_CRC;
        else if (PX.obj == PO_PROYECTO) {
            if (fg_box_unpack(b, pack_buf, PX.len)) st = PX_FORMATO;
            else { mark(b); undo_reset(b); }
        } else if (PX.obj == PO_BANCO) {
            if (ub_from(pack_buf, PX.len)) st = PX_FORMATO; else { ub_dirty = 1; ub_flush(b); }
        } else if (PX.obj == PO_RANURA) {
            uint32_t h[2];
            fg_copy(h, pack_buf, sizeof h);
            if (PX.len < 8u || h[0] != PACK_MAGIC) st = PX_FORMATO;
            else if (b->store_write(PX.idx, pack_buf, PX.len)) st = PX_FALLO_FLASH;
            else b->slots |= (uint16_t)(1u << PX.idx);
        }
    }
    head(PX_CERRAR, id, st); w14(px_own(b));
}
static void px_borrar(fg_box_t *b, unsigned id)
{
    unsigned obj = r7(), ix = r7(), st = PX_BIEN;
    if (PX.bad) { head(PX_BORRAR, id, PX_NO_ENTIENDO); return; }
    if (!px_flash_ok(b)) { head(PX_BORRAR, id, PX_SIN_FLASH); return; }
    if (b->playing) { head(PX_BORRAR, id, PX_TOCANDO); return; }
    if (PX.open) { head(PX_BORRAR, id, PX_OCUPADO); return; }
    if (obj == PO_RANURA) {
        if (ix >= FBX_SLOTS) st = PX_FUERA;
        else if (b->store_write(ix, pack_buf, 0)) st = PX_FALLO_FLASH;   /* (largo 0: la ranura queda vacía) */
        else b->slots &= (uint16_t)~(1u << ix);
    } else if (obj == PO_BANCO) {
        if (ix >= UBN) st = PX_FUERA;
        else { for (unsigned i = ix; i + 1u < UBN; i++) UB[i] = UB[i + 1u]; UBN--; ub_dirty = 1; ub_flush(b); }
    } else if (obj == PO_SAMPLE) {
        post(b, M_SAMPOFF, 0, 0, 0, 0); pause_audio(b); resume_audio(b);
        int r = fg_usr_delete(ix);
        st = !r ? PX_BIEN : r == FGU_VACIO ? PX_VACIO : r == FGU_FUERA ? PX_FUERA : PX_FALLO_FLASH;
    } else st = PX_NO_ENTIENDO;
    head(PX_BORRAR, id, st); w14(px_own(b));
}
static void px_lista(fg_box_t *b, unsigned id)
{
    static uint8_t o[600] FG_POOL;
    unsigned obj = r7(), n = 0;
    if (PX.bad) { head(PX_LISTA, id, PX_NO_ENTIENDO); return; }
    if (obj == PO_RANURA) {
        head(PX_LISTA, id, PX_BIEN); w7(FBX_SLOTS + 1u);
        for (unsigned i = 0; i <= FBX_SLOTS; i++) { uint32_t l = 0, c = 0; if (b->store_info) b->store_info(i, &l, &c); w21(l); w32(c); }
        return;
    }
    if (obj == PO_SAMPLE) {
        unsigned cnt = 0;
        for (unsigned k = 0; k < FGS_USR; k++) cnt += fg_usr_entry(k) != 0;
        head(PX_LISTA, id, PX_BIEN); w21(fg_usr_free()); w7(cnt);
        for (unsigned k = 0; k < FGS_USR; k++) {
            const fg_usr_ent_t *e = fg_usr_entry(k);
            if (!e) continue;
            uint8_t nm[12]; unsigned nn = 0;
            while (nn < 11u && e->name[nn]) { nm[nn] = (uint8_t)e->name[nn]; nn++; }
            w7(k); w21(e->len); w21(e->bytes); w32(e->crc); w21(e->rate); w7(e->root); w7(e->loop); w21(e->loop_s); w21(e->loop_e);
            w7(nn); wpack(nm, nn);                        /* (el largo antes: la app sabe dónde termina) */
        }
        return;
    }
    if (obj == PO_BANCO) {
        for (unsigned k = 0; k < UBN && n + 16u < sizeof o; k++) {
            unsigned j = 0;
            while (j < sizeof UB[0].name && UB[k].name[j]) o[n++] = (uint8_t)UB[k].name[j++];
            o[n++] = 0;
        }
        head(PX_LISTA, id, PX_BIEN); w7(UBN); wpack(o, n);
        return;
    }
    head(PX_LISTA, id, PX_NO_ENTIENDO);
}

/* ------------------------------------------------- perillas y pasos --- */
static void px_perilla(fg_box_t *b, unsigned id, int many)
{
    unsigned t = r7(), pg = r7(), sub = r7(), k = many ? 0u : r7();
    int v = many ? 0 : rs14();
    if (PX.bad) { head(PX_PERILLA + (unsigned)many, id, PX_NO_ENTIENDO); return; }
    if (!many && pg == 0u && k >= 4u && t < 8u) {        /* ALGO y PRESET */
        if (k == 4u) {
            v = clampi(v, 0, NMODELS[t] - 1);
            if (b->s.model[t] != v) { b->s.model[t] = (uint8_t)v; post(b, M_MODEL, t, (unsigned)v, 0, 0); clear_acc(b); mark(b); }
            head(PX_PERILLA, id, PX_BIEN); ws14(b->s.model[t]); w14(px_own(b));
        } else if (k == 5u) {
            px_preset(b, t, (unsigned)clampi(v, 0, 255));
            head(PX_PERILLA, id, PX_BIEN); ws14(b->preset_idx[t][b->s.model[t]]); w14(px_own(b));
        } else head(PX_PERILLA, id, PX_FUERA);
        return;
    }
    px_view_t sv;
    if (!px_view(b, &sv, t, pg, sub) || k >= 4u) { head(PX_PERILLA + (unsigned)many, id, PX_FUERA); return; }
    if (many) {
        head(PX_PERILLAS, id, PX_BIEN);
        for (unsigned q = 0; q < 4u; q++) { tgt_t g; ws14(target(b, q, &g) ? tget(b, &g) : 0); }
        px_unview(b, &sv);
        return;
    }
    tgt_t g;
    if (!target(b, k, &g)) { px_unview(b, &sv); head(PX_PERILLA, id, PX_FUERA); return; }
    v = clampi(v, g.min, g.max);
    if ((g.kind == TK_PARAM || g.kind == TK_PLOCK) && g.idx == 1u && is_samp(b, b->sel) && !fg_samp_valid((unsigned)v)) {
        unsigned w = fg_samp_step((unsigned)v, -1);   /* (un lugar vacío: el sample de antes o, si no hay, el siguiente) */
        v = (int)(fg_samp_valid(w) ? w : fg_samp_step((unsigned)v, 1));
    }
    if (g.kind == TK_SCL2 && g.idx == 0u) move_steps(b, v);
    else if (v != tget(b, &g)) tset(b, &g, v);
    int got = g.kind == TK_SCL2 && g.idx == 0u ? 0 : tget(b, &g);
    px_unview(b, &sv);
    head(PX_PERILLA, id, PX_BIEN); ws14(got); w14(px_own(b));
}
/* un paso o una pista en el formato del proyecto (sección 6.3): el mismo codificador de las ranuras */
static void px_paso(fg_box_t *b, unsigned id)
{
    uint8_t q[64];
    unsigned p = r7(), t = r7(), s = r7();
    q[0] = 32; q[1] = 1; q[2] = 0; q[3] = (uint8_t)s;
    unsigned n = 4u + rpack(q + 4, sizeof q - 6u);
    if (PX.bad) { head(PX_PASO, id, PX_NO_ENTIENDO); return; }
    if (p >= FBX_NP || t >= 8u || s >= FG_SEQ_NS) { head(PX_PASO, id, PX_FUERA); return; }
    q[n++] = 0xFF;
    static fg_track_t tmp FG_POOL;
    uint32_t at = 0;
    fg_step_t st;
    if (n == 5u || (n == 6u && q[4] == 0)) { fg_seq_clear_track(&tmp); st = tmp.st[0]; }   /* registro 00: paso vacío */
    else { if (dec_track(&tmp, q, n, &at, PACK_VER)) { head(PX_PASO, id, PX_FORMATO); return; } st = tmp.st[s]; }
    pause_audio(b);
    b->Q.pat[p].tr[t].st[s] = st;
    resume_audio(b);
    mark(b);
    head(PX_PASO, id, PX_BIEN); w14(px_own(b));
}
static void px_pista(fg_box_t *b, unsigned id)
{
    static uint8_t q[2048] FG_POOL;
    unsigned p = r7(), t = r7(), wr = r7();
    if (PX.bad || wr > 1u) { head(PX_PISTA, id, PX_NO_ENTIENDO); return; }
    if (p >= FBX_NP || t >= 8u) { head(PX_PISTA, id, PX_FUERA); return; }
    if (!wr) {
        uint32_t n = enc_track(&b->Q.pat[p].tr[t], q, sizeof q);
        if (n > PX_TROZO) { head(PX_PISTA, id, PX_NO_CABE); w21(n); return; }
        head(PX_PISTA, id, PX_BIEN); w7(b->s.eu[p][t][0]); w7(b->s.eu[p][t][1]); wpack(q, n);   /* (lo empacado, al final) */
        return;
    }
    unsigned n = rpack(q, sizeof q);
    static fg_track_t tmp FG_POOL;
    uint32_t at = 0;
    if (PX.bad || dec_track(&tmp, q, n, &at, PACK_VER)) { head(PX_PISTA, id, PX_FORMATO); return; }
    pause_audio(b);
    b->Q.pat[p].tr[t] = tmp;
    resume_audio(b);
    mark(b);
    head(PX_PISTA, id, PX_BIEN); w14(px_own(b));
}
static void px_transporte(fg_box_t *b, unsigned id)
{
    unsigned what = r7(), v = r7();
    if (what >= 2u) v |= r7() << 7;
    if (PX.bad) { head(PX_TRANSPORTE, id, PX_NO_ENTIENDO); return; }
    switch (what) {
    case 0: if (b->playing != (v != 0)) { b->playing = v != 0; post(b, M_PLAY, 0, 0, 0, b->playing); } break;
    case 1:
        if (v >= FBX_NP) { head(PX_TRANSPORTE, id, PX_FUERA); return; }
        if (b->s.chain_on) { b->s.chain_on = 0; send_chain(b, 0); }
        if (b->playing) post(b, M_QUEUE, 0, 0, 0, (int32_t)v); else { b->s.cur = (uint8_t)v; post(b, M_QUEUE, 0, 0, 0, (int32_t)v); }
        mark(b); break;
    case 2: b->mute = (uint8_t)v; b->has_pend = 0; send_mutes(b, 1); break;
    case 3: b->solo = (uint8_t)v; send_mutes(b, 1); break;
    default: head(PX_TRANSPORTE, id, PX_NO_ENTIENDO); return;
    }
    head(PX_TRANSPORTE, id, PX_BIEN); w14(px_own(b));
}
static void px_nombre(fg_box_t *b, unsigned id)
{
    uint8_t nm[16];
    unsigned k = r7(), n = rpack(nm, sizeof nm);
    if (PX.bad) { head(PX_NOMBRE, id, PX_NO_ENTIENDO); return; }
    if (k >= UBN) { head(PX_NOMBRE, id, PX_FUERA); return; }
    unsigned j = 0;
    while (j < n && j < 10u && nm[j]) j++;
    while (j && (nm[j] & 0xC0u) == 0x80u) j--;           /* (sin partir una letra con acento) */
    fg_copy(UB[k].name, nm, j); UB[k].name[j] = 0;
    ub_dirty = 1;
    head(PX_NOMBRE, id, PX_BIEN); w14(px_own(b));
}
static void px_ajuste(fg_box_t *b, unsigned id)
{
    unsigned which = r7(), wr = r7();
    int v = wr ? rs14() : 0;
    if (PX.bad) { head(PX_AJUSTE, id, PX_NO_ENTIENDO); return; }
    if (which != 0u) { head(PX_AJUSTE, id, PX_FUERA); return; }
    if (wr) { fg_lang = (uint8_t)clampi(v, 0, FG_LANGS - 1); dev_dirty = 1; px_own(b); }
    head(PX_AJUSTE, id, PX_BIEN); ws14(fg_lang);
}

/* m: desde cmd (después de 7D 46 47) */
void fg_box_sysex(fg_box_t *b, const uint8_t *m, unsigned n)
{
    if (n < 2u) return;
    unsigned cmd = m[0], id = m[1];
    if (cmd < 0x10u) return;                             /* (los del espejo van del aparato a la app) */
    if (PX.has_last && cmd == PX.last_cmd && id == PX.last_id && id && cmd != PX_ESCUCHA) { if (b->sysex_out) b->sysex_out(PX.out, PX.on); return; }
    PX.in = m; PX.ip = 2; PX.in_n = n; PX.bad = 0;
    switch (cmd) {
    case PX_INFO: {
        uint8_t v[48]; unsigned k = 0;
        const char *s = b->version ? b->version : "FNTSMG-1";
        while (s[k] && k < sizeof v) { v[k] = (uint8_t)s[k]; k++; }
        head(PX_INFO, id, PX_BIEN);
        w7(PX_VERSION); w7(PACK_VER); w7(FBX_NP); w7(8); w7(FG_SEQ_NS); w7(FBX_SLOTS); w21(FBX_PROJ_MAX); w7(FBX_UBANK); w14(PX_TROZO);
        w21(fg_usr_total()); w21(fg_usr_free()); w7(fg_lang); w14(PX.count); w7((b->store_write ? 0u : 1u) | (b->playing ? 2u : 0u));
        wpack(v, k);
        break; }
    case PX_ESCUCHA: PX.listen = (uint8_t)(r7() & 3u); PX.listen_ms = b->now; head(PX_ESCUCHA, id, PX.bad ? PX_NO_ENTIENDO : PX_BIEN); break;
    case PX_META: px_meta(b, id); break;
    case PX_ABRIR: px_abrir(b, id); break;
    case PX_LEER: px_leer(b, id); break;
    case PX_ESCRIBIR: px_escribir(b, id); break;
    case PX_CERRAR: px_cerrar(b, id); break;
    case PX_BORRAR: px_borrar(b, id); break;
    case PX_LISTA: px_lista(b, id); break;
    case PX_PERILLA: px_perilla(b, id, 0); break;
    case PX_PERILLAS: px_perilla(b, id, 1); break;
    case PX_PASO: px_paso(b, id); break;
    case PX_PISTA: px_pista(b, id); break;
    case PX_TRANSPORTE: px_transporte(b, id); break;
    case PX_NOMBRE: px_nombre(b, id); break;
    case PX_AJUSTE: px_ajuste(b, id); break;
    default: head(cmd, id, PX_NO_ENTIENDO); break;
    }
    PX.last_cmd = (uint8_t)cmd; PX.last_id = (uint8_t)id; PX.has_last = 1;
    send(b);
}
unsigned fg_box_listening(const fg_box_t *b) { (void)b; return PX.listen; }
/* el aparato usa pack_buf para guardar o abrir: un traslado abierto se cancela (manda la persona) o se espera (lo
 * automático); 1 = puede seguir */
static int px_take_buf(int user)
{
    if (!PX.open) return 1;
    if (!user) return 0;
    if (PX.obj == PO_SAMPLE && PX.wr) fg_usr_end(0);
    PX.open = 0;
    return 1;
}
