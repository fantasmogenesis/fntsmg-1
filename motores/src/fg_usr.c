#include "fg_usr.h"
#include "fg_dsp.h"

#define SEC 4096u
#define HDR 16u
#define IDX_MAGIC 0x31555746u                    /* "FGU1" */
#define ENT_MAGIC 0x45555746u                    /* "FGUE" */
#define NREC ((SEC - HDR) / sizeof(fg_usr_ent_t))   /* 85 entradas antes de reescribir el índice */

static const fg_usr_io_t *io;
static int16_t live[FGS_USR] FG_POOL;             /* su entrada en el registro, -1 = vacío (todo se llena en fg_usr_init) */
static fg_usr_ent_t ent[FGS_USR] FG_POOL;
static char names[FGS_USR][12] FG_POOL;
static unsigned nrec;
static uint32_t gen;
static struct { uint8_t on; fg_usr_ent_t e; uint32_t got, erased; } w;   /* el que se está escribiendo */

static uint32_t nsec(void) { uint32_t n = io ? (io->size - SEC) / SEC : 0; return n > 64u ? 64u : n; }
static uint32_t dbase(void) { return io->base + SEC; }
static uint32_t rec_at(unsigned i) { return io->base + HDR + i * (uint32_t)sizeof(fg_usr_ent_t); }
static uint32_t crc_run(uint32_t c, const uint8_t *p, uint32_t n)
{
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return c;
}
static int sane(const fg_usr_ent_t *e)
{
    return e->k < FGS_USR && e->len && e->bytes >= (e->len + 63u) / 64u * 36u && e->rate >= 4000u && e->rate <= 96000u &&
           (!e->loop || (e->loop_s < e->loop_e && e->loop_e <= e->len)) && e->off % SEC == 0 && e->off + e->bytes <= nsec() * SEC;
}
static void publish(unsigned k)
{
    fg_sample_t *s = &fg_usr[k];
    fg_usr_mask &= ~(1u << k);
    if (live[k] < 0) return;
    const fg_usr_ent_t *e = &ent[k];
    for (unsigned i = 0; i < 12u; i++) names[k][i] = i < 11u ? e->name[i] : 0;
    s->data = io->ptr(dbase() + e->off); s->len = e->len; s->rate = e->rate; s->root = e->root;
    s->loop = e->loop; s->loop_s = e->loop_s; s->loop_e = e->loop_e; s->name = names[k];
    fg_usr_mask |= 1u << k;
}
static int put_rec(unsigned i, const fg_usr_ent_t *e)   /* la entrada y, al final, su marca */
{
    uint32_t m = ENT_MAGIC;
    if (io->pr(rec_at(i) + 4u, (const uint8_t *)e + 4, sizeof *e - 4u)) return FGU_FLASH;
    return io->pr(rec_at(i), &m, 4u) ? FGU_FLASH : FGU_OK;
}

void fg_usr_init(const fg_usr_io_t *x)
{
    uint32_t m = 0;
    io = x; nrec = NREC; gen++; w.on = 0;
    for (unsigned k = 0; k < FGS_USR; k++) live[k] = -1;
    fg_usr_mask = 0;
    if (!io || io->rd(io->base, &m, 4u) || m != IDX_MAGIC) return;   /* sin índice: el primero que se escriba lo arma */
    for (nrec = 0; nrec < NREC; nrec++) {
        fg_usr_ent_t e;
        if (io->rd(rec_at(nrec), &e, sizeof e) || e.magic == 0xFFFFFFFFu) break;
        if (e.magic == ENT_MAGIC && sane(&e)) { live[e.k] = (int16_t)nrec; ent[e.k] = e; }   /* (la más nueva gana) */
    }
    for (unsigned k = 0; k < FGS_USR; k++) publish(k);
}

static uint64_t used(void)
{
    uint64_t u = 0;
    for (unsigned k = 0; k < FGS_USR; k++)
        if (live[k] >= 0) for (uint32_t s = ent[k].off / SEC; s < (ent[k].off + ent[k].bytes + SEC - 1u) / SEC; s++) u |= 1ull << s;
    return u;
}
uint32_t fg_usr_total(void) { return nsec() * SEC; }
uint32_t fg_usr_free(void)
{
    uint64_t u = used();
    uint32_t n = 0;
    for (uint32_t s = 0; s < nsec(); s++) n += !(u >> s & 1u);
    return n * SEC;
}
uint32_t fg_usr_gen(void) { return gen; }
const fg_usr_ent_t *fg_usr_entry(unsigned k) { return k < FGS_USR && live[k] >= 0 ? &ent[k] : 0; }

