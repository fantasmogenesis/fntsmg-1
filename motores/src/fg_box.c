/* La caja en C (ver fg_box.h). La interfaz es la de caja/index.html, función por función: target() dice qué
 * hace cada perilla según la página y lo que esté sostenido; press/release, whitePress/blackPress y encoder()
 * llevan los mismos nombres. Los textos van en mayúsculas (la letra de la pantalla solo tiene mayúsculas). */
#include <stddef.h>
#include "fg_box.h"
#include "fg_dsp.h"
#include "fg_lang.h"

/* la app (fg_box_proto.h, incluido al final) */
static void px_init(void);
static void px_tick(fg_box_t *b);
static int px_take_buf(int user);                          /* (comparte pack_buf con el guardado) */

/* ------------------------------------------------------------------ tablas --- */

static const char *const TN[8] = {"BOMBO", "CAJA", "METAL", "PERC", "SINTE A", "SINTE B", "SINTE C", "SINTE D"};
static const uint8_t NMODELS[8] = {3, 3, 3, 3, FGY_MODELS, FGY_MODELS, FGY_MODELS, FGY_MODELS};
#define DSAMPLE 2u                                       /* SAMPLE en la percusión (en los sintes: FGY_SAMPLE) */
/* los nombres de los motores no se traducen: "\1" adelante (fg_lang.c lo quita) */
static const char *const MNAME[4][3] = {{"\1BARRIDO", "\1MEMBRANA", "\1SAMPLE"}, {"\1CAJA", "\1APLAUSO", "\1SAMPLE"}, {"\1GRANIZO", "\1PLACA", "\1SAMPLE"},
                                        {"\1GOTA", "\1CHATARRA", "\1SAMPLE"}};
static const char *const YNAME[FGY_MODELS] = {"\1SH", "\1ÁCIDO", "\1FM", "\1CUERDAS", "\1TEXTURA", "\1DRON", "\1DUB", "\1RESONADOR", "\1SAMPLE"};
#define SAMPLE_L {"AFINA", "SAMPLE", "INICIO", "CAÍDA", "CORTE", "CRUJIDO", "MODO"}
static const char *const PLAB_D[4][3][7] = {
    {{"TONO", "BARRIDO", "CAÍDA", "DOBLEZ", "SATURA", "CLICK", "PEGADA"}, {"TONO", "TENSIÓN", "CAÍDA", "MATERIAL", "GOLPE", "SATURA", "PEGADA"},
     SAMPLE_L},
    {{"TONO", "CUERPO", "RUIDO", "CAÍDA", "BRILLO", "CHASQUIDO", "PEGADA"}, {"TONO", "PALMAS", "SEPARA", "CAÍDA", "SALA", "COLOR", "PEGADA"},
     SAMPLE_L},
    {{"DENSIDAD", "CAÍDA", "COLOR", "RESO", "AGITA", "GRANO", "PEGADA"}, {"TONO", "CAÍDA", "FORMA", "BRILLO", "GOLPE", "BATIDO", "PEGADA"}, SAMPLE_L},
    {{"TONO", "GOTA", "RESONANCIA", "DUREZA", "REBOTE", "MATERIAL", "PEGADA"}, {"TONO", "CAOS", "PEINE", "CAÍDA", "ROTURA", "BITS", "RUIDO"}, SAMPLE_L}};
static const char *const PLAB_Y[FGY_MODELS][7] = {
    {"NOTA", "ONDA", "SUB", "CORTE", "RESO", "ENV", "FORMA"}, {"NOTA", "CORTE", "RESO", "ENV", "CAÍDA", "RUGOSIDAD", "GLIDE"},
    {"NOTA", "RAZÓN", "ÍNDICE", "FEEDBACK", "GOLPE", "CAÍDA", "RUGIDO"}, {"NOTA", "DESAFINA", "BRILLO", "CORO", "ATAQUE", "SALIDA", "SUB"},
    {"NOTA", "MUESTRA", "POSICIÓN", "MOVIMIENTO", "BRILLO", "LAVADO", "SALIDA"}, {"NOTA", "LATIDO", "FORMA", "MOVIMIENTO", "BRILLO", "ENSAMBLE", "SATURA"},
    {"NOTA", "TIPO", "CORTE", "CAÍDA", "ECO", "TIEMPO", "SUCIO"}, {"NOTA", "MATERIAL", "CAÍDA", "ARCO", "BRILLO", "SIMPATÍA", "MOVIMIENTO"}, SAMPLE_L};

typedef struct { const char *name; uint8_t p[8]; } preset_t;
/* presets de fábrica: 20 por motor, en motores/tools/presets.py (de ahí sale fg_presets.h con el NIVEL ya igualado) */
#include "fg_presets.h"
#define NP_(a) (sizeof(a) / sizeof(a[0]))
/* (los de SAMPLE en el bombo y la caja son los de MUESTRA, que se convierten al leerlos: preset_p) */
static const preset_t *const PR_D[4][3] = {{PR_BARRIDO, PR_MEMB, PR_KMUESTRA}, {PR_CAJA, PR_APLAUSO, PR_SMUESTRA}, {PR_GRANIZO, PR_PLACA, 0}, {PR_GOTA, PR_CHATARRA, 0}};
static const uint8_t PRN_D[4][3] = {{NP_(PR_BARRIDO), NP_(PR_MEMB), NP_(PR_KMUESTRA)}, {NP_(PR_CAJA), NP_(PR_APLAUSO), NP_(PR_SMUESTRA)}, {NP_(PR_GRANIZO), NP_(PR_PLACA), 0},
                                    {NP_(PR_GOTA), NP_(PR_CHATARRA), 0}};
static const preset_t *const PR_Y[FGY_MODELS] = {PR_SH, PR_ACIDO, PR_FM, PR_CUERDAS, PR_TEXTURA, PR_DRON, PR_DUB, PR_RESON, 0};
static const uint8_t PRN_Y[FGY_MODELS] = {NP_(PR_SH), NP_(PR_ACIDO), NP_(PR_FM), NP_(PR_CUERDAS), NP_(PR_TEXTURA), NP_(PR_DRON), NP_(PR_DUB), NP_(PR_RESON), 0};
/* el banco de sonidos del usuario: los mismos en todos los proyectos (en el FM-1 va a .bss, fuera del pool) */
static fbx_upre_t UB[FBX_UBANK];
static uint8_t UBN, ub_dirty;
#define UB_VER 3u                                        /* 3 (0.9): los sonidos de MUESTRA pasan a SAMPLE */
static int remap_upre_v4(fbx_upre_t *u);

static const char *const MIXL[4] = {"VOLUMEN", "PANEO", "DELAY", "REVERB"};
static const char *const FXL[8] = {"DELAY TIEMPO", "DELAY REPITE", "DELAY TONO", "REVERB TAMAÑO", "REVERB TONO", "FILTRO", "DRIVE", "COMPRESIÓN"};
static const uint8_t FXDEF[8] = {4, 50, 70, 70, 60, 64, 0, 30};
static const uint8_t FXPAGE[2][4] = {{0, 1, 3, 5}, {2, 4, 6, 7}};
static const char *const CONDS[12] = {"SIEMPRE", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "FILL", "SIN FILL"};
static const char *const SPEEDS[3] = {"½", "1", "2"};
static const char *const SHAPES[5] = {"SENO", "TRIÁNGULO", "SIERRA", "CUADRADA", "AZAR"};
static const char *const SCALEN[8] = {"CROMÁTICA", "MENOR", "MAYOR", "DÓRICA", "FRIGIA", "PENTA MENOR", "PENTA MAYOR", "ÁRABE"};
static const uint8_t SCALE_N[8] = {12, 7, 7, 7, 7, 5, 5, 7};
static const uint8_t SCALE_I[8][12] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10},
                                       {0, 1, 3, 5, 7, 8, 10}, {0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}, {0, 1, 4, 5, 7, 8, 11}};
/* FX de performance, una por blanca: nombre, bits de la mezcla, bits del secuenciador */
static const struct { const char *name; uint16_t mix; uint8_t seq; } PERF[16] = {
    {"REPITE 1/4", 0, FG_SEQ_REP4}, {"REPITE 1/8", 0, FG_SEQ_REP8}, {"STUTTER 16", FGP_STUT16, 0}, {"STUTTER 32", FGP_STUT32, 0},
    {"REVERSA", FGP_REV, 0}, {"MEDIO TIEMPO", FGP_HALF, 0}, {"FRENO", FGP_BRAKE, 0}, {"GATE", FGP_GATE, 0},
    {"FILTRO GRAVE", FGP_LP, 0}, {"FILTRO AGUDO", FGP_HP, 0}, {"BITS", FGP_BITS, 0}, {"ECO", FGP_THROW, 0},
    {"CONGELA", FGP_FREEZE, 0}, {"SOLO PERC", 0, FG_SEQ_SOLOPERC}, {"SIN BOMBO", 0, FG_SEQ_NOKICK}, {"SUBIDA", FGP_RISER, 0}};
static const char *const NOTES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const FMR[9] = {"×.5", "×1", "×1.41", "×2", "×2.76", "×3", "×3.5", "×5.19", "×7.13"};
static const char *const SHF[4] = {"PLUCK", "BAJO", "LEAD", "PAD"};

/* teclas: F3..G5, el patrón blanca/negra desde F */
static const int8_t KEYW[FBX_KEYS] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
static const int8_t KEYB[FBX_KEYS] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, 4, -1, -1, 5, -1, 6, -1, 7, -1, -1, 8, -1, 9, -1, -1, 10, -1};

/* ------------------------------------------------------------ utilidades --- */

static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static void scpy(char *d, const char *s, unsigned cap) { unsigned i = 0; while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; } d[i] = 0; }
static void scat(char *d, const char *s, unsigned cap) { unsigned n = 0; while (d[n]) n++; scpy(d + n, s, cap > n ? cap - n : 0); }
static void sint(char *d, int v, unsigned cap)
{
    char t[12]; int n = 0; unsigned u = v < 0 ? (unsigned)-v : (unsigned)v;
    do t[n++] = (char)('0' + u % 10u); while ((u /= 10u) && n < 11);
    unsigned k = 0;
    if (v < 0 && k + 1 < cap) d[k++] = '-';
    while (n && k + 1 < cap) d[k++] = t[--n];
    d[k] = 0;
}
static void cati(char *d, int v, unsigned cap) { char t[12]; sint(t, v, sizeof t); scat(d, t, cap); }
static void catsi(char *d, int v, unsigned cap) { if (v > 0) scat(d, "+", cap); cati(d, v, cap); }   /* con signo */
/* un texto que la pantalla no traduce (nombres de sonidos y de samples): uno a la vez */
static const char *raw(const char *s) { static char o[24]; o[0] = 1; scpy(o + 1, s, sizeof o - 1u); return o; }
static uint8_t dev_dirty;                                 /* el idioma cambió: se escribe parado (dev_flush) */
static int is_synth(unsigned t) { return t >= 4; }
static fg_track_t *TRK(fg_box_t *b) { return &b->Q.pat[b->s.cur].tr[b->sel]; }
static uint8_t *P(fg_box_t *b, unsigned t) { return b->s.p[t][b->s.model[t]]; }
static int held(const fg_box_t *b, unsigned id) { return (b->down >> id) & 1u; }
static int held_b(const fg_box_t *b, unsigned i) { return (b->bdown >> i) & 1u; }
static const char *MN(const fg_box_t *b, unsigned t) { return is_synth(t) ? YNAME[b->s.model[t] % FGY_MODELS] : MNAME[t][b->s.model[t] % 3u]; }
static int is_samp(const fg_box_t *b, unsigned t) { return b->s.model[t] == (is_synth(t) ? FGY_SAMPLE : DSAMPLE); }
static const char *PL(const fg_box_t *b, unsigned t, unsigned i)
{
    if (i >= 7) return "AZAR";
    return is_synth(t) ? PLAB_Y[b->s.model[t] % FGY_MODELS][i] : PLAB_D[t][b->s.model[t] % 3u][i];
}
static void note_name(char *d, int midi, unsigned cap) { scpy(d, NOTES[midi % 12], cap); cati(d, midi / 12 - 1, cap); }
static int midi_of(int v) { return 24 + v * 61 / 128; }
static int semi_of(int v) { return v * 61 / 128; }

/* ------------------------------------------------------ cola de mensajes --- */

enum { M_PARAM = 1, M_MODEL, M_MIX, M_FX, M_PERF, M_PLAY, M_BPM, M_SWING, M_MUTE, M_FILL, M_QUEUE, M_CHAIN, M_AZAR,
       M_LFO, M_TRANSP, M_MUTEQ, M_TRIG, M_CHAINSET, M_MIDI, M_NOTEOFF, M_SCL2, M_LFO2, M_ARPCFG, M_ARPKEY, M_PERFK, M_SAMPOFF };
static void post(fg_box_t *b, unsigned type, unsigned t, unsigned a, unsigned bb, int32_t v)
{
    uint16_t w = b->mq_w, n = (uint16_t)((w + 1u) % (sizeof b->mq / sizeof b->mq[0]));
    if (n == b->mq_r) return;                            /* llena: se pierde (no debería: el audio la vacía cada bloque) */
    fbx_msg_t *m = &b->mq[w];
    m->type = (uint8_t)type; m->t = (uint8_t)t; m->a = (uint8_t)a; m->b = (uint8_t)bb; m->v = v;
    __asm__ volatile("" ::: "memory");
    b->mq_w = n;
}

static void arp_key(fg_box_t *b, unsigned s, unsigned key, unsigned lk, int on);
typedef struct { uint8_t v, c, p, r; int8_t u; uint8_t lk[12], au[8]; } fg_step_v2_t;   /* el paso de v1 y v2 */
static void au_set(fg_step_t *st, unsigned i, unsigned v)
{
    if (st->au[0] == i || st->au[0] == FG_NONE) { st->au[0] = (uint8_t)i; st->au[1] = (uint8_t)v; }
    else { st->au[2] = (uint8_t)i; st->au[3] = (uint8_t)v; }
}
static void move_steps(fg_box_t *b, int d);

/* pausar el audio para lo que mueve muchos bytes (copiar un patrón, cargar un proyecto) */
static void pause_audio(fg_box_t *b)
{
    if (!b->ms) return;                                  /* (en la Mac el audio corre en el mismo hilo) */
    b->pause_req = 1;
    uint32_t t0 = b->ms();
    while (!b->pause_ack && b->ms() - t0 < 20u) { }
}
static void resume_audio(fg_box_t *b) { b->pause_req = 0; }

/* ------------------------------------------------------------- lado audio --- */

static void eng_param(fg_box_t *b, unsigned t, unsigned m, unsigned i, unsigned v)
{
    switch (t) {
    case 0: fg_kick_param(&b->K, m, i, v); break;
    case 1: fg_snare_param(&b->SN, m, i, v); break;
    case 2: fg_metal_param(&b->MT, m, i, v); break;
    case 3: fg_perc_param(&b->PC, m, i, v); break;
    default: fg_synth_param(&b->SY[t - 4], m, i, v);
    }
}
static void eng_model(fg_box_t *b, unsigned t, unsigned m)
{
    switch (t) {
    case 0: fg_kick_model(&b->K, m); break;
    case 1: fg_snare_model(&b->SN, m); break;
    case 2: fg_metal_model(&b->MT, m); break;
    case 3: fg_perc_model(&b->PC, m); break;
    default: fg_synth_model(&b->SY[t - 4], m);
    }
}

static void usr_off(fg_sampv_t *v) { if (v->on && v->s >= fg_usr && v->s < fg_usr + FGS_USR) v->on = 0; }
static void apply_msg(fg_box_t *b, const fbx_msg_t *m)
{
    fg_seq_t *q = &b->Q;
    switch (m->type) {
    case M_PARAM: eng_param(b, m->t, m->a, m->b, (unsigned)m->v); fg_seq_param(q, m->t, m->a, m->b, (unsigned)m->v); break;
    case M_MODEL: eng_model(b, m->t, m->a); fg_seq_model(q, m->t, m->a); break;
    case M_MIX: fg_mix_strip(&b->MX, m->t, m->a, (unsigned)m->v); if (m->a < 4) fg_seq_mix(q, m->t, m->a, (unsigned)m->v); break;
    case M_SCL2: fg_seq_track2(q, m->t, m->a, (unsigned)m->v); break;
    case M_LFO2: { uint8_t l[4] = {(uint8_t)m->v, (uint8_t)(m->v >> 8), (uint8_t)(m->v >> 16), (uint8_t)(m->v >> 24)}; fg_seq_lfo2(q, m->t, l); break; }
    case M_ARPCFG: if (m->t >= 4) { fbx_arp_t *a = &b->ar[m->t - 4]; a->cfg[0] = (uint8_t)m->v; a->cfg[1] = (uint8_t)(m->v >> 8); a->cfg[2] = (uint8_t)(m->v >> 16); a->cfg[3] = (uint8_t)(m->v >> 24); a->on = m->a; if (!a->on) a->held = 0; if (!a->on || (!a->cfg[3] && !a->held)) { a->n = 0; a->pos = 0; } } break;
    case M_ARPKEY: if (m->t >= 4) arp_key(b, m->t - 4, m->a, m->b, m->v != 0); break;
    case M_FX: fg_mix_fx(&b->MX, m->a, (unsigned)m->v); break;
    case M_PERF: fg_mix_perf(&b->MX, (uint32_t)m->v & 0xFFFFu); fg_seq_perf(q, (uint32_t)m->v >> 16); b->perf_seq = (uint32_t)m->v >> 16; break;
    case M_PERFK: fg_mix_perfk(&b->MX, m->t, m->a, (unsigned)m->v); break;
    case M_PLAY:
        fg_seq_play(q, m->v != 0);
        if (m->v) { b->clk_start = b->mclk_out; b->clk_acc = 0; } else { b->clk_stop = b->mclk_out; }
        b->a_playing = (uint8_t)(m->v != 0);
        if (!m->v) for (int t = 0; t < 8; t++) b->pos[t] = -1;
        break;
    case M_BPM: fg_seq_tempo(q, (unsigned)m->v); fg_mix_tempo(&b->MX, fg_seq_step_samples(q)); break;
    case M_SWING: fg_seq_swing(q, (unsigned)m->v); break;
    case M_MUTE: fg_seq_mutes(q, m->a, m->b, m->v); break;
    case M_FILL: fg_seq_fill(q, m->v != 0); break;
    case M_QUEUE: q->nchain = 0; fg_seq_queue(q, (unsigned)m->v); break;
    case M_CHAIN: fg_seq_chain(q, b->chain_buf, (unsigned)m->v); break;
    case M_AZAR: q->azar[m->t] = (uint8_t)m->v; break;
    case M_LFO: { uint8_t l[4] = {(uint8_t)m->v, (uint8_t)(m->v >> 8), (uint8_t)(m->v >> 16), (uint8_t)(m->v >> 24)}; fg_seq_lfo(q, m->t, l); break; }
    case M_TRANSP: q->transp = (int8_t)m->v; break;
    case M_MUTEQ: q->muteq = (uint8_t)(m->v != 0); break;
    case M_TRIG: {                                       /* v: tecla | 0x100 si se sostiene hasta M_NOTEOFF */
        uint8_t lk[8]; for (int i = 0; i < 8; i++) lk[i] = FG_NONE; lk[0] = m->b;
        int hold = (m->v & 0x100) && m->t >= 4;
        fg_seq_audition(q, m->t, m->a, lk, hold ? (unsigned)(m->v & 0xFF) : 255u, hold ? (int32_t)FGY_HOLD : 0);
        break; }
    case M_NOTEOFF: if (b->n_noff < 16) { b->noff[b->n_noff][0] = m->t; b->noff[b->n_noff][1] = m->a; b->n_noff++; } break;
    case M_CHAINSET: {                                   /* la cadena cambia sin volver al principio */
        unsigned n = (unsigned)m->v > FG_SEQ_CHAIN ? FG_SEQ_CHAIN : (unsigned)m->v;
        q->queued = -1;
        if (n == 0) { q->nchain = 0; break; }
        fg_copy(q->chain, b->chain_buf, n);
        if (!q->nchain || m->a) q->chainpos = (uint8_t)(n - 1u);   /* recién prendida: al terminar el patrón, la fila 1 */
        else if (q->chainpos >= n) q->chainpos = (uint8_t)(n - 1u);
        q->nchain = (uint8_t)(n > 1u ? n : 0u);
        if (n == 1u) fg_seq_queue(q, q->chain[0]);   /* una sola fila: ese patrón en loop */
        break; }
    case M_MIDI: b->mclk_out = m->a; b->mnotes_out = m->b; b->ck_in = (uint8_t)(m->v != 0); if (!b->ck_in) b->ck_on = 0; break;
    case M_SAMPOFF:                                       /* la app va a borrar o escribir TUYOS: lo que suene de ahí se calla */
        for (unsigned k = 0; k < 2u; k++) { usr_off(&b->K.sm[k]); usr_off(&b->SN.sm[k]); usr_off(&b->MT.sm[k]); usr_off(&b->PC.sm[k]); }
        for (unsigned s = 0; s < 4u; s++) for (unsigned k = 0; k < FGY_VOICES; k++) if (b->SY[s].v[k].model == FGY_SAMPLE) { usr_off(&b->SY[s].v[k].smp); b->SY[s].v[k].on = b->SY[s].v[k].smp.on; }
        break;
    }
}

static void apply_ev(fg_box_t *b, const fg_seq_ev_t *e)
{
    unsigned t = e->t;
    switch (e->kind) {
    case FG_EV_TRIG:
        switch (t) {
        case 0: fg_kick_trig(&b->K, e->vel, e->lk, e->mask); break;
        case 1: fg_snare_trig(&b->SN, e->vel, e->lk, e->mask); break;
        case 2: fg_metal_trig(&b->MT, e->vel, e->lk, e->mask); break;
        case 3: fg_perc_trig(&b->PC, e->vel, e->lk, e->mask); break;
        default: fg_synth_trig(&b->SY[t - 4], e->vel, e->lk, e->mask, (uint32_t)e->len, (e->flags & FG_EVF_MANUAL) ? e->a : 255u);
        }
        break;
    case FG_EV_PRE: fg_snare_pre(&b->SN, (uint32_t)e->len); break;
    case FG_EV_PARAM: eng_param(b, t, e->a, e->b, e->v); break;
    case FG_EV_MIX: fg_mix_strip(&b->MX, t, e->b, e->v); break;
    case FG_EV_MUTE:
        for (unsigned k = 0; k < 8; k++) fg_mix_mute(&b->MX, k, (e->a >> k) & 1u, (e->b >> k) & 1u);
        b->a_mute = e->a; b->a_solo = e->b;
        break;
    default: break;
    }
}

