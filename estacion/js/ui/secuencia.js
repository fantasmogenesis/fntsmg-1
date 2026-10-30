// Secuencia: 8 pistas en rejilla, una página de 32 pasos (16 en el cel). Cada golpe enseña sus locks como barritas
// del color de su perilla; tocar un paso abre el inspector con las 8 perillas del motor y las 4 de mezcla, y arrastrar
// una fija el lock. Abajo, los carriles de la pista elegida: probabilidad y automatización.
import { h, icono, perilla, KCOL } from './util.js';
import { t } from '../i18n.js';
import { E, on, emit, paso as mandarPaso, perilla as mandarPerilla, tocar, patron, mutes, elegirPista, hayProyecto, hold, sostenido } from '../estado.js';
import { F, NS, NONE, pasoVacio, pasoTieneAlgo } from '../proyecto.js';
import { meta, perillasMotor, nombreLock, fmtLock, baseLock, objetivo, resolver, esSinte } from '../meta.js';
import { PAG } from '../proto.js';
import { pref } from '../almacen.js';

const cel = matchMedia('(max-width:820px)');
let raiz, rejilla, regla, carriles, insp, cabeza, celdas = [], filas = [], mitad = 0, auK = pref('auK') ?? 1;
let pintando = null, verMas = pref('verMas') ?? false;

const P = () => E.pr.pats[E.patron][E.sel];
const st = (tk, s) => E.pr.pats[E.patron][tk].st[s];
const rango = () => cel.matches ? [mitad * 16, mitad * 16 + 16] : [0, NS];

export function montar(el) {
  raiz = el;
  const off = [
    on('proyecto', dibujar), on('meta', dibujar), on('sel', dibujar), on('pista', dibujar),
    on('paso', (p, tk, s) => { if (p === E.patron) { pintarCelda(tk, s); if (tk === E.sel) pintarCarriles(); if (E.paso && E.paso.t === tk && E.paso.s === s) pintarInsp(); } }),
    on('estado', c => { if (c) pintarCabeza(); }), on('bpm', pintarCabeza), on('hold', pintarCabeza), on('sel', pintarCabeza),
    on('perilla', () => { if (!E.paso && !raiz?.querySelector('.kn.mueve')) pintarInsp(); }),
  ];
  cel.addEventListener('change', dibujar);
  let raf = 0;
  const cabezal = () => { raf = requestAnimationFrame(cabezal); pintarCabezal(); };
  raf = requestAnimationFrame(cabezal);
  addEventListener('keydown', teclas);
  dibujar();
  return () => { off.forEach(f => f()); cancelAnimationFrame(raf); cel.removeEventListener('change', dibujar); removeEventListener('keydown', teclas); };
}

function repintarPista(tk) { const [a, b] = rango(); for (let s = a; s < b; s++) pintarCelda(tk, s); pintarCarriles(); pintarCabeza(); }
let diferido = 0;
function dibujar() {
  if (!raiz) return;
  /* no rehacer la pantalla con una perilla en la mano: se pierde el arrastre */
  if (raiz.querySelector('.kn.mueve')) { clearTimeout(diferido); diferido = setTimeout(dibujar, 300); return; }
  raiz.replaceChildren();
  if (!hayProyecto()) return;
  const [a, b] = rango(), n = b - a;
  cabeza = h('div.top');
  raiz.append(cabeza);
  pintarCabeza();
  regla = h('div.regla', { style: { '--n': n } }, h('span'), ...Array.from({ length: n }, (_, i) => h('span' + ((a + i) % 4 === 0 ? '.q' : ''), {}, (a + i) % 4 === 0 ? a + i + 1 : '')));
  rejilla = h('div.rejilla', { style: { '--n': n }, role: 'grid', 'aria-label': t('sec.titulo') });
  celdas = []; filas = [];
  for (let tk = 0; tk < 8; tk++) {
    const m = E.pr.song[F.model + tk];
    const fila = h('div.fila-p' + (tk === E.sel ? '.sel' : ''), { role: 'row' },
      h('button.tn', { onclick: () => { elegirPista(tk); }, 'aria-pressed': tk === E.sel }, h('b', {}, meta.pistas[tk]), h('small', {}, meta.modelos[tk][m] || '')));
    celdas[tk] = [];
    for (let s = a; s < b; s++) {
      const c = h('button.c' + (s % 4 === 0 ? '.q' : ''), { role: 'gridcell', 'data-t': tk, 'data-s': s, tabindex: -1 });
      celdas[tk][s] = c; fila.append(c);
    }
    filas[tk] = fila; rejilla.append(fila);
  }
  rejilla.addEventListener('pointerdown', abajo);
  rejilla.addEventListener('pointermove', moverPintando);
  rejilla.addEventListener('pointerup', () => pintando = null);
  rejilla.addEventListener('pointercancel', () => pintando = null);
  rejilla.addEventListener('contextmenu', e => { const c = e.target.closest('.c'); if (c) { e.preventDefault(); acento(+c.dataset.t, +c.dataset.s); } });
  carriles = h('div.carriles');
  insp = h('section.insp', { 'aria-label': t('sec.inspector') });
  const mitades = cel.matches ? h('div.seg', { role: 'group' }, ...[0, 1].map(i => h('button', { 'aria-pressed': mitad === i, onclick: () => { mitad = i; dibujar(); } }, i ? '17–32' : '1–16'))) : null;
  /* lo de menos uso, plegado: los carriles y los ajustes de la pista */
  const extra = h('button.btn.g.mini', { 'aria-expanded': verMas, onclick: () => { verMas = !verMas; pref('verMas', verMas); dibujar(); } }, icono(verMas ? 'subir' : 'bajar'), t(verMas ? 'sec.menos' : 'sec.mas'));
  carriles.hidden = !verMas;
  raiz.append(...(mitades ? [h('div.fila', {}, h('span.eyebrow', {}, t('sec.pasos')), mitades)] : []), regla, rejilla, h('div.fila', {}, extra, h('span.muted.chico', {}, t('sec.tocaUnPaso'))), carriles, insp);
  for (let tk = 0; tk < 8; tk++) for (let s = a; s < b; s++) pintarCelda(tk, s);
  pintarFilas(); pintarCarriles(); pintarInsp();
}

