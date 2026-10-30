// La Estación: el armazón (secciones, conexión, avisos, tema e idioma). Cada sección es un módulo con montar(el)
// que devuelve con qué desmontarse.
import { h, icono, aviso, explicar, preguntar, $, ESTILOS, estiloPerilla } from './ui/util.js';
import { t, idiomaApp, ponerIdioma, IDIOMAS } from './i18n.js';
import { E, on, conectar, desconectar, detectar, sinMidi, hayProyecto, pedirParar } from './estado.js';
import { cargarEspejo, mostrarEn, paleta } from './ui/espejo.js';
import * as alm from './almacen.js';
import * as secuencia from './ui/secuencia.js';
import * as sonido from './ui/sonido.js';
import * as mezcla from './ui/mezcla.js';
import * as samples from './ui/samples.js';
import * as proyectos from './ui/proyectos.js';
import * as grabar from './ui/grabar.js';

const SECCIONES = [['secuencia', secuencia, true], ['sonido', sonido, true], ['mezcla', mezcla, true], ['samples', samples], ['proyectos', proyectos], ['grabar', grabar]];
let actual = null, desmontar = null, main, nav, pill, banda, memoria = false;

function tema(v) {
  if (v !== undefined) alm.pref('tema', v);
  const x = alm.pref('tema') || 'auto';
  if (x === 'auto') delete document.documentElement.dataset.theme; else document.documentElement.dataset.theme = x;
  return x;
}

function armar() {
  document.documentElement.lang = idiomaApp();
  document.title = 'FNTSMG-1 · ' + t('app.nombre');
  const espejo = h('canvas', { width: 360, height: 360 });
  mostrarEn(espejo);
  pill = h('button.pill', { onclick: () => E.conexion === 'sin' || E.conexion === 'error' ? ir('secuencia') : null });
  nav = h('nav.secciones', { 'aria-label': t('app.secciones') }, ...SECCIONES.map(([id]) => h('button', { 'data-id': id, onclick: () => ir(id) }, icono(id === 'mezcla' ? 'mezcla' : id), h('span', {}, t('nav.' + id)))));
  const temaBtn = h('button.ico', { 'aria-label': t('app.tema'), title: t('app.tema'), onclick: () => { const s = { auto: 'light', light: 'dark', dark: 'auto' }[tema()]; tema(s); temaBtn.replaceChildren(icono(s === 'dark' ? 'luna' : s === 'light' ? 'sol' : 'ojo')); aviso(t('app.tema_' + s)); } }, icono(tema() === 'dark' ? 'luna' : tema() === 'light' ? 'sol' : 'ojo'));
  const lang = h('select.sel', { 'aria-label': t('app.idioma'), onchange: e => { ponerIdioma(e.target.value); reiniciar(); } }, ...IDIOMAS.map(([c, n]) => h('option', { value: c, selected: c === idiomaApp() }, n)));
  const side = h('aside.side', {},
    h('div.mark', {}, 'FNTSMG-1', h('b', {}, t('app.nombre').toUpperCase())),
    h('div.pie', {}, h('div.fila.conex', {}, pill)),
    h('button.mirror', { 'aria-label': t('app.espejo'), onclick: () => ir('grabar') }, espejo, h('span.rotulo', {}, t('app.espejo')), h('span.rec-dot', { hidden: true }, h('i'), 'REC')),
    nav,
    h('div.pie', {},
      h('label.fila.chico', { style: { gap: '6px' } }, h('span.eyebrow', {}, t('app.perillas')),
        h('select.sel', { 'aria-label': t('app.perillas'), onchange: e => { alm.pref('perilla', e.target.value); reiniciar(); } },
          ...ESTILOS.map(x => h('option', { value: x, selected: x === estiloPerilla() }, t('app.perilla_' + x))))),
      h('div.fila', {}, temaBtn, lang)));
  banda = h('div');
  main = h('main', { id: 'contenido', tabindex: -1 });
  document.body.replaceChildren(h('a.btn', { href: '#contenido', style: { position: 'absolute', left: '-9999px' } }, t('app.saltar')), h('div.app', {}, side, h('div', { style: { minWidth: 0 } }, banda, main)));
  pintarConexion();
}
function reiniciar() { const s = actual; actual = null; desmontar?.(); desmontar = null; armar(); ir(s || 'secuencia'); }

function ir(id) {
  if (!SECCIONES.some(s => s[0] === id)) id = 'secuencia';
  if (location.hash !== '#' + id) history.replaceState(null, '', '#' + id);
  nav.querySelectorAll('button').forEach(b => b.setAttribute('aria-current', b.dataset.id === id ? 'page' : 'false'));
  if (actual === id && desmontar) return;
  desmontar?.(); desmontar = null; actual = id;
  main.replaceChildren();
  const [, mod, pideProyecto] = SECCIONES.find(s => s[0] === id);
  if (pideProyecto && !hayProyecto()) { main.append(bienvenida()); return; }
  const el = h('div', { style: { display: 'grid', gridTemplateColumns: 'minmax(0, 1fr)', gap: '16px', minWidth: 0 } });
  main.append(el);
  desmontar = mod.montar(el);
}
/* cuando llega o se va el proyecto, las secciones que lo piden se vuelven a montar */
function refrescar() {
  const pide = SECCIONES.find(s => s[0] === actual)?.[2];
  if (pide && !!desmontar !== hayProyecto()) { const s = actual; actual = null; ir(s); }
}

