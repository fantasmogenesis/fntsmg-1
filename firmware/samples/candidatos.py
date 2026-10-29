#!/usr/bin/env python3
"""Candidatos para el banco de FNTSMG-1: bombos y texturas de la librería de Cris (recortados) y otros hechos aquí
con procesos que el FM-1 no puede correr en vivo (reverbs de convolución largas, capas, cinta, síntesis modal fina,
granular). Escribe candidatos/<grupo>/<nombre>.wav y candidatos/candidatos.json (de dónde salió cada uno).
Las texturas salen de 3.4 s: gen_samples.py funde el final con el principio para el lazo."""
import json, pathlib, subprocess
import numpy as np
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "candidatos"
SR = 44100
LIB = pathlib.Path("/Volumes/PRODUCCION/Samples, extensiones, máquinas, etc/librerías")
rng = np.random.default_rng(7)


def wav(path, x, sr=SR):
    x = np.asarray(x, dtype=np.float64)
    x = x / (np.max(np.abs(x)) + 1e-9) * 0.89
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f64le", "-ar", str(sr), "-ac", "1", "-i", "-", str(path)],
                   input=x.astype("<f8").tobytes(), check=True)


def desde_lib(rel, path, ini=0.0, largo=0.7):
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(ini), "-t", str(largo), "-i", str(LIB / rel), "-ac", "1", "-ar", str(SR), str(path)], check=True)


def t_(dur):
    return np.arange(int(dur * SR)) / SR


def lp(x, fc, order=2):
    b, a = signal.butter(order, fc / (SR / 2))
    return signal.lfilter(b, a, x)


def hp(x, fc, order=2):
    b, a = signal.butter(order, fc / (SR / 2), "high")
    return signal.lfilter(b, a, x)


def reverb(x, dur=3.5, tone=4000.0, mix=0.7, pre=0.02):
    """convolución con una cola de ruido que cae (y se oscurece al caer): sala enorme, sin metal"""
    n = int(dur * SR)
    tt = np.arange(n) / SR
    ir = rng.standard_normal(n) * np.exp(-6.9 * tt / dur)
    ir = lp(ir, tone) * 0.6 + lp(ir, tone / 4) * 0.4
    ir[: int(pre * SR)] = 0
    ir /= np.sqrt(np.sum(ir ** 2)) + 1e-9
    wet = signal.fftconvolve(x, ir)[: len(x)]
    return (1 - mix) * x / (np.max(np.abs(x)) + 1e-9) + mix * wet / (np.max(np.abs(wet)) + 1e-9)


def saw(f, tt, ph0=0.0):
    ph = np.cumsum(np.broadcast_to(f, tt.shape) / SR) + ph0
    return 2 * (ph % 1.0) - 1


# ------------------------------------------------------------------ bombos generados
def bombo(f0, f1, kf, ka, dur=0.6, drive=1.0, click=0.0, crush=0, lowpass=0.0, wow=0.0):
    tt = t_(dur)
    f = f0 + f1 * np.exp(-tt / kf)
    if wow: f = f * (1 + wow * np.sin(2 * np.pi * 3 * tt))
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt / ka)
    if click:
        c = hp(rng.standard_normal(len(tt)), 2000) * np.exp(-tt / 0.003) * click
        x = x + c
    x = np.tanh(drive * x) / np.tanh(drive)
    if crush: x = np.round(x * (2 ** crush)) / 2 ** crush
    if lowpass: x = lp(x, lowpass)
    fade = int(0.005 * SR)
    x[-fade:] *= np.linspace(1, 0, fade)
    return x


GEN_BOMBOS = {
    "TECHNO": bombo(45, 180, 0.03, 0.35, 0.55, drive=2.0, click=0.6),
    "808 LARGO": bombo(48, 60, 0.02, 1.2, 0.7, drive=1.2),
    "ROTO": bombo(55, 300, 0.015, 0.3, 0.5, drive=6.0, lowpass=4000),
    "SUB DUB": bombo(40, 40, 0.05, 0.8, 0.7, drive=1.0, lowpass=900),
    "CINTA": bombo(60, 120, 0.02, 0.25, 0.45, drive=2.5, crush=7, lowpass=3000, wow=0.01),
    "SECO": bombo(70, 250, 0.01, 0.12, 0.3, drive=1.5, click=1.0),
}


