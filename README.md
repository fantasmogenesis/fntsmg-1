# FNTSMG-1

Firmware de groovebox para el **M-VAVE FM-1**, de [fantasmogenesis](https://fantasmogenesis.art). Corre sobre [Felucca](https://github.com/hugelton/Felucca) de Hügelton Instruments.

- 8 pistas: bombo, caja, metal, percusión y 4 sintes, con 18 motores propios (SH, ácido, FM, cuerdas, textura, dron, dub, resonador…).
- Secuenciador de 32 pasos por pista, 16 patrones, locks de sonido y de mezcla, condiciones, probabilidad, ratchet y micro.
- Cadena de patrones, 16 efectos en vivo, arpegiador, azar de sonido y de patrón, deshacer.
- Audio USB de 8 pistas (44.1 kHz) y MIDI por USB y TRS.

Guía rápida: <https://fantasmogenesis.art/fm1/guia>

## Instalar

Con el instalador web de fantasmogenesis.art, o con el de Felucca (`vendor/Felucca/tools/fm1_install.py`). El regreso al firmware de fábrica se hace con el Workbench de M-VAVE.

Instalar firmware de terceros es bajo tu propio riesgo. Respalda tus proyectos antes.

## Estructura

- `motores/src/`: todo lo propio en C: motores (`fg_kick`, `fg_snare`, `fg_metal`, `fg_perc`, `fg_synth`), mezcla (`fg_mix`), secuenciador (`fg_seq`), visuales (`fg_vis`) y la interfaz de la caja (`fg_box`, `fg_box_draw`). No depende del aparato.
- `firmware/`: cómo se arma la imagen para el FM-1 (`build.sh`) y el simulador en la Mac (`host/`), que saca capturas y WAV.
- `vendor/Felucca/`: submódulo con el fork de Felucca (rama `groovebox`): arranque, USB, flash, pantalla y teclado.
- `caja/`, `espejo/`: la caja y los visuales en el navegador (WebAssembly).

## Compilar

Se necesita el toolchain de JieLi y el SDK AC79 que describe `vendor/Felucca/BUILDING.md`. Las rutas de `build.sh` apuntan a donde viven en la máquina de desarrollo: cámbialas por las tuyas.

```sh
git clone --recursive https://github.com/fantasmogenesis/fntsmg-1
cd fntsmg-1/firmware
./build.sh mt        # out/fntsmg-1-8pistas.fwsc
./build.sh host      # el simulador en la Mac
```

## Licencia

GPL-3.0, igual que Felucca (ver `LICENSE`). Los samples del banco son de fantasmogenesis o generados por estos motores y se distribuyen con el firmware bajo la misma licencia. La letra Manrope va bajo la SIL Open Font License (`firmware/fonts/OFL.txt`).
