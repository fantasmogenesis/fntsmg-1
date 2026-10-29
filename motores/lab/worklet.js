// Laboratorio: corre los motores en C (WASM), la mezcladora y un secuenciador de 16 pasos con exactitud
// de muestra. 8 pistas: 0 bombo, 1 caja, 2 metal, 3 perc, 4..7 sintes.
// Locks por paso: 0..7 parámetros del motor, 8..11 mezcla (volumen, paneo, delay, reverb).
const NOOP = new Proxy({}, { get: () => () => 0 });
const IMPORTS = new Proxy({}, { get: () => NOOP });
const T = 8, MIXN = 4;
// locks relativos: la NOTA del paso se mueve con la NOTA de la pista; CORTE de chops recorre las rebanadas
function relNote(base, lk) { const sb = Math.floor(base * 61 / 128), s = Math.max(0, Math.min(60, sb + lk - 64)); return Math.min(127, Math.ceil(s * 128 / 61)); }
function relTrans(base, lk) { const sb = Math.floor(base * 49 / 128) - 24, s = Math.max(-24, Math.min(24, sb + lk - 64)); return Math.min(127, Math.ceil((s + 24) * 128 / 49)); }
function relSlice(base, lk) { const sb = Math.floor(base * 16 / 128), s = ((sb + lk - 64) % 16 + 16) % 16; return s * 8 + 4; }

