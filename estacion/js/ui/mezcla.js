// Mezcla y GLO: una tira por pista (ENV, las dos páginas, con mute, solo y nivel), los efectos globales (FX), los
// ajustes globales (GLO: tempo, swing, raíz, escala, mutes, RELOJ, IDIOMA del aparato, NOTAS Y CC) y la CADENA.
import { h, perilla, KCOL } from './util.js';
import { t } from '../i18n.js';
import { E, on, perilla as mandar, mutes, idioma, hayProyecto, elegirPista } from '../estado.js';
import { pref } from '../almacen.js';
import { F } from '../proyecto.js';
import { meta, objetivo, resolver } from '../meta.js';
import { PAG } from '../proto.js';

let raiz, medidores = [], raf = 0, verMas = pref('mezMas') ?? false;

/* un fader (vertical) o un deslizador de paneo (horizontal, con el centro marcado): se arrastra, rueda o flechas */
function deslizador({ nombre, min, max, valor, fmt, alCambiar, vertical = false, centro = null }) {
  const lleno = h('i.lleno'), perno = h('i.perno'), vl = h('span.vl', {}, fmt(valor));
  const pista = h('div.pista', {}, ...(centro != null ? [h('i.centro')] : []), lleno, perno);
  const el = h('div.desl' + (vertical ? '.v' : '.h'), { role: 'slider', tabindex: 0, 'aria-label': nombre, 'aria-valuemin': min, 'aria-valuemax': max, 'aria-orientation': vertical ? 'vertical' : 'horizontal' },
    h('span.nm', {}, nombre), pista, vl);
  let v = valor;
  const pintar = () => {
    const f = (v - min) / (max - min), c = centro != null ? (centro - min) / (max - min) : 0;
    const a = Math.min(f, c) * 100, b = Math.abs(f - c) * 100;
    if (vertical) { lleno.style.bottom = a + '%'; lleno.style.height = b + '%'; perno.style.bottom = `calc(${f * 100}% - 8px)`; }
    else { lleno.style.left = a + '%'; lleno.style.width = b + '%'; perno.style.left = `calc(${f * 100}% - 8px)`; }
    vl.textContent = fmt(v); el.setAttribute('aria-valuenow', v); el.setAttribute('aria-valuetext', vl.textContent);
  };
  const poner = n => { n = Math.max(min, Math.min(max, Math.round(n))); if (n === v) return; v = n; pintar(); alCambiar(v); };
  const desde = e => { const r = pista.getBoundingClientRect(); return min + (max - min) * (vertical ? (r.bottom - e.clientY) / r.height : (e.clientX - r.left) / r.width); };
  pista.addEventListener('pointerdown', e => { pista.setPointerCapture(e.pointerId); el.classList.add('mueve'); poner(desde(e)); e.preventDefault(); el.focus({ preventScroll: true }); });
  pista.addEventListener('pointermove', e => { if (pista.hasPointerCapture(e.pointerId)) poner(desde(e)); });
  pista.addEventListener('pointerup', () => el.classList.remove('mueve'));
  el.addEventListener('dblclick', () => poner(centro ?? 100));
  el.addEventListener('keydown', e => { const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key]; if (d) { e.preventDefault(); poner(v + d * (e.shiftKey ? 8 : 1)); } });
  el.addEventListener('wheel', e => { e.preventDefault(); poner(v - Math.sign(e.deltaY)); }, { passive: false });
  pintar();
  return el;
}

export function montar(el) {
  raiz = el;
  const offs = [on('proyecto', dibujar), on('meta', dibujar), on('ajustes', dibujar), on('estado', c => c && pintarMutes()), on('bpm', dibujar), on('sel', dibujar)];
  dibujar();
  const medir = () => { raf = requestAnimationFrame(medir); medidores.forEach((m, tk) => { if (m) m.style.height = (E.niveles[tk] * 100 / 127) + '%'; }); };
  raf = requestAnimationFrame(medir);
  return () => { offs.forEach(f => f()); cancelAnimationFrame(raf); };
}
const enMano = () => raiz?.querySelector('.kn.mueve, .desl.mueve');

function kn(tk, pg, sub, k, color = 'var(--ink)') {
  const g = resolver(objetivo(tk, pg, sub, k, E.ajustes), E.pr);
  if (!g) return h('div.kn.muerta');
  return perilla({ nombre: g.nombre, min: g.min, max: g.max, valor: g.get(E.pr), estado: 'valor', color, fmt: g.fmt, alCambiar: v => mandar(tk, pg, sub, k, v) }).el;
}
function opciones(nombre, valores, actual, poner) {
  return h('div', { style: { display: 'grid', gap: '6px' } }, h('span.eyebrow', {}, nombre),
    h('div.seg', { role: 'group', 'aria-label': nombre }, ...valores.map((v, i) => h('button', { 'aria-pressed': i === actual, onclick: () => poner(i) }, v))));
}

