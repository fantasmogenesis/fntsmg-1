/* La caja: el groovebox entero del FM-1 en C portable (puerto de caja/index.html + caja/worklet.js).
 * Dos lados, como la página y su worklet:
 *   audio     fg_box_audio() en el callback de audio: secuenciador, 8 motores, mezcla. Los sintes pueden ir en
 *             otro núcleo (fg_core1_run/wait, los da la plataforma).
 *   interfaz  fg_box_button/key/enc/tick en el loop principal; fg_box_leds() y fg_box_frame()/fg_box_band()
 *             para las luces y la pantalla.
 * La interfaz le habla al audio por una cola de mensajes (como postMessage) y el audio le contesta con golpes,
 * posiciones y niveles. Los patrones viven una sola vez (en el secuenciador) y la interfaz los edita en su lugar:
 * un paso es un byte, y lo que mueve muchos bytes (copiar, cargar) pausa el audio un bloque.
 * La plataforma da: fg_core1_run/wait (otro núcleo o nada), y en fg_box_t las funciones de guardado y el reloj. */
#pragma once
#include <stdint.h>
#include "fg_kick.h"
#include "fg_snare.h"
#include "fg_metal.h"
#include "fg_perc.h"
#include "fg_synth.h"
#include "fg_mix.h"
#include "fg_seq.h"
#include "fg_vis.h"
#include "fg_usr.h"

/* botones, en el orden de las etiquetas del FM-1 (el mismo que Felucca panel.c) */
enum { FB_FX, FB_SCL, FB_ENV, FB_LFO, FB_EDIT, FB_GLO, FB_HOME, FB_SAVE, FB_ARP, FB_SEQ, FB_PLAY, FB_REC,
       FB_OCTDN, FB_OCTUP, FB_NBTN };
/* perillas: las tres de arriba y las cuatro de sonido */
enum { FE_SELECT, FE_ALGO, FE_PRESET, FE_K1, FE_K2, FE_K3, FE_K4, FE_NENC };
#define FBX_KEYS 27                         /* teclas F3..G5: 16 blancas (pasos) y 11 negras */
#define FBX_SLOTS 16                        /* ranuras de proyecto (SAVE + blanca) */
#define FBX_UPRE 32                         /* sonidos guardados dentro de un proyecto (los de antes: al abrirlo pasan al banco) */
#define FBX_UBANK 96                        /* el banco de sonidos del usuario: uno para todos los proyectos (más no cabe en la RAM con 8 pistas USB) */
#define FBX_UBANK_SLOT (FBX_SLOTS + 1)      /* su ranura en la memoria */
#define FBX_DEV_SLOT (FBX_SLOTS + 2)        /* los ajustes del aparato (idioma): aparte de los proyectos */
#define FBX_PROJ_MAX 8180u                  /* lo que cabe en una ranura de proyecto (8 KB menos la cabecera) */
#define FBX_AUTO_MAX 16372u                 /* el autoguardado (16 KB): ahí sí cabe un proyecto que no cupo en una ranura */
#define FBX_NP FG_SEQ_NP
#define FBX_NM FG_SEQ_NM                    /* motores por pista (los sintes tienen 10) */

enum { PG_EDIT, PG_ENV, PG_FX, PG_SCL, PG_LFO, PG_GLO, PG_CHAIN };
enum { BK_MUTE = 8, BK_FILL = 9, BK_COPY = 10 };   /* las tres negras de arriba: sostenerlas cambia lo que hacen las otras */

typedef struct { uint8_t type, t, a, b; int32_t v; } fbx_msg_t;
typedef struct { uint8_t t, m, vel, p[8]; } fbx_hit_t;
typedef struct { uint8_t key, m; char name[11]; uint8_t p[8]; } fbx_upre_t;   /* key = pista; m = modelo */
/* arpegiador de un sinte (lado del audio): las teclas sostenidas y por dónde va */
typedef struct {
    uint8_t on, cfg[4], n, keys[16], lks[16], pos, up, held;
    uint32_t next, lastk;
    int32_t rx;                             /* REC: el tick de la última nota grabada (una por paso) */
} fbx_arp_t;
#define FBX_UNDO_N 6                        /* deshacer: estados guardados (sonido de las pistas + el patrón) */
#define FBX_UNDO_SZ 2048

