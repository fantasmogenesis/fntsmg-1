# Protocolo SysEx del FNTSMG-1 (companion)

Contrato entre el firmware FNTSMG-1 y la app companion (`estacion/`). La app solo habla con el aparato por lo que dice este documento. Si necesita algo que aquí no está, se pide un cambio a este documento antes de programarlo.

- **Versión del protocolo:** 1, revisión 4 (30-oct-2026). Es la que implementa el firmware; los cambios contra la revisión 1 están en el Historial, al final.
- **Firmware que lo implementa:** 0.9, en la rama `v0.9` (sin instalar todavía). El 0.8.7 de la beta solo tiene el espejo (sección 9).
- **Dueño:** la sesión del firmware (`motores/src/fg_box.c`, `vendor/Felucca/firmware/src/fgbox.c`). La sesión de la companion no toca firmware.
- **Cuándo cambia el número de versión:** un comando nuevo no lo cambia, porque la app lo descubre con `INFO`. Cambiar el significado de un comando que ya existe sí lo sube.

---

## 1. Forma de los mensajes

Todo viaja por USB-MIDI como SysEx con la cabecera del espejo:

```
F0 7D 46 47 cmd id datos… F7
```

- `7D` es el ID no comercial. `46 47` son las letras «FG».
- `cmd` dice qué comando es (sección 4). Los tipos 1 y 2 ya los usa el espejo. Los comandos de la companion empiezan en `0x10`.
- `id` es un número de 1 a 127 que pone la app y que el aparato repite en la respuesta. Así la app sabe qué respuesta es de qué pregunta. Los avisos que el aparato manda por su cuenta llevan `id = 0`, así que la app no usa el 0.
- Entre `F0` y `F7` todos los bytes tienen el bit 7 en cero.

### Respuesta

Cada pregunta de la app tiene exactamente una respuesta, con el mismo `cmd` y el mismo `id`:

```
F0 7D 46 47 cmd id estado datos… F7
```

`estado` es 0 si salió bien. Si no, es un código de la sección 3, y en ese caso los datos pueden venir vacíos.

### Números

| Nombre | Bytes | Qué es |
|---|---|---|
| `u7` | 1 | 0..127 |
| `u14` | 2 | 0..16383, primero los 7 bits bajos |
| `s14` | 2 | −8192..8191: se manda `valor + 8192` como `u14` (igual que el editor de Felucca) |
| `u21` | 3 | 0..2 097 151, primero los 7 bits bajos |
| `u32` | 5 | 32 bits (CRC), primero los 7 bits bajos |

### Bloques de bytes (empaque 7 en 8)

Lo que no son números sueltos (pasos, nombres, trozos de un archivo) va empacado en el mismo formato que usa Felucca (`ed_unpack7`):

- Cada grupo de hasta 7 bytes se manda como 1 byte con los bits altos y luego los 7 bytes sin su bit alto.
- En el byte de bits altos, el bit `j` es el bit 7 del byte `j` del grupo.
- El último grupo puede ser más corto. Los bits del byte de bits altos que no se usan van en cero; si no, el aparato rechaza el mensaje.
- 512 bytes empacados ocupan 586.

### Tamaño

- Un mensaje que entra al aparato puede tener hasta **640 bytes** entre `F0` y `F7` (el búfer de Felucca). Uno más largo se descarta sin respuesta.
- El aparato procesa un mensaje a la vez. Si llega otro mientras procesa, se pierde, así que la app espera la respuesta antes de mandar el siguiente.
- `INFO` dice el trozo máximo de datos por mensaje (`trozo`, hoy 512 bytes).

### Integridad

El CRC es CRC-32 estándar (el de zlib, polinomio `0xEDB88320`), el mismo con el que el aparato guarda las ranuras.

---

## 2. Reglas de la plática

1. **Primero `INFO`.** La app pregunta `INFO` al conectarse y se ajusta a lo que dice: cuántos patrones, pasos, ranuras, espacio para samples y versión.
2. **Esperar respuesta.** Si no llega en 1 s, la app repite con el mismo `id`. Si la pregunta que llega tiene el mismo comando y el mismo `id` que la anterior, el aparato no la aplica otra vez: repite su última respuesta. Por eso la app cambia de `id` en cada pregunta nueva (también en cada trozo de un traslado).
3. **`ESCUCHA` para los avisos.** El aparato solo manda avisos (`CAMBIO`, el espejo) mientras la app pida `ESCUCHA` al menos cada 2 s. Si deja de pedir, a los 3 s el aparato se calla.
4. **Un traslado a la vez.** Leer o escribir algo grande (proyecto, banco, ranura, sample) se hace en un traslado: `ABRIR`, trozos, `CERRAR`. Mientras hay uno abierto, otro `ABRIR` responde `OCUPADO`. Si pasan 5 s sin trozos, el aparato lo cierra solo sin aplicar nada. El traslado usa el mismo espacio de memoria con el que el aparato guarda: mientras está abierto, el autoguardado espera; si la persona guarda o abre una ranura en el aparato, el traslado se cancela y el siguiente trozo responde `SIN_TRASLADO`.
5. **La flash se escribe parado.** Borrar la flash detiene un momento el código que corre desde ella, así que tocando se oiría un corte. Escribir una ranura, el banco o un sample con el aparato tocando responde `TOCANDO`. La app le pide a la persona que pare, o manda `TRANSPORTE` parar.
6. **El aparato manda.** Lo que la app escribe pasa por el mismo camino que la perilla o el botón del aparato: se acota igual, se puede deshacer igual y se autoguarda igual. Si el aparato acota un valor, la respuesta trae el valor que quedó.
7. **Modo seguro.** Si el aparato arrancó en modo seguro (un arranque anterior falló), no escribe nada en la flash: toda escritura responde `SIN_FLASH`. Leer sí se puede, y así la app rescata los proyectos antes de reinstalar.

