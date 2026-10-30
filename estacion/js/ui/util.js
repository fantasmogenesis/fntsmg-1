// Piezas chicas de la interfaz: crear nodos, íconos SVG, la perilla, avisos y preguntas.
import { t } from '../i18n.js';

export function h(tag, at = {}, ...hijos) {
  const [n, ...cls] = tag.split('.');
  const e = n === 'svg' || n === 'path' || n === 'circle' || n === 'line' || n === 'rect' || n === 'polyline' ? document.createElementNS('http://www.w3.org/2000/svg', n) : document.createElement(n || 'div');
  if (cls.length) e.setAttribute('class', cls.join(' '));
  for (const [k, v] of Object.entries(at || {})) {
    if (k.startsWith('aria-') && typeof v === 'boolean') { e.setAttribute(k, String(v)); continue; }
    if (v == null || v === false) continue;
    if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'style' && typeof v === 'object') for (const [a, b] of Object.entries(v)) a.startsWith('--') ? e.style.setProperty(a, b) : e.style[a] = b;
    else if (k === 'html') e.innerHTML = v;
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of hijos.flat(9)) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(c));
  return e;
}
export const $ = (s, r = document) => r.querySelector(s);

const P = {
  secuencia: 'M3 6h18M3 12h18M3 18h18M8 3v18M16 3v18', sonido: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 12l4-4',
  mezcla: 'M6 3v18M12 3v18M18 3v18M4 15h4M10 8h4M16 12h4', samples: 'M2 12h3l2-6 3 12 3-9 2 5 2-2h5',
  proyectos: 'M4 4h16v16H4zM4 9h16M9 4v5', grabar: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z',
  tocar: 'M7 4l13 8-13 8z', parar: 'M6 6h12v12H6z', usb: 'M9 7V3M15 7V3M7 7h10v5a5 5 0 0 1-10 0zM12 17v4',
  x: 'M6 6l12 12M18 6L6 18', mas: 'M12 5v14M5 12h14', subir: 'M12 19V5M6 11l6-6 6 6', bajar: 'M12 5v14M6 13l6 6 6-6',
  basura: 'M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13', lapiz: 'M4 20h4L19 9l-4-4L4 16zM14 6l4 4', ojo: 'M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12zM12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z',
  lleno: 'M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5', luna: 'M20 14A8 8 0 1 1 10 4a6 6 0 0 0 10 10z', sol: 'M12 7a5 5 0 1 0 0 10 5 5 0 0 0 0-10zM12 1v2M12 21v2M4 4l1.5 1.5M18.5 18.5L20 20M1 12h2M21 12h2M4 20l1.5-1.5M18.5 5.5L20 4',
  globo: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM3 12h18M12 3c3 3 3 15 0 18M12 3c-3 3-3 15 0 18', ayuda: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM9.5 9a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .9-1 1.7M12 17h.01',
  archivo: 'M6 3h8l4 4v14H6zM14 3v4h4', reloj: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2', aviso: 'M12 3l10 18H2zM12 10v5M12 18h.01',
  check: 'M5 12l5 5 9-10', onda: 'M2 12h2l2-5 3 10 3-14 3 16 3-9 2 2h2', copia: 'M8 8h12v12H8zM4 16V4h12', escena: 'M3 5h18v14H3zM8 21h8',
  glo: 'M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM12 2v3M12 19v3M2 12h3M19 12h3M5 5l2 2M17 17l2 2M5 19l2-2M17 7l2-2',
};
export function icono(n, cls = 'i') {
  const s = h('svg.' + cls, { viewBox: '0 0 24 24', 'aria-hidden': 'true' });
  s.append(h('path', { d: P[n] || P.x }));
  return s;
}

/* ---- la perilla: arrastre vertical (o rueda, o flechas), doble clic la regresa ---- */
const ARC = 270, R0 = -135;
const polar = (a, r = 22) => [28 + r * Math.sin(a * Math.PI / 180), 28 - r * Math.cos(a * Math.PI / 180)];
function arco(a0, a1, r = 22) { const [x0, y0] = polar(a0, r), [x1, y1] = polar(a1, r); return `M${x0} ${y0}A${r} ${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1} ${y1}`; }

/* tres maneras de dibujar la misma perilla (Cris elige): anillo (girar arrastrando), barra (deslizar de lado a lado, como
   los parámetros de Ableton) y columna (un fader chico, como la mezcla). Todas se arrastran, ruedan y responden a flechas. */
export const ESTILOS = ['anillo', 'barra', 'columna'];
export const estiloPerilla = () => { try { const v = JSON.parse(localStorage.getItem('estacion.perilla')); return ESTILOS.includes(v) ? v : 'anillo'; } catch (e) { return 'anillo'; } };

