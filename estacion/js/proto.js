// El protocolo SysEx del FNTSMG-1 (PROTOCOLO.md v1): números, empaque 7 en 8, CRC y un cliente que pregunta de uno
// en uno, repite con el mismo id si no hay respuesta y reparte los avisos. No sabe de qué transporte viene cada byte.

export const CAB = [0xF0, 0x7D, 0x46, 0x47];
export const CMD = { GOLPE: 0x01, ESTADO: 0x02, INFO: 0x10, ESCUCHA: 0x11, CAMBIO: 0x12, META: 0x13, ABRIR: 0x20, LEER: 0x21,
  ESCRIBIR: 0x22, CERRAR: 0x23, BORRAR: 0x24, LISTA: 0x25, PERILLA: 0x30, PERILLAS: 0x31, PASO: 0x32, PISTA: 0x33,
  TRANSPORTE: 0x34, NOMBRE: 0x35, AJUSTE: 0x40 };
export const EST = ['BIEN', 'NO_ENTIENDO', 'FUERA', 'OCUPADO', 'TOCANDO', 'NO_CABE', 'CRC', 'SIN_TRASLADO', 'FALLO_FLASH',
  'SIN_FLASH', 'FORMATO', 'VACIO'];
export const OBJ = { PROYECTO: 0, BANCO: 1, RANURA: 2, AUTO: 3, SAMPLE: 4 };
export const PAG = { EDIT: 0, ENV: 1, FX: 2, SCL: 3, LFO: 4, GLO: 5, CADENA: 6, ARP: 7 };

/* ---- empaque 7 en 8 (ed_unpack7 de Felucca) ---- */
export function pack7(b) {
  const o = [];
  for (let i = 0; i < b.length; i += 7) {
    const n = Math.min(7, b.length - i);
    let hi = 0;
    for (let j = 0; j < n; j++) hi |= ((b[i + j] >> 7) & 1) << j;
    o.push(hi);
    for (let j = 0; j < n; j++) o.push(b[i + j] & 0x7F);
  }
  return o;
}
export function unpack7(b, from = 0, to = b.length) {
  const o = [];
  for (let i = from; i < to;) {
    const hi = b[i++];
    for (let j = 0; j < 7 && i < to; j++) o.push(b[i++] | (((hi >> j) & 1) << 7));
  }
  return Uint8Array.from(o);
}
export const packedLen = n => n + Math.ceil(n / 7);

/* ---- CRC-32 de zlib ---- */
const CRCT = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
export function crc32(b, c = 0) { c = ~c >>> 0; for (let i = 0; i < b.length; i++) c = CRCT[(c ^ b[i]) & 255] ^ (c >>> 8); return ~c >>> 0; }

const te = new TextEncoder(), td = new TextDecoder();

/* ---- escribir y leer los campos de un mensaje ---- */
export class Esc {
  constructor() { this.b = []; }
  u7(v) { this.b.push(v & 0x7F); return this; }
  u14(v) { this.b.push(v & 0x7F, (v >> 7) & 0x7F); return this; }
  s14(v) { return this.u14(Math.max(-8192, Math.min(8191, v)) + 8192); }
  u21(v) { this.b.push(v & 0x7F, (v >> 7) & 0x7F, (v >> 14) & 0x7F); return this; }
  u32(v) { v >>>= 0; for (let i = 0; i < 5; i++) this.b.push(Math.floor(v / 2 ** (7 * i)) & 0x7F); return this; }
  bloque(bytes) { this.b.push(...pack7(bytes)); return this; }
  texto(s, cero = true) { const u = te.encode(s); return this.bloque(cero ? [...u, 0] : u); }
  textos(l) { const u = []; for (const s of l) u.push(...te.encode(s), 0); return this.bloque(u); }
}
export class Lec {
  constructor(b, i = 0) { this.b = b; this.i = i; }
  get queda() { return this.b.length - this.i; }
  u7() { return this.b[this.i++] ?? 0; }
  u14() { return this.u7() | this.u7() << 7; }
  s14() { return this.u14() - 8192; }
  u21() { return this.u7() | this.u7() << 7 | this.u7() << 14; }
  u32() { let v = 0; for (let i = 0; i < 5; i++) v += this.u7() * 2 ** (7 * i); return v >>> 0; }
  /* lo que queda del mensaje, desempacado */
  resto() { const o = unpack7(this.b, this.i); this.i = this.b.length; return o; }
  /* un nombre con su largo antes (u7) y luego empacado: ocupa largo + ⌈largo / 7⌉ bytes */
  nombre() { const n = this.u7(), m = packedLen(n), o = unpack7(this.b, this.i, this.i + m); this.i += m; return td.decode(o.subarray(0, n)); }
  /* una lista de textos terminados en 0 dentro de un solo bloque */
  textos() { return td.decode(this.resto()).split('\0').filter((s, i, a) => i < a.length - 1 || s); }
}
export const textoPlano = b => td.decode(b);

