// Grabar: el visual a 1080×1080 con el audio USB del FM-1 («Felucca», estéreo 44.1 kHz) en un solo video. El audio se
// pide sin cancelación de eco, sin supresión de ruido y sin control de ganancia: así llega igual que sale del aparato.
// MP4 si el navegador lo graba, si no WebM. El modo escenario es el visual a pantalla completa con el botón de grabar.
import { h, icono, aviso, bajarArchivo, preguntar } from './util.js';
import { t } from '../i18n.js';
import { E, on, emit } from '../estado.js';
import { mostrarEn, paleta, PALETAS, forzarCuadro } from './espejo.js';
import { pref } from '../almacen.js';

const FORMATOS = ['video/mp4;codecs=avc1.42E01E,mp4a.40.2', 'video/mp4;codecs=avc1,mp4a', 'video/mp4', 'video/webm;codecs=vp9,opus', 'video/webm;codecs=vp8,opus', 'video/webm'];
const PISTA = /felucca|fntsmg|fm-?1|m-?vave/i;
let raiz, entradas = [], elegida = pref('audioId') ?? '', stream = null, analizador = null, medT = 0, audioMal = null, buscando = false;
const G = { rec: null, partes: [], t0: 0, lienzo: null, quitar: null, tomas: [], fondo: 0 };
export const grabando = () => !!G.rec;

export function formato() { if (!globalThis.MediaRecorder) return null; return FORMATOS.find(f => MediaRecorder.isTypeSupported(f)) || null; }

export function montar(el) {
  raiz = el;
  const offs = [on('grabando', dibujar)];
  dibujar();
  arrancarAudio();
  return () => { offs.forEach(f => f()); cancelAnimationFrame(medT); };
}

/* Chrome solo da los nombres de las entradas cuando ya hay permiso de micrófono: si ya lo hay, se busca «Felucca» sola */
async function arrancarAudio() {
  if (stream || elegida === 'nada' || !navigator.mediaDevices?.enumerateDevices) return;
  let ok = false;
  try { ok = (await navigator.permissions.query({ name: 'microphone' })).state === 'granted'; } catch (e) {}
  if (ok) buscarFm(false);
}
const RAZON = { NotAllowedError: 'rec.sinPermiso', SecurityError: 'rec.sinPermiso', NotReadableError: 'rec.ocupada', AbortError: 'rec.ocupada', NotFoundError: 'rec.noFm', OverconstrainedError: 'rec.noFm' };
/* pide permiso (con cualquier entrada), lista las entradas y abre la del FM-1 */
async function buscarFm(pedir = true) {
  if (!navigator.mediaDevices?.getUserMedia) { audioMal = 'rec.sinMedios'; dibujar(); return; }
  buscando = true; audioMal = null; dibujar();
  try {
    if (pedir && !entradas.some(d => d.label)) { const s0 = await navigator.mediaDevices.getUserMedia({ audio: true }); s0.getTracks().forEach(x => x.stop()); }
    entradas = (await navigator.mediaDevices.enumerateDevices()).filter(d => d.kind === 'audioinput');
    const fm = entradas.find(d => PISTA.test(d.label));
    const guardada = entradas.find(d => d.deviceId === elegida && elegida);
    if (fm && (!guardada || !PISTA.test(guardada.label))) elegida = fm.deviceId;
    if (!fm && !guardada) { audioMal = entradas.some(d => d.label) ? 'rec.noFm' : 'rec.sinPermiso'; elegida = ''; }
    else await abrirAudio();
  } catch (e) { audioMal = RAZON[e.name] || 'rec.noAudio'; }
  buscando = false; dibujar();
}
async function abrirAudio() {
  soltarAudio();
  if (!elegida || elegida === 'nada') { pref('audioId', elegida); dibujar(); return; }
  try {
    stream = await navigator.mediaDevices.getUserMedia({ audio: { deviceId: { exact: elegida }, echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2, sampleRate: 44100 } });
    pref('audioId', elegida); audioMal = null;
    const ac = new AudioContext(), src = ac.createMediaStreamSource(stream);
    analizador = ac.createAnalyser(); analizador.fftSize = 1024; src.connect(analizador);
    stream.getAudioTracks()[0].onended = () => { soltarAudio(); audioMal = 'rec.noFm'; dibujar(); };
  } catch (e) { audioMal = RAZON[e.name] || 'rec.noAudio'; }
  dibujar();
}
function soltarAudio() { stream?.getTracks().forEach(x => x.stop()); stream = null; analizador = null; }

