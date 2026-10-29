#!/bin/sh
# El espejo: fg_vis a WebAssembly (Emscripten en Docker, como motores/build.sh) -> espejo/espejo.wasm
set -e
cd "$(dirname "$0")/.."
colima status >/dev/null 2>&1 || colima start
docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 \
    emcc -O2 -std=gnu99 -Wall -Imotores/src --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sINITIAL_MEMORY=16777216 \
    -o espejo/espejo.wasm espejo/espejo_wasm.c motores/src/fg_vis.c motores/src/fg_tables.c
echo "wasm: espejo/espejo.wasm ($(wc -c < espejo/espejo.wasm | tr -d ' ') B)"
