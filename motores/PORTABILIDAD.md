# El motor fuera del FM-1

Los motores (`src/fg_*`) se van a reusar en varias cajas: el FM-1, una app de iPad/iOS, un plugin VST3/AU, una app web, el Move con Schwung y otras máquinas. Aquí está qué hay que hacer para que el mismo C suene igual en todas, y qué variante tiene sentido en cada una. Escrito el 2026-10-20, con los motores ya cerrados a oído en el laboratorio.

## Lo que ya juega a favor

- **C99 en punto fijo, sin libc, sin float, sin divisiones por muestra.** Lo que corre en un AC791N sin FPU corre en cualquier cosa: ARM con NEON, x86, WASM, RISC-V. En hosts con float sólo se convierte en el borde (`int32 ±32767` ↔ `float ±1`).
- **Mismo código, dos destinos probados:** WASM (laboratorio) y `pi32v2` (toolchain JieLi). `build.sh pi32` es la prueba de que no se coló nada del navegador.
- **Bit-exacto:** al ser enteros, un render de referencia (WAV dorado) tiene que salir idéntico en todas las plataformas. Es la prueba de que un port suena igual.
- **Visual de 1 bit:** la estética de la pantalla del FM-1 (Bayer, crema sobre negro) escala tal cual al OLED de 128×64 del Move y se ve intencional a 1024 px en un iPad.

## Lo que falta para que sea portable

Por orden. Lo de arriba bloquea a lo de abajo.

1. **Repo propio, fuera de Felucca.** Los motores se escribieron desde cero (no hay código de Felucca, que es GPL-3). Mantenerlos en un repo aparte (`fg-motor`, privado) para poder venderlos en App Store y como VST: la GPL no es compatible con la App Store. Sólo la capa de plataforma del FM-1 vive en Felucca (GPL). Decidir licencia del core (propietaria al inicio).
2. **Todo en un struct, nada estático.** `lab_wasm.c` usa estáticos globales (`K`, `SN`, `MX`…). Pasar a `fg_box_t` con todo adentro: un VST o el Move pueden tener varias instancias.
3. **Tasa de muestreo en runtime.** Hoy `FG_SR = 44118` está horneado en las tablas (`FG_INC_HZ`, `FG_DECAY`, `FG_CUT_G`). Opción elegida: `fg_box_init(box, sr)` regenera esas tablas al arrancar (fuera del audio, ahí sí se vale hacer cuentas lentas). Tasas: 44 100, 44 118, 48 000, 96 000.
4. **Bloque variable.** Los hosts piden bloques de cualquier tamaño (1–4096). El render parte en trozos de `FG_BLOCK`.
5. **Secuenciador a C (`fg_seq`).** Hoy vive en `lab/worklet.js`: pasos, swing, locks del motor y de mezcla, locks relativos (NOTA, CORTE), automatización, gate hasta el siguiente paso, mutes al instante o al compás, perf de secuencia (repite, reversa, solo perc, sin bombo), anticipación de la caja. Port 1:1, con una prueba que compare los eventos del worklet contra los del C. **Hecho 2026-10-20:** `src/fg_seq.{h,c}`, prueba `test/seq_compare.mjs`.
6. **Visuales a C (`fg_vis`).** Hoy en `lab/index.html`. Dibuja a un buffer de 1 bit de cualquier tamaño a partir de: golpes, niveles por pista, parámetros y estado de los FX de performance. **Hecho 2026-10-20:** `src/fg_vis.{h,c}` (por bandas de 16 filas), hoja de contacto `test/vis_sheet.c`.
7. **Tabla de parámetros única (`fg_meta`).** Nombres, rangos, valores por defecto y cómo se muestran (NOTA → G2, TIPO → m7, RAZÓN → ×1.41). De ahí se generan: la UI web, los parámetros de JUCE, el `ui_hierarchy` de Schwung y el header de una unidad de drumlogue. Hoy esa información está duplicada a mano entre `fg_*.h` y `index.html`.
8. **Estado del proyecto (`fg_state`).** Formato binario versionado (pistas, motores, pasos, locks, automatización, mezcla, FX, foto, sample) y exportación a JSON para escritorio/web.
9. **Polifonía como variante.** Los sintes son monofónicos con dos ranuras (nota nueva + cola). En iPad/VST se puede tener N voces por sinte sin tocar el motor: es un asignador de voces encima.
10. **Pruebas de igualdad.** `tools/knob_audit.mjs` + un WAV dorado por motor y preset. Cada port debe pasar esto antes de tocarlo a oído.

