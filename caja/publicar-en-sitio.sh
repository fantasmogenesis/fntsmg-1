#!/bin/sh
# Copia la caja tocable al sitio (fantasmogenesis.art/fm1/caja) con su cabecera de página completa.
# Correr después de cambiar la caja o los motores; el deploy del sitio sigue siendo "haz push y deploy".
set -e
cd "$(dirname "$0")"
DEST="/Volumes/SSD-HUB/música/fantasmogenesis/source/public/fm1/caja"
mkdir -p "$DEST"
cp motores.wasm worklet.js "$DEST/"
{
  printf '<!doctype html>\n<html lang="es">\n<head>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">\n<link rel="icon" href="/favicon.svg">\n'
  # rutas absolutas: Next quita la barra final de /fm1/caja/ y las relativas apuntarían a /fm1/
  sed -e "s#fetch('motores.wasm')#fetch('/fm1/caja/motores.wasm')#" -e "s#addModule('worklet.js')#addModule('/fm1/caja/worklet.js')#" index.html
  printf '\n</html>\n'
} > "$DEST/index.html"
echo "caja copiada a $DEST"
