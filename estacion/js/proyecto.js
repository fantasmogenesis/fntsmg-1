// El proyecto del FNTSMG-1 en la Estación (PROTOCOLO.md §6): la canción se guarda tal cual, como los 6052 bytes de
// fbx_song_t (así lo que la app no entiende viaja intacto) y los patrones como objetos de pasos. pack() y unpack()
// son el formato de una ranura sin su cabecera.

export const NP = 16, NT = 8, NS = 32, NM = 11, NONE = 255;
export const SONG = 6052;
const HAND = 1070, HAND_END = 5166;

/* dónde vive cada campo de la canción (bytes desde el inicio de fbx_song_t) */
export const F = {
  model: 0, p: 8, mix: 712, fx: 744, lfo: 752, azar: 784, bpm: 792, swing: 793, scale: 794, muteq: 795, cur: 796, transp: 797,
  eu: 798, has_hand: 1054, hand: HAND, edited: 5166, has_edited: 5870, crow: 5886, crep: 5902, chain_on: 5918, reloj: 5919,
  mclk_out: 5920, mnotes: 5921, mix2: 5922, scl2: 5954, lfo2: 5986, arp: 6018, arp_on: 6050,
};
export const off = {
  p: (t, m, i) => F.p + (t * NM + m) * 8 + i,
  mix: (t, i) => F.mix + t * 4 + i, mix2: (t, i) => F.mix2 + t * 4 + i, scl2: (t, i) => F.scl2 + t * 4 + i,
  lfo: (t, i) => F.lfo + t * 4 + i, lfo2: (t, i) => F.lfo2 + t * 4 + i, arp: (t, i) => F.arp + t * 4 + i,
  eu: (pt, t, i) => F.eu + (pt * NT + t) * 2 + i, hand: (pt, t) => F.hand + (pt * NT + t) * 32,
};

export function pasoVacio() { return { v: 0, c: 0, p: 100, r: 1, u: 0, lk: new Uint8Array(12).fill(NONE), au: [] }; }
export function pistaVacia() { return { len: 16, spd: 1, st: Array.from({ length: NS }, pasoVacio) }; }
export const pasoTieneAlgo = s => s.v || s.c || s.p !== 100 || s.r !== 1 || s.u || s.lk.some(x => x !== NONE) || s.au.length;
const soloGolpe = s => s.v && !s.c && s.p === 100 && s.r === 1 && !s.u && s.lk.every(x => x === NONE) && !s.au.length;

export function proyectoVacio() {
  const s = new Uint8Array(SONG);
  /* los mismos valores con los que arranca el aparato (fg_box_init) en lo que importa para leerlo */
  for (let t = 0; t < NT; t++) {
    s[off.mix(t, 0)] = 100; s[off.mix(t, 1)] = 64; s[off.mix2(t, 0)] = 64; s[off.scl2(t, 1)] = 64;
    for (let m = 0; m < NM; m++) for (let i = 0; i < 8; i++) s[off.p(t, m, i)] = 64;   /* la 8.ª es NIVEL: 64 = tal cual */
  }
  [4, 50, 70, 70, 60, 64, 0, 30].forEach((v, i) => s[F.fx + i] = v);
  s[F.bpm] = 124; s[F.swing] = 0;
  s.fill(NONE, F.crow, F.crow + 16); s.fill(1, F.crep, F.crep + 16);
  return { ver: 7, song: s, pats: Array.from({ length: NP }, () => Array.from({ length: NT }, pistaVacia)), upre: new Uint8Array(0) };
}

/* ---- un registro de paso (§6.3), desde las banderas ---- */
export function registro(s) {
  const o = [];
  let fl = 0;
  if (s.v) fl |= 1; if (s.c) fl |= 2; if (s.p !== 100) fl |= 4; if (s.r !== 1) fl |= 8; if (s.u) fl |= 16;
  const lm = s.lk.reduce((m, v, i) => v !== NONE ? m | 1 << i : m, 0);
  if (lm) fl |= 32; if (s.au.length) fl |= 64;
  o.push(fl);
  if (fl & 1) o.push(s.v);
  if (fl & 2) o.push(s.c);
  if (fl & 4) o.push(s.p);
  if (fl & 8) o.push(s.r);
  if (fl & 16) o.push(s.u & 255);
  if (fl & 32) { o.push(lm & 255, lm >> 8); for (let i = 0; i < 12; i++) if (lm >> i & 1) o.push(s.lk[i]); }
  if (fl & 64) { const a = [...s.au].sort((x, y) => x[0] - y[0]); o.push(a.reduce((m, [i]) => m | 1 << i, 0)); for (const [, v] of a) o.push(v); }
  return o;
}
export function leerRegistro(b, i) {
  const s = pasoVacio(), fl = b[i++];
  if (fl & 1) s.v = b[i++];
  if (fl & 2) s.c = b[i++];
  if (fl & 4) s.p = b[i++];
  if (fl & 8) s.r = b[i++];
  if (fl & 16) { const u = b[i++]; s.u = u > 127 ? u - 256 : u; }
  if (fl & 32) { const m = b[i] | b[i + 1] << 8; i += 2; for (let k = 0; k < 12; k++) if (m >> k & 1) s.lk[k] = b[i++]; }
  if (fl & 64) { const m = b[i++]; for (let k = 0; k < 8; k++) if (m >> k & 1) s.au.push([k, b[i++]]); }
  return [s, i];
}