class Lab extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.tr = Array.from({ length: T }, () => ({ steps: new Array(16).fill(0), locks: Array.from({ length: 16 }, () => ({})), auto: [], model: 0, mixLocked: 0 }));
    this.mix = Array.from({ length: T }, () => [100, 64, 0, 0]);
    this.pbase = Array.from({ length: T }, () => Array.from({ length: 6 }, () => new Array(8).fill(64)));
    this.mute = new Array(T).fill(0); this.solo = new Array(T).fill(0); this.pend = null; this.muteQ = false;
    this.bpm = 124; this.swing = 0; this.playing = false;
    this.clock = 0; this.k = 0; this.nextAt = 0; this.brake = 1;
    this.seq = {}; this.repA = 0; this.repK = -1;
    this.preDone = new Set(); this.manual = [];
    this.blocks = 0;
    this.port.onmessage = e => this.msg(e.data);
  }
  async msg(d) {
    if (d.type === 'wasm') {
      const { instance } = await WebAssembly.instantiate(d.bytes, IMPORTS);
      const ex = this.ex = instance.exports;
      if (ex._initialize) ex._initialize();
      ex.lab_init();
      const s = d.state;
      s.models.forEach((m, t) => { ex.lab_model(t, m); this.tr[t].model = m; });
      s.params.forEach(([t, m, i, v]) => { ex.lab_param(t, m, i, v); this.pbase[t][m][i] = v; });
      s.mix.forEach((arr, t) => arr.forEach((v, i) => { this.mix[t][i] = v; ex.lab_mix(t, i, v); }));
      s.fx.forEach((v, i) => ex.lab_fx(i, v));
      if (d.sample) this.setSample(d.sample.data, d.sample.rate);
      this.tempo();
      const tab = new Int16Array(ex.memory.buffer, ex.lab_table(), 64 * 256).slice();
      this.port.postMessage({ type: 'ready', table: tab }, [tab.buffer]);
      return;
    }
    const ex = this.ex; if (!ex) return;
    switch (d.type) {
      case 'param': ex.lab_param(d.t, d.m, d.i, d.v); this.pbase[d.t][d.m][d.i] = d.v; break;
      case 'model': ex.lab_model(d.t, d.m); this.tr[d.t].model = d.m; break;
      case 'steps': this.tr[d.t].steps = d.steps; break;
      case 'locks': this.tr[d.t].locks = d.locks; break;
      case 'auto': this.tr[d.t].auto = d.auto; break;
      case 'mix': this.mix[d.t][d.i] = d.v; ex.lab_mix(d.t, d.i, d.v); break;
      case 'mute':
        if (this.muteQ && this.playing && !d.now) this.pend = { mute: d.mute, solo: d.solo };
        else this.applyMute(d.mute, d.solo);
        break;
      case 'muteq': this.muteQ = d.on; break;
      case 'fx': ex.lab_fx(d.i, d.v); break;
      case 'perf':
        ex.lab_perf(d.flags);
        if (d.seq.rep4 || d.seq.rep8) { if (this.repK < 0) { this.repA = this.k; this.repK = 0; } } else this.repK = -1;
        this.seq = d.seq;
        break;
      case 'trig': this.audition(d.t, d.vel || 110, d.locks || {}); break;
      case 'bpm': this.bpm = d.bpm; this.tempo(); break;
      case 'swing': this.swing = d.swing; break;
      case 'play': this.playing = d.on; this.k = 0; this.nextAt = this.clock + 64; this.preDone.clear(); this.repK = -1; break;
      case 'table': new Int16Array(ex.memory.buffer, ex.lab_table(), 64 * 256).set(d.data); break;
      case 'sample': this.setSample(d.data, d.rate); break;
    }
  }
  setSample(data, rate) {
    const max = this.ex.lab_sample_max(), n = Math.min(max, data.length);
    new Int16Array(this.ex.memory.buffer, this.ex.lab_sample(), n).set(data.subarray(0, n));
    this.ex.lab_sample_set(n, rate);
  }
  applyMute(mute, solo) { this.mute = mute; this.solo = solo; for (let t = 0; t < T; t++) this.ex.lab_mute(t, mute[t] ? 1 : 0, solo[t] ? 1 : 0); this.port.postMessage({ type: 'muted', mute, solo }); }
  base() { return sampleRate * 60 / this.bpm / 4; }
  dur(k) {
    const b = this.base(), sw = this.swing / 100 * b * 0.66;
    return Math.round(k % 2 ? b - sw : b + sw);
  }
  tempo() { if (this.ex) this.ex.lab_tempo(Math.round(this.base())); }
  timeOf(j) { let t = this.nextAt; for (let q = 0; q < j; q++) t += this.dur(this.k + q); return t; }
  // el paso que suena: normal, en reversa o repitiendo los últimos pasos
  stepIndex() {
    const rep = this.seq.rep8 ? 2 : this.seq.rep4 ? 4 : 0;
    if (rep && this.repK >= 0) { const s = ((this.repA - rep + (this.repK % rep)) % 16 + 16) % 16; this.repK++; return s; }
    return this.k % 16;
  }
  gateFor(tr, s) {
    let n = 1; for (; n < 16; n++) if (tr.steps[(s + n) % 16]) break;
    return Math.round(n * this.base() * 0.95);
  }
  loadLocks(t, lk) {
    const buf = new Uint8Array(this.ex.memory.buffer, this.ex.lab_locks(t), 8); let mask = 0;
    const m = this.tr[t].model, b = this.pbase[t][m];
    for (const i in lk) { const n = +i; if (n >= 8) continue; let v = lk[i];
      if (t >= 4 && n === 0) v = m === 3 ? relTrans(b[0], v) : relNote(b[0], v);
      else if (t >= 4 && m === 3 && n === 1) v = relSlice(b[1], v);
      buf[n] = v; mask |= 1 << n; }
    return mask;
  }
  audition(t, vel, lk) {
    const ahead = this.ex.lab_ahead(t);
    if (ahead > 0) {
      const L = Math.round(ahead * this.base());
      this.ex.lab_pre(t, L); this.port.postMessage({ type: 'pre', t, ms: L / sampleRate * 1000 });
      this.manual.push({ at: this.clock + L, t, vel, lk });
    } else this.fire(t, vel, lk, Math.round(this.base() * 3.8));
  }
  fire(t, vel, lk, gate) {
    const mask = this.loadLocks(t, lk);
    this.ex.lab_trig(t, vel, mask, gate);
    this.port.postMessage({ type: 'hit', t, vel, lk });
  }
  fireStep() {
    if (this.pend && this.k % 16 === 0) { this.applyMute(this.pend.mute, this.pend.solo); this.pend = null; }
    const s = this.stepIndex(), hits = [];
    const nokick = this.seq.nokick, soloperc = this.seq.soloperc;
    this.tr.forEach((tr, t) => {
      const a = tr.auto[s]; if (a) for (const i in a) this.ex.lab_param(t, tr.model, +i, a[i]);
      const lk = tr.locks[s] || {};
      // locks de mezcla: valen este paso; si no hay, la pista regresa a su mezcla
      for (let i = 0; i < MIXN; i++) {
        const v = lk[8 + i];
        if (v != null) { this.ex.lab_mix(t, i, v); tr.mixLocked |= 1 << i; }
        else if (tr.mixLocked & (1 << i)) { this.ex.lab_mix(t, i, this.mix[t][i]); tr.mixLocked &= ~(1 << i); }
      }
      const v = tr.steps[s];
      if (!v || (nokick && t === 0) || (soloperc && t >= 4)) return;
      const vel = v === 2 ? 127 : 100, r = 1;
      const mask = this.loadLocks(t, lk);
      this.ex.lab_trig(t, vel, mask, this.gateFor(tr, s));
      if (r > 1) this.manual.push({ at: this.clock + Math.round(this.dur(this.k) / 2), t, vel, lk, quiet: true });
      hits.push([t, vel, lk]);
    });
    this.port.postMessage({ type: 'step', step: s, hits });
    this.nextAt += this.dur(this.k); this.k++;
  }
  events(n) {
    const ev = [], end = this.clock + n;
    if (this.playing) {
      if (this.nextAt < end) ev.push({ at: this.nextAt, kind: 'step' });
      this.tr.forEach((tr, t) => {           // subidas de la anticipación para los pasos que vienen
        const ahead = this.ex.lab_ahead(t); if (!ahead) return;
        const L = Math.round(ahead * this.base());
        for (let j = 0; j <= ahead + 1; j++) {
          const kk = this.k + j; if (!tr.steps[kk % 16]) continue;
          const key = t * 1e7 + kk; if (this.preDone.has(key)) continue;
          const hitAt = this.timeOf(j), start = hitAt - L;
          if (start < end) { this.preDone.add(key); const at = Math.max(start, this.clock); ev.push({ at, kind: 'pre', t, len: hitAt - at }); }
        }
      });
      if (this.preDone.size > 64) this.preDone = new Set([...this.preDone].slice(-32));
    }
    this.manual = this.manual.filter(m => { if (m.at < end) { ev.push({ at: m.at, kind: 'manual', m }); return false; } return true; });
    return ev.sort((a, b) => a.at - b.at);
  }
  renderTo(L, R, o, k) {
    if (k <= 0) return;
    const p = this.ex.lab_render(k), buf = new Float32Array(this.ex.memory.buffer, p, k * 2);
    for (let j = 0; j < k; j++) { L[o + j] = buf[2 * j]; R[o + j] = buf[2 * j + 1]; }
  }
  process(_, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    if (!this.ex) return true;
    let o = 0;
    for (const ev of this.events(n)) {
      const at = Math.min(n, Math.max(o, Math.round(ev.at - this.clock)));
      this.renderTo(L, R, o, at - o); o = at;
      if (ev.kind === 'step') this.fireStep();
      else if (ev.kind === 'pre') { this.ex.lab_pre(ev.t, Math.round(ev.len)); this.port.postMessage({ type: 'pre', t: ev.t, ms: ev.len / sampleRate * 1000 }); }
      else { const m = ev.m; const mask = this.loadLocks(m.t, m.lk); this.ex.lab_trig(m.t, m.vel, mask, Math.round(this.base() * (m.quiet ? 0.45 : 3.8))); if (!m.quiet) this.port.postMessage({ type: 'hit', t: m.t, vel: m.vel, lk: m.lk }); }
    }
    this.renderTo(L, R, o, n - o);
    this.clock += n;
    if (++this.blocks % 4 === 0) {           // niveles por pista, para que el visual crezca con el volumen
      const lv = new Int32Array(this.ex.memory.buffer, this.ex.lab_levels(), T), rows = [];
      for (let t = 4; t < 8; t++) rows.push(this.ex.lab_row(t));
      this.port.postMessage({ type: 'levels', lv: Array.from(lv, x => x / 32768), rows });
    }
    return true;
  }
}
registerProcessor('lab', Lab);
