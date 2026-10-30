// Un FNTSMG-1 inventado que vive en la página y contesta PROTOCOLO.md v1 byte por byte: así el modo sin aparato pasa
// por el mismo cliente, los mismos traslados y los mismos errores que el de verdad. Toca un ritmo (golpes y estado del
// espejo), guarda sus ranuras, banco y samples en esta computadora y se porta como el aparato en lo que importa:
// la flash solo se escribe parado, un traslado a la vez, la misma orden repetida no se aplica dos veces.
import { CMD, OBJ, PAG, Esc, Lec, unpack7, crc32, mensaje, armarMeta } from './proto.js';
import { proyectoVacio, pack, unpack, packPista, unpackPista, leerRegistro, F, off, NS } from './proyecto.js';
import { meta, objetivo, perillasMotor, presets, SAMPLE_M, esSinte, sampleEn, resolver } from './meta.js';
import * as alm from './almacen.js';

const RANURA = 8192 - 12, ZONA = 136 * 1024, SECTOR = 4096;
const r = () => Math.random();

export class AparatoDemo {
  constructor() {
    this.salida = null;
    this.pr = proyectoVacio();
    this.ranuras = Array(17).fill(null);          /* 16 proyectos + el autoguardado */
    this.banco = [];                               /* {pista, modelo, nombre, p[8]} */
    this.samples = Array(32).fill(null);           /* {nombre, datos, tasa, muestras, raiz, lazo, lazoIni, lazoFin} */
    this.ajustes = { idioma: 0 };
    this.cont = 1; this.tocando = false; this.escucha = 0; this.escuchaHasta = 0;
    this.ultima = null; this.tr = null;
    this.mute = 0; this.solo = 0; this.cola = 255; this.sel = 0;
    this.pos = new Int16Array(8).fill(-1); this.nivel = new Float32Array(8); this.loops = new Uint32Array(8);
    this.tick = 0; this.cambios = 0; this.cambioTimer = null;
  }
  async iniciar() {
    const e = await alm.leer('demo');
    if (e) {
      try { this.pr = unpack(e.pr); } catch (err) { this.pr = ejemplo(); }
      this.ranuras = e.ranuras || this.ranuras; this.banco = e.banco || []; this.samples = e.samples || this.samples; this.ajustes = e.ajustes || this.ajustes;
    } else {
      this.pr = ejemplo();
      this.ranuras[0] = pack(this.pr);
      const b = ejemplo(); b.song[F.bpm] = 96; b.song[F.model + 4] = 6; this.ranuras[1] = pack(b);
      this.banco = [{ pista: 0, modelo: 0, nombre: 'BOMBO SECO', p: [58, 70, 30, 10, 20, 90, 80, 0] }, { pista: 4, modelo: 1, nombre: 'ÁCIDO RUGE', p: [40, 30, 110, 90, 60, 100, 40, 0] },
        { pista: 5, modelo: 6, nombre: 'DUB LEJOS', p: [36, 64, 50, 70, 100, 60, 40, 0] }];
    }
    this._tuyos();
    this.timer = setInterval(() => this._reloj(), 5);
    this.t0 = performance.now(); this.tAcc = 0;
    this.estadoTimer = setInterval(() => this._estado(), 33);
  }
  detener() { clearInterval(this.timer); clearInterval(this.estadoTimer); }
  _guardar() { clearTimeout(this.gT); this.gT = setTimeout(() => alm.escribir('demo', { pr: pack(this.pr), ranuras: this.ranuras, banco: this.banco, samples: this.samples, ajustes: this.ajustes }), 600); }
  _sale(b) { this.salida?.(b); }
  _cambio(que, patron = 127, pistas = 0) {
    this.cont = (this.cont + 1) & 16383;
    this.cambios |= que; this._pt = patron; this._pp = (this._pp || 0) | pistas;
    this.ranuras[16] = pack(this.pr);
    this._guardar();
    if (this.cambioTimer) return;
    this.cambioTimer = setTimeout(() => {
      this.cambioTimer = null;
      if (this.escucha & 2 && performance.now() < this.escuchaHasta) this._sale(mensaje(CMD.CAMBIO, 0, [0, ...new Esc().u14(this.cont).u14(this.cambios).u7(this._pt).u7(this._pp).b]));
      this.cambios = 0; this._pp = 0;
    }, 100);
  }
  _libre() { return ZONA - this.samples.reduce((a, s) => a + (s ? Math.ceil(s.datos.length / SECTOR) * SECTOR : 0), 0); }

