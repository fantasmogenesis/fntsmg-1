#!/usr/bin/env python3
"""La caja en el FM-1 desde la computadora, por la consola USB (fgbox.c / console.c):
  fm1.py cmd "fg"                 un comando y su respuesta
  fm1.py shot out.png             captura de la pantalla real (15 bandas por fgshot)
  fm1.py btn PLAY | key 0 | enc K1 3 | tap PLAY    tocar los controles (etiquetas de FB_* / FE_*)
  fm1.py script FILE              una línea por acción: btn/tap/key/enc/wait MS/shot FILE/cmd TEXT
Abre y cierra el puerto en cada comando (no deja screen colgado)."""
import glob, os, sys, termios, time

BTN = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCTDN", "OCTUP"]
ENC = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"]
W2K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]   # blanca i -> tecla
B2K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]                       # negra i -> tecla


def port():
    p = sorted(glob.glob('/dev/cu.usbmodem*'))
    if not p:
        sys.exit("no hay FM-1 en USB")
    fd = os.open(p[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    return fd


def cmd(text, wait=1.0, total=20):
    fd = port()
    os.write(fd, b'\r')
    time.sleep(.15)
    try:
        while os.read(fd, 4096):
            pass
    except BlockingIOError:
        pass
    os.write(fd, text.encode() + b'\r')
    out, t0, last = b'', time.time(), time.time()
    while time.time() - t0 < total and time.time() - last < wait:
        try:
            b = os.read(fd, 65536)
            if b:
                out += b; last = time.time()
                if out.rstrip().endswith(b'>'):
                    break
        except BlockingIOError:
            time.sleep(.01)
    os.close(fd)
    return out.decode('utf-8', 'replace')


def shot(path):
    from PIL import Image
    im = Image.new('RGB', (240, 240))
    px = im.load()
    for band in range(15):
        txt = cmd(f"fgshot {band}", wait=1.0)
        hexs = "".join(l.strip() for l in txt.splitlines() if l.strip() and all(c in "0123456789ABCDEF" for c in l.strip()))
        vals = [int(hexs[i:i + 4], 16) for i in range(0, len(hexs) - 3, 4)][:240 * 16]
        for i, v in enumerate(vals):
            c = ((v & 0xFF) << 8) | (v >> 8)                # (los bytes vienen al revés, como los pide la pantalla)
            px[i % 240, band * 16 + i // 240] = ((c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3)
    im.save(path)
    print("captura ->", path)


def do(a):
    k = a[0]
    if k == "cmd": print(cmd(" ".join(a[1:])))
    elif k == "shot": shot(a[1])
    elif k == "btn": cmd(f"fgb {BTN.index(a[1])} {a[2] if len(a) > 2 else 1}")
    elif k == "tap": cmd(f"fgb {BTN.index(a[1])} 1"); cmd(f"fgb {BTN.index(a[1])} 0")
    elif k == "key": cmd(f"fgk {a[1]} {a[2] if len(a) > 2 else 1}")
    elif k == "white": cmd(f"fgk {W2K[int(a[1])]} {a[2] if len(a) > 2 else 1}")
    elif k == "black": cmd(f"fgk {B2K[int(a[1])]} {a[2] if len(a) > 2 else 1}")
    elif k == "enc": cmd(f"fge {ENC.index(a[1])} {a[2]}")
    elif k == "wait": time.sleep(int(a[1]) / 1000)
    else: sys.exit("? " + k)


if __name__ == "__main__":
    if sys.argv[1] == "script":
        for line in open(sys.argv[2]):
            line = line.split("#")[0].split()
            if line:
                do(line)
    else:
        do(sys.argv[1:])