/* ---- una pista (§6.3) ---- */
export function packPista(tr) {
  const o = [tr.len, tr.spd];
  let g = 0, a = 0;
  tr.st.forEach((s, i) => { if (soloGolpe(s)) { if (s.v === 2) a |= 1 << i; else g |= 1 << i; } });
  const u32 = v => [v & 255, v >>> 8 & 255, v >>> 16 & 255, v >>> 24 & 255];
  o.push((g ? 1 : 0) | (a ? 2 : 0));
  if (g) o.push(...u32(g >>> 0));
  if (a) o.push(...u32(a >>> 0));
  tr.st.forEach((s, i) => { if (!soloGolpe(s) && pasoTieneAlgo(s)) o.push(i, ...registro(s)); });
  o.push(0xFF);
  return o;
}
export function unpackPista(b, i) {
  const tr = pistaVacia();
  tr.len = b[i++]; tr.spd = b[i++];
  const mk = b[i++], u32 = () => (b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24) >>> 0;
  if (mk & 1) { const g = u32(); i += 4; for (let s = 0; s < NS; s++) if (g >>> s & 1) tr.st[s].v = 1; }
  if (mk & 2) { const a = u32(); i += 4; for (let s = 0; s < NS; s++) if (a >>> s & 1) tr.st[s].v = 2; }
  while (i < b.length && b[i] !== 0xFF) { const n = b[i++]; const [s, j] = leerRegistro(b, i); if (n < NS) tr.st[n] = s; i = j; }
  return [tr, i + 1];
}

/* ---- el proyecto entero (§6) ---- */
export function pack(pr) {
  const s = pr.song, o = [0x46, 0x47, 0x42, 0x31, 7, NP, SONG & 255, SONG >> 8];
  for (let i = 0; i < HAND; i++) o.push(s[i]);
  for (let i = HAND_END; i < SONG; i++) o.push(s[i]);
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) if (s[F.has_hand + p] >> t & 1) { const h = off.hand(p, t); for (let i = 0; i < 32; i++) o.push(s[h + i]); }
  o.push(0);                                        /* nupre: la app no escribe sonidos viejos */
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) o.push(...packPista(pr.pats[p][t]));
  return Uint8Array.from(o);
}
export function unpack(b) {
  if (b.length < 8 || b[0] !== 0x46 || b[1] !== 0x47 || b[2] !== 0x42 || b[3] !== 0x31) throw Object.assign(new Error('FORMATO'), { codigo: 'FORMATO' });
  const ver = b[4], np = b[5], sz = b[6] | b[7] << 8;
  if (ver !== 7 || np !== NP || sz !== SONG) throw Object.assign(new Error('FORMATO'), { codigo: 'FORMATO', ver });
  const pr = proyectoVacio(), s = pr.song;
  let i = 8;
  s.set(b.subarray(i, i + HAND), 0); i += HAND;
  s.set(b.subarray(i, i + SONG - HAND_END), HAND_END); i += SONG - HAND_END;
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) if (s[F.has_hand + p] >> t & 1) { s.set(b.subarray(i, i + 32), off.hand(p, t)); i += 32; }
  const nupre = b[i++]; pr.upre = b.slice(i, i + nupre * 21); i += nupre * 21;
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) { const [tr, j] = unpackPista(b, i); pr.pats[p][t] = tr; i = j; }
  return pr;
}
/* la versión de un proyecto en bytes, sin abrirlo (las ranuras viejas llegan en v6) */
export const versionDe = b => b.length >= 8 && b[0] === 0x46 && b[1] === 0x47 ? b[4] : 0;
export const clonar = pr => unpack(pack(pr));
export const pistasUsadas = pr => pr.pats.map(pt => pt.some(tr => tr.st.some(pasoTieneAlgo)));
