#!/bin/sh
# La caja web = el firmware del FM-1 compilado a WASM (Emscripten en Docker) -> caja/fntsmg1.wasm
set -e
cd "$(dirname "$0")/.."
M=motores/src
S="$M/fg_box.c $M/fg_box_draw.c $M/fg_font.c $M/fg_tables.c $M/fg_smp.c $M/fg_samp.c $M/fg_usr.c $M/fg_lang.c $M/fg_samples.c $M/fg_kick.c $M/fg_snare.c $M/fg_metal.c $M/fg_perc.c $M/fg_synth.c $M/fg_cand.c $M/fg_mix.c $M/fg_seq.c $M/fg_vis.c"
colima status >/dev/null 2>&1 || colima start
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 \
    emcc -O2 -std=gnu99 -Wall -Wno-unused-parameter -I$M --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 \
    -sINITIAL_MEMORY=16777216 -sEXPORTED_FUNCTIONS=_cj_init,_cj_boot,_cj_block,_cj_left,_cj_right,_cj_button,_cj_key,_cj_enc,_cj_frame,_cj_repaint,_cj_band,_cj_px,_cj_slot,_cj_slot_len,_cj_set_slot_len,_cj_nslots,_cj_dirty,_cj_leds_buttons,_cj_leds_keys,_cj_leds_half,_cj_playing \
    -o caja/fntsmg1.wasm caja/caja_wasm.c $S
echo "wasm: caja/fntsmg1.wasm ($(wc -c < caja/fntsmg1.wasm | tr -d ' ') B)"
