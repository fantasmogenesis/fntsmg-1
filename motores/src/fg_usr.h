/* Los samples del usuario (TUYOS) en la flash: un sector de índice y los datos detrás, en sectores de 4 KB.
 * El índice es un registro: cada entrada se escribe completa con su marca al final, y una entrada nueva para el
 * mismo lugar gana sobre la anterior (que después se anula). Un corte a medias deja el sample de antes o el nuevo,
 * nunca uno roto. Cuando el registro se llena, se reescribe con lo que sigue vivo.
 * La plataforma da la flash (fg_usr_io_t); los datos se tocan desde ahí con ptr() (XIP en el FM-1). */
#pragma once
#include <stdint.h>
#include "fg_samp.h"

typedef struct {
    int (*rd)(uint32_t off, void *d, uint32_t n);
    int (*er)(uint32_t off);                       /* borra el sector de 4 KB que empieza en off */
    int (*pr)(uint32_t off, const void *s, uint32_t n);
    const uint8_t *(*ptr)(uint32_t off);
    uint32_t base, size;                           /* la zona: el índice en base, los datos desde base + 4096 */
} fg_usr_io_t;

typedef struct {
    uint32_t magic;                                /* FGU_ENT vivo, 0 anulado, 0xFFFFFFFF libre (el final del registro) */
    uint8_t k, root, loop, rsv;
    uint32_t off, bytes, crc, len, rate, loop_s, loop_e;   /* off: desde el principio de los datos */
    char name[12];
} fg_usr_ent_t;                                    /* 48 bytes */

enum { FGU_OK, FGU_FUERA = -1, FGU_OCUPADO = -2, FGU_NO_CABE = -3, FGU_CRC = -4, FGU_FLASH = -5, FGU_FORMATO = -6, FGU_VACIO = -7 };

void fg_usr_init(const fg_usr_io_t *io);           /* lee el índice y llena fg_usr[] */
uint32_t fg_usr_total(void);                       /* bytes para datos */
uint32_t fg_usr_free(void);
uint32_t fg_usr_gen(void);                         /* cambia con cada sample escrito o borrado */
const fg_usr_ent_t *fg_usr_entry(unsigned k);      /* 0 si ese lugar está vacío */
/* escribir uno: begin aparta el espacio (si no cabe: FGU_NO_CABE y *room = el hueco más grande), data va en orden,
 * end revisa el CRC y escribe la entrada; end(0) cancela */
int fg_usr_begin(const fg_usr_ent_t *e, uint32_t *room);
int fg_usr_data(uint32_t from, const uint8_t *d, uint32_t n);
int fg_usr_end(int apply);
int fg_usr_delete(unsigned k);
int fg_usr_read(unsigned k, uint32_t from, uint8_t *d, uint32_t n);