function dibujar() {
  if (!raiz || enMano()) return;
  raiz.replaceChildren(); medidores = [];
  if (!hayProyecto()) return;
  const s = E.pr.song;
  /* una tira por pista, como un mezclador: el volumen en fader, el paneo de lado a lado, los envíos en perillas chicas */
  const tiras = h('div.tiras', {}, ...Array.from({ length: 8 }, (_, tk) => {
    const med = h('i'); medidores[tk] = med;
    const vol = resolver(objetivo(tk, PAG.ENV, 0, 0), E.pr), pan = resolver(objetivo(tk, PAG.ENV, 0, 1), E.pr);
    return h('div.tira' + (tk === E.sel ? '.sel' : ''), {},
      h('button.tira-n', { onclick: () => elegirPista(tk) }, meta.pistas[tk]),
      deslizador({ nombre: 'PANEO', min: 0, max: 127, centro: 64, valor: pan.get(E.pr), fmt: pan.fmt, alCambiar: v => mandar(tk, PAG.ENV, 0, 1, v) }),
      h('div.envios', {}, kn(tk, PAG.ENV, 0, 2, KCOL(10)), kn(tk, PAG.ENV, 0, 3, KCOL(11))),
      ...(verMas ? [h('div.envios', {}, ...[0, 1, 2, 3].map(k => kn(tk, PAG.ENV, 1, k)))] : []),
      h('div.canal', {}, h('div.medidor-v', { 'aria-hidden': 'true' }, med),
        deslizador({ nombre: 'VOLUMEN', vertical: true, min: 0, max: 127, valor: vol.get(E.pr), fmt: vol.fmt, alCambiar: v => mandar(tk, PAG.ENV, 0, 0, v) })),
      h('div.fila', { style: { justifyContent: 'center', gap: '4px' } },
        h('button.btn.g.mini.mute', { 'data-t': tk, 'aria-pressed': !!(E.mute >> tk & 1), title: t('mute'), onclick: () => mutes(E.mute ^ 1 << tk, E.solo) }, 'M'),
        h('button.btn.g.mini.solo', { 'data-t': tk, 'aria-pressed': !!(E.solo >> tk & 1), title: t('solo'), onclick: () => mutes(E.mute, E.solo ^ 1 << tk) }, 'S')));
  }));
  const fx = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('mez.fx')), h('p.muted.chico', {}, t('mez.fxAyuda')))),
    h('div.perillas', { style: { '--np': 4 } }, ...[0, 1, 2, 3].map(k => kn(0, PAG.FX, 0, k)), ...[0, 1, 2, 3].map(k => kn(0, PAG.FX, 1, k))));
  const glo = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('mez.glo')), h('p.muted.chico', {}, t('mez.gloAyuda')))),
    h('div.perillas', { style: { '--np': 4 } }, ...[0, 1, 2, 3].map(k => kn(0, PAG.GLO, 0, k))),
    opciones(t('mez.mutes'), [t('mez.alInstante'), t('mez.alCompas')], s[F.muteq], v => { mandar(0, PAG.GLO, 1, 0, v); dibujar(); }),
    opciones(t('mez.reloj'), [t('mez.relojNo'), t('mez.relojEntra'), t('mez.relojSale'), t('mez.relojAmbos')], s[F.reloj], v => { mandar(0, PAG.GLO, 1, 1, v); dibujar(); }),
    h('p.muted.chico', {}, t('mez.relojAyuda')),
    opciones(t('mez.notas'), [t('mez.relojNo'), t('mez.entran'), t('mez.salen'), t('mez.lasDos')], s[F.mnotes], v => { mandar(0, PAG.GLO, 1, 3, v); dibujar(); }),
    opciones(t('mez.idioma'), ['Español', 'English', 'Português'], E.ajustes.idioma, v => idioma(v)),
    h('p.muted.chico', {}, t('mez.idiomaAyuda')));
  const cad = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('mez.cadena')), h('p.muted.chico', {}, t('mez.cadenaAyuda'))),
    h('div.seg', {}, ...[0, 1].map(v => h('button', { 'aria-pressed': s[F.chain_on] === v, onclick: () => { mandar(0, PAG.CADENA, 0, 3, v); dibujar(); } }, t(v ? 'si' : 'no'))))),
    h('div.lista', {}, ...Array.from({ length: 16 }, (_, r) => {
      const p = s[F.crow + r], rep = s[F.crep + r];
      return h('div', {}, h('span.n', {}, r + 1),
        h('select.sel', { 'aria-label': t('patron'), onchange: e => { mandar(r, PAG.CADENA, 0, 0, +e.target.value); dibujar(); } },
          h('option', { value: 0, selected: p === 255 }, '–'), ...Array.from({ length: 16 }, (_, i) => h('option', { value: i + 1, selected: p === i }, t('patron') + ' ' + (i + 1)))),
        h('select.sel', { 'aria-label': t('mez.veces'), disabled: p === 255, onchange: e => mandar(r, PAG.CADENA, 0, 1, +e.target.value) },
          ...Array.from({ length: 16 }, (_, i) => h('option', { value: i + 1, selected: rep === i + 1 }, '×' + (i + 1)))));
    }).slice(0, Math.min(16, Math.max(4, [...Array(16).keys()].reverse().find(r => s[F.crow + r] !== 255) + 2 || 4)))));
  raiz.append(h('div.top', {}, h('div', {}, h('div.eyebrow', {}, 'ENV · FX · GLO'), h('h1', {}, t('mez.titulo')))),
    h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('mez.mezcla')), h('p.muted.chico', {}, t('mez.mezclaAyuda'))),
      h('button.btn.g.mini', { 'aria-expanded': verMas, onclick: () => { verMas = !verMas; pref('mezMas', verMas); dibujar(); } }, t(verMas ? 'mez.menos' : 'mez.mas'))), tiras),
    h('div.dos', {}, fx, glo), cad);
}
function pintarMutes() {
  raiz?.querySelectorAll('.mute').forEach(b => b.setAttribute('aria-pressed', !!(E.mute >> b.dataset.t & 1)));
  raiz?.querySelectorAll('.solo').forEach(b => b.setAttribute('aria-pressed', !!(E.solo >> b.dataset.t & 1)));
}
