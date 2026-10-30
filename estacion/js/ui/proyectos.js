// Proyectos: las 16 ranuras del aparato y el autoguardado; abrir una, guardar lo que suena en otra, bajarla o subirla
// como archivo. El respaldo completo (banco, ranuras y samples) se guarda en esta computadora y se puede bajar como
// archivo .fntsmg; devolverlo escribe todo de regreso (por ejemplo, después de reinstalar el firmware).
import { h, icono, aviso, explicar, preguntar, bajarArchivo, kb } from './util.js';
import { t, fecha } from '../i18n.js';
import { E, on, conProtocolo, asegurarParado, releer, OBJ } from '../estado.js';
import { versionDe } from '../proyecto.js';
import { Esc } from '../proto.js';
import * as alm from '../almacen.js';

let raiz, ranuras = null, err = null, respaldos = [], tarea = null;

export function montar(el) {
  raiz = el;
  const offs = [on('ranuras', leer), on('conexion', () => { ranuras = null; leer(); })];
  leer(); leerRespaldos();
  return () => offs.forEach(f => f());
}
async function leer() {
  if (!conProtocolo()) { ranuras = null; dibujar(); return; }
  try { ranuras = await E.cliente.lista(OBJ.RANURA); err = null; } catch (e) { err = e; }
  dibujar();
}
async function leerRespaldos() {
  const k = await alm.claves('respaldo:');
  respaldos = (await Promise.all(k.map(async c => ({ clave: c, ...(await alm.leer(c)) })))).filter(r => r.ranuras).sort((a, b) => b.fecha - a.fecha);
  dibujar();
}

/* un trabajo largo con su barra; uno a la vez */
async function trabajo(texto, fn) {
  if (tarea) return;
  tarea = { texto, p: 0 }; dibujar();
  try { await fn(p => { tarea.p = p; const i = raiz.querySelector('.tarea i'); if (i) i.style.width = (p * 100) + '%'; }); return true; }
  catch (e) { aviso(explicar(e), { mal: true }); return false; }
  finally { tarea = null; dibujar(); }
}

const b64 = u => { let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode(...u.subarray(i, i + 0x8000)); return btoa(s); };
const deB64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));

async function abrirRanura(i) {
  if (!await preguntar(t('pro.abrirTitulo', { n: i + 1 }), t('pro.abrirTexto'), t('pro.abrir'))) return;
  await trabajo(t('pro.abriendo', { n: i + 1 }), async prog => {
    const b = await E.cliente.leerObjeto(OBJ.RANURA, i, p => prog(p / 2));
    await E.cliente.escribirObjeto(OBJ.PROYECTO, 0, b, {}, p => prog(0.5 + p / 2));
    await releer(true);
  });
}
async function guardarEn(i) {
  if (ranuras?.[i]?.largo && !await preguntar(t('pro.pisarTitulo', { n: i + 1 }), t('pro.pisarTexto'), t('pro.guardar'))) return;
  await trabajo(t('pro.guardando', { n: i + 1 }), async prog => {
    await asegurarParado();
    const b = await E.cliente.leerObjeto(OBJ.PROYECTO, 0, p => prog(p / 2));
    await E.cliente.escribirObjeto(OBJ.RANURA, i, b, {}, p => prog(0.5 + p / 2));
  });
  leer();
}
async function bajar(i) {
  await trabajo(t('pro.bajando'), async prog => {
    const b = await E.cliente.leerObjeto(i === 16 ? OBJ.AUTO : OBJ.RANURA, i === 16 ? 0 : i, prog);
    bajarArchivo(`fntsmg-${i === 16 ? 'autoguardado' : 'ranura-' + String(i + 1).padStart(2, '0')}.fgp`, b);
  });
}
function subir(i) {
  const inp = h('input', { type: 'file', accept: '.fgp,application/octet-stream' });
  inp.onchange = async () => {
    const f = inp.files[0]; if (!f) return;
    const b = new Uint8Array(await f.arrayBuffer());
    if (!versionDe(b)) { aviso(t('pro.noEsProyecto'), { mal: true }); return; }
    if (ranuras?.[i]?.largo && !await preguntar(t('pro.pisarTitulo', { n: i + 1 }), t('pro.pisarTexto'), t('pro.guardar'))) return;
    await trabajo(t('pro.subiendo', { n: i + 1 }), async prog => { await asegurarParado(); await E.cliente.escribirObjeto(OBJ.RANURA, i, b, {}, prog); });
    leer();
  };
  inp.click();
}
async function borrarRanura(i) {
  if (!await preguntar(t('pro.borrarTitulo', { n: i + 1 }), t('pro.borrarTexto'), t('borrar'))) return;
  try { await asegurarParado(); await E.cliente.borrar(OBJ.RANURA, i); leer(); } catch (e) { aviso(explicar(e), { mal: true }); }
}

