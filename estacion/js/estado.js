// El estado de la Estación y la plática con el FM-1. La app guarda una copia del proyecto (E.pr) y cada cambio va al
// aparato por el protocolo; lo que el aparato cambia por su lado llega como CAMBIO y se vuelve a leer. Con un 0.8.x
// (no contesta INFO) solo funciona el espejo.
import { Cliente, CMD, OBJ, PAG, Lec } from './proto.js';
import { transporteReal, TransporteDemo, detectar, sinMidi } from './midi.js';
import { AparatoDemo } from './aparato-demo.js';
import { unpack, registro, F, off } from './proyecto.js';
import { meta, objetivo } from './meta.js';
import { pref } from './almacen.js';

export const E = {
  conexion: 'sin',          /* sin, buscando, conectado, demo, espejo, perdido, error */
  error: null, info: null, cliente: null, tr: null, pr: null,
  sel: pref('sel') ?? 0, patron: 0, paso: null,
  tocando: false, cur: 0, cola: 255, mute: 0, solo: 0, pos: new Int16Array(8).fill(-1), usados: 0, niveles: new Uint8Array(8),
  ajustes: { idioma: 0 }, cargando: 0, ultimoEstado: 0,
};
const subs = {};
export const on = (ev, fn) => ((subs[ev] ||= new Set()).add(fn), () => subs[ev].delete(fn));
export const emit = (ev, ...a) => subs[ev]?.forEach(f => { try { f(...a); } catch (e) { console.error(e); } });

let demo = null, escuchaT = null, ccT = null, releerT = null;
export const hayProyecto = () => !!E.pr;
export const conProtocolo = () => E.conexion === 'conectado' || E.conexion === 'demo';

function poner(c, error = null) { E.conexion = c; E.error = error; emit('conexion'); }

export async function conectar(modo) {
  await desconectar(false);
  const tr = modo === 'demo' ? new TransporteDemo(demo ||= new AparatoDemo()) : transporteReal();
  if (modo === 'demo' && !demo.timer) await demo.iniciar();
  if (modo !== 'demo') { const r = sinMidi(); if (r) { poner('error', { codigo: 'NOMIDI_' + r }); return; } }
  poner('buscando');
  E.tr = tr;
  const c = E.cliente = new Cliente(tr);
  c.onAviso(aviso);
  tr.onEstado = s => {
    if (s === 'perdido') { pararEscucha(); poner('perdido'); }
    else if (s === 'conectado' && E.conexion === 'perdido') arrancar(modo);
  };
  try { await tr.conectar(); }
  catch (err) { E.tr = E.cliente = null; poner('error', err); return; }
  pref('modo', modo);
  await arrancar(modo);
}
async function arrancar(modo) {
  const c = E.cliente;
  poner('buscando');
  try { E.info = await c.info(); }
  catch (err) {
    if (err.codigo === 'SIN_RESPUESTA' && modo !== 'demo') { E.info = null; poner('espejo'); escucharEspejo(); return; }
    poner('error', err); return;
  }
  E.ajustes.idioma = E.info.idioma; E.tocando = E.info.tocando;
  poner(modo === 'demo' ? 'demo' : 'conectado');
  escuchar();
  await releer(true);
  nombresDelAparato();
}
export async function desconectar(avisar = true) {
  pararEscucha();
  try { E.tr?.desconectar(); } catch (e) {}
  E.tr = E.cliente = null; E.info = null;
  if (avisar) { E.pr = null; pref('modo', null); poner('sin'); emit('proyecto'); }
}

function escuchar() {
  const c = E.cliente;
  const f = () => { if (E.cliente === c) c.escucha(3); };
  f(); escuchaT = setInterval(f, 1500);
}
function escucharEspejo() {
  const f = () => E.tr?.cc?.(16, 119, 127);
  f(); ccT = setInterval(f, 1000);
}
function pararEscucha() {
  clearInterval(escuchaT); clearInterval(ccT); escuchaT = ccT = null;
  try { if (E.cliente && conProtocolo()) E.cliente.escucha(0); E.tr?.cc?.(16, 119, 0); } catch (e) {}
}
addEventListener('pagehide', () => pararEscucha());