---

## 3. Estados

| Código | Nombre | Cuándo |
|---|---|---|
| 0 | `BIEN` | |
| 1 | `NO_ENTIENDO` | El comando no existe en este firmware o el mensaje está mal formado |
| 2 | `FUERA` | Un índice fuera de rango (pista, patrón, paso, ranura, sample) |
| 3 | `OCUPADO` | Ya hay un traslado abierto, o el aparato está a media operación |
| 4 | `TOCANDO` | Hay que parar para hacer esto (sección 2, regla 5) |
| 5 | `NO_CABE` | No hay espacio; la respuesta trae cuánto cabe (`u21`) |
| 6 | `CRC` | Lo recibido no coincide con el CRC anunciado |
| 7 | `SIN_TRASLADO` | Un trozo o un `CERRAR` sin `ABRIR`, o con otro objeto |
| 8 | `FALLO_FLASH` | La flash no aceptó la escritura |
| 9 | `SIN_FLASH` | Modo seguro: no se escribe |
| 10 | `FORMATO` | Un proyecto o banco que el aparato no sabe abrir (versión, tamaño) |
| 11 | `VACIO` | Se pidió leer algo que no existe (ranura o sample vacíos) |

---

## 4. Comandos

| cmd | Nombre | Dirección |
|---|---|---|
| 0x01 | `GOLPE` (espejo) | aparato → app |
| 0x02 | `ESTADO` (espejo) | aparato → app |
| 0x10 | `INFO` | app → aparato |
| 0x11 | `ESCUCHA` | app → aparato |
| 0x12 | `CAMBIO` | aparato → app (aviso) |
| 0x13 | `META` | app → aparato |
| 0x20 | `ABRIR` | app → aparato |
| 0x21 | `LEER` | app → aparato |
| 0x22 | `ESCRIBIR` | app → aparato |
| 0x23 | `CERRAR` | app → aparato |
| 0x24 | `BORRAR` | app → aparato |
| 0x25 | `LISTA` | app → aparato |
| 0x30 | `PERILLA` | app → aparato |
| 0x31 | `PERILLAS` | app → aparato |
| 0x32 | `PASO` | app → aparato |
| 0x33 | `PISTA` | app → aparato |
| 0x34 | `TRANSPORTE` | app → aparato |
| 0x35 | `NOMBRE` | app → aparato |
| 0x40 | `AJUSTE` | app → aparato |

### 0x10 INFO

Pregunta: sin datos.

Respuesta:

| Campo | Tipo | Hoy |
|---|---|---|
| protocolo | `u7` | 1 |
| formato del proyecto | `u7` | 7 |
| patrones | `u7` | 16 |
| pistas | `u7` | 8 |
| pasos por pista | `u7` | 32 |
| ranuras de proyecto | `u7` | 16 |
| tamaño de ranura | `u21` | bytes de proyecto que caben en una ranura |
| sonidos en el banco | `u7` | máximo 96 |
| trozo máximo | `u14` | 512 |
| zona de samples | `u21` | bytes en total |
| zona de samples libre | `u21` | bytes libres |
| idioma | `u7` | 0 español, 1 English, 2 português |
| contador de cambios | `u14` | el mismo de `CAMBIO` |
| banderas | `u7` | bit 0 modo seguro, bit 1 tocando |
| versión | texto empacado | `"FNTSMG-1 0.9"` (UTF-8, sin cero al final) |

### 0x11 ESCUCHA

Pregunta: `qué` (`u7`).

- bit 0: el espejo (golpes y estado de cada cuadro, sección 9).
- bit 1: los avisos `CAMBIO`.
- 0 deja de escuchar.

Hay que repetirlo cada 2 s o menos. El CC 119 en el canal 16 que usa la página del espejo sigue funcionando igual.

Respuesta: sin datos.

### 0x12 CAMBIO (aviso)

El aparato lo manda cuando algo cambió por su lado: la persona movió una perilla, editó pasos, cargó un proyecto o deshizo. Lo manda como mucho una vez cada 100 ms, y junta en uno los cambios de ese rato.

| Campo | Tipo | Qué es |
|---|---|---|
| contador | `u14` | sube con cada cambio (al llegar a 16383 vuelve a 0) |
| qué | `u14` | máscara, abajo |
| patrón | `u7` | el patrón con pasos tocados; 127 si fueron varios o ninguno |
| pistas | `u7` | máscara de pistas con pasos tocados en ese patrón |

Máscara `qué`:

| bit | Cambió |
|---|---|
| 0 | perillas de sonido, mezcla, LFO, ARP, AZAR o modelos |
| 1 | pasos o largo de pista |
| 2 | patrón que suena, cola, cadena, mutes, solos, transporte |
| 3 | el proyecto entero (cargar, nuevo, deshacer): hay que volver a leerlo |
| 4 | el banco de sonidos |
| 5 | las ranuras de proyecto |
| 6 | los samples del usuario |
| 7 | los ajustes del aparato (idioma) |

