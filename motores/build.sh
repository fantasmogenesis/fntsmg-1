#!/bin/sh
# Motores del FM-1 -> lab/motores.wasm (Emscripten en Docker) y, con "pi32", una compilación de prueba
# con el toolchain del FM-1 para ver que el C entra al aparato.
set -e
cd "$(dirname "$0")"
python3 tools/gen_tables.py
colima status >/dev/null 2>&1 || colima start
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 \
    emcc -O2 -std=gnu99 -Wall -Isrc --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sINITIAL_MEMORY=8388608 \
    -o lab/motores.wasm src/lab_wasm.c src/fg_kick.c src/fg_snare.c src/fg_metal.c src/fg_perc.c src/fg_synth.c src/fg_mix.c src/fg_tables.c
cp lab/motores.wasm ../caja/motores.wasm
echo "wasm: lab/motores.wasm ($(wc -c < lab/motores.wasm | tr -d ' ') B)"
# secuenciador y visuales: su propio wasm de prueba (test/seq_compare.mjs) y el puente de pantalla para la web
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 sh -c '
    emcc -O2 -std=gnu99 -Wall -Isrc --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -o test/seq.wasm test/seq_wasm.c src/fg_seq.c src/fg_tables.c &&
    emcc -O2 -std=gnu99 -Wall -Isrc --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -o test/vis.wasm test/vis_wasm.c src/fg_vis.c src/fg_tables.c'
echo "wasm: test/seq.wasm ($(wc -c < test/seq.wasm | tr -d ' ') B), test/vis.wasm ($(wc -c < test/vis.wasm | tr -d ' ') B)"
node test/seq_compare.mjs
if [ "$1" = pi32 ]; then
    TC=/Volumes/SSD-HUB/tools/_sdk/jieli/jieli-linux-toolchains-20260730.1
    docker run --rm --platform linux/amd64 -v "$PWD":/w -v "$TC":/opt/jieli:ro -w /w debian:bookworm-slim \
        sh tools/pi32.sh
fi