function pintarCabeza() {
  if (!cabeza || !E.pr) return;
  const usados = E.pr.pats.map(pt => pt.some(tr => tr.st.some(x => x.v)));
  cabeza.replaceChildren(
    h('div', {}, h('div.eyebrow', {}, t('sec.eyebrow', { p: E.patron + 1, n: P().len })), h('h1', {}, t('sec.titulo'))),
    h('div.fila', {},
      h('div.pats', { role: 'group', 'aria-label': t('patrones') }, ...Array.from({ length: 16 }, (_, i) => h('button' + (usados[i] ? '.usado' : '') + (i === E.cur && E.tocando ? '.suena' : '') + (i === E.cola ? '.cola' : ''),
        { 'aria-pressed': i === E.patron, title: t('patron') + ' ' + (i + 1), onclick: () => { patron(i); dibujar(); } }, i + 1))),
      h('span.pill', {}, E.pr.song[F.bpm] + ' BPM'),
      botonHold(),
      h('button.btn' + (E.tocando ? '.g' : ''), { onclick: () => tocar(!E.tocando) }, icono(E.tocando ? 'parar' : 'tocar'), t(E.tocando ? 'parar' : 'tocar'))));
  pintarFilas();
}
/* HOLD de la pista (solo los sintes): el SOSTENER del ARP */
export function botonHold(tk = E.sel) {
  if (tk < 4) return null;
  const on = sostenido(tk);
  return h('button.btn.g.hold', { 'aria-pressed': on, title: t('hold.ayuda'), onclick: () => hold(tk, !on) }, t('hold'), ' · ', meta.pistas[tk]);
}
function pintarFilas() {
  filas.forEach((f, tk) => {
    const callada = E.solo ? !(E.solo >> tk & 1) : !!(E.mute >> tk & 1);
    f.classList.toggle('callada', callada);
    const [m, s] = f.querySelectorAll('.ms button');
    m?.setAttribute('aria-pressed', !!(E.mute >> tk & 1)); s?.setAttribute('aria-pressed', !!(E.solo >> tk & 1));
  });
}