# ---------------------------------------------------------------- texturas generadas
def coro():
    tt = t_(3.4)
    x = np.zeros_like(tt)
    for i, n in enumerate([60, 63, 67, 72]):                     # do menor con octava
        f = 440 * 2 ** ((n - 69) / 12) * (1 + 0.004 * np.sin(2 * np.pi * (4.6 + i * 0.3) * tt + i))
        x += saw(f, tt, rng.random()) * (0.8 if i else 1)
    v = np.zeros_like(x)                                          # vocal: tres formantes que pasan de "a" a "o"
    m = 0.5 + 0.5 * np.sin(2 * np.pi * 0.15 * tt)
    for fa, fo, g in ((800, 500, 1.0), (1150, 900, 0.6), (2900, 2500, 0.25)):
        for f in (fa, fo):
            b, a = signal.iirpeak(f, 6, SR)
            w = m if f == fo else 1 - m
            v += signal.lfilter(b, a, x) * g * w
    return reverb(v, 3.5, 5000, 0.6)


def campanas():
    tt = t_(3.4)
    x = np.zeros_like(tt)
    notes = [60, 63, 67, 70, 72, 75, 79]
    for k in range(10):
        st = int(rng.uniform(0, 3.0) * SR)
        f = 440 * 2 ** ((rng.choice(notes) + 12 - 69) / 12)
        n = len(tt) - st
        tk = np.arange(n) / SR
        mod = np.sin(2 * np.pi * f * 3.5 * tk) * 2.5 * np.exp(-tk / 0.4)
        x[st:] += np.sin(2 * np.pi * f * tk + mod) * np.exp(-tk / 1.8) * rng.uniform(0.4, 1)
    r = reverb(x, 4.5, 6000, 0.8)
    oct_ = signal.resample_poly(r, 1, 2)                           # brillo una octava arriba (shimmer), en lazo
    oct_ = np.tile(oct_, 2)[: len(r)]
    return r + 0.35 * reverb(oct_, 4.0, 7000, 1.0)


def cinta():
    tt = t_(3.4)
    wow = 1 + 0.006 * np.sin(2 * np.pi * 0.5 * tt) + 0.002 * np.sin(2 * np.pi * 7 * tt)
    x = np.zeros_like(tt)
    for n in (48, 55, 58, 62, 67):
        for d in (-0.004, 0.004):
            x += saw(440 * 2 ** ((n - 69) / 12) * (1 + d) * wow, tt, rng.random())
    x = lp(x, 1200, 4)
    x = np.tanh(1.8 * x / np.max(np.abs(x))) + 0.01 * lp(rng.standard_normal(len(tt)), 6000)
    return reverb(x, 2.0, 3000, 0.35)


def vidrio():
    tt = t_(3.4)
    x = np.zeros_like(tt)
    f0 = 261.63
    for k in range(1, 18):
        f = f0 * k * (1 + 0.0015 * k * k)                          # un poco estirado: vidrio, no órgano
        env = 0.5 + 0.5 * np.sin(2 * np.pi * rng.uniform(0.05, 0.4) * tt + rng.uniform(0, 6.28))
        x += np.sin(2 * np.pi * f * tt) * env ** 3 / k ** 0.6
    return reverb(x, 4.0, 8000, 0.65)


def mar():
    tt = t_(3.4)
    x = np.zeros_like(tt)
    for n, a in ((36, 1.0), (48, 0.7), (55, 0.5), (60, 0.3), (67, 0.2)):
        f = 440 * 2 ** ((n - 69) / 12)
        for d in (-0.003, 0.0, 0.0035):
            x += np.sin(2 * np.pi * f * (1 + d) * tt + rng.uniform(0, 6.28)) * a
    swell = 0.7 + 0.3 * np.sin(2 * np.pi * 0.3 * tt)
    return reverb(x * swell, 3.0, 2500, 0.5)


def metal():
    tt = t_(3.4)
    bow = lp(rng.standard_normal(len(tt)), 3000) * (0.6 + 0.4 * np.sin(2 * np.pi * 0.25 * tt))
    x = np.zeros_like(tt)
    f0 = 196.0
    for k in range(30):
        ratio = (k + 1) ** 1.7 * (1 + rng.uniform(-0.02, 0.02))
        f = f0 * ratio
        if f > 9000: break
        b, a = signal.iirpeak(f, 400, SR)                          # modo muy angosto: metal que canta
        x += signal.lfilter(b, a, bow) / (k + 1) ** 0.5
    return reverb(x, 4.0, 5000, 0.55)


