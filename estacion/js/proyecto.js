// El proyecto del FNTSMG-1 en la Estación (PROTOCOLO.md §6): la canción se guarda tal cual, como los bytes de
// fbx_song_t (así lo que la app no entiende viaja intacto) y los patrones como objetos de pasos. pack() y unpack()
// son el formato de una ranura sin su cabecera. Entiende la v7 (0.9), la v8 y la v9 (0.10); escribe la misma que leyó.
// En la v8 y la v9 el sonido es de cada patrón: la canción trae el del patrón que suena y los 16 van al final (§6.5).
// Adentro la canción siempre tiene el acomodo de la v9 (16 motores por pista); la v7 y la v8 se convierten al leer y
// al escribir, así el resto de la app usa una sola tabla.

export const NP = 16, NT = 8, NS = 32, NM = 16, NONE = 255;
export const SONG = 6724, SONG_V8 = 6084, SONG_V7 = 6052, KIT = 273;
const HAND = 1390, HAND_END = 5486;            /* v9: viajan 0..1389 y 5486..6723 */
const NM8 = 11, HAND8 = 1070, HAND_END8 = 5166; /* v7 y v8: 11 motores por pista */

/* dónde vive cada campo de la canción (bytes desde el inicio de fbx_song_t, v9) */
export const F = {
  model: 0, p: 8, mix: 1032, fx: 1064, lfo: 1072, azar: 1104, bpm: 1112, swing: 1113, scale: 1114, muteq: 1115, cur: 1116, transp: 1117,
  eu: 1118, has_hand: 1374, hand: HAND, edited: HAND_END, has_edited: 6510, crow: 6526, crep: 6542, chain_on: 6558, reloj: 6559,
  mclk_out: 6560, mnotes: 6561, mix2: 6562, scl2: 6594, lfo2: 6626, arp: 6658, arp_on: 6690, arp2: 6691,
};
/* de la canción v7/v8 (p y edited de [8][11][8]) a la de adentro, y de regreso */
function aV9(v, n) {
  const s = new Uint8Array(SONG);
  s.set(v.subarray(0, 8), 0);
  for (let t = 0; t < NT; t++) for (let m = 0; m < NM8; m++) {
    s.set(v.subarray(8 + (t * NM8 + m) * 8, 16 + (t * NM8 + m) * 8), 8 + (t * NM + m) * 8);
    s.set(v.subarray(HAND_END8 + (t * NM8 + m) * 8, HAND_END8 + 8 + (t * NM8 + m) * 8), HAND_END + (t * NM + m) * 8);
  }
  s.set(v.subarray(712, HAND_END8), F.mix);                   /* mix .. hand, corridos 320 */
  s.set(v.subarray(HAND_END8 + 704, n), F.has_edited);        /* has_edited .. arp2, corridos 640 */
  return s;
}
function deV9(s, n) {
  const v = new Uint8Array(n);
  v.set(s.subarray(0, 8), 0);
  for (let t = 0; t < NT; t++) for (let m = 0; m < NM8; m++) {
    v.set(s.subarray(8 + (t * NM + m) * 8, 16 + (t * NM + m) * 8), 8 + (t * NM8 + m) * 8);
    v.set(s.subarray(HAND_END + (t * NM + m) * 8, HAND_END + 8 + (t * NM + m) * 8), HAND_END8 + (t * NM8 + m) * 8);
  }
  v.set(s.subarray(F.mix, HAND_END), 712);
  v.set(s.subarray(F.has_edited, F.has_edited + n - (HAND_END8 + 704)), HAND_END8 + 704);
  return v;
}
export const off = {
  p: (t, m, i) => F.p + (t * NM + m) * 8 + i,
  mix: (t, i) => F.mix + t * 4 + i, mix2: (t, i) => F.mix2 + t * 4 + i, scl2: (t, i) => F.scl2 + t * 4 + i,
  lfo: (t, i) => F.lfo + t * 4 + i, lfo2: (t, i) => F.lfo2 + t * 4 + i, arp: (t, i) => F.arp + t * 4 + i, arp2: (t, i) => F.arp2 + t * 4 + i,
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
  s[off.arp2(4, 0)] = s[off.arp2(5, 0)] = s[off.arp2(6, 0)] = s[off.arp2(7, 0)] = 64;
  const k = kitTomar(s);
  return { ver: 9, song: s, pats: Array.from({ length: NP }, () => Array.from({ length: NT }, pistaVacia)), upre: new Uint8Array(0), kits: Array.from({ length: NP }, () => k.slice()), kitOwn: 0 };
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

/* ---- el sonido de un patrón (fbx_kit_t, 273 bytes, §6.5): de la canción y de regreso (kit_take / kit_put) ---- */
export function kitTomar(s) {
  const k = new Uint8Array(KIT);
  k.set(s.subarray(F.model, F.model + 8), 0);
  for (let t = 0; t < NT; t++) k.set(s.subarray(off.p(t, s[F.model + t] % NM, 0), off.p(t, s[F.model + t] % NM, 8)), 8 + t * 8);
  k.set(s.subarray(F.mix, F.mix + 32), 72); k.set(s.subarray(F.mix2, F.mix2 + 32), 104); k.set(s.subarray(F.lfo, F.lfo + 32), 136);
  k.set(s.subarray(F.lfo2, F.lfo2 + 32), 168); k.set(s.subarray(F.scl2, F.scl2 + 32), 200); k.set(s.subarray(F.azar, F.azar + 8), 232);
  k.set(s.subarray(off.arp(4, 0), off.arp(8, 0)), 240); k.set(s.subarray(off.arp2(4, 0), off.arp2(8, 0)), 256); k[272] = s[F.arp_on];
  return k;
}
export function kitPoner(s, k) {
  s.set(k.subarray(0, 8), F.model);
  for (let t = 0; t < NT; t++) s.set(k.subarray(8 + t * 8, 16 + t * 8), off.p(t, k[t] % NM, 0));
  s.set(k.subarray(72, 104), F.mix); s.set(k.subarray(104, 136), F.mix2); s.set(k.subarray(136, 168), F.lfo);
  s.set(k.subarray(168, 200), F.lfo2); s.set(k.subarray(200, 232), F.scl2); s.set(k.subarray(232, 240), F.azar);
  s.set(k.subarray(240, 256), off.arp(4, 0)); s.set(k.subarray(256, 272), off.arp2(4, 0)); s[F.arp_on] = k[272];
}
/* un registro: 0 igual al anterior, 1 mapa de 35 bytes + lo que cambia, 2 entero (la misma regla que kit_enc) */
function kitCodificar(ref, k, o) {
  const nb = Math.ceil(KIT / 8); let nd = 0;
  for (let i = 0; i < KIT; i++) nd += ref[i] !== k[i];
  if (!nd) { o.push(0); return; }
  if (nb + nd >= KIT) { o.push(2, ...k); return; }
  const m = new Uint8Array(nb), v = [];
  for (let i = 0; i < KIT; i++) if (ref[i] !== k[i]) { m[i >> 3] |= 1 << (i & 7); v.push(k[i]); }
  o.push(1, ...m, ...v);
}
function kitLeer(ref, b, i) {
  const modo = b[i++], k = ref.slice(), nb = Math.ceil(KIT / 8);
  if (modo === 2) { k.set(b.subarray(i, i + KIT)); i += KIT; }
  else if (modo === 1) { const m = b.subarray(i, i + nb); i += nb; for (let j = 0; j < KIT; j++) if (m[j >> 3] >> (j & 7) & 1) k[j] = b[i++]; }
  else if (modo !== 0) throw Object.assign(new Error('FORMATO'), { codigo: 'FORMATO' });
  return [k, i];
}

/* ---- el proyecto entero (§6) ---- */
export function pack(pr) {
  const ver = pr.ver >= 9 ? 9 : pr.ver >= 8 ? 8 : 7, v8 = ver >= 8, s = pr.song;
  const n = ver === 9 ? SONG : ver === 8 ? SONG_V8 : SONG_V7, o = [0x46, 0x47, 0x42, 0x31, ver, NP, n & 255, n >> 8];
  const c = ver === 9 ? s : deV9(s, n), h0 = ver === 9 ? HAND : HAND8, h1 = ver === 9 ? HAND_END : HAND_END8;
  for (let i = 0; i < h0; i++) o.push(c[i]);
  for (let i = h1; i < n; i++) o.push(c[i]);
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) if (s[F.has_hand + p] >> t & 1) { const h = off.hand(p, t); for (let i = 0; i < 32; i++) o.push(s[h + i]); }
  o.push(pr.upre.length / 21, ...pr.upre);
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) o.push(...packPista(pr.pats[p][t]));
  if (v8) {
    /* lo que se edita es del patrón que suena: su sonido es el de la canción */
    const cur = s[F.cur] % NP; pr.kits[cur] = kitTomar(s);
    o.push(pr.kitOwn & 255, pr.kitOwn >> 8);
    let ref = kitTomar(s);
    for (let p = 0; p < NP; p++) { kitCodificar(ref, pr.kits[p], o); ref = pr.kits[p]; }
  }
  return Uint8Array.from(o);
}
export function unpack(b) {
  if (b.length < 8 || b[0] !== 0x46 || b[1] !== 0x47 || b[2] !== 0x42 || b[3] !== 0x31) throw Object.assign(new Error('FORMATO'), { codigo: 'FORMATO' });
  const ver = b[4], np = b[5], sz = b[6] | b[7] << 8;
  if (!((ver === 7 && sz === SONG_V7) || (ver === 8 && sz === SONG_V8) || (ver === 9 && sz === SONG)) || np !== NP) throw Object.assign(new Error('FORMATO'), { codigo: 'FORMATO', ver });
  const pr = proyectoVacio();
  pr.ver = ver;
  let i = 8;
  const h0 = ver === 9 ? HAND : HAND8, h1 = ver === 9 ? HAND_END : HAND_END8;
  if (ver === 9) {
    pr.song.set(b.subarray(i, i + h0), 0); i += h0;
    pr.song.set(b.subarray(i, i + sz - h1), h1); i += sz - h1;
  } else {
    /* lo de proyectoVacio queda en los motores 11..15, que una canción vieja no trae */
    const v = deV9(pr.song, sz);
    v.set(b.subarray(i, i + h0), 0); i += h0;
    v.set(b.subarray(i, i + sz - h1), h1); i += sz - h1;
    const nueva = aV9(v, sz);
    for (let t = 0; t < NT; t++) for (let m = NM8; m < NM; m++) { nueva.set(pr.song.subarray(off.p(t, m, 0), off.p(t, m, 8)), off.p(t, m, 0)); nueva.set(pr.song.subarray(F.edited + (t * NM + m) * 8, F.edited + (t * NM + m + 1) * 8), F.edited + (t * NM + m) * 8); }
    if (ver === 7) nueva.set(pr.song.subarray(F.arp2, F.arp2 + 32), F.arp2);
    pr.song = nueva;
  }
  const s = pr.song;
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) if (s[F.has_hand + p] >> t & 1) { s.set(b.subarray(i, i + 32), off.hand(p, t)); i += 32; }
  const nupre = b[i++]; pr.upre = b.slice(i, i + nupre * 21); i += nupre * 21;
  for (let p = 0; p < NP; p++) for (let t = 0; t < NT; t++) { const [tr, j] = unpackPista(b, i); pr.pats[p][t] = tr; i = j; }
  if (ver >= 8) {
    pr.kitOwn = b[i] | b[i + 1] << 8; i += 2;
    let ref = kitTomar(s);
    for (let p = 0; p < NP; p++) { const [k, j] = kitLeer(ref, b, i); pr.kits[p] = k; ref = k; i = j; }
  } else { const k = kitTomar(s); pr.kits = pr.kits.map(() => k.slice()); }
  return pr;
}
export const sonidoPorPatron = pr => pr.ver >= 8;
/* la versión de un proyecto en bytes, sin abrirlo (las ranuras viejas llegan en v6) */
export const versionDe = b => b.length >= 8 && b[0] === 0x46 && b[1] === 0x47 ? b[4] : 0;
export const clonar = pr => unpack(pack(pr));
export const pistasUsadas = pr => pr.pats.map(pt => pt.some(tr => tr.st.some(pasoTieneAlgo)));