function pintarCelda(tk, s) {
  const c = celdas[tk]?.[s]; if (!c) return;
  const tr = E.pr.pats[E.patron][tk], x = tr.st[s], fuera = s >= tr.len;
  c.className = 'c' + (s % 4 === 0 ? ' q' : '') + (fuera ? ' fuera' : '') + (x.v ? ' on' : '') + (x.v === 2 ? ' acc' : '')
    + (E.paso && E.paso.t === tk && E.paso.s === s ? ' elegido' : '');
  /* el paso es un punto redondo; la probabilidad lo aclara; debajo, un puntito por lock en el color de su perilla */
  c.replaceChildren(h('span.t', { style: x.v && x.p < 100 ? { opacity: 0.3 + 0.7 * x.p / 100 } : {} }));
  const lk = []; for (let i = 0; i < 12; i++) if (x.lk[i] !== NONE) lk.push(i);
  for (const [i] of x.au) if (!lk.includes(i)) lk.push(i);
  if (lk.length) c.append(h('span.pts', {}, ...lk.slice(0, 4).map(i => h('i', { style: { background: KCOL(i) } })), lk.length > 4 ? h('b', {}, '+') : null));
  if (x.r > 1) c.append(h('span.rt', {}, '×' + x.r));
  if (x.v && x.c) c.append(h('span.cd', {}, meta.condiciones[x.c]));
  const nm = [`${meta.pistas[tk]} ${s + 1}`, x.v ? t(x.v === 2 ? 'acento' : 'golpe') : t('vacio'), lk.length ? t('sec.nlocks', { n: lk.length }) : ''].filter(Boolean).join(', ');
  c.setAttribute('aria-label', nm);
}
let ultimoPos = '';
function pintarCabezal() {
  if (!rejilla) return;
  const k = E.tocando ? Array.from(E.pos).join() : '';
  if (k === ultimoPos) return;
  ultimoPos = k;
  rejilla.querySelectorAll('.c.aqui').forEach(c => c.classList.remove('aqui'));
  if (E.tocando && E.cur === E.patron) for (let tk = 0; tk < 8; tk++) celdas[tk]?.[E.pos[tk]]?.classList.add('aqui');
}

/* ---- tocar la rejilla ---- */
function abajo(e) {
  const c = e.target.closest('.c'); if (!c || e.button > 0) return;
  const tk = +c.dataset.t, s = +c.dataset.s, tr = E.pr.pats[E.patron][tk];
  if (s >= tr.len) return;
  if (tk !== E.sel) elegirPista(tk);
  const x = tr.st[s];
  if (e.altKey) { acento(tk, s); return; }
  if (!x.v) {
    x.v = 1; elegir(tk, s); mandarPaso(E.patron, tk, s);
    pintando = { tk, modo: 1 };
    rejilla.setPointerCapture?.(e.pointerId);
  } else if (E.paso && E.paso.t === tk && E.paso.s === s) {
    tr.st[s] = pasoVacio(); E.paso = null; mandarPaso(E.patron, tk, s); pintarInsp();
    pintando = { tk, modo: 0 };
  } else elegir(tk, s);
}
function moverPintando(e) {
  if (!pintando) return;
  const c = document.elementFromPoint(e.clientX, e.clientY)?.closest?.('.c'); if (!c) return;
  const tk = +c.dataset.t, s = +c.dataset.s; if (tk !== pintando.tk) return;
  const tr = E.pr.pats[E.patron][tk]; if (s >= tr.len) return;
  const x = tr.st[s];
  if (pintando.modo && !x.v) { x.v = 1; mandarPaso(E.patron, tk, s); }
  else if (!pintando.modo && x.v) { tr.st[s] = pasoVacio(); mandarPaso(E.patron, tk, s); }
}
function acento(tk, s) { const x = st(tk, s); x.v = x.v === 2 ? 1 : 2; mandarPaso(E.patron, tk, s); }
function elegir(tk, s) {
  const antes = E.paso; E.paso = { t: tk, s };
  if (antes) pintarCelda(antes.t, antes.s);
  pintarCelda(tk, s); pintarInsp();
}
function teclas(e) {
  if (!rejilla?.isConnected || /INPUT|SELECT|TEXTAREA/.test(document.activeElement?.tagName)) return;
  if (e.key === ' ' && !e.target.closest?.('button')) { e.preventDefault(); tocar(!E.tocando); return; }
  if (!E.paso) return;
  const { t: tk, s } = E.paso, L = E.pr.pats[E.patron][tk].len;
  const mv = { ArrowLeft: [0, -1], ArrowRight: [0, 1], ArrowUp: [-1, 0], ArrowDown: [1, 0] }[e.key];
  if (mv && !e.target.closest?.('.kn')) { e.preventDefault(); const nt = (tk + mv[0] + 8) % 8; elegirPista(nt); E.paso = { t: nt, s: (s + mv[1] + L) % L }; dibujar(); return; }
  if (e.key === 'Escape') { E.paso = null; pintarCelda(tk, s); pintarInsp(); }
  if ((e.key === 'Delete' || e.key === 'Backspace') && !e.target.closest?.('.kn')) { E.pr.pats[E.patron][tk].st[s] = pasoVacio(); mandarPaso(E.patron, tk, s); }
  if (e.key === 'Enter' && !e.target.closest?.('button')) { const x = st(tk, s); x.v = x.v ? 0 : 1; mandarPaso(E.patron, tk, s); }
}

