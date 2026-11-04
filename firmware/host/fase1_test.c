/* 0.10, fase 1: el sonido de cada patrón (cambia justo al empezar el patrón, se guarda y se migra), los toques
 * nuevos (un toque abre encima, otro la segunda página, mantener borra, HOME + botón), COPIAR / PEGAR, los FX
 * enganchados y el FILL (editar como suena y el automático) */
#include <stdio.h>
#include <string.h>
#include "fg_box.h"
static fg_box_t B;
static uint8_t VMEM[100000];
static uint8_t SLOT[FBX_DEV_SLOT + 1][16384];
static uint32_t SLOTN[FBX_DEV_SLOT + 1];
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }
int fg_core1_wait(void) { return 0; }
uint32_t fg_now(void) { return 0; }
static int s_write(unsigned slot, const void *d, uint32_t n) { if (n > sizeof SLOT[0]) return -1; memcpy(SLOT[slot], d, n); SLOTN[slot] = n; return 0; }
static int s_read(unsigned slot, void *d, uint32_t cap, uint32_t *n) { if (!SLOTN[slot] || SLOTN[slot] > cap) return -1; memcpy(d, SLOT[slot], SLOTN[slot]); *n = SLOTN[slot]; return 0; }
static int s_has(unsigned slot) { return SLOTN[slot] != 0; }
static const uint8_t W2K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t B2K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static int bad;
static long blk;
static int kit_at = -1, kit_blk = -1, step0_at = -1, step0_blk = -1, caja_hits;
static void check_ev(void)
{
    for (int k = 0; k < B.nev; k++) {
        const fg_seq_ev_t *e = &B.ev[k];
        if (e->kind == FG_EV_KIT && kit_blk < 0) { kit_blk = (int)blk; kit_at = e->at; }
        if (e->kind == FG_EV_STEP && e->t == 0 && e->s == 0 && kit_blk >= 0 && step0_blk < 0) { step0_blk = (int)blk; step0_at = e->at; }
        if (e->kind == FG_EV_TRIG && e->t == 1) caja_hits++;
    }
}
static void run(unsigned ms) { static int32_t o[2 * FG_BLOCK]; static double acc; for (unsigned i = 0; i < ms; i++) { B.now++; fg_box_tick(&B, B.now); acc += FG_SR / 1000.0; while (acc >= FG_BLOCK) { acc -= FG_BLOCK; fg_box_audio(&B, o, FG_BLOCK); blk++; check_ev(); } if (B.now % 33u == 0) { static uint16_t px[240 * 16]; fg_box_frame(&B); for (unsigned y = 0; y < 240; y += 16) fg_box_band(&B, y, px); } fg_box_idle(&B); } }
static void btn(unsigned id, int d) { fg_box_button(&B, id, d); run(20); }
static void tap(unsigned id) { btn(id, 1); btn(id, 0); }
static void white(unsigned i, int d) { fg_box_key(&B, W2K[i], d); run(20); }
static void wtap(unsigned i) { white(i, 1); white(i, 0); }
static void black(unsigned i, int d) { fg_box_key(&B, B2K[i], d); run(20); }
static void ok(int c, const char *what) { printf("%s  %s\n", c ? "bien " : "FALLA", what); bad += !c; }
static void to_pat(unsigned p) { btn(FB_HOME, 1); wtap(p); btn(FB_HOME, 0); run(80); }

