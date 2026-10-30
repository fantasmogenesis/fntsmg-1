// Qué hace cada perilla en cada página (lo mismo que target() y tfmt() de fg_box.c, con la numeración v7 del
// PROTOCOLO): nombre, rango, dónde vive en el proyecto y cómo se lee el valor. Lo usan la interfaz y el aparato de
// demostración, así los dos mueven los mismos bytes. Con el aparato conectado, los nombres de META reemplazan a éstos.
import D from './meta-datos.js';
import { F, off, NS } from './proyecto.js';
import { PAG } from './proto.js';

export const esSinte = t => t >= 4;
export const SAMPLE_M = t => esSinte(t) ? 8 : 2;
/* rev. 2: AFINA es la 0 (en los sintes, la nota) y SAMPLE la 1; la 8.ª de p es NIVEL y la 8.ª de EDIT es AZAR (azar[]) */
const SAMPLE_K = ['AFINA', 'SAMPLE', 'INICIO', 'CAÍDA', 'CORTE', 'CRUJIDO', 'MODO', 'AZAR'];
/* dónde empieza cada banco en la perilla SAMPLE: BOMBOS 0–23, CAJAS 24–47, TEXTURAS 48–95, TUYOS 96–127 */
export const LUGAR0 = [0, 24, 48, 96];
export const BANCOS = ['BOMBOS', 'CAJAS', 'TEXTURAS', 'TUYOS'];
const NOTAS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const FMR = ['×.5', '×1', '×1.41', '×2', '×2.76', '×3', '×3.5', '×5.19', '×7.13'];
const SHF = ['PLUCK', 'BAJO', 'LEAD', 'PAD'];
const DT = ['1/32', '1/16', '3/32', '1/8', '3/16', '1/4', '3/8', '1/2'];
const ARPM = ['ARRIBA', 'ABAJO', 'SUBE Y BAJA', 'AL AZAR', 'COMO TOCAS'], ARPV = ['1/32', '1/16', '1/8', '1/4', '1/16 TRESILLO', '1/8 TRESILLO'];
const RELOJ = ['NO', 'ENTRA', 'SALE', 'ENTRA Y SALE'], NOTASCC = ['NO', 'ENTRAN', 'SALEN', 'LAS DOS'], IDIOMAS = ['ESPAÑOL', 'ENGLISH', 'PORTUGUÊS'];
const cl = (v, a, b) => v < a ? a : v > b ? b : v;
const sg = v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(v);

/* lo que el aparato contesta con META: aquí empieza con la tabla local y se pisa con lo que llegue */
export const meta = {
  pistas: [...D.pistas],
  modelos: Array.from({ length: 8 }, (_, t) => esSinte(t) ? [...D.modelosSinte, 'SAMPLE'] : [D.modelosPerc[t][0], D.modelosPerc[t][1], 'SAMPLE']),
  condiciones: [...D.condiciones], escalas: [...D.escalas], formas: [...D.formas], performance: [...D.performance],
  samples: D.samples.map((l, b) => l.map((nombre, i) => ({ lugar: LUGAR0[b] + i, nombre }))).concat([[]]),   /* por banco: {lugar, nombre} */
  perillasDe: {},                                  /* 't,m' -> 8 nombres, de META tipo 2 */
  rangosDe: {},                                    /* 't,m' -> 8 [mínimo, máximo] */
};
export function perillasMotor(t, m) {
  const k = meta.perillasDe[t + ',' + m];
  if (k) return k;
  if (m === SAMPLE_M(t)) return SAMPLE_K;
  const l = esSinte(t) ? D.perillasSinte[m] : D.perillasPerc[t][m];
  return l ? [...l, 'AZAR'] : SAMPLE_K;
}
export function presets(t, m) {
  if (m === SAMPLE_M(t)) return [];
  return (esSinte(t) ? D.presetsSinte[m] : D.presetsPerc[t][m]) || [];
}
export const todosLosSamples = () => meta.samples.flat();
/* el sample de un lugar; si está vacío, el más cercano hacia abajo (como el aparato), si no hacia arriba */
export function sampleEn(v) { const l = todosLosSamples().sort((a, b) => a.lugar - b.lugar); return [...l].reverse().find(x => x.lugar <= v) || l[0] || null; }
export function rangoMotor(t, m, i) { const r = meta.rangosDe[t + ',' + m]?.[i]; if (r) return r; return m === SAMPLE_M(t) && i === 0 && !esSinte(t) ? [40, 88] : [0, 127]; }

