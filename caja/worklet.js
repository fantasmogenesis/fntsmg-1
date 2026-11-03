// La caja: el firmware del FM-1 (caja/fntsmg1.wasm) corriendo en el hilo de audio.
// Cada process() es un bloque de 128, igual que el I2S del aparato. Cada 11 bloques (~32 ms) manda a la página
// las bandas de pantalla que cambiaron y las luces; cuando el aparato guarda, manda la ranura para guardarla.
class Caja extends AudioWorkletProcessor {
  constructor() {
    super();
    this.X = null; this.n = 0;
    this.port.onmessage = (e) => this.msg(e.data);
  }
  async msg(m) {
    if (m.t === 'init') {
      const { instance } = await WebAssembly.instantiate(m.wasm, {});
      const X = instance.exports;
      if (X._initialize) X._initialize();
      X.cj_init(m.seed >>> 0);
      for (const [i, bytes] of Object.entries(m.slots || {})) {
        const p = X.cj_slot(+i); if (!p || bytes.length > 16384) continue;
        new Uint8Array(X.memory.buffer, p, bytes.length).set(bytes); X.cj_set_slot_len(+i, bytes.length);
      }
      X.cj_boot(); X.cj_repaint();
      this.X = X;
      this.port.postMessage({ t: 'listo' });
      return;
    }
    const X = this.X; if (!X) return;
    if (m.t === 'btn') X.cj_button(m.id, m.d ? 1 : 0);
    else if (m.t === 'key') X.cj_key(m.k, m.d ? 1 : 0);
    else if (m.t === 'enc') X.cj_enc(m.r, m.s | 0);
    else if (m.t === 'repaint') X.cj_repaint();
  }
  process(_in, outs) {
    const X = this.X, o = outs[0];
    if (!X) return true;
    X.cj_block();
    const mem = X.memory.buffer;
    o[0].set(new Float32Array(mem, X.cj_left(), 128));
    if (o[1]) o[1].set(new Float32Array(mem, X.cj_right(), 128));
    if (++this.n >= 11) {
      this.n = 0;
      X.cj_frame();
      const bands = [], xfer = [];
      for (let y = 0; y < 240; y += 16) {
        if (!X.cj_band(y)) continue;
        const px = new Uint16Array(X.memory.buffer, X.cj_px(), 240 * 16).slice();
        bands.push([y, px]); xfer.push(px.buffer);
      }
      const msg = { t: 'f', bands, bt: X.cj_leds_buttons(), ky: X.cj_leds_keys(), half: X.cj_leds_half() };
      const d = X.cj_dirty();
      if (d) {
        msg.slots = {};
        for (let i = 0; i < X.cj_nslots(); i++) if (d & (1 << i)) {
          const len = X.cj_slot_len(i);
          msg.slots[i] = new Uint8Array(X.memory.buffer, X.cj_slot(i), len).slice();
        }
      }
      this.port.postMessage(msg, xfer);
    }
    return true;
  }
}
registerProcessor('caja-fntsmg1', Caja);