enum { G_SYNTH, G_DRUM, G_MIX };
static unsigned group(const fg_seq_ev_t *e)
{
    if (e->kind == FG_EV_MIX || e->kind == FG_EV_MUTE) return G_MIX;
    if (e->kind == FG_EV_TRIG || e->kind == FG_EV_PARAM) return e->t >= 4 ? G_SYNTH : G_DRUM;
    if (e->kind == FG_EV_PRE) return G_DRUM;
    return 3;
}
static void r_drums(fg_box_t *b, unsigned o, unsigned k)
{
    fg_kick_render(&b->K, b->trk[0] + o, k);
    fg_snare_render(&b->SN, b->trk[1] + o, k);
    fg_metal_render(&b->MT, b->trk[2] + o, k);
    fg_perc_render(&b->PC, b->trk[3] + o, k);
}
static void r_synths(fg_box_t *b, unsigned o, unsigned k) { for (unsigned s = 0; s < 4; s++) fg_synth_render(&b->SY[s], b->trk[4 + s] + o, k); }
static void r_mix(fg_box_t *b, unsigned o, unsigned k)
{
    int32_t *in[FG_TRACKS];
    for (unsigned t = 0; t < FG_TRACKS; t++) in[t] = b->trk[t] + o;
    fg_mix_render(&b->MX, in, b->outl + o, b->outr + o, k);
    fg_mix_tape(&b->MX, b->outl + o, b->outr + o, k);
}
static void run_group(fg_box_t *b, unsigned g, unsigned n, void (*render)(fg_box_t *, unsigned, unsigned))
{
    unsigned o = 0;
    for (int k = 0; k < b->nev; k++) {
        if (group(&b->ev[k]) != g) continue;
        unsigned at = b->ev[k].at > n ? n : b->ev[k].at;
        if (at > o) { render(b, o, at - o); o = at; }
        apply_ev(b, &b->ev[k]);
    }
    if (n > o) render(b, o, n - o);
}
static fg_box_t *job_box;                                /* (el trabajo del otro núcleo solo recibe un entero) */
static void synth_job(uint32_t n)
{
    fg_box_t *b = job_box;
    uint32_t t0 = fg_now();
    for (unsigned t = 4; t < 8; t++) fg_zero(b->trk[t], n * sizeof(int32_t));
    run_group(b, G_SYNTH, n, r_synths);
    b->t_core1 = fg_now() - t0;
}

/* ---- arpegiador (audio): las teclas que se sostienen, en orden de altura o como se tocaron, por octavas ---- */
static void arp_key(fg_box_t *b, unsigned s, unsigned key, unsigned lk, int on)
{
    fbx_arp_t *a = &b->ar[s];
    unsigned i;
    if (on) {
        if (!a->held && a->cfg[3]) a->n = 0;             /* SOSTENER: un acorde nuevo reemplaza al que quedó */
        a->held++;
        for (i = 0; i < a->n && a->keys[i] != key; i++) { }
        if (i == a->n && a->n < 16) { a->keys[a->n] = (uint8_t)key; a->lks[a->n] = (uint8_t)lk; a->n++; }
    } else {
        if (a->held) a->held--;
        if (a->cfg[3]) return;
        for (i = 0; i < a->n && a->keys[i] != key; i++) { }
        if (i < a->n) { for (; i + 1 < a->n; i++) { a->keys[i] = a->keys[i + 1]; a->lks[i] = a->lks[i + 1]; } a->n--; }
    }
    if (!a->n) a->pos = 0;
}
static unsigned arp_next(fg_box_t *b, fbx_arp_t *a)
{
    uint8_t v[16];
    unsigned n = a->n, oct = a->cfg[2] + 1u, total = n * oct, i, k;
    fg_copy(v, a->lks, n);
    if (a->cfg[0] != 4)                                   /* por altura (menos COMO TOCAS) */
        for (i = 1; i < n; i++) { uint8_t x = v[i]; for (k = i; k > 0 && v[k - 1] > x; k--) v[k] = v[k - 1]; v[k] = x; }
    switch (a->cfg[0]) {
    case 1: i = total - 1u - a->pos % total; break;
    case 2: { unsigned L = total > 1u ? 2u * total - 2u : 1u, p = a->pos % L; i = p < total ? p : L - p; break; }
    case 3: b->rng = b->rng * 1664525u + 1013904223u; i = (b->rng >> 8) % total; break;
    default: i = a->pos % total;
    }
    a->pos++;
    int x = v[i % n] + 12 * (int)(i / n);
    return (unsigned)(x > 127 ? 127 : x);
}
/* REC tocando: la nota del arpegio queda en el paso de la pista donde cae (x: tick de 1/32 del reloj del
 * secuenciador). El último paso decidido empezó en tick+1 − (patTick−1) mod ticks-por-paso; de ahí se cuenta.
 * Una nota por paso: la primera; las de 1/32 que caen a la mitad no caben. */
static const uint8_t SPD_T[3] = {4, 2, 1};
static void arp_rec(fg_box_t *b, unsigned t, fbx_arp_t *a, int32_t x, unsigned note)
{
    fg_seq_t *q = &b->Q;
    fg_track_t *tr = &q->pat[q->cur].tr[t];
    int32_t m = SPD_T[tr->spd < 3 ? tr->spd : 1], len = tr->len ? tr->len : 1;
    if (q->pos[t] < 0 || !q->patTick) return;
    int32_t s0 = (int32_t)q->tick + 1 - (int32_t)((q->patTick - 1u) % (uint32_t)m), d = x - s0;
    int32_t at = s0 + (d >= 0 ? d / m : -((-d + m - 1) / m)) * m;   /* el tick donde empieza su paso */
    if (at + 1 == a->rx) return;
    a->rx = at + 1;
    fg_step_t *st = &tr->st[((q->pos[t] + (at - s0) / m) % len + len) % len];
    if (!st->v) st->v = 1;
    st->lk[0] = (uint8_t)note;
    b->arec = 1;
}
/* largo de cada nota del arpegio en ticks del secuenciador (1/32) en Q8: 1/32 1/16 1/8 1/4 1/16T 1/8T */
static const uint16_t ARP_P[6] = {256, 512, 1024, 2048, 341, 683};
static void run_arp(fg_box_t *b, unsigned n)
{
    fg_seq_t *q = &b->Q;
    uint32_t dt = (q->dt_q[0] + q->dt_q[1]) / 2u;
    if (!dt) return;
    int32_t p0 = b->apos, p1;
    if (q->playing) {                                     /* tocando: amarrado a los ticks del secuenciador */
        p1 = (int32_t)(q->tick * 256u) - (int32_t)(q->tickS - (q->clock + n)) * 256 / (int32_t)dt;   /* (32 bits: la distancia es de pocos ms) */
        if (!b->aplay || p1 - p0 > (int32_t)(4u * 256u) || p1 < p0) p0 = p1 - (int32_t)(n * 256u / dt);
    } else p1 = p0 + (int32_t)(n * 256u / dt);
    b->aplay = q->playing; b->apos = p1;
    if (p1 <= p0) return;
    for (unsigned s = 0; s < 4; s++) {
        fbx_arp_t *a = &b->ar[s];
        if (!a->on || !a->n) continue;
        int32_t P = ARP_P[a->cfg[1] < 6 ? a->cfg[1] : 1];
        for (int32_t k = p0 / P + 1; k * P <= p1; k++) {
            uint32_t off = (uint32_t)((k * P - p0) * (int32_t)n / (p1 - p0));
            uint8_t lk[8];
            for (int i = 0; i < 8; i++) lk[i] = FG_NONE;
            lk[0] = (uint8_t)arp_next(b, a);
            if (b->rec && q->playing) arp_rec(b, 4 + s, a, (k * P + 128) >> 8, lk[0]);
            fg_seq_note_at(q, 4 + s, 100, lk, (int32_t)((uint32_t)P * dt * 6u / 2560u), q->clock + off);
        }
    }
}

/* notas MIDI de salida: canal = pista, la nota del golpe (percusión: 36 38 42 39), se sueltan solas */
static const uint8_t DRUM_NOTE[4] = {36, 38, 42, 39};
static void mo_push(fg_box_t *b, uint32_t pkt)
{
    uint16_t w = b->mo_w, n = (uint16_t)((w + 1u) % (sizeof b->mo / sizeof b->mo[0]));
    if (n == b->mo_r) return;
    b->mo[w] = pkt;
    __asm__ volatile("" ::: "memory");
    b->mo_w = n;
}
static void note_off(fg_box_t *b, unsigned t)
{
    if (!b->off_note[t]) return;
    mo_push(b, 0x08u | (0x80u | t) << 8 | (uint32_t)b->off_note[t] << 16);
    b->off_note[t] = 0;
}
static void note_out(fg_box_t *b, const fg_seq_ev_t *e)
{
    unsigned t = e->t, note;
    uint8_t p[8];
    if (t < 4) note = DRUM_NOTE[t];
    else { fg_seq_params(&b->Q, t, e->mask, e->lk, p); note = (unsigned)(24 + p[0] * 61 / 128); }
    note_off(b, t);
    mo_push(b, 0x09u | (0x90u | t) << 8 | note << 16 | (uint32_t)(e->vel ? e->vel : 1u) << 24);
    b->off_note[t] = (uint8_t)note;
    b->off_left[t] = t < 4 ? 4410 : (e->len > 64 ? e->len : 64);
}
int fg_box_midi_out(fg_box_t *b, uint32_t *pkt)
{
    if (b->mo_r == b->mo_w) return 0;
    *pkt = b->mo[b->mo_r];
    b->mo_r = (uint16_t)((b->mo_r + 1u) % (sizeof b->mo / sizeof b->mo[0]));
    return 1;
}

/* reloj MIDI de entrada, en el audio con el tiempo de llegada de cada pulso. El tempo es el promedio de los
 * intervalos; la fase se compara en cada corchea (12 pulsos: ahí el swing no mueve el tick) contra el tick que el
 * secuenciador tiene para ese momento, y se corrige un cuarto del desfase (o todo, si pasa de 10 ms) moviendo el
 * siguiente tick. Lo ya agendado (2 ticks adelante) no se mueve: la corrección es de pocas muestras */
void fg_box_clock(fg_box_t *b, uint8_t c, uint32_t age)
{
    fg_seq_t *q = &b->Q;
    fbx_msg_t m;
    if (!b->ck_in) return;
    int32_t te = (int32_t)(q->clock - age);
    if (c == 0xFA || c == 0xFB) {                        /* arranca con el primer pulso: ese es el tick 0 */
        b->ck_on = 1; b->ck_n = 0;
        if (q->playing) { m.type = M_PLAY; m.v = 0; apply_msg(b, &m); }
        return;
    }
    if (c == 0xFC) {
        b->ck_on = 0;
        if (q->playing) { m.type = M_PLAY; m.v = 0; apply_msg(b, &m); b->a_ext_play = 2; }
        return;
    }
    if (c != 0xF8) return;
    if (b->ck_have) {
        int32_t d = te - b->ck_last;
        if (d > 80 && d < 4000) {                        /* (330 .. 27 BPM) */
            b->ck_avg = b->ck_avg ? b->ck_avg + ((int32_t)((uint32_t)d * 256u - b->ck_avg) >> 4) : (uint32_t)d * 256u;
            if (++b->ck_cnt >= 24u) {
                uint32_t bq = (uint32_t)(((uint64_t)q->sr * 163840u + b->ck_avg / 2u) / b->ck_avg), bpm = (bq + 128u) >> 8;   /* BPM en Q8 */
                int32_t dq = (int32_t)bq - (int32_t)q->bpm * 256;
                if (bpm >= 40u && bpm <= 250u && (dq > 154 || dq < -154)) {   /* (0.6 BPM: un 137.5 no brinca entre 137 y 138) */
                    fg_seq_tempo(q, bpm); fg_mix_tempo(&b->MX, fg_seq_step_samples(q));
                }
                b->a_bpm = q->bpm;
                b->ck_cnt = 24;
            }
        } else b->ck_cnt = 0;
    }
    b->ck_last = te; b->ck_have = 1;
    if (!b->ck_on) return;
    uint32_t n = b->ck_n++;
    if (n == 0 && !q->playing) { m.type = M_PLAY; m.v = 1; apply_msg(b, &m); b->a_ext_play = 1; return; }
    if (!q->playing || n % 12u) return;
    uint32_t k = n / 3u, base = q->tick & ~3u;           /* el tick de este pulso; el último múltiplo de 4 */
    int32_t tb = (int32_t)q->tickS;
    for (uint32_t j = q->tick; j > base; j--) tb -= (int32_t)q->dt_q[((j - 1u) >> 1) & 1u];
    int32_t ti = tb + ((int32_t)k - (int32_t)base) / 4 * (int32_t)(2u * (q->dt_q[0] + q->dt_q[1]));
    int32_t e = ti - te, lim = (int32_t)q->sr / 100;
    if (e > 2 * (int32_t)(q->dt_q[0] + q->dt_q[1]) || -e > 2 * (int32_t)(q->dt_q[0] + q->dt_q[1])) return;   /* más de una corchea: no es este */
    int32_t corr = e > lim || -e > lim ? e : e / 4;
    if (corr != e / 4) b->ck_snap++;
    q->tickS -= (uint32_t)corr;
    b->ck_err = e; b->ck_fix++;
}

/* lo que la interfaz ve de cada golpe: para los visuales */
static void push_hit(fg_box_t *b, const fg_seq_ev_t *e)
{
    uint16_t w = b->hq_w, n = (uint16_t)((w + 1u) % (sizeof b->hq / sizeof b->hq[0]));
    if (n == b->hq_r) return;
    fbx_hit_t *h = &b->hq[w];
    h->t = e->t; h->m = b->Q.model[e->t]; h->vel = e->vel;
    fg_seq_params(&b->Q, e->t, e->mask, e->lk, h->p);
    __asm__ volatile("" ::: "memory");
    b->hq_w = n;
}

void fg_box_audio(fg_box_t *b, int32_t *out, unsigned n)
{
    if (n > FG_BLOCK) n = FG_BLOCK;
    if (b->pause_req) {
        b->pause_ack = 1;
        for (unsigned i = 0; i < 2u * n; i++) out[i] = 0;
        return;
    }
    b->pause_ack = 0;
    uint32_t start = fg_now();
    while (b->mq_r != b->mq_w) {
        apply_msg(b, &b->mq[b->mq_r]);
        b->mq_r = (uint16_t)((b->mq_r + 1u) % (sizeof b->mq / sizeof b->mq[0]));
    }
    run_arp(b, n);
    {                                                    /* el eco de DUB, el LAVADO de TEXTURA y el tempo de los sintes */
        int owner = -1;
        for (unsigned s = 0; s < 4; s++) {
            fg_synth_t *y = &b->SY[s];
            if (owner < 0 && y->model == FGY_DUB) owner = (int)s;
            y->eco = (int)s == owner ? &b->eco : 0;
            y->step = fg_seq_step_samples(&b->Q);
            fg_mix_xrev(&b->MX, 4 + s, y->xrev);
        }
    }
    b->nev = fg_seq_process(&b->Q, n, b->ev, (int)(sizeof b->ev / sizeof b->ev[0]));
    for (int k = 0; k < b->nev; k++) {
        const fg_seq_ev_t *e = &b->ev[k];
        if (e->kind == FG_EV_STEP) b->pos[e->t] = e->s;
        else if (e->kind == FG_EV_PAT) { b->a_cur = e->a; b->a_queued = e->b; b->a_chainpos = e->v; }
        else if (e->kind == FG_EV_TRIG) {
            if (e->flags & (FG_EVF_FIRST | FG_EVF_MANUAL)) push_hit(b, e);
            if (b->mnotes_out) note_out(b, e);
        }
    }
    for (unsigned t = 0; t < 8; t++)                     /* las notas que ya duraron lo suyo */
        if (b->off_note[t] && (b->off_left[t] -= (int32_t)n) <= 0) note_off(b, t);
    job_box = b;
    int on1 = fg_core1_run(synth_job, n);
    for (unsigned t = 0; t < 4; t++) fg_zero(b->trk[t], n * sizeof(int32_t));
    for (unsigned i = 0; i < n; i++) { b->outl[i] = 0; b->outr[i] = 0; }
    run_group(b, G_DRUM, n, r_drums);
    uint32_t tw = fg_now();
    if (on1) { if (fg_core1_wait() < 0) synth_job(n); }
    else synth_job(n);
    b->t_wait = on1 ? fg_now() - tw : 0;
    run_group(b, G_MIX, n, r_mix);
    for (unsigned i = 0; i < n; i++) { out[2 * i] = fg_clamp16(b->outl[i]); out[2 * i + 1] = fg_clamp16(b->outr[i]); }
    for (unsigned k = 0; k < b->n_noff; k++) if (b->noff[k][0] >= 4) fg_synth_release(&b->SY[b->noff[k][0] - 4], b->noff[k][1]);
    b->n_noff = 0;
    if ((++b->blocks & 3u) == 0) for (int t = 0; t < 8; t++) b->level[t] = b->MX.st[t].peak;
    /* reloj MIDI: 24 pulsos por negra, parejos desde el arranque (el swing no se manda) */
    if (b->a_playing && b->mclk_out) {
        uint32_t per = ((b->Q.sr * 5u) << 7) / b->Q.bpm;     /* muestras por pulso (sr·60/24/bpm), Q8 */
        b->clk_acc += n << 8;
        while (b->clk_acc >= per) { b->clk_acc -= per; b->clk_pulses++; }
    }
    uint32_t tot = fg_now() - start;
    b->t_total = tot; b->t_core0 = tot - b->t_wait;
    if (tot > b->t_max) b->t_max = tot;
    if (b->t_core0 > b->t_core0_max) b->t_core0_max = b->t_core0;
    if (b->t_core1 > b->t_core1_max) b->t_core1_max = b->t_core1;
    b->t_avg += (int32_t)(tot - b->t_avg) >> 6;              /* promedio de ~64 bloques */
    if (b->blk_ticks) {                                      /* polifonía: el núcleo de los sintes manda cuántas voces caben */
        uint32_t c1 = tot * 100u / b->blk_ticks;              /* el bloque entero: los sintes, la percusión y la mezcla que los espera */
        if (!b->maxv) b->maxv = FGY_VOICES;
        if (c1 > 78u && b->maxv > 1u && b->blocks - b->v_blk > 4u) { b->maxv--; b->v_blk = b->blocks; }        /* ~12 ms */
        else if (c1 < 52u && b->maxv < FGY_VOICES && b->blocks - b->v_blk > 344u) { b->maxv++; b->v_blk = b->blocks; }   /* ~1 s */
        for (unsigned s = 0; s < 4; s++) { b->SY[s].maxv = b->maxv; fg_synth_limit(&b->SY[s], b->maxv); }
    }
    b->t_n++;
}

/* ------------------------------------------------------------ interfaz --- */

static void show_ov(fg_box_t *b, const char *label, const char *value, int bar)
{
    scpy(b->ov.label, label, sizeof b->ov.label); scpy(b->ov.value, value, sizeof b->ov.value);
    b->ov.bar = (int16_t)bar; b->ov.until = b->now + 1300u;
    b->names_until = 0; b->big_until = 0;
}
static void show_big(fg_box_t *b) { b->big_until = b->now + 1100u; b->ov.until = 0; b->names_until = 0; }
static void mark(fg_box_t *b) { b->dirty = 1; b->dirty_ms = b->now; b->u_dirty = 1; }

static void send_perf(fg_box_t *b)
{
    uint32_t mix = 0, seq = 0;
    for (int i = 0; i < 16; i++) if ((b->perf >> i) & 1u) { mix |= PERF[i].mix; seq |= PERF[i].seq; }
    post(b, M_PERF, 0, 0, 0, (int32_t)(mix | seq << 16));
}
static void send_lfo(fg_box_t *b, unsigned t)
{
    const uint8_t *l = b->s.lfo[t];
    post(b, M_LFO, t, 0, 0, (int32_t)(l[0] | l[1] << 8 | l[2] << 16 | (uint32_t)l[3] << 24));
}
static void send_lfo2(fg_box_t *b, unsigned t)
{
    const uint8_t *l = b->s.lfo2[t];
    post(b, M_LFO2, t, 0, 0, (int32_t)(l[0] | l[1] << 8 | l[2] << 16 | (uint32_t)l[3] << 24));
}
static void send_arp(fg_box_t *b, unsigned t)
{
    const uint8_t *a = b->s.arp[t];
    if (t >= 4) post(b, M_ARPCFG, t, (b->s.arp_on >> t) & 1u, 0, (int32_t)(a[0] | a[1] << 8 | a[2] << 16 | (uint32_t)a[3] << 24));
}
static void send_param(fg_box_t *b, unsigned t, unsigned m, unsigned i) { post(b, M_PARAM, t, m, i, b->s.p[t][m][i]); }
static void send_mutes(fg_box_t *b, int now) { post(b, M_MUTE, 0, b->has_pend ? b->pend : b->mute, b->solo, now); }
static void send_midi_cfg(fg_box_t *b) { post(b, M_MIDI, 0, b->s.reloj >> 1 & 1u, (b->s.mnotes & 2u) != 0, b->s.reloj & 1u); }
/* CADENA: las filas llenas de arriba a abajo (una vacía se salta), cada una sus veces; se despliega para el
 * secuenciador */