int main(void)
{
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    fg_box_init(&B, VMEM, 240, 240, 4, 1); fg_box_boot(&B); run(3000);

    /* ---- el sonido de cada patrón ---- */
    while (B.sel != 4) { fg_box_enc(&B, FE_SELECT, 1); run(5); }
    unsigned m0 = B.s.model[4];
    to_pat(1);
    ok(B.s.cur == 1 && B.s.model[4] == m0, "un patrón vacío empieza con el sonido del que sonaba");
    fg_box_enc(&B, FE_ALGO, 1); run(20);
    unsigned m1 = B.s.model[4];
    tap(FB_ENV); fg_box_enc(&B, FE_K1, -10); run(20);
    unsigned vol1 = B.s.mix[4][0];
    tap(FB_SEQ); run(20);                                                /* (de la mezcla a los pasos) */
    wtap(0); wtap(4); run(20);                                           /* (pasos: el patrón 1 ya tiene algo) */
    to_pat(0);
    ok(B.s.model[4] == m0 && B.a_model[4] == m0 && B.Q.model[4] == m0, "el patrón 0 regresa con su motor (canción, motor y secuenciador)");
    ok(B.s.mix[4][0] != vol1, "y con su volumen");
    to_pat(1);
    ok(B.s.model[4] == m1 && B.a_model[4] == m1 && B.s.mix[4][0] == vol1, "el patrón 1 regresa con el suyo");
    /* tocando: el cambio cae en el primer paso del patrón nuevo */
    tap(FB_PLAY); run(300);
    kit_blk = step0_blk = -1;
    to_pat(0);
    run(4500);
    printf("      sonido nuevo en el bloque %d muestra %d; paso 1 del bombo en el bloque %d muestra %d\n", kit_blk, kit_at, step0_blk, step0_at);
    ok(kit_blk >= 0 && kit_blk == step0_blk && kit_at == step0_at, "tocando, el sonido cambia en la muestra del primer paso del patrón nuevo");
    ok(B.a_model[4] == m0, "y el motor quedó el del patrón 0");
    tap(FB_PLAY); run(100);
    /* guardar y abrir: los dos sonidos */
    uint8_t buf[16384];
    uint32_t n = fg_box_pack(&B, buf, sizeof buf);
    printf("      proyecto v8: %u bytes (la ranura aguanta %u)\n", n, FBX_PROJ_MAX);
    fbx_kit_t k0 = B.kit[0], k1 = B.kit[1];
    uint16_t own = B.kit_own;
    fg_box_init(&B, VMEM, 240, 240, 4, 2);
    B.store_write = s_write; B.store_read = s_read; B.store_has = s_has;
    ok(fg_box_unpack(&B, buf, n) == 0, "abre el proyecto v8");
    ok(!memcmp(&B.kit[0], &k0, sizeof k0) && !memcmp(&B.kit[1], &k1, sizeof k1) && B.kit_own == own, "con el sonido de cada patrón igual");
    run(50);

    /* ---- toques ---- */
    tap(FB_EDIT); run(20);
    if (!B.seqmode) tap(FB_SEQ);
    tap(FB_ENV); run(20);
    ok(B.page == PG_ENV && B.vui.view == FG_VIEW_MIX, "ENV abre la mezcla encima de SEQ");
    unsigned mu = B.mute; wtap(2); run(30);
    ok((B.mute ^ mu) == 4u, "en la mezcla, una blanca silencia su pista");
    wtap(2); run(30);
    tap(FB_ARP); run(20);
    ok(B.page == PG_ARP && !B.seqmode, "ARP abre su página y las blancas tocan notas");
    tap(FB_ENV); run(20);
    ok(B.page == PG_ENV && B.vui.view == FG_VIEW_MIX, "ENV abre la mezcla también desde TOCAR");
    tap(FB_ARP); run(20);
    while (B.sel != 4) { fg_box_enc(&B, FE_SELECT, 1); run(5); }
    if ((B.s.arp_on >> 4) & 1u) { fg_box_enc(&B, FE_K1, -6); run(20); }
    run(100); fg_box_enc(&B, FE_K1, 1); run(100); fg_box_enc(&B, FE_K1, 1); run(100); fg_box_enc(&B, FE_K1, 1); run(20);
    ok(((B.s.arp_on >> 4) & 1u) && B.s.arp[4][0] == 0, "ARP: MODO de NO a ARRIBA prende el arpegiador");
    tap(FB_ARP); run(20);
    fg_box_enc(&B, FE_K2, 2); run(20);
    ok(B.pn == 1 && B.s.arp2[4][1] >= 1, "ARP otra vez: segunda página (REPETIR)");
    fg_box_enc(&B, FE_K1, -6); run(20);
    tap(FB_ARP); fg_box_enc(&B, FE_K1, -6); run(20);
    ok(!((B.s.arp_on >> 4) & 1u), "MODO en NO lo apaga");
    tap(FB_SEQ); run(20);
    ok(B.seqmode && B.page == PG_EDIT, "SEQ: las blancas son pasos");
    tap(FB_SEQ); run(20);
    ok(B.page == PG_PASO, "SEQ otra vez: la página PASO");
    white(3, 1); fg_box_enc(&B, FE_K2, -20); run(20); white(3, 0);
    ok(B.Q.pat[B.s.cur].tr[4].st[3].p < 100, "PASO: sostener un paso y girar cambia su probabilidad");
    tap(FB_SEQ); run(20);
    ok(B.page == PG_EDIT, "SEQ una tercera vez: de regreso a EDIT");
    /* HOME */
    tap(FB_HOME); run(20);
    ok(B.at_home && B.page == PG_EDIT, "HOME: al visual");
    tap(FB_HOME); run(20);
    ok(B.page == PG_PATS && B.vui.view == FG_VIEW_PAT, "HOME otra vez: los PATRONES");
    wtap(5); run(80);
    ok(B.s.cur == 5, "PATRONES: una blanca elige el patrón");
    for (unsigned p = 0; p < 4; p++) B.Q.pat[p].tr[0].st[0].v = 1;
    btn(FB_HOME, 1); wtap(1); wtap(2); wtap(3); run(1500); btn(FB_HOME, 0); run(80);
    ok(B.page == PG_PATS && B.Q.pat[1].tr[0].st[0].v && B.Q.pat[2].tr[0].st[0].v && B.Q.pat[3].tr[0].st[0].v && B.s.chain_on,
       "PATRONES: HOME + blancas arma la cadena y no borra nada");
    white(2, 1); run(1500); white(2, 0); run(20);
    ok(B.Q.pat[2].tr[0].st[0].v, "PATRONES: mantener 1 s una vez no borra (pide confirmar)");
    run(3200); white(2, 1); run(1100); white(2, 0); run(20);
    ok(B.Q.pat[2].tr[0].st[0].v, "la confirmación caduca a los 3 s");
    white(1, 1); run(1100); white(1, 0); run(200); white(1, 1); run(1100); white(1, 0);
    ok(!B.Q.pat[1].tr[0].st[0].v, "otra vez 1 s antes de 3 s: lo vacía");
    white(0, 1); run(1100); white(0, 0); white(2, 1); run(1100); white(2, 0);
    ok(B.Q.pat[0].tr[0].st[0].v && B.Q.pat[2].tr[0].st[0].v, "confirmar con otra blanca no borra ninguna");
    white(0, 1); run(1100); white(0, 0); white(0, 1); run(1100); white(0, 0);
    ok(!B.Q.pat[0].tr[0].st[0].v, "PATRONES: mantener una blanca 1 s, dos veces, la vacía");
    tap(FB_HOME); run(20);
    ok(B.page == PG_EDIT, "HOME desde los PATRONES: al visual");
    btn(FB_HOME, 1); tap(FB_SEQ); btn(FB_HOME, 0); run(20);
    ok(B.page == PG_CHAIN, "HOME + SEQ: la CADENA");
    btn(FB_HOME, 1); tap(FB_SEQ); btn(FB_HOME, 0); run(20);
    while (B.sel != 5) { fg_box_enc(&B, FE_SELECT, 1); run(5); }
    run(800);
    uint8_t before[8]; memcpy(before, B.s.p[5][B.s.model[5]], 8);
    btn(FB_HOME, 1); tap(FB_EDIT); btn(FB_HOME, 0); run(800);
    ok(memcmp(before, B.s.p[5][B.s.model[5]], 8) != 0, "HOME + EDIT: sonido al azar");
    btn(FB_HOME, 1); tap(FB_OCTDN); btn(FB_HOME, 0); run(20);
    ok(!memcmp(before, B.s.p[5][B.s.model[5]], 8), "HOME + OCT −: deshacer");
    /* COPIAR / PEGAR */
    if (!B.seqmode) tap(FB_SEQ);
    fg_track_t *tr = &B.Q.pat[B.s.cur].tr[5];
    tr->st[2].v = 2; tr->st[2].lk[0] = 70; tr->st[9].v = 0;
    black(9, 1); wtap(2); black(9, 0);
    black(10, 1); wtap(9); black(10, 0);
    ok(tr->st[9].v == 2 && tr->st[9].lk[0] == 70, "COPIAR + paso, PEGAR + otro paso");
    black(9, 1); black(5, 1); black(5, 0); black(9, 0);
    black(10, 1); black(7, 1); black(7, 0); black(10, 0);
    ok(!memcmp(&B.Q.pat[B.s.cur].tr[7], tr, sizeof *tr), "COPIAR + pista, PEGAR + otra pista");
    /* FX: tocarlo abre su página (al soltarlo); sostenerlo + blanca = el efecto mientras se sostiene la blanca;
     * soltar FX antes que la blanca lo deja puesto; FX + esa blanca lo quita */
    tap(FB_SEQ); if (B.page == PG_PASO) tap(FB_SEQ);
    tap(FB_FX); run(20);
    ok(B.page == PG_FX && B.seqmode, "FX abre su página y las blancas siguen siendo pasos");
    tap(FB_SEQ); if (B.page == PG_PASO) tap(FB_SEQ);
    unsigned pg0 = B.page; uint16_t st0 = 0; for (unsigned i = 0; i < 16; i++) st0 |= (uint16_t)((B.Q.pat[B.s.cur].tr[B.sel].st[i].v != 0) << i);
    btn(FB_FX, 1); white(1, 1); run(50);
    ok((B.perf >> 1) & 1u, "FX sostenido + blanca: suena");
    white(1, 0); run(50);
    ok(!((B.perf >> 1) & 1u), "al soltar la blanca se apaga");
    btn(FB_FX, 0); run(20);
    uint16_t st1 = 0; for (unsigned i = 0; i < 16; i++) st1 |= (uint16_t)((B.Q.pat[B.s.cur].tr[B.sel].st[i].v != 0) << i);
    ok(B.page == pg0 && st1 == st0, "usar FX para un efecto no cambia de página ni toca los pasos");
    btn(FB_FX, 1); white(0, 1); run(50); btn(FB_FX, 0); run(20); white(0, 0); run(50);
    ok((B.perf & 1u) && (B.latch & 1u), "soltar FX antes que la blanca: se queda puesto");
    btn(FB_FX, 1); wtap(0); btn(FB_FX, 0); run(50);
    ok(!(B.perf & 1u) && !(B.latch & 1u), "FX + esa blanca otra vez: lo quita");
    /* FILL */
    btn(FB_FX, 1); white(FBX_PERF_FILL, 1); btn(FB_FX, 0); white(FBX_PERF_FILL, 0); run(50);
    ok(B.Q.fill, "FILL enganchado: el secuenciador en fill");
    tap(FB_SEQ); run(20);
    tr = &B.Q.pat[B.s.cur].tr[5];
    fg_seq_clear_track(tr); tr->st[0].v = 1;
    wtap(4); run(20);                                                    /* prender en el fill: solo en el fill */
    wtap(0); run(20);                                                    /* apagar en el fill: solo fuera de él */
    ok(tr->st[4].v && tr->st[4].c == FG_COND_FILL && tr->st[0].v && tr->st[0].c == FG_COND_NOFILL, "con FILL, lo que se prende es de fill y lo que se apaga, sin fill");
    btn(FB_FX, 1); wtap(FBX_PERF_FILL); btn(FB_FX, 0); run(50);
    ok(!B.Q.fill, "FILL otra vez: fuera");
    /* el automático: un patrón sin pasos de fill; la caja redobla */
    to_pat(9);
    for (unsigned t = 0; t < 8; t++) fg_seq_clear_track(&B.Q.pat[9].tr[t]);
    B.Q.pat[9].tr[0].st[0].v = 1;
    tap(FB_PLAY); run(200);
    caja_hits = 0; run(1935); unsigned plain = (unsigned)caja_hits;
    btn(FB_FX, 1); white(FBX_PERF_FILL, 1); btn(FB_FX, 0); white(FBX_PERF_FILL, 0); caja_hits = 0; run(1935); unsigned fillh = (unsigned)caja_hits;
    btn(FB_FX, 1); wtap(FBX_PERF_FILL); btn(FB_FX, 0); tap(FB_PLAY); run(50);
    printf("      caja en un compás: %u sin fill, %u con el fill automático\n", plain, fillh);
    ok(plain == 0 && fillh >= 16, "FILL sin pasos de fill: el fill lo pone la caja");
    /* mantener 1 s */
    tap(FB_SEQ); white(6, 1); white(6, 0);
    B.Q.pat[B.s.cur].tr[B.sel].st[6].au[0] = 1; B.Q.pat[B.s.cur].tr[B.sel].st[6].au[1] = 30;
    unsigned rec = B.rec;
    btn(FB_REC, 1); run(1100); btn(FB_REC, 0);
    ok(B.Q.pat[B.s.cur].tr[B.sel].st[6].au[0] == FG_NONE && B.rec == rec, "REC 1 s: borra la automatización (y no prende REC)");
    tap(FB_REC); ok(B.rec != rec, "REC tocado: prende o apaga"); tap(FB_REC);
    /* REC abre TOCAR; apagar REC no lo cierra; solo SEQ regresa a pasos */
    tap(FB_SEQ); if (B.page == PG_PASO) tap(FB_SEQ);
    B.rec = 0; tap(FB_REC);
    ok(B.rec && !B.seqmode && B.page == PG_EDIT, "REC: entra a TOCAR (las blancas son notas)");
    tap(FB_REC);
    ok(!B.rec && !B.seqmode, "REC apagado: sigue en TOCAR");
    tap(FB_SEQ);
    ok(B.seqmode, "SEQ: de regreso a pasos");
    /* RANURAS */
    tap(FB_SAVE); run(20);
    ok(B.page == PG_SLOTS && SLOTN[FBX_SLOTS], "SAVE: guarda y abre las RANURAS");
    wtap(3); run(20);
    ok(SLOTN[3] != 0 && ((B.slots >> 3) & 1u), "RANURAS: una vacía guarda ahí");
    B.s.bpm = 99;
    wtap(3); run(20);
    ok(B.s.bpm == 99 && B.slot_open == 4, "una llena: el primer toque no abre");
    wtap(3); run(20);
    ok(B.s.bpm != 99, "el segundo la abre");
    tap(FB_SAVE); run(20);
    ok(B.page == PG_EDIT, "SAVE otra vez: de regreso");
    /* lock de mezcla: sostener un paso, tocar ENV sin soltarlo y girar */
    {
        if (B.page == PG_PASO) tap(FB_SEQ);
        if (!B.seqmode) tap(FB_SEQ);
        fg_track_t *tr = &B.Q.pat[B.s.cur].tr[B.sel];
        tr->st[4].lk[8] = FG_NONE; uint8_t vol = B.s.mix[B.sel][0], v4 = tr->st[4].v;
        white(4, 1); tap(FB_ENV);
        ok(B.page == PG_ENV, "con un paso sostenido, ENV abre la mezcla");
        fg_box_enc(&B, FE_K1, -8); run(20);
        ok(tr->st[4].lk[8] != FG_NONE && B.s.mix[B.sel][0] == vol, "girar pone un lock de volumen en ese paso (la mezcla no cambia)");
        tap(FB_ENV);
        ok(tr->st[4].lk[8] == FG_NONE, "ENV otra vez con el paso sostenido: borra sus locks de mezcla");
        white(4, 0);
        ok(tr->st[4].v == v4, "soltar el paso en la mezcla no lo prende ni lo apaga");
        fg_box_enc(&B, FE_K1, -8); run(20);
        ok(B.s.mix[B.sel][0] != vol && tr->st[4].lk[8] == FG_NONE, "sin paso sostenido, la perilla mueve la mezcla");
    }
    printf(bad ? "FALLAS %d\n" : "todo bien\n", bad);
    return bad;
}
