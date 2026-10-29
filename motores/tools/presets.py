#!/usr/bin/env python3
"""Los presets de fábrica de FNTSMG-1: 20 por motor. Aquí se escriben los 7 primeros valores de cada uno (las perillas);
el 8.o, el NIVEL, lo pone este script: toca cada preset en la Mac (tools/presets_oir.c), mide qué tan fuerte suena y lo
iguala al INICIAL de su motor, así cambiar de preset no brinca de volumen. Avisa si alguno queda mudo, no se apaga o
suena casi igual a otro. Escribe src/fg_presets.h (lo incluye fg_box.c).
  python3 tools/presets.py            (desde motores/)
  python3 tools/presets.py --wav DIR  (deja el .wav de cada preset ahí; si no, en la carpeta del oído)"""
import pathlib, subprocess, sys, math

HERE = pathlib.Path(__file__).resolve().parent.parent
BIN = pathlib.Path("/tmp/claude-501/-Volumes-SSD-HUB/e38ca3de-d3fd-42cc-bd46-65b0649159a6/scratchpad/presets_oir")


def n(midi):          # NOTA de los sintes: nota = 24 + p * 61 / 128
    return next(p for p in range(128) if 24 + p * 61 // 128 >= midi)


def sel(k, total):    # el valor que elige la opción k de total (MUESTRA, TIPO, RAZÓN, TIEMPO)
    return min(127, (k * 128 + 64) // total)


KICK = lambda k: sel(k, 10)     # TECHNO 808-LARGO ROTO SUB-DUB CINTA SECO 808-PACK13 PACK13 PACK14 PACK22
TEX = lambda k: sel(k, 11)      # CORO CAMPANAS CINTA VIDRIO MAR-GRAVE METAL-ARCO HIELO TEXTURE-PAD PAD23 PAD25 PAD4
AC = lambda k: sel(k, 9)        # m7 m9 menor mayor sus4 quintas cuartas dim m11
DB = lambda k: sel(k, 8)        # m m7 5+m7 mayor 4+m7 5+9 m+10 sus4
TIEMPO = lambda k: sel(k, 4)    # 1/8 1/8. 1/4 3/8

# (pista o motor, clave en el .h, tipo de motor para el oído, modelo, ¿acorde al oírlo?, [(nombre, [7 valores])])
MOTORES = [
    ("PR_BARRIDO", "K", 0, 0, [
        ("909", [62, 55, 40, 15, 0, 85, 70]), ("808 LARGO", [51, 40, 100, 0, 10, 20, 30]), ("SATURADO", [55, 65, 50, 20, 95, 40, 60]),
        ("DURO", [66, 90, 30, 70, 20, 100, 100]), ("SUB", [43, 25, 85, 0, 0, 10, 25]), ("BOOM", [20, 30, 120, 0, 30, 10, 60]),
        ("PROFUNDO", [8, 35, 95, 0, 15, 10, 25]), ("TECHNO", [50, 70, 45, 15, 60, 75, 70]), ("HOUSE", [56, 60, 50, 10, 30, 60, 60]),
        ("GABBER", [60, 95, 40, 90, 127, 110, 110]), ("DEMBOW", [50, 55, 55, 20, 50, 55, 80]), ("TRAP 808", [38, 30, 120, 5, 40, 20, 40]),
        ("RUMBLE", [45, 45, 110, 30, 80, 30, 50]), ("MINIMAL", [64, 70, 22, 0, 10, 40, 30]), ("CLICK", [75, 100, 15, 0, 0, 127, 20]),
        ("BREAKS", [58, 65, 35, 25, 45, 90, 70]), ("DISTORSIÓN", [52, 60, 60, 60, 110, 70, 90]), ("LO-FI", [44, 30, 60, 60, 90, 0, 40]),
        ("LEJANO", [40, 30, 70, 0, 5, 0, 10]), ("PUNCH", [68, 90, 20, 5, 30, 100, 127])]),
    ("PR_MEMB", "K", 1, 0, [
        ("TOM BAJO", [68, 60, 70, 40, 60, 0, 30]), ("TIMBAL", [97, 80, 55, 85, 90, 0, 40]), ("PIEL", [44, 40, 90, 20, 40, 30, 50]),
        ("TAMBORA", [56, 70, 45, 60, 100, 10, 80]), ("CONGA", [80, 50, 40, 60, 70, 0, 30]), ("GRAVE", [6, 40, 85, 30, 50, 40, 40]),
        ("TOM ALTO", [100, 60, 55, 40, 60, 10, 30]), ("TOM MEDIO", [84, 60, 62, 40, 60, 10, 30]), ("SURDO", [20, 30, 100, 30, 40, 20, 40]),
        ("LEGÜERO", [30, 45, 80, 50, 70, 30, 50]), ("CAJÓN", [50, 70, 35, 70, 90, 20, 60]), ("DJEMBÉ", [70, 50, 40, 80, 110, 10, 40]),
        ("BOMBA", [60, 60, 50, 55, 90, 30, 60]), ("TOM TECHNO", [72, 90, 45, 20, 40, 50, 50]), ("ELECTRO", [64, 110, 40, 10, 30, 70, 40]),
        ("PIEL SUELTA", [40, 10, 90, 30, 30, 10, 20]), ("CUMBIA", [58, 55, 45, 60, 80, 20, 50]), ("GOLPE", [50, 80, 25, 90, 127, 40, 80]),
        ("BOOM BAP", [45, 50, 55, 40, 80, 60, 90]), ("SATURADO", [44, 60, 70, 40, 60, 100, 90])]),
    ("PR_KMUESTRA", "K", 2, 0, [
        ("TECHNO", [64, KICK(0), 127, 127, 20, 0, 40]), ("808 LARGO", [64, KICK(1), 127, 127, 10, 0, 30]), ("ROTO", [64, KICK(2), 127, 127, 30, 0, 50]),
        ("SUB DUB", [64, KICK(3), 127, 127, 10, 0, 30]), ("CINTA", [64, KICK(4), 127, 127, 30, 0, 40]), ("SECO", [64, KICK(5), 127, 127, 20, 0, 40]),
        ("808 PACK", [64, KICK(6), 127, 127, 20, 0, 40]), ("PACK 13", [64, KICK(7), 127, 127, 20, 0, 40]), ("PACK 14", [64, KICK(8), 127, 127, 20, 0, 40]),
        ("PACK 22", [64, KICK(9), 127, 127, 20, 0, 40]), ("808 GRAVE", [52, KICK(1), 127, 110, 30, 0, 40]), ("TECHNO DURO", [66, KICK(0), 70, 127, 90, 0, 100]),
        ("SUB CORTO", [64, KICK(3), 50, 127, 40, 0, 60]), ("SECO ALTO", [76, KICK(5), 60, 127, 40, 0, 70]), ("OSCURO", [60, KICK(7), 110, 40, 30, 0, 40]),
        ("SIN GOLPE", [64, KICK(0), 127, 100, 10, 60, 20]), ("ROTO SATURA", [62, KICK(2), 100, 127, 120, 0, 90]), ("TRAP", [56, KICK(6), 127, 127, 60, 0, 60]),
        ("LO-FI", [58, KICK(9), 90, 50, 70, 0, 60]), ("PUNCH", [64, KICK(8), 80, 127, 60, 0, 127])]),
    ("PR_CAJA", "S", 0, 0, [   # TONO (120..400 Hz) CUERPO RUIDO CAÍDA | BRILLO CHASQUIDO (>90 = aro) PEGADA
        ("909", [50, 80, 90, 50, 70, 60, 80]), ("808", [70, 100, 60, 40, 50, 30, 50]), ("ELECTRO", [30, 100, 110, 45, 110, 85, 110]),
        ("TECHNO", [40, 70, 110, 45, 100, 70, 90]), ("HOUSE", [55, 60, 90, 60, 80, 50, 60]), ("BREAK", [35, 100, 100, 65, 60, 80, 110]),
        ("TRAP", [60, 80, 80, 35, 85, 70, 90]), ("DEMBOW", [70, 70, 95, 30, 70, 85, 110]), ("GORDA", [20, 127, 100, 70, 50, 60, 127]),
        ("PICCOLO", [110, 60, 90, 40, 110, 90, 70]), ("LO-FI", [35, 90, 70, 60, 20, 30, 80]), ("INDUSTRIAL", [30, 100, 127, 70, 110, 100, 127]),
        ("RIMSHOT", [80, 50, 30, 20, 90, 120, 60]), ("CROSS", [95, 20, 10, 15, 100, 127, 40]), ("LARGA", [40, 80, 100, 110, 80, 60, 70]),
        ("CORTA", [50, 80, 90, 20, 80, 70, 80]), ("SUAVE", [45, 60, 60, 50, 40, 20, 20]), ("DUB", [30, 90, 80, 90, 40, 40, 60]),
        ("RUIDO", [50, 30, 127, 70, 127, 60, 60]), ("TOM CAJA", [25, 127, 30, 50, 40, 40, 90])]),
    ("PR_SMUESTRA", "S", 2, 0, None),   # se arma solo con las cajas del banco (firmware/samples/banco.json): ver cajas_muestra()
    ("PR_APLAUSO", "S", 1, 0, [
        ("808", [45, 80, 40, 55, 20, 40, 30]), ("909", [60, 60, 30, 40, 10, 70, 60]), ("SECO", [70, 30, 20, 15, 0, 60, 50]),
        ("SALÓN", [50, 90, 60, 70, 100, 40, 30]), ("CHASQUIDO", [110, 0, 0, 10, 0, 100, 70]), ("LEJANO", [40, 90, 70, 60, 120, 30, 20]),
        ("MUCHAS MANOS", [50, 127, 90, 60, 50, 50, 40]), ("UNA MANO", [60, 0, 0, 30, 0, 60, 60]), ("REGGAETÓN", [52, 90, 50, 30, 20, 55, 90]),
        ("GARAGE", [70, 50, 30, 55, 60, 80, 50]), ("LARGO", [50, 80, 60, 100, 60, 50, 40]), ("OSCURO", [30, 70, 45, 50, 40, 20, 40]),
        ("BRILLANTE", [100, 40, 20, 25, 10, 127, 40]), ("ROTO", [60, 90, 127, 40, 20, 60, 80]), ("IGLESIA", [35, 100, 80, 90, 127, 20, 20]),
        ("TRAP", [62, 40, 15, 20, 5, 80, 110]), ("ESPACIO", [70, 40, 50, 20, 110, 90, 20]), ("PAPEL", [100, 30, 20, 15, 0, 127, 30]),
        ("DUB", [40, 60, 50, 70, 90, 30, 40]), ("SUAVE", [50, 50, 40, 40, 30, 40, 10])]),
    ("PR_GRANIZO", "M", 0, 0, [
        ("CERRADO", [90, 15, 90, 30, 0, 30, 30]), ("ABIERTO", [100, 80, 85, 40, 0, 30, 40]), ("SHAKER", [85, 35, 75, 25, 70, 25, 90]),
        ("LLUVIA", [45, 60, 50, 90, 40, 100, 20]), ("SILBO", [110, 40, 100, 120, 10, 60, 50]), ("TICK", [120, 5, 110, 20, 0, 50, 10]),
        ("808", [95, 20, 95, 50, 0, 20, 40]), ("909 ABIERTO", [105, 90, 90, 30, 0, 30, 50]), ("CORTITO", [110, 5, 100, 30, 0, 40, 30]),
        ("MARACA", [70, 25, 55, 30, 110, 40, 90]), ("ARENA", [60, 50, 60, 20, 60, 80, 20]), ("CHISPAS", [30, 40, 90, 60, 30, 110, 20]),
        ("VAPOR", [120, 110, 80, 20, 0, 10, 20]), ("METÁLICO", [100, 30, 100, 110, 0, 40, 40]), ("LIJA", [90, 40, 50, 60, 40, 70, 30]),
        ("FUEGO", [40, 80, 60, 40, 20, 120, 20]), ("GÜIRO", [75, 40, 60, 50, 100, 60, 90]), ("OSCURO", [90, 30, 40, 40, 0, 30, 40]),
        ("LARGO", [100, 127, 85, 40, 0, 30, 30]), ("RESO", [85, 40, 70, 127, 0, 30, 40])]),
    ("PR_PLACA", "M", 1, 0, [
        ("RIDE", [70, 90, 127, 100, 60, 30, 10]), ("CAMPANA", [80, 85, 64, 70, 80, 10, 0]), ("GONG", [20, 120, 0, 60, 40, 60, 20]),
        ("COWBELL", [60, 35, 70, 50, 100, 20, 60]), ("CHINA", [50, 110, 20, 90, 90, 60, 40]), ("CRASH", [60, 110, 120, 110, 90, 50, 30]),
        ("SPLASH", [80, 60, 115, 110, 80, 40, 30]), ("TRIÁNGULO", [110, 90, 70, 120, 40, 0, 10]), ("AGOGÓ", [90, 40, 64, 80, 90, 10, 50]),
        ("CAMPANITA", [120, 70, 64, 110, 50, 0, 0]), ("GONG CHICO", [45, 90, 10, 60, 60, 40, 20]), ("TAM-TAM", [15, 127, 5, 50, 80, 80, 20]),
        ("HIERRO", [40, 50, 40, 60, 110, 60, 60]), ("CAMPANÓN", [40, 110, 60, 50, 70, 20, 10]), ("RIDE OSCURO", [55, 95, 120, 60, 50, 30, 10]),
        ("PLATO 808", [75, 80, 127, 90, 40, 20, 20]), ("VIBRÁFONO", [70, 100, 50, 80, 30, 90, 0]), ("PLANCHA", [50, 60, 90, 90, 100, 70, 70]),
        ("CAMPANA DUB", [65, 100, 64, 70, 60, 40, 0]), ("CENCERRO", [70, 25, 75, 60, 110, 30, 70])]),
    ("PR_GOTA", "P", 0, 0, [
        ("AGUA", [80, 110, 80, 50, 0, 40, 10]), ("CONGA", [45, 30, 50, 70, 0, 20, 50]), ("CLAVE", [110, 64, 90, 120, 0, 90, 40]),
        ("PELOTA", [60, 20, 60, 60, 110, 50, 20]), ("TABLA", [55, 10, 100, 80, 30, 30, 60]), ("BURBUJA", [70, 120, 60, 40, 90, 30, 20]),
        ("BONGÓ", [85, 30, 50, 80, 0, 20, 50]), ("TIMBAL", [75, 25, 60, 90, 0, 70, 50]), ("MADERA", [90, 64, 40, 100, 0, 50, 40]),
        ("WOODBLOCK", [100, 64, 70, 120, 0, 55, 30]), ("TOM GOMA", [50, 20, 60, 60, 0, 10, 60]), ("GRILLO", [120, 110, 90, 110, 60, 90, 20]),
        ("ZAP", [70, 127, 30, 90, 0, 30, 30]), ("CANICAS", [110, 80, 70, 100, 120, 100, 20]), ("TECLA", [95, 64, 90, 110, 0, 110, 30]),
        ("BALÓN", [40, 30, 40, 50, 110, 10, 40]), ("LÁSER", [80, 127, 100, 127, 0, 80, 10]), ("TAMBOR AGUA", [55, 100, 80, 40, 30, 20, 40]),
        ("CHASQUIDO", [127, 64, 20, 127, 0, 60, 40]), ("GOTERA", [85, 115, 70, 60, 80, 30, 10])]),
    ("PR_CHATARRA", "P", 1, 0, [
        ("FIERRO", [30, 90, 80, 60, 80, 40, 40]), ("TUBO", [50, 30, 30, 70, 30, 10, 10]), ("RUIDO", [60, 127, 100, 30, 127, 90, 100]),
        ("LATA", [80, 60, 110, 20, 60, 60, 30]), ("MÁQUINA", [40, 80, 90, 40, 100, 70, 80]), ("MOTOR", [20, 70, 60, 80, 80, 60, 60]),
        ("CORTO", [60, 60, 60, 10, 60, 40, 40]), ("GLITCH", [80, 100, 40, 20, 60, 110, 30]), ("BITS", [50, 40, 40, 30, 40, 127, 20]),
        ("TUBERÍA", [35, 20, 110, 60, 30, 10, 10]), ("RESORTE", [45, 50, 127, 70, 20, 20, 20]), ("SIERRA", [70, 110, 80, 40, 100, 70, 80]),
        ("ESTÁTICA", [90, 80, 20, 30, 40, 60, 127]), ("PISTÓN", [25, 40, 50, 20, 90, 40, 50]), ("DIGITAL", [65, 90, 30, 40, 20, 100, 0]),
        ("CADENA", [55, 70, 90, 50, 70, 50, 60]), ("TRUENO", [10, 90, 40, 120, 110, 30, 90]), ("CRUJIDO", [75, 50, 60, 15, 110, 80, 70]),
        ("ALARMA", [100, 30, 100, 60, 50, 20, 10]), ("ENGRANE", [40, 60, 70, 25, 60, 70, 50])]),
    # ---- sintes: NOTA primero ----
    ("PR_SH", "Y", 0, 0, [   # NOTA ONDA SUB CORTE | RESO ENV FORMA  (ONDA 0 sierra · 42 cuadrada · 84 pulso 12 % · >84 PWM; SUB >95 = −2 oct; FORMA pluck/bajo/lead/pad cada 32)
        ("BAJO 101", [n(36), 42, 80, 30, 45, 85, 36]), ("ELECTRO", [n(36), 5, 60, 25, 70, 95, 12]), ("ACID 101", [n(36), 0, 0, 20, 100, 100, 40]),
        ("SUB BAJO", [n(36), 42, 115, 20, 10, 30, 55]), ("PWM LEAD", [n(60), 110, 0, 70, 40, 40, 75]), ("BLEEP", [n(72), 42, 0, 95, 20, 0, 6]),
        ("ZAP", [n(48), 0, 0, 10, 110, 127, 14]), ("STAB", [n(48), 0, 30, 45, 60, 90, 18]), ("PLUCK IDM", [n(60), 75, 0, 40, 70, 80, 10]),
        ("BOC", [n(60), 25, 40, 40, 15, 20, 90]), ("PAD 101", [n(48), 100, 50, 50, 20, 30, 108]), ("BARRIDO", [n(48), 0, 30, 15, 80, 110, 120]),
        ("BAJO RESO", [n(36), 60, 70, 30, 90, 70, 40]), ("DETROIT", [n(60), 0, 0, 60, 50, 50, 70]), ("MIAMI", [n(36), 42, 127, 35, 30, 60, 28]),
        ("ARPEGIO", [n(60), 20, 20, 55, 45, 75, 16]), ("SILBIDO", [n(72), 42, 0, 20, 127, 40, 70]), ("ÓRGANO", [n(48), 42, 90, 80, 0, 0, 95]),
        ("GOMA", [n(41), 110, 50, 40, 35, 70, 58]), ("LÁSER", [n(84), 0, 0, 5, 120, 127, 24])]),
    ("PR_ACIDO", "Y", 1, 0, [
        ("303", [20, 30, 100, 90, 40, 0, 30]), ("CUADRADA", [20, 40, 90, 80, 50, 40, 25]), ("SYNC", [25, 50, 80, 70, 60, 110, 10]),
        ("BAJO", [12, 20, 40, 40, 70, 0, 0]), ("CHILLA", [30, 60, 127, 110, 30, 20, 50]), ("LÍQUIDO", [22, 50, 110, 60, 90, 10, 90]),
        ("SQUELCH", [20, 25, 120, 110, 30, 10, 40]), ("SUAVE", [20, 50, 60, 40, 60, 0, 30]), ("PROFUNDO", [8, 20, 90, 60, 40, 0, 20]),
        ("ROTO", [22, 40, 110, 100, 40, 100, 30]), ("GOMA", [20, 30, 80, 70, 90, 20, 60]), ("PIU", [30, 10, 127, 127, 20, 0, 0]),
        ("BAJO SUB", [6, 40, 30, 20, 80, 0, 10]), ("ACID HOUSE", [24, 35, 105, 95, 50, 30, 50]), ("DISTORSIÓN", [18, 45, 100, 80, 50, 127, 30]),
        ("CERRADO", [20, 15, 90, 30, 30, 10, 40]), ("ABIERTO", [20, 90, 70, 30, 90, 10, 30]), ("LARGO", [20, 30, 100, 90, 127, 20, 60]),
        ("ELECTRO", [26, 60, 60, 60, 40, 60, 0]), ("GLIDE", [26, 55, 110, 70, 60, 0, 120])]),
    ("PR_FM", "Y", 2, 0, [
        ("BAJO FM", [20, 20, 50, 30, 80, 50, 40]), ("METAL", [45, 100, 90, 60, 60, 40, 50]), ("RUIDO", [35, 85, 127, 127, 40, 30, 100]),
        ("CAMPANA", [60, 35, 60, 10, 30, 90, 10]), ("PATADA", [10, 20, 70, 40, 127, 40, 80]), ("PIANO E", [50, 30, 40, 10, 30, 90, 0]),
        ("DX BAJO", [18, sel(1, 9), 70, 20, 60, 50, 20]), ("GOLPE FM", [25, sel(3, 9), 90, 40, 100, 30, 60]), ("MARIMBA", [55, sel(6, 9), 50, 0, 40, 50, 0]),
        ("TUBULAR", [60, sel(5, 9), 60, 10, 20, 110, 0]), ("ÓRGANO FM", [45, sel(1, 9), 40, 10, 0, 127, 0]), ("BAJO SUCIO", [15, sel(0, 9), 80, 60, 70, 60, 90]),
        ("LÁSER", [40, sel(7, 9), 127, 30, 127, 30, 30]), ("GONG FM", [30, sel(4, 9), 90, 40, 40, 120, 20]), ("PLUCK", [50, sel(3, 9), 60, 20, 40, 30, 0]),
        ("TECHNO STAB", [35, sel(2, 9), 90, 80, 70, 40, 60]), ("RESO", [30, sel(1, 9), 100, 100, 40, 60, 40]), ("CAMPANITA", [75, sel(8, 9), 40, 0, 20, 70, 0]),
        ("DIGITAL", [40, sel(7, 9), 110, 90, 60, 50, 100]), ("SUB GOLPE", [8, sel(1, 9), 40, 0, 127, 40, 30])]),
    ("PR_CUERDAS", "Y", 3, 1, [
        ("SOLINA", [40, 70, 60, 90, 60, 90, 20]), ("NOCHE", [35, 40, 40, 60, 90, 110, 60]), ("BRILLANTE", [52, 90, 100, 100, 30, 70, 0]),
        ("SUB PAD", [28, 30, 30, 40, 70, 100, 127]), ("LENTO", [45, 60, 70, 80, 127, 127, 30]), ("STAB", [45, 80, 90, 50, 0, 30, 40]),
        ("OMNI", [45, 80, 70, 110, 60, 90, 20]), ("CINTA", [40, 110, 40, 80, 70, 100, 30]), ("CRISTAL", [55, 30, 110, 60, 80, 110, 0]),
        ("GRAVE", [25, 50, 40, 60, 70, 100, 100]), ("ANCHO", [45, 100, 80, 127, 60, 100, 40]), ("SUAVE", [45, 40, 30, 50, 90, 110, 20]),
        ("ÓRGANO", [45, 10, 60, 30, 10, 40, 60]), ("PULSO", [52, 40, 110, 20, 0, 10, 0]), ("DESAFINADO", [35, 127, 45, 60, 40, 70, 50]),
        ("AMANECER", [50, 60, 80, 90, 127, 127, 20]), ("OCHENTAS", [48, 70, 90, 100, 40, 80, 40]), ("OSCURAS", [35, 60, 20, 70, 80, 110, 60]),
        ("SIN CORO", [45, 50, 60, 0, 60, 80, 30]), ("BRILLO LENTO", [55, 80, 127, 110, 110, 127, 0])]),
    ("PR_TEXTURA", "Y", 4, 1, [
        ("CORO", [n(60), TEX(0), 0, 30, 90, 50, 90]), ("CAMPANAS", [n(60), TEX(1), 0, 20, 100, 60, 100]), ("CINTA", [n(60), TEX(2), 0, 50, 70, 30, 80]),
        ("VIDRIO", [n(60), TEX(3), 0, 30, 110, 50, 90]), ("MAR GRAVE", [n(60), TEX(4), 0, 60, 60, 40, 110]), ("METAL ARCO", [n(60), TEX(5), 0, 40, 90, 50, 90]),
        ("HIELO", [n(60), TEX(6), 0, 30, 110, 70, 100]), ("PAD CÁLIDO", [n(60), TEX(7), 0, 30, 80, 40, 90]), ("PAD AIRE", [n(60), TEX(8), 0, 30, 100, 50, 90]),
        ("PAD MAYOR", [n(60), TEX(9), 0, 30, 90, 50, 90]), ("PAD GRAVE", [n(60), TEX(10), 0, 30, 90, 40, 100]), ("CORO LEJANO", [n(60), TEX(0), 60, 60, 50, 110, 120]),
        ("CAMPANA BAJA", [n(48), TEX(1), 30, 30, 80, 80, 110]), ("CINTA ROTA", [n(60), TEX(2), 90, 127, 60, 20, 60]), ("VIDRIO ALTO", [n(72), TEX(3), 40, 20, 127, 60, 90]),
        ("OLEAJE", [n(60), TEX(4), 50, 110, 80, 70, 127]), ("HIELO OSCURO", [n(60), TEX(6), 70, 40, 55, 90, 110]), ("METAL GRAVE", [n(48), TEX(5), 40, 50, 70, 60, 110]),
        ("NIEBLA", [n(60), TEX(8), 90, 80, 60, 127, 127]), ("SUBMARINO", [n(48), TEX(10), 60, 70, 40, 60, 120])]),
    ("PR_DRON", "Y", 5, 1, [
        ("DRON", [40, 50, 30, 40, 60, 60, 30]), ("ÓRGANO", [45, 10, 20, 10, 70, 30, 20]), ("IGLESIA", [45, 15, 15, 15, 70, 60, 0]),
        ("TAMBURA", [35, 30, 40, 60, 80, 40, 20]), ("SIERRAS", [30, 70, 0, 40, 110, 100, 60]), ("PULSO", [40, 40, 110, 40, 60, 40, 30]),
        ("LATIDO LENTO", [35, 90, 30, 40, 50, 60, 20]), ("LATIDO VIVO", [35, 127, 40, 30, 60, 50, 30]), ("GRAVE", [20, 50, 30, 40, 40, 60, 40]),
        ("SUBTERRÁNEO", [10, 40, 60, 60, 30, 40, 60]), ("SATURADO", [35, 60, 40, 40, 80, 60, 120]), ("FÁBRICA", [30, 80, 90, 90, 70, 30, 100]),
        ("NIEBLA", [45, 40, 20, 110, 30, 90, 10]), ("DERIVA", [35, 20, 80, 127, 50, 70, 20]), ("VIDRIO", [60, 30, 100, 60, 110, 60, 10]),
        ("ARMÓNIUM", [45, 20, 50, 20, 80, 20, 30]), ("MOTOR", [25, 110, 80, 20, 60, 20, 90]), ("CORO", [50, 40, 20, 50, 70, 127, 10]),
        ("ÁSPERO", [35, 70, 64, 30, 127, 40, 80]), ("QUIETO", [40, 0, 30, 0, 50, 50, 0])]),
    ("PR_DUB", "Y", 6, 0, [
        ("DUB", [45, DB(0), 60, 50, 70, TIEMPO(1), 40]), ("CHORD DUB", [48, DB(1), 75, 30, 80, TIEMPO(1), 50]), ("CANAL", [40, DB(2), 30, 60, 100, TIEMPO(2), 80]),
        ("STAB SECO", [45, DB(1), 70, 35, 0, TIEMPO(1), 0]), ("ECO LARGO", [45, DB(0), 55, 50, 120, TIEMPO(2), 50]), ("QUINTAS", [40, DB(2), 60, 40, 80, TIEMPO(0), 40]),
        ("MAYOR", [52, DB(3), 90, 25, 60, TIEMPO(0), 20]), ("CUARTAS", [45, DB(4), 50, 40, 80, TIEMPO(2), 40]), ("ABIERTO", [45, DB(5), 80, 50, 70, TIEMPO(1), 30]),
        ("OSCURO", [40, DB(1), 25, 60, 90, TIEMPO(1), 70]), ("CINTA SUCIA", [45, DB(0), 50, 50, 90, TIEMPO(1), 127]), ("TRES OCTAVOS", [45, DB(1), 60, 40, 90, TIEMPO(3), 50]),
        ("CORTO", [45, DB(0), 60, 10, 60, TIEMPO(0), 30]), ("GRAVE", [30, DB(1), 40, 60, 80, TIEMPO(2), 50]), ("BRILLANTE", [55, DB(2), 110, 40, 70, TIEMPO(1), 20]),
        ("PING", [60, DB(7), 90, 15, 100, TIEMPO(0), 30]), ("MENOR 10", [42, DB(6), 50, 55, 100, TIEMPO(3), 60]), ("NOCHE", [38, DB(1), 35, 80, 110, TIEMPO(2), 80]),
        ("LIMPIO", [45, DB(1), 95, 25, 60, TIEMPO(0), 0]), ("SUS", [40, DB(7), 40, 70, 90, TIEMPO(2), 70])]),
    ("PR_RESON", "Y", 7, 0, [
        ("RESONADOR", [52, 40, 80, 30, 70, 20, 30]), ("CUERDA", [45, 0, 70, 0, 60, 10, 0]), ("BARRA", [60, 43, 60, 0, 70, 0, 0]),
        ("CAMPANA", [60, 85, 100, 0, 80, 10, 10]), ("VIDRIO", [70, 127, 90, 0, 110, 10, 0]), ("ARCO", [45, 0, 90, 100, 60, 40, 20]),
        ("ARCO BARRA", [50, 43, 90, 90, 70, 40, 20]), ("DRON CUERDA", [40, 10, 110, 80, 50, 100, 30]), ("SIMPATÍA", [50, 20, 100, 40, 60, 127, 20]),
        ("MARIMBA", [55, 40, 30, 0, 60, 0, 0]), ("GONG", [30, 100, 127, 20, 50, 30, 40]), ("KALIMBA", [65, 55, 40, 0, 90, 0, 0]),
        ("CARILLÓN", [75, 110, 90, 30, 120, 20, 60]), ("TUBO", [40, 64, 90, 40, 40, 30, 10]), ("METAL ARCO", [45, 100, 100, 110, 90, 50, 40]),
        ("CRISTAL", [80, 120, 110, 10, 127, 40, 10]), ("GRAVE", [25, 20, 110, 40, 40, 60, 20]), ("TAMBOR", [35, 70, 20, 0, 30, 0, 0]),
        ("DERIVA", [50, 85, 110, 60, 70, 60, 127]), ("COPA", [70, 127, 127, 70, 100, 80, 30])]),
]
# el INICIAL de cada motor (DEF de su .c): el nivel al que se igualan sus presets
INICIAL = {"PR_BARRIDO": [58, 60, 55, 10, 25, 50, 45, 110], "PR_MEMB": [48, 55, 60, 50, 70, 25, 45, 110], "PR_KMUESTRA": [64, 0, 127, 127, 20, 0, 40, 110],
           "PR_CAJA": [40, 80, 90, 50, 70, 60, 70, 105], "PR_APLAUSO": [55, 70, 45, 50, 30, 55, 40, 105], "PR_SMUESTRA": [64, 0, 127, 127, 20, 0, 40, 105], "PR_GRANIZO": [80, 30, 80, 40, 0, 40, 30, 100],
           "PR_PLACA": [50, 70, 64, 95, 70, 40, 20, 95], "PR_GOTA": [60, 40, 60, 70, 0, 50, 30, 100], "PR_CHATARRA": [40, 60, 60, 40, 60, 30, 30, 100],
           "PR_SH": [26, 20, 60, 35, 60, 75, 40, 100], "PR_ACIDO": [20, 35, 95, 85, 45, 0, 25, 100], "PR_FM": [30, 49, 60, 50, 70, 50, 50, 95],
           "PR_CUERDAS": [40, 60, 70, 70, 50, 90, 30, 92], "PR_TEXTURA": [76, 0, 0, 30, 90, 50, 90, 95], "PR_DRON": [40, 50, 30, 40, 60, 60, 30, 95],
           "PR_DUB": [45, 0, 60, 50, 70, 40, 40, 95], "PR_RESON": [52, 40, 80, 30, 70, 20, 30, 95]}
def cajas_muestra():
    """MUESTRA de la caja: cada caja del banco tal cual y, hasta llegar a 20, variaciones (TONO MUESTRA CAÍDA FILTRO | SATURA ARRANQUE PEGADA)."""
    import json
    banco = json.loads((HERE.parent / "firmware" / "samples" / "banco.json").read_text()).get("cajas", [])
    if not banco: return [("TAL CUAL", [64, 0, 127, 127, 20, 0, 40])]
    N = len(banco); corto = lambda nm: nm.replace("CAJA ", "")
    out = [(corto(e["nombre"])[:12], [64, sel(k, N), 127, 127, 20, 0, 40]) for k, e in enumerate(banco)]
    VAR = [("GRAVE", [52, None, 127, 110, 30, 0, 50]), ("CORTA", [66, None, 55, 127, 40, 0, 70]), ("OSCURA", [60, None, 110, 45, 30, 0, 40]),
           ("SATURA", [64, None, 127, 127, 110, 0, 90]), ("AGUDA", [76, None, 80, 127, 30, 0, 50]), ("SECA", [64, None, 40, 100, 60, 0, 100])]
    i = 0
    while len(out) < 20:
        k = (i * 3) % N; vn, vp = VAR[i % len(VAR)]          # (cada variación sobre otra caja)
        out.append((f"{corto(banco[k]['nombre']).split()[0]} {vn}"[:12], [vp[0], sel(k, N)] + vp[2:])); i += 1
    return out


PRUEBA = 100   # el NIVEL con que se oye cada preset para medirlo
UMBRAL = 3.0   # dB de diferencia promedio en la huella: abajo de esto, dos presets suenan casi igual


def oir(lineas, wavdir=None):
    r = subprocess.run([str(BIN)] + ([str(wavdir)] if wavdir else []), input="".join(lineas), capture_output=True, text=True, check=True)
    return [tuple(float(x) for x in l.split()[1:]) for l in r.stdout.splitlines()]


def main():
    if not BIN.exists() or BIN.stat().st_mtime < max(p.stat().st_mtime for p in [*(HERE / "src").glob("fg_*.c"), HERE / "tools" / "presets_oir.c"]):
        src = HERE / "src"
        subprocess.run(["cc", "-O2", "-I" + str(src), "-o", str(BIN), str(HERE / "tools" / "presets_oir.c")] +
                       [str(src / f"fg_{x}.c") for x in ("kick", "snare", "metal", "perc", "synth", "smp", "samples", "tables")] + ["-lm"], check=True)
    wavdir = pathlib.Path(sys.argv[sys.argv.index("--wav") + 1]) if "--wav" in sys.argv else BIN.parent / "presets_wav"
    wavdir.mkdir(parents=True, exist_ok=True)
    lineas, quien = [], []
    for key, kind, model, chord, lista in MOTORES:
        if lista is None: lista = cajas_muestra()
        assert len(lista) == 20 or key == "PR_SMUESTRA", f"{key}: {len(lista)} presets"
        nombres = [nm for nm, _ in lista]
        assert len(set(nombres)) == len(nombres), f"{key}: nombres repetidos"
        for nm, p in lista:
            assert len(p) == 7 and all(0 <= v <= 127 for v in p) and len(nm) <= 12, (key, nm)
        for nm, p in [("INICIAL", INICIAL[key][:7])] + lista:
            lineas.append(f"{kind} {model} {' '.join(map(str, p))} {PRUEBA} {chord}\n"); quien.append((key, nm, p))
    res = oir(lineas, wavdir)
    avisos, salida, ref = [], {}, {}
    for (key, nm, p), (pico, rms, largo, brillo) in zip(quien, res):
        kind = next(k for kk, k, *_ in MOTORES if kk == key)
        if nm == "INICIAL":   # su nivel con el NIVEL de verdad del INICIAL
            ref[key] = rms + 40 * math.log10(INICIAL[key][7] / PRUEBA); continue
        if rms < -60: avisos.append(f"{key} {nm}: casi mudo ({rms:.0f} dB)")
        nivel = round(PRUEBA * 10 ** ((ref[key] - rms) / 40))
        if nivel > 127: avisos.append(f"{key} {nm}: le falta volumen aun con NIVEL 127 ({ref[key] - rms - 40 * math.log10(127 / PRUEBA):+.1f} dB)")
        nivel = max(40, min(127, nivel))
        pk_final = pico + 40 * math.log10(nivel / PRUEBA)
        if pk_final > 6: avisos.append(f"{key} {nm}: pico alto ({pk_final:+.1f} dBFS con NIVEL {nivel})")
        if kind != "Y" and largo > 1950: avisos.append(f"{key} {nm}: no se apaga en 2 s")
        corto = 200 if kind == "Y" else 0 if key == "PR_GRANIZO" else 60   # (los hats sí pueden ser un tic)
        if largo < corto: avisos.append(f"{key} {nm}: dura {largo:.0f} ms, suena a clic")
        salida.setdefault(key, []).append((nm, p + [nivel]))
    import numpy as np, scipy.io.wavfile as wf   # repetidos: el espectro (tercios de octava) por tramos de tiempo, sin contar el volumen
    def huella(i):
        r, x = wf.read(str(wavdir / f"{i:03d}.wav")); x = x.astype(float)
        perc = next(k for kk, k, *_ in MOTORES if kk == quien[i][0]) != "Y"
        out = []
        for t0, t1 in (((0, .02), (.02, .06), (.06, .15), (.15, .4), (.4, 1), (1, 2)) if perc else ((0, .1), (.1, .4), (.4, 1), (1, 2))):
            seg = x[int(t0 * r):int(t1 * r)]
            X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / r)
            ed = np.geomspace(40, 16000, 27)                         # tercios de octava
            out += [10 * np.log10(X[(f >= ed[k]) & (f < ed[k + 1])].sum() + 1e-3) for k in range(26)]
        v = np.array(out); v = np.maximum(v - v.max(), -60)
        if perc: return v
        seg = x[int(.05 * r):int(1 * r)]                  # y las 12 notas (la armonía: un mayor y un sus4 tienen las mismas bandas)
        X = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2; f = np.fft.rfftfreq(len(seg), 1 / r)
        m = (f > 50) & (f < 5000); pc = np.round(12 * np.log2(f[m] / 440)).astype(int) % 12
        c = np.array([X[m][pc == k].sum() for k in range(12)]) + 1e-3
        c = np.maximum(10 * np.log10(c / c.max()), -40)
        return np.concatenate([v, c])
    H = [huella(i) for i in range(len(quien))]
    for key in salida:
        idx = [i for i, q in enumerate(quien) if q[0] == key and q[1] != "INICIAL"]
        for a_ in range(len(idx)):
            for b_ in range(a_ + 1, len(idx)):
                ha, hb = H[idx[a_]], H[idx[b_]]; vivo = np.maximum(ha, hb) > -45   # solo donde alguno suena
                d = float(np.mean(np.abs(ha - hb)[vivo]))
                if d < UMBRAL: avisos.append(f"{key}: {quien[idx[a_]][1]} y {quien[idx[b_]][1]} se parecen (diferencia {d:.1f} dB)")
    h = ["/* Generado por motores/tools/presets.py (los valores y el NIVEL igualado al INICIAL de cada motor): no editar a mano. */"]
    for key, lista in salida.items():
        h.append(f"static const preset_t {key}[] = {{")
        h.append(",\n".join("    {\"%s\", {%s}}" % (nm, ", ".join(map(str, p))) for nm, p in lista))
        h.append("};")
    (HERE / "src" / "fg_presets.h").write_text("\n".join(h) + "\n")
    for a in avisos: print("aviso:", a)
    print(f"{sum(len(v) for v in salida.values())} presets -> src/fg_presets.h")


if __name__ == "__main__":
    main()
