// Auditoría de perillas: ¿cuánto cambia el sonido cada perilla de punta a punta?
// Para cada motor/modelo/perilla rinde golpes con la perilla en 0, 32, 64, 96 y 127 (las demás en su valor
// inicial) y compara un "retrato" del sonido: energía en 6 bandas x 4 tramos de tiempo, en dB.
// Uso: node tools/knob_audit.mjs lab/motores.wasm
import fs from 'fs';
const bytes = fs.readFileSync(process.argv[2]);
const NOOP = new Proxy({}, { get: () => () => 0 });
const { instance } = await WebAssembly.instantiate(bytes, new Proxy({}, { get: () => NOOP }));
const ex = instance.exports; if (ex._initialize) ex._initialize();
const SR = 44118;

const MOTORES = [
  { e: 0, name: 'BOMBO', models: [
    ['Barrido', ['TONO', 'BARRIDO', 'CAÍDA', 'DOBLEZ', 'RUMBLE', 'CLICK', 'PEGADA', 'NIVEL']],
    ['Membrana', ['TONO', 'TENSIÓN', 'CAÍDA', 'MATERIAL', 'GOLPE', 'RUMBLE', 'PEGADA', 'NIVEL']]] },
  { e: 1, name: 'CAJA', models: [
    ['Caja', ['TONO', 'CUERPO', 'BORDONA', 'CAÍDA', 'BRILLO', 'GOLPE', 'PEGADA', 'NIVEL']],
    ['Aplauso', ['TONO', 'PALMAS', 'SEPARA', 'CAÍDA', 'SALA', 'COLOR', 'PEGADA', 'NIVEL']]] },
  { e: 2, name: 'METAL', models: [
    ['Granizo', ['DENSIDAD', 'CAÍDA', 'COLOR', 'RESO', 'AGITA', 'GRANO', 'PEGADA', 'NIVEL']],
    ['Placa', ['TONO', 'CAÍDA', 'FORMA', 'BRILLO', 'GOLPE', 'BATIDO', 'PEGADA', 'NIVEL']]] },
  { e: 3, name: 'PERC', models: [
    ['Gota', ['TONO', 'GOTA', 'RESONANCIA', 'DUREZA', 'REBOTE', 'MATERIAL', 'PEGADA', 'NIVEL']],
    ['Chatarra', ['TONO', 'CAOS', 'PEINE', 'CAÍDA', 'ROTURA', 'BITS', 'RUIDO', 'NIVEL']]] },
  { e: 4, name: 'SINTE', models: [
    ['Ojo', ['NOTA', 'FILA', 'RECORRIDO', 'PIXEL', 'ATAQUE', 'CAÍDA', 'UNÍSONO', 'NIVEL']],
    ['Ácido', ['NOTA', 'CORTE', 'RESO', 'ENV', 'CAÍDA', 'RUGOSIDAD', 'GLIDE', 'NIVEL']],
    ['Fase rota', ['NOTA', 'FORMA', 'DCW', 'ENV DCW', 'ATAQUE', 'CAÍDA', 'CAOS', 'NIVEL']],
    ['Chops', ['NOTA', 'CORTE', 'LARGO', 'DIRECCIÓN', 'FILTRO', 'BITS', 'CAÍDA', 'NIVEL']],
    ['FM', ['NOTA', 'RAZÓN', 'ÍNDICE', 'FEEDBACK', 'GOLPE', 'CAÍDA', 'RUGIDO', 'NIVEL']],
    ['Acorde', ['NOTA', 'TIPO', 'ABRE', 'FORMA', 'CORTE', 'ENV', 'CAÍDA', 'NIVEL']]] },
];
// sample de prueba para CHOPS: un break de 2 compases a 124 hecho con ruido y senos (22 050 Hz)
function testSample() {
  const sr = 22050, len = Math.round(sr * 60 / 124 * 8), a = new Int16Array(len); let seed = 7;
  const rnd = () => { seed = (seed * 1103515245 + 12345) >>> 0; return seed / 4294967296 * 2 - 1; };
  const hits = [[0, 'k'], [3, 'h'], [4, 's'], [6, 'k'], [8, 'h'], [10, 'k'], [12, 's'], [14, 'h'], [16, 'k'], [19, 'k'], [20, 's'], [22, 'h'], [24, 'k'], [27, 'h'], [28, 's'], [30, 's']];
  const st = sr * 60 / 124 / 4;
  for (const [k, t] of hits) { const o = Math.round(k * st); for (let i = 0; i < st * 3 && o + i < len; i++) { const tt = i / sr; let v = 0;
    if (t === 'k') v = Math.sin(2 * Math.PI * (50 + 120 * Math.exp(-tt * 40)) * tt) * Math.exp(-tt * 9);
    if (t === 's') v = (rnd() * 0.7 + Math.sin(2 * Math.PI * 190 * tt) * 0.4) * Math.exp(-tt * 18);
    if (t === 'h') v = rnd() * Math.exp(-tt * 60) * 0.5;
    a[o + i] = Math.max(-32767, Math.min(32767, a[o + i] + v * 20000)); } }
  return { a, sr };
}
{ const { a, sr } = testSample(); new Int16Array(ex.memory.buffer, ex.lab_sample(), a.length).set(a); ex.lab_sample_set(a.length, sr); }
const BANDS = [[30, 90], [90, 250], [250, 800], [800, 2500], [2500, 7000], [7000, 16000]];
const WINS = [[0, 10], [10, 40], [40, 150], [150, 600]];

