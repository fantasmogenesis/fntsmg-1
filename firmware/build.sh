#!/bin/sh
# FNTSMG-1, el firmware de la caja para el FM-1: Felucca (arranque, USB, actualización, flash, pantalla, teclado) + fg_box.
#   ./build.sh          -> out/fntsmg-1.fwsc y el sitio del instalador en out/sitio
#   ./build.sh host     -> la caja en la Mac (host/build.sh): capturas y WAV
#   ./build.sh mt       -> out/fntsmg-1-8pistas.fwsc: el audio USB con 8 canales (las 8 pistas antes de la mezcla), solo 44.1 kHz
set -e
cd "$(dirname "$0")"
if [ "$1" = host ]; then exec host/build.sh; fi
HERE=$PWD
MOT=$HERE/../motores/src
FEL=$HERE/../vendor/Felucca
SRCS="fg_tables.c fg_smp.c fg_samples.c fg_kick.c fg_snare.c fg_metal.c fg_perc.c fg_synth.c fg_mix.c fg_seq.c fg_vis.c fg_font.c fg_box.c fg_box_draw.c"
mkdir -p out
rm -rf "$FEL/build/fg"; mkdir -p "$FEL/build/fg"
for f in $SRCS; do cp "$MOT/$f" "$FEL/build/fg/"; done
cp "$MOT"/fg_*.h "$FEL/build/fg/"
# 16 patrones (el formato del proyecto los guarda todos)
echo "-DFG_DEVICE -DFG_SEQ_NP=16 ${FGX_CFLAGS}" > "$FEL/build/fg/cflags.txt"
if [ "$1" = mt ]; then
    FELUCCA_FG=1 FELUCCA_SLICE=0 FELUCCA_UAC_MT=1 ../build.sh
    cp "$FEL/build/felucca.fwsc" out/fntsmg-1-8pistas.fwsc
    shasum -a 256 out/fntsmg-1-8pistas.fwsc | tee out/fntsmg-1-8pistas.fwsc.sha256
    exit 0
fi
FELUCCA_FG=1 FELUCCA_SLICE=0 ../build.sh
cp "$FEL/build/felucca.fwsc" out/fntsmg-1.fwsc
shasum -a 256 out/fntsmg-1.fwsc | tee out/fntsmg-1.fwsc.sha256
rm -rf out/sitio
python3 "$FEL/web/make_site.py" out/fntsmg-1.fwsc FNTSMG-1-0.6 out/sitio >/dev/null
