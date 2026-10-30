#!/bin/sh
# La caja en la Mac: compila box_sim con el C de los motores y corre el guion (capturas PNG y WAV en out/)
set -e
cd "$(dirname "$0")"
M=../../motores/src
S="$M/fg_box.c $M/fg_box_draw.c $M/fg_font.c $M/fg_tables.c $M/fg_smp.c $M/fg_samp.c $M/fg_usr.c $M/fg_lang.c $M/fg_samples.c $M/fg_kick.c $M/fg_snare.c $M/fg_metal.c $M/fg_perc.c $M/fg_synth.c $M/fg_mix.c $M/fg_seq.c $M/fg_vis.c"
mkdir -p out
rm -f out/*.ppm out/*.png
cc -O2 -g -std=gnu99 -Wall -Wextra -Wno-unused-parameter -I"$M" -o out/box_sim box_sim.c $S
./out/box_sim out
for f in out/*.ppm; do sips -s format png "$f" --out "${f%.ppm}.png" >/dev/null 2>&1 && rm "$f"; done