def polvo():
    tt = t_(3.4)
    x = np.zeros_like(tt)
    for n in (60, 64, 67, 71):
        x += saw(440 * 2 ** ((n - 69) / 12) * (1 + 0.003 * np.sin(2 * np.pi * 0.3 * tt)), tt, rng.random())
    x = lp(x, 900, 4) / 4
    crack = np.zeros_like(tt)
    idx = rng.integers(0, len(tt), 90)
    crack[idx] = rng.uniform(-1, 1, 90)
    crack = hp(crack, 1500) * 3 + 0.02 * lp(rng.standard_normal(len(tt)), 5000)
    return reverb(x, 2.5, 3000, 0.45) + 0.25 * crack


def hielo():
    tt = t_(3.4)
    src_t = np.arange(int(1.5 * SR)) / SR
    bell = np.sin(2 * np.pi * 523.25 * src_t + 1.5 * np.sin(2 * np.pi * 523.25 * 2.76 * src_t) * np.exp(-src_t / 0.5)) * np.exp(-src_t / 0.9)
    x = np.zeros(len(tt))
    for _ in range(420):                                           # granos largos y suaves: el tiempo se estira
        L = int(rng.uniform(0.05, 0.15) * SR)
        a = int(rng.uniform(0, len(bell) - L - 1))
        g = bell[a:a + L] * np.hanning(L)
        if rng.random() < 0.3: g = signal.resample_poly(g, 1, 2)   # algunos una octava arriba
        st = int(rng.uniform(0, len(x) - len(g) - 1))
        x[st:st + len(g)] += g * rng.uniform(0.3, 1.0)
    return reverb(x, 3.5, 7000, 0.6)


GEN_TEX = {"CORO": coro, "CAMPANAS LEJANAS": campanas, "CINTA": cinta, "VIDRIO": vidrio, "MAR GRAVE": mar,
           "METAL ARCO": metal, "POLVO": polvo, "HIELO": hielo}

O = "LIBRERÍAS ORGANIZADAS/Librería 1/PACKS/"
D = "LIBRERÍAS SIN ORGANIZAR/"
LIB_BOMBOS = {
    "808 PACK 13": O + "PACK 13/KICK 808.wav", "PACK 13": O + "PACK 13/KICK.WAV", "PACK 14": O + "PACK 14/KICK 1.wav",
    "PACK 22": O + "PACK 22/KICK.wav", "PACK 24": O + "PACK 24/KICK.wav", "PACK 1": O + "Pack 1/KICK.wav",
    "TTA 1": D + "Telefon Tel Aviv Kit Samples/kick 1.wav", "TTA 7": D + "Telefon Tel Aviv Kit Samples/kick 7.wav",
    "909": D + "Drum Machine Archive 1 2/SH09 Drum sounds/909bass3.wav", "DR-110": D + "Drum Machine Archive 1 2/Boss DR-110/DR-110Kick.wav",
}
MOVE = "Ableton Move/Samples/Pads/"
LIB_TEX = {
    "TEXTURE PAD": O + "PACK 24/TEXTURE PAD.wav", "PAD 23": O + "PACK 23/PAD.wav", "PAD 25": O + "PACK 25/PAD.wav", "PAD 4": O + "PACK 4/PAD.wav",
    "MOVE PAD 3": MOVE + "Pads_text 3 [2026-08-12 193324].wav", "MOVE PAD 7": MOVE + "Pads_text 7 [2026-08-12 193323].wav",
    "MOVE PAD 11": MOVE + "Pads_text 11 [2026-08-12 193322].wav", "MOVE PAD 15": MOVE + "Pads_text 15 [2026-08-12 193327].wav",
}


def main():
    man = []
    for nm, x in GEN_BOMBOS.items():
        p = OUT / "bombos" / f"{nm}.wav"; wav(p, x); man.append({"grupo": "bombos", "nombre": nm, "archivo": str(p), "origen": "generado"})
    for nm, rel in LIB_BOMBOS.items():
        p = OUT / "bombos" / f"{nm}.wav"; desde_lib(rel, p, 0, 0.6); man.append({"grupo": "bombos", "nombre": nm, "archivo": str(p), "origen": "librería: " + rel})
    for nm, fn in GEN_TEX.items():
        p = OUT / "texturas" / f"{nm}.wav"; wav(p, fn()); man.append({"grupo": "texturas", "nombre": nm, "archivo": str(p), "origen": "generado", "nota": 60})
        print("hecha", nm)
    for nm, rel in LIB_TEX.items():
        p = OUT / "texturas" / f"{nm}.wav"; desde_lib(rel, p, 0.3, 3.4); man.append({"grupo": "texturas", "nombre": nm, "archivo": str(p), "origen": "librería: " + rel, "nota": 60})
    (OUT / "candidatos.json").write_text(json.dumps(man, ensure_ascii=False, indent=1))
    print(len(man), "candidatos en", OUT)


if __name__ == "__main__":
    main()