  /* ---------------- el protocolo ---------------- */
  recibir(d) {
    if (d[0] === 0xB0 + 15 && d[1] === 119) { if (d[2]) { this.escucha |= 1; this.escuchaHasta = performance.now() + 3000; } return; }
    if (d.length < 7 || d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x46 || d[3] !== 0x47) return;
    const cmd = d[4], id = d[5], l = new Lec(d.subarray(0, d.length - 1), 6);
    if (this.ultima && this.ultima.cmd === cmd && this.ultima.id === id && cmd !== CMD.ESCUCHA && cmd !== CMD.INFO) { this._sale(this.ultima.r); return; }
    let est = 0, e = new Esc();
    try { [est, e] = this._orden(cmd, l, e) || [0, e]; }
    catch (err) {
      e = new Esc();
      if (typeof err === 'number') { est = err; if (est === 5) e.u21(this._libre()); }
      else if (err?.datos) { est = +err; e = err.datos; }
      else if (err?.codigo === 'FORMATO') est = 10;
      else { console.error(err); est = 1; }
    }
    const r = mensaje(cmd, id, [est, ...e.b]);
    this.ultima = { cmd, id, r };
    this._sale(r);
  }
  _orden(cmd, l, e) {
    const pr = this.pr;
    switch (cmd) {
      case CMD.INFO:
        e.u7(1).u7(7).u7(16).u7(8).u7(32).u7(16).u21(RANURA).u7(96).u14(512).u21(ZONA).u21(this._libre()).u7(this.ajustes.idioma).u14(this.cont).u7(this.tocando ? 2 : 0)
          .bloque(new TextEncoder().encode('FNTSMG-1 0.9 DEMO'));
        return [0, e];
      case CMD.ESCUCHA: this.escucha = l.u7(); this.escuchaHasta = performance.now() + 3000; return [0, e];
      case CMD.META: return [0, this._meta(l, e)];
      case CMD.PERILLA: {
        const t = l.u7(), pag = l.u7(), sub = l.u7(), k = l.u7(), v = l.s14();
        const g = objetivo(pag === PAG.CADENA ? t : t & 7, pag, sub, k, this.ajustes);
        if (!g || t > (pag === PAG.CADENA ? 15 : 7)) throw 2;
        const gr = resolver(g, pr);
        let q = Math.max(gr.min, Math.min(gr.max, v));
        /* la perilla SAMPLE solo acepta lugares con sample: el más cercano hacia abajo (o hacia arriba) */
        if (pag === PAG.EDIT && sub === 0 && k === 1 && pr.song[F.model + t] === SAMPLE_M(t)) q = sampleEn(q)?.lugar ?? q;
        g.set(pr, q);
        if (pag === PAG.GLO && sub === 1 && k === 2) { this._cambio(128); this._guardar(); }
        else this._cambio(pag === PAG.SCL ? 2 : 1, pr.song[F.cur], 1 << t);
        this.sel = t & 7;
        return [0, e.s14(g.accion ? q : g.get(pr)).u14(this.cont)];
      }
      case CMD.PERILLAS: {
        const t = l.u7(), pag = l.u7(), sub = l.u7();
        for (let k = 0; k < 4; k++) { const g = objetivo(t, pag, sub, k, this.ajustes); e.s14(g && !g.accion ? g.get(pr) : 0); }
        return [0, e];
      }
      case CMD.PASO: {
        const p = l.u7(), t = l.u7(), s = l.u7();
        if (p > 15 || t > 7 || s >= NS) throw 2;
        const reg = unpack7(l.b, l.i); const [st] = leerRegistro(reg, 0);
        pr.pats[p][t].st[s] = st;
        this._cambio(2, p, 1 << t);
        return [0, e.u14(this.cont)];
      }
      case CMD.PISTA: {
        const p = l.u7(), t = l.u7(), modo = l.u7();
        if (p > 15 || t > 7) throw 2;
        if (!modo) return [0, e.u7(pr.song[off.eu(p, t, 0)]).u7(pr.song[off.eu(p, t, 1)]).bloque(packPista(pr.pats[p][t]))];
        const [tr] = unpackPista(unpack7(l.b, l.i), 0);
        pr.pats[p][t] = tr; this._cambio(2, p, 1 << t);
        return [0, e.u14(this.cont)];
      }
      case CMD.TRANSPORTE: {
        const q = l.u7(), v = l.u7();
        if (q === 0) { this.tocando = !!v; if (v) { this.tick = 0; this.pos.fill(-1); this.loops.fill(0); this.tAcc = 0; this.t0 = performance.now(); } else this.pos.fill(-1); }
        else if (q === 1) { if (v > 15) throw 2; if (this.tocando) this.cola = v; else pr.song[F.cur] = v; }
        else if (q === 2) this.mute = v | l.u7() << 7;
        else if (q === 3) this.solo = v | l.u7() << 7;
        else throw 2;
        this._cambio(q === 1 && !this.tocando ? 8 : 4);
        return [0, e.u14(this.cont)];
      }
      case CMD.NOMBRE: {
        const i = l.u7(); if (!this.banco[i]) throw 2;
        this.banco[i].nombre = new TextDecoder().decode(unpack7(l.b, l.i)).replace(/\0.*$/, '').slice(0, 10).toUpperCase();
        this._cambio(16); return [0, e.u14(this.cont)];
      }
      case CMD.AJUSTE: {
        const c = l.u7(), modo = l.u7(); if (c !== 0) throw 2;
        if (modo) { this.ajustes.idioma = Math.max(0, Math.min(2, l.s14())); this._cambio(128); }
        return [0, e.s14(this.ajustes.idioma)];
      }
      case CMD.LISTA: {
        const o = l.u7();
        if (o === OBJ.RANURA) { e.u7(17); for (const s of this.ranuras) e.u21(s ? s.length : 0).u32(s ? crc32(s) : 0); return [0, e]; }
        if (o === OBJ.SAMPLE) {
          const hay = this.samples.map((s, i) => [s, i]).filter(x => x[0]);
          e.u21(this._libre()).u7(hay.length);
          for (const [s, i] of hay) { const nm = new TextEncoder().encode(s.nombre); e.u7(i).u21(s.muestras).u21(s.datos.length).u32(crc32(s.datos)).u21(s.tasa).u7(s.raiz).u7(s.lazo).u21(s.lazoIni).u21(s.lazoFin).u7(nm.length).bloque(nm); }
          return [0, e];
        }
        if (o === OBJ.BANCO) { e.u7(this.banco.length).textos(this.banco.map(b => b.nombre)); return [0, e]; }
        throw 2;
      }
      case CMD.BORRAR: {
        const o = l.u7(), i = l.u7();
        if (o === OBJ.RANURA) { if (i > 15) throw 2; if (this.tocando) throw 4; this.ranuras[i] = null; this._cambio(32); }
        else if (o === OBJ.BANCO) { if (!this.banco[i]) throw 2; if (this.tocando) throw 4; this.banco.splice(i, 1); this._cambio(16); }
        else if (o === OBJ.SAMPLE) { if (i > 31) throw 2; if (this.tocando) throw 4; this.samples[i] = null; this._tuyos(); this._cambio(64); }
        else throw 2;
        return [0, e.u14(this.cont)];
      }
      case CMD.ABRIR: return this._abrir(l, e);
      case CMD.LEER: {
        if (!this.tr || this.tr.modo !== 0) throw 7;
        const d = l.u21(), n = Math.min(l.u14(), 512); this.tr.t = performance.now();
        return [0, e.u21(d).bloque(this.tr.datos.subarray(d, d + n))];
      }
      case CMD.ESCRIBIR: {
        if (!this.tr || this.tr.modo !== 1) throw 7;
        const d = l.u21(), b = unpack7(l.b, l.i); this.tr.t = performance.now();
        if (d !== this.tr.llego) { const x = new Esc().u21(this.tr.llego); throw Object.assign(new Error(), { datos: x, valueOf: () => 2 }); }
        if (d + b.length > this.tr.datos.length) throw 2;
        this.tr.datos.set(b, d); this.tr.llego = d + b.length;
        return [0, e.u21(this.tr.llego)];
      }
      case CMD.CERRAR: {
        const ap = l.u7(), t = this.tr; this.tr = null;
        if (!t) throw 7;
        if (ap && t.modo === 1) {
          if (t.llego !== t.datos.length || crc32(t.datos) !== t.crc) throw 6;
          this._aplicar(t);
        }
        return [0, e.u14(this.cont)];
      }
    }
    throw 1;
  }
  _abrir(l, e) {
    if (this.tr && performance.now() - this.tr.t < 5000) throw 3;
    const o = l.u7(), i = l.u7(), modo = l.u7();
    const t = { o, i, modo, t: performance.now(), llego: 0 };
    if (!modo) {
      let d = null;
      if (o === OBJ.PROYECTO) d = pack(this.pr);
      else if (o === OBJ.BANCO) d = this._packBanco();
      else if (o === OBJ.RANURA) { if (i > 15) throw 2; d = this.ranuras[i]; if (!d) throw 11; }
      else if (o === OBJ.AUTO) { d = this.ranuras[16]; if (!d) throw 11; }
      else if (o === OBJ.SAMPLE) { if (i > 31) throw 2; d = this.samples[i]?.datos; if (!d) throw 11; }
      else throw 2;
      t.datos = Uint8Array.from(d); this.tr = t;
      return [0, e.u21(t.datos.length).u32(crc32(t.datos))];
    }
    const largo = l.u21(); t.crc = l.u32();
    if (o === OBJ.AUTO) throw 2;
    if (o !== OBJ.PROYECTO && this.tocando) throw 4;
    if (o === OBJ.RANURA && (i > 15)) throw 2;
    if ((o === OBJ.RANURA || o === OBJ.PROYECTO) && largo > RANURA) { throw Object.assign(new Error(), { datos: new Esc().u21(RANURA), valueOf: () => 5 }); }
    if (o === OBJ.SAMPLE) {
      if (i > 31) throw 2;
      t.tasa = l.u21(); t.muestras = l.u21(); t.raiz = l.u7(); t.lazo = l.u7(); t.lazoIni = l.u21(); t.lazoFin = l.u21(); t.nombre = new TextDecoder().decode(unpack7(l.b, l.i)).replace(/\0.*$/s, '');   /* el nombre va al final, empacado */
      const viejo = this.samples[i] ? Math.ceil(this.samples[i].datos.length / SECTOR) * SECTOR : 0, cabe = this._libre() + viejo;
      if (Math.ceil(largo / SECTOR) * SECTOR > cabe) throw Object.assign(new Error(), { datos: new Esc().u21(cabe), valueOf: () => 5 });
    }
    t.datos = new Uint8Array(largo); this.tr = t;
    return [0, e];
  }
  _aplicar(t) {
    if (t.o === OBJ.PROYECTO) { const p = unpack(t.datos); this.pr = p; this._cambio(8); }
    else if (t.o === OBJ.RANURA) { unpack(t.datos); this.ranuras[t.i] = t.datos; this._cambio(32); }
    else if (t.o === OBJ.BANCO) { this.banco = this._unpackBanco(t.datos); this._cambio(16); }
    else if (t.o === OBJ.SAMPLE) {
      this.samples[t.i] = { nombre: t.nombre.slice(0, 16), datos: t.datos, tasa: t.tasa, muestras: t.muestras, raiz: t.raiz, lazo: t.lazo, lazoIni: t.lazoIni, lazoFin: t.lazoFin };
      this._tuyos();
      this._cambio(64);
    }
  }
  _packBanco() {
    const o = [0x83, this.banco.length];
    for (const b of this.banco) { const n = new TextEncoder().encode(b.nombre).slice(0, 10), nm = new Uint8Array(11); nm.set(n); o.push(b.pista, b.modelo, ...nm, ...b.p); }
    return Uint8Array.from(o);
  }
  _unpackBanco(d) {
    if (d[0] < 0x81 || d[0] > 0x83) throw 10;
    const n = d[1], o = [];
    for (let k = 0; k < n; k++) { const b = d.subarray(2 + k * 21, 23 + k * 21); const z = b.indexOf(0, 2); o.push({ pista: b[0], modelo: b[1], nombre: new TextDecoder().decode(b.subarray(2, z < 0 || z > 12 ? 12 : z)), p: [...b.subarray(13, 21)] }); }
    return o;
  }
  /* META (rev. 2): un solo bloque empacado */
  _meta(l, e) {
    const tipo = l.u7(), pr = this.pr;
    let d;
    if (tipo === 0) d = meta.pistas;
    else if (tipo === 1) { const t = l.u7(); if (t > 7) throw 2; d = meta.modelos[t]; }
    else if (tipo === 2) {
      const t = l.u7(), pag = l.u7(), sub = l.u7();
      d = [0, 1, 2, 3].map(k => { const g = resolver(objetivo(t, pag, sub, k, this.ajustes), pr); return g ? { nombre: g.nombre || '', min: g.min, max: g.max } : { nombre: '', min: 0, max: 0 }; });
    } else if (tipo === 3) {
      const t = l.u7(), pag = l.u7(), sub = l.u7(), k = l.u7(), v = l.s14(), g = resolver(objetivo(t, pag, sub, k, this.ajustes), pr);
      if (!g) throw 2;
      d = g.fmt(v);
    } else if (tipo === 4) { const t = l.u7(), m = l.u7(); d = presets(t, m).map(x => x.nombre); }
    else if (tipo === 5) { const b = l.u7(); if (b > 3) throw 2; d = meta.samples[b]; }
    else if (tipo === 6) { const q = l.u7(); d = q === 3 ? perillasMotor(l.u7() & 7, pr.song[F.model]).slice(0, 7) : [meta.condiciones, meta.escalas, meta.formas, null, meta.performance][q]; if (!d) throw 2; }
    else throw 2;
    return e.bloque(armarMeta(tipo, d));
  }
  _tuyos() { meta.samples[3] = this.samples.map((s, i) => s && { lugar: 96 + i, nombre: s.nombre }).filter(Boolean); }