export async function grabarONo() {
  if (!stream && elegida !== 'nada' && !await preguntar(t('rec.sinAudioTitulo'), t(audioMal || 'rec.audioAyuda'), t('rec.grabarMudo'))) return;
  empezar();
}
export function empezar() {
  const f = formato();
  if (!f) { aviso(t('rec.sinGrabador'), { mal: true }); return; }
  const c = document.createElement('canvas'); c.width = c.height = 1080; c.dataset.fuera = '1';
  G.lienzo = c; G.quitar = mostrarEn(c);
  const s = c.captureStream(30);
  if (stream) stream.getAudioTracks().forEach(x => s.addTrack(x));
  G.partes = [];
  G.rec = new MediaRecorder(s, { mimeType: f, videoBitsPerSecond: 8_000_000, audioBitsPerSecond: 192_000 });
  G.rec.ondataavailable = e => e.data.size && G.partes.push(e.data);
  G.rec.onstop = () => {
    const tipo = f.split(';')[0], blob = new Blob(G.partes, { type: tipo }), ext = tipo === 'video/mp4' ? 'mp4' : 'webm';
    const d = new Date(), z = n => String(n).padStart(2, '0');
    G.tomas.unshift({ blob, url: URL.createObjectURL(blob), nombre: `fntsmg-${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}-${z(d.getHours())}${z(d.getMinutes())}${z(d.getSeconds())}.${ext}`, dur: (performance.now() - G.t0) / 1000, audio: !!stream });
    clearInterval(G.fondo); G.quitar?.(); G.lienzo = null; G.rec = null; emit('grabando');
  };
  /* con la pestaña escondida el navegador frena requestAnimationFrame: el cuadro se sigue dibujando con un reloj */
  G.fondo = setInterval(() => { if (document.hidden) forzarCuadro(performance.now()); }, 33);
  G.rec.start(1000); G.t0 = performance.now(); emit('grabando');
}
export function detener() { G.rec?.stop(); }

const mmss = s => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;