/* ---- respaldo completo ---- */
async function respaldar() {
  let r = null;
  const ok = await trabajo(t('pro.respaldando'), async prog => {
    const c = E.cliente, lr = await c.lista(OBJ.RANURA), ls = await c.lista(OBJ.SAMPLE);
    const total = 1 + lr.filter(x => x.largo).length + ls.samples.length;
    let hecho = 0; const paso = () => prog(++hecho / total);
    r = { formato: 'fntsmg-respaldo', v: 1, fecha: Date.now(), version: E.info?.version || '', banco: null, ranuras: [], samples: [] };
    r.banco = b64(await c.leerObjeto(OBJ.BANCO, 0)); paso();
    for (let i = 0; i < 17; i++) {
      if (!lr[i]?.largo) { r.ranuras.push(null); continue; }
      r.ranuras.push(b64(await c.leerObjeto(i === 16 ? OBJ.AUTO : OBJ.RANURA, i === 16 ? 0 : i))); paso();
    }
    for (const s of ls.samples) { const d = await c.leerObjeto(OBJ.SAMPLE, s.indice); r.samples.push({ ...s, datos: b64(d) }); paso(); }
    await alm.escribir('respaldo:' + r.fecha, r);
  });
  if (!ok) return;
  aviso(t('pro.respaldado'), { accion: { texto: t('pro.bajarArchivo'), fn: () => bajarRespaldo(r) } });
  leerRespaldos();
}
function bajarRespaldo(r) {
  const d = new Date(r.fecha), z = n => String(n).padStart(2, '0');
  bajarArchivo(`fntsmg-respaldo-${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}-${z(d.getHours())}${z(d.getMinutes())}.fntsmg`, JSON.stringify(r), 'application/json');
}
function abrirArchivoRespaldo() {
  const inp = h('input', { type: 'file', accept: '.fntsmg,application/json' });
  inp.onchange = async () => {
    try {
      const r = JSON.parse(await inp.files[0].text());
      if (r.formato !== 'fntsmg-respaldo' || !Array.isArray(r.ranuras)) throw new Error();
      await alm.escribir('respaldo:' + r.fecha, r); leerRespaldos(); aviso(t('pro.respaldoAbierto'));
    } catch (e) { aviso(t('pro.noEsRespaldo'), { mal: true }); }
  };
  inp.click();
}
async function devolver(r) {
  const n = r.ranuras.slice(0, 16).filter(Boolean).length;
  if (!await preguntar(t('pro.devolverTitulo'), t('pro.devolverTexto', { n, s: r.samples.length }), t('pro.devolver'))) return;
  const ok = await trabajo(t('pro.devolviendo'), async prog => {
    const c = E.cliente, total = 1 + n + r.samples.length;
    let hecho = 0; const paso = () => prog(++hecho / total);
    await asegurarParado();
    if (r.banco) await c.escribirObjeto(OBJ.BANCO, 0, deB64(r.banco)); paso();
    for (let i = 0; i < 16; i++) if (r.ranuras[i]) { await c.escribirObjeto(OBJ.RANURA, i, deB64(r.ranuras[i])); paso(); }
    for (const s of r.samples) {
      const extra = new Esc().u21(s.tasa).u21(s.muestras).u7(s.raiz).u7(s.lazo).u21(s.lazoIni).u21(s.lazoFin).b;
      await c.escribirObjeto(OBJ.SAMPLE, s.indice, deB64(s.datos), { nombre: s.nombre, extra }); paso();
    }
  });
  if (ok) { aviso(t('pro.devuelto')); leer(); }
}
async function borrarRespaldo(r) {
  if (!await preguntar(t('pro.borrarRespaldo'), t('pro.borrarRespaldoTexto'), t('borrar'))) return;
  await alm.borrar(r.clave); leerRespaldos();
}