export function nota(v) { const m = 24 + (v * 61 >> 7); return NOTAS[m % 12] + (Math.floor(m / 12) - 1); }
/* el valor de una perilla del motor, como lo escribe la pantalla (rel: un lock de NOTA, 64 = sin mover) */
export function fmtMotor(t, m, i, v, rel = false) {
  if (i === 7) return String(v);                  /* NIVEL (un lock de la 8.ª); el AZAR de EDIT tiene su propio texto */
  if (m === SAMPLE_M(t)) {
    if (i === 0) return esSinte(t) ? (rel ? (v === 64 ? '0 ST' : sg(v - 64) + ' ST') : nota(v)) : (v === 64 ? '0 ST' : sg(v - 64) + ' ST');
    if (i === 1) { const x = sampleEn(v); return x ? x.nombre : '–'; }
    if (i === 3 && v >= 120) return 'ENTERO';
    if (i === 4) return v >= 63 && v <= 65 ? 'ABIERTO' : (v < 63 ? 'GRAVES ' + (63 - v) : 'AGUDOS ' + (v - 65));
    if (i === 5) return v ? String(v) : 'LIMPIO';
    if (i === 6) return v <= 42 ? 'GOLPE' : v <= 85 ? 'LAZO' : 'REVERSA';
    return String(v);
  }
  if (esSinte(t)) {
    if (i === 0) return rel ? (v === 64 ? '0 ST' : sg(v - 64) + ' ST') : nota(v);
    if (m === 2 && i === 1) return FMR[v * 9 >> 7];
    if (m === 0 && i === 6) return SHF[v * 4 >> 7];
  }
  if (t === 3 && m === 0 && i === 1) return v < 60 ? 'CAE' : v > 68 ? 'SUBE' : 'QUIETA';
  return String(v);
}
const fmtMix = (t, i, v) => {
  if (i === 1) return v < 60 ? 'IZQ ' + (64 - v) : v > 68 ? 'DER ' + (v - 64) : 'CENTRO';
  if (i === 4) return v === 64 ? 'ABIERTO' : (v < 64 ? 'GRAVES ' + (64 - v) : 'AGUDOS ' + (v - 64));
  if (i > 4) return i === 7 && t === 0 ? '(MANDA)' : v ? String(v) : 'NADA';
  return String(v);
};
const fmtFx = (i, v) => i === 0 ? DT[v * 8 >> 7] : i === 5 ? (v < 61 ? 'GRAVE ' + (64 - v) : v > 67 ? 'AGUDO ' + (v - 64) : 'ABIERTO') : String(v);
const hz = v => (0.05 * 2 ** (v * 9 / 127)).toFixed(2) + ' HZ';

/* pista actual del patrón que suena */
const trk = (pr, t) => pr.pats[pr.song[F.cur] % 16][t];
const byte = (o, mi = 0, ma = 127, fmt = String) => ({ min: mi, max: ma, get: pr => pr.song[o], set: (pr, v) => { pr.song[o] = v; }, fmt });
const sbyte = (o, mi, ma, fmt) => ({ min: mi, max: ma, get: pr => { const v = pr.song[o]; return v > 127 ? v - 256 : v; }, set: (pr, v) => { pr.song[o] = v & 255; }, fmt });