/* ---- carriles: probabilidad y automatización de la pista elegida ---- */
function pintarCarriles() {
  if (!carriles) return;
  const [a, b] = rango(), n = b - a, tk = E.sel, tr = E.pr.pats[E.patron][tk], m = E.pr.song[F.model + tk], nombres = perillasMotor(tk, m);
  const carril = (titulo, sub, valor, poner, quitar, color) => {
    const fila = h('div.carril', { style: { '--n': n } }, h('div.tn', {}, h('b', {}, titulo), sub));
    for (let s = a; s < b; s++) {
      const v = s < tr.len ? valor(s) : null;
      const bx = h('div.b' + (s >= tr.len ? '.fuera' : v == null ? '.vacio' : ''), { 'data-s': s }, h('i', { style: { height: v == null ? 0 : Math.max(4, v * 100) + '%', background: color } }));
      fila.append(bx);
    }
    let activo = false;
    const en = e => {
      const bx = document.elementFromPoint(e.clientX, e.clientY)?.closest?.('.b'); if (!bx || bx.parentNode !== fila || bx.classList.contains('fuera')) return;
      const r = bx.getBoundingClientRect(), v = Math.max(0, Math.min(1, (r.bottom - e.clientY) / r.height));
      poner(+bx.dataset.s, v);
    };
    fila.addEventListener('pointerdown', e => { activo = true; fila.setPointerCapture(e.pointerId); en(e); });
    fila.addEventListener('pointermove', e => { if (activo) en(e); });
    fila.addEventListener('pointerup', () => { activo = false; });
    fila.addEventListener('dblclick', e => { const bx = e.target.closest('.b'); if (bx && quitar) quitar(+bx.dataset.s); });
    return fila;
  };
  const probab = carril(t('probabilidad'), h('small', {}, meta.pistas[tk]),
    s => tr.st[s].v ? tr.st[s].p / 100 : null,
    (s, v) => { const x = tr.st[s]; if (!x.v) return; const p = Math.max(1, Math.round(v * 100)); if (p === x.p) return; x.p = p; mandarPaso(E.patron, tk, s); },
    s => { const x = tr.st[s]; if (x.v && x.p !== 100) { x.p = 100; mandarPaso(E.patron, tk, s); } }, 'var(--ink)');
  const sel = h('select', { 'aria-label': t('sec.perillaAuto'), onchange: e => { auK = +e.target.value; pref('auK', auK); pintarCarriles(); } },
    ...nombres.slice(0, 7).map((nm, i) => h('option', { value: i, selected: i === auK }, nm)));
  const auto = carril(t('automatizacion'), sel,
    s => { const a2 = tr.st[s].au.find(x => x[0] === auK); return a2 ? a2[1] / 127 : null; },
    (s, v) => {
      const x = tr.st[s], q = Math.round(v * 127), a2 = x.au.find(y => y[0] === auK);
      if (a2) { if (a2[1] === q) return; a2[1] = q; }
      else if (x.au.length < 2) x.au.push([auK, q]); else x.au[1] = [auK, q];
      mandarPaso(E.patron, tk, s);
    },
    s => { const x = tr.st[s], i = x.au.findIndex(y => y[0] === auK); if (i >= 0) { x.au.splice(i, 1); mandarPaso(E.patron, tk, s); } }, KCOL(auK));
  carriles.replaceChildren(probab, auto, h('p.muted.chico', {}, t('sec.ayudaCarriles')));
}

