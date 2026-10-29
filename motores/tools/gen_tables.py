#!/usr/bin/env python3
"""Tablas de los motores (punto fijo): seno, caídas, exp2. Escribe src/fg_tables.h (declaraciones) y
src/fg_tables.c (los datos, una sola copia para todos los motores: antes cada objeto traía la suya, ~5 KB c/u)."""
import math, pathlib

SR = 44118                      # tasa del FM-1 (FM1_AUDIO_HARDWARE_RATE)
out, hdr = [], []
w = out.append
h = hdr.append
h("/* generado por tools/gen_tables.py: no editar */")
h("#pragma once")
h("#include <stdint.h>")
h(f"#define FG_SR {SR}")
w("/* generado por tools/gen_tables.py: no editar */")
w('#include "fg_tables.h"')

# seno Q15, 2048 puntos + 1 para interpolar
n = 2048
vals = [round(32767 * math.sin(2 * math.pi * i / n)) for i in range(n + 1)]
h("extern const int16_t FG_SIN[2049];")
w(f"const int16_t FG_SIN[{n + 1}] = {{" + ",".join(map(str, vals)) + "};")

# caídas: índice 0..127 -> coeficiente Q30 por muestra para caer 60 dB en T, T de 3 ms a 4 s (exponencial)
dec = []
for i in range(128):
    t = 0.003 * (4.0 / 0.003) ** (i / 127)
    dec.append(round(math.exp(math.log(0.001) / (t * SR)) * (1 << 30)))
h("extern const int32_t FG_DECAY[128];")
w("const int32_t FG_DECAY[128] = {" + ",".join(map(str, dec)) + "};")

# 2^(i/256) en Q16, 257 puntos
h("extern const uint32_t FG_EXP2[257];")
w("const uint32_t FG_EXP2[257] = {" + ",".join(str(round(2 ** (i / 256) * 65536)) for i in range(257)) + "};")

# filtro escalera: índice 0..1023 -> fc = 20 Hz * 2^(i/100) (tope 18 kHz), g = 1 - exp(-2 pi fc / SR) en Q15
cut = []
for i in range(1024):
    fc = min(18000.0, 20.0 * 2 ** (i / 100))
    cut.append(round((1 - math.exp(-2 * math.pi * fc / SR)) * 32767))
h("extern const int16_t FG_CUT_G[1024];")
w("const int16_t FG_CUT_G[1024] = {" + ",".join(map(str, cut)) + "};")

# incremento de fase por Hz en Q16: inc = (f_q8 * FG_INC_HZ) >> 16
h(f"#define FG_INC_HZ {round(2 ** 40 / SR)}ULL")

d = pathlib.Path(__file__).resolve().parent.parent / "src"
(d / "fg_tables.h").write_text("\n".join(hdr) + "\n")
(d / "fg_tables.c").write_text("\n".join(out) + "\n")
print("tablas ->", d / "fg_tables.h", d / "fg_tables.c")