Lo que cambia la app también sube el contador, y la respuesta a cada escritura trae el contador nuevo. Así la app distingue sus propios cambios de los del aparato: si el contador de un `CAMBIO` es el mismo que el de su última respuesta, no hay nada que releer.

Qué hacer con cada bit:

- bit 3: volver a leer el proyecto completo.
- bit 1: `PISTA` leer de las pistas que dice `pistas`.
- bit 0: `PERILLAS` de lo que la app tenga a la vista.

### 0x13 META

Los nombres de las cosas, en el idioma del aparato, sacados de las mismas tablas que dibuja la pantalla. La app arma su interfaz con esto y no copia las tablas a mano.

Pregunta: `tipo` (`u7`) y luego argumentos.

| tipo | Argumentos (`u7` cada uno, salvo el valor) | Respuesta |
|---|---|---|
| 0 pistas | | 8 textos |
| 1 modelos | pista | cuántos (1 byte) y sus nombres |
| 2 perillas | pista, página, subpágina | 4 grupos: nombre, mínimo y máximo (`int16` little endian cada uno). Nombre vacío: esa perilla no hace nada ahí (mínimo y máximo en 0) |
| 3 valores | pista, página, subpágina, perilla, valor `s14` | el texto que mostraría la pantalla para ese valor («1/16», «MENOR», «−3 ST») |
| 4 presets | pista, modelo | cuántos (1 byte) y sus nombres, en el orden de la perilla PRESET: EDITADO (si hay), INICIAL, los de fábrica, en SAMPLE todos los samples (de fábrica y TUYOS, en orden de lugar), los del banco |
| 5 samples | banco (0 BOMBOS, 1 CAJAS, 2 TEXTURAS, 3 TUYOS) | cuántos (1 byte) y, por sample, su lugar (1 byte: el valor de la perilla SAMPLE) y su nombre |
| 6 listas | 0 condiciones, 1 escalas, 2 formas de LFO, 3 destinos de LFO (+ pista), 4 efectos de performance | cuántos (1 byte) y sus nombres |

La respuesta entera es un solo bloque empacado (7 en 8). Adentro, los textos van en UTF-8, cada uno terminado en 0, y los números en binario como dice la tabla. Las páginas son las de `PERILLA`; la 7 (ARP) solo tiene perillas en los sintes (pistas 4..7).

La regla aprobada sigue: los nombres de motores y de presets no se traducen, en ningún idioma.

### 0x20 ABRIR

Abre un traslado de un objeto (sección 5).

Pregunta:

| Campo | Tipo |
|---|---|
| objeto | `u7` |
| índice | `u7` (ranura o sample; 0 si no aplica) |
| modo | `u7`: 0 leer, 1 escribir |
| largo | `u21` (solo al escribir) |
| CRC | `u32` (solo al escribir) |
| extra | según el objeto (solo samples al escribir, sección 8) |
| nombre | texto empacado, al final (solo samples al escribir) |

Respuesta al leer: `largo` (`u21`) y `CRC` (`u32`).

- Al abrir para leer, el aparato saca una foto del objeto. Los trozos se leen de esa foto, aunque la persona siga editando. Así lo que llega es consistente.

Respuesta al escribir: sin datos, o `NO_CABE` con lo que cabe.

- Para un sample, el aparato aparta el espacio al abrir.

### 0x21 LEER

Pregunta: `desde` (`u21`) y `cuántos` (`u14`, hasta el trozo máximo).

Respuesta: `desde` (`u21`) y el trozo empacado.

### 0x22 ESCRIBIR

Pregunta: `desde` (`u21`) y el trozo empacado.

Los trozos van en orden y sin huecos: `desde` tiene que ser lo que ya llegó. Si no lo es, la respuesta es `FUERA` y trae lo que ya llegó (`u21`), para que la app siga desde ahí.

Respuesta: `llegó` (`u21`), el total recibido.

### 0x23 CERRAR

Pregunta: `aplicar` (`u7`). 1 aplica lo escrito; 0 cancela, y es la manera normal de terminar una lectura.

Al escribir, el aparato revisa el largo y el CRC y aplica:

- **proyecto:** lo abre como si se cargara de una ranura;
- **banco:** lo reemplaza;
- **ranura:** la escribe en la flash;
- **sample:** escribe su entrada en el índice al final, así que un corte a medias no deja un sample roto.

Respuesta: contador de cambios (`u14`).

### 0x24 BORRAR

Pregunta: `objeto`, `índice`.

Sirve para ranuras, sonidos del banco (índice = lugar en el banco) y samples del usuario (con la flash, solo parado).

Al borrar un sample, lo que estaba sonando de TUYOS se calla. El proyecto guarda el número del lugar. Mientras ese lugar esté vacío, la pista toca el sample más cercano hacia abajo que sí exista: es la misma regla que el aparato usa cuando AZAR o un lock caen en un lugar vacío. Si el lugar se vuelve a llenar, suena el nuevo.

Respuesta: contador de cambios.

### 0x25 LISTA

Pregunta: `objeto`.

Respuesta para ranuras: `cuántas` (`u7`, hoy 17) y, por ranura, `largo` (`u21`: bytes del proyecto, 0 = vacía) y `CRC` (`u32`). La ranura 16 es el autoguardado.