export function perilla({ nombre = '', min = 0, max = 127, valor = 0, color = 'var(--ink)', fmt = String, estado = 'base', base = null, alCambiar, alSoltar, alQuitar, desactivada = false, estilo = estiloPerilla() }) {
  const nm = h('span.nm', {}, nombre), vl = h('span.vl');
  const k = { v: valor, min, max, fmt, estado, base };
  const frac = v => (v - k.min) / ((k.max - k.min) || 1);
  let el, dibujo;
  if (estilo === 'anillo') {
    const pista = h('path', { d: arco(R0, R0 + ARC), fill: 'none', stroke: 'var(--rule)', 'stroke-width': 5, 'stroke-linecap': 'round' });
    const ar = h('path.ar', { fill: 'none', stroke: color, 'stroke-width': 5, 'stroke-linecap': 'round', style: { color } });
    const marca = h('circle', { r: 2.5, fill: 'var(--ink-3)' });
    const aguja = h('line', { x1: 28, y1: 28, stroke: 'var(--ink)', 'stroke-width': 2.5, 'stroke-linecap': 'round' });
    el = h('div.kn.anillo', {}, h('svg', { viewBox: '0 0 56 56' }, pista, ar, marca, aguja), nm, vl);
    dibujo = () => {
      const f = frac(k.v), a = R0 + ARC * f;
      ar.setAttribute('d', f > 0.004 ? arco(R0, a) : '');
      ar.setAttribute('stroke', k.estado === 'base' ? 'var(--ink-3)' : color);
      const [x, y] = polar(a, 16); aguja.setAttribute('x2', x); aguja.setAttribute('y2', y);
      if (k.base != null && k.estado === 'lock') { const [bx, by] = polar(R0 + ARC * frac(k.base), 28); marca.setAttribute('cx', bx); marca.setAttribute('cy', by); marca.style.display = ''; } else marca.style.display = 'none';
    };
  } else {
    const lleno = h('i.lleno'), marca = h('i.marca'), pista = h('span.pista', {}, lleno, marca);
    el = estilo === 'barra' ? h('div.kn.barra', {}, h('span.cab', {}, nm, vl), pista) : h('div.kn.columna', {}, pista, nm, vl);
    dibujo = () => {
      const f = frac(k.v) * 100;
      lleno.style.background = k.estado === 'base' ? 'var(--ink-3)' : color;
      if (estilo === 'barra') lleno.style.width = f + '%'; else lleno.style.height = f + '%';
      if (k.base != null && k.estado === 'lock') { marca.style.display = ''; marca.style[estilo === 'barra' ? 'left' : 'bottom'] = frac(k.base) * 100 + '%'; } else marca.style.display = 'none';
    };
  }
  el.tabIndex = desactivada ? -1 : 0;
  el.setAttribute('role', 'slider'); el.setAttribute('aria-label', nombre); el.setAttribute('aria-valuemin', min); el.setAttribute('aria-valuemax', max);
  k.el = el;
  k.pintar = () => {
    dibujo();
    vl.textContent = k.fmt(k.v);
    el.classList.toggle('base', k.estado === 'base'); el.classList.toggle('lock', k.estado === 'lock'); el.classList.toggle('valor', k.estado === 'valor');
    el.classList.toggle('muerta', desactivada);
    el.setAttribute('aria-valuenow', k.v); el.setAttribute('aria-valuetext', vl.textContent);
  };
  k.poner = (v, estado = k.estado, base = k.base) => { k.v = v; k.estado = estado; k.base = base; k.pintar(); };
  k.nombre = s => { nm.textContent = s; el.setAttribute('aria-label', s); };
  k.pintar();
  if (desactivada) return k;
  const mover = v => { v = Math.max(k.min, Math.min(k.max, Math.round(v))); if (v === k.v && k.estado !== 'base') return; k.v = v; if (k.estado === 'base' && alQuitar) k.estado = 'lock'; k.pintar(); alCambiar?.(v); };
  /* anillo: relativo (arrastrar hacia arriba o a la derecha sube); la distancia para todo el recorrido depende de cuántos
     valores tiene, así VELOCIDAD (3 valores) no pide 180 px. Barra y columna: el valor está donde está el dedo. */
  let x0 = 0, y0 = 0, v0 = 0, acc = 0, id = null;
  const absoluto = e => { const r = el.querySelector('.pista').getBoundingClientRect(); return k.min + (k.max - k.min) * (estilo === 'barra' ? (e.clientX - r.left) / r.width : (r.bottom - e.clientY) / r.height); };
  el.addEventListener('pointerdown', e => {
    if (e.button) return;
    id = e.pointerId; el.setPointerCapture(id); x0 = e.clientX; y0 = e.clientY; v0 = k.v; acc = 0; el.classList.add('mueve'); e.preventDefault(); el.focus({ preventScroll: true });
    if (estilo !== 'anillo' && e.target.closest('.pista')) mover(absoluto(e));
  });
  el.addEventListener('pointermove', e => {
    if (e.pointerId !== id) return;
    const rango = k.max - k.min;
    if (estilo === 'anillo') { const px = Math.max(48, Math.min(200, rango * 1.6)) * (e.shiftKey ? 4 : 1); mover(v0 + ((y0 - e.clientY) + (e.clientX - x0)) * rango / px); }
    else mover(absoluto(e));
  });
  const fin = e => { if (e.pointerId !== id) return; id = null; el.classList.remove('mueve'); alSoltar?.(k.v); };
  el.addEventListener('pointerup', fin); el.addEventListener('pointercancel', fin);
  el.addEventListener('wheel', e => { e.preventDefault(); acc += e.deltaY - e.deltaX; const st = Math.trunc(acc / 24); if (st) { acc -= st * 24; mover(k.v - st); clearTimeout(k.wt); k.wt = setTimeout(() => alSoltar?.(k.v), 250); } }, { passive: false });
  el.addEventListener('keydown', e => {
    const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 10, PageDown: -10 }[e.key];
    if (d) { e.preventDefault(); mover(k.v + d * (e.shiftKey ? 10 : 1)); alSoltar?.(k.v); }
    if ((e.key === 'Delete' || e.key === 'Backspace') && alQuitar) { e.preventDefault(); alQuitar(); }
  });
  el.addEventListener('dblclick', () => alQuitar?.());
  el.addEventListener('contextmenu', e => { if (alQuitar) { e.preventDefault(); alQuitar(); } });
  return k;
}

