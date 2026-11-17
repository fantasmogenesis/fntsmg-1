#!/usr/bin/env python3
"""La caja en el FM-1 desde la computadora, por la consola USB (fgbox.c / console.c):
  fm1.py cmd "fg"                 un comando y su respuesta
  fm1.py shot out.png             captura de la pantalla real (15 bandas por fgshot)
  fm1.py btn PLAY | key 0 | enc K1 3 | tap PLAY    tocar los controles (etiquetas de FB_* / FE_*)
  fm1.py script FILE              una línea por acción: btn/tap/key/enc/wait MS/shot FILE/cmd TEXT
  fm1.py puerto                   qué cu.usbmodem es el FM-1 (por su nombre en ioreg)
  fm1.py respaldo NOMBRE          la zona de proyectos de la flash, leída dos veces -> out/respaldo/
Abre y cierra el puerto en cada comando (no deja screen colgado)."""
import glob, os, sys, termios, time

BTN = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCTDN", "OCTUP"]
ENC = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"]
W2K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]   # blanca i -> tecla
B2K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]                       # negra i -> tecla


def fm1_tty():
    """El cu.usbmodem cuyo dueño en ioreg es el FM-1 (FNTSMG-1, Felucca o FM-1), nunca el primero que aparezca:
    con el SP-404 u otro aparato conectado, el primero puede no ser el FM-1. FM1_TTY=/dev/... lo fuerza."""
    if os.environ.get("FM1_TTY"):
        return os.environ["FM1_TTY"]
    import re, subprocess
    out = subprocess.run(["ioreg", "-r", "-c", "IOUSBHostDevice", "-l", "-w0"], capture_output=True, text=True).stdout
    found = []
    for blk in re.split(r"\n(?=\+-o )", out):
        name = re.search(r'"USB Product Name" = "([^"]*)"', blk)
        for tty in re.findall(r'"IOCalloutDevice" = "(/dev/cu\.usbmodem[^"]*)"', blk):
            found.append((name.group(1) if name else "?", tty))
    mine = [t for n, t in found if re.search(r"fntsmg|felucca|fm-1", n, re.I)]
    if len(mine) == 1:
        return mine[0]
    sys.exit("no encuentro un solo FM-1 por USB (puertos: %s). Usa FM1_TTY=/dev/cu.usbmodem..." % (", ".join("%s=%s" % x for x in found) or "ninguno"))


def port():
    fd = os.open(fm1_tty(), os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
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


def flash_read(fd, a0, a1):
    """La zona [a0, a1) con flr (256 B por llamada, la consola abierta todo el tiempo)."""
    data = bytearray()
    for a in range(a0, a1, 256):
        n = min(256, a1 - a)
        os.write(fd, f"flr 0x{a:X} {n}\r".encode())
        out, t0 = b"", time.time()
        while time.time() - t0 < 5:
            try:
                b = os.read(fd, 65536)
                if b:
                    out += b
                    if out.rstrip().endswith(b">"):
                        break
            except BlockingIOError:
                time.sleep(.003)
        row = bytearray()
        for l in out.decode("ascii", "replace").splitlines():
            l = l.strip()
            if len(l) > 7 and l[6] == ":" and all(c in "0123456789ABCDEF" for c in l[:6]):
                row += bytes(int(h, 16) for h in l[7:].split())
        if len(row) != n:
            sys.exit(f"lectura corta en 0x{a:X}: {len(row)} de {n}")
        data += row
        if (a - a0) % 0x8000 == 0:
            print(f"  0x{a:X}", flush=True)
    return bytes(data)


def respaldo(nombre, a0=0x97000, a1=0xE0000):
    """Dos lecturas de la zona de proyectos (ranuras, autoguardado, banco, TUYOS, ajustes); si son iguales, se guarda."""
    import hashlib
    fd = port()
    os.write(fd, b"\r"); time.sleep(.2)
    try:
        while os.read(fd, 65536):
            pass
    except BlockingIOError:
        pass
    lec = []
    for k in range(2):
        print(f"lectura {k + 1} de 2: 0x{a0:X}..0x{a1:X}", flush=True)
        lec.append(flash_read(fd, a0, a1))
    os.close(fd)
    h = [hashlib.sha256(x).hexdigest() for x in lec]
    if h[0] != h[1]:
        sys.exit("las dos lecturas NO son iguales: no se guarda nada. Repite.")
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out", "respaldo")
    os.makedirs(d, exist_ok=True)
    f = os.path.join(d, f"fm1-zona-{a0:X}-{a1:X}-{nombre}.bin")
    open(f, "wb").write(lec[0])
    print("iguales, sha256", h[0], "->", os.path.abspath(f))


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
    elif k == "puerto": print(fm1_tty())
    elif k == "respaldo": respaldo(a[1])
    else: sys.exit("? " + k)


if __name__ == "__main__":
    if sys.argv[1] == "script":
        for line in open(sys.argv[2]):
            line = line.split("#")[0].split()
            if line:
                do(line)
    else:
        do(sys.argv[1:])
