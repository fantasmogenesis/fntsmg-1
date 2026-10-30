// Cómo llegan los bytes al FM-1. Una sola forma para todos (conectar, enviar, onMensaje, onEstado) y tres caminos:
//   WebMIDI  Chrome y Edge en computadora, Chrome en Android.
//   puente   la app nativa de iPad (Capacitor + un plugin de CoreMIDI), porque Safari no tiene WebMIDI. El plugin
//            se llama FntsmgMidi y da: conectar() -> {nombre}, enviar({datos: number[]}), desconectar(), y los
//            eventos 'mensaje' {datos: number[]} (un SysEx completo, F0..F7) y 'estado' {conectado: boolean}.
//   demo     un FM-1 inventado dentro de la página (aparato-demo.js), que contesta el mismo protocolo.
// El resto de la app no sabe cuál está usando.

const PUERTO = /felucca|fntsmg|fm-?1|m-?vave/i;

export function detectar() {
  const ua = navigator.userAgent, plat = navigator.platform || '';
  const ios = /iPad|iPhone|iPod/.test(ua) || (plat === 'MacIntel' && navigator.maxTouchPoints > 1);
  const puente = !!globalThis.Capacitor?.Plugins?.FntsmgMidi;
  const webmidi = typeof navigator.requestMIDIAccess === 'function';
  const navegador = /Edg\//.test(ua) ? 'edge' : /Firefox\//.test(ua) ? 'firefox' : /Chrome\//.test(ua) ? 'chrome' : /Safari\//.test(ua) ? 'safari' : 'otro';
  return { webmidi, puente, ios, navegador, android: /Android/.test(ua), seguro: globalThis.isSecureContext !== false };
}

/* la razón por la que no se puede conectar, para explicarla en palabras (null: sí se puede) */
export function sinMidi(d = detectar()) {
  if (d.puente || d.webmidi) return d.seguro ? null : 'inseguro';
  if (d.ios) return 'ios';
  if (d.navegador === 'safari') return 'safari';
  if (d.navegador === 'firefox') return 'firefox';
  return 'navegador';
}

export class TransporteWebMIDI {
  constructor() { this.tipo = 'webmidi'; this.onMensaje = () => {}; this.onEstado = () => {}; this.acc = null; this.in = this.out = null; }
  async conectar() {
    try { this.acc = await navigator.requestMIDIAccess({ sysex: true }); }
    catch (e) { throw Object.assign(new Error('PERMISO'), { codigo: 'PERMISO' }); }
    this.acc.onstatechange = () => this._buscar();
    if (!this._buscar()) throw Object.assign(new Error('NO_APARATO'), { codigo: 'NO_APARATO' });
  }
  _buscar() {
    let i = null, o = null;
    this.acc.inputs.forEach(p => { if (PUERTO.test(p.name) && p.state !== 'disconnected') i = p; });
    this.acc.outputs.forEach(p => { if (PUERTO.test(p.name) && p.state !== 'disconnected') o = p; });
    const antes = !!(this.in && this.out);
    if (this.in && this.in !== i) this.in.onmidimessage = null;
    this.in = i; this.out = o;
    if (i) i.onmidimessage = e => { if (e.data[0] === 0xF0) this.onMensaje(e.data); };
    this.nombre = o?.name || '';
    const ahora = !!(i && o);
    if (ahora !== antes) this.onEstado(ahora ? 'conectado' : 'perdido');
    return ahora;
  }
  enviar(b) { if (!this.out) throw Object.assign(new Error('NO_APARATO'), { codigo: 'NO_APARATO' }); this.out.send(b); }
  /* la página del espejo de 0.8.x pide el visual con CC 119 en el canal 16 */
  cc(ch, n, v) { try { this.out?.send([0xB0 | (ch - 1), n, v]); } catch (e) {} }
  desconectar() { if (this.in) this.in.onmidimessage = null; if (this.acc) this.acc.onstatechange = null; this.in = this.out = null; }
}

export class TransportePuente {
  constructor() { this.tipo = 'puente'; this.onMensaje = () => {}; this.onEstado = () => {}; this.p = globalThis.Capacitor.Plugins.FntsmgMidi; this.l = []; }
  async conectar() {
    this.l.push(await this.p.addListener('mensaje', e => this.onMensaje(Uint8Array.from(e.datos))));
    this.l.push(await this.p.addListener('estado', e => this.onEstado(e.conectado ? 'conectado' : 'perdido')));
    const r = await this.p.conectar();
    if (!r?.nombre) throw Object.assign(new Error('NO_APARATO'), { codigo: 'NO_APARATO' });
    this.nombre = r.nombre;
  }
  enviar(b) { this.p.enviar({ datos: Array.from(b) }); }
  cc(ch, n, v) { this.p.enviar({ datos: [0xB0 | (ch - 1), n, v] }); }
  desconectar() { this.l.forEach(x => x.remove?.()); this.l = []; this.p.desconectar?.(); }
}

export class TransporteDemo {
  constructor(aparato) { this.tipo = 'demo'; this.nombre = 'FNTSMG-1 (demostración)'; this.a = aparato; this.onMensaje = () => {}; this.onEstado = () => {}; }
  async conectar() { this.a.salida = d => this.onMensaje(d); }
  /* como el USB: la respuesta llega después, nunca en la misma vuelta */
  enviar(b) { const c = Uint8Array.from(b); setTimeout(() => this.a.recibir(c), 2); }
  cc() {}
  desconectar() { this.a.salida = null; }
}

export function transporteReal(d = detectar()) { return d.puente ? new TransportePuente() : new TransporteWebMIDI(); }