/* la perilla k (0..3) de la página pag, subpágina sub, con la pista t elegida; k = 4 ALGO (modelo), 5 PRESET */
export function objetivo(t, pag, sub, k, ajustes = { idioma: 0 }) {
  let g = null;
  const S = s => s;
  switch (pag) {
    case PAG.EDIT: {
      if (k === 4) return { nombre: 'MODELO', min: 0, max: meta.modelos[t].length - 1, get: pr => pr.song[F.model + t], set: (pr, v) => { pr.song[F.model + t] = v; }, fmt: v => meta.modelos[t][v] || '–' };
      if (k === 5) return { nombre: 'PRESET', min: 0, max: 127, accion: true, get: () => 0, set: (pr, v) => { const m = pr.song[F.model + t], l = presets(t, m)[v]; if (l) l.p.forEach((x, i) => pr.song[off.p(t, m, i)] = x); }, fmt: v => '' + v };
      const i = sub * 4 + k;
      if (i === 7) return { nombre: 'AZAR', ...byte(F.azar + t), fmt: v => v ? String(v) : 'NADA' };
      g = { nombre: null, i, min: 0, max: 127, rangoDe: pr => rangoMotor(t, pr.song[F.model + t], i),
        get: pr => pr.song[off.p(t, pr.song[F.model + t], i)], set: (pr, v) => { pr.song[off.p(t, pr.song[F.model + t], i)] = v; } };
      g.nombreDe = pr => perillasMotor(t, pr.song[F.model + t])[i];
      g.fmtDe = pr => v => fmtMotor(t, pr.song[F.model + t], i, v);
      return g;
    }
    case PAG.ENV: {
      const L = sub ? ['FILTRO', 'ANCHO', 'SATURA', 'SIDECHAIN'] : D.mezcla;
      return { nombre: L[k], ...byte(sub ? off.mix2(t, k) : off.mix(t, k)), fmt: v => fmtMix(t, sub * 4 + k, v) };
    }
    case PAG.FX: { const i = D.fxPagina[sub & 1][k]; return { nombre: D.fx[i], global: true, ...byte(F.fx + i), fmt: v => fmtFx(i, v) }; }
    case PAG.SCL:
      if (sub) {
        const L = ['MOVER', 'TRANSPONER', 'HUMANO', 'DENSIDAD'], MI = [-64, 40, 0, 0], MA = [64, 88, 127, 127];
        if (k === 0) return { nombre: L[0], min: MI[0], max: MA[0], accion: true, get: () => 0,
          set: (pr, v) => { const tr = trk(pr, t), n = tr.len, a = tr.st.slice(0, n); for (let s = 0; s < n; s++) tr.st[((s + v) % n + n) % n] = a[s]; }, fmt: () => '← GIRA →' };
        return { nombre: L[k], min: MI[k], max: MA[k], ...{ get: pr => pr.song[off.scl2(t, k)], set: (pr, v) => { pr.song[off.scl2(t, k)] = v; } },
          fmt: k === 1 ? v => v === 64 ? 'NADA' : sg(v - 64) + (esSinte(t) ? ' ST' : '') : k === 2 ? v => v ? String(v) : 'NADA' : v => v >= 127 ? 'TODOS' : (v * 100 / 127 | 0) + '%' };
      } else {
        const L = ['LARGO', 'VELOCIDAD', 'GOLPES', 'ROTAR'];
        if (k === 0) return { nombre: L[0], min: 1, max: NS, get: pr => trk(pr, t).len, set: (pr, v) => { trk(pr, t).len = v; }, fmt: v => v + ' PASOS' };
        if (k === 1) return { nombre: L[1], min: 0, max: 2, get: pr => trk(pr, t).spd, set: (pr, v) => { trk(pr, t).spd = v; }, fmt: v => ['½', '1', '2'][v] + '×' };
        const cur = pr => pr.song[F.cur] % 16;
        return { nombre: L[k], min: 0, max: k === 2 ? 32 : 31, get: pr => pr.song[off.eu(cur(pr), t, k - 2)], set: (pr, v) => { pr.song[off.eu(cur(pr), t, k - 2)] = v; euclides(pr, t); },
          fmt: k === 2 ? v => v ? v + ' DE ?' : 'A MANO' : v => '+' + v };
      }
    case PAG.LFO: {
      const MA = [6, 127, 127, 4], o = sub ? off.lfo2(t, k) : off.lfo(t, k);
      const L = ['DESTINO', 'VELOCIDAD', 'PROFUNDIDAD', 'FORMA'].map(x => sub ? x + ' 2' : x);
      g = { nombre: L[k], ...byte(o, 0, MA[k]) };
      if (k === 0) { g.fmtDe = pr => v => perillasMotor(t, pr.song[F.model + t])[v]; g.fmt = String; }
      else g.fmt = k === 1 ? hz : k === 3 ? v => D.formas[cl(v, 0, 4)] : String;
      return g;
    }
    case PAG.GLO:
      if (!sub) {
        if (k === 0) return { nombre: 'TEMPO', global: true, ...byte(F.bpm, 60, 200), fmt: v => v + ' BPM' };
        if (k === 1) return { nombre: 'SWING', global: true, ...byte(F.swing, 0, 75), fmt: v => v + '%' };
        if (k === 2) return { nombre: 'RAÍZ', global: true, ...sbyte(F.transp, -12, 12), fmt: v => sg(v) + ' ST' };
        return { nombre: 'ESCALA', global: true, ...byte(F.scale, 0, 7), fmt: v => D.escalas[cl(v, 0, 7)] };
      }
      if (k === 0) return { nombre: 'MUTES', global: true, ...byte(F.muteq, 0, 1), fmt: v => v ? 'AL COMPÁS' : 'AL INSTANTE' };
      if (k === 1) return { nombre: 'RELOJ', global: true, ...byte(F.reloj, 0, 3), fmt: v => RELOJ[cl(v, 0, 3)] };
      if (k === 2) return { nombre: 'IDIOMA', global: true, ajuste: true, min: 0, max: 2, get: () => ajustes.idioma, set: (pr, v) => { ajustes.idioma = v; }, fmt: v => IDIOMAS[v] };
      return { nombre: 'NOTAS Y CC', global: true, ...byte(F.mnotes, 0, 3), fmt: v => NOTASCC[cl(v, 0, 3)] };
    case PAG.CADENA: {
      const r = t;
      if (k === 0) return { nombre: 'PATRÓN', global: true, min: 0, max: 16, get: pr => { const v = pr.song[F.crow + r]; return v === 255 ? 0 : v + 1; }, set: (pr, v) => { pr.song[F.crow + r] = v ? v - 1 : 255; }, fmt: v => v ? 'PATRÓN ' + v : '– VACÍA' };
      if (k === 1) return { nombre: 'VECES', global: true, ...byte(F.crep + r, 1, 16), fmt: v => '×' + v };
      if (k === 3) return { nombre: 'CADENA', global: true, ...byte(F.chain_on, 0, 1), fmt: v => v ? 'SÍ' : 'NO' };
      return null;
    }
    case PAG.ARP: {
      if (!esSinte(t)) return null;
      const MA = [4, 5, 2, 1], L = ['MODO', 'VELOCIDAD', 'OCTAVAS', 'SOSTENER'];
      return { nombre: L[k], ...byte(off.arp(t, k), 0, MA[k]), fmt: k === 0 ? v => ARPM[v] : k === 1 ? v => ARPV[v] : k === 2 ? v => (v + 1) + (v ? ' OCTAVAS' : ' OCTAVA') : v => v ? 'SÍ' : 'NO' };
    }
  }
  return S(g);
}
/* los nombres y textos de una perilla ya resueltos contra el proyecto */
export function resolver(g, pr) {
  if (!g) return null;
  const r = g.rangoDe?.(pr);
  return { ...g, nombre: g.nombreDe ? g.nombreDe(pr) : g.nombre, fmt: g.fmtDe ? g.fmtDe(pr) : g.fmt, ...(r ? { min: r[0], max: r[1] } : {}) };
}