Respuesta para samples: `libre` (`u21`), `cuántos` (`u7`) y, por sample:

| Campo | Tipo |
|---|---|
| índice | `u7` |
| largo en muestras | `u21` |
| bytes | `u21` |
| CRC | `u32` |
| tasa | `u21` |
| raíz MIDI | `u7` |
| lazo | `u7` |
| inicio del lazo | `u21` |
| fin del lazo | `u21` |
| largo del nombre | `u7` (bytes, sin 0 al final) |
| nombre | el texto empacado (ocupa `largo + ⌈largo / 7⌉` bytes) |

Respuesta para el banco: `cuántos` (`u7`) y los nombres empacados (cada uno terminado en 0).

### 0x30 PERILLA

Mueve una perilla como si la persona la girara con esa pista y esa página a la vista, pero con un valor absoluto.

Pregunta:

| Campo | Tipo |
|---|---|
| pista | `u7` (0..7; en CADENA, la fila 0..15) |
| página | `u7`: 0 EDIT, 1 ENV, 2 FX, 3 SCL, 4 LFO, 5 GLO, 6 CADENA, 7 ARP |
| subpágina | `u7` (0 o 1, la segunda vuelta del botón) |
| perilla | `u7` (0..3, K1..K4) |
| valor | `s14` |

Respuesta: el valor que quedó (`s14`) y el contador de cambios.

Qué hay en cada página: lo da `META` tipo 2. Hoy:

| Página | Subpágina 0 | Subpágina 1 |
|---|---|---|
| EDIT | perillas 1–4 del motor | perillas 5–7 del motor y AZAR |
| ENV | VOLUMEN, PANEO, DELAY, REVERB | FILTRO, ANCHO, SATURA, SIDECHAIN |
| FX (global) | DELAY TIEMPO, DELAY REPITE, REVERB TAMAÑO, FILTRO | DELAY TONO, REVERB TONO, DRIVE, COMPRESIÓN |
| SCL | LARGO, VELOCIDAD, GOLPES, ROTAR (del patrón que suena) | MOVER, TRANSPONER, HUMANO, DENSIDAD |
| LFO | DESTINO, VELOCIDAD, PROFUNDIDAD, FORMA | lo mismo del LFO 2 |
| GLO (global) | TEMPO, SWING, RAÍZ, ESCALA | MUTES, RELOJ, IDIOMA, NOTAS Y CC (0.9) |
| CADENA | PATRÓN, VECES, (nada), CADENA | |
| ARP (sintes) | MODO, VELOCIDAD, OCTAVAS, SOSTENER | |

Notas:

- MOVER es una acción, no un valor guardado: mueve los pasos de la pista `valor` lugares.
- IDIOMA es ajuste del aparato. También se cambia con `AJUSTE`.
- Para cambiar de modelo, la página es 0 y la perilla es 4 (la perilla ALGO del aparato). Para el preset, la perilla es 5 (PRESET): el valor es el número de preset de `META` tipo 4, y cargarlo pisa las 8 perillas del motor. Si el sonido de ahora no es el del preset elegido, antes queda guardado como EDITADO, igual que al girar PRESET.
- La perilla SAMPLE (página 0, perilla 1 en SAMPLE) solo acepta lugares con sample. Si llega un lugar vacío, el aparato pone el más cercano hacia abajo (o hacia arriba, si abajo no hay) y la respuesta trae cuál quedó.

### 0x31 PERILLAS

Pregunta: `pista`, `página`, `subpágina`.

Respuesta: los 4 valores (`s14`). Las perillas sin función dan 0.

### 0x32 PASO

Escribe un paso completo. Lo que no viene en el registro queda como paso vacío.

Pregunta: `patrón`, `pista`, `paso` (`u7` cada uno) y el **registro de paso** empacado (formato en la sección 6.3, desde el byte de banderas).

Respuesta: contador de cambios.

Para borrar un paso se manda el registro `00` (banderas en cero). Un registro con los locks de un motor se aplica tal cual, aunque la pista tenga otro motor (como en el aparato: el lock es de la perilla, no del motor).

### 0x33 PISTA

Lee o escribe una pista entera de un patrón.

Pregunta: `patrón`, `pista`, `modo` (0 leer, 1 escribir) y, al escribir, la pista empacada en el formato de la sección 6.3 (largo, velocidad, máscaras, pasos, `FF`).

Respuesta al leer: `golpes` y `rotar` del euclidiano (`u7` cada uno) y después la pista empacada en ese mismo formato. Una pista de más de 512 bytes (muchos pasos con muchos locks) no cabe en un mensaje: responde `NO_CABE` con su tamaño, y la app la lee con el proyecto entero.

Respuesta al escribir: contador de cambios.

### 0x34 TRANSPORTE

Pregunta: `qué` (`u7`) y `valor` (`u7`).

| qué | valor |
|---|---|
| 0 tocar / parar | 1 tocar, 0 parar |
| 1 patrón | 0..15. Tocando, entra al terminar el patrón que suena, como el program change; parado, queda como el patrón de ahora. Apaga la CADENA si estaba prendida |
| 2 mute | máscara de 8 pistas (2 bytes: bits 0–6 y bit 7) |
| 3 solo | igual |

Respuesta: contador de cambios.

### 0x35 NOMBRE