export function mensaje(cmd, id, datos = []) { return Uint8Array.from([...CAB, cmd, id & 0x7F, ...datos, 0xF7]); }
export function esNuestro(d) { return d.length >= 6 && d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x47; }

/* lo que trae META según su tipo, ya desempacado (el demo arma lo mismo con armarMeta) */
export function leerMeta(tipo, b) {
  let i = 0;
  const z = () => { const j = b.indexOf(0, i), k = j < 0 ? b.length : j, s = td.decode(b.subarray(i, k)); i = k + 1; return s; };
  const s16 = () => { const v = (b[i] | b[i + 1] << 8) << 16 >> 16; i += 2; return v; };
  if (tipo === 0) { const r = []; while (i < b.length) r.push(z()); return r; }
  if (tipo === 2) { const r = []; for (let k = 0; k < 4; k++) r.push({ nombre: z(), min: s16(), max: s16() }); return r; }
  if (tipo === 3) return z();
  const n = b[i++], r = [];
  for (let k = 0; k < n; k++) r.push(tipo === 5 ? { lugar: b[i++], nombre: z() } : z());
  return r;
}
export function armarMeta(tipo, datos) {
  const o = [], txt = s => o.push(...te.encode(s), 0), s16 = v => o.push(v & 255, (v >> 8) & 255);
  if (tipo === 0) datos.forEach(txt);
  else if (tipo === 2) datos.forEach(d => { txt(d.nombre); s16(d.min); s16(d.max); });
  else if (tipo === 3) txt(datos);
  else { o.push(datos.length); datos.forEach(d => { if (tipo === 5) { o.push(d.lugar); txt(d.nombre); } else txt(d); }); }
  return o;
}

export class ErrorAparato extends Error {
  constructor(cmd, estado, lec) { super(EST[estado] || 'ESTADO ' + estado); this.cmd = cmd; this.estado = estado; this.codigo = EST[estado]; this.lec = lec; }
}