/* ---- pantalla ---- */
function dibujar() {
  if (!raiz) return;
  raiz.replaceChildren();
  raiz.append(h('div.top', {}, h('div', {}, h('div.eyebrow', {}, t('pro.eyebrow')), h('h1', {}, t('nav.proyectos')))));
  if (tarea) raiz.append(h('div.banda', {}, h('p', {}, tarea.texto), h('div.prog.tarea', { style: { flex: 1, minWidth: '160px' } }, h('i', { style: { width: (tarea.p * 100) + '%' } }))));
  const p = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('pro.ranuras')), h('p.muted.chico', {}, t('pro.ranurasAyuda')))));
  if (!conProtocolo()) p.append(h('div.vacio', {}, icono('usb'), t('pro.conecta')));
  else if (err) p.append(h('div.banda.mal', {}, h('p', {}, explicar(err)), h('button.btn.g.mini', { onclick: leer }, t('reintentar'))));
  else if (!ranuras) p.append(h('p.muted', {}, t('cargando')));
  else p.append(h('div.ranuras', {}, ...ranuras.slice(0, 17).map((r, i) => {
    const auto = i === 16, lleno = !!r.largo, ocup = !!tarea;
    return h('div.ranura' + (lleno ? '' : '.vacia'), {},
      h('div.fila', { style: { justifyContent: 'space-between' } }, h('span.num', {}, auto ? 'A' : i + 1), h('span.chico.muted', {}, lleno ? kb(r.largo) : t('pro.vacia'))),
      h('span.eyebrow', {}, auto ? t('pro.autoguardado') : lleno ? t('pro.proyecto') : ''),
      h('div.acc', {},
        lleno && !auto ? h('button.btn.g.mini', { disabled: ocup, onclick: () => abrirRanura(i) }, t('pro.abrir')) : null,
        !auto ? h('button.btn.g.mini', { disabled: ocup, onclick: () => guardarEn(i), title: t('pro.guardarAquiAyuda') }, t('pro.guardarAqui')) : null,
        lleno ? h('button.ico', { disabled: ocup, 'aria-label': t('pro.bajarArchivo'), onclick: () => bajar(i) }, icono('bajar')) : null,
        !auto ? h('button.ico', { disabled: ocup, 'aria-label': t('pro.subirArchivo'), onclick: () => subir(i) }, icono('subir')) : null,
        lleno && !auto ? h('button.ico', { disabled: ocup, 'aria-label': t('borrar'), onclick: () => borrarRanura(i) }, icono('basura')) : null));
  })));
  const rp = h('section.panel', {}, h('header', {}, h('div', {}, h('h3', {}, t('pro.respaldos')), h('p.muted.chico', {}, t('pro.respaldosAyuda'))),
    h('div.fila', {}, h('button.btn.g', { onclick: abrirArchivoRespaldo }, icono('archivo'), t('pro.abrirRespaldo')),
      h('button.btn', { disabled: !conProtocolo() || !!tarea, onclick: respaldar }, icono('copia'), t('pro.respaldar')))));
  if (!respaldos.length) rp.append(h('div.vacio', {}, icono('copia'), h('b', {}, t('pro.sinRespaldos')), h('span.chico', {}, t('pro.sinRespaldosAyuda'))));
  else rp.append(h('div.lista', {}, ...respaldos.map(r => h('div', {},
    h('span.nom', {}, fecha(r.fecha)), h('span.muted.chico', {}, t('pro.resumen', { r: r.ranuras.slice(0, 16).filter(Boolean).length, s: r.samples.length, v: r.version || '' })),
    h('button.btn.g.mini', { disabled: !conProtocolo() || !!tarea, onclick: () => devolver(r) }, t('pro.devolver')),
    h('button.ico', { 'aria-label': t('pro.bajarArchivo'), onclick: () => bajarRespaldo(r) }, icono('bajar')),
    h('button.ico', { 'aria-label': t('borrar'), onclick: () => borrarRespaldo(r) }, icono('basura'))))));
  if (E.info?.seguro) raiz.append(h('div.banda.mal', {}, h('p', {}, t('pro.seguro'))));
  raiz.append(p, rp);
}