Renombra un sonido del banco.

Pregunta: `lugar` (`u7`) y el nombre empacado (hasta 10 bytes, se cortan si son más).

Las letras que la fuente de la pantalla no tiene salen como espacio. En 0.9 la fuente suma Ã, Õ, Â, Ê, Ô y Ç para el portugués.

Respuesta: contador de cambios.

### 0x40 AJUSTE

Pregunta: `cuál` (`u7`), `modo` (0 leer, 1 escribir) y `valor` (`s14`, solo al escribir).

| cuál | Qué | Valores |
|---|---|---|
| 0 | IDIOMA | 0 español, 1 English, 2 português |

Los ajustes del aparato se guardan aparte de los proyectos. Si volver a la V15 los borra, el idioma regresa a español. RELOJ y NOTAS Y CC son del proyecto y se cambian con `PERILLA` en GLO.

Respuesta: el valor (`s14`).

---

## 5. Objetos de los traslados

| objeto | Qué | Leer | Escribir |
|---|---|---|---|
| 0 | El proyecto en la memoria (lo que suena), formato de la sección 6 | sí | sí: lo abre, como cargar una ranura (el aparato para un bloque) |
| 1 | El banco de sonidos del usuario, sección 7 | sí | sí: lo reemplaza entero (parado) |
| 2 | Una ranura de proyecto (índice 0..15), formato de la sección 6 | sí | sí (parado) |
| 3 | El autoguardado | sí | no |
| 4 | Un sample del usuario (índice), sección 8 | sí | sí (parado) |

Con estos objetos se resuelven:

- **Respaldo completo:** la app lee el banco, las 16 ranuras y los samples.
- **Devolver un respaldo al aparato:** la app escribe las ranuras y el banco. Hoy no hay manera de hacerlo.
- **Pasar un proyecto de un FM-1 a otro.**

---

## 6. Formato del proyecto

Es exactamente lo que el aparato guarda en una ranura, sin la cabecera de la ranura (`FGS1`, largo, CRC). Todos los números de más de un byte van en little endian.

### 6.1 Cabecera (8 bytes)

| Bytes | Qué |
|---|---|
| 0–3 | `46 47 42 31` («FGB1») |
| 4 | versión del formato (6 en 0.8.6 y 0.8.7; 7 en 0.9) |
| 5 | patrones (16) |
| 6–7 | `sizeof` de la canción (6052 en v6) |

El aparato abre las versiones 1 a 7. La app solo necesita entender la 7: si lee una ranura vieja, se la pide al aparato como objeto 2 y la recibe tal como está guardada. Si la quiere en v7, la carga (objeto 0, escribir) y la vuelve a leer.

### 6.2 La canción

Así está en v6. La v7 cambia lo marcado.

Primero van los bytes 0 a 1069 de la tabla. Después, los bytes 5166 a 6051.

| Desde | Bytes | Campo | Qué es |
|---|---|---|---|
| 0 | 8 | `model[8]` | el modelo de cada pista (índice en la lista de `META` tipo 1) |
| 8 | 704 | `p[8][11][8]` | las 8 perillas de cada modelo de cada pista (0..127); la 8.ª es el NIVEL del motor (por MIDI CC), no AZAR |
| 712 | 32 | `mix[8][4]` | ENV pág. 1: VOLUMEN, PANEO, DELAY, REVERB |
| 744 | 8 | `fx[8]` | FX global, en el orden de la tabla de FX |
| 752 | 32 | `lfo[8][4]` | DESTINO, VELOCIDAD, PROFUNDIDAD, FORMA |
| 784 | 8 | `azar[8]` | AZAR de cada pista (la 8.ª perilla de EDIT) |
| 792 | 1 | `bpm` | 40..250 |
| 793 | 1 | `swing` | 0..75 |
| 794 | 1 | `scale` | 0..7 |
| 795 | 1 | `muteq` | MUTES al compás |
| 796 | 1 | `cur` | el patrón que suena |
| 797 | 1 | `transp` | RAÍZ, con signo −12..12 |
| 798 | 256 | `eu[16][8][2]` | euclidiano: golpes, rotar |
| 1054 | 16 | `has_hand[16]` | bit `t`: la pista `t` de ese patrón trae pasos a mano guardados |
| (1070) | — | `hand` | no viaja aquí: va en 6.4 |
| 5166 | 704 | `edited[8][11][8]` | la versión del usuario de cada preset (PRESETS, el primero de la lista) |
| 5870 | 16 | `has_edited[8]` | 16 bits por pista |
| 5886 | 16 | `crow[16]` | CADENA: patrón de cada fila (255 = vacía) |
| 5902 | 16 | `crep[16]` | veces de cada fila (1..16) |
| 5918 | 1 | `chain_on` | |
| 5919 | 1 | `mclk_in` | v6: RELOJ ENTRA. **v7: `reloj`**: 0 NO, 1 ENTRA, 2 SALE, 3 ENTRA Y SALE (de fábrica, 3) |
| 5920 | 1 | `mclk_out` | v6: RELOJ SALE. **v7: sin uso** (0) |
| 5921 | 1 | `mnotes` | NOTAS Y CC: 0 no, 1 entran, 2 salen, 3 las dos |
| 5922 | 32 | `mix2[8][4]` | ENV pág. 2: FILTRO, ANCHO, SATURA, SIDECHAIN |
| 5954 | 32 | `scl2[8][4]` | SCL pág. 2: (MOVER, sin uso), TRANSPONER (64 = 0), HUMANO, DENSIDAD |
| 5986 | 32 | `lfo2[8][4]` | el LFO 2 |
| 6018 | 32 | `arp[8][4]` | MODO, VELOCIDAD, OCTAVAS, SOSTENER |
| 6050 | 1 | `arp_on` | bit `t`: el ARP de la pista `t` prendido |
| 6051 | 1 | — | relleno |