/* ---- el inspector: el paso elegido, o la pista si no hay paso ---- */
function pintarInsp() {
  if (!insp) return;
  insp.replaceChildren();
  insp.classList.toggle('fijo', !!E.paso);
  insp.hidden = false;
  if (!E.paso) return pintarPista();
  const { t: tk, s } = E.paso, x = st(tk, s), pr = E.pr, m = pr.song[F.model + tk];
  const knob = i => {
    const base = baseLock(pr, tk, i), lk = x.lk[i] !== NONE;
    const k = perilla({ nombre: nombreLock(pr, tk, i), valor: lk ? x.lk[i] : base, base, estado: lk ? 'lock' : 'base', color: KCOL(i), fmt: v => fmtLock(pr, tk, i, v),
      alCambiar: v => { x.lk[i] = v; if (!x.v) x.v = 1; mandarPaso(E.patron, tk, s); },
      alQuitar: () => { x.lk[i] = NONE; k.poner(base, 'base'); mandarPaso(E.patron, tk, s); } });
    return k.el;
  };
  const disp = h('div.disp', {},
    h('div.seg', { role: 'group' }, ...[[0, 'vacio'], [1, 'golpe'], [2, 'acento']].map(([v, k]) => h('button', { 'aria-pressed': x.v === v, onclick: () => { x.v = v; mandarPaso(E.patron, tk, s); } }, t(k)))),
    h('label', {}, t('condicion'), h('select.sel', { onchange: e => { x.c = +e.target.value; mandarPaso(E.patron, tk, s); } }, ...meta.condiciones.map((c, i) => h('option', { value: i, selected: i === x.c }, c))), h('b')),
    rango2(t('probabilidad'), 1, 100, x.p, v => v + '%', v => { x.p = v; mandarPaso(E.patron, tk, s); }),
    h('label', {}, t('ratchet'), h('div.seg', {}, ...[1, 2, 3, 4].map(r => h('button', { 'aria-pressed': x.r === r, onclick: () => { x.r = r; mandarPaso(E.patron, tk, s); } }, '×' + r))), h('b')),
    rango2(t('micro'), -8, 7, x.u, v => (v > 0 ? '+' : '') + v + '/16', v => { x.u = v; mandarPaso(E.patron, tk, s); }),
    h('div.fila', {},
      h('button.btn.g.mini', { disabled: !x.lk.some(v => v !== NONE) && !x.au.length, onclick: () => { x.lk.fill(NONE); x.au = []; mandarPaso(E.patron, tk, s); } }, t('sec.quitarLocks')),
      h('button.btn.g.mini', { disabled: !pasoTieneAlgo(x), onclick: () => { E.pr.pats[E.patron][tk].st[s] = pasoVacio(); mandarPaso(E.patron, tk, s); } }, icono('basura'), t('borrar'))));
  insp.append(
    h('div', {}, h('div.eyebrow', {}, `${meta.pistas[tk]} · ${meta.modelos[tk][m] || ''}`), h('h2', {}, t('sec.paso', { n: s + 1 })), h('p.muted.chico', {}, t('sec.ayudaLock'))),
    h('div', { style: { display: 'grid', gap: '10px', minWidth: 0 } },
      h('div.perillas', {}, ...Array.from({ length: 8 }, (_, i) => knob(i))),
      h('div.perillas', { style: { '--np': cel.matches ? 4 : 8 } }, ...[8, 9, 10, 11].map(knob))),
    disp,
    h('button.ico.cerrar', { 'aria-label': t('cerrar'), onclick: () => { E.paso = null; pintarCelda(tk, s); pintarInsp(); } }, icono('x')));
}
function rango2(nombre, mi, ma, v, fmt, poner) {
  const b = h('b', {}, fmt(v));
  const r = h('input', { type: 'range', min: mi, max: ma, value: v, oninput: e => { b.textContent = fmt(+e.target.value); poner(+e.target.value); } });
  return h('label', {}, nombre, r, b);
}
function pintarPista() {
  insp.hidden = !verMas;
  if (!verMas) return;
  const tk = E.sel, m = E.pr.song[F.model + tk];
  const knobs = [[PAG.SCL, 0, 0], [PAG.SCL, 0, 1], [PAG.SCL, 0, 2], [PAG.SCL, 0, 3], [PAG.SCL, 1, 1], [PAG.SCL, 1, 2], [PAG.SCL, 1, 3]].map(([pg, sub, k]) => {
    const g = resolver(objetivo(tk, pg, sub, k, E.ajustes), E.pr);
    return perilla({ nombre: g.nombre, min: g.min, max: g.max, valor: g.get(E.pr), estado: 'valor', color: 'var(--ink)', fmt: g.fmt,
      alCambiar: v => { mandarPerilla(tk, pg, sub, k, v); if (pg === PAG.SCL && !sub) repintarPista(tk); } }).el;
  });
  insp.append(
    h('div', {}, h('div.eyebrow', {}, t('sec.ajustesPista')), h('h2', {}, `${meta.pistas[tk]}`), h('p.muted.chico', {}, meta.modelos[tk][m] || '')),
    h('div.perillas', { style: { '--np': cel.matches ? 4 : 7 } }, ...knobs),
    h('p.vacio-insp', {}, t('sec.tocaUnPaso')));
}
export { emit };