static unsigned chain_rows(const fg_box_t *b) { unsigned n = 0; for (unsigned r = 0; r < 16; r++) n += b->s.crow[r] < FBX_NP; return n; }
static unsigned chain_expand(fg_box_t *b)
{
    unsigned n = 0;
    if (b->s.chain_on)
        for (unsigned r = 0; r < 16; r++)
            if (b->s.crow[r] < FBX_NP)
                for (unsigned k = 0; k < (b->s.crep[r] ? b->s.crep[r] : 1u) && n < FG_SEQ_CHAIN; k++) b->chain_buf[n++] = b->s.crow[r];
    return n;
}
static void send_chain(fg_box_t *b, int restart) { unsigned n = chain_expand(b); post(b, M_CHAINSET, 0, restart ? 1u : 0u, 0, (int32_t)n); }
/* la fila que suena ahora (de la posición desplegada del secuenciador) */
int fg_box_chain_row(const fg_box_t *b)
{
    if (!b->s.chain_on || !b->playing) return -1;
    unsigned k = b->chainpos;
    for (unsigned r = 0; r < 16; r++) {
        if (b->s.crow[r] >= FBX_NP) continue;
        unsigned c = b->s.crep[r] ? b->s.crep[r] : 1u;
        if (k < c) return (int)r;
        k -= c;
    }
    return -1;
}
static void audition(fg_box_t *b, int lock0)
{
    if (!b->playing) post(b, M_TRIG, b->sel, 110, lock0 < 0 ? FG_NONE : (unsigned)lock0, 0);
}
/* todo el estado del proyecto otra vez al audio (al cargar): con el audio en pausa, directo */
static void push_all(fg_box_t *b)
{
    fbx_msg_t m;
    for (unsigned t = 0; t < 8; t++) {
        for (unsigned md = 0; md < NMODELS[t]; md++)
            for (unsigned i = 0; i < 8; i++) { m.type = M_PARAM; m.t = (uint8_t)t; m.a = (uint8_t)md; m.b = (uint8_t)i; m.v = b->s.p[t][md][i]; apply_msg(b, &m); }
        m.type = M_MODEL; m.t = (uint8_t)t; m.a = b->s.model[t]; apply_msg(b, &m);
        for (unsigned i = 0; i < 4; i++) { m.type = M_MIX; m.t = (uint8_t)t; m.a = (uint8_t)i; m.v = b->s.mix[t][i]; apply_msg(b, &m); }
        b->Q.azar[t] = b->s.azar[t];
        fg_seq_lfo(&b->Q, t, b->s.lfo[t]);
        fg_seq_lfo2(&b->Q, t, b->s.lfo2[t]);
        for (unsigned i = 0; i < 4; i++) fg_mix_strip(&b->MX, t, 4 + i, b->s.mix2[t][i]);
        for (unsigned i = 1; i < 4; i++) fg_seq_track2(&b->Q, t, i, b->s.scl2[t][i]);
        if (t >= 4) { fbx_arp_t *a = &b->ar[t - 4]; fg_copy(a->cfg, b->s.arp[t], 4); a->on = (b->s.arp_on >> t) & 1u; a->n = 0; }
    }
    for (unsigned i = 0; i < 8; i++) fg_mix_fx(&b->MX, i, b->s.fx[i]);
    fg_seq_tempo(&b->Q, b->s.bpm); fg_mix_tempo(&b->MX, fg_seq_step_samples(&b->Q));
    fg_seq_swing(&b->Q, b->s.swing);
    b->Q.transp = b->s.transp; b->Q.muteq = b->s.muteq;
    b->Q.cur = b->s.cur;
    b->mclk_out = b->s.reloj >> 1 & 1u; b->mnotes_out = (b->s.mnotes & 2u) != 0; b->ck_in = b->s.reloj & 1u;
}

/* el patrón por defecto de la caja (seed() de caja/index.html) */
static void seed_pattern(fg_pattern_t *p)
{
    static const uint8_t S[8][16] = {{2,0,0,0,1,0,0,1,0,0,1,0,1,0,0,0}, {0,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0},
                                     {1,0,2,0,1,0,2,0,1,0,2,0,1,0,2,1}, {0,0,1,0,0,0,0,1,0,0,1,0,0,0,0,0},
                                     {2,0,1,1,0,1,2,0,1,0,1,1,0,1,2,0}, {0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,0},
                                     {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0}, {1,0,0,1,0,0,1,0,1,0,1,0,0,1,0,0}};
    static const int8_t N4[16] = {9,0,21,9,0,16,9,0,24,0,21,12,0,9,28,0};
    static const uint8_t N7[6][2] = {{0, 0}, {3, 5}, {6, 7}, {8, 3}, {10, 10}, {13, 12}};
    for (unsigned t = 0; t < FG_SEQ_T; t++) {
        fg_seq_clear_track(&p->tr[t]);
        for (unsigned s = 0; s < 16; s++) p->tr[t].st[s].v = S[t][s];
    }
    p->tr[3].st[2].lk[0] = 70; p->tr[3].st[7].lk[0] = 48; p->tr[3].st[10].lk[0] = 86; p->tr[3].st[10].lk[4] = 50;
    for (unsigned s = 0; s < 16; s++) if (S[4][s]) p->tr[4].st[s].lk[0] = (uint8_t)(64 + N4[s] - 9);
    p->tr[5].st[6].lk[0] = 69; p->tr[5].st[14].lk[0] = 72; p->tr[5].st[14].lk[2] = 80;
    p->tr[6].st[0].lk[0] = 66; p->tr[6].st[8].lk[0] = 62;
    for (unsigned k = 0; k < 6; k++) p->tr[7].st[N7[k][0]].lk[0] = (uint8_t)(64 + N7[k][1] - 5);   /* unas notas (antes eran rebanadas) */
    p->tr[1].st[12].lk[11] = 45;
    p->tr[2].st[15].c = 1;
    p->tr[0].st[14].c = FG_COND_FILL;
}

/* INICIAL: los valores de cada motor al encender (los motores se mueven después, así que se copian al iniciar) */
static uint8_t DEF_P[8][FBX_NM][8];

static uint8_t engine_p(fg_box_t *b, unsigned t, unsigned m, unsigned i)
{
    switch (t) {
    case 0: return b->K.p[m][i];
    case 1: return b->SN.p[m][i];
    case 2: return b->MT.p[m][i];
    case 3: return b->PC.p[m][i];
    default: return b->SY[t - 4].p[m][i];
    }
}

static const uint8_t MIX0[8][4] = {{105, 64, 0, 0}, {95, 64, 0, 20}, {80, 82, 0, 0}, {85, 44, 40, 0},
                                   {95, 64, 0, 0}, {80, 40, 50, 30}, {82, 64, 10, 60}, {85, 90, 0, 10}};
static void song_defaults(fg_box_t *b)
{
    static const uint8_t M0[8] = {0, 0, 0, 0, FGY_ACIDO, FGY_SH, FGY_FM, FGY_CUERDAS};
    fbx_song_t *s = &b->s;
    fg_zero(s, sizeof *s);
    for (unsigned t = 0; t < 8; t++) {
        s->model[t] = M0[t];
        for (unsigned m = 0; m < NMODELS[t]; m++) for (unsigned i = 0; i < 8; i++) s->p[t][m][i] = engine_p(b, t, m, i);
        for (unsigned i = 0; i < 4; i++) s->mix[t][i] = MIX0[t][i];
        s->lfo[t][1] = 40;
    }
    for (unsigned i = 0; i < 8; i++) s->fx[i] = FXDEF[i];
    s->bpm = 124; s->scale = 1;
    for (unsigned r = 0; r < 16; r++) { s->crow[r] = 255; s->crep[r] = 1; }
    s->reloj = 3;
    for (unsigned t = 0; t < 8; t++) {
        s->mix2[t][0] = 64; s->mix2[t][3] = t ? 64 : 0;  /* FILTRO abierto; SIDECHAIN: lo de siempre (el bombo no se baja solo) */
        s->scl2[t][1] = 64; s->scl2[t][3] = 127;          /* sin transponer; DENSIDAD: todos los pasos */
        s->lfo2[t][1] = 40;
        s->arp[t][1] = 1;                                  /* 1/16 */
    }
    for (unsigned p = 0; p < FBX_NP; p++) for (unsigned t = 0; t < 8; t++) fg_seq_clear_track(&b->Q.pat[p].tr[t]);
    seed_pattern(&b->Q.pat[0]);
}

void fg_box_init(fg_box_t *b, void *vis_mem, unsigned vis_w, unsigned vis_h, unsigned vis_hist, uint32_t seed)
{
    int (*sw)(unsigned, const void *, uint32_t) = b->store_write;
    int (*sr)(unsigned, void *, uint32_t, uint32_t *) = b->store_read;
    int (*sh)(unsigned) = b->store_has;
    uint32_t (*ms)(void) = b->ms;
    int (*si)(unsigned, uint32_t *, uint32_t *) = b->store_info;
    int (*so)(const uint8_t *, unsigned) = b->sysex_out;
    const fg_usr_io_t *ui = b->usr_io;
    const char *ver = b->version;
    fg_zero(b, sizeof *b);
    b->store_write = sw; b->store_read = sr; b->store_has = sh; b->ms = ms;
    b->store_info = si; b->sysex_out = so; b->usr_io = ui; b->version = ver;
    px_init();
    fg_kick_init(&b->K); fg_snare_init(&b->SN); fg_metal_init(&b->MT); fg_perc_init(&b->PC);
    for (unsigned k = 0; k < 4; k++) fg_synth_init(&b->SY[k], 0, 0);
    fg_mix_init(&b->MX);
    fg_seq_init(&b->Q, FG_SR, seed);
    song_defaults(b);
    fg_copy(DEF_P, b->s.p, sizeof DEF_P);
    push_all(b);
    for (int t = 0; t < 8; t++) b->pos[t] = -1;
    b->a_queued = 255;
    b->page = PG_EDIT; b->seqmode = 1; b->queued = 255;
    b->copy_kind = 0;
    fg_vis_init(&b->vis, vis_mem, vis_w, vis_h, vis_hist, seed ^ 0x5f3759dfu);
    b->rng = seed * 2654435761u + 1u;
    b->perf_last = 255;
    for (unsigned i = 0; i < 16; i++) b->pk[i][0] = b->pk[i][1] = 64;
}

/* ---- qué hace cada perilla ahora (target() de la caja) ---- */

enum { TK_NONE, TK_ARP, TK_PARAM, TK_AZAR, TK_PLOCK, TK_MIX, TK_MLOCK, TK_FX, TK_SCL, TK_LFO, TK_GLO, TK_GLO2, TK_CHAIN, TK_SCL2, TK_LFO2,
       TK_ARPC, TK_PERFK };
typedef struct { uint8_t kind, idx; int16_t min, max; uint16_t step8; const char *label; } tgt_t;

static unsigned held_steps(fg_box_t *b, uint8_t *out)
{
    const fg_track_t *tr = TRK(b);
    unsigned n = 0;
    for (unsigned k = 0; k < b->nhw; k++) { unsigned s = b->sp * 16u + b->hw[k]; if (s < tr->len) out[n++] = (uint8_t)s; }
    return n;
}
static int rel_param(const fg_box_t *b, unsigned t, unsigned i) { (void)b; return is_synth(t) && i == 0; }

static int tget(fg_box_t *b, const tgt_t *g);
static int target(fg_box_t *b, unsigned k, tgt_t *g)
{
    unsigned t = b->sel;
    uint8_t hs[16];
    unsigned nhs = 0;
    if (b->seqmode && !held(b, FB_FX) && !held(b, FB_HOME) && !held(b, FB_SAVE) && !held_b(b, BK_COPY)) nhs = held_steps(b, hs);
    g->step8 = 256; g->min = 0; g->max = 127;
    if (held(b, FB_ARP) && nhs) {
        static const char *const L[4] = {"CONDICIÓN", "PROBABILIDAD", "RATCHET", "MICRO"};
        static const int16_t MI[4] = {0, 0, 1, -8}, MA[4] = {11, 100, 4, 7};
        static const uint16_t ST[4] = {128, 256, 96, 128};
        g->kind = TK_ARP; g->idx = (uint8_t)k; g->min = MI[k]; g->max = MA[k]; g->step8 = ST[k]; g->label = L[k];
        return 1;
    }
    {                                                    /* un efecto de performance sonando: K1 y K2 son suyos */
        static const char *const L[16][2] = {{0, 0}, {0, 0}, {"STUTTER LARGO", 0}, {"STUTTER LARGO", 0}, {"REVERSA VELOCIDAD", 0},
            {"MEDIO TIEMPO VELOC.", 0}, {"FRENO TIEMPO", 0}, {"GATE VELOCIDAD", "GATE ANCHO"}, {"FILTRO CORTE", "FILTRO RESO"},
            {"FILTRO CORTE", "FILTRO RESO"}, {"BITS", "BITS TASA"}, {"ECO REPITE", "ECO TONO"}, {"CONGELA TAMAÑO", "CONGELA TONO"},
            {0, 0}, {0, 0}, {"SUBIDA VELOCIDAD", "SUBIDA RESO"}};
        unsigned e = b->perf_last;
        if (k < 2u && e < 16u && ((b->perf >> e) & 1u) && L[e][k]) {
            g->kind = TK_PERFK; g->idx = (uint8_t)k; g->label = L[e][k]; return 1;
        }
    }
    if (held(b, FB_ARP)) {                               /* ARP sostenido sin pasos: el arpegiador de la pista */
        static const char *const L[4] = {"ARP MODO", "ARP VELOCIDAD", "ARP OCTAVAS", "ARP SOSTENER"};
        static const int16_t MA[4] = {4, 5, 2, 1};
        static const uint16_t ST[4] = {96, 96, 96, 64};
        if (!is_synth(t)) return 0;
        g->kind = TK_ARPC; g->idx = (uint8_t)k; g->label = L[k]; g->max = MA[k]; g->step8 = ST[k]; return 1;
    }
    if (held(b, FB_HOME) || held(b, FB_SAVE)) return 0;
    switch (b->page) {
    case PG_EDIT: {
        unsigned i = b->pn * 4u + k;
        g->idx = (uint8_t)i; g->label = PL(b, t, i);
        if (i == 7) { g->kind = TK_AZAR; return 1; }
        g->kind = nhs ? TK_PLOCK : TK_PARAM;
        if (is_samp(b, t)) {
            if (i == 0 && !is_synth(t)) { g->min = 40; g->max = 88; }
            if (i == 6) g->step8 = 43u * 256u;                /* un clic, otro MODO */
            if (i == 1) { static char L[24]; static const char *const BK[4] = {"BOMBOS", "CAJAS", "TEXTURAS", "TUYOS"};
                          int v = tget(b, g); scpy(L, "SAMPLE · ", sizeof L); scat(L, BK[fg_samp_bank((unsigned)v)], sizeof L); g->label = L; }
        }
        return 1; }
    case PG_ENV:
        if (b->pn) { static const char *const L[4] = {"FILTRO", "ANCHO", "SATURA", "SIDECHAIN"}; g->idx = (uint8_t)(4 + k); g->label = L[k]; g->kind = TK_MIX; return 1; }
        g->idx = (uint8_t)k; g->label = MIXL[k]; g->kind = nhs ? TK_MLOCK : TK_MIX; return 1;
    case PG_FX: g->idx = FXPAGE[b->pn & 1u][k]; g->label = FXL[g->idx]; g->kind = TK_FX; return 1;
    case PG_SCL: {
        if (b->pn) {
            static const char *const L[4] = {"MOVER", "TRANSPONER", "HUMANO", "DENSIDAD"};
            static const int16_t MI[4] = {-64, 40, 0, 0}, MA[4] = {64, 88, 127, 127};
            static const uint16_t ST[4] = {256, 128, 256, 256};
            g->kind = TK_SCL2; g->idx = (uint8_t)k; g->label = L[k]; g->min = MI[k]; g->max = MA[k]; g->step8 = ST[k]; return 1;
        }
        static const char *const L[4] = {"LARGO", "VELOCIDAD", "GOLPES", "ROTAR"};
        static const int16_t MI[4] = {1, 0, 0, 0}, MA[4] = {32, 2, 32, 31};
        static const uint16_t ST[4] = {256, 64, 256, 256};
        g->kind = TK_SCL; g->idx = (uint8_t)k; g->label = L[k]; g->min = MI[k]; g->max = MA[k]; g->step8 = ST[k]; return 1; }
    case PG_LFO: {
        static const char *const L[4] = {"DESTINO", "VELOCIDAD", "PROFUNDIDAD", "FORMA"};
        static const int16_t MA[4] = {6, 127, 127, 4};
        static const uint16_t ST[4] = {96, 256, 256, 96};
        static const char *const L2[4] = {"DESTINO 2", "VELOCIDAD 2", "PROFUNDIDAD 2", "FORMA 2"};
        g->kind = b->pn ? TK_LFO2 : TK_LFO; g->idx = (uint8_t)k; g->label = b->pn ? L2[k] : L[k]; g->max = MA[k]; g->step8 = ST[k]; return 1; }
    case PG_GLO:
        if (b->pn == 0) {
            static const char *const L[4] = {"TEMPO", "SWING", "RAÍZ", "ESCALA"};
            static const int16_t MI[4] = {60, 0, -12, 0}, MA[4] = {200, 75, 12, 7};
            static const uint16_t ST[4] = {256, 256, 128, 96};
            g->kind = TK_GLO; g->idx = (uint8_t)k; g->label = L[k]; g->min = MI[k]; g->max = MA[k]; g->step8 = ST[k]; return 1;
        }
        {
            static const char *const L[4] = {"MUTES", "RELOJ", "IDIOMA", "NOTAS Y CC"};
            static const int16_t MA[4] = {1, 3, FG_LANGS - 1, 3};
            static const uint16_t ST[4] = {64, 96, 96, 96};
            g->kind = TK_GLO2; g->idx = (uint8_t)k; g->label = L[k]; g->max = MA[k]; g->step8 = ST[k]; return 1;
        }
    case PG_CHAIN: {
        static const char *const L[4] = {"PATRÓN", "VECES", 0, "CADENA"};
        static const int16_t MI[4] = {0, 1, 0, 0}, MA[4] = {FBX_NP, 16, 0, 1};
        static const uint16_t ST[4] = {128, 128, 0, 64};
        if (k == 2) return 0;
        g->kind = TK_CHAIN; g->idx = (uint8_t)k; g->label = L[k]; g->min = MI[k]; g->max = MA[k]; g->step8 = ST[k]; return 1; }
    }
    return 0;
}

static int tget(fg_box_t *b, const tgt_t *g)
{
    unsigned t = b->sel;
    fg_track_t *tr = TRK(b);
    uint8_t hs[16];
    unsigned nhs = held_steps(b, hs);
    switch (g->kind) {
    case TK_ARP: { const fg_step_t *st = &tr->st[hs[0]]; return g->idx == 0 ? st->c : g->idx == 1 ? st->p : g->idx == 2 ? st->r : st->u; }
    case TK_PARAM: return P(b, t)[g->idx];
    case TK_AZAR: return b->s.azar[t];
    case TK_PLOCK: { uint8_t v = nhs ? tr->st[hs[0]].lk[g->idx] : FG_NONE; return v != FG_NONE ? v : rel_param(b, t, g->idx) ? 64 : P(b, t)[g->idx]; }
    case TK_MIX: return g->idx < 4 ? b->s.mix[t][g->idx] : b->s.mix2[t][g->idx - 4];
    case TK_SCL2: return g->idx == 0 ? 0 : b->s.scl2[t][g->idx];
    case TK_LFO2: return b->s.lfo2[t][g->idx];
    case TK_ARPC: return b->s.arp[t][g->idx];
    case TK_PERFK: return b->perf_last < 16u ? b->pk[b->perf_last][g->idx] : 64;
    case TK_MLOCK: { uint8_t v = nhs ? tr->st[hs[0]].lk[8 + g->idx] : FG_NONE; return v != FG_NONE ? v : b->s.mix[t][g->idx]; }
    case TK_FX: return b->s.fx[g->idx];
    case TK_SCL: return g->idx == 0 ? tr->len : g->idx == 1 ? tr->spd : b->s.eu[b->s.cur][t][g->idx - 2];
    case TK_LFO: return b->s.lfo[t][g->idx];
    case TK_GLO: return g->idx == 0 ? b->s.bpm : g->idx == 1 ? b->s.swing : g->idx == 2 ? b->s.transp : b->s.scale;
    case TK_GLO2: return g->idx == 0 ? b->s.muteq : g->idx == 1 ? b->s.reloj : g->idx == 2 ? fg_lang : b->s.mnotes;
    case TK_CHAIN: { unsigned r = b->crow_cur; return g->idx == 0 ? (b->s.crow[r] == 255 ? 0 : b->s.crow[r] + 1) : g->idx == 1 ? b->s.crep[r] : b->s.chain_on; }
    }
    return 0;
}

static void euclid(fg_box_t *b, unsigned t)
{
    fg_track_t *tr = &b->Q.pat[b->s.cur].tr[t];
    unsigned h = b->s.eu[b->s.cur][t][0], r = b->s.eu[b->s.cur][t][1], n = tr->len;
    if (!h) return;
    for (unsigned i = 0; i < n; i++) {
        unsigned j = (i + n - r % n) % n;
        int on = (j * h) % n < h;
        tr->st[i].v = (uint8_t)(on ? (tr->st[i].v == 2 ? 2 : 1) : 0);
    }
}

