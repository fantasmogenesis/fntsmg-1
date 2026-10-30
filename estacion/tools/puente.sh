#!/bin/sh
# Compila el puente (estacion/tools/puente.c) contra el fg_box de una copia del firmware y corre la prueba del cliente.
# Uso: estacion/tools/puente.sh [carpeta del repo con el firmware]   (por omisión ../fm1-groovebox-09, la rama 0.9)
set -e
cd "$(dirname "$0")"
R=${1:-../../../fm1-groovebox-09}
M=$R/motores/src
OUT=${TMPDIR:-/tmp}/fntsmg-puente
cc -O1 -std=gnu99 -w -I"$M" -o "$OUT" puente.c $M/fg_box.c $M/fg_box_draw.c $M/fg_font.c $M/fg_tables.c $M/fg_smp.c $M/fg_samp.c $M/fg_usr.c $M/fg_lang.c \
  $M/fg_samples.c $M/fg_kick.c $M/fg_snare.c $M/fg_metal.c $M/fg_perc.c $M/fg_synth.c $M/fg_mix.c $M/fg_seq.c $M/fg_vis.c
node prueba-caja.mjs "$OUT"
