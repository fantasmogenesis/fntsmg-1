// El visual del FM-1 en la Estación: el mismo espejo.wasm de espejo/ (fg_vis en C), alimentado con los golpes y el
// estado que llegan por SysEx. Dibuja un cuadro de 720×720 a 1 bit y lo copia a cada lienzo que lo pida (la barra,
// Grabar, el escenario y el lienzo de 1080 que se graba).
import { on, E } from '../estado.js';

export const PALETAS = { hueso: [[11, 11, 10], [242, 238, 228]], rojo: [[8, 4, 3], [214, 64, 46]], fosforo: [[3, 16, 9], [124, 255, 138]], negativo: [[242, 238, 228], [11, 11, 10]] };
const W = 720;
let ex = null, mem = null, img = null, fuente = null, fctx = null, pal = PALETAS.hueso, demoVivo = false;
const destinos = new Set();
const ui = { view: 0, bpm: 124, sel: 0, cur: 0, queued: 255, used: 1, mute: 0, solo: 0, pend: 0, has_pend: 0, perf: 0 };

export async function cargarEspejo() {
  fuente = document.createElement('canvas'); fuente.width = fuente.height = W;
  fctx = fuente.getContext('2d'); img = fctx.createImageData(W, W);
  try {
    const NOOP = new Proxy({}, { get: () => () => 0 });
    const { instance } = await WebAssembly.instantiate(await (await fetch('../espejo/espejo.wasm')).arrayBuffer(), new Proxy({}, { get: () => NOOP }));
    ex = instance.exports; mem = ex.memory;
    ex._initialize?.(); ex.e_init(12345);
  } catch (e) { console.warn('espejo sin wasm', e); ex = null; }
  on('golpe', d => { if (ex && d.length >= 11) golpe(d[0], d[1], d[2], d.subarray(3, 11)); });
  on('espejo', d => {
    if (!ex) return;
    Object.assign(ui, { view: 0, bpm: d[1] << 7 | d[2], sel: d[3], cur: d[4], queued: d[5] === 127 ? 255 : d[5], used: d[6] << 14 | d[7] << 7 | d[8],
      mute: d[9] << 7 | d[10], solo: d[11] << 7 | d[12], pend: d[13] << 7 | d[14], has_pend: d[15], perf: d[16] << 14 | d[17] << 7 | d[18] });
    for (let t = 0; t < 8; t++) ex.e_level(t, d[19 + t] << 8);
    for (let t = 0; t < 8; t++) for (let i = 0; i < 4; i++) ex.e_mix(t, i, d[27 + t * 4 + i]);
  });
  requestAnimationFrame(cuadro);
}
function golpe(t, m, vel, p) { new Uint8Array(mem.buffer, ex.e_params(), 8).set(p); ex.e_hit(t, m, vel); }
export function paleta(n) { if (PALETAS[n]) pal = PALETAS[n]; }

/* un lienzo que muestra el espejo; devuelve con qué quitarlo */
export function mostrarEn(canvas) { destinos.add(canvas); return () => destinos.delete(canvas); }
export const fuenteEspejo = () => fuente;

function cuadro(ts) {
  requestAnimationFrame(cuadro);
  dibujarCuadro(ts);
}
export const forzarCuadro = ts => dibujarCuadro(ts);
function dibujarCuadro(ts) {
  if (!destinos.size) return;
  if (ex) {
    ex.e_state(0, ui.bpm, ui.sel, ui.cur, ui.queued, ui.used, ui.mute, ui.solo, ui.pend, ui.has_pend, ui.perf);
    ex.e_frame(ts | 0);
    const o = new Uint8Array(mem.buffer, ex.e_out(), 90 * W), d = img.data, [b, f] = pal;
    for (let i = 0, p = 0; i < o.length; i++) {
      const v = o[i];
      for (let k = 7; k >= 0; k--, p += 4) { const c = (v >> k) & 1 ? f : b; d[p] = c[0]; d[p + 1] = c[1]; d[p + 2] = c[2]; d[p + 3] = 255; }
    }
    fctx.putImageData(img, 0, 0);
  } else { fctx.fillStyle = `rgb(${pal[0]})`; fctx.fillRect(0, 0, W, W); }
  for (const c of destinos) {
    if (!c.isConnected && !c.dataset.fuera) { destinos.delete(c); continue; }
    const x = c.getContext('2d'); x.imageSmoothingEnabled = false; x.drawImage(fuente, 0, 0, c.width, c.height);
  }
}
export { E };
