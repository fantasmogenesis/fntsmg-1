#!/bin/sh
# Compila Felucca (firmware + emulador web) con el toolchain del SSD.
#   ./build.sh          firmware -> vendor/Felucca/build/felucca.fwsc
#   ./build.sh emu      + emulador -> vendor/Felucca/build/emu/ (servidor "fm1-groovebox", :8131)
set -e
SDK=/Volumes/SSD-HUB/tools/_sdk
export JIELI_TOOLCHAIN=$SDK/jieli/toolchain AC79_SDK=$SDK/fw-AC79_AIoT_SDK
export PYTHON=$SDK/fm1-venv/bin/python DYLD_LIBRARY_PATH=/opt/homebrew/lib   # Pillow con raqm
colima status >/dev/null 2>&1 || colima start
cd "$(dirname "$0")/vendor/Felucca"
./build.sh
if [ "$1" = emu ]; then
    docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:4.0.10 sh web/emu/build.sh
fi