**v7, modelos.** MUESTRA desaparece y se suma SAMPLE:

| Pista | Modelos en v7 |
|---|---|
| bombo | 0 BARRIDO, 1 MEMBRANA, 2 SAMPLE |
| caja | 0 CAJA, 1 APLAUSO, 2 SAMPLE |
| metal | 0 GRANIZO, 1 PLACA, 2 SAMPLE |
| perc | 0 GOTA, 1 CHATARRA, 2 SAMPLE |
| sintes | 0..7 como hoy, 8 SAMPLE |

Las 8 perillas de SAMPLE, en `p[t][modelo SAMPLE]`. AFINA va primero porque en los sintes la perilla 0 siempre es la nota: las teclas y los pasos la mueven, y así SAMPLE se toca cromático. Es el mismo orden que tenían MUESTRA (TONO, MUESTRA) y TEXTURA (NOTA, MUESTRA).

| # | Perilla | Qué hace |
|---|---|---|
| 0 | AFINA | percusión: 40..88, 64 = como es (±24 st). Sintes: la nota, como la NOTA de los otros sintes; 76 (C4) = como es |
| 1 | SAMPLE | el lugar del sample: BOMBOS 0–23, CAJAS 24–47, TEXTURAS 48–95, TUYOS 96–127 (el lugar 96 + n es TUYOS n). Los lugares son fijos: agregar un sample no cambia los demás |
| 2 | INICIO | desde dónde: 0..127 es la fracción del largo |
| 3 | CAÍDA | lo acorta; de 120 para arriba suena entero |
| 4 | CORTE | 63..65 abierto; abajo quita agudos (paso bajo), arriba quita graves (paso alto) |
| 5 | CRUJIDO | 0 limpio; arriba menos bits y menos muestras |
| 6 | MODO | 0–42 GOLPE, 43–85 LAZO (en los sintes, mientras se sostiene la nota), 86–127 REVERSA |
| 7 | NIVEL | el volumen del motor: 64 = tal cual; arriba satura suave. No está en las páginas EDIT (llega por MIDI CC, igual que el NIVEL de los otros motores) |

En el aparato, EDIT página 1 es AFINA, SAMPLE, INICIO, CAÍDA y la página 2 es CORTE, CRUJIDO, MODO y AZAR. En los sintes, SAMPLE toca hasta 2 notas a la vez.

Un proyecto de antes (v1 a v6) se migra solo al abrirlo:

- RELOJ: `reloj = mclk_in + 2 × mclk_out`.
- El bombo y la caja con MUESTRA pasan a SAMPLE: el mismo sample, la misma altura (TONO), el mismo filtro (FILTRO → CORTE) y el mismo arranque (ARRANQUE → INICIO). PEGADA sostenía el golpe: ese sostén pasa a una CAÍDA más larga, y su empujón, junto con SATURA (y el NIVEL de la caja), pasa al NIVEL. Medido con un proyecto de prueba, el bombo queda a menos de 0.7 dB del 0.8 y la caja 2.5 dB abajo en el golpe. Los locks y la automatización de MUESTRA se convierten igual; los sonidos de MUESTRA del banco y del proyecto, también.
- METAL, PERC y los sintes reciben SAMPLE con sus valores de fábrica.

### 6.3 Pistas (128, al final del proyecto)

Las 128 pistas van en orden, patrón por patrón y dentro de cada patrón pista por pista (0..7). Cada pista es:

| Bytes | Qué |
|---|---|
| 1 | largo (1..32) |
| 1 | velocidad: 0 ½, 1 normal, 2 doble |
| 1 | máscaras: bit 0 viene la de golpes, bit 1 la de acentos |
| 4 | máscara de golpes (bit `s` = el paso `s` es golpe y no tiene nada más), si el bit 0 |
| 4 | máscara de acentos, igual, si el bit 1 |
| … | los demás pasos con algo: un **registro de paso** por cada uno |
| 1 | `FF`: fin de la pista |

Registro de paso:

| Bytes | Qué |
|---|---|
| 1 | número de paso (0..31). No va dentro de `PASO`, porque ahí el paso ya viene en la pregunta. |
| 1 | banderas: 1 golpe, 2 condición, 4 probabilidad, 8 ratchet, 16 micro, 32 locks, 64 automatización |
| 1 | golpe: 1 golpe, 2 acento (si bandera 1) |
| 1 | condición 0..11 (si 2) |
| 1 | probabilidad 0..100 (si 4) |
| 1 | ratchet 1..4 (si 8) |
| 1 | micro, con signo −8..7 (si 16) |
| 2 + n | locks (si 32): máscara de 12 bits (2 bytes, little endian) y un byte por cada lock prendido. Locks 0..7 son las perillas del motor (en sintes, el 0 es la NOTA relativa: 64 = sin mover); 8..11 son VOLUMEN, PANEO, DELAY y REVERB |
| 1 + n | automatización (si 64): máscara de 8 bits y un byte por perilla grabada (como mucho 2) |