## Capas

```
fg-motor/
  core/   motores + mezcladora + FX de cinta (lo que ya existe)
  seq/    secuenciador, locks, automatización, mutes
  vis/    visuales de 1 bit
  meta/   tabla de parámetros
  state/  proyecto
  plat/   fm1/  web/  juce/  move/  logue/  disting/  daisy/
```

Las plataformas sólo traducen: botones y perillas → acciones, buffer de audio → salida, buffer de 1 bit → pantalla.

## Destinos

| Destino | Cómo entra | Encaje | Variante |
|---|---|---|---|
| **FM-1** | Firmware sobre la plataforma Felucca (AC791N, 44 118 Hz, 128 frames, 240×240) | Es el original | Groovebox completa, 8 pistas |
| **Web** | WASM + AudioWorklet (ya existe: el laboratorio) | Total | Juguete/demo gratis que vende lo demás, y la estación para mandar fotos y samples al FM-1 por USB (SysEx: Felucca ya tiene `ED_SMP_BEGIN/WRITE/END` en `firmware/src/editor.c`) |
| **iPad / iPhone** | App standalone + AUv3 con JUCE 9 (kit en `tools/_sdk`) o Swift + el C directo | Total, con pantalla grande | 8 pistas, perillas táctiles, visual a pantalla completa en Metal, MIDI y Link. Puede compartir el shell de app con MBR-01 |
| **VST3 / AU** | JUCE (licencia Starter gratis hasta USD 20k) | Total | A: groovebox con secuenciador sincronizado al DAW y 8 salidas. B: por motor, "Percusión" (4 carriles por nota MIDI) y "Sintes" (6 motores, poli) |
| **Ableton Move** | Schwung: `sound_generator` en C (`plugin_api_v2_t` de `host/plugin_api_v1.h`, `render_block` en int16 estéreo intercalado) | Muy bueno | Cada motor o el kit de 4 percusiones como módulo de sonido; y un módulo *overtake* (tool) con la groovebox completa: 16 pasos, 8 perillas = una página entera de EDIT, OLED de 1 bit. Pre-alfa; en el callback nada de malloc, archivos ni logs |
| **Korg drumlogue** | logue SDK, unidades synth en C/C++ (hasta 24 parámetros) | Muy bueno para la percusión | Bombo, caja, metal y perc como unidades de usuario |
| **Korg NTS-1 mkII / minilogue xd / prologue** | logue SDK, unidades de oscilador y efecto (memoria chica) | Parcial | Fase rota, FM y Ácido como osciladores; stutter/cinta como efecto. Verificar límites de tamaño por unidad |
| **Expert Sleepers disting NT** | API C++ de algoritmos | Bueno | Cada motor como algoritmo con entradas de CV (eurorack) |
| **Electrosmith Daisy** | libDaisy (C++, 48 kHz) | Total | Base para un hardware propio o un módulo eurorack (Patch.Init, Pod, Field) |
| **Raspberry Pi Pico 2 / Teensy** | C directo | Bueno | Hardware DIY barato |

## Notas por destino

- **Move:** sus controles son 32 pads, 16 botones de paso, 8 perillas, volumen y rueda; pantalla de 128×64 a 1 bit. Para los visuales hay que pensar el encuadre a 2:1. El módulo *overtake* toma pads, LEDs y pantalla (con `raw_ui` también Back); Shift, Menu, Back, rueda, perillas y Mute no se pueden reclamar. La comunidad reporta CPU como límite real: medir antes de ofrecer 8 pistas.
- **iPad:** el visual es el producto. Aquí sí cabe tocar la pantalla para elegir pista/paso, pero conservar la gramática del FM-1 (mantener + girar = lock) para que quien aprende una aprenda las dos.
- **VST:** el secuenciador interno se sincroniza al reloj del host; los locks pueden salir como automatización del DAW.
- **FM-1:** el presupuesto real de CPU con 8 pistas todavía no está medido. Es lo primero al tener la caja.

## Fuentes

- Schwung (MIT, código propio; algunas piezas GPL): github.com/charlesvestal/schwung, `docs/MODULES.md`.
- drumlogue SDK: korg.com/products/drums/drumlogue/sdk.php.
- disting NT C++ API: firmware 1.7 (marzo 2025).
