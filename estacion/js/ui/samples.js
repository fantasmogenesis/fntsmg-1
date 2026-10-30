// Samples: arrastras audio, lo recortas, eliges tasa y normalizado, y la Estación lo convierte aquí mismo al formato
// del aparato (mono, ADPCM IMA en bloques de 64, como gen_samples.py). El medidor cuenta en sectores de 4 KB, que es
// como el FM-1 aparta la zona. Se manda parado (la flash solo se escribe sin tocar) y se guarda una copia en la compu.
import { h, icono, aviso, explicar, preguntar, bajarArchivo, kb } from './util.js';
import { t } from '../i18n.js';
import { E, on, conProtocolo, asegurarParado, refrescarSamples, perilla as mandar, OBJ } from '../estado.js';
import { meta, SAMPLE_M } from '../meta.js';
import { PAG } from '../proto.js';
import { Esc } from '../proto.js';
import { codificar, decodificar, bytesPara } from '../adpcm.js';
import * as alm from '../almacen.js';

const TASAS = [44100, 32000, 22050, 16000, 11025], SECTOR = 4096, LUGARES = 32;
let raiz, items = [], lista = null, listaErr = null, ac = null, sonando = null, ultimo = null;
const audio = () => (ac ||= new (globalThis.AudioContext || globalThis.webkitAudioContext)());

export function montar(el) {
  raiz = el;
  const offs = [on('samples', leerLista), on('conexion', () => { lista = null; leerLista(); })];
  leerLista();
  return () => offs.forEach(f => f());
}

async function leerLista() {
  if (!conProtocolo()) { lista = null; dibujar(); return; }
  try { lista = await E.cliente.lista(OBJ.SAMPLE); listaErr = null; } catch (err) { listaErr = err; lista = null; }
  dibujar();
}
const zona = () => E.info?.zona || 136 * 1024;
const sectores = b => Math.ceil(b / SECTOR) * SECTOR;
const usadoAparato = () => lista ? zona() - lista.libre : 0;
const libres = () => { const u = new Set([...(lista?.samples || []).map(s => s.indice), ...items.map(i => i.lugar)]); return [...Array(LUGARES).keys()].filter(i => !u.has(i)); };

/* ---- convertir ---- */
async function abrirArchivos(files) {
  for (const f of files) {
    try {
      const buf = await audio().decodeAudioData(await f.arrayBuffer());
      const n = buf.length, mono = new Float32Array(n);
      for (let c = 0; c < buf.numberOfChannels; c++) { const d = buf.getChannelData(c); for (let i = 0; i < n; i++) mono[i] += d[i] / buf.numberOfChannels; }
      const lugar = libres()[0];
      const nombre = f.name.replace(/\.[^.]+$/, '').replace(/[^\p{L}\p{N} ]+/gu, ' ').toUpperCase().slice(0, 12).trim() || 'SAMPLE';
      items.push({ id: Math.random(), nombre, mono, rate: buf.sampleRate, ini: 0, fin: n, normalizar: true, tasa: buf.sampleRate <= 22050 ? 22050 : 44100, lugar: lugar ?? 0, raiz: 60, lazo: 0, bytes: null, prog: 0 });
    } catch (e) { aviso(t('smp.noAbre', { n: f.name }), { mal: true }); }
  }
  dibujar();
}
const muestrasDe = it => Math.ceil((it.fin - it.ini) * it.tasa / it.rate);
const bytesDe = it => bytesPara(muestrasDe(it));

async function convertir(it) {
  const n = muestrasDe(it);
  const off = new OfflineAudioContext(1, Math.max(1, n), it.tasa);
  const b = off.createBuffer(1, it.fin - it.ini, it.rate);
  b.copyToChannel(it.mono.subarray(it.ini, it.fin), 0);
  const src = off.createBufferSource(); src.buffer = b; src.connect(off.destination); src.start();
  const y = (await off.startRendering()).getChannelData(0);
  let pico = 0; for (let i = 0; i < y.length; i++) pico = Math.max(pico, Math.abs(y[i]));
  const g = it.normalizar && pico > 0 ? 0.89 / pico : 1, fade = Math.min(y.length, Math.round(it.tasa * 0.004));
  const s = new Int16Array(y.length);
  for (let i = 0; i < y.length; i++) { const f = i >= y.length - fade ? (y.length - i) / fade : 1; s[i] = Math.max(-32767, Math.min(32767, Math.round(y[i] * g * f * 32767))); }
  return { datos: codificar(s), muestras: s.length };
}
async function oir(it) {
  pararOir();
  const { datos, muestras } = await convertir(it), y = decodificar(datos, muestras);
  const b = audio().createBuffer(1, muestras, it.tasa), d = b.getChannelData(0);
  for (let i = 0; i < muestras; i++) d[i] = y[i] / 32768;
  const s = audio().createBufferSource(); s.buffer = b; s.connect(audio().destination); s.start(); sonando = s;
}
function pararOir() { try { sonando?.stop(); } catch (e) {} sonando = null; }