static void tset(fg_box_t *b, const tgt_t *g, int v)
{
    unsigned t = b->sel, m = b->s.model[t];
    fg_track_t *tr = TRK(b);
    uint8_t hs[16];
    unsigned nhs = held_steps(b, hs);
    switch (g->kind) {
    case TK_ARP:
        for (unsigned k = 0; k < nhs; k++) {
            fg_step_t *st = &tr->st[hs[k]];
            if (g->idx == 0) st->c = (uint8_t)v; else if (g->idx == 1) st->p = (uint8_t)v; else if (g->idx == 2) st->r = (uint8_t)v; else st->u = (int8_t)v;
        }
        break;
    case TK_PARAM:
        P(b, t)[g->idx] = (uint8_t)v; send_param(b, t, m, g->idx);
        if (b->rec && b->playing && b->seqmode && b->pos[t] >= 0) au_set(&tr->st[b->pos[t]], g->idx, (unsigned)v);
        break;
    case TK_AZAR: b->s.azar[t] = (uint8_t)v; post(b, M_AZAR, t, 0, 0, v); break;
    case TK_PLOCK: for (unsigned k = 0; k < nhs; k++) tr->st[hs[k]].lk[g->idx] = (uint8_t)v; break;
    case TK_MIX:
        if (g->idx < 4) b->s.mix[t][g->idx] = (uint8_t)v; else b->s.mix2[t][g->idx - 4] = (uint8_t)v;
        post(b, M_MIX, t, g->idx, 0, v); break;
    case TK_SCL2:
        if (g->idx == 0) { move_steps(b, v); break; }
        b->s.scl2[t][g->idx] = (uint8_t)v; post(b, M_SCL2, t, g->idx, 0, v); break;
    case TK_LFO2: b->s.lfo2[t][g->idx] = (uint8_t)v; send_lfo2(b, t); break;
    case TK_ARPC: b->s.arp[t][g->idx] = (uint8_t)v; send_arp(b, t); break;
    case TK_PERFK: {
        unsigned e = b->perf_last, bit = 0;
        if (e >= 16u) break;
        b->pk[e][g->idx] = (uint8_t)v;
        while (bit < 12u && !((PERF[e].mix >> bit) & 1u)) bit++;
        if (bit < 12u) post(b, M_PERFK, bit, g->idx, 0, v);
        break; }
    case TK_MLOCK: for (unsigned k = 0; k < nhs; k++) tr->st[hs[k]].lk[8 + g->idx] = (uint8_t)v; break;
    case TK_FX: b->s.fx[g->idx] = (uint8_t)v; post(b, M_FX, 0, g->idx, 0, v); break;
    case TK_SCL: {
        uint8_t *eu = b->s.eu[b->s.cur][t];
        if (g->idx == 0) { tr->len = (uint8_t)v; if (eu[0] > v) eu[0] = (uint8_t)v; if (b->sp * 16 >= v) b->sp = 0; }
        else if (g->idx == 1) tr->spd = (uint8_t)v;
        else if (g->idx == 2) {
            int nv = v > tr->len ? tr->len : v;
            if (eu[0] == 0 && nv > 0) {                  /* el primer giro guarda lo que hiciste a mano */
                for (unsigned s = 0; s < 32; s++) b->s.hand[b->s.cur][t][s] = tr->st[s].v;
                b->s.has_hand[b->s.cur] |= (uint8_t)(1u << t);
            }
            eu[0] = (uint8_t)nv;
            if (nv == 0 && ((b->s.has_hand[b->s.cur] >> t) & 1u)) {
                for (unsigned s = 0; s < 32; s++) tr->st[s].v = b->s.hand[b->s.cur][t][s];
                b->s.has_hand[b->s.cur] &= (uint8_t)~(1u << t);
            } else euclid(b, t);
        } else { eu[1] = (uint8_t)(v % (tr->len ? tr->len : 1)); euclid(b, t); }
        break; }
    case TK_LFO: b->s.lfo[t][g->idx] = (uint8_t)v; send_lfo(b, t); break;
    case TK_GLO:
        if (g->idx == 0) { b->s.bpm = (uint8_t)v; post(b, M_BPM, 0, 0, 0, v); }
        else if (g->idx == 1) { b->s.swing = (uint8_t)v; post(b, M_SWING, 0, 0, 0, v); }
        else if (g->idx == 2) { b->s.transp = (int8_t)v; post(b, M_TRANSP, 0, 0, 0, v); }
        else b->s.scale = (uint8_t)v;
        break;
    case TK_GLO2:
        if (g->idx == 0) { b->s.muteq = (uint8_t)v; post(b, M_MUTEQ, 0, 0, 0, v); }
        else if (g->idx == 2) { fg_lang = (uint8_t)v; dev_dirty = 1; return; }   /* (del aparato, no del proyecto) */
        else { if (g->idx == 1) b->s.reloj = (uint8_t)v; else b->s.mnotes = (uint8_t)v; send_midi_cfg(b); }
        break;
    case TK_CHAIN: {
        unsigned r = b->crow_cur;
        int was = b->s.chain_on;
        if (g->idx == 0) {
            b->s.crow[r] = v ? (uint8_t)(v - 1) : 255;
            if (!b->s.crep[r]) b->s.crep[r] = 1;
            if (v && chain_rows(b) >= 2) b->s.chain_on = 1;   /* armar filas ya es armar la cadena */
        } else if (g->idx == 1) b->s.crep[r] = (uint8_t)v;
        else b->s.chain_on = (uint8_t)v;
        send_chain(b, b->s.chain_on && !was);
        break; }
    }
    mark(b);
}

static void fmt_param(fg_box_t *b, unsigned t, unsigned i, int v, int rel, char *o, unsigned cap)
{
    unsigned m = b->s.model[t];
    o[0] = 0;
    if (rel) { int d = v - 64; note_name(o, 24 + clampi(semi_of(P(b, t)[0]) + d, 0, 60), cap); scat(o, " (", cap); catsi(o, d, cap); scat(o, ")", cap); return; }
    if (is_synth(t)) {
        if (i == 0) { note_name(o, midi_of(v), cap); return; }
        if (m == FGY_FM && i == 1) { scpy(o, FMR[v * 9 / 128], cap); return; }
        if (m == FGY_SH && i == 1) { scpy(o, v <= 10 ? "SIERRA" : v < 36 ? "SIERRA·CUAD" : v <= 46 ? "CUADRADA" : v <= 84 ? "PULSO" : "PWM", cap); return; }
        if (m == FGY_SH && i == 2) { if (!v) scpy(o, "SIN SUB", cap); else { scpy(o, v > 95 ? "−2 OCT " : "−1 OCT ", cap); cati(o, v > 95 ? 127 : v * 127 / 95, cap); } return; }
        if (m == FGY_SH && i == 6) { scpy(o, SHF[v >> 5], cap); scat(o, " ", cap); cati(o, (v & 31) * 100 / 31, cap); return; }
    }
    if (is_samp(b, t)) {
        static const char *const MO[3] = {"GOLPE", "LAZO", "REVERSA"};
        const fg_sample_t *sm;
        switch (i) {
        case 0: catsi(o, clampi(v, 40, 88) - 64, cap); scat(o, " ST", cap); return;   /* (en los sintes, la nota: arriba) */
        case 1: sm = fg_samp_at((unsigned)v); scpy(o, sm ? raw(sm->name) : "NO HAY", cap); return;
        case 2: cati(o, v * 100 / 128, cap); scat(o, "%", cap); return;
        case 3: if (v >= 120) scpy(o, "ENTERA", cap); else cati(o, v, cap); return;
        case 4: if (v >= 63 && v <= 65) scpy(o, "ABIERTO", cap); else { scpy(o, v < 64 ? "GRAVES " : "AGUDOS ", cap); cati(o, v < 64 ? 64 - v : v - 64, cap); } return;
        case 5: if (v) cati(o, v, cap); else scpy(o, "NADA", cap); return;
        case 6: scpy(o, MO[v * 3 / 128], cap); return;
        }
    }
    if (t == 1 && m == 1 && i == 1) { cati(o, 1 + v * 5 / 127, cap); scat(o, " PALMAS", cap); return; }
    if (i == 1 && is_synth(t) && m == FGY_TEXTURA && FG_SMP_TEX_N) { scpy(o, raw(FG_SMP_TEX[(unsigned)v * FG_SMP_TEX_N / 128u].name), cap); return; }   /* MUESTRA: su nombre */
    if (t == 3 && m == 0 && i == 1) { scpy(o, v < 60 ? "CAE" : v > 68 ? "SUBE" : "QUIETA", cap); return; }
    cati(o, v, cap);
}
static void fmt_mix(unsigned i, int v, char *o, unsigned cap)
{
    o[0] = 0;
    if (i == 1) { if (v < 60) { scpy(o, "IZQ ", cap); cati(o, 64 - v, cap); } else if (v > 68) { scpy(o, "DER ", cap); cati(o, v - 64, cap); } else scpy(o, "CENTRO", cap); return; }
    cati(o, v, cap);
}
static void fmt_fx(unsigned i, int v, char *o, unsigned cap)
{
    static const char *const DT[8] = {"1/32", "1/16", "3/32", "1/8", "3/16", "1/4", "3/8", "1/2"};
    o[0] = 0;
    if (i == 0) { scpy(o, DT[v * 8 / 128], cap); return; }
    if (i == 5) { if (v < 61) { scpy(o, "GRAVE ", cap); cati(o, 64 - v, cap); } else if (v > 67) { scpy(o, "AGUDO ", cap); cati(o, v - 64, cap); } else scpy(o, "ABIERTO", cap); return; }
    cati(o, v, cap);
}
static void tfmt(fg_box_t *b, const tgt_t *g, int v, char *o, unsigned cap)
{
    unsigned t = b->sel;
    o[0] = 0;
    switch (g->kind) {
    case TK_ARP:
        if (g->idx == 0) scpy(o, CONDS[clampi(v, 0, 11)], cap);
        else if (g->idx == 1) { cati(o, v, cap); scat(o, "%", cap); }
        else if (g->idx == 2) { scpy(o, "×", cap); cati(o, v, cap); }
        else { catsi(o, v, cap); scat(o, "/16", cap); }
        return;
    case TK_PARAM: fmt_param(b, t, g->idx, v, 0, o, cap); return;
    case TK_PLOCK: fmt_param(b, t, g->idx, v, rel_param(b, t, g->idx), o, cap); return;
    case TK_AZAR: if (v) cati(o, v, cap); else scpy(o, "NADA", cap); return;
    case TK_MIX: case TK_MLOCK:
        if (g->idx == 4) { if (v == 64) scpy(o, "ABIERTO", cap); else { scpy(o, v < 64 ? "GRAVES " : "AGUDOS ", cap); cati(o, v < 64 ? 64 - v : v - 64, cap); } return; }
        if (g->idx > 4) { if (g->idx == 7 && t == 0) { scpy(o, "(MANDA)", cap); return; } if (v) cati(o, v, cap); else scpy(o, "NADA", cap); return; }
        fmt_mix(g->idx, v, o, cap); return;
    case TK_SCL2:
        if (g->idx == 0) scpy(o, "← GIRA →", cap);
        else if (g->idx == 1) { if (v == 64) scpy(o, "NADA", cap); else { catsi(o, v - 64, cap); scat(o, t >= 4 ? " ST" : "", cap); } }
        else if (g->idx == 2) { if (v) cati(o, v, cap); else scpy(o, "NADA", cap); }
        else { if (v >= 127) scpy(o, "TODOS", cap); else { cati(o, v * 100 / 127, cap); scat(o, "%", cap); } }
        return;
    case TK_PERFK: if (v == 64) scpy(o, "COMO SIEMPRE", cap); else { catsi(o, v - 64, cap); } return;
    case TK_ARPC: {
        static const char *const MO[5] = {"ARRIBA", "ABAJO", "SUBE Y BAJA", "AL AZAR", "COMO TOCAS"};
        static const char *const VE[6] = {"1/32", "1/16", "1/8", "1/4", "1/16 TRESILLO", "1/8 TRESILLO"};
        if (g->idx == 0) scpy(o, MO[clampi(v, 0, 4)], cap);
        else if (g->idx == 1) scpy(o, VE[clampi(v, 0, 5)], cap);
        else if (g->idx == 2) { cati(o, v + 1, cap); scat(o, v ? " OCTAVAS" : " OCTAVA", cap); }
        else scpy(o, v ? "SÍ" : "NO", cap);
        return; }
    case TK_FX: fmt_fx(g->idx, v, o, cap); return;
    case TK_SCL:
        if (g->idx == 0) { cati(o, v, cap); scat(o, " PASOS", cap); }
        else if (g->idx == 1) { scpy(o, SPEEDS[clampi(v, 0, 2)], cap); scat(o, "×", cap); }
        else if (g->idx == 2) { if (v) { cati(o, v, cap); scat(o, " DE ", cap); cati(o, TRK(b)->len, cap); } else scpy(o, "A MANO", cap); }
        else { scpy(o, "+", cap); cati(o, v, cap); }
        return;
    case TK_LFO: case TK_LFO2:
        if (g->idx == 0) scpy(o, PL(b, t, (unsigned)v), cap);
        else if (g->idx == 1) {                          /* 0.05·2^(v·9/127) Hz, con dos decimales */
            uint32_t hz100 = (uint32_t)(((uint64_t)5u * fg_exp2_q16((uint32_t)v * 9u * 65536u / 127u)) >> 16);
            cati(o, (int)(hz100 / 100u), cap); scat(o, ".", cap); if (hz100 % 100u < 10u) scat(o, "0", cap); cati(o, (int)(hz100 % 100u), cap); scat(o, " HZ", cap);
        } else if (g->idx == 3) scpy(o, SHAPES[clampi(v, 0, 4)], cap);
        else cati(o, v, cap);
        return;
    case TK_GLO:
        if (g->idx == 0) { cati(o, v, cap); scat(o, " BPM", cap); }
        else if (g->idx == 1) { cati(o, v, cap); scat(o, "%", cap); }
        else if (g->idx == 2) { catsi(o, v, cap); scat(o, " ST", cap); }
        else scpy(o, SCALEN[clampi(v, 0, 7)], cap);
        return;
    case TK_GLO2:
        if (g->idx == 0) scpy(o, v ? "AL COMPÁS" : "AL INSTANTE", cap);
        else if (g->idx == 1) { static const char *const R[4] = {"NO", "ENTRA", "SALE", "ENTRA Y SALE"}; scpy(o, R[clampi(v, 0, 3)], cap); }
        else if (g->idx == 2) scpy(o, FG_LANG_NAME[clampi(v, 0, FG_LANGS - 1)], cap);
        else { static const char *const N[4] = {"NO", "ENTRAN", "SALEN", "LAS DOS"}; scpy(o, N[clampi(v, 0, 3)], cap); }
        return;
    case TK_CHAIN:
        if (g->idx == 0) { if (v) { scpy(o, "PATRÓN ", cap); cati(o, v, cap); } else scpy(o, "– VACÍA", cap); }
        else if (g->idx == 1) { scpy(o, "×", cap); cati(o, v, cap); }
        else scpy(o, v ? "SÍ" : "NO", cap);
        return;
    }
}

static void clear_acc(fg_box_t *b) { for (int k = 0; k < 4; k++) b->acc_key[k] = 0; }

static void turn(fg_box_t *b, unsigned k, int d)
{
    tgt_t g;
    { unsigned a = b->kmov[k] + (unsigned)(d < 0 ? -d : d) * 48u; b->kmov[k] = (uint16_t)(a > 256u ? 256u : a); }   /* el visual siente el giro */
    if (!target(b, k, &g)) { show_ov(b, "LIBRE", "–", 0); return; }
    if ((g.kind == TK_PARAM || g.kind == TK_PLOCK) && g.idx == 1 && is_samp(b, b->sel)) {   /* SAMPLE: de uno en uno, sin huecos */
        unsigned v = (unsigned)tget(b, &g), w = v;
        for (int s = d < 0 ? -d : d; s > 0; s--) w = fg_samp_step(w, d > 0 ? 1 : -1);
        if (w != v) tset(b, &g, (int)w);
        char val[28];
        target(b, k, &g);                                 /* (el banco del nuevo) */
        tfmt(b, &g, (int)w, val, sizeof val);
        show_ov(b, g.label, val, (int)w * 2);
        return;
    }
    if (g.kind == TK_SCL2 && g.idx == 0) {               /* MOVER: un paso por clic, no una posición */
        move_steps(b, d > 0 ? 1 : -1);
        show_ov(b, "MOVER", d > 0 ? "→ UN PASO" : "← UN PASO", 128);
        return;
    }
    uint32_t key = 1u + b->page + b->pn * 8u + k * 64u + (held(b, FB_ARP) ? 512u : 0u) + g.kind * 1024u;
    for (unsigned i = 0; i < b->nhw; i++) key = key * 31u + b->hw[i] + 1u;
    int cur = tget(b, &g);
    if (key != b->acc_key[k]) { b->acc_key[k] = key; b->acc[k] = cur * 256; }
    b->acc[k] = clampi(b->acc[k] + d * (int)g.step8, g.min * 256, g.max * 256);
    int v = (b->acc[k] + 128) >> 8;
    if (v != cur) tset(b, &g, v);
    for (unsigned i = 0; i < b->nhw; i++) b->used_w |= (uint16_t)(1u << b->hw[i]);
    char lab[44], val[28];
    scpy(lab, g.label, sizeof lab);
    uint8_t hs[16];
    unsigned nhs = held_steps(b, hs);
    if ((g.kind == TK_PLOCK || g.kind == TK_MLOCK) && nhs) {
        scat(lab, " · LOCK", sizeof lab);
        for (unsigned i = 0; i < nhs && i < 3; i++) { scat(lab, " ", sizeof lab); cati(lab, hs[i] + 1, sizeof lab); }
    } else if (g.kind == TK_ARP && nhs) { scat(lab, " · PASO ", sizeof lab); cati(lab, hs[0] + 1, sizeof lab); }
    else if (g.kind == TK_CHAIN) { b->ov.until = 0; return; }   /* (la tabla de la CADENA ya lo dice) */
    int nv = tget(b, &g);
    tfmt(b, &g, nv, val, sizeof val);
    int span = g.max - g.min;
    show_ov(b, lab, val, span ? (nv - g.min) * 256 / span : 0);
}

/* ---- perillas de arriba ---- */
static void select_track(fg_box_t *b, unsigned t) { b->sel = (uint8_t)t; b->sp = 0; clear_acc(b); show_big(b); mark(b); }


/* MUESTRA (bombo y caja hasta la 0.8) -> SAMPLE: el mismo sample, la misma altura y el mismo filtro. PEGADA sostenía
 * el golpe (bombo hasta ~1.2 s, caja hasta ~1.2 s) y lo empujaba: el sostén pasa a una CAÍDA que tarda lo mismo en bajar
 * 20 dB, y el empujón y SATURA (más el NIVEL de la caja) al NIVEL, que satura igual.
 * MUESTRA: TONO MUESTRA CAÍDA FILTRO SATURA ARRANQUE PEGADA NIVEL */
static unsigned isqrt(unsigned x) { unsigned r = 0; while ((r + 1u) * (r + 1u) <= x) r++; return r; }
static uint32_t tau_of(unsigned i) { return (uint32_t)((1u << 30) / (uint32_t)((1 << 30) - FG_DECAY[i])); }   /* muestras */
static void muestra_to_sample(unsigned t, const uint8_t *o, uint8_t *p)
{
    unsigned nb = t ? FG_SMP_SNR_N : FG_SMP_KICK_N, k = nb ? o[1] * nb / 128u : 0u;
    const fg_sample_t *s = nb ? (t ? &FG_SMP_SNR[k] : &FG_SMP_KICK[k]) : 0;
    unsigned lo = t ? 50u : 30u, d = lo + o[2] * (100u - lo) / 127u;           /* el índice de FG_DECAY de antes */
    uint32_t hold = t ? o[6] * 400u : o[6] * 17u * 44u, peg = 4096u + o[6] * 4096u * 25u / 10u / 127u;
    uint32_t drive = (4096u + 290u * o[4]) * peg / 4096u;                       /* Q12 */
    if (t) drive = drive * (o[7] * o[7] * 2u) / 32768u;                        /* (la caja también tenía su NIVEL) */
    unsigned st = s && s->len ? o[5] * 10u * 128u / s->len : 0u, c = 127u;
    if (o[2] < 120u) {
        uint32_t tau = tau_of(d) + hold * 10u / 23u;
        for (unsigned i = 30; i <= 110u; i++) if (tau_of(i) >= tau) { c = ((i - 30u) * 127u + 79u) / 80u; break; }
        if (c > 119u) c = 127u;
    }
    p[0] = (uint8_t)(64 + ((int)o[0] - 64) * 12 / 64);
    p[1] = (uint8_t)((t ? FGSB_CAJAS : FGSB_BOMBOS) + k);
    p[2] = (uint8_t)(st > 127u ? 127u : st);
    p[3] = (uint8_t)c;
    p[4] = (uint8_t)(o[3] >= 127u ? 64u : o[3] * 62u / 126u);
    p[5] = 0; p[6] = FGSV_GOLPE;
    unsigned nv = isqrt(drive);
    p[7] = (uint8_t)(nv > 127u ? 127u : nv);
}
static void preset_p(unsigned t, unsigned m, const uint8_t *src, uint8_t *out)
{
    if (t < 2u && m == DSAMPLE) muestra_to_sample(t, src, out); else fg_copy(out, src, 8);
}
/* un lock o una automatización de MUESTRA: qué perilla de SAMPLE es (255: ninguna) y su valor con el resto del sonido.
 * Un lock de MUESTRA lockea las perillas de SAMPLE que salen de esa (PEGADA: CAÍDA y NIVEL) */
static unsigned muestra_idx(unsigned i) { static const uint8_t N[8] = {0, 1, 3, 4, 7, 2, 7, 7}; return i < 8u ? N[i] : i; }
static void muestra_locks(unsigned t, const uint8_t *base, uint8_t *lk)
{
    static const uint8_t DEP[8] = {1u << 0, 1u << 1, 1u << 5 | 1u << 1, 1u << 2 | 1u << 6, 1u << 3, 0, 0, 1u << 4 | 1u << 6 | 1u << 7};
    uint8_t o[8], p[8];
    unsigned has = 0;
    for (unsigned i = 0; i < 8; i++) { o[i] = lk[i] != FG_NONE ? lk[i] : base[i]; if (lk[i] != FG_NONE) has |= 1u << i; }
    if (!has) return;
    muestra_to_sample(t, o, p);
    for (unsigned i = 0; i < 8; i++) lk[i] = has & DEP[i] ? p[i] : FG_NONE;
}

static unsigned presets_of(fg_box_t *b, unsigned t, const preset_t **fac, unsigned *nfac)
{
    unsigned m = b->s.model[t];
    if (is_synth(t)) { *fac = PR_Y[m % FGY_MODELS]; *nfac = PRN_Y[m % FGY_MODELS]; } else { *fac = PR_D[t][m % 3u]; *nfac = PRN_D[t][m % 3u]; }
    unsigned nu = 0;
    for (unsigned i = 0; i < UBN; i++) if (UB[i].key == t && UB[i].m == m) nu++;
    return nu;
}
/* la lista de PRESETS: [EDITADO], INICIAL, de fábrica, guardados. Devuelve el nombre y los valores del i-ésimo */
static const char *preset_at(fg_box_t *b, unsigned t, unsigned i, uint8_t *out)
{
    unsigned m = b->s.model[t];
    const preset_t *fac; unsigned nfac;
    presets_of(b, t, &fac, &nfac);
    if (b->s.has_edited[t] & (1u << m)) { if (i == 0) { fg_copy(out, b->s.edited[t][m], 8); return "EDITADO"; } i--; }
    if (i == 0) { fg_copy(out, DEF_P[t][m], 8); return "INICIAL"; }
    i--;
    if (i < nfac) { preset_p(t, m, fac[i].p, out); return fac[i].name; }
    i -= nfac;
    for (unsigned k = 0; k < UBN; k++)
        if (UB[k].key == t && UB[k].m == m) { if (i == 0) { fg_copy(out, UB[k].p, 8); return UB[k].name; } i--; }
    return "";
}
static unsigned preset_count(fg_box_t *b, unsigned t)
{
    const preset_t *fac; unsigned nfac, nu = presets_of(b, t, &fac, &nfac);
    return ((b->s.has_edited[t] >> b->s.model[t]) & 1u) + 1u + nfac + nu;
}