Lo que no viene en el registro vale:

- golpe 0, condición 0 (SIEMPRE), probabilidad 100, ratchet 1, micro 0;
- sin locks y sin automatización.

### 6.4 Pasos a mano, sonidos viejos

Después de la canción, y antes de las pistas, van:

1. Por cada bit prendido en `has_hand`, en orden de patrón y de pista: 32 bytes. Son los pasos que la persona dibujó con GOLPES en 0.
2. `nupre` (1 byte) y `nupre × 21` bytes de sonidos guardados dentro del proyecto. Es formato de antes de la 0.7: al abrir, el aparato los pasa al banco. La app escribe `nupre = 0`.

### 6.5 Cuánto pesa

| Proyecto | Bytes |
|---|---|
| vacío | 2 650 |
| real típico (respaldos de Cris) | 3 100 |
| 16 patrones × 8 pistas × 32 golpes | 3 655 |
| peor caso (todos los pasos con todo) | ≈ 130 000: no cabe en ninguna ranura |

Un paso con 4 locks ocupa 9 bytes. Lo que no cabe en la ranura responde `NO_CABE`, igual que SAVE en el aparato («NO CABE»).

---

## 7. Formato del banco de sonidos

| Bytes | Qué |
|---|---|
| 1 | `0x82` (versión 2) |
| 1 | cuántos (0..96) |
| 21 × n | cada sonido |

Cada sonido (21 bytes):

| Bytes | Qué |
|---|---|
| 1 | pista |
| 1 | modelo |
| 11 | nombre, terminado en 0 |
| 8 | perillas |

En 0.9 el modelo usa la numeración v7, y el banco sube a versión 3 (`0x83`). El aparato abre la 1, la 2 y la 3; al abrir una 1 o una 2, los sonidos de MUESTRA pasan a SAMPLE. La app escribe la 3. Los nombres terminan en 0 dentro de sus 11 bytes.

---

## 8. Samples del usuario

### Formato del audio

Es el mismo de los samples de fábrica (`motores/src/fg_samples.h`):

- ADPCM IMA mono, en bloques de 64 muestras;
- cada bloque ocupa 36 bytes: 4 de cabecera (la muestra anterior en 16 bits con signo y el índice de paso en 1 byte, más 1 de relleno) y 32 de datos;
- la cabecera de cada bloque deja empezar o tocar al revés desde cualquier bloque.

La app convierte el audio antes de mandarlo: lo pasa a mono, lo lleva a 44.1 kHz (u otra tasa, si la anuncia) y lo codifica.

Cada bloque de 64 muestras ocupa 36 bytes, así que caben unas 1 800 muestras por KB. Con 140 KB de zona (136 KB para datos) caben unos **5.6 s** en total, a 44.1 kHz: por ejemplo, unos 40 golpes cortos de 0.14 s.

El aparato revisa al abrir: `bytes` ≥ `⌈largo / 64⌉ × 36`, tasa entre 4 000 y 96 000 y, con lazo, `inicio < fin ≤ largo`. Si no, responde `FORMATO`. La raíz MIDI es la nota en la que el sample suena tal cual en los sintes (60 = C4).

### Escribir uno

`ABRIR` con objeto 4, modo escribir y estos datos:

- índice: el lugar en TUYOS, 0..31;
- largo y CRC de los datos;
- nombre (hasta 11 bytes; empacado, al final de la pregunta, después del extra);
- extra: tasa (`u21`), largo en muestras (`u21`), raíz MIDI (`u7`), lazo (`u7`: 0 no, 1 sí), inicio y fin del lazo (`u21` cada uno).

El aparato:

- aparta espacio en sectores de 4 KB, todos seguidos (si no hay un hueco seguido del tamaño, responde `NO_CABE` con el hueco seguido más grande);
- calla lo que esté sonando de TUYOS;
- borra cada sector cuando le llega su primer trozo;
- al `CERRAR`, vuelve a leer lo escrito, revisa el CRC y escribe la entrada en el índice.

Hasta ese momento el sample no existe para el motor SAMPLE.

Escribir en un lugar que ya tiene sample lo reemplaza: el viejo se borra al `CERRAR` bien.

### La zona

Va después de las ranuras (sección 10). Tiene:

- un sector de 4 KB de índice: un registro de entradas de 48 bytes (lugar, posición, bytes, CRC, largo, tasa, raíz, lazo y nombre). Cada entrada se escribe completa con su marca al final; una entrada nueva para el mismo lugar gana sobre la anterior, que después se anula. Un corte de corriente a medias deja el sample de antes o el nuevo, nunca uno roto. Cuando el registro se llena (85 entradas), el aparato lo reescribe con lo que sigue vivo;
- 34 sectores de 4 KB (136 KB) para los datos.

Volver a la V15 borra la zona, igual que las ranuras. La app guarda una copia de todo lo que mandó, para devolverlo después de reinstalar.

---

## 9. El espejo (ya existe, 0.8.x)

Lo usa `espejo/index.html` y sigue igual. La app lo recibe con `ESCUCHA` bit 0, o pidiendo CC 119 = 127 en el canal 16 cada segundo.

**Tipo 1, `GOLPE`:**

```
F0 7D 46 47 01 pista modelo fuerza p0..p7 F7
```