async function enviar(it) {
  try {
    await asegurarParado();
    const { datos, muestras } = await convertir(it);
    const extra = new Esc().u21(it.tasa).u21(muestras).u7(it.raiz).u7(it.lazo).u21(0).u21(it.lazo ? muestras : 0).b;
    it.prog = 0.001; dibujar();
    await E.cliente.escribirObjeto(OBJ.SAMPLE, it.lugar, datos, { nombre: it.nombre, extra }, p => { it.prog = p; pintarProg(it); });
    await alm.escribir('sample:' + it.lugar, { nombre: it.nombre, datos, tasa: it.tasa, muestras, raiz: it.raiz, lazo: it.lazo, lazoIni: 0, lazoFin: it.lazo ? muestras : 0, fecha: Date.now() });
    items = items.filter(x => x !== it);
    ultimo = { nombre: it.nombre, indice: it.lugar };
    await leerLista(); refrescarSamples();
  } catch (err) { it.prog = 0; aviso(explicar(err), { mal: true }); dibujar(); }
}
async function borrarDelAparato(s) {
  if (!await preguntar(t('smp.borrarTitulo', { n: s.nombre }), t('smp.borrarTexto'), t('borrar'))) return;
  try { await asegurarParado(); await E.cliente.borrar(OBJ.SAMPLE, s.indice); await alm.borrar('sample:' + s.indice); await leerLista(); refrescarSamples(); }
  catch (err) { aviso(explicar(err), { mal: true }); }
}
async function bajarWav(s) {
  try {
    const d = await E.cliente.leerObjeto(OBJ.SAMPLE, s.indice), y = decodificar(d, s.muestras);
    bajarArchivo(s.nombre.toLowerCase().replace(/\s+/g, '-') + '.wav', wav(y, s.tasa), 'audio/wav');
  } catch (err) { aviso(explicar(err), { mal: true }); }
}
function wav(y, rate) {
  const b = new ArrayBuffer(44 + y.length * 2), v = new DataView(b), w = (o, s) => [...s].forEach((c, i) => v.setUint8(o + i, c.charCodeAt(0)));
  w(0, 'RIFF'); v.setUint32(4, 36 + y.length * 2, true); w(8, 'WAVEfmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, rate, true); v.setUint32(28, rate * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, y.length * 2, true);
  y.forEach((s, i) => v.setInt16(44 + i * 2, s, true));
  return b;
}

/* ---- pantalla ---- */
function dibujar() {
  if (!raiz) return;
  raiz.replaceChildren();
  const pend = items.reduce((a, it) => a + sectores(bytesDe(it)), 0), usado = usadoAparato(), Z = zona(), libre = Z - usado - pend;
  const med = h('div', { style: { display: 'grid', gap: '6px' } },
    h('div.medidor', { role: 'meter', 'aria-valuemin': 0, 'aria-valuemax': Z, 'aria-valuenow': usado + pend, 'aria-label': t('smp.espacio') },
      h('i', { style: { width: (usado * 100 / Z) + '%', background: 'var(--ink)' } }), h('i', { style: { width: (Math.min(pend, Z - usado) * 100 / Z) + '%', background: libre < 0 ? 'var(--rec)' : 'var(--cobalt)' } })),
    h('div.fila.chico', { style: { justifyContent: 'space-between' } },
      h('span', {}, h('b', {}, t('smp.enAparato')), ' ', kb(usado)), h('span', { style: { color: 'var(--cobalt)' } }, h('b', {}, t('smp.porMandar')), ' ', kb(pend)),
      h('span', { style: { color: libre < 0 ? 'var(--rec)' : '' } }, h('b', {}, t('smp.libre')), ' ', libre < 0 ? '−' + kb(-libre) : kb(libre), ' · ', t('smp.segundos', { s: (Math.max(0, libre) / 36 * 64 / 44100).toFixed(1) }))));
  const input = h('input', { type: 'file', accept: 'audio/*,.wav,.aif,.aiff,.flac,.mp3', multiple: true, hidden: true, onchange: e => abrirArchivos([...e.target.files]) });
  const zonaSoltar = h('div.soltar', { role: 'button', tabindex: 0, onclick: () => input.click(), onkeydown: e => { if (e.key === 'Enter' || e.key === ' ') input.click(); } },
    icono('onda'), h('b', {}, t('smp.soltar')), h('span.chico', {}, t('smp.soltarAyuda')), input);
  ['dragenter', 'dragover'].forEach(ev => zonaSoltar.addEventListener(ev, e => { e.preventDefault(); zonaSoltar.classList.add('sobre'); }));
  ['dragleave', 'drop'].forEach(ev => zonaSoltar.addEventListener(ev, () => zonaSoltar.classList.remove('sobre')));
  zonaSoltar.addEventListener('drop', e => { e.preventDefault(); abrirArchivos([...e.dataTransfer.files]); });

  raiz.append(h('div.top', {}, h('div', {}, h('div.eyebrow', {}, t('smp.eyebrow')), h('h1', {}, t('nav.samples')))),
    h('section.panel', {}, h('header', {}, h('h3', {}, t('smp.espacio')), h('span.muted.chico', {}, t('smp.zona', { k: kb(Z) }))), med));
  if (!conProtocolo()) raiz.append(h('div.banda', {}, h('p', {}, t('smp.sinAparato'))));
  if (ultimo && conProtocolo()) raiz.append(h('div.banda', { role: 'status', style: { borderLeftColor: 'var(--ok)' } },
    h('p', {}, h('b', {}, t('smp.enviado', { n: ultimo.nombre, l: ultimo.indice + 1 })), ' ', t('smp.dondeQueda')),
    botonProbar(ultimo.indice, ultimo.nombre), h('button.ico', { 'aria-label': t('cerrar'), onclick: () => { ultimo = null; dibujar(); } }, icono('x'))));
  raiz.append(zonaSoltar, ...items.map(editor));
  raiz.append(panelAparato());
}
function editor(it) {
  const p = h('section.panel', {});
  const dur = (it.fin - it.ini) / it.rate, n = muestrasDe(it), b = bytesDe(it);
  const nombre = h('input', { type: 'text', value: it.nombre, maxlength: 12, 'aria-label': t('nombre'), style: { flex: 1, textTransform: 'uppercase' }, oninput: e => it.nombre = e.target.value.toUpperCase().slice(0, 12) });
  const lugares = h('select.sel', { 'aria-label': t('smp.lugar'), onchange: e => { it.lugar = +e.target.value; dibujar(); } },
    ...[...Array(LUGARES).keys()].map(i => { const ocupadoPor = lista?.samples.find(s => s.indice === i); return h('option', { value: i, selected: i === it.lugar }, `${t('smp.tuyos')} ${i + 1}${ocupadoPor ? ' · ' + t('smp.reemplaza', { n: ocupadoPor.nombre }) : ''}`); }));
  const onda = h('div.onda');
  p.append(
    h('header', {}, h('div.fila', { style: { flex: 1, minWidth: '220px' } }, nombre), h('div.fila', {},
      h('button.btn.g.mini', { onclick: () => oir(it) }, icono('tocar'), t('smp.oir')),
      h('button.ico', { 'aria-label': t('quitar'), onclick: () => { items = items.filter(x => x !== it); dibujar(); } }, icono('x')))),
    onda,
    h('div.fila', {},
      h('div.seg', { role: 'group', 'aria-label': t('smp.tasa') }, ...TASAS.map(r => h('button', { 'aria-pressed': it.tasa === r, onclick: () => { it.tasa = r; dibujar(); } }, ({ 44100: '44.1', 32000: '32', 22050: '22', 16000: '16', 11025: '11' })[r] + ' kHz'))),
      h('label.fila.chico', {}, h('input', { type: 'checkbox', checked: it.normalizar, onchange: e => it.normalizar = e.target.checked }), t('smp.normalizar')),
      h('label.fila.chico', {}, h('input', { type: 'checkbox', checked: !!it.lazo, onchange: e => it.lazo = e.target.checked ? 1 : 0 }), t('smp.lazo')),
      lugares),
    h('div.fila.chico.muted', {}, `${dur.toFixed(2)} s · ${n} ${t('smp.muestras')} · ${kb(b)} · ${t('smp.ocupa', { k: kb(sectores(b)) })}`),
    h('div.prog', { 'data-id': it.id }, h('i', { style: { width: (it.prog * 100) + '%' } })),
    h('div.fila', { style: { justifyContent: 'flex-end' } },
      h('button.btn', { disabled: !conProtocolo() || it.prog > 0, onclick: () => enviar(it) }, icono('subir'), t('smp.enviar'))));
  requestAnimationFrame(() => dibujarOnda(onda, it));
  return p;
}
function pintarProg(it) { const i = raiz?.querySelector(`.prog[data-id="${it.id}"] i`); if (i) i.style.width = (it.prog * 100) + '%'; }

function dibujarOnda(caja, it) {
  const c = h('canvas'), W = caja.clientWidth || 600, H = 120, dpr = devicePixelRatio || 1;
  c.width = W * dpr; c.height = H * dpr; caja.append(c);
  const x = c.getContext('2d'); x.scale(dpr, dpr);
  const cs = getComputedStyle(document.documentElement);
  x.fillStyle = cs.getPropertyValue('--ink').trim();
  const n = it.mono.length, por = n / W;
  for (let px = 0; px < W; px++) { let mi = 0, ma = 0; for (let i = px * por | 0, e = Math.min(n, (px + 1) * por | 0); i < e; i++) { const v = it.mono[i]; if (v < mi) mi = v; if (v > ma) ma = v; } x.fillRect(px, H / 2 - ma * H / 2, 1, Math.max(1, (ma - mi) * H / 2)); }
  const fa = h('div.fuera'), fb = h('div.fuera'), ms = h('div.m', {}, h('span', {}, 'S')), me = h('div.m', {}, h('span', {}, 'E'));
  const pintar = () => { const a = it.ini / n * 100, b = it.fin / n * 100; fa.style.left = 0; fa.style.width = a + '%'; fb.style.left = b + '%'; fb.style.right = 0; ms.style.left = a + '%'; me.style.left = b + '%'; };
  caja.append(fa, fb, ms, me); pintar();
  const arrastrar = (m, cual) => {
    m.addEventListener('pointerdown', e => { m.setPointerCapture(e.pointerId); e.preventDefault(); });
    m.addEventListener('pointermove', e => {
      if (!m.hasPointerCapture(e.pointerId)) return;
      const r = caja.getBoundingClientRect(), v = Math.round(Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * n);
      if (cual === 'ini') it.ini = Math.min(v, it.fin - 64); else it.fin = Math.max(v, it.ini + 64);
      pintar();
    });
    m.addEventListener('pointerup', () => dibujar());
  };
  arrastrar(ms, 'ini'); arrastrar(me, 'fin');
}

/* probarlo ya: la pista elegida pasa a SAMPLE y su perilla SAMPLE al lugar 96 + n (TUYOS n) */
function botonProbar(indice, nombre) {
  const tk = E.sel;
  return h('button.btn.g.mini', { disabled: !E.pr, onclick: async () => {
    try { await mandar(tk, PAG.EDIT, 0, 4, SAMPLE_M(tk)); await mandar(tk, PAG.EDIT, 0, 1, 96 + indice); aviso(t('smp.probando', { n: nombre, p: meta.pistas[tk] })); }
    catch (err) { aviso(explicar(err), { mal: true }); }
  } }, icono('tocar'), t('smp.probarEn', { p: meta.pistas[tk] }));
}
function panelAparato() {
  const p = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('smp.tuyos')), h('p.muted.chico', {}, t('smp.tuyosAyuda'))), h('button.ico', { 'aria-label': t('reintentar'), onclick: leerLista }, icono('reloj'))));
  if (!conProtocolo()) { p.append(h('div.vacio', {}, icono('usb'), t('smp.conecta'))); return p; }
  if (listaErr) { p.append(h('div.banda.mal', {}, h('p', {}, explicar(listaErr)), h('button.btn.g.mini', { onclick: leerLista }, t('reintentar')))); return p; }
  if (!lista) { p.append(h('p.muted', {}, t('cargando'))); return p; }
  if (!lista.samples.length) { p.append(h('div.vacio', {}, icono('onda'), h('b', {}, t('smp.vacio')), h('span.chico', {}, t('smp.vacioAyuda')))); return p; }
  p.append(h('div.lista', {}, ...lista.samples.map(s => h('div', {}, h('span.n', {}, s.indice + 1), h('span.nom', {}, s.nombre),
    h('span.muted.chico', {}, `${(s.muestras / s.tasa).toFixed(2)} s · ${(s.tasa / 1000).toFixed(1)} kHz · ${kb(s.bytes)}`),
    h('span.muted.chico', {}, t('son.lugar', { n: 96 + s.indice })), botonProbar(s.indice, s.nombre),
    h('button.ico', { 'aria-label': t('smp.bajarWav'), onclick: () => bajarWav(s) }, icono('bajar')),
    h('button.ico', { 'aria-label': t('borrar'), onclick: () => borrarDelAparato(s) }, icono('basura'))))));
  return p;
}