function biquadBP(f0) {   // RBJ paso banda, Q 0.9
  const w = 2 * Math.PI * f0 / SR, a = Math.sin(w) / (2 * 0.9), c = Math.cos(w), a0 = 1 + a;
  return { b0: a / a0, b2: -a / a0, a1: -2 * c / a0, a2: (1 - a) / a0 };
}
const FILT = BANDS.map(([lo, hi]) => biquadBP(Math.sqrt(lo * hi)));

function render(e, m, defaults, i, v) {
  const keep = new Int16Array(ex.memory.buffer, ex.lab_sample(), ex.lab_sample_max()).slice();
  ex.lab_init(); new Int16Array(ex.memory.buffer, ex.lab_sample(), keep.length).set(keep); if (globalThis.SMPLEN) ex.lab_sample_set(SMPLEN, 22050);
  ex.lab_fx(6, 0); ex.lab_fx(7, 0); ex.lab_mix(e, 0, 127); ex.lab_model(e, m);
  defaults.forEach((d, k) => ex.lab_param(e, m, k, k === i ? v : d));
  const ahead = ex.lab_ahead ? ex.lab_ahead(e) : 0, pre = ahead ? Math.round(ahead * SR * 60 / 124 / 4) : 0;
  const out = [];
  const pull = n => { for (let o = 0; o < n; o += 128) { const p = ex.lab_render(128), f = new Float32Array(ex.memory.buffer, p, 256); for (let j = 0; j < 128; j++) out.push((f[2 * j] + f[2 * j + 1]) * 0.7071); } };
  if (pre) { ex.lab_pre(e, pre); pull(pre); }
  const t0 = out.length; ex.lab_trig(e, 110, 0, Math.round(SR * 0.4)); pull(SR * 0.6);
  return { sig: out, t0, pre };
}
function portrait({ sig, t0, pre }) {
  const v = [];
  for (const f of FILT) {
    // dos pasabandas en cascada (4.º orden): con uno solo, una fundamental grave y fuerte se cuela a todas las bandas
    let y = Float32Array.from(sig);
    for (let pass = 0; pass < 2; pass++) { let x1 = 0, x2 = 0, y1 = 0, y2 = 0; const z = new Float32Array(y.length);
      for (let n = 0; n < y.length; n++) { const x = y[n], o = f.b0 * x + f.b2 * x2 - f.a1 * y1 - f.a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = o; z[n] = o; }
      y = z; }
    const P = pre / SR * 1000, wins = pre ? [[-P, -P / 4], [-P / 4, 0], ...WINS] : WINS;
    for (const [a, b] of wins) { let s = 0; const i0 = t0 + Math.round(a * SR / 1000), i1 = t0 + Math.round(b * SR / 1000);
      for (let n = i0; n < i1; n++) s += y[n] * y[n]; v.push(Math.max(-80, 10 * Math.log10(s / Math.max(1, i1 - i0) + 1e-12))); }
  }
  // centro de brillo (10..150 ms): dónde está la energía, en octavas x6, para ver cambios de timbre dentro de las bandas
  { const e = []; for (let b = 0; b < BANDS.length; b++) { const db = v[b * (v.length / BANDS.length | 0) + (pre ? 3 : 1)] ?? -80; e.push(Math.pow(10, db / 10)); }
    const tot = e.reduce((x, y) => x + y, 0) || 1e-12; let c = 0; BANDS.forEach(([lo, hi], b) => { c += e[b] * Math.log2(Math.sqrt(lo * hi)); });
    v.push(6 * c / tot); }
  // altura: cruces por cero (log2), 1 octava = 6 "dB" del retrato, para que TONO cuente aunque no cambie de banda
  for (const [a, b] of [[0, 40], [40, 150]]) { let z = 0; const i0 = t0 + Math.round(a * SR / 1000), i1 = t0 + Math.round(b * SR / 1000);
    for (let n = i0 + 1; n < i1; n++) if (sig[n - 1] < 0 && sig[n] >= 0) z++;
    v.push(6 * Math.log2(z + 1)); }
  return v;
}
const dist = (a, b) => a.reduce((s, x, k) => s + Math.abs(x - b[k]), 0) / a.length;

const DEF = JSON.parse(process.argv[3] || 'null');
globalThis.SMPLEN = Math.round(22050 * 60 / 124 * 8);
for (const M of MOTORES) M.models.forEach(([mname, labels], m) => {
  const defaults = (DEF && DEF[M.e] && DEF[M.e][m]) || null;
  if (!defaults) return;
  console.log(`\n${M.name} · ${mname}  (dB: "donde más" = las 3 partes del sonido que más cambian; >10 se siente claro)`);
  labels.forEach((lab, i) => {
    const P = [0, 32, 64, 96, 127].map(v => portrait(render(M.e, m, defaults, i, v)));
    const total = dist(P[0], P[4]), steps = [1, 2, 3, 4].map(k => dist(P[k - 1], P[k]));
    const top = P[0].map((x, k) => Math.abs(x - P[4][k])).sort((a, b) => b - a).slice(0, 3).reduce((s, x) => s + x, 0) / 3;
    const bar = '#'.repeat(Math.min(30, Math.round(top / 2)));
    console.log(`  ${lab.padEnd(10)} medio ${total.toFixed(1).padStart(5)}  donde más ${top.toFixed(1).padStart(5)}  ${bar.padEnd(30)}  tramos ${steps.map(x => x.toFixed(1)).join(' ')}`);
  });
});