Llega cada golpe, en cuanto suena.

**Tipo 2, `ESTADO`:**

```
F0 7D 46 47 02 … F7
```

Llega una vez por cuadro, unos 30 por segundo, con estos campos en orden:

- vista;
- tempo (2 bytes);
- pista;
- patrón;
- en cola (127 = nada);
- patrones con pasos (3 bytes);
- mutes (2);
- solos (2);
- al compás (2);
- hay al compás (1);
- efectos de performance (3);
- niveles de las 8 pistas;
- mezcla 8 × 4;
- desde 0.9: el paso que suena en cada pista (8 bytes; 127 = parada), porque cada pista puede tener otro largo y otra velocidad;
- desde 0.9: tocando (1 byte: 0 o 1).

El mensaje entero mide 74 bytes de `F0` a `F7` (65 en 0.8). Quien lea solo los primeros campos no se rompe.

Los dos tipos no llevan `id` ni `estado`, porque son avisos del espejo, anteriores a este protocolo.

---

## 10. Mapa de la flash

**0.8.x:** 18 ranuras de 16 KB desde `0x97000` (0..15 proyectos, 16 autoguardado, 17 banco), con cabecera «FGS1». Llegan hasta `0xDF000`. En `0xE0000` empieza la zona de actualización.

**0.9 (decidido por Cris el 29-oct: ranuras de 8 KB):**

| Desde | Zona | Tamaño |
|---|---|---|
| `0x97000` | 16 ranuras de proyecto | 8 KB cada una (128 KB): caben 8 180 bytes de proyecto |
| `0xB7000` | autoguardado | 16 KB |
| `0xBB000` | banco de sonidos | 4 KB (96 × 21 + 2 = 2 018 bytes) |
| `0xBC000` | samples del usuario: índice | 4 KB |
| `0xBD000` | samples del usuario: datos | 136 KB |
| `0xDF000` | ajustes del aparato (idioma) | 4 KB |

Las cabeceras de 0.9 dicen «FGS0». La app no depende del mapa: lee los tamaños en `INFO` y en `LISTA`.

**Al pasar de 0.8 a 0.9** el aparato acomoda las ranuras viejas en el primer arranque: copia cada ranura, el autoguardado y el banco a su lugar nuevo y al final escribe el índice de samples, que marca que ya está hecho. Un corte de corriente a medias no pierde nada: el siguiente arranque sigue donde iba (probado con un corte en cada una de las 285 escrituras). Un proyecto de 0.8 que pase de 8 KB no cabe en su ranura nueva: se pierde, y la pantalla lo dice al arrancar («NO CUPO EN 8 KB»). Los proyectos reales pesan unos 3 KB; antes de instalar conviene respaldar las ranuras.

Con ranuras de 8 KB caben unos 5 500 bytes de pasos por encima del proyecto vacío: los proyectos reales (3.1 KB), todos los golpes en los 16 patrones (3.7 KB) o unos 600 pasos con 4 locks. Un proyecto más cargado se queda en el autoguardado (16 KB) y SAVE en una ranura dice NO CABE. Cuando lleguen los 64 pasos, hay que volver a medir esto.

---

## Historial

- 1 (29-oct-2026): primera versión, antes de programar el firmware 0.9.
- 1, revisión 2 (29-oct-2026), al programarlo:
  - SAMPLE: AFINA es la perilla 0 y SAMPLE la 1 (antes al revés). La 8.ª perilla de `p` es NIVEL, no AZAR. Lugares fijos de cada banco en la perilla SAMPLE.
  - `META`: la respuesta es un solo bloque empacado; mínimo y máximo de las perillas en `int16`; los samples traen su lugar; los destinos de LFO piden la pista.
  - `id` de 1 a 127, y una pregunta repetida es la que repite comando e `id` de la anterior.
  - En una respuesta, lo empacado va siempre al final: `PISTA` (leer) trae primero el euclidiano; `LISTA` de samples trae el largo de cada nombre antes del nombre; en `ABRIR` de un sample el nombre va después del extra.
  - Borrar un sample: la pista toca el más cercano hacia abajo mientras el lugar esté vacío.
  - Traslados: comparten memoria con el guardado del aparato (el autoguardado espera; guardar o abrir en el aparato cancela el traslado).
  - TUYOS: sectores seguidos, `NO_CABE` con el hueco seguido más grande, revisión del formato al abrir, índice como registro.
  - Mapa de la flash decidido (sección 10) y la mudanza desde 0.8.
- 1, revisión 4 (30-oct-2026), a pedido de Cris: en SAMPLE, la perilla PRESET también recorre los samples. Cada uno es el sonido de ahora con ese sample en la perilla SAMPLE. `META` 4 los lista después de los de fábrica; `PERILLA` perilla 5 los elige por su número en esa lista.
- 1, revisión 3 (29-oct-2026), a pedido de la companion:
  - `ESTADO` del espejo suma al final el paso de cada pista y el byte «tocando».
  - Textos dentro de una lista: se quedan como en la revisión 2. `META` responde un solo bloque empacado con los textos terminados en 0 adentro (se desempaca entero y luego se parte por los 0). En `LISTA` de samples cada nombre trae su largo (`u7`) antes y va empacado solo, sin 0. La companion había implementado nombres empacados por separado con su 0 al final; la regla de aquí es la que vale.