  /* ---------------- el secuenciador de mentiras ---------------- */
  _reloj() {
    if (!this.tocando) return;
    const now = performance.now(), dt = 60000 / this.pr.song[F.bpm] / 8;   /* un tick = 1/32 */
    while (now - this.t0 >= dt) { this.t0 += dt; this._tick(); }
  }
  _tick() {
    const pr = this.pr, tk = this.tick++;
    if (tk % 32 === 0 && this.cola !== 255) { pr.song[F.cur] = this.cola; this.cola = 255; this.pos.fill(-1); this._cambio(4); }
    const p = pr.song[F.cur] % 16;
    for (let t = 0; t < 8; t++) {
      const tr = pr.pats[p][t], each = [4, 2, 1][tr.spd] || 2;
      if (tk % each) continue;
      const s = (this.pos[t] + 1) % tr.len;
      if (s === 0 && this.pos[t] >= 0) this.loops[t]++;
      this.pos[t] = s;
      const st = tr.st[s];
      if (!st.v) continue;
      if (this.solo ? !(this.solo >> t & 1) : this.mute >> t & 1) continue;
      if (st.p < 100 && r() * 100 >= st.p) continue;
      if (st.c >= 1 && st.c <= 9) { const AB = [[1, 2], [2, 2], [1, 3], [2, 3], [3, 3], [1, 4], [2, 4], [3, 4], [4, 4]][st.c - 1]; if (this.loops[t] % AB[1] !== AB[0] - 1) continue; }
      if (st.c === 10) continue;
      const m = pr.song[F.model + t], ps = [];
      for (let i = 0; i < 8; i++) { const b = pr.song[off.p(t, m, i)], lk = st.lk[i]; ps.push(lk === 255 ? b : esSinte(t) && i === 0 ? Math.max(0, Math.min(127, b + lk - 64)) : lk); }
      const vel = st.v === 2 ? 127 : 96;
      const golpe = () => { this.nivel[t] = Math.min(1, vel / 127); if (this.escucha & 1 && performance.now() < this.escuchaHasta) this._sale(Uint8Array.from([0xF0, 0x7D, 0x46, 0x47, 1, t, m, vel, ...ps, 0xF7])); };
      golpe();
      for (let k = 1; k < st.r; k++) setTimeout(golpe, k * 60000 / pr.song[F.bpm] / 4 / each * 2 / st.r);
    }
  }
  _estado() {
    for (let t = 0; t < 8; t++) this.nivel[t] *= 0.86;
    if (!(this.escucha & 1) || performance.now() > this.escuchaHasta) return;
    const pr = this.pr, bpm = pr.song[F.bpm], used = this._usados(), a = [0, bpm >> 7, bpm & 127, this.sel, pr.song[F.cur], this.cola === 255 ? 127 : this.cola,
      used >> 14 & 127, used >> 7 & 127, used & 127, this.mute >> 7, this.mute & 127, this.solo >> 7, this.solo & 127, 0, 0, 0, 0, 0, 0];
    for (let t = 0; t < 8; t++) a.push(Math.min(127, this.nivel[t] * 127 | 0));
    for (let t = 0; t < 8; t++) for (let i = 0; i < 4; i++) a.push(pr.song[off.mix(t, i)] & 127);
    /* desde 0.9: al final, el paso de cada pista (127 = parada) y si toca (74 bytes de F0 a F7) */
    for (let t = 0; t < 8; t++) a.push(this.tocando && this.pos[t] >= 0 ? this.pos[t] : 127);
    a.push(this.tocando ? 1 : 0);
    this._sale(Uint8Array.from([0xF0, 0x7D, 0x46, 0x47, 2, ...a, 0xF7]));
  }
  _usados() { let u = 0; this.pr.pats.forEach((pt, i) => { if (pt.some(tr => tr.st.some(s => s.v))) u |= 1 << i; }); return u; }
}