/* GOLPES > 0: los pasos los reparte Bjorklund (como el aparato: los locks de los pasos que quedan se respetan) */
export function euclides(pr, t) {
  const p = pr.song[F.cur] % 16, n = pr.song[off.eu(p, t, 0)], rot = pr.song[off.eu(p, t, 1)], tr = pr.pats[p][t];
  if (!n) return;
  const L = tr.len;
  const k = Math.min(n, L);
  for (let i = 0; i < L; i++) { const s = tr.st[(i + rot) % L]; s.v = (i * k) % L < k ? (s.v || 1) : 0; }
}

/* qué guarda cada lock de un paso: 0..7 perillas del motor, 8..11 mezcla */
export function nombreLock(pr, t, i) { return i === 7 ? 'NIVEL' : i < 8 ? perillasMotor(t, pr.song[F.model + t])[i] : D.mezcla[i - 8]; }
export function fmtLock(pr, t, i, v) { return i < 8 ? fmtMotor(t, pr.song[F.model + t], i, v, esSinte(t) && i === 0) : fmtMix(t, i - 8, v); }
export function baseLock(pr, t, i) { return i < 8 ? (esSinte(t) && i === 0 ? 64 : pr.song[off.p(t, pr.song[F.model + t], i)]) : pr.song[off.mix(t, i - 8)]; }

export const D_ = D;