static int compact(void)                          /* el registro lleno: se reescribe con lo vivo */
{
    uint32_t h[4] = {IDX_MAGIC, 1u, 0xFFFFFFFFu, 0xFFFFFFFFu};
    if (io->er(io->base) || io->pr(io->base, h, sizeof h)) return FGU_FLASH;
    nrec = 0;
    for (unsigned k = 0; k < FGS_USR; k++) {
        if (live[k] < 0) continue;
        if (put_rec(nrec, &ent[k])) return FGU_FLASH;
        live[k] = (int16_t)nrec++;
    }
    return FGU_OK;
}

int fg_usr_begin(const fg_usr_ent_t *e, uint32_t *room)
{
    if (!io) return FGU_FLASH;
    if (w.on) return FGU_OCUPADO;
    if (e->k >= FGS_USR) return FGU_FUERA;
    fg_usr_ent_t t = *e;
    t.off = 0;
    uint64_t u = used();
    uint32_t need = (t.bytes + SEC - 1u) / SEC, run = 0, best = 0, at = 0xFFFFFFFFu;
    for (uint32_t s = 0; s < nsec(); s++) {
        run = u >> s & 1u ? 0 : run + 1u;
        if (run > best) best = run;
        if (run == need && at == 0xFFFFFFFFu) at = s + 1u - need;
    }
    if (t.bytes > nsec() * SEC) at = 0xFFFFFFFFu;
    else if (!sane(&t)) return FGU_FORMATO;
    if (at == 0xFFFFFFFFu) { if (room) *room = best * SEC; return FGU_NO_CABE; }
    if (nrec + 1u > NREC && compact()) return FGU_FLASH;
    w.e = t; w.e.off = at * SEC; w.e.magic = ENT_MAGIC; w.e.rsv = 0xFF;
    w.got = 0; w.erased = w.e.off; w.on = 1;
    return FGU_OK;
}
int fg_usr_data(uint32_t from, const uint8_t *d, uint32_t n)
{
    if (!w.on) return FGU_VACIO;
    if (from != w.got || w.got + n > w.e.bytes) return FGU_FUERA;
    while (w.erased < w.e.off + w.got + n) { if (io->er(dbase() + w.erased)) return FGU_FLASH; w.erased += SEC; }
    if (n && io->pr(dbase() + w.e.off + w.got, d, n)) return FGU_FLASH;
    w.got += n;
    return FGU_OK;
}
int fg_usr_end(int apply)
{
    if (!w.on) return FGU_VACIO;
    w.on = 0;
    if (!apply) return FGU_OK;
    if (w.got != w.e.bytes) return FGU_FORMATO;
    uint8_t t[256];
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < w.e.bytes; i += sizeof t) {
        uint32_t k = w.e.bytes - i < sizeof t ? w.e.bytes - i : sizeof t;
        if (io->rd(dbase() + w.e.off + i, t, k)) return FGU_FLASH;
        c = crc_run(c, t, k);
    }
    if (~c != w.e.crc) return FGU_CRC;
    if (nrec >= NREC && compact()) return FGU_FLASH;
    if (nrec == 0) {                               /* (el primero sin índice: compact() lo arma) */
        uint32_t m = 0;
        if (io->rd(io->base, &m, 4u) || m != IDX_MAGIC) { if (compact()) return FGU_FLASH; }
    }
    unsigned k = w.e.k;
    int old = live[k];
    if (put_rec(nrec, &w.e)) return FGU_FLASH;
    live[k] = (int16_t)nrec++; ent[k] = w.e;
    if (old >= 0) { uint32_t z = 0; io->pr(rec_at((unsigned)old), &z, 4u); }   /* la anterior se anula después */
    gen++;
    publish(k);
    return FGU_OK;
}
int fg_usr_delete(unsigned k)
{
    if (k >= FGS_USR) return FGU_FUERA;
    if (live[k] < 0) return FGU_VACIO;
    uint32_t z = 0;
    if (io->pr(rec_at((unsigned)live[k]), &z, 4u)) return FGU_FLASH;
    live[k] = -1; gen++;
    publish(k);
    return FGU_OK;
}
int fg_usr_read(unsigned k, uint32_t from, uint8_t *d, uint32_t n)
{
    const fg_usr_ent_t *e = fg_usr_entry(k);
    if (!e) return FGU_VACIO;
    if (from > e->bytes || n > e->bytes - from) return FGU_FUERA;
    return io->rd(dbase() + e->off + from, d, n) ? FGU_FLASH : FGU_OK;
}