/* ---- el cliente ---- */
export class Cliente {
  constructor(transporte) {
    this.t = transporte;
    this.id = 0;
    this.cola = Promise.resolve();
    this.espera = null;
    this.avisos = new Set();
    this.contador = -1;
    transporte.onMensaje = d => this._llega(d);
  }
  onAviso(fn) { this.avisos.add(fn); return () => this.avisos.delete(fn); }
  _llega(d) {
    if (!esNuestro(d)) return;
    const cmd = d[4];
    if (cmd === CMD.GOLPE || cmd === CMD.ESTADO) { for (const f of this.avisos) f(cmd, d.subarray(5, d.length - 1)); return; }
    const id = d[5];
    if (id === 0 && cmd === CMD.CAMBIO) {
      const l = new Lec(d.subarray(0, d.length - 1), 7);   /* F0 7D 46 47 12 00 estado contador… (el aviso también trae estado) */
      const a = { contador: l.u14(), que: l.u14(), patron: l.u7(), pistas: l.u7() };
      const propio = a.contador === this.contador;
      this.contador = a.contador;
      for (const f of this.avisos) f(cmd, a, propio);
      return;
    }
    const e = this.espera;
    if (e && e.cmd === cmd && e.id === id) { this.espera = null; clearTimeout(e.timer); e.ok(new Lec(d.subarray(0, d.length - 1), 7), d[6]); }
  }
  /* una pregunta y su respuesta; en orden, de una en una */
  pedir(cmd, datos = [], { intentos = 3, ms = 1000 } = {}) {
    const p = this.cola.then(() => new Promise((ok, mal) => {
      this.id = this.id % 127 + 1;
      const id = this.id, msg = mensaje(cmd, id, datos);
      let n = 0;
      const mandar = () => {
        if (n++ >= intentos) { this.espera = null; mal(Object.assign(new Error('SIN_RESPUESTA'), { codigo: 'SIN_RESPUESTA', cmd })); return; }
        try { this.t.enviar(msg); } catch (err) { this.espera = null; mal(err); return; }
        this.espera.timer = setTimeout(mandar, ms);
      };
      this.espera = { cmd, id, ok: (l, est) => est ? mal(new ErrorAparato(cmd, est, l)) : ok(l) };
      mandar();
    }));
    this.cola = p.catch(() => {});
    return p;
  }
  /* respuestas que traen el contador de cambios: se apunta, así el CAMBIO propio no se relee */
  async cambio(cmd, datos) { const l = await this.pedir(cmd, datos); this.contador = l.u14(); return this.contador; }

