#!/usr/bin/env python3
"""Letras de la pantalla del FM-1: Manrope (OFL) en mapas de 4 bits por pixel, recortados a lo que pinta cada letra.
Escribe motores/src/fg_font.c y fg_font.h. Solo mayúsculas, números y los signos que usa la interfaz (la UI escribe
todo en mayúsculas): así cinco tamaños caben en ~60 KB de flash.
  DYLD_LIBRARY_PATH=/opt/homebrew/lib tools/_sdk/fm1-venv/bin/python firmware/tools/gen_font.py"""
import pathlib
from PIL import Image, ImageDraw, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
TTF = HERE.parent / "fonts" / "Manrope-var.ttf"
OUT = HERE.parent.parent / "motores" / "src"

BASE = "".join(chr(c) for c in range(32, 96)) + "ÁÉÍÓÚÑÜ·›×½–" + "ÃÕÂÊÔÇ"   # (0.9: el portugués)
# nombre, tamaño en px, peso, juego de letras
FACES = [("FG_FONT_S", 12, 700, BASE), ("FG_FONT_M", 16, 800, BASE), ("FG_FONT_L", 26, 800, BASE),
         ("FG_FONT_XL", 40, 700, BASE), ("FG_FONT_N", 64, 700, "0123456789")]


def face(size, weight):
    f = ImageFont.truetype(str(TTF), size)
    f.set_variation_by_axes([weight])
    return f


def build(name, size, weight, chars):
    f = face(size, weight)
    asc, desc = f.getmetrics()
    glyphs, data = [], bytearray()
    for ch in chars:
        adv = round(f.getlength(ch))
        box = f.getbbox(ch)                      # (x0, y0, x1, y1) desde el origen de arriba
        w, h = max(0, box[2] - box[0]), max(0, box[3] - box[1])
        off = len(data)
        if w and h:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-box[0], -box[1]), ch, font=f, fill=255)
            px = im.load()
            for y in range(h):
                row = [px[x, y] >> 4 for x in range(w)]
                if len(row) % 2:
                    row.append(0)
                for i in range(0, len(row), 2):
                    data.append(row[i] << 4 | row[i + 1])
        glyphs.append((ord(ch), w, h, box[0], box[1] - asc, adv, off))   # yoff respecto a la línea base
    return asc, desc, glyphs, data


def main():
    c, h = [], []
    c.append("/* generado por firmware/tools/gen_font.py desde Manrope (SIL OFL 1.1): no editar */")
    c.append('#include "fg_font.h"')
    h.append("/* generado por firmware/tools/gen_font.py: no editar */")
    h.append("#pragma once")
    h.append("#include <stdint.h>")
    h.append("typedef struct { uint16_t cp; uint8_t w, h; int8_t x, y; uint8_t adv; uint32_t off; } fg_glyph_t;")
    h.append("typedef struct { uint8_t size, asc, desc; uint16_t n; const fg_glyph_t *g; const uint8_t *bits; } fg_font_t;")
    total = 0
    for name, size, weight, chars in FACES:
        asc, desc, glyphs, data = build(name, size, weight, chars)
        total += len(data) + len(glyphs) * 12
        lo = name.lower()
        c.append(f"static const uint8_t {lo}_bits[{max(1, len(data))}] = {{" + ",".join(map(str, data or [0])) + "};")
        c.append(f"static const fg_glyph_t {lo}_g[{len(glyphs)}] = {{" +
                 ",".join("{%d,%d,%d,%d,%d,%d,%d}" % g for g in glyphs) + "};")
        c.append(f"const fg_font_t {name} = {{{size}, {asc}, {desc}, {len(glyphs)}, {lo}_g, {lo}_bits}};")
        h.append(f"extern const fg_font_t {name};   /* Manrope {weight}, {size} px */")
        print(f"{name}: {size}px {weight}, {len(glyphs)} letras, {len(data)} B")
    (OUT / "fg_font.c").write_text("\n".join(c) + "\n")
    (OUT / "fg_font.h").write_text("\n".join(h) + "\n")
    print(f"total ~{total} B -> {OUT / 'fg_font.c'}")


main()