static int same(const void *a, const void *c, unsigned n) { const uint8_t *x = a, *y = c; while (n--) if (*x++ != *y++) return 0; return 1; }
static int seq(const char *a, const char *c) { while (*a && *a == *c) { a++; c++; } return *a == *c; }
static uint8_t pack_buf[16384];                           /* (en el FM-1 va a .bss: el pool lo llenan la mezcla y el visual) */
/* el banco se escribe parado (borrar la flash detiene el código un momento: tocando, se oiría); si no, al parar */
static void ub_flush(fg_box_t *b)
{
    if (!ub_dirty || !b->store_write || b->playing || !px_take_buf(0)) return;
    pack_buf[0] = 0x80u | UB_VER;                         /* [versión][cuántos][sonidos] */
    pack_buf[1] = UBN;
    fg_copy(pack_buf + 2, UB, UBN * sizeof UB[0]);
    if (!b->store_write(FBX_UBANK_SLOT, pack_buf, 2u + UBN * sizeof UB[0])) ub_dirty = 0;
    else { show_ov(b, "SONIDOS", "NO SE GUARDARON", 0); b->retry_ms = b->now + 30000u; }
}
/* los ajustes del aparato (el idioma): aparte de los proyectos, se escriben parado */
static void dev_flush(fg_box_t *b)
{
    if (!dev_dirty || !b->store_write || b->playing) return;
    uint8_t d[2] = {1, fg_lang};                          /* [versión][idioma] */
    if (!b->store_write(FBX_DEV_SLOT, d, sizeof d)) dev_dirty = 0; else b->retry_ms = b->now + 30000u;
}
static void dev_load(fg_box_t *b)
{
    uint8_t d[8]; uint32_t n = 0;
    if (b->store_read && !b->store_read(FBX_DEV_SLOT, d, sizeof d, &n) && n >= 2u && d[0] == 1u && d[1] < FG_LANGS) fg_lang = d[1];
}
/* el banco desde un búfer (la flash o la app); 0 = bien */
static int ub_from(const uint8_t *q, uint32_t n)
{
    if (!n) return -1;
    unsigned v1 = !(q[0] & 0x80u), h = v1 ? 1u : 2u, k = q[h - 1u];   /* (v1, 0.7: [cuántos][sonidos]) */
    if (k > FBX_UBANK || h + k * sizeof UB[0] > n || (!v1 && (q[0] & 0x7Fu) > UB_VER)) return -1;
    fg_copy(UB, q + h, k * sizeof UB[0]);
    UBN = (uint8_t)k;
    for (unsigned i = 0; i < UBN; i++) UB[i].name[sizeof UB[0].name - 1u] = 0;
    if (v1) {                                             /* de 0.7: los sintes cambiaron de número */
        unsigned j = 0;
        for (unsigned i = 0; i < UBN; i++) if (remap_upre_v4(&UB[i])) UB[j++] = UB[i];
        UBN = (uint8_t)j; ub_dirty = 1;
    }
    if (v1 || (q[0] & 0x7Fu) < 3u) {                      /* hasta la 0.8: MUESTRA pasa a SAMPLE */
        for (unsigned i = 0; i < UBN; i++)
            if (UB[i].key < 2u && UB[i].m == DSAMPLE) { uint8_t o[8]; fg_copy(o, UB[i].p, 8); muestra_to_sample(UB[i].key, o, UB[i].p); }
        ub_dirty = 1;
    }
    return 0;
}
static void ub_load(fg_box_t *b)
{
    uint32_t n = 0;
    UBN = 0;
    if (!b->store_read || b->store_read(FBX_UBANK_SLOT, pack_buf, sizeof pack_buf, &n) || !n) return;
    ub_from(pack_buf, n);
}
/* los sonidos que traía un proyecto de antes pasan al banco (sin repetir) */
static void ub_merge(fg_box_t *b)
{
    for (unsigned i = 0; i < b->nupre; i++) {
        const fbx_upre_t *u = &b->upre[i];
        int dup = 0;
        for (unsigned k = 0; k < UBN; k++) if (UB[k].key == u->key && UB[k].m == u->m && same(UB[k].p, u->p, 8)) dup = 1;
        if (!dup && UBN < FBX_UBANK) { UB[UBN++] = *u; ub_dirty = 1; }
    }
    b->nupre = 0;
}
/* el sonido tuyo que está a la vista en la lista de PRESETS (su lugar en el banco), o -1 */
static int ub_shown(fg_box_t *b, unsigned t, unsigned m)
{
    const preset_t *fac; unsigned nfac;
    presets_of(b, t, &fac, &nfac);
    int i = (int)b->preset_idx[t][m] - (int)((b->s.has_edited[t] >> m) & 1u) - 1 - (int)nfac;
    if (i < 0) return -1;
    for (unsigned k = 0; k < UBN; k++) if (UB[k].key == t && UB[k].m == m && i-- == 0) return (int)k;
    return -1;
}

/* EDIT + SAVE (o SAVE + PRESET a la derecha): el sonido de la pista al banco, con el nombre del motor y un número */
static void save_sound(fg_box_t *b)
{
    unsigned t = b->sel, m = b->s.model[t];
    if (UBN >= FBX_UBANK) { show_ov(b, "SONIDOS", "YA NO CABEN", 256); return; }
    char nm[12];
    for (unsigned n = 1;; n++) {                     /* el primer número libre: MOTOR 1, MOTOR 2… */
        char num[6]; num[0] = 0; cati(num, (int)n, sizeof num);
        unsigned nd = 0; while (num[nd]) nd++;
        unsigned j = 0, room = sizeof UB[0].name - 2u - nd; const char *mn = MN(b, t) + 1;   /* (sin la marca "\1") */
        while (mn[j] && j < room) { nm[j] = mn[j]; j++; }
        while (j && (mn[j] & 0xC0) == 0x80) j--;     /* (sin partir una letra con acento) */
        while (j && nm[j - 1] == ' ') j--;
        nm[j] = 0; scat(nm, " ", sizeof nm); scat(nm, num, sizeof nm);
        int used = 0;
        for (unsigned k = 0; k < UBN; k++) if (UB[k].key == t && UB[k].m == m && seq(UB[k].name, nm)) used = 1;
        if (!used) break;
    }
    fbx_upre_t *u = &UB[UBN++];
    u->key = (uint8_t)t; u->m = (uint8_t)m;
    scpy(u->name, nm, sizeof u->name);
    fg_copy(u->p, P(b, t), 8);
    b->preset_idx[t][m] = (uint8_t)(preset_count(b, t) - 1u);   /* queda a la vista: el último de la lista */
    ub_dirty = 1; ub_flush(b);
    show_ov(b, "SONIDO GUARDADO", raw(nm), 256);
}

static void enc_presets(fg_box_t *b, int d)
{
    unsigned t = b->sel, m = b->s.model[t];
    if (held(b, FB_SAVE)) {                              /* SAVE + derecha: guardar el sonido; + izquierda: borrar el tuyo */
        b->used_btn |= 1u << FB_SAVE;
        if (b->saved_preset) return;
        b->saved_preset = 1;
        if (d < 0) {
            int k = ub_shown(b, t, m);
            if (k < 0) { show_ov(b, "BORRAR", "SOLO TUS SONIDOS", 256); return; }
            char nm[12]; scpy(nm, UB[k].name, sizeof nm);
            for (unsigned i = (unsigned)k; i + 1u < UBN; i++) UB[i] = UB[i + 1];
            UBN--; ub_dirty = 1; ub_flush(b);
            if (b->preset_idx[t][m]) b->preset_idx[t][m]--;
            show_ov(b, "SONIDO BORRADO", raw(nm), 256);
            return;
        }
        save_sound(b);
        return;
    }
    uint8_t pv[8];
    unsigned n = preset_count(b, t), cur = b->preset_idx[t][m];
    if (cur >= n) cur = n - 1u;
    preset_at(b, t, cur, pv);
    int differs = 0;
    for (unsigned j = 0; j < 8; j++) if (P(b, t)[j] != pv[j]) differs = 1;
    if (differs) {                                       /* tu versión queda primera en la lista */
        fg_copy(b->s.edited[t][m], P(b, t), 8);
        b->s.has_edited[t] |= (uint16_t)(1u << m);
        n = preset_count(b, t); cur = 0;
    }
    unsigned i = (unsigned)(((int)cur + d + (int)n * 4) % (int)n);
    b->preset_idx[t][m] = (uint8_t)i;
    const char *name = preset_at(b, t, i, pv);
    for (unsigned j = 0; j < 8; j++) { b->s.p[t][m][j] = pv[j]; send_param(b, t, m, j); }
    clear_acc(b);
    char lab[44];
    scpy(lab, TN[t], sizeof lab); scat(lab, " · ", sizeof lab); scat(lab, MN(b, t), sizeof lab);
    scat(lab, " · ", sizeof lab); cati(lab, (int)i + 1, sizeof lab); scat(lab, "/", sizeof lab); cati(lab, (int)n, sizeof lab);
    show_ov(b, lab, i == 0 || (i == 1 && (b->s.has_edited[t] >> m & 1u)) ? name : raw(name), n > 1 ? (int)(i * 256u / (n - 1u)) : 256);
    audition(b, -1); mark(b);
}

/* ---- al azar (SEL + EDIT o SEQ: un dado, sin historial; SAVE + OCT – deshace) y empezar de cero (mantener 1 s) ---- */
static unsigned rr(fg_box_t *b, unsigned n) { b->rng = b->rng * 1664525u + 1013904223u; return (b->rng >> 8) % n; }

/* un sonido: un preset de fábrica del motor, movido un poco; la NOTA de los sintes y el NIVEL se quedan */
static void rand_sound(fg_box_t *b)
{
    unsigned t = b->sel, m = b->s.model[t];
    uint8_t *p = P(b, t), o[8];
    const preset_t *fac; unsigned nfac;
    presets_of(b, t, &fac, &nfac);
    uint8_t base[8];
    if (nfac) preset_p(t, m, fac[rr(b, nfac)].p, base); else fg_copy(base, DEF_P[t][m], 8);
    for (unsigned j = 0; j < 8; j++) o[j] = (uint8_t)clampi(base[j] + (int)rr(b, 41) - 20, 0, 127);
    o[7] = p[7];
    if (is_synth(t)) o[0] = p[0];
    if (is_samp(b, t)) { o[1] = p[1]; o[6] = p[6]; }        /* SAMPLE: el mismo sample y el mismo MODO */
    for (unsigned j = 0; j < 8; j++) { b->s.p[t][m][j] = o[j]; send_param(b, t, m, j); }
    clear_acc(b);
    char lab[44]; scpy(lab, TN[t], sizeof lab); scat(lab, " · AL AZAR", sizeof lab);
    show_ov(b, lab, "SONIDO", -1);
    audition(b, -1); mark(b);
}

/* un ritmo según la pista: bombo al piso o quebrado, caja en 2 y 4 con fantasmas, metales a contratiempo con
 * dieciseisavos y ratchets, percusión euclidiana con alturas, sintes en la escala con acentos; a veces otro largo */
static void gen_rhythm(fg_box_t *b, fg_track_t *tr, unsigned t)
{
    unsigned len = tr->len, spd = tr->spd;
    if (t >= 3 && rr(b, 4) == 0) { static const uint8_t PL[4] = {12, 14, 15, 10}; len = PL[rr(b, 4)]; }
    fg_seq_clear_track(tr);
    tr->len = (uint8_t)len; tr->spd = (uint8_t)spd;
    fg_step_t *st = tr->st;
    switch (t) {
    case 0:
        if (rr(b, 10) < 7) { for (unsigned s = 0; s < len; s += 4) st[s].v = 1; }
        else { static const uint8_t C[7] = {3, 6, 7, 8, 10, 11, 14}; st[0].v = 1; for (unsigned k = 2 + rr(b, 2); k; k--) { unsigned s = C[rr(b, 7)]; if (s < len) st[s].v = 1; } if (len > 16) for (unsigned s = 16; s < len; s++) st[s].v = st[s - 16].v; }
        st[0].v = 2;
        if (len >= 4 && rr(b, 3) == 0) { unsigned s = 1 + 2 * rr(b, len / 2); if (s < len && !st[s].v) { st[s].v = 1; st[s].p = 60; } }
        break;
    case 1:
        for (unsigned s = 4; s < len; s += 8) st[s].v = 2;
        if (rr(b, 4) == 0 && len > 13) { st[12].v = 0; st[rr(b, 2) ? 13 : 11].v = 2; }
        for (unsigned s = 1; s < len; s += 2) if (rr(b, 6) == 0) { st[s].v = 1; st[s].p = (uint8_t)(50 + rr(b, 30)); }
        if (rr(b, 3) == 0) { st[len - 1].v = 1; st[len - 1].r = (uint8_t)(2 + rr(b, 2)); }
        break;
    case 2: {
        unsigned sty = rr(b, 3);
        for (unsigned s = 0; s < len; s++) {
            if (s % 4 == 2) st[s].v = 2;
            else if (sty == 1 && rr(b, 10) < 6) st[s].v = 1;
            else if (sty == 2 && s % 2 == 0) st[s].v = 1;
        }
        for (unsigned k = rr(b, 3); k; k--) { unsigned s = rr(b, len); st[s].v = st[s].v ? st[s].v : 1; st[s].r = (uint8_t)(2 + rr(b, 2)); }
        break; }
    case 3: {
        unsigned h = 3 + rr(b, 5), r = rr(b, len);
        for (unsigned i = 0; i < len; i++) {
            unsigned j = (i + len - r) % len;
            if ((j * h) % len < h) { st[i].v = rr(b, 4) ? 1 : 2; if (rr(b, 2)) st[i].lk[0] = (uint8_t)(30 + rr(b, 80)); }
        }
        break; }
    default: {
        unsigned sc = b->s.scale & 7u, n = SCALE_N[sc], dens = t == 4 ? 45 : 30;
        for (unsigned s = 0; s < len; s++) {
            if (s == 0 ? rr(b, 10) < 7 : rr(b, 100) >= dens) continue;
            unsigned deg = rr(b, 5) < 2 ? 0 : rr(b, n);
            int oct = rr(b, 4) == 0 ? 12 : rr(b, 6) == 0 ? -12 : 0;
            st[s].v = rr(b, 5) ? 1 : 2;
            st[s].lk[0] = (uint8_t)clampi(64 + SCALE_I[sc][deg] + oct, 0, 127);
            if (rr(b, 10) == 0) st[s].r = 2;
        }
        break; }
    }
}
static uint32_t trk_sum(const fg_track_t *tr)
{
    uint32_t h = 2166136261u;
    const uint8_t *q = (const uint8_t *)tr;
    for (unsigned i = 0; i < sizeof *tr; i++) h = (h ^ q[i]) * 16777619u;
    return h;
}
static void rand_rhythm(fg_box_t *b)
{
    unsigned t = b->sel, key = t | (unsigned)b->s.cur << 3;
    fg_track_t *tr = TRK(b), nt = *tr;
    if (b->rtrk_t != key || b->rtrk_sum != trk_sum(tr)) { b->rtrk_t = (uint8_t)key; b->rtrk_len = tr->len; }
    nt.len = b->rtrk_len;
    gen_rhythm(b, &nt, t);
    pause_audio(b);
    *tr = nt;
    resume_audio(b);
    b->rtrk_sum = trk_sum(tr);
    b->s.eu[b->s.cur][t][0] = 0; b->s.has_hand[b->s.cur] &= (uint8_t)~(1u << t);
    char lab[44]; scpy(lab, TN[t], sizeof lab); scat(lab, " · AL AZAR", sizeof lab);
    show_ov(b, lab, "PATRÓN", -1);
    mark(b);
}

/* EDIT 1 s: el sonido INICIAL del motor (lo que había queda como EDITADO en PRESETS) */
static void init_sound(fg_box_t *b)
{
    unsigned t = b->sel, m = b->s.model[t];
    if (!same(P(b, t), DEF_P[t][m], 8)) { fg_copy(b->s.edited[t][m], P(b, t), 8); b->s.has_edited[t] |= (uint16_t)(1u << m); }
    for (unsigned j = 0; j < 8; j++) { b->s.p[t][m][j] = DEF_P[t][m][j]; send_param(b, t, m, j); }
    b->preset_idx[t][m] = (uint8_t)((b->s.has_edited[t] >> m) & 1u);
    b->s.azar[t] = 0; post(b, M_AZAR, t, 0, 0, 0);
    b->pn = 0; clear_acc(b);
    char lab[44]; scpy(lab, TN[t], sizeof lab); scat(lab, " · ", sizeof lab); scat(lab, MN(b, t), sizeof lab);
    show_ov(b, lab, "SONIDO INICIAL", 256);
    audition(b, -1); mark(b);
}
/* SCL 1 s: la pista vacía (su largo y velocidad se quedan); HOME 1 s en la CADENA: el patrón entero vacío */
static void clear_track(fg_box_t *b)
{
    unsigned t = b->sel;
    fg_track_t *tr = TRK(b);
    uint8_t len = tr->len, spd = tr->spd;
    pause_audio(b);
    fg_seq_clear_track(tr);
    tr->len = len; tr->spd = spd;
    resume_audio(b);
    b->s.eu[b->s.cur][t][0] = 0; b->s.has_hand[b->s.cur] &= (uint8_t)~(1u << t);
    char lab[44]; scpy(lab, TN[t], sizeof lab); scat(lab, " · PATRÓN ", sizeof lab); cati(lab, b->s.cur + 1, sizeof lab);
    show_ov(b, lab, "PISTA VACÍA", 0); mark(b);
}
static void clear_pattern(fg_box_t *b)
{
    unsigned p = b->s.cur;
    pause_audio(b);
    for (unsigned t = 0; t < 8; t++) fg_seq_clear_track(&b->Q.pat[p].tr[t]);
    resume_audio(b);
    fg_zero(b->s.eu[p], sizeof b->s.eu[p]); b->s.has_hand[p] = 0;
    char lab[44]; scpy(lab, "PATRÓN ", sizeof lab); cati(lab, (int)p + 1, sizeof lab);
    show_ov(b, lab, "VACÍO", 0); mark(b);
}
/* ENV 1 s: la mezcla de la pista como al empezar (las dos páginas) */
static void reset_mix(fg_box_t *b)
{
    unsigned t = b->sel;
    static const uint8_t M2[4] = {64, 0, 0, 64};               /* FILTRO abierto, sin ANCHO ni SATURA, SIDECHAIN de siempre */
    for (unsigned i = 0; i < 4; i++) { b->s.mix[t][i] = MIX0[t][i]; post(b, M_MIX, t, i, 0, MIX0[t][i]); }
    for (unsigned i = 0; i < 4; i++) { b->s.mix2[t][i] = t == 0 && i == 3 ? 0 : M2[i]; post(b, M_MIX, t, 4 + i, 0, b->s.mix2[t][i]); }
    show_ov(b, TN[t], "MEZCLA INICIAL", 256); mark(b);
}
static void reset_fx(fg_box_t *b)
{
    for (unsigned i = 0; i < 8; i++) { b->s.fx[i] = FXDEF[i]; post(b, M_FX, 0, i, 0, FXDEF[i]); }
    show_ov(b, "EFECTOS", "COMO AL EMPEZAR", 256); mark(b);
}
static void reset_lfo(fg_box_t *b)
{
    unsigned t = b->sel;
    b->s.lfo[t][2] = 0; b->s.lfo2[t][2] = 0;
    send_lfo(b, t); send_lfo2(b, t);
    show_ov(b, TN[t], "SIN LFO", 0); mark(b);
}
static void reset_glo(fg_box_t *b)
{
    b->s.swing = 0; post(b, M_SWING, 0, 0, 0, 0);
    b->s.transp = 0; post(b, M_TRANSP, 0, 0, 0, 0);
    b->s.scale = 1;
    show_ov(b, "SWING, RAÍZ Y ESCALA", "COMO AL EMPEZAR", 256); mark(b);
}
static void arp_hold(fg_box_t *b)                           /* ARP 1 s: SOSTENER (al soltar las teclas el arpegio sigue); lo prende si estaba apagado */
{
    unsigned t = b->sel;
    if (!is_synth(t)) { show_ov(b, "ARPEGIADOR", "SOLO SINTES", 0); return; }
    b->s.arp[t][3] = (uint8_t)!b->s.arp[t][3];
    if (b->s.arp[t][3]) { b->s.arp_on |= (uint8_t)(1u << t); if (b->seqmode) { b->seqmode = 0; b->oct = 0; } }
    send_arp(b, t);
    show_ov(b, TN[t], b->s.arp[t][3] ? "ARP SOSTENIDO" : "ARP SUELTO", b->s.arp[t][3] ? 256 : 0); mark(b);
}
/* SCL pág. 2, MOVER: los pasos de la pista corridos uno (con todo lo que traen), dentro de su largo */
static void move_steps(fg_box_t *b, int d)
{
    fg_track_t *tr = TRK(b);
    unsigned L = tr->len ? tr->len : 1u;
    fg_step_t tmp;
    pause_audio(b);
    if (d > 0) { tmp = tr->st[L - 1u]; for (unsigned s = L - 1u; s > 0; s--) tr->st[s] = tr->st[s - 1u]; tr->st[0] = tmp; }
    else { tmp = tr->st[0]; for (unsigned s = 0; s + 1u < L; s++) tr->st[s] = tr->st[s + 1u]; tr->st[L - 1u] = tmp; }
    resume_audio(b);
    mark(b);
}