/* el proyecto: todo lo que se guarda (los patrones están en el secuenciador) */
typedef struct {
    uint8_t model[8], p[8][FBX_NM][8], mix[8][4], fx[8], lfo[8][4], azar[8];
    uint8_t bpm, swing, scale, muteq, cur;
    int8_t transp;
    uint8_t eu[FBX_NP][8][2];               /* euclidiano: golpes, rotar */
    uint8_t has_hand[FBX_NP];               /* bit t: hay pasos a mano guardados para GOLPES en 0 */
    uint8_t hand[FBX_NP][8][32];
    uint8_t edited[8][FBX_NM][8];           /* PRESETS: tu versión, primera de la lista */
    uint16_t has_edited[8];
    uint8_t crow[16], crep[16], chain_on;   /* CADENA: patrón de cada fila (255 = vacía), veces, si suena */
    uint8_t reloj, rsv_clk, mnotes;         /* MIDI: RELOJ 0 no, 1 entra, 2 sale, 3 entra y sale (hasta la v6: entra y sale en dos bytes); notas 0 no, 1 entran, 2 salen, 3 las dos */
    uint8_t mix2[8][4];                     /* ENV pág. 2: FILTRO (64 abierto: abajo graves, arriba agudos), RESO, SATURA, SIDECHAIN */
    uint8_t scl2[8][4];                     /* SCL pág. 2: (MOVER no se guarda), TRANSPONER (64 = 0), HUMANO, DENSIDAD */
    uint8_t lfo2[8][4];                     /* LFO pág. 2: el segundo LFO (destino, velocidad, profundidad, forma) */
    uint8_t arp[8][4], arp_on;              /* ARP: modo, velocidad, octavas, sostener; bit t: prendido */
} fbx_song_t;

typedef struct {
    char label[44], value[28];
    int16_t bar;                            /* 0..256, −1 sin barra */
    uint32_t until;
} fbx_ov_t;