/* los avisos: el espejo (golpes, estado de cada cuadro) y CAMBIO */
function aviso(cmd, d, propio) {
  if (cmd === CMD.GOLPE) { emit('golpe', d); return; }
  if (cmd === CMD.ESTADO) {
    if (d.length < 59) return;
    const bpm = d[1] << 7 | d[2], cur = d[4], cola = d[5] === 127 ? 255 : d[5], used = d[6] << 14 | d[7] << 7 | d[8];
    const mute = d[9] << 7 | d[10], solo = d[11] << 7 | d[12];
    for (let t = 0; t < 8; t++) E.niveles[t] = d[19 + t];
    /* la pista elegida en el aparato: la app la sigue cuando cambia allá */
    if (d[3] < 8 && d[3] !== E.selAparato) { E.selAparato = d[3]; if (d[3] !== E.sel) elegirPista(d[3]); }
    /* desde 0.9: el paso de cada pista y si toca (74 bytes); el 0.8 no lo trae y la rejilla se queda sin cabezal */
    if (d.length >= 68) { for (let t = 0; t < 8; t++) E.pos[t] = d[59 + t] === 127 ? -1 : d[59 + t]; E.tocando = !!d[67]; }
    const cambio = cur !== E.cur || cola !== E.cola || mute !== E.mute || solo !== E.solo || used !== E.usados;
    Object.assign(E, { cur, cola, mute, solo, usados: used, ultimoEstado: performance.now() });
    if (E.pr && E.pr.song[F.bpm] !== bpm && bpm >= 40) { E.pr.song[F.bpm] = bpm; emit('bpm'); }
    emit('espejo', d);
    emit('estado', cambio);
    return;
  }
  if (cmd === CMD.CAMBIO && !propio && E.pr) {
    if (d.que & (1 | 2 | 8)) releerPronto();
    if (d.que & 64) refrescarSamples();
    if (d.que & 16) emit('banco');
    if (d.que & 32) emit('ranuras');
    if (d.que & 64) emit('samples');
    if (d.que & 128) E.cliente.ajuste(0).then(v => { E.ajustes.idioma = v; emit('ajustes'); });
  }
}

/* releer lo que cambió en el aparato, pero no a media ráfaga de la app: el aparato podría ir unos valores atrás */
function releerPronto() {
  clearTimeout(releerT);
  releerT = setTimeout(() => performance.now() - (E.ultimoEnvio || 0) < 500 ? releerPronto() : releer(), 250);
}
export async function releer(inicio = false) {
  if (!conProtocolo()) return;
  E.cargando++; emit('cargando');
  try {
    const b = await E.cliente.leerObjeto(OBJ.PROYECTO, 0, p => emit('progreso', p));
    E.pr = unpack(b);
    E.cur = E.pr.song[F.cur]; if (inicio) E.patron = E.cur;
    emit('proyecto');
  } catch (err) { emit('falla', err); }
  finally { E.cargando--; emit('cargando'); }
}

/* los nombres en el idioma del aparato (META); si no contesta, se quedan los de la tabla local */
async function nombresDelAparato() {
  const c = E.cliente;
  try {
    const p = await c.meta(0); if (p.length === 8) meta.pistas = p;
    for (let t = 0; t < 8; t++) { const m = await c.meta(1, t); if (m.length) meta.modelos[t] = m; }
    for (let b = 0; b < 4; b++) { const s = await c.meta(5, b); if (Array.isArray(s)) meta.samples[b] = s; }
    emit('meta');
    await nombresPerillas();
  } catch (e) { /* el 0.9 que no tenga META sigue con la tabla local */ }
}
export async function nombresPerillas() {
  if (!conProtocolo() || !E.pr) return;
  for (let t = 0; t < 8; t++) {
    const m = E.pr.song[F.model + t], k = t + ',' + m;
    if (meta.perillasDe[k]) continue;
    const a = await E.cliente.meta(2, t, PAG.EDIT, 0), b = await E.cliente.meta(2, t, PAG.EDIT, 1);
    meta.perillasDe[k] = [...a, ...b].map(x => x.nombre);
    meta.rangosDe[k] = [...a, ...b].map(x => x.nombre ? [x.min, x.max] : [0, 127]);
  }
  emit('meta');
}

/* los nombres de los samples (TUYOS cambia cuando se manda o se borra uno) */
export async function refrescarSamples() {
  if (!conProtocolo()) return;
  try { for (let b = 0; b < 4; b++) { const l = await E.cliente.meta(5, b); if (Array.isArray(l)) meta.samples[b] = l; } emit('meta'); } catch (e) {}
}

