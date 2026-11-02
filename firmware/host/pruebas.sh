#!/bin/sh
# Las pruebas de la caja en la Mac (con ASan y UBSan): ./pruebas.sh [nombre ...]  (sin nombres: todas)
set -e
cd "$(dirname "$0")"
M=../../motores/src
S="$M/fg_box.c $M/fg_box_draw.c $M/fg_font.c $M/fg_tables.c $M/fg_smp.c $M/fg_samp.c $M/fg_usr.c $M/fg_lang.c $M/fg_samples.c $M/fg_kick.c $M/fg_snare.c $M/fg_metal.c $M/fg_perc.c $M/fg_synth.c $M/fg_cand.c $M/fg_mix.c $M/fg_seq.c $M/fg_vis.c"
mkdir -p out
T=${*:-"fase1_test hold_test samp_test proto_test v9_test"}
for t in $T; do
    cc -O1 -g -fsanitize=address,undefined -std=gnu99 -Wall -Wno-unused-parameter -I"$M" -o "out/$t" "$t.c" $S
    echo "== $t"
    if [ "$t" = v9_test ]; then "./out/$t" $(ls out/v7.bin out/v6.bin 2>/dev/null); else "./out/$t"; fi   # (v7/v6: mig8_test con -DVIEJO)
done
