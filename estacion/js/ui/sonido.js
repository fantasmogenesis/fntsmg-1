// Sonido: el motor de cada pista con sus 8 perillas (EDIT), los presets de fábrica, los dos LFO, el ARP de los sintes
// y el banco de sonidos del usuario (el mismo para todos los proyectos): renombrar, borrar, usar en la pista y guardar.
import { h, icono, perilla, KCOL, aviso, explicar, preguntar, pedirTexto } from './util.js';
import { botonHold } from './secuencia.js';
import { t } from '../i18n.js';
import { E, on, emit, perilla as mandar, elegirPista, hayProyecto, conProtocolo, asegurarParado, OBJ } from '../estado.js';
import { F, off } from '../proyecto.js';
import { meta, presets, objetivo, resolver, esSinte, perillasMotor, SAMPLE_M, sampleEn, BANCOS } from '../meta.js';
import { PAG } from '../proto.js';

let raiz, banco = null, bancoErr = null, presetsAp = {};

/* los presets en el orden de la perilla PRESET del aparato (EDITADO, INICIAL, fábrica, banco): los da META tipo 4 */
function listaPresets(tk, m) {
  if (!conProtocolo()) return presets(tk, m).map(p => p.nombre);
  const k = tk + ',' + m;
  if (!presetsAp[k]) { presetsAp[k] = []; E.cliente.meta(4, tk, m).then(l => { presetsAp[k] = l; if (!enMano()) dibujar(); }).catch(() => { delete presetsAp[k]; }); }
  return presetsAp[k];
}

export function montar(el) {
  raiz = el;
  const offs = [on('proyecto', dibujar), on('meta', dibujar), on('sel', dibujar), on('pista', dibujar), on('perilla', dibujar), on('hold', dibujar), on('banco', () => { banco = null; presetsAp = {}; leerBanco(); }), on('conexion', () => { banco = null; presetsAp = {}; dibujar(); })];
  dibujar(); leerBanco();
  return () => offs.forEach(f => f());
}
const enMano = () => raiz?.querySelector('.kn.mueve');

function grupo(tk, lista, color = i => KCOL(i), np = 8) {
  return h('div.perillas', { style: { '--np': np } }, ...lista.map(([pg, sub, k], n) => {
    const g = resolver(objetivo(tk, pg, sub, k, E.ajustes), E.pr);
    if (!g || !g.nombre) return h('div.kn.muerta', {}, h('span.nm', {}, '–'));
    return perilla({ nombre: g.nombre, min: g.min, max: g.max, valor: g.get(E.pr), estado: 'valor', color: color(n), fmt: g.fmt,
      alCambiar: v => mandar(tk, pg, sub, k, v) }).el;
  }));
}

function dibujar() {
  if (!raiz || enMano()) return;
  raiz.replaceChildren();
  if (!hayProyecto()) return;
  const tk = E.sel, pr = E.pr, m = pr.song[F.model + tk], ps = listaPresets(tk, m);
  const tabs = h('div.pistas-tabs', { role: 'tablist' }, ...meta.pistas.map((n, i) => h('button', { role: 'tab', 'aria-pressed': i === tk, 'aria-selected': i === tk, onclick: () => elegirPista(i) }, n)));
  const modelos = h('div.seg', { role: 'group', 'aria-label': t('son.motor') }, ...meta.modelos[tk].map((n, i) => h('button', { 'aria-pressed': i === m, onclick: () => mandar(tk, PAG.EDIT, 0, 4, i)?.then?.(dibujar) ?? dibujar() }, n)));
  const edit = Array.from({ length: 8 }, (_, i) => [PAG.EDIT, i >> 2, i & 3]);
  const motor = h('section.panel', {},
    h('header', {}, h('div', {}, h('div.eyebrow', {}, t('son.motor')), h('h2', {}, meta.modelos[tk][m] || '–')), modelos),
    grupo(tk, edit),
    m === SAMPLE_M(tk) ? elegirSample(tk, m)
      : ps.length ? h('div', { style: { display: 'grid', gap: '8px' } }, h('h3', {}, t('son.presets')),
        h('div.fila', {}, ...ps.map((nombre, i) => h('button.btn.g.mini', { onclick: async () => { await mandar(tk, PAG.EDIT, 0, 5, i); dibujar(); } }, nombre))))
      : h('p.muted.chico', {}, t('son.sinPresets')));
  const lfo = h('section.panel', {}, h('header', {}, h('h3', {}, 'LFO')),
    h('div.eyebrow', {}, 'LFO 1'), grupo(tk, [0, 1, 2, 3].map(k => [PAG.LFO, 0, k]), () => 'var(--ink)', 4),
    h('div.eyebrow', {}, 'LFO 2'), grupo(tk, [0, 1, 2, 3].map(k => [PAG.LFO, 1, k]), () => 'var(--ink)', 4));
  const arp = esSinte(tk) ? h('section.panel', {}, h('header', {}, h('h3', {}, t('son.arp')), h('span.pill', {}, t(pr.song[F.arp_on] >> tk & 1 ? 'son.arpOn' : 'son.arpOff'))),
    grupo(tk, [0, 1, 2, 3].map(k => [PAG.ARP, 0, k]), () => 'var(--ink)', 4), h('p.muted.chico', {}, t('son.arpAyuda'))) : null;
  raiz.append(
    h('div.top', {}, h('div', {}, h('div.eyebrow', {}, meta.pistas[tk]), h('h1', {}, t('nav.sonido'))), h('div.fila', {}, botonHold(tk), tabs)),
    motor, h('div.dos', {}, lfo, ...(arp ? [arp] : [])), panelBanco(tk, m));
}