typedef struct fg_box {
    /* ---------------- audio ---------------- */
    fg_kick_t K; fg_snare_t SN; fg_metal_t MT; fg_perc_t PC; fg_synth_t SY[4];
    fg_mix_t MX;
    fg_seq_t Q;
    fg_seq_ev_t ev[96];
    int nev;
    int32_t trk[8][FG_BLOCK], outl[FG_BLOCK], outr[FG_BLOCK];
    uint32_t blocks, perf_seq;
    volatile uint8_t pause_req, pause_ack;
    /* cola interfaz -> audio */
    fbx_msg_t mq[192];
    volatile uint16_t mq_w, mq_r;
    uint8_t chain_buf[FG_SEQ_CHAIN];
    /* audio -> interfaz */
    fbx_hit_t hq[32];
    volatile uint16_t hq_w, hq_r;
    volatile int16_t pos[8];
    volatile uint8_t a_cur, a_queued, a_chainpos, a_mute, a_solo, a_playing;
    volatile int32_t level[8];
    /* reloj MIDI de salida: pulsos de 24 ppqn que la plataforma manda (y arranque/parada) */
    /* MIDI de salida (notas): paquetes USB-MIDI que la plataforma manda desde el callback de audio */
    uint32_t mo[64];
    volatile uint16_t mo_w, mo_r;
    uint8_t off_note[8], mclk_out, mnotes_out;   /* (copias del lado del audio de la configuración MIDI) */
    int32_t off_left[8];
    volatile uint16_t clk_pulses;
    volatile uint8_t clk_start, clk_stop;
    uint32_t clk_acc;
    /* reloj MIDI de entrada (lado del audio: fg_box_clock): tempo medido y fase amarrada en cada corchea */
    uint8_t ck_in, ck_on, ck_have;
    volatile uint8_t a_ext_play, a_bpm;     /* para la interfaz: 1 arrancó / 2 paró; el tempo de fuera */
    uint32_t ck_n, ck_cnt, ck_avg;          /* pulsos desde el arranque; medidos; intervalo promedio (muestras, Q8) */
    int32_t ck_last;
    volatile int32_t ck_err;                /* el último desfase corregido (muestras; + = íbamos tarde) */
    volatile uint32_t ck_fix, ck_snap;
    fbx_arp_t ar[4];
    fgy_eco_t eco;                          /* el eco de cinta de DUB: de la primera pista de sinte que lo use */
    int32_t apos;                           /* el arpegiador: dónde va (ticks Q8) */
    uint8_t aplay;
    uint8_t arec;                           /* el arpegio grabó notas (el audio avisa; la interfaz marca cambios) */
    /* notas sostenidas: las que se sueltan al final del bloque (después de que sonó su golpe) */
    uint8_t noff[16][2], n_noff, maxv;
    uint32_t v_blk;
    uint32_t blk_ticks;                     /* lo que dura un bloque en unidades de fg_now (0: no se mide; 4 voces siempre) */
    /* cuánto tarda cada parte (unidades de fg_now) */
    uint32_t t_core0, t_core1, t_wait, t_total, t_max, t_core0_max, t_core1_max;
    uint32_t t_avg, t_n, late;

    /* ---------------- interfaz ---------------- */
    fbx_song_t s;
    uint8_t sel, page, pn, seqmode, rec, sp, playing, chainpos;
    int8_t oct;
    uint8_t mute, solo, pend, has_pend;     /* máscaras por pista; pend: lo que quedará al compás */
    uint16_t down;                          /* botones sostenidos */
    uint16_t wdown, bdown;                  /* blancas y negras sostenidas */
    uint8_t hw[16], nhw;                    /* blancas sostenidas, en orden */
    uint8_t kt[16];                         /* TOCAR: la pista que sonó cada blanca (para soltarla en la misma) */
    uint16_t used_btn, used_w;              /* ya se usó como modificador: al soltar no hace lo suyo */
    uint8_t slot_arm;                       /* SAVE + blanca sostenida en una ranura llena: 1 + ranura (0 = nada) */
    uint32_t slot_ms;
    uint16_t perf, latch;                   /* FX de performance (por blanca) */
    uint8_t perf_last, pk[16][2];           /* el último efecto tocado y las perillas de cada uno (K1, K2; 64 = como siempre) */
    uint16_t kmov[4];                       /* cuánto se acaban de mover K1..K4 (0..256): el visual lo siente */
    uint8_t home_list[16], nhome;
    int8_t copy_kind, copy_idx;             /* 0 nada, 1 patrón, 2 paso, 3 pista */
    uint8_t fx_used, saved_preset, just_on, queued;
    uint32_t tap_btn[FB_NBTN], tap_step[32], tap_slot[FBX_SLOTS];
    uint32_t mix_t[8];                      /* ENV: blanca de pista apretada (mantener = solo) */
    uint8_t mix_armed;
    uint8_t cursor;
    uint8_t preset_idx[8][FBX_NM];
    fbx_upre_t upre[FBX_UPRE];
    uint8_t nupre;
    int32_t acc[4];                         /* perillas: valor fraccionario Q8 en su contexto */
    uint32_t acc_key[4], enc_ms[FE_NENC];
    fbx_ov_t ov;
    char names[4][16];
    uint32_t names_until, big_until, msg_until;
    char msg[28];
    uint32_t now;                           /* ms */
    uint8_t dirty, save_slot_req, load_slot_req;
    uint16_t slots;                         /* ranuras con proyecto guardado */
    uint8_t pg_before, pn_before;           /* la página antes de apretar SEL (SEL + EDIT / SEQ regresa a ella) */
    uint8_t crow_cur, prev_page;            /* CADENA: la fila elegida; la página a la que regresa HOME */
    uint32_t press_ms[FB_NBTN];             /* cuándo se apretó cada botón (mantener 1 s) */
    uint16_t long_done;                     /* ya hizo lo de mantener */
    uint32_t rtrk_sum;                      /* SEL + SEQ: la huella del último patrón al azar y el largo de la pista antes de él */
    uint8_t rtrk_t, rtrk_len;               /* (si la pista sigue igual, el siguiente parte de ese largo y no del que salió) */
    uint32_t rng;
    uint32_t dirty_ms;
    uint32_t retry_ms;                      /* una escritura falló: no reintentar antes de esto (cada intento borra la flash) */
    /* deshacer / rehacer: cada ráfaga de cambios que se asienta deja un estado; SAVE + OCT− / OCT+ los recorre */
    uint8_t undo[FBX_UNDO_N][FBX_UNDO_SZ];
    uint16_t u_len[FBX_UNDO_N];
    uint8_t u_n, u_i, u_dirty;
    /* pantalla */
    uint32_t intro_t0;                      /* el arranque: FNTSMG-1 (fg_box_draw.c), ~2 s o hasta tocar algo */
    uint8_t intro, intro_k;
    fg_vis_t vis;
    fg_vis_ui_t vui;
    uint8_t view;
    uint32_t band_sum[16];                  /* lo último mandado de cada banda: si no cambió, no se manda */
    /* espejo: los golpes también van a una cola que la plataforma manda por USB (fg_box_mirror) */
    fbx_hit_t mh[16];
    uint8_t mh_w, mh_r, mirror;

    /* ---------------- plataforma ---------------- */
    /* guardado: ranura 0..FBX_SLOTS-1, FBX_SLOTS = autoguardado, FBX_UBANK_SLOT = banco de sonidos, FBX_DEV_SLOT = ajustes del aparato. Devuelven 0 si salió bien */
    int (*store_write)(unsigned slot, const void *data, uint32_t len);
    int (*store_read)(unsigned slot, void *data, uint32_t cap, uint32_t *len);
    int (*store_has)(unsigned slot);
    int (*store_info)(unsigned slot, uint32_t *len, uint32_t *crc);   /* (la app: LISTA de ranuras; 0 = vacía) */
    uint32_t (*ms)(void);
    /* la app (PROTOCOLO.md, fg_box_proto.h): por dónde salen las respuestas (F0..F7), la flash de TUYOS y la versión */
    int (*sysex_out)(const uint8_t *p, unsigned n);
    const fg_usr_io_t *usr_io;
    const char *version;
} fg_box_t;