/* ---- deshacer / rehacer: el sonido de las pistas y el patrón que se estaba editando ---- */
#define PACK_MAGIC 0x31424746u                           /* "FGB1" */
#define PACK_VER 7u                                      /* 7 (0.9): SAMPLE y RELOJ (migrate_v6); 6: golpes sueltos en máscaras, GOLPES a mano solo si hay */
static uint32_t enc_track(const fg_track_t *tr, uint8_t *buf, uint32_t cap);
static int dec_track(fg_track_t *tr, const uint8_t *buf, uint32_t len, uint32_t *n, unsigned ver);
#define SNAP_SOUND(X) X(model) X(p) X(mix) X(mix2) X(fx) X(lfo) X(lfo2) X(azar) X(scl2) X(arp)
static uint32_t snap_take(fg_box_t *b, uint8_t *o)
{
    uint32_t n = 0, cap = FBX_UNDO_SZ;
    unsigned p = b->s.cur;
#define PUTF(f) do { if (n + sizeof b->s.f > cap) return 0; fg_copy(o + n, &b->s.f, sizeof b->s.f); n += sizeof b->s.f; } while (0);
    o[n++] = (uint8_t)p; o[n++] = b->s.arp_on;
    SNAP_SOUND(PUTF)
#undef PUTF
    if (n + 16u > cap) return 0;
    fg_copy(o + n, b->s.eu[p], 16); n += 16;
    for (unsigned t = 0; t < 8; t++) { uint32_t k = enc_track(&b->Q.pat[p].tr[t], o + n, cap - n); if (!k) return 0; n += k; }
    return n;
}
static void snap_restore(fg_box_t *b, const uint8_t *o, uint32_t len)
{
    uint32_t n = 2;
    unsigned p = o[0];
    if (p >= FBX_NP) return;
    pause_audio(b);
    b->s.arp_on = o[1];
#define GETF(f) do { fg_copy(&b->s.f, o + n, sizeof b->s.f); n += sizeof b->s.f; } while (0);
    SNAP_SOUND(GETF)
#undef GETF
    fg_copy(b->s.eu[p], o + n, 16); n += 16;
    for (unsigned t = 0; t < 8; t++) if (dec_track(&b->Q.pat[p].tr[t], o, len, &n, PACK_VER)) break;
    push_all(b);
    resume_audio(b);
    clear_acc(b);
    b->dirty = 1; b->dirty_ms = b->now;
}
static void undo_commit(fg_box_t *b)                        /* lo de ahora queda como un estado (lo que había adelante se va) */
{
    if (b->u_i + 1u < b->u_n) b->u_n = (uint8_t)(b->u_i + 1u);
    if (b->u_n == FBX_UNDO_N) {
        for (unsigned k = 1; k < FBX_UNDO_N; k++) { fg_copy(b->undo[k - 1], b->undo[k], b->u_len[k]); b->u_len[k - 1] = b->u_len[k]; }
        b->u_n--;
    }
    uint32_t n = snap_take(b, b->undo[b->u_n]);
    if (!n) return;                                        /* (no cupo: se queda el anterior) */
    b->u_len[b->u_n] = (uint16_t)n;
    b->u_i = b->u_n++;
    b->u_dirty = 0;
}
static void undo_reset(fg_box_t *b) { b->u_n = 0; b->u_i = 0; undo_commit(b); }
static void undo_step(fg_box_t *b, int redo)
{
    char v[28];
    if (b->u_dirty) undo_commit(b);
    if (redo ? b->u_i + 1u >= b->u_n : b->u_i == 0) { show_ov(b, redo ? "REHACER" : "DESHACER", "NO HAY MÁS", 0); return; }
    b->u_i = (uint8_t)(redo ? b->u_i + 1u : b->u_i - 1u);
    snap_restore(b, b->undo[b->u_i], b->u_len[b->u_i]);
    v[0] = 0; cati(v, b->u_i + 1, sizeof v); scat(v, " DE ", sizeof v); cati(v, b->u_n, sizeof v);
    show_ov(b, redo ? "REHACER" : "DESHACER", v, b->u_n > 1 ? b->u_i * 256 / (b->u_n - 1) : 256);
}

#define LONG_BTNS (1u << FB_EDIT | 1u << FB_SCL | 1u << FB_HOME | 1u << FB_ENV | 1u << FB_FX | 1u << FB_LFO | 1u << FB_GLO | 1u << FB_ARP)
static void long_press(fg_box_t *b)
{
    static const uint8_t ID[8] = {FB_EDIT, FB_SCL, FB_HOME, FB_ENV, FB_FX, FB_LFO, FB_GLO, FB_ARP};
    static const char *const WHAT[8] = {"SONIDO INICIAL", "VACIAR PISTA", "VACIAR PATRÓN", "MEZCLA INICIAL", "EFECTOS INICIALES",
                                        "SIN LFO", "SWING Y ESCALA", "ARP SOSTENIDO"};
    for (unsigned k = 0; k < 8; k++) {
        unsigned id = ID[k];
        if (!held(b, id) || ((b->used_btn | b->long_done) >> id & 1u)) continue;
        if (id == FB_HOME && (b->page != PG_CHAIN || b->nhome)) continue;
        if (id == FB_FX && b->fx_used) continue;
        if (b->wdown || b->bdown) continue;                  /* (con teclas apretadas es otra cosa) */
        uint32_t dt = b->now - b->press_ms[id];
        if (dt < 300u) continue;
        if (dt < 1000u) {                                /* (los de panel de ayuda los dibuja fg_box_draw: fg_box_hint) */
            if (id == FB_HOME || id == FB_FX) show_ov(b, "MANTÉN PARA", WHAT[k], (int)((dt - 300u) * 256u / 700u));
            continue;
        }
        b->long_done |= (uint16_t)(1u << id);
        switch (id) {
        case FB_EDIT: init_sound(b); break;
        case FB_SCL: clear_track(b); break;
        case FB_HOME: clear_pattern(b); break;
        case FB_ENV: reset_mix(b); break;
        case FB_FX: reset_fx(b); break;
        case FB_LFO: reset_lfo(b); break;
        case FB_GLO: reset_glo(b); break;
        default: arp_hold(b);
        }
    }
}

void fg_box_enc(fg_box_t *b, unsigned role, int d)
{
    if (!d) return;
    /* aceleración: girar rápido mueve más (solo perillas de rango largo) */
    uint32_t dt = b->now - b->enc_ms[role];
    b->enc_ms[role] = b->now;
    b->used_btn |= (uint16_t)(b->down & LONG_BTNS);   /* girar con el botón apretado no es mantenerlo */
    unsigned t = b->sel;
    switch (role) {
    case FE_SELECT: select_track(b, (unsigned)((int)t + (d > 0 ? 1 : -1) + 8) % 8u); audition(b, -1); return;
    case FE_ALGO: {
        unsigned n = NMODELS[t];
        b->s.model[t] = (uint8_t)(((int)b->s.model[t] + (d > 0 ? 1 : -1) + (int)n) % (int)n);
        post(b, M_MODEL, t, b->s.model[t], 0, 0);
        clear_acc(b);
        char lab[32]; scpy(lab, TN[t], sizeof lab); scat(lab, " · MOTOR", sizeof lab);
        show_ov(b, lab, MN(b, t), n > 1 ? b->s.model[t] * 256 / (int)(n - 1u) : 0);
        audition(b, -1); mark(b);
        return; }
    case FE_PRESET:
        enc_presets(b, d > 0 ? 1 : -1);
        return;
    default: {
        unsigned k = role - FE_K1;
        tgt_t g;
        int mult = 1;
        /* rango largo (0..127): 2 por clic despacio y hasta 12 girando rápido (de punta a punta en media vuelta);
         * rango corto (modos, listas): 1 por clic, 2 si va rápido */
        if (target(b, k, &g)) {
            if (g.max - g.min > 40) mult = dt < 20u ? 12 : dt < 45u ? 7 : dt < 90u ? 4 : 2;
            else mult = dt < 45u ? 2 : 1;
        }
        turn(b, k, d * mult);
        return; }
    }
}

/* ---- botones ---- */
static const uint8_t PAGES_N[7] = {2, 2, 2, 2, 2, 2, 1};
static int page_of(unsigned id)
{
    switch (id) { case FB_EDIT: return PG_EDIT; case FB_ENV: return PG_ENV; case FB_FX: return PG_FX; case FB_SCL: return PG_SCL; case FB_LFO: return PG_LFO; case FB_GLO: return PG_GLO; }
    return -1;
}
static void set_page(fg_box_t *b, unsigned pg)
{
    if (b->page == pg && PAGES_N[pg] > 1) b->pn = (uint8_t)((b->pn + 1u) % PAGES_N[pg]);
    else { b->page = (uint8_t)pg; b->pn = 0; }
    clear_acc(b);
    for (unsigned k = 0; k < 4; k++) { tgt_t g; scpy(b->names[k], target(b, k, &g) ? g.label : "–", sizeof b->names[k]); }
    b->names_until = b->now + 1300u; b->ov.until = 0; b->big_until = 0;
}

static int save_project(fg_box_t *b, unsigned slot);
static void load_project(fg_box_t *b, unsigned slot);

void fg_box_button(fg_box_t *b, unsigned id, int down)
{
    if (id >= FB_NBTN) return;
    b->intro = 0;
    if (!down) {
        if (!held(b, id)) return;
        b->down &= (uint16_t)~(1u << id);
        if ((LONG_BTNS >> id & 1u) && !(b->long_done >> id & 1u) && !(b->used_btn >> id & 1u) && b->now - b->press_ms[id] >= 300u) {
            if (!(id == FB_HOME && (b->page != PG_CHAIN || b->nhome))) { b->used_btn |= (uint16_t)(1u << id); b->ov.until = 0; }
        }
        switch (id) {
        case FB_ARP:                                     /* tocar ARP: el arpegiador de la pista prende o apaga */
            if (!((b->used_btn | b->long_done) & (1u << FB_ARP))) {
                unsigned t = b->sel;
                if (!is_synth(t)) { show_ov(b, "ARPEGIADOR", "SOLO SINTES", 0); break; }
                b->s.arp_on ^= (uint8_t)(1u << t);
                send_arp(b, t);
                if (((b->s.arp_on >> t) & 1u) && b->seqmode) { b->seqmode = 0; b->oct = 0; }   /* prenderlo es para tocar: las teclas pasan a notas */
                show_ov(b, TN[t], (b->s.arp_on >> t) & 1u ? "ARPEGIADOR SÍ · TOCAR" : "ARPEGIADOR NO", (b->s.arp_on >> t) & 1u ? 256 : 0);
                mark(b);
            }
            break;
        case FB_FX:
            if (!b->fx_used && !((b->used_btn | b->long_done) & (1u << FB_FX))) set_page(b, PG_FX);
            else {                                       /* soltar FX antes que la tecla: se engancha */
                for (unsigned k = 0; k < b->nhw; k++) if ((b->perf >> b->hw[k]) & 1u) b->latch |= (uint16_t)(1u << b->hw[k]);
                b->perf &= b->latch;
                send_perf(b);
            }
            break;
        case FB_HOME:
            if (b->nhome == 1) {
                if (b->s.chain_on) { b->s.chain_on = 0; send_chain(b, 0); }
                post(b, M_QUEUE, 0, 0, 0, b->home_list[0]);
                mark(b);
            } else if (b->nhome > 1) {                   /* la cadena rápida: las blancas en orden quedan en las filas */
                for (unsigned r = 0; r < 16; r++) { b->s.crow[r] = r < b->nhome ? b->home_list[r] : 255; b->s.crep[r] = 1; }
                b->s.chain_on = 1; b->crow_cur = 0;
                send_chain(b, 1);
                mark(b);
            } else if (!(b->used_btn & (1u << FB_HOME)) && !(b->long_done & (1u << FB_HOME)) && b->now - b->press_ms[FB_HOME] < 400u) {
                if (b->page == PG_CHAIN) b->page = b->prev_page;   /* tocar HOME: la vista CADENA y de regreso */
                else {
                    b->prev_page = b->page; b->page = PG_CHAIN;
                    if (!chain_rows(b)) { b->s.crow[0] = b->s.cur; b->s.crep[0] = 1; }
                }
                b->pn = 0; b->ov.until = 0; b->names_until = 0; b->big_until = 0; clear_acc(b);
            }
            b->copy_kind = 0;
            break;
        case FB_SAVE:
            if (!(b->used_btn & (1u << FB_SAVE)) && !save_project(b, FBX_SLOTS)) show_ov(b, "PROYECTO", "GUARDADO", 256);
            break;
        case FB_REC:
            if (!(b->used_btn & (1u << FB_REC))) { b->rec ^= 1u; if (b->rec && !b->seqmode && !b->playing) b->cursor = 0; }
            break;
        }
        b->used_btn &= (uint16_t)~(1u << id);
        b->long_done &= (uint16_t)~(1u << id);
        return;
    }
    b->down |= (uint16_t)(1u << id);
    b->press_ms[id] = b->now;
    b->long_done &= (uint16_t)~(1u << id);
    int dbl = b->now - b->tap_btn[id] < 350u;
    b->tap_btn[id] = b->now;
    if (held(b, FB_SCL) && (id == FB_EDIT || id == FB_SEQ)) {   /* SEL + EDIT: sonido al azar; SEL + SEQ: ritmo al azar */
        b->used_btn |= (uint16_t)(1u << FB_SCL | 1u << id);
        b->page = b->pg_before; b->pn = b->pn_before; b->names_until = 0;   /* (SEL ya había cambiado de página: de regreso) */
        if (id == FB_EDIT) rand_sound(b); else rand_rhythm(b);
        return;
    }
    if ((id == FB_SAVE && held(b, FB_EDIT)) || (id == FB_EDIT && held(b, FB_SAVE))) {   /* EDIT + SAVE: guardar el sonido */
        b->used_btn |= (uint16_t)(1u << FB_SAVE | 1u << FB_EDIT);
        save_sound(b);
        return;
    }
    if (id == FB_SCL) { b->pg_before = b->page; b->pn_before = b->pn; }
    int pg = page_of(id);
    if (pg >= 0 && id != FB_FX) {
        if (held(b, FB_REC) && id == FB_EDIT) {
            b->used_btn |= 1u << FB_REC | 1u << FB_EDIT;
            fg_track_t *tr = TRK(b);
            for (unsigned s = 0; s < 32; s++) for (unsigned i = 0; i < 4; i++) tr->st[s].au[i] = FG_NONE;
            show_ov(b, "AUTOMATIZACIÓN", "BORRADA", 0); mark(b);
            return;
        }
        uint8_t hs[16];
        unsigned nhs = b->seqmode ? held_steps(b, hs) : 0;
        if (nhs && b->page == pg && (pg == PG_EDIT || pg == PG_ENV)) {
            fg_track_t *tr = TRK(b);
            unsigned lo = pg == PG_EDIT ? 0 : 8, hi = pg == PG_EDIT ? 8 : 12;
            for (unsigned k = 0; k < nhs; k++) for (unsigned i = lo; i < hi; i++) tr->st[hs[k]].lk[i] = FG_NONE;
            b->used_w |= b->wdown;
            b->used_btn |= (uint16_t)(1u << id);
            show_ov(b, "LOCKS", "BORRADOS", 0); mark(b);
            return;
        }
        set_page(b, (unsigned)pg);
        return;
    }
    switch (id) {
    case FB_FX:
        b->fx_used = 0;
        if (dbl && b->latch) { b->latch = 0; b->perf = 0; send_perf(b); b->fx_used = 1; }
        break;
    case FB_HOME:
        b->nhome = 0;
        if (dbl && b->s.chain_on) {
            b->s.chain_on = 0; send_chain(b, 0); post(b, M_QUEUE, 0, 0, 0, b->s.cur);
            b->used_btn |= 1u << FB_HOME;
            show_ov(b, "CADENA", "APAGADA", 0); mark(b);
        }
        break;
    case FB_SAVE: b->saved_preset = 0; break;
    case FB_SEQ:
        if (b->page == PG_ENV || b->page == PG_CHAIN) set_page(b, PG_EDIT);   /* ahí las teclas son silencios o filas: salir para que pasen a pasos o notas */
        b->seqmode ^= 1u; b->oct = 0; show_ov(b, "MODO", b->seqmode ? "SEQ" : "TOCAR", b->seqmode ? 0 : 256); break;
    case FB_PLAY:
        b->playing ^= 1u;
        post(b, M_PLAY, 0, 0, 0, b->playing);
        break;
    case FB_OCTDN: case FB_OCTUP: {
        if (held(b, FB_SAVE)) { b->used_btn |= 1u << FB_SAVE; undo_step(b, id == FB_OCTUP); break; }   /* SAVE + OCT: deshacer / rehacer */
        int d = id == FB_OCTDN ? -1 : 1;
        char v[24];
        if (b->seqmode) {
            int pages = (TRK(b)->len + 15) / 16;
            b->sp = (uint8_t)clampi(b->sp + d, 0, pages - 1);
            v[0] = 0; cati(v, b->sp * 16 + 1, sizeof v); scat(v, "–", sizeof v); cati(v, clampi(b->sp * 16 + 16, 1, TRK(b)->len), sizeof v);
            show_ov(b, "PASOS", v, b->sp * 256);
        } else {
            b->oct = (int8_t)clampi(b->oct + d, -2, 2);
            v[0] = 0; catsi(v, b->oct, sizeof v);
            show_ov(b, "OCTAVA", v, (b->oct + 2) * 64);
        }
        break; }
    }
}

/* ---- teclas ---- */
static void play_note(fg_box_t *b, unsigned i)
{
    unsigned t = b->sel;
    int lk;
    if (is_synth(t)) {
        unsigned sc = b->s.scale & 7u, n = SCALE_N[sc];
        int semi = SCALE_I[sc][i % n] + 12 * (int)(i / n) + 12 * b->oct;
        lk = clampi(64 + semi, 0, 127);
    } else lk = (int)(i * 8u + 4u);
    b->kt[i] = (uint8_t)t;
    if (is_synth(t) && ((b->s.arp_on >> t) & 1u)) { post(b, M_ARPKEY, t, 128u + i, (unsigned)lk, 1); return; }   /* al arpegiador */
    post(b, M_TRIG, t, 110, (unsigned)lk, is_synth(t) ? (int32_t)(0x100u | (128u + i)) : 0);   /* el sinte suena mientras se sostiene */
    if (b->rec) {
        fg_track_t *tr = TRK(b);
        unsigned s;
        if (b->playing) s = b->pos[t] > 0 ? (unsigned)b->pos[t] : 0u;
        else { s = b->cursor % (tr->len ? tr->len : 1u); b->cursor = (uint8_t)((b->cursor + 1u) % (tr->len ? tr->len : 1u)); }
        tr->st[s].v = tr->st[s].v ? tr->st[s].v : 1u;
        tr->st[s].lk[0] = (uint8_t)lk;
        mark(b);
    }
}
static void toggle_mute(fg_box_t *b, unsigned t)
{
    uint8_t m = (uint8_t)((b->has_pend ? b->pend : b->mute) ^ (1u << t));
    if (b->s.muteq && b->playing) { b->pend = m; b->has_pend = 1; post(b, M_MUTE, 0, m, b->solo, 0); }
    else { b->mute = m; b->has_pend = 0; send_mutes(b, 1); }
    if (b->page != PG_ENV || held_b(b, BK_MUTE)) show_ov(b, TN[t], (m >> t) & 1u ? "SILENCIO" : "SUENA", (m >> t) & 1u ? 0 : 256);
}
static void toggle_solo(fg_box_t *b, unsigned t)
{
    b->solo ^= (uint8_t)(1u << t);
    send_mutes(b, 1);
    show_ov(b, TN[t], (b->solo >> t) & 1u ? "SOLO" : "SIN SOLO", (b->solo >> t) & 1u ? 256 : 0);
}

static void copy_pattern(fg_box_t *b, unsigned from, unsigned to)
{
    pause_audio(b);
    fg_copy(&b->Q.pat[to], &b->Q.pat[from], sizeof b->Q.pat[0]);
    fg_copy(b->s.eu[to], b->s.eu[from], sizeof b->s.eu[0]);
    fg_copy(b->s.hand[to], b->s.hand[from], sizeof b->s.hand[0]);
    b->s.has_hand[to] = b->s.has_hand[from];
    resume_audio(b);
}

static void white_press(fg_box_t *b, unsigned i)
{
    b->wdown |= (uint16_t)(1u << i);
    if (held(b, FB_ARP)) b->used_btn |= 1u << FB_ARP;
    char v[24];
    if (held(b, FB_FX)) {
        b->fx_used = 1; b->used_w |= (uint16_t)(1u << i);
        b->perf_last = (uint8_t)i;
        if ((b->latch >> i) & 1u) { b->latch &= (uint16_t)~(1u << i); b->perf &= (uint16_t)~(1u << i); }
        else b->perf |= (uint16_t)(1u << i);
        if (b->nhw < 16) b->hw[b->nhw++] = (uint8_t)i;
        send_perf(b);
        return;
    }
    if (held(b, FB_HOME)) {
        b->used_btn |= 1u << FB_HOME;
        if (held_b(b, BK_COPY)) {
            if (i >= FBX_NP) { show_ov(b, "PATRÓN", "NO HAY", 0); return; }
            if (b->copy_kind != 1) { b->copy_kind = 1; b->copy_idx = (int8_t)i; v[0] = 0; scpy(v, "PATRÓN ", sizeof v); cati(v, (int)i + 1, sizeof v); show_ov(b, "COPIA", v, 0); }
            else { copy_pattern(b, (unsigned)b->copy_idx, i); v[0] = 0; scpy(v, "PATRÓN ", sizeof v); cati(v, (int)i + 1, sizeof v); show_ov(b, "PEGADO", v, 256); mark(b); }
            return;
        }
        if (i >= FBX_NP) { show_ov(b, "PATRÓN", "NO HAY", 0); return; }
        if (b->nhome < 16) b->home_list[b->nhome++] = (uint8_t)i;
        v[0] = 0;
        for (unsigned k = 0; k < b->nhome; k++) { if (k) scat(v, " › ", sizeof v); cati(v, b->home_list[k] + 1, sizeof v); }
        show_ov(b, b->nhome > 1 ? "CADENA" : "PATRÓN", v, (int)(i * 256u / 15u));
        return;
    }
    if (held(b, FB_SAVE)) {
        b->used_btn |= 1u << FB_SAVE;
        int dbl = b->now - b->tap_slot[i] < 400u;
        b->tap_slot[i] = b->now;
        v[0] = 0; scpy(v, "RANURA ", sizeof v); cati(v, (int)i + 1, sizeof v);
        if (!((b->slots >> i) & 1u)) { if (!save_project(b, i)) show_ov(b, "GUARDADO EN", v, 256); return; }
        /* llena: doble toque la abre; mantenerla 1 s guarda encima (un toque nunca borra nada) */
        if (dbl) { b->slot_arm = 0; load_project(b, i); return; }
        b->slot_arm = (uint8_t)(i + 1u); b->slot_ms = b->now;
        show_ov(b, v, "DOBLE = ABRIR", 0);
        return;
    }
    if (held_b(b, BK_COPY)) {
        fg_track_t *tr = TRK(b);
        unsigned s = b->sp * 16u + i;
        if (s >= tr->len) return;
        if (b->copy_kind != 2) { b->copy_kind = 2; b->copy_idx = (int8_t)s; v[0] = 0; scpy(v, "PASO ", sizeof v); cati(v, (int)s + 1, sizeof v); show_ov(b, "COPIA", v, 0); }
        else { tr->st[s] = tr->st[b->copy_idx]; v[0] = 0; scpy(v, "PASO ", sizeof v); cati(v, (int)s + 1, sizeof v); show_ov(b, "PEGADO", v, 256); mark(b); }
        return;
    }
    if (b->page == PG_CHAIN) { b->crow_cur = (uint8_t)i; clear_acc(b); return; }   /* en la CADENA: la fila */
    if (!b->seqmode) { play_note(b, i); return; }
    if (b->page == PG_ENV && !b->nhw && !held(b, FB_ARP)) {
        if (i < 8) { b->mix_armed |= (uint8_t)(1u << i); b->mix_t[i] = b->now; }
        return;
    }
    if (b->nhw < 16) b->hw[b->nhw++] = (uint8_t)i;
    clear_acc(b);
}
static void white_release(fg_box_t *b, unsigned i)
{
    b->wdown &= (uint16_t)~(1u << i);
    if (b->slot_arm == i + 1u) b->slot_arm = 0;
    if (i < 8 && ((b->mix_armed >> i) & 1u)) {
        b->mix_armed &= (uint8_t)~(1u << i);
        if (!((b->used_w >> i) & 1u)) toggle_mute(b, i);
        b->used_w &= (uint16_t)~(1u << i);
        return;
    }
    int k = -1;
    for (unsigned j = 0; j < b->nhw; j++) if (b->hw[j] == i) { k = (int)j; break; }
    if (k >= 0) { for (unsigned j = (unsigned)k; j + 1 < b->nhw; j++) b->hw[j] = b->hw[j + 1]; b->nhw--; }
    int perf_held = ((b->perf >> i) & 1u) && !((b->latch >> i) & 1u);
    if (held(b, FB_FX) || perf_held) {
        if (perf_held) { b->perf &= (uint16_t)~(1u << i); send_perf(b); }
        b->used_w &= (uint16_t)~(1u << i);
        return;
    }
    if (!b->seqmode) {
        if (is_synth(b->kt[i])) post(b, (b->s.arp_on >> b->kt[i]) & 1u ? M_ARPKEY : M_NOTEOFF, b->kt[i], 128u + i, 0, 0);
        b->used_w &= (uint16_t)~(1u << i);
        return;
    }
    if (k >= 0 && !((b->used_w >> i) & 1u) && !held(b, FB_ARP) && !held(b, FB_HOME) && !held(b, FB_SAVE) && !held_b(b, BK_COPY)) {
        fg_track_t *tr = TRK(b);
        unsigned s = b->sp * 16u + i;
        if (s < tr->len) {
            int dbl = b->now - b->tap_step[s] < 320u;
            b->tap_step[s] = b->now;
            fg_step_t *st = &tr->st[s];
            if (dbl && st->v == 0) st->v = 2;
            else if (dbl && st->v == 1 && b->just_on == s) st->v = 2;
            else if (st->v) { st->v = 0; b->just_on = 255; }
            else { st->v = 1; b->just_on = (uint8_t)s; }
            mark(b);
        }
    }
    b->used_w &= (uint16_t)~(1u << i);
}
static void black_press(fg_box_t *b, unsigned i)
{
    b->bdown |= (uint16_t)(1u << i);
    if (i == BK_FILL) { post(b, M_FILL, 0, 0, 0, 1); return; }
    if (i >= 8) return;
    if (held_b(b, BK_MUTE)) { toggle_mute(b, i); return; }
    if (held_b(b, BK_COPY)) {
        char v[24];
        if (b->copy_kind != 3) { b->copy_kind = 3; b->copy_idx = (int8_t)i; show_ov(b, "COPIA", TN[i], 0); }
        else {
            pause_audio(b);
            b->Q.pat[b->s.cur].tr[i] = b->Q.pat[b->s.cur].tr[b->copy_idx];
            resume_audio(b);
            v[0] = 0; scpy(v, TN[i], sizeof v);
            show_ov(b, "PEGADO", v, 256); mark(b);
        }
        return;
    }
    select_track(b, i);
    audition(b, -1);
}
static void black_release(fg_box_t *b, unsigned i)
{
    b->bdown &= (uint16_t)~(1u << i);
    if (i == BK_FILL) post(b, M_FILL, 0, 0, 0, 0);
    if (i == BK_COPY) b->copy_kind = 0;
}