/* ---- SAMPLE: el sample se elige con la perilla SAMPLE (página 0, perilla 1), por su lugar fijo de META 5 ---- */
function elegirSample(tk, m) {
  const v = E.pr.song[off.p(tk, m, 1)], actual = sampleEn(v)?.lugar;
  const bancos = BANCOS.map((nb, b) => ({ nb, l: meta.samples[b] || [] }));
  return h('div', { style: { display: 'grid', gap: '10px' } },
    h('div.fila', { style: { justifyContent: 'space-between' } }, h('h3', {}, t('son.sample')), h('span.muted.chico', {}, t('son.sampleAyuda'))),
    ...bancos.map(({ nb, l }, b) => h('div', { style: { display: 'grid', gap: '6px' } }, h('span.eyebrow', {}, b === 3 ? t('smp.tuyos') : nb),
      l.length ? h('div.fila', {}, ...l.map(x => h('button.btn.g.mini', { 'aria-pressed': x.lugar === actual, title: t('son.lugar', { n: x.lugar }),
        onclick: async () => { await mandar(tk, PAG.EDIT, 0, 1, x.lugar); dibujar(); } }, x.nombre)))
        : h('span.muted.chico', {}, b === 3 ? t('son.tuyosVacio') : '–'))));
}

/* ---- banco del usuario ---- */
async function leerBanco() {
  if (!conProtocolo()) { banco = null; return; }
  try { const b = await E.cliente.leerObjeto(OBJ.BANCO, 0); banco = desempacar(b); bancoErr = null; }
  catch (err) { bancoErr = err; banco = null; }
  if (!enMano()) dibujar();
}
function desempacar(d) {
  const n = d[1] || 0, o = [];
  for (let k = 0; k < n; k++) { const b = d.subarray(2 + k * 21, 23 + k * 21), z = b.indexOf(0, 2); o.push({ pista: b[0], modelo: b[1], nombre: new TextDecoder().decode(b.subarray(2, z < 0 || z > 12 ? 12 : z)), p: [...b.subarray(13, 21)] }); }
  return o;
}
function empacar(l) {
  const o = [0x83, l.length];
  for (const b of l) { const nm = new Uint8Array(11); nm.set(new TextEncoder().encode(b.nombre).slice(0, 10)); o.push(b.pista, b.modelo, ...nm, ...b.p); }
  return Uint8Array.from(o);
}
const compatible = (b, tk) => esSinte(tk) ? esSinte(b.pista) : b.pista === tk;