/* ---- mandar cambios: el último valor gana; si uno va en camino, el siguiente espera su turno ---- */
const enVuelo = new Map();
function ultimo(clave, fn) {
  const x = enVuelo.get(clave);
  if (x) { x.sig = fn; return x.p; }
  const st = { sig: null };
  const correr = async f => {
    try { await f(); } catch (err) { emit('falla', err); }
    if (st.sig) { const g = st.sig; st.sig = null; return correr(g); }
    enVuelo.delete(clave);
  };
  st.p = correr(fn); enVuelo.set(clave, st);
  return st.p;
}

/* una perilla: se ve al instante y se manda; lo que el aparato deje (acotado) es lo que queda */
export function perilla(t, pag, sub, k, v) {
  const g = objetivo(t, pag, sub, k, E.ajustes);
  if (!g || !E.pr) return;
  if (!g.accion) g.set(E.pr, v);
  if (!conProtocolo()) return;
  E.ultimoEnvio = performance.now();
  return ultimo(`p${t}.${pag}.${sub}.${k}`, async () => {
    const q = await E.cliente.perilla(t, pag, sub, k, v);
    /* solo si el aparato acotó lo que mandamos y nadie lo ha movido después: una respuesta vieja no regresa la perilla */
    if (!g.accion && q !== v && g.get(E.pr) === v) { g.set(E.pr, q); emit('perilla', t, pag, sub, k); }
    if (pag === PAG.EDIT && k === 4) { await nombresPerillas(); emit('pista', t); }
    if (pag === PAG.EDIT && k === 1 && q !== v) emit('pista', t);   /* SAMPLE se movió al lugar con sample más cercano */
    if (g.accion) await releer();
    else if (pag === PAG.SCL && !sub && k >= 2) { clearTimeout(releerT); releerT = setTimeout(releer, 700); }
  });
}
export function idioma(v) {
  E.ajustes.idioma = v; emit('ajustes');
  if (conProtocolo()) return ultimo('idioma', async () => { E.ajustes.idioma = await E.cliente.ajuste(0, v); emit('ajustes'); await nombresDelAparato(); });
}
/* un paso entero (lo que la app ya cambió en E.pr) */
export function paso(p, t, s) {
  emit('paso', p, t, s);
  if (!conProtocolo()) return;
  E.ultimoEnvio = performance.now();
  return ultimo(`s${p}.${t}.${s}`, () => E.cliente.paso(p, t, s, registro(E.pr.pats[p][t].st[s])));
}
export function pista(p, t) {
  emit('pista', t);
  if (!conProtocolo()) return;
  const tr = E.pr.pats[p][t];
  return ultimo(`t${p}.${t}`, async () => { const { packPista } = await import('./proyecto.js'); return E.cliente.escribirPista(p, t, packPista(tr)); });
}
export function tocar(on) {
  E.tocando = on; emit('estado', true);
  if (conProtocolo()) return ultimo('play', () => E.cliente.transporte(0, on ? 1 : 0));
}
/* HOLD es el SOSTENER de cada sinte (ARP, perilla 4): con o sin arpegiador, las notas siguen sonando al soltar las
   teclas hasta tocar otra; con REC se graban así sin nada extra. Vive en el proyecto, así que llega con CAMBIO bit 0. */
export const sostenido = t => t >= 4 && !!E.pr?.song[off.arp(t, 3)];
export function hold(t, on) { const r = perilla(t, PAG.ARP, 0, 3, on ? 1 : 0); emit('hold'); return r; }
export function patron(n) {
  E.patron = n;
  if (!E.tocando) { E.cur = n; if (E.pr) E.pr.song[F.cur] = n; } else E.cola = n;
  emit('estado', true);
  if (conProtocolo()) return ultimo('pat', () => E.cliente.transporte(1, n));
}
export function mutes(m, s) {
  E.mute = m; E.solo = s; emit('estado', true);
  if (conProtocolo()) return ultimo('mute', async () => { await E.cliente.transporte(2, m); await E.cliente.transporte(3, s); });
}
export function elegirPista(t) { E.sel = t; pref('sel', t); emit('sel'); }

/* parar para escribir la flash: lo pregunta la interfaz antes de mandar ranuras, banco o samples */
export const pedirParar = { fn: async () => true };
export async function asegurarParado() {
  if (!E.tocando) return true;
  if (!await pedirParar.fn()) throw Object.assign(new Error('TOCANDO'), { codigo: 'TOCANDO' });
  await tocar(false);
  return true;
}
export { detectar, sinMidi, OBJ, PAG, Lec };