void fg_box_key(fg_box_t *b, unsigned k, int down)
{
    if (k >= FBX_KEYS) return;
    b->intro = 0;
    if (down) b->used_btn |= (uint16_t)(b->down & (1u << FB_EDIT | 1u << FB_SCL));
    if (KEYW[k] >= 0) { if (down) white_press(b, (unsigned)KEYW[k]); else white_release(b, (unsigned)KEYW[k]); }
    else if (KEYB[k] >= 0) { if (down) black_press(b, (unsigned)KEYB[k]); else black_release(b, (unsigned)KEYB[k]); }
}

void fg_box_tick(fg_box_t *b, uint32_t now)
{
    b->now = now;
    if (b->arec) { b->arec = 0; mark(b); }
    if (b->slot_arm) {                                   /* SAVE + blanca de ranura llena sostenida: guardar encima */
        unsigned i = b->slot_arm - 1u, dt = now - b->slot_ms;
        char v[24]; v[0] = 0; scpy(v, "RANURA ", sizeof v); cati(v, (int)i + 1, sizeof v);
        if (!held(b, FB_SAVE)) b->slot_arm = 0;
        else if (dt >= 1000u) { b->slot_arm = 0; if (!save_project(b, i)) show_ov(b, "GUARDADO ENCIMA", v, 256); }
        else if (dt >= 300u) show_ov(b, "MANTÉN = GUARDAR ENCIMA", v, (int)((dt - 300u) * 256u / 700u));
    }
    long_press(b);
    /* ENV: mantener la blanca de la pista = solo */
    for (unsigned i = 0; i < 8; i++)
        if (((b->mix_armed >> i) & 1u) && !((b->used_w >> i) & 1u) && now - b->mix_t[i] >= 420u) {
            b->used_w |= (uint16_t)(1u << i);
            toggle_solo(b, i);
        }
    /* lo que el audio cambió */
    if (b->a_ext_play) { b->playing = b->a_ext_play == 1; b->a_ext_play = 0; }
    if (b->a_bpm && b->a_bpm != b->s.bpm && (b->s.reloj & 1u)) b->s.bpm = b->a_bpm;
    b->s.cur = b->a_cur;
    b->queued = b->a_queued;
    b->chainpos = b->a_chainpos;
    if (b->a_mute != b->mute && b->has_pend && b->a_mute == b->pend) { b->mute = b->pend; b->has_pend = 0; }
    while (b->hq_r != b->hq_w) {
        const fbx_hit_t *h = &b->hq[b->hq_r];
        fg_vis_hit(&b->vis, h->t, h->m, h->vel, h->p, now);
        if (b->mirror && (uint8_t)(b->mh_w - b->mh_r) < 16u) b->mh[b->mh_w++ & 15u] = *h;
        b->hq_r = (uint16_t)((b->hq_r + 1u) % (sizeof b->hq / sizeof b->hq[0]));
    }
    for (unsigned t = 0; t < 8; t++) fg_vis_level(&b->vis, t, b->level[t]);
    if (b->intro) {                                      /* el arranque: un compás corto con los visuales de tus pistas */
        static const struct { uint16_t at; uint8_t t; } HIT[8] = {{420, 0}, {520, 2}, {640, 4}, {760, 0}, {860, 3}, {980, 6}, {1100, 0}, {1180, 5}};
        uint32_t it = now - b->intro_t0;
        while (b->intro_k < 8u && it >= HIT[b->intro_k].at) {
            unsigned t = HIT[b->intro_k++].t;
            fg_vis_hit(&b->vis, t, b->s.model[t], 110, P(b, t), now);
        }
        for (unsigned t = 0; t < 8; t++) fg_vis_level(&b->vis, t, 22000);
    }
}

void fg_box_midi_in(fg_box_t *b, uint8_t st, uint8_t d1, uint8_t d2)
{
    unsigned t = st & 0x0Fu, ty = st & 0xF0u;
    char lab[44], val[28];
    if (!(b->s.mnotes & 1u) || t > 7u) return;
    if (t >= 4 && ((b->s.arp_on >> t) & 1u) && (ty == 0x90u || ty == 0x80u)) {   /* al arpegiador */
        post(b, M_ARPKEY, t, d1 & 127u, (unsigned)clampi(64 + (int)d1 - 60, 0, 127), ty == 0x90u && d2);
        return;
    }
    if (ty == 0x90u && d2) {                             /* sinte: suena mientras se sostiene; do 60 = su NOTA */
        post(b, M_TRIG, t, d2, t >= 4 ? (unsigned)clampi(64 + (int)d1 - 60, 0, 127) : FG_NONE, t >= 4 ? (int32_t)(0x100u | (d1 & 127u)) : 0);
        return;
    }
    if (ty == 0x80u || ty == 0x90u) { if (t >= 4) post(b, M_NOTEOFF, t, d1 & 127u, 0, 0); return; }
    if (ty == 0xC0u) {                                   /* program change: el patrón, al terminar el que suena */
        if (d1 >= FBX_NP) return;
        if (b->s.chain_on) { b->s.chain_on = 0; send_chain(b, 0); }
        post(b, M_QUEUE, 0, 0, 0, d1);
        scpy(val, "PATRÓN ", sizeof val); cati(val, d1 + 1, sizeof val);
        show_ov(b, "MIDI", val, d1 * 256 / (FBX_NP - 1)); mark(b);
        return;
    }
    if (ty != 0xB0u) return;
    int mx = d1 == 7 ? 0 : d1 == 10 ? 1 : d1 == 12 ? 2 : d1 == 91 ? 3 : -1;
    scpy(lab, TN[t], sizeof lab); scat(lab, " · ", sizeof lab);
    if (d1 >= 16 && d1 <= 23) {                          /* las 8 perillas del motor de la pista */
        unsigned i = d1 - 16u, m = b->s.model[t];
        b->s.p[t][m][i] = d2; send_param(b, t, m, i);
        if (i == 7) scat(lab, "NIVEL", sizeof lab); else scat(lab, PL(b, t, i), sizeof lab);
        if (i == 7) { val[0] = 0; cati(val, d2, sizeof val); } else fmt_param(b, t, i, d2, 0, val, sizeof val);
    } else if (mx >= 0) {
        b->s.mix[t][mx] = d2; post(b, M_MIX, t, (unsigned)mx, 0, d2);
        scat(lab, MIXL[mx], sizeof lab);
        fmt_mix((unsigned)mx, d2, val, sizeof val);
    } else return;
    if (t == b->sel) clear_acc(b);
    show_ov(b, lab, val, d2 * 2);
    mark(b);
}

/* ---- espejo: F0 7D 46 47 tipo … F7 (7 bits). 1 golpe: pista modelo fuerza p0..p7. 2 estado: vista, tempo (2),
 * pista, patrón, en cola, patrones con pasos (3), mutes (2), solos (2), al compás (2), hay (1), efectos (3),
 * niveles (8), mezcla (32) ---- */
unsigned fg_box_mirror(fg_box_t *b, uint8_t *o, int state)
{
    unsigned n = 0;
    o[n++] = 0xF0; o[n++] = 0x7D; o[n++] = 0x46; o[n++] = 0x47;
    if (b->mh_r != b->mh_w) {
        const fbx_hit_t *h = &b->mh[b->mh_r++ & 15u];
        o[n++] = 1; o[n++] = h->t & 7u; o[n++] = h->m & 127u; o[n++] = h->vel & 127u;
        for (unsigned i = 0; i < 8; i++) o[n++] = h->p[i] & 127u;
    } else if (state) {
        const fg_vis_ui_t *u = &b->vui;
        o[n++] = 2; o[n++] = u->view & 127u; o[n++] = (uint8_t)(u->bpm >> 7); o[n++] = u->bpm & 127u;
        o[n++] = u->sel & 127u; o[n++] = u->cur & 127u; o[n++] = u->queued == 255 ? 127u : u->queued & 127u;
        o[n++] = (uint8_t)(u->used >> 14); o[n++] = (u->used >> 7) & 127u; o[n++] = u->used & 127u;
        o[n++] = (uint8_t)(u->mute >> 7); o[n++] = u->mute & 127u; o[n++] = (uint8_t)(u->solo >> 7); o[n++] = u->solo & 127u;
        o[n++] = (uint8_t)(u->pend >> 7); o[n++] = u->pend & 127u; o[n++] = u->has_pend & 1u;
        o[n++] = (uint8_t)(u->perf >> 14) & 127u; o[n++] = (u->perf >> 7) & 127u; o[n++] = u->perf & 127u;
        for (unsigned t = 0; t < 8; t++) { int32_t l = b->level[t] >> 8; o[n++] = (uint8_t)(l > 127 ? 127 : l < 0 ? 0 : l); }
        for (unsigned t = 0; t < 8; t++) for (unsigned i = 0; i < 4; i++) o[n++] = u->mix[t][i] & 127u;
        for (unsigned t = 0; t < 8; t++) { int s = b->pos[t]; o[n++] = (uint8_t)(b->a_playing && s >= 0 && s < 127 ? s : 127); }   /* 0.9: el paso de cada pista */
        o[n++] = b->a_playing ? 1u : 0u;
    } else return 0;
    o[n++] = 0xF7;
    return n;
}

/* ---- luces: prendida = sí, apagada = no; lo único que se mueve es la cabeza del secuenciador ---- */
void fg_box_leds(fg_box_t *b, uint32_t *btn, uint32_t *keys)
{
    const fg_track_t *tr = TRK(b);
    unsigned t = b->sel;
    int pos = b->pos[t];
    uint8_t want = b->has_pend ? b->pend : b->mute;
    uint32_t w = 0, bk = 0, bt = 0;
    for (unsigned i = 0; i < 16; i++) {
        unsigned s = b->sp * 16u + i, on = 0;
        if (held(b, FB_FX)) on = (b->perf >> i) & 1u;
        else if (held(b, FB_HOME)) {
            on = i == b->s.cur || i == b->queued;
            for (unsigned k = 0; k < b->nhome; k++) if (b->home_list[k] == i) on = 1;
            if (b->s.chain_on) for (unsigned r = 0; r < 16; r++) if (b->s.crow[r] == i) on = 1;
        } else if (held(b, FB_SAVE)) on = (b->slots >> i) & 1u;
        else if (b->page == PG_CHAIN) {                  /* las filas con patrón; la que suena parpadea */
            on = b->s.crow[i] < FBX_NP;
            if ((int)i == fg_box_chain_row(b) && (b->now >> 7 & 1u)) on = !on;
        } else if (!b->seqmode) {
            if (is_synth(t)) on = i % SCALE_N[b->s.scale & 7u] == 0;
            else on = i == P(b, t)[0] / 8u;              /* percusión: la altura de su TONO */
            if ((b->wdown >> i) & 1u) on = 1;
        } else if (b->page == PG_ENV && !b->nhw && !held(b, FB_ARP)) {
            if (i < 8) on = !(((want >> i) & 1u) || (b->solo && !((b->solo >> i) & 1u)));
        } else if (held(b, FB_ARP) && !b->nhw) {
            const fg_step_t *st = &tr->st[s];
            on = s < tr->len && (st->c || st->p != 100 || st->r != 1 || st->u);
        } else {
            on = s < tr->len && tr->st[s].v;
            if (b->playing && (int)s == pos) on = !on;
        }
        if (on) w |= 1u << i;
    }
    for (unsigned i = 0; i < 11; i++) {
        unsigned on;
        if (i < 8) on = held_b(b, BK_MUTE) ? !((want >> i) & 1u) : i == t;
        else on = held_b(b, i);
        if (on) bk |= 1u << i;
    }
    for (unsigned id = 0; id < FB_NBTN; id++) {
        int pg = page_of(id), on = 0;
        if (pg >= 0) on = b->page == pg;
        else if (id == FB_HOME) on = b->page == PG_CHAIN;
        else if (id == FB_ARP) on = (b->s.arp_on >> t) & 1u;
        else if (id == FB_SEQ) on = !b->seqmode;
        else if (id == FB_PLAY) on = b->playing;
        else if (id == FB_REC) on = b->rec;
        if (on) bt |= 1u << id;
    }
    uint32_t km = 0;
    for (unsigned k = 0; k < FBX_KEYS; k++) {
        if (KEYW[k] >= 0 && ((w >> KEYW[k]) & 1u)) km |= 1u << k;
        if (KEYB[k] >= 0 && ((bk >> KEYB[k]) & 1u)) km |= 1u << k;
    }
    *btn = bt; *keys = km;
}

uint32_t fg_box_leds_half(const fg_box_t *b)
{
    uint32_t h = 0;
    for (unsigned id = 0; id < FB_NBTN; id++) { int pg = page_of(id); if (pg >= 0 && b->page != pg) h |= 1u << id; }
    return h;
}

/* ------------------------------------------------------------- guardado --- */
/* v4 -> v5 (0.8): de 11 sintes quedaron 8; PLIEGUE, FASE ROTA, MUELLE y ACORDE pasan a SH (con su NOTA y su NIVEL) */
static const uint8_t SYN_V4[11] = {FGY_SH, FGY_ACIDO, FGY_SH, FGY_SH, FGY_FM, FGY_SH, FGY_CUERDAS, FGY_TEXTURA, FGY_DRON, FGY_DUB, FGY_RESONADOR};
static int syn_v4_kept(unsigned om) { return om == 1u || (om >= 4u && om != 5u); }   /* los que siguen siendo el mismo motor */
static void remap_synths_v4(fg_box_t *b, unsigned ver)
{
    fbx_song_t *s = &b->s;
    for (unsigned t = 4; t < 8; t++) {
        uint8_t p[FBX_NM][8], e[FBX_NM][8];
        uint16_t he = 0;
        unsigned om = s->model[t] < 11u ? s->model[t] : 0u;
        fg_copy(p, DEF_P[t], sizeof p); fg_zero(e, sizeof e);
        for (unsigned o = 0; o < 11u; o++) if (syn_v4_kept(o) && (o < 7u || ver == 4u)) {   /* (antes de v4, 7..10 no traían perillas: de fábrica) */
            unsigned m = SYN_V4[o];
            fg_copy(p[m], s->p[t][o], 8); fg_copy(e[m], s->edited[t][o], 8);
            if ((s->has_edited[t] >> o) & 1u) he |= (uint16_t)(1u << m);
        }
        if (!syn_v4_kept(om)) { p[FGY_SH][0] = s->p[t][om][0]; p[FGY_SH][7] = s->p[t][om][7]; }
        fg_copy(s->p[t], p, sizeof p); fg_copy(s->edited[t], e, sizeof e);
        s->has_edited[t] = he; s->model[t] = SYN_V4[om];
    }
}
/* un sonido guardado de antes de la 0.8: el de un motor que ya no está no tiene arreglo (0 = descartarlo) */
static int remap_upre_v4(fbx_upre_t *u)
{
    if (u->key < 4u) return 1;
    if (u->m >= 11u || !syn_v4_kept(u->m)) return 0;
    u->m = SYN_V4[u->m];
    return 1;
}
/* v3 (0.6): la canción con 10 motores de sinte (NUBE, VIENTO, ÓRGANO en 7..9) */
typedef struct {
    uint8_t model[8], p[8][10][8], mix[8][4], fx[8], lfo[8][4], azar[8];
    uint8_t bpm, swing, scale, muteq, cur;
    int8_t transp;
    uint8_t eu[16][8][2], has_hand[16], hand[16][8][32], edited[8][10][8];
    uint16_t has_edited[8];
    uint8_t crow[16], crep[16], chain_on, mclk_in, mclk_out, mnotes, mix2[8][4], scl2[8][4], lfo2[8][4], arp[8][4], arp_on;
} fbx_song_v3_t;

/* v1 -> v2: el TONO del bombo bajó de rango (BARRIDO 20–97 Hz, MEMBRANA 22–120 Hz): el mismo tono, otro número */
static uint8_t kick_tone_v1(unsigned m, unsigned v)
{
    if (v == FG_NONE) return FG_NONE;
    unsigned r = m ? (v * 8172u + 237200u) / 10000u : (v * 7877u + 274800u) / 10000u;
    return (uint8_t)(r > 127u ? 127u : r);
}
/* v6 -> v7 (0.9): RELOJ en un solo ajuste; MUESTRA pasa a SAMPLE (sus valores, sus locks y su automatización si
 * es el motor de la pista, su versión EDITADA y sus sonidos); METAL, PERC y los sintes estrenan SAMPLE de fábrica */
static void migrate_v6(fg_box_t *b)
{
    fbx_song_t *s = &b->s;
    if (s->reloj <= 1u) s->reloj = (uint8_t)(s->reloj | (s->rsv_clk ? 2u : 0u));   /* (arriba de 1: la de fábrica, la v1 no los traía) */
    s->rsv_clk = 0;
    for (unsigned t = 0; t < 2u; t++) {
        uint8_t o[8], e[8];
        fg_copy(o, s->p[t][DSAMPLE], 8); muestra_to_sample(t, o, s->p[t][DSAMPLE]);
        if (s->has_edited[t] >> DSAMPLE & 1u) { fg_copy(e, s->edited[t][DSAMPLE], 8); muestra_to_sample(t, e, s->edited[t][DSAMPLE]); }
        if (s->model[t] != DSAMPLE) continue;
        for (unsigned p = 0; p < FBX_NP; p++)
            for (unsigned i = 0; i < FG_SEQ_NS; i++) {
                fg_step_t *st = &b->Q.pat[p].tr[t].st[i];
                muestra_locks(t, o, st->lk);
                for (unsigned a = 0; a < 4u; a += 2u) {
                    unsigned oi = st->au[a], ni = muestra_idx(oi);
                    if (oi >= 8u) continue;                        /* (FG_NONE o una de la mezcla) */
                    uint8_t l[8];
                    for (unsigned j = 0; j < 8u; j++) l[j] = FG_NONE;
                    l[oi] = st->au[a + 1u];
                    muestra_locks(t, o, l);
                    if (ni < 8u) { st->au[a] = (uint8_t)ni; st->au[a + 1u] = l[ni]; } else st->au[a] = st->au[a + 1u] = FG_NONE;
                }
            }
    }
    for (unsigned t = 2; t < 8u; t++) {
        unsigned m = is_synth(t) ? FGY_SAMPLE : DSAMPLE;
        fg_copy(s->p[t][m], DEF_P[t][m], 8);
        s->has_edited[t] &= (uint16_t)~(1u << m);
    }
    for (unsigned k = 0; k < b->nupre; k++)
        if (b->upre[k].key < 2u && b->upre[k].m == DSAMPLE) { uint8_t o[8]; fg_copy(o, b->upre[k].p, 8); muestra_to_sample(b->upre[k].key, o, b->upre[k].p); }
}
static void migrate_v1(fg_box_t *b)
{
    fbx_song_t *s = &b->s;
    for (unsigned m = 0; m < 2; m++) {
        s->p[0][m][0] = kick_tone_v1(m, s->p[0][m][0]);
        s->edited[0][m][0] = kick_tone_v1(m, s->edited[0][m][0]);
    }
    for (unsigned k = 0; k < b->nupre; k++) if (b->upre[k].key == 0) b->upre[k].p[0] = kick_tone_v1(b->upre[k].m & 1u, b->upre[k].p[0]);
    for (unsigned p = 0; p < FBX_NP; p++)
        for (unsigned i = 0; i < FG_SEQ_NS; i++) {
            fg_step_t *st = &b->Q.pat[p].tr[0].st[i];
            st->lk[0] = kick_tone_v1(s->model[0] & 1u, st->lk[0]);
            if (FG_AU_GET(st, 0) != FG_NONE) au_set(st, 0, kick_tone_v1(s->model[0] & 1u, FG_AU_GET(st, 0)));
        }
}