function panelBanco(tk, m) {
  const p = h('section.panel', {});
  const cab = h('header', {}, h('div', {}, h('h3', {}, t('son.banco')), h('p.muted.chico', {}, t('son.bancoAyuda'))),
    h('button.btn.g', { disabled: !banco || banco.length >= 96, onclick: () => guardarEnBanco(tk, m) }, icono('mas'), t('son.guardarEnBanco')));
  p.append(cab);
  if (!conProtocolo()) { p.append(h('div.vacio', {}, icono('usb'), t('son.bancoSinAparato'))); return p; }
  if (bancoErr) { p.append(h('div.banda.mal', {}, h('p', {}, explicar(bancoErr)), h('button.btn.g.mini', { onclick: leerBanco }, t('reintentar')))); return p; }
  if (!banco) { p.append(h('p.muted', {}, t('cargando'))); return p; }
  if (!banco.length) { p.append(h('div.vacio', {}, icono('sonido'), h('b', {}, t('son.bancoVacio')), h('span.chico', {}, t('son.bancoVacioAyuda')))); return p; }
  const l = h('div.lista');
  banco.forEach((b, i) => {
    const ok = compatible(b, tk);
    const nombre = h('span.nom', {}, b.nombre);
    l.append(h('div', {},
      h('span.n', {}, i + 1), nombre, h('span.muted.chico', {}, `${meta.pistas[b.pista]} · ${meta.modelos[b.pista]?.[b.modelo] || ''}`),
      h('button.btn.g.mini', { disabled: !ok, title: ok ? '' : t('son.noCompatible'), onclick: () => usar(b, tk) }, t('son.usar')),
      h('button.ico', { 'aria-label': t('renombrar'), onclick: () => renombrar(i, nombre) }, icono('lapiz')),
      h('button.ico', { 'aria-label': t('borrar'), onclick: () => borrar(i) }, icono('basura'))));
  });
  p.append(l, h('p.muted.chico', {}, t('son.bancoCuenta', { n: banco.length })));
  return p;
}
async function usar(b, tk) {
  try {
    await mandar(tk, PAG.EDIT, 0, 4, b.modelo);
    /* el aparato pone los sonidos del banco al final de la lista de PRESET: cargarlo así trae también su NIVEL */
    const l = await E.cliente.meta(4, tk, b.modelo).catch(() => []), i = l.lastIndexOf(b.nombre);
    if (i >= 0) await mandar(tk, PAG.EDIT, 0, 5, i);
    else for (let k = 0; k < 7; k++) await mandar(tk, PAG.EDIT, k >> 2, k & 3, b.p[k]);
    aviso(t('son.usado', { n: b.nombre, p: meta.pistas[tk] }));
    dibujar();
  } catch (err) { aviso(explicar(err), { mal: true }); }
}
function renombrar(i, el) {
  const inp = h('input', { type: 'text', value: banco[i].nombre, maxlength: 10, 'aria-label': t('renombrar'), style: { width: '12ch' } });
  const fin = async ok => {
    const v = inp.value.trim().toUpperCase().slice(0, 10);
    inp.replaceWith(el);
    if (!ok || !v || v === banco[i].nombre) return;
    try { await E.cliente.nombre(i, v); banco[i].nombre = v; el.textContent = v; }
    catch (err) { aviso(explicar(err), { mal: true }); }
  };
  inp.addEventListener('keydown', e => { if (e.key === 'Enter') fin(true); if (e.key === 'Escape') fin(false); });
  inp.addEventListener('blur', () => fin(true));
  el.replaceWith(inp); inp.focus(); inp.select();
}
async function borrar(i) {
  if (!await preguntar(t('son.borrarTitulo', { n: banco[i].nombre }), t('son.borrarTexto'), t('borrar'))) return;
  try { await asegurarParado(); await E.cliente.borrar(OBJ.BANCO, i); banco.splice(i, 1); presetsAp = {}; dibujar(); }
  catch (err) { aviso(explicar(err), { mal: true }); }
}
async function guardarEnBanco(tk, m) {
  const sug = (meta.modelos[tk][m] || 'SONIDO').slice(0, 7) + ' ' + (banco.length + 1);
  const nombre = await pedirTexto(t('son.nombreNuevo'), sug);
  if (!nombre) return;
  const p = Array.from({ length: 8 }, (_, i) => E.pr.song[off.p(tk, m, i)]);
  try {
    await asegurarParado();
    const nuevo = [...banco, { pista: tk, modelo: m, nombre, p }];
    await E.cliente.escribirObjeto(OBJ.BANCO, 0, empacar(nuevo));
    banco = nuevo; presetsAp = {}; aviso(t('son.guardado', { n: nombre })); dibujar();
  } catch (err) { aviso(explicar(err), { mal: true }); }
}
export { emit, perillasMotor };