/* ---- sin conectar: la bienvenida (como «Help me connect» de Components) ---- */
function bienvenida() {
  const d = detectar(), r = sinMidi(d), buscando = E.conexion === 'buscando' || E.cargando > 0;
  const pasos = h('ol', {}, h('li', {}, t('bien.paso1')), h('li', {}, t('bien.paso2')), h('li', {}, t('bien.paso3')));
  return h('section.panel', { style: { maxWidth: '720px', margin: '6vh auto 0', padding: '28px', gap: '18px' } },
    h('div.eyebrow', {}, t('bien.eyebrow')), h('h1', {}, t('bien.titulo')),
    h('p.muted', {}, t('bien.texto')),
    r ? h('div.banda.mal', {}, h('p', {}, t('nomidi.' + r))) : pasos,
    h('div.fila', {},
      h('button.btn', { disabled: !!r || buscando, onclick: () => conectar('aparato') }, icono('usb'), t(buscando ? 'bien.buscando' : 'conectar')),
      h('button.btn.g', { disabled: buscando, onclick: () => conectar('demo') }, icono('tocar'), t('bien.demo'))),
    h('p.muted.chico', {}, t('bien.demoAyuda')));
}

function pintarConexion() {
  const c = E.conexion, nombre = { conectado: E.tr?.nombre || 'FM-1', demo: t('con.demo') }[c];
  pill.dataset.e = c;
  pill.replaceChildren(h('i'), h('span', {}, t('con.' + c) + (c === 'conectado' && E.info ? ' · ' + E.info.version.replace('FNTSMG-1 ', '') : '')));
  pill.title = nombre || '';
  banda.replaceChildren();
  const b = (txt, mal, ...acc) => banda.append(h('div', { style: { padding: '12px 24px 0' } }, h('div.banda' + (mal ? '.mal' : ''), { role: mal ? 'alert' : 'status' }, h('p', {}, txt), ...acc)));
  if (c === 'error') b(E.error?.codigo?.startsWith('NOMIDI_') ? t('nomidi.' + E.error.codigo.slice(7)) : explicar(E.error), true,
    h('button.btn.g.mini', { onclick: () => conectar('aparato') }, t('reintentar')), h('button.btn.g.mini', { onclick: () => conectar('demo') }, t('bien.demo')));
  if (c === 'perdido') b(t('con.perdidoTexto'), true, h('button.btn.g.mini', { onclick: () => conectar('aparato') }, t('reintentar')));
  if (c === 'espejo') b(t('con.espejoTexto'), false, h('button.btn.g.mini', { onclick: () => ir('grabar') }, t('nav.grabar')));
  if (c === 'demo') b(t('con.demoTexto'), false, h('button.btn.g.mini', { onclick: () => conectar('aparato') }, icono('usb'), t('conectar')), h('button.btn.g.mini', { onclick: () => desconectar() }, t('con.salirDemo')));
  if (c === 'conectado') banda.append(h('div', { style: { padding: '12px 24px 0', display: 'flex', justifyContent: 'flex-end' } }, h('button.btn.g.mini', { onclick: () => desconectar() }, t('desconectar'))));
  if (E.info?.seguro) b(t('con.seguro'), true);
  if (memoria) b(t('app.sinGuardar'), false);
  if (E.cargando) banda.append(h('div', { style: { padding: '8px 24px 0' } }, h('div.prog', {}, h('i.cargando', { style: { width: '30%' } }))));
}

async function inicio() {
  tema();
  const p = alm.pref('paleta'); if (p) paleta(p);
  await cargarEspejo();
  pedirParar.fn = () => preguntar(t('parar.titulo'), t('parar.texto'), t('parar'));
  armar();
  on('conexion', () => { pintarConexion(); if (!hayProyecto() && SECCIONES.find(s => s[0] === actual)?.[2]) { desmontar?.(); desmontar = null; const s = actual; actual = null; ir(s); } });
  on('cargando', pintarConexion);
  on('progreso', p => { const i = $('.cargando'); if (i) i.style.width = Math.round(p * 100) + '%'; });
  on('proyecto', refrescar);
  on('falla', err => aviso(explicar(err), { mal: true }));
  on('grabando', () => { const d = $('.mirror .rec-dot'); if (d) d.hidden = !grabar.grabando(); });
  /* el rótulo del espejo dice si llega señal */
  setInterval(() => { const r = $('.mirror .rotulo'); if (r) r.textContent = performance.now() - E.ultimoEstado < 3000 ? t('app.espejo') : t('app.sinSenal'); }, 1000);
  addEventListener('hashchange', () => ir(location.hash.slice(1)));
  ir(location.hash.slice(1) || 'secuencia');
  memoria = await alm.guardaEnMemoria(); if (memoria) pintarConexion();
  /* volver como se quedó: la demo arranca sola; el aparato, solo si ya hay permiso de MIDI (si no, el navegador preguntaría sin que nadie tocara nada) */
  const modo = alm.pref('modo');
  if (modo === 'demo') conectar('demo');
  else if (modo === 'aparato' && !sinMidi()) {
    try { const s = await navigator.permissions?.query({ name: 'midi', sysex: true }); if (s?.state === 'granted') conectar('aparato'); } catch (e) {}
  }
}
inicio();