/* v1 y v2 guardaban la canción tal cual estaba en memoria (6 motores, np patrones, la cadena vieja): se lee campo
 * por campo a la de ahora; lo que no traían queda como en una canción nueva. Devuelve los bytes leídos */
static uint32_t read_old_song(fg_box_t *b, const uint8_t *q0, unsigned ver, unsigned np)
{
    fbx_song_t *s = &b->s;
    const uint8_t *q = q0;
    uint8_t he[8], chain[32], nchain;
#define RD(d, l) do { fg_copy((d), q, (l)); q += (l); } while (0)
#define SKIP(l) (q += (l))
    RD(s->model, 8);
    for (unsigned t = 0; t < 8; t++) for (unsigned m = 0; m < 6; m++) RD(s->p[t][m], 8);
    RD(s->mix, 32); RD(s->fx, 8); RD(s->lfo, 32); RD(s->azar, 8);
    RD(&s->bpm, 1); RD(&s->swing, 1); RD(&s->scale, 1); RD(&s->muteq, 1); RD(&s->cur, 1); RD(&s->transp, 1);
    for (unsigned p = 0; p < np; p++) { if (p < FBX_NP) RD(s->eu[p], 16); else SKIP(16); }
    for (unsigned p = 0; p < np; p++) { if (p < FBX_NP) RD(&s->has_hand[p], 1); else SKIP(1); }
    for (unsigned p = 0; p < np; p++) { if (p < FBX_NP) RD(s->hand[p], 256); else SKIP(256); }
    for (unsigned t = 0; t < 8; t++) for (unsigned m = 0; m < 6; m++) RD(s->edited[t][m], 8);
    RD(he, 8);
    for (unsigned t = 0; t < 8; t++) s->has_edited[t] = he[t];
    RD(chain, 32); RD(&nchain, 1);
    if (ver >= 2) { RD(s->crow, 16); RD(s->crep, 16); RD(&s->chain_on, 1); RD(&s->reloj, 1); RD(&s->rsv_clk, 1); RD(&s->mnotes, 1); }
    else if (nchain > 1 && nchain <= 32) {                  /* v1: la cadena vieja queda en las filas */
        for (unsigned r = 0; r < nchain && r < 16u; r++) s->crow[r] = chain[r] < FBX_NP ? chain[r] : 255;
        s->chain_on = 1;
    }
#undef RD
#undef SKIP
    return (uint32_t)(q - q0);
}
static uint32_t old_song_size(unsigned ver, unsigned np) { return 8 + 384 + 32 + 8 + 32 + 8 + 6 + np * (16 + 1 + 256) + 384 + 8 + 33 + (ver >= 2 ? 36 : 0); }

static int step_default(const fg_step_t *s)
{
    if (s->v || s->c || s->p != 100 || s->r != 1 || s->u) return 0;
    for (int i = 0; i < 12; i++) if (s->lk[i] != FG_NONE) return 0;
    if (s->au[0] != FG_NONE || s->au[2] != FG_NONE) return 0;
    return 1;
}
/* v3: cada paso con algo trae su índice, qué campos trae y solo esos (un paso con solo golpe son 3 bytes, no 26).
 * v6: antes, un byte dice si siguen las máscaras de golpes y de acentos (4 bytes cada una) con los pasos que solo
 * tienen eso: una pista con 32 golpes ocupa 11 bytes y no 98. */
enum { SF_V = 1, SF_C = 2, SF_P = 4, SF_R = 8, SF_U = 16, SF_LK = 32, SF_AU = 64 };
static uint32_t enc_track(const fg_track_t *tr, uint8_t *buf, uint32_t cap)
{
    uint32_t n = 0;
#define PUT1(x) do { if (n >= cap) return 0; buf[n++] = (uint8_t)(x); } while (0)
    PUT1(tr->len); PUT1(tr->spd);
    uint32_t mk[2] = {0, 0};
    for (unsigned s = 0; s < FG_SEQ_NS; s++) {
        const fg_step_t *st = &tr->st[s];
        if (st->v >= 1u && st->v <= 2u) { fg_step_t o = *st; o.v = 0; if (step_default(&o)) mk[st->v - 1u] |= 1u << s; }
    }
    PUT1((mk[0] ? 1u : 0u) | (mk[1] ? 2u : 0u));
    for (unsigned k = 0; k < 2; k++) if (mk[k]) { PUT1(mk[k]); PUT1(mk[k] >> 8); PUT1(mk[k] >> 16); PUT1(mk[k] >> 24); }
    for (unsigned s = 0; s < FG_SEQ_NS; s++) {
        const fg_step_t *st = &tr->st[s];
        if (step_default(st) || ((mk[0] | mk[1]) >> s & 1u)) continue;
        unsigned lk = 0, au = 0, f;
        for (unsigned i = 0; i < 12; i++) if (st->lk[i] != FG_NONE) lk |= 1u << i;
        for (unsigned i = 0; i < 8; i++) if (FG_AU_GET(st, i) != FG_NONE) au |= 1u << i;
        f = (st->v ? SF_V : 0) | (st->c ? SF_C : 0) | (st->p != 100 ? SF_P : 0) | (st->r != 1 ? SF_R : 0) | (st->u ? SF_U : 0) | (lk ? SF_LK : 0) | (au ? SF_AU : 0);
        PUT1(s); PUT1(f);
        if (f & SF_V) PUT1(st->v);
        if (f & SF_C) PUT1(st->c);
        if (f & SF_P) PUT1(st->p);
        if (f & SF_R) PUT1(st->r);
        if (f & SF_U) PUT1(st->u);
        if (lk) { PUT1(lk); PUT1(lk >> 8); for (unsigned i = 0; i < 12; i++) if ((lk >> i) & 1u) PUT1(st->lk[i]); }
        if (au) { PUT1(au); for (unsigned i = 0; i < 8; i++) if ((au >> i) & 1u) PUT1(FG_AU_GET(st, i)); }
    }
    PUT1(0xFF);
#undef PUT1
    return n;
}
/* una pista v3 (v6: con máscaras) desde buf[*n]; avanza *n. 0 = bien */
static int dec_track(fg_track_t *tr, const uint8_t *buf, uint32_t len, uint32_t *np, unsigned ver)
{
    uint32_t n = *np;
#define G1(x) do { if (n >= len) return -1; (x) = buf[n++]; } while (0)
    fg_seq_clear_track(tr);
    if (n + 2u > len) return -1;
    tr->len = buf[n] >= 1 && buf[n] <= 32 ? buf[n] : 16; tr->spd = buf[n + 1] <= 2 ? buf[n + 1] : 1; n += 2;
    if (ver >= 6u) {
        unsigned has; G1(has);
        for (unsigned k = 0; k < 2; k++) {
            if (!((has >> k) & 1u)) continue;
            if (n + 4u > len) return -1;
            uint32_t m = buf[n] | (uint32_t)buf[n + 1] << 8 | (uint32_t)buf[n + 2] << 16 | (uint32_t)buf[n + 3] << 24; n += 4;
            for (unsigned s = 0; s < FG_SEQ_NS; s++) if ((m >> s) & 1u) tr->st[s].v = (uint8_t)(k + 1u);
        }
    }
    for (;;) {
        unsigned si, f;
        G1(si);
        if (si == 0xFF) break;
        if (si >= FG_SEQ_NS) return -1;
        fg_step_t *st = &tr->st[si];
        G1(f);
        if (f & SF_V) G1(st->v);
        if (f & SF_C) G1(st->c);
        if (f & SF_P) G1(st->p);
        if (f & SF_R) G1(st->r);
        if (f & SF_U) { uint8_t u; G1(u); st->u = (int8_t)u; }
        if (f & SF_LK) { unsigned lo, hi; G1(lo); G1(hi); unsigned lk = lo | hi << 8; for (unsigned i = 0; i < 12; i++) if ((lk >> i) & 1u) G1(st->lk[i]); }
        if (f & SF_AU) { unsigned au, v; G1(au); for (unsigned i = 0; i < 8; i++) if ((au >> i) & 1u) { G1(v); au_set(st, i, v); } }
    }
#undef G1
    *np = n;
    return 0;
}
uint32_t fg_box_pack(fg_box_t *b, uint8_t *buf, uint32_t cap)
{
    uint32_t n = 0;
#define PUT(p, len) do { if (n + (len) > cap) return 0; fg_copy(buf + n, (p), (len)); n += (len); } while (0)
    uint32_t hdr[2] = {PACK_MAGIC, PACK_VER | (uint32_t)FBX_NP << 8 | (uint32_t)sizeof(fbx_song_t) << 16};
    PUT(hdr, sizeof hdr);
    /* la canción sin hand[] (casi siempre ceros: 4 KB); después, solo las pistas con pasos a mano */
    PUT(&b->s, offsetof(fbx_song_t, hand));
    PUT(&b->s.edited, sizeof b->s - offsetof(fbx_song_t, edited));
    for (unsigned p = 0; p < FBX_NP; p++) for (unsigned t = 0; t < 8; t++) if ((b->s.has_hand[p] >> t) & 1u) PUT(b->s.hand[p][t], 32);
    PUT(&b->nupre, 1);
    PUT(b->upre, b->nupre * sizeof b->upre[0]);
    for (unsigned p = 0; p < FBX_NP; p++)
        for (unsigned t = 0; t < 8; t++) {
            uint32_t k = enc_track(&b->Q.pat[p].tr[t], buf + n, cap - n);
            if (!k) return 0;
            n += k;
        }
#undef PUT
    return n;
}
/* lo que se lee de la flash se acota: el CRC protege de la corrupción, no de un proyecto de otra versión */
static void lim(uint8_t *a, unsigned n, unsigned hi) { while (n--) { if (*a > hi) *a = (uint8_t)hi; a++; } }
static void song_sanitize(fbx_song_t *s)
{
    static const uint8_t LFO_MA[4] = {6, 127, 127, 4}, ARP_MA[4] = {4, 5, 2, 1};
    for (unsigned t = 0; t < 8; t++) {
        if (s->model[t] >= NMODELS[t]) s->model[t] = 0;
        for (unsigned i = 0; i < 4; i++) {
            if (s->lfo[t][i] > LFO_MA[i]) s->lfo[t][i] = LFO_MA[i];
            if (s->lfo2[t][i] > LFO_MA[i]) s->lfo2[t][i] = LFO_MA[i];
            if (s->arp[t][i] > ARP_MA[i]) s->arp[t][i] = ARP_MA[i];
        }
    }
    lim(&s->p[0][0][0], sizeof s->p, 127); lim(&s->edited[0][0][0], sizeof s->edited, 127);
    lim(&s->mix[0][0], sizeof s->mix, 127); lim(&s->mix2[0][0], sizeof s->mix2, 127); lim(&s->scl2[0][0], sizeof s->scl2, 127);
    lim(s->fx, sizeof s->fx, 127); lim(s->azar, sizeof s->azar, 127);
    if (s->bpm < 40u || s->bpm > 250u) s->bpm = 120;
    lim(&s->swing, 1, 75); lim(&s->scale, 1, 7);
    if (s->transp < -12 || s->transp > 12) s->transp = 0;
    for (unsigned p = 0; p < FBX_NP; p++) for (unsigned t = 0; t < 8; t++) { lim(&s->eu[p][t][0], 1, 32); lim(&s->eu[p][t][1], 1, 31); }
    for (unsigned r = 0; r < 16; r++) {
        if (s->crow[r] >= FBX_NP) s->crow[r] = 255;
        if (!s->crep[r] || s->crep[r] > 16u) s->crep[r] = 1;
    }
}
static void song_defaults(fg_box_t *b);
int fg_box_unpack(fg_box_t *b, const uint8_t *buf, uint32_t len)
{
    uint32_t n = 0, hdr[2];
    if (len < sizeof hdr) return -1;
    fg_copy(hdr, buf, sizeof hdr); n = sizeof hdr;
    unsigned ver = hdr[1] & 0xFFu, np = (hdr[1] >> 8) & 0xFFu, ss = hdr[1] >> 16;
    if (hdr[0] != PACK_MAGIC || !np || np > 64u) return -2;
    if (ver == PACK_VER || ver == 6u || ver == 5u || ver == 4u ? (ss != sizeof(fbx_song_t) || np != FBX_NP) :
        ver == 3u ? (ss != sizeof(fbx_song_v3_t) || np != 16u) : (ver < 1u || ver > 2u || ss != old_song_size(ver, np))) return -2;
    if (ver < 6u && n + ss + 1u > len) return -1;          /* (la v6 y la v7 se revisan por partes) */
    pause_audio(b);
    song_defaults(b);                                     /* (también vacía los patrones) */
    fg_copy(b->s.p, DEF_P, sizeof DEF_P);
    if (ver >= 6u) {                                      /* sin hand[]: va después (ss sigue siendo la canción entera) */
        uint32_t a = offsetof(fbx_song_t, hand), z = sizeof b->s - offsetof(fbx_song_t, edited);
        if (n + a + z + 1u > len) { resume_audio(b); return -1; }
        fg_copy(&b->s, buf + n, a); fg_copy(&b->s.edited, buf + n + a, z);
        fg_zero(b->s.hand, sizeof b->s.hand);
        n += a + z;
        for (unsigned p = 0; p < FBX_NP; p++) for (unsigned t = 0; t < 8; t++) if ((b->s.has_hand[p] >> t) & 1u) {
            if (n + 32u > len) { resume_audio(b); return -1; }
            fg_copy(b->s.hand[p][t], buf + n, 32); n += 32;
        }
        ss = 0;
    } else if (ver == 5u || ver == 4u) fg_copy(&b->s, buf + n, ss);
    else if (ver == 3u) {                                 /* 0.6: los motores 7..9 cambiaron (sus perillas quedan de fábrica) */
        static fbx_song_v3_t o;
        fbx_song_t *s = &b->s;
        fg_copy(&o, buf + n, ss);
        fg_copy(s->model, o.model, 8);
        for (unsigned t = 0; t < 8; t++) for (unsigned m = 0; m < 7; m++) { fg_copy(s->p[t][m], o.p[t][m], 8); fg_copy(s->edited[t][m], o.edited[t][m], 8); }
        for (unsigned t = 0; t < 8; t++) s->has_edited[t] = o.has_edited[t] & 0x7Fu;
        fg_copy(s->mix, o.mix, 32); fg_copy(s->fx, o.fx, 8); fg_copy(s->lfo, o.lfo, 32); fg_copy(s->azar, o.azar, 8);
        s->bpm = o.bpm; s->swing = o.swing; s->scale = o.scale; s->muteq = o.muteq; s->cur = o.cur; s->transp = o.transp;
        fg_copy(s->eu, o.eu, sizeof o.eu); fg_copy(s->has_hand, o.has_hand, 16); fg_copy(s->hand, o.hand, sizeof o.hand);
        fg_copy(s->crow, o.crow, 16); fg_copy(s->crep, o.crep, 16);
        s->chain_on = o.chain_on; s->reloj = o.mclk_in; s->rsv_clk = o.mclk_out; s->mnotes = o.mnotes;
        fg_copy(s->mix2, o.mix2, 32); for (unsigned t = 0; t < 8; t++) s->mix2[t][1] = 0;   /* (RESO pasó a ser ANCHO) */
        fg_copy(s->scl2, o.scl2, 32); fg_copy(s->lfo2, o.lfo2, 32); fg_copy(s->arp, o.arp, 32); s->arp_on = o.arp_on;
    } else read_old_song(b, buf + n, ver, np);
    song_sanitize(&b->s);
    n += ss;
    uint8_t nu = buf[n++];
    if (nu > FBX_UPRE || n + nu * sizeof b->upre[0] > len) { resume_audio(b); return -1; }
    fg_copy(b->upre, buf + n, nu * sizeof b->upre[0]); n += nu * sizeof b->upre[0];
    b->nupre = nu;
#define NEED(k) do { if (n + (k) > len) { resume_audio(b); return -1; } } while (0)
    for (unsigned p = 0; p < np; p++)
        for (unsigned t = 0; t < 8; t++) {
            static fg_track_t dummy;                      /* (patrones de más: se leen y se tiran) */
            fg_track_t *tr = p < FBX_NP ? &b->Q.pat[p].tr[t] : &dummy;
            if (ver >= 3u) { if (dec_track(tr, buf, len, &n, ver)) { resume_audio(b); return -1; } continue; }
            fg_seq_clear_track(tr);
            NEED(2);
            tr->len = buf[n] >= 1 && buf[n] <= 32 ? buf[n] : 16; tr->spd = buf[n + 1] <= 2 ? buf[n + 1] : 1; n += 2;
            for (;;) {                                    /* v1, v2: el paso entero */
                NEED(1);
                uint8_t si = buf[n++];
                if (si == 0xFF) break;
                if (si >= FG_SEQ_NS) { resume_audio(b); return -1; }
                fg_step_v2_t o;                           /* (su automatización tenía las 8 perillas: quedan las 2 primeras) */
                NEED(sizeof o); fg_copy(&o, buf + n, sizeof o); n += sizeof o;
                fg_step_t *st = &tr->st[si];
                st->v = o.v; st->c = o.c; st->p = o.p; st->r = o.r; st->u = o.u; fg_copy(st->lk, o.lk, 12);
                for (unsigned i = 0; i < 8; i++) if (o.au[i] != FG_NONE) au_set(st, i, o.au[i]);
            }
        }
#undef NEED
    if (ver == 1u) migrate_v1(b);
    if (ver < 5u) {
        remap_synths_v4(b, ver);
        unsigned k = 0;
        for (unsigned i = 0; i < b->nupre; i++) if (remap_upre_v4(&b->upre[i])) b->upre[k++] = b->upre[i];
        b->nupre = (uint8_t)k;
    }
    if (ver < 7u) migrate_v6(b);
    ub_merge(b);
    if (b->s.cur >= FBX_NP) b->s.cur = 0;
    b->Q.nchain = 0;
    unsigned nc = chain_expand(b);
    if (nc == 1) b->s.cur = b->chain_buf[0];
    push_all(b);
    if (nc > 1) fg_seq_chain(&b->Q, b->chain_buf, nc);
    b->a_cur = b->s.cur;
    resume_audio(b);
    clear_acc(b);
    return 0;
}

/* 0 = guardado; si no, ya dijo por qué en la pantalla */
static int save_project(fg_box_t *b, unsigned slot)
{
    if (!b->store_write) { show_ov(b, "GUARDAR", "SIN MEMORIA", 0); return -1; }
    if (!px_take_buf(slot < FBX_SLOTS)) return -1;       /* (el autoguardado espera a que la app termine) */
    uint32_t n = fg_box_pack(b, pack_buf, slot < FBX_SLOTS ? FBX_PROJ_MAX : FBX_AUTO_MAX);
    if (!n) { show_ov(b, "BORRA PASOS O PATRONES", "NO CABE", 0); b->retry_ms = b->now + 30000u; return -1; }
    if (b->store_write(slot, pack_buf, n)) { show_ov(b, "GUARDAR", "FALLÓ", 0); b->retry_ms = b->now + 30000u; return -1; }
    if (slot < FBX_SLOTS) b->slots |= (uint16_t)(1u << slot);
    if (slot == FBX_SLOTS) b->dirty = 0;
    return 0;
}
static void load_project(fg_box_t *b, unsigned slot)
{
    uint32_t n = 0;
    char v[24];
    px_take_buf(1);
    if (!b->store_read || b->store_read(slot, pack_buf, sizeof pack_buf, &n) || fg_box_unpack(b, pack_buf, n)) { show_ov(b, "ABRIR", "NO SE PUDO", 0); return; }
    if (slot < FBX_SLOTS) { v[0] = 0; scpy(v, "RANURA ", sizeof v); cati(v, (int)slot + 1, sizeof v); show_ov(b, "ABIERTO", v, 256); }
    mark(b);
    undo_reset(b);
}

void fg_box_boot(fg_box_t *b)
{
    uint32_t n = 0;
    fg_usr_init(b->usr_io);
    if (b->store_has) for (unsigned i = 0; i < FBX_SLOTS; i++) if (b->store_has(i)) b->slots |= (uint16_t)(1u << i);
    dev_load(b);
    ub_load(b);
    if (b->store_read && !b->store_read(FBX_SLOTS, pack_buf, sizeof pack_buf, &n)) fg_box_unpack(b, pack_buf, n);
    b->dirty = 0;
    undo_reset(b);
    b->ov.until = 0;
    b->intro = 1; b->intro_t0 = b->now; b->intro_k = 0;
}

void fg_box_note(fg_box_t *b, const char *label, const char *value)   /* después del arranque, unos segundos */
{
    show_ov(b, label, value, 0);
    b->ov.until = b->now + 2800u + 4000u;
}

void fg_box_idle(fg_box_t *b)
{
    px_tick(b);                                          /* la app: avisos y traslados que se quedaron abiertos */
    /* deshacer: una ráfaga de cambios que lleva 0.7 s quieta queda como un estado */
    if (b->u_dirty && b->now - b->dirty_ms > 700u && !b->down && !b->wdown && !b->bdown) undo_commit(b);
    /* autoguardado: parado, con cambios, y 3 s sin tocar nada */
    /* (si una escritura falló, se espera retry_ms: reintentar en cada vuelta borraría la flash sin parar) */
    if ((int32_t)(b->now - b->retry_ms) < 0 || !b->store_write) return;
    if (b->dirty && !b->playing && b->now - b->dirty_ms > 3000u && !b->down && !b->wdown && !b->bdown) save_project(b, FBX_SLOTS);
    if (ub_dirty && !b->playing && !b->down && !b->wdown && !b->bdown) ub_flush(b);   /* un sonido guardado tocando: al parar */
    if (dev_dirty && !b->playing && !b->down && !b->wdown && !b->bdown && b->now - b->dirty_ms > 1000u) dev_flush(b);
}

#include "fg_box_proto.h"
