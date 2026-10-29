#!/usr/bin/env python3
"""El banco de samples de FNTSMG-1: lee samples/banco.json, convierte cada sample (ffmpeg: mono, su tasa, recorte,
normalizado, fundido; las texturas con su lazo cruzado para que no truene) y lo comprime en ADPCM IMA por bloques de
64 muestras (4 bytes de cabecera: la muestra anterior y el índice; 32 de nibbles), así se puede saltar a cualquier
bloque. Escribe motores/src/fg_samples.c (los datos van a la flash) y dice cuánto ocupa.
  banco.json: {"bombos": [{"nombre", "archivo", "inicio", "largo", "tasa"}], "cajas": [...], "texturas": [{..., "nota": 60}]}
  "tasa" (opcional) baja la frecuencia de muestreo de un sample oscuro para que quepan más; el motor la compensa al tocarlo.
  Uso: gen_samples.py [spec.json]   (por omisión samples/banco.json)"""
import json, pathlib, subprocess, sys, array

HERE = pathlib.Path(__file__).resolve().parent.parent
OUT = HERE.parent / "motores" / "src" / "fg_samples.c"
STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
        143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
        1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]
BLK = 64


def leer(arch, rate, ini, largo):
    cmd = ["ffmpeg", "-v", "error", "-ss", str(ini), "-i", str(arch)]
    if largo: cmd += ["-t", str(largo)]
    cmd += ["-ac", "1", "-ar", str(rate), "-f", "s16le", "-"]
    a = array.array("h"); a.frombytes(subprocess.run(cmd, capture_output=True, check=True).stdout)
    return list(a)


def normal(x, pico=0.89):
    m = max(1, max(abs(v) for v in x))
    g = pico * 32767 / m
    return [int(max(-32767, min(32767, v * g))) for v in x]


def adpcm(x):
    x = x + [0] * ((-len(x)) % BLK)
    out, prev, idx = bytearray(), 0, 0
    for b in range(0, len(x), BLK):
        out += int(prev).to_bytes(2, "little", signed=True) + bytes([idx, 0])
        pred, nib = prev, []
        for s in x[b:b + BLK]:
            st = STEP[idx]; d = s - pred; code = 0
            if d < 0: code = 8; d = -d
            if d >= st: code |= 4; d -= st
            if d >= st >> 1: code |= 2; d -= st >> 1
            if d >= st >> 2: code |= 1
            st = STEP[idx]; dq = st >> 3
            if code & 4: dq += st
            if code & 2: dq += st >> 1
            if code & 1: dq += st >> 2
            pred = max(-32768, min(32767, pred - dq if code & 8 else pred + dq))
            idx = max(0, min(88, idx + IDX[code & 7]))
            nib.append(code)
        for i in range(0, BLK, 2): out.append(nib[i] | nib[i + 1] << 4)
        prev = pred
    return bytes(out), len(x)


def main():
    spec = json.loads(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else HERE / "samples" / "banco.json").read_text())
    c = ['/* Generado por firmware/tools/gen_samples.py desde firmware/samples/banco.json: no editar a mano. */',
         '#include "fg_samples.h"', '']
    tablas, total = {"bombos": [], "cajas": [], "texturas": []}, 0
    for grupo, base in (("bombos", 44100), ("cajas", 32000), ("texturas", 22050)):
        for k, e in enumerate(spec.get(grupo, [])):
            rate = e.get("tasa", base)
            x = leer(e["archivo"], rate, e.get("inicio", 0), e.get("largo", 0))
            if grupo != "texturas":
                f = int(rate * 0.004)                      # fundido corto al final
                x = normal(x[:len(x)])
                for i in range(f): x[-1 - i] = x[-1 - i] * i // f
                loop = 0; ls = le = 0
            else:
                x = normal(x, 0.8)
                xf = min(len(x) // 4, int(rate * 0.25))    # lazo: el final se funde con el principio
                body, tail = x[:len(x) - xf], x[len(x) - xf:]
                for i in range(xf): body[i] = (body[i] * i + tail[i] * (xf - i)) // xf
                x = body; loop = 1
            data, n = adpcm(x)
            if loop: ls, le = 0, n
            nm = f"S_{grupo[0]}{k}"
            c.append(f"static const uint8_t {nm}[{len(data)}] = {{" + ",".join(map(str, data)) + "};")
            tablas[grupo].append(f'{{{nm}, {n}u, {ls}u, {le}u, {rate}u, {e.get("nota", 60)}, {loop}, "{e["nombre"]}"}}')
            total += len(data)
            print(f"{grupo:8s} {e['nombre']:16s} {n / rate:5.2f} s  {rate:5d} Hz  {len(data) / 1024:6.1f} KB")
    for grupo, sym in (("bombos", "FG_SMP_KICK"), ("cajas", "FG_SMP_SNR"), ("texturas", "FG_SMP_TEX")):
        t = tablas[grupo] or ['{0, 0, 0, 0, 44100u, 60, 0, "-"}']
        c.append(f"const fg_sample_t {sym}[] = {{" + ", ".join(t) + "};")
        c.append(f"const unsigned {sym}_N = {len(tablas[grupo])}u;")
    OUT.write_text("\n".join(c) + "\n")
    print(f"total {total / 1024:.1f} KB -> {OUT}")


if __name__ == "__main__":
    main()