/* ---- avisos que se van solos ---- */
export function aviso(texto, { mal = false, accion = null, ms = 4200 } = {}) {
  let c = $('.avisos'); if (!c) { c = h('div.avisos', { role: 'status', 'aria-live': 'polite' }); document.body.append(c); }
  const a = h('div.aviso' + (mal ? '.mal' : ''), {}, h('span', { style: { flex: 1 } }, texto));
  if (accion) a.append(h('button.btn.g.mini', { onclick: () => { accion.fn(); a.remove(); } }, accion.texto));
  c.append(a);
  setTimeout(() => a.remove(), accion ? ms * 2 : ms);
}
/* el texto en palabras de un error del aparato o del navegador */
export function explicar(err) {
  const c = err?.codigo || err?.message;
  const k = 'err.' + c;
  const s = t(k);
  return s === k ? t('err.otro', { c: c || '?' }) : s;
}

export function preguntar(titulo, texto, si = t('ok'), no = t('cancelar')) {
  return new Promise(ok => {
    const d = h('dialog', {}, h('form.cuerpo', { method: 'dialog' }, h('h2', {}, titulo), h('p.muted', {}, texto),
      h('div.fila', { style: { justifyContent: 'flex-end' } }, h('button.btn.g', { value: 'no' }, no), h('button.btn', { value: 'si' }, si))));
    document.body.append(d);
    d.addEventListener('close', () => { ok(d.returnValue === 'si'); d.remove(); });
    d.showModal();
  });
}
export function pedirTexto(titulo, valor = '', max = 10) {
  return new Promise(ok => {
    const inp = h('input', { type: 'text', value: valor, maxlength: max, required: true, style: { width: '100%', textTransform: 'uppercase' } });
    const d = h('dialog', {}, h('form.cuerpo', { method: 'dialog' }, h('h2', {}, titulo), inp,
      h('div.fila', { style: { justifyContent: 'flex-end' } }, h('button.btn.g', { value: 'no', formnovalidate: true }, t('cancelar')), h('button.btn', { value: 'si' }, t('ok')))));
    document.body.append(d);
    d.addEventListener('close', () => { ok(d.returnValue === 'si' ? inp.value.trim().toUpperCase().slice(0, max) : null); d.remove(); });
    d.showModal(); inp.select();
  });
}
export function bajarArchivo(nombre, datos, tipo = 'application/octet-stream') {
  const u = URL.createObjectURL(datos instanceof Blob ? datos : new Blob([datos], { type: tipo }));
  const a = h('a', { href: u, download: nombre }); document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(u), 4000);
}
export const KCOL = i => `var(--k${i})`;
export const kb = n => n < 1024 ? n + ' B' : (n / 1024).toFixed(n < 10240 ? 1 : 0) + ' KB';
