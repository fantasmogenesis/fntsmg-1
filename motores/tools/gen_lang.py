#!/usr/bin/env python3
"""Los textos de la pantalla en inglés y portugués: lee motores/tools/lang.tsv (español TAB inglés TAB portugués, una
línea por texto; # comenta) y escribe motores/src/fg_lang_tab.h, ordenado por el hash FNV-1a del español.
  python3 motores/tools/gen_lang.py           escribe la tabla
  python3 motores/tools/gen_lang.py --falta   los textos de fg_box.c / fg_box_draw.c que no están en la tabla"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "src"
TSV = HERE / "lang.tsv"


def fnv(b):
    h = 2166136261
    for c in b:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return h


def rows():
    out = []
    for n, ln in enumerate(TSV.read_text().splitlines(), 1):
        if not ln.strip() or ln.startswith("#"):
            continue
        p = ln.split("\t")
        if len(p) != 3 or not all(x.strip() for x in p):
            sys.exit(f"lang.tsv:{n}: hacen falta tres columnas")
        out.append(tuple(x.strip() for x in p))
    return out


def pieces(s):
    """lo que fg_tr busca de un texto: el texto entero, cada tramo entre · y cada pedazo sin números"""
    if s.startswith("\\1"):
        return []
    out = [s.strip()]
    for seg in s.split("·"):
        seg = seg.strip()
        out.append(seg)
        out += [w.strip() for w in re.split(r"[0-9]+", seg)]
    return [w for w in out if re.search(r"[A-ZÁÉÍÓÚÑ]{2}", w)]


def falta(tab):
    have = {r[0] for r in tab}
    seen = []
    for f in ("fg_box.c", "fg_box_draw.c"):
        for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', (SRC / f).read_text()):
            if not re.search(r"[A-ZÁÉÍÓÚÑ]{2}", lit) or lit.startswith("\\1"):
                continue
            ps = pieces(lit)
            if ps and not any(p in have for p in ps[:1]) and not all(p in have for p in ps[1:] if p):
                seen.append(lit)
    for s in sorted(set(seen)):
        print(s)


def main():
    tab = rows()
    if "--falta" in sys.argv:
        falta(tab)
        return
    by = {}
    for es, en, pt in tab:
        h = fnv(es.encode())
        if h in by and by[h][0] != es:
            sys.exit(f"choque de hash: {es} / {by[h][0]}")
        by[h] = (es, en, pt)
    pool, off, idx = bytearray(), {}, []
    for h in sorted(by):
        es, en, pt = by[h]
        o = []
        for t in (en, pt):
            if t not in off:
                off[t] = len(pool)
                pool += t.encode() + b"\0"
            o.append(off[t])
        idx.append((h, o))
    if len(pool) > 65535:
        sys.exit("la tabla pasa de 64 KB")
    esc, hexed, nul = [], False, False
    for c in pool:                                   # (tras un \x o un \0, un dígito pegado se volvería parte del escape)
        ch = chr(c)
        if (hexed and ch in "0123456789abcdefABCDEF") or (nul and ch in "01234567"):
            esc.append('" "')
        hexed, nul = c >= 0x80, c == 0
        esc.append(f"\\x{c:02x}" if hexed else "\\0" if not c else "\\" + ch if ch in '"\\' else ch)
    L = ["/* generado por motores/tools/gen_lang.py desde lang.tsv: no editar */", "#pragma once",
         f"#define FG_LANG_N {len(idx)}",
         "static const uint32_t FG_LH[" + str(max(1, len(idx))) + "] = {" + ",".join(f"0x{h:08x}u" for h, _ in idx) + "};",
         "static const uint16_t FG_LO[" + str(max(1, len(idx))) + "][2] = {" + ",".join(f"{{{a},{b}}}" for _, (a, b) in idx) + "};",
         'static const char FG_LS[] = "' + "".join(esc) + '";']
    (SRC / "fg_lang_tab.h").write_text("\n".join(L) + "\n")
    print(f"{len(idx)} textos, {len(pool)} B de texto, {len(idx) * 8} B de índice -> {SRC / 'fg_lang_tab.h'}")


main()