/* la plataforma: otro núcleo para los sintes (1 = lo tomó) y un reloj para medir */
int fg_core1_run(void (*fn)(uint32_t), uint32_t arg);
int fg_core1_wait(void);
uint32_t fg_now(void);

void fg_box_init(fg_box_t *b, void *vis_mem, unsigned vis_w, unsigned vis_h, unsigned vis_hist, uint32_t seed);
/* audio: n <= FG_BLOCK cuadros estéreo intercalados en rango int16 */
void fg_box_audio(fg_box_t *b, int32_t *out, unsigned n);
/* interfaz */
void fg_box_button(fg_box_t *b, unsigned id, int down);
void fg_box_key(fg_box_t *b, unsigned k, int down);
void fg_box_enc(fg_box_t *b, unsigned role, int steps);
void fg_box_tick(fg_box_t *b, uint32_t now_ms);
void fg_box_leds(fg_box_t *b, uint32_t *buttons, uint32_t *keys);
uint32_t fg_box_leds_half(const fg_box_t *b);   /* botones de página a media luz (los que no están elegidos) */
/* audio, antes de fg_box_audio: un byte de reloj (F8 FA FB FC) que llegó age muestras antes de este bloque */
void fg_box_clock(fg_box_t *b, uint8_t byte, uint32_t age);
/* canal 1..8 = pista 1..8: notas; CC 16–23 las 8 perillas del motor, 7 volumen, 10 paneo, 12 delay, 91 reverb;
 * program change 0..7 = patrón al terminar el que suena */
void fg_box_midi_in(fg_box_t *b, uint8_t status, uint8_t d1, uint8_t d2);
int fg_box_midi_out(fg_box_t *b, uint32_t *pkt);
int fg_box_chain_row(const fg_box_t *b);     /* la fila de la CADENA que suena (−1 si no suena una cadena) */   /* audio: el siguiente paquete USB-MIDI a mandar (1 = hay) */
/* pantalla: un cuadro, luego las bandas de 16 filas (y0 = 0, 16, …). px: 240×16 RGB565 con los bytes al revés
 * (como los pide el ST7789). Devuelve 0 si la banda es igual a la última que se mandó */
void fg_box_frame(fg_box_t *b);
/* espejo del visual: el siguiente mensaje SysEx (F0..F7) en buf; 0 si no hay. Golpes primero, luego el estado
 * del cuadro (state = 1: pedirlo). El formato lo lee espejo/index.html */
unsigned fg_box_mirror(fg_box_t *b, uint8_t *buf, int state);
int fg_box_band(fg_box_t *b, unsigned y0, uint16_t *px);
/* proyecto en bytes (para la flash): devuelve el largo, 0 si no cabe */
uint32_t fg_box_pack(fg_box_t *b, uint8_t *buf, uint32_t cap);
int fg_box_unpack(fg_box_t *b, const uint8_t *buf, uint32_t len);
/* al encender: abre el autoguardado si hay */
void fg_box_boot(fg_box_t *b);
void fg_box_note(fg_box_t *b, const char *label, const char *value);
/* la app: un mensaje que llegó (desde cmd, después de F0 7D 46 47, sin F7); qué escucha (bit 0 espejo, bit 1 avisos) */
void fg_box_sysex(fg_box_t *b, const uint8_t *m, unsigned n);
unsigned fg_box_listening(const fg_box_t *b);
/* el loop principal llama esto cuando no hay nada más que hacer: guarda si toca */
void fg_box_idle(fg_box_t *b);