/* el proyecto con el que arranca la demostración: un ritmo con locks, probabilidad y automatización */
export function ejemplo() {
  const pr = proyectoVacio(), s = pr.song;
  s[F.bpm] = 124; s[F.swing] = 12;
  [0, 1, 0, 1, 1, 6, 2, 5].forEach((m, t) => s[F.model + t] = m);
  for (let t = 0; t < 8; t++) { const m = s[F.model + t], ps = presets(t, m)[t === 4 ? 3 : 1]; if (ps) ps.p.forEach((v, i) => s[off.p(t, m, i)] = v); }
  const P = pr.pats[0];
  P.forEach(tr => tr.len = 16);
  for (let i = 0; i < 16; i += 4) P[0].st[i].v = 1;
  P[0].st[14].v = 1; P[0].st[14].p = 50;
  P[1].st[4].v = 2; P[1].st[12].v = 1; P[1].st[15].v = 1; P[1].st[15].c = 1;
  for (let i = 2; i < 16; i += 2) P[2].st[i].v = i % 4 ? 1 : 2;
  P[2].len = 12;
  [3, 7, 10, 11].forEach(i => { P[3].st[i].v = 1; P[3].st[i].lk[1] = 20 + i * 8; });
  P[3].st[11].r = 3;
  const bajo = [0, 3, 6, 8, 10, 11, 14], notas = [64, 64, 67, 64, 70, 64, 63];
  bajo.forEach((i, k) => { const st = P[4].st[i]; st.v = k === 0 ? 2 : 1; if (notas[k] !== 64) st.lk[0] = notas[k]; if (k % 2) st.lk[1] = 30 + k * 12; if (k === 4) { st.lk[4] = 110; st.u = 3; } });
  P[4].st[6].au = [[1, 90]]; P[4].st[10].au = [[1, 40]];
  [0, 8].forEach(i => { P[5].st[i].v = 1; P[5].st[i].lk[0] = i ? 67 : 64; P[5].st[i].lk[10] = 90; });
  P[5].len = 32;
  P[6].st[2].v = 1; P[6].st[9].v = 1; P[6].st[9].p = 70; P[6].st[13].v = 1; P[6].st[13].lk[9] = 30;
  P[7].st[0].v = 1; P[7].len = 32; P[7].spd = 0;
  /* el patrón 2 es una variación */
  pr.pats[1] = pr.pats[0].map(tr => ({ len: tr.len, spd: tr.spd, st: tr.st.map(x => ({ ...x, lk: Uint8Array.from(x.lk), au: x.au.map(a => [...a]) })) }));
  [2, 6, 10, 14].forEach(i => pr.pats[1][0].st[i].v = i === 14 ? 1 : 0);
  pr.pats[1][1].st[7].v = 1; pr.pats[1][1].st[7].r = 2;
  return pr;
}