  async info() {
    const l = await this.pedir(CMD.INFO);
    const i = { protocolo: l.u7(), formato: l.u7(), patrones: l.u7(), pistas: l.u7(), pasos: l.u7(), ranuras: l.u7(), tamRanura: l.u21(),
      banco: l.u7(), trozo: l.u14(), zona: l.u21(), libre: l.u21(), idioma: l.u7(), contador: l.u14(), banderas: l.u7() };
    i.version = textoPlano(l.resto());
    i.seguro = !!(i.banderas & 1); i.tocando = !!(i.banderas & 2);
    this.contador = i.contador; this.trozo = i.trozo || 512;
    return i;
  }
  escucha(que) { return this.pedir(CMD.ESCUCHA, [que], { intentos: 1, ms: 1500 }).catch(() => {}); }
  /* META (rev. 2): la respuesta entera es un solo bloque empacado; textos terminados en 0, números en binario */
  async meta(tipo, ...args) {
    const e = new Esc().u7(tipo);
    args.forEach((a, i) => tipo === 3 && i === 4 ? e.s14(a) : e.u7(a));
    const b = (await this.pedir(CMD.META, e.b)).resto();
    return leerMeta(tipo, b);
  }
  async perilla(t, pag, sub, k, v) { const l = await this.pedir(CMD.PERILLA, new Esc().u7(t).u7(pag).u7(sub).u7(k).s14(v).b); const q = l.s14(); this.contador = l.u14(); return q; }
  async perillas(t, pag, sub) { const l = await this.pedir(CMD.PERILLAS, [t, pag, sub]); return [l.s14(), l.s14(), l.s14(), l.s14()]; }
  paso(p, t, s, registro) { return this.cambio(CMD.PASO, new Esc().u7(p).u7(t).u7(s).bloque(registro).b); }
  /* golpes y rotar del euclidiano y después la pista empacada */
  async leerPista(p, t) {
    const l = await this.pedir(CMD.PISTA, [p, t, 0]);
    const golpes = l.u7(), rotar = l.u7();
    return { golpes, rotar, bytes: l.resto() };
  }
  escribirPista(p, t, bytes) { return this.cambio(CMD.PISTA, new Esc().u7(p).u7(t).u7(1).bloque(bytes).b); }
  /* qué: 0 tocar, 1 patrón, 2 mute y 3 solo (máscaras de 8 en 2 bytes) */
  transporte(que, v) { return this.cambio(CMD.TRANSPORTE, que === 2 || que === 3 ? [que, v & 0x7F, (v >> 7) & 1] : [que, v]); }
  nombre(lugar, s) { return this.cambio(CMD.NOMBRE, new Esc().u7(lugar).texto(s.slice(0, 10), false).b); }
  borrar(obj, idx) { return this.cambio(CMD.BORRAR, [obj, idx]); }
  async ajuste(cual, v) { const e = new Esc().u7(cual).u7(v == null ? 0 : 1); if (v != null) e.s14(v); const l = await this.pedir(CMD.AJUSTE, e.b); return l.s14(); }
  async lista(obj) {
    const l = await this.pedir(CMD.LISTA, [obj]);
    if (obj === OBJ.RANURA) { const n = l.u7(), r = []; for (let i = 0; i < n; i++) r.push({ largo: l.u21(), crc: l.u32() }); return r; }
    if (obj === OBJ.SAMPLE) {
      const libre = l.u21(), n = l.u7(), s = [];
      for (let i = 0; i < n; i++) s.push({ indice: l.u7(), muestras: l.u21(), bytes: l.u21(), crc: l.u32(), tasa: l.u21(), raiz: l.u7(), lazo: l.u7(), lazoIni: l.u21(), lazoFin: l.u21(), nombre: l.nombre() });
      return { libre, samples: s };
    }
    const n = l.u7(); return l.textos().slice(0, n);
  }
  /* traslados: leer un objeto entero, o escribirlo, con progreso 0..1 */
  leerObjeto(obj, idx = 0, prog) { return this._traslado(() => this._leer(obj, idx, prog)); }
  escribirObjeto(obj, idx, bytes, op = {}, prog) { return this._traslado(() => this._escribir(obj, idx, bytes, op, prog)); }
  /* un traslado a la vez (regla 4): sin esto, releer el proyecto mientras se lista un banco abre dos y el aparato contesta OCUPADO */
  _traslado(fn) { const p = (this.tras || Promise.resolve()).then(fn); this.tras = p.catch(() => {}); return p; }
  /* ABRIR con paciencia: OCUPADO también es el aparato guardando por su lado (autoguardado, SAVE) */
  async _abrir(datos) {
    for (let n = 0; ; n++) {
      try { return await this.pedir(CMD.ABRIR, datos); }
      catch (err) { if (err.codigo !== 'OCUPADO' || n >= 4) throw err; await new Promise(r => setTimeout(r, 600)); }
    }
  }
  async _leer(obj, idx, prog) {
    const l = await this._abrir([obj, idx, 0]);
    const largo = l.u21(), crc = l.u32(), out = new Uint8Array(largo);
    try {
      for (let d = 0; d < largo;) {
        const n = Math.min(this.trozo || 512, largo - d);
        const r = await this.pedir(CMD.LEER, new Esc().u21(d).u14(n).b);
        const desde = r.u21(), tr = r.resto();
        out.set(tr.subarray(0, Math.min(tr.length, largo - desde)), desde);
        d = desde + tr.length;
        prog?.(d / largo);
      }
    } finally { await this.pedir(CMD.CERRAR, [0]).catch(() => {}); }
    if (crc32(out) !== crc) throw Object.assign(new Error('CRC'), { codigo: 'CRC' });
    return out;
  }
  async _escribir(obj, idx, bytes, { nombre, extra } = {}, prog) {
    const e = new Esc().u7(obj).u7(idx).u7(1).u21(bytes.length).u32(crc32(bytes));
    if (extra) e.b.push(...extra);
    if (nombre != null) e.texto(nombre);            /* lo empacado va al final: el nombre después del extra */
    await this._abrir(e.b);
    let cerrado = false;
    try {
      for (let d = 0; d < bytes.length;) {
        const n = Math.min(this.trozo || 512, bytes.length - d);
        try {
          const r = await this.pedir(CMD.ESCRIBIR, new Esc().u21(d).bloque(bytes.subarray(d, d + n)).b);
          d = r.u21();
        } catch (err) {
          if (err.codigo === 'FUERA') { d = err.lec.u21(); continue; }
          throw err;
        }
        prog?.(d / bytes.length);
      }
      const r = await this.pedir(CMD.CERRAR, [1], { ms: 4000 });
      cerrado = true;
      this.contador = r.u14();
    } finally { if (!cerrado) await this.pedir(CMD.CERRAR, [0]).catch(() => {}); }
  }
}