function dibujar() {
  if (!raiz) return;
  raiz.replaceChildren();
  const f = formato();
  const lienzo = h('canvas', { width: 720, height: 720 }); mostrarEn(lienzo);
  const reloj = h('span.reloj', {}, G.rec ? mmss((performance.now() - G.t0) / 1000) : '0:00');
  const nivel = h('i', { style: { background: 'var(--ok)', width: '0%' } });
  const med = () => {
    medT = requestAnimationFrame(med);
    if (G.rec) reloj.textContent = mmss((performance.now() - G.t0) / 1000);
    if (analizador) { const a = new Float32Array(analizador.fftSize); analizador.getFloatTimeDomainData(a); let p = 0; for (const v of a) p = Math.max(p, Math.abs(v)); nivel.style.width = Math.min(100, p * 100) + '%'; nivel.style.background = p > 0.98 ? 'var(--rec)' : 'var(--ok)'; }
  };
  cancelAnimationFrame(medT); medT = requestAnimationFrame(med);
  const palAct = pref('paleta') || 'hueso';
  const hayNombres = entradas.some(d => d.label);
  const fuente = hayNombres ? h('select.sel', { 'aria-label': t('rec.audio'), onchange: e => { elegida = e.target.value; abrirAudio(); } },
    ...entradas.map(d => h('option', { value: d.deviceId, selected: d.deviceId === elegida }, (d.label || t('rec.entrada')) + (PISTA.test(d.label) ? ' · FM-1' : ''))),
    h('option', { value: 'nada', selected: elegida === 'nada' }, t('rec.sinAudio'))) : null;
  const hayFm = entradas.some(d => PISTA.test(d.label) && d.deviceId === elegida) && !!stream;
  const buscar = h('button.btn' + (stream ? '.g.mini' : ''), { disabled: buscando, onclick: () => buscarFm(true) }, icono('usb'), t(buscando ? 'rec.buscando' : stream ? 'rec.buscarOtraVez' : 'rec.buscarFm'));
  raiz.append(
    h('div.top', {}, h('div', {}, h('div.eyebrow', {}, t('rec.eyebrow')), h('h1', {}, t('nav.grabar'))),
      h('button.btn.g', { onclick: escenario }, icono('lleno'), t('rec.escenario'))),
    h('div.dos', {},
      h('div.escena', {}, lienzo),
      h('section.panel', {},
        h('div.fila', { style: { justifyContent: 'space-between' } }, reloj,
          G.rec ? h('button.btn.rec', { onclick: detener }, icono('parar'), t('rec.detener')) : h('button.btn.rec', { disabled: !f, onclick: grabarONo }, icono('grabar'), t('rec.grabar'))),
        h('div', { style: { display: 'grid', gap: '8px' } }, h('h3', {}, t('rec.audio')),
          hayFm ? h('div.fila', {}, h('span.pill', { 'data-e': 'conectado' }, h('i'), t('rec.felucca')), buscar) : buscar, fuente,
          stream ? h('div.medidor', { 'aria-hidden': 'true' }, nivel) : null,
          audioMal ? h('div.banda.mal', { role: 'alert' }, h('p', {}, t(audioMal))) : h('p.muted.chico', {}, stream ? (hayFm ? t('rec.audioFm') : t('rec.audioOtro')) : t('rec.audioAyuda'))),
        h('div', { style: { display: 'grid', gap: '6px' } }, h('h3', {}, t('rec.colores')),
          h('div.paletas', { role: 'group' }, ...Object.keys(PALETAS).map(p => h('button', { title: p, 'aria-label': p, 'aria-pressed': p === palAct, style: { background: `rgb(${PALETAS[p][1]})` }, onclick: () => { paleta(p); pref('paleta', p); dibujar(); } })))),
        h('p.muted.chico', {}, f ? t('rec.formato', { f: f.startsWith('video/mp4') ? 'MP4' : 'WebM' }) : t('rec.sinGrabador')),
        !E.tr ? h('p.muted.chico', {}, t('rec.sinAparato')) : null)),
    ...(G.tomas.length ? [h('section.panel', {}, h('h3', {}, t('rec.tomas')), h('div.lista', {}, ...G.tomas.map(x => h('div', {},
      h('video', { src: x.url, controls: true, muted: true, playsinline: true, style: { width: '96px', height: '96px', background: '#000', borderRadius: '6px' } }),
      h('span.nom', {}, x.nombre), h('span.muted.chico', {}, mmss(x.dur) + (x.audio ? '' : ' · ' + t('rec.mudo'))),
      h('button.btn.mini', { onclick: () => bajarArchivo(x.nombre, x.blob) }, icono('bajar'), t('rec.bajar'))))))] : []));
}

/* ---- escenario: el visual a pantalla completa ---- */
export function escenario() {
  const c = h('canvas', { width: 1080, height: 1080 }), quitar = mostrarEn(c);
  const btn = h('button.btn.rec', {}), reloj = h('span.reloj', { style: { color: '#F2EEE4' } });
  const pintar = () => { btn.replaceChildren(icono(G.rec ? 'parar' : 'grabar'), t(G.rec ? 'rec.detener' : 'rec.grabar')); el.classList.toggle('graba', !!G.rec); };
  const salir = () => { quitar(); clearInterval(tick); offG(); removeEventListener('keydown', tecla); if (document.fullscreenElement) document.exitFullscreen?.(); el.remove(); };
  const el = h('div.escenario', { role: 'dialog', 'aria-label': t('rec.escenario') }, c,
    h('div.barra', {}, reloj, btn, h('button.btn.g', { onclick: () => document.fullscreenElement ? document.exitFullscreen() : el.requestFullscreen?.() }, icono('lleno'), t('rec.pantallaCompleta')),
      h('button.btn.g', { onclick: salir }, icono('x'), t('cerrar'))));
  btn.onclick = () => G.rec ? detener() : grabarONo();
  const tecla = e => { if (e.key === 'Escape' && !document.fullscreenElement) salir(); if (e.key === 'r' || e.key === 'R') btn.click(); };
  const tick = setInterval(() => { reloj.textContent = G.rec ? mmss((performance.now() - G.t0) / 1000) : ''; }, 250);
  let mt; el.addEventListener('pointermove', () => { el.classList.add('mueve'); clearTimeout(mt); mt = setTimeout(() => el.classList.remove('mueve'), 1800); });
  const offG = on('grabando', pintar);
  addEventListener('keydown', tecla);
  document.body.append(el); pintar();
}
