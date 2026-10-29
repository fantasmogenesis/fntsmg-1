// La caja: el secuenciador del FM-1 sobre los motores en C (WASM). Es la versión de referencia de lo que va
// a pasar a C (fg_seq): 8 pistas con largo y velocidad propios, locks de motor y de mezcla, automatización,
// condición / probabilidad / ratchet / microtiming por paso, LFO por pista, 16 patrones y cadenas.
// El reloj corre en 1/32; cada paso se decide 2 ticks antes para poder adelantarlo con microtiming.
const NOOP = new Proxy({}, { get: () => () => 0 });
const IMPORTS = new Proxy({}, { get: () => NOOP });
const T = 8, MIXN = 4, NS = 32, NP = 16;
const MULT = [4, 2, 1];                       // ticks de 1/32 por paso: velocidad ½, 1, 2
const CONDAB = [null, [1, 2], [2, 2], [1, 3], [2, 3], [3, 3], [1, 4], [2, 4], [3, 4], [4, 4]];   // 10 FILL, 11 SIN FILL
function relNote(base, lk) { const sb = Math.floor(base * 61 / 128), s = Math.max(0, Math.min(60, sb + lk - 64)); return Math.min(127, Math.ceil(s * 128 / 61)); }
function relTrans(base, lk) { const sb = Math.floor(base * 49 / 128) - 24, s = Math.max(-24, Math.min(24, sb + lk - 64)); return Math.min(127, Math.ceil((s + 24) * 128 / 49)); }
function relSlice(base, lk) { const sb = Math.floor(base * 16 / 128), s = ((sb + lk - 64) % 16 + 16) % 16; return s * 8 + 4; }
// AZAR: qué mueve cada motor en cada golpe — [color/tono, caída]. En los sintes nunca la NOTA.
const AZ = [[[0, 2], [0, 2]], [[0, 3], [0, 3]], [[2, 1], [0, 1]], [[0, 2], [0, 3]]];
const AZS = [[1, 6], [1, 4], [2, 5], [2, 5], [2, 5], [4, 6]];   // pliegue ácido fase muelle fm acorde: color, caída
const emptyTrack = () => ({ steps: new Array(NS).fill(0), locks: Array.from({ length: NS }, () => ({})), auto: Array.from({ length: NS }, () => ({})), props: Array.from({ length: NS }, () => ({})), len: 16, spd: 1 });

class Caja extends AudioWorkletProcessor {
  constructor() {
    super();
    this.ex = null;
    this.pats = Array.from({ length: NP }, () => Array.from({ length: T }, emptyTrack));
    this.cur = 0; this.queued = -1; this.chain = []; this.chainPos = 0;
    this.model = new Array(T).fill(0);
    this.pbase = Array.from({ length: T }, () => Array.from({ length: 6 }, () => new Array(8).fill(64)));
    this.mix = Array.from({ length: T }, () => [100, 64, 0, 0]);
    this.lfo = Array.from({ length: T }, () => [0, 40, 0, 0]);
    this.azar = new Array(T).fill(0);
    this.lfoPh = new Array(T).fill(0); this.lfoSH = new Array(T).fill(0); this.lfoLast = Array.from({ length: T }, () => [-1, -1]);
    this.mute = new Array(T).fill(0); this.solo = new Array(T).fill(0); this.pend = null; this.muteQ = false;
    this.bpm = 124; this.swing = 0; this.transp = 0; this.fill = false; this.playing = false;
    this.clock = 0; this.tick = 0; this.tickAt = 0; this.patTick = 0;
    this.pos = new Array(T).fill(-1); this.loops = new Array(T).fill(0); this.mixLocked = new Array(T).fill(0);
    this.seq = {}; this.repA = new Array(T).fill(0); this.repK = new Array(T).fill(-1);
    this.ev = []; this.preDone = new Set(); this.blocks = 0;
    this.port.onmessage = e => this.msg(e.data);
  }
  async msg(d) {
    if (d.type === 'wasm') {
      const { instance } = await WebAssembly.instantiate(d.bytes, IMPORTS);
      const ex = this.ex = instance.exports;
      if (ex._initialize) ex._initialize();
      ex.lab_init();
      const s = d.state;
      s.models.forEach((m, t) => { ex.lab_model(t, m); this.model[t] = m; });
      s.params.forEach(([t, m, i, v]) => { ex.lab_param(t, m, i, v); this.pbase[t][m][i] = v; });
      s.mix.forEach((arr, t) => arr.forEach((v, i) => { this.mix[t][i] = v; ex.lab_mix(t, i, v); }));
      s.fx.forEach((v, i) => ex.lab_fx(i, v));
      this.pats = s.pats; this.cur = s.cur; this.lfo = s.lfo; this.azar = s.azar || this.azar;
      this.bpm = s.bpm; this.swing = s.swing; this.transp = s.transp; this.muteQ = s.muteQ;
      if (d.sample) this.setSample(d.sample.data, d.sample.rate);
      if (d.table) new Int16Array(ex.memory.buffer, ex.lab_table(), 64 * 256).set(d.table);
      this.tempo();
      const tab = new Int16Array(ex.memory.buffer, ex.lab_table(), 64 * 256).slice();
      this.port.postMessage({ type: 'ready', table: tab }, [tab.buffer]);
      return;
    }
    const ex = this.ex; if (!ex) return;
    switch (d.type) {
      case 'param': ex.lab_param(d.t, d.m, d.i, d.v); this.pbase[d.t][d.m][d.i] = d.v; this.lfoLast[d.t][1] = -1; break;
      case 'model': this.restoreLfo(d.t); ex.lab_model(d.t, d.m); this.model[d.t] = d.m; break;
      case 'track': this.pats[d.p][d.t] = d.d; break;
      case 'pats': this.pats = d.pats; break;
      case 'mix': this.mix[d.t][d.i] = d.v; ex.lab_mix(d.t, d.i, d.v); break;
      case 'mute':
        if (this.muteQ && this.playing && !d.now) { this.pend = { mute: d.mute, solo: d.solo }; this.port.postMessage({ type: 'pending', mute: d.mute }); }
        else this.applyMute(d.mute, d.solo);
        break;
      case 'muteq': this.muteQ = d.on; break;
      case 'fx': ex.lab_fx(d.i, d.v); break;
      case 'lfo': this.restoreLfo(d.t); this.lfo[d.t] = d.v; break;
      case 'perf':
        ex.lab_perf(d.flags);
        for (let t = 0; t < T; t++) {
          if (d.seq.rep4 || d.seq.rep8) { if (this.repK[t] < 0) { this.repA[t] = this.pos[t] + 1; this.repK[t] = 0; } } else this.repK[t] = -1;
        }
        this.seq = d.seq; break;
      case 'fill': this.fill = d.on; break;
      case 'trig': this.audition(d.t, d.vel || 110, d.locks || {}); break;
      case 'bpm': this.bpm = d.bpm; this.tempo(); break;
      case 'swing': this.swing = d.swing; break;
      case 'transp': this.transp = d.v; break;
      case 'azar': this.azar[d.t] = d.v; break;
      case 'queue': this.chain = []; this.queue(d.p); break;
      case 'chain':
        this.chain = d.list.slice(); this.chainPos = 0;
        if (this.playing && d.list[0] === this.cur) { this.queued = -1; this.port.postMessage({ type: 'pat', cur: this.cur, queued: -1, chain: this.chain, chainPos: 0 }); }
        else this.queue(d.list[0]);
        break;
      case 'play':
        this.playing = d.on; this.ev = this.ev.filter(e => e.kind === 'manual'); this.preDone.clear();
        if (d.on) {
          this.pos.fill(-1); this.loops.fill(0); this.patTick = 0; this.tick = 0; this.tickAt = this.clock + 64;
          this.decide(0, this.tickAt); this.decide(1, this.tickAt + this.dTick(0));
        }
        break;
      case 'table': new Int16Array(ex.memory.buffer, ex.lab_table(), 64 * 256).set(d.data); break;
      case 'sample': this.setSample(d.data, d.rate); break;
    }
  }
  queue(p) {
    if (!this.playing) { this.switchTo(p); this.queued = -1; }
    else { this.queued = p; this.port.postMessage({ type: 'pat', cur: this.cur, queued: p, chain: this.chain, chainPos: this.chainPos }); }
  }
  switchTo(p) {
    this.cur = p; this.pos.fill(-1); this.loops.fill(0); this.patTick = 0;
    this.port.postMessage({ type: 'pat', cur: p, queued: -1, chain: this.chain, chainPos: this.chainPos });
  }
  setSample(data, rate) {
    const max = this.ex.lab_sample_max(), n = Math.min(max, data.length);
    new Int16Array(this.ex.memory.buffer, this.ex.lab_sample(), n).set(data.subarray(0, n));
    this.ex.lab_sample_set(n, rate);
  }
  applyMute(mute, solo) { this.mute = mute; this.solo = solo; for (let t = 0; t < T; t++) this.ex.lab_mute(t, mute[t] ? 1 : 0, solo[t] ? 1 : 0); this.port.postMessage({ type: 'muted', mute, solo }); }
  base16() { return sampleRate * 60 / this.bpm / 4; }
  dTick(n) { const b = this.base16(), sw = this.swing / 100 * b * 0.66; return ((n >> 1) % 2 ? b - sw : b + sw) / 2; }
  tempo() { this.ex.lab_tempo(Math.round(this.base16())); }
  // largo del patrón en ticks: lo que dura su pista más larga, redondeado a compases
  patTicks() { let m = 16; for (const tr of this.pats[this.cur]) m = Math.max(m, Math.ceil(tr.len * MULT[tr.spd] / 2 / 16) * 16); return m * 2; }
  loadLocks(t, lk) {
    const buf = new Uint8Array(this.ex.memory.buffer, this.ex.lab_locks(t), 8); let mask = 0;
    const m = this.model[t], b = this.pbase[t][m];
    for (const i in lk) { const n = +i; if (n >= 8) continue; let v = lk[i];
      if (t >= 4 && n === 0) v = relNote(b[0], v + this.transp);   // (el modelo 3 ya no es CHOPS: nota como los demás)
      buf[n] = v; mask |= 1 << n; }
    if (t >= 4 && this.transp && !(mask & 1)) { buf[0] = relNote(b[0], 64 + this.transp); mask |= 1; }
    return mask;
  }
  audition(t, vel, lk) {
    const ahead = this.ex.lab_ahead(t);
    if (ahead > 0) {
      const L = Math.round(ahead * this.base16());
      this.ex.lab_pre(t, L); this.port.postMessage({ type: 'pre', t, ms: L / sampleRate * 1000 });
      this.push({ at: this.clock + L, kind: 'manual', t, vel, lk, gate: Math.round(this.base16() * 3.8) });
    } else this.fire(t, vel, lk, Math.round(this.base16() * 3.8), true);
  }
  fire(t, vel, lk, gate, post) {
    const az = this.azar[t];
    if (az) {                                   // cada golpe un poco distinto: color/tono ±19, caída ±16, fuerza -30
      const m = this.model[t], tg = t < 4 ? AZ[t][m] : AZS[m], b = this.pbase[t][m], amt = az / 127; lk = Object.assign({}, lk);
      tg.forEach((i, k) => { const base = lk[i] != null ? lk[i] : b[i]; lk[i] = Math.max(0, Math.min(127, Math.round(base + (Math.random() * 2 - 1) * amt * (k ? 16 : 19)))); });
      vel = Math.max(1, Math.round(vel - Math.random() * amt * 30));
    }
    const mask = this.loadLocks(t, lk);
    this.ex.lab_trig(t, vel, mask, gate);
    if (post) this.port.postMessage({ type: 'hit', t, vel, lk });
  }
  push(e) { this.ev.push(e); }
  passes(t, pr) {
    const c = pr.c || 0;
    if (c >= 1 && c <= 9) { const [a, b] = CONDAB[c]; if (this.loops[t] % b !== a - 1) return false; }
    if (c === 10 && !this.fill) return false;
    if (c === 11 && this.fill) return false;
    if (pr.p != null && pr.p < 100 && Math.random() * 100 >= pr.p) return false;
    return true;
  }
  // decide qué pasa en el tick dk (que suena en "at"); se llama 2 ticks antes
  decide(dk, at) {
    const span = this.patTicks();
    if (this.patTick > 0 && this.patTick % span === 0) {
      if (this.queued >= 0) { const q = this.queued; this.queued = -1; this.switchTo(q); }
      else if (this.chain.length > 1) { this.chainPos = (this.chainPos + 1) % this.chain.length; this.switchTo(this.chain[this.chainPos]); }
    }
    if (dk % 32 === 0 && this.pend) { const p = this.pend; this.pend = null; this.push({ at, kind: 'mutes', p }); }
    const pk = this.patTick++;
    const pat = this.pats[this.cur], rep = this.seq.rep8 ? 2 : this.seq.rep4 ? 4 : 0;
    for (let t = 0; t < T; t++) {
      const tr = pat[t], m = MULT[tr.spd]; if (pk % m) continue;
      const prev = this.pos[t]; this.pos[t] = (prev + 1) % tr.len; if (prev >= 0 && this.pos[t] === 0) this.loops[t]++;
      let s = this.pos[t];
      if (rep && this.repK[t] >= 0) { s = ((this.repA[t] - rep + (this.repK[t] % rep)) % tr.len + tr.len) % tr.len; this.repK[t]++; }
      const stepDur = this.base16() * m / 2;
      this.push({ at, kind: 'apply', t, s, auto: tr.auto[s], lk: tr.locks[s] || {} });
      const v = tr.steps[s];
      if (!v || (this.seq.nokick && t === 0) || (this.seq.soloperc && t >= 4)) continue;
      const pr = tr.props[s] || {};
      if (!this.passes(t, pr)) continue;
      const vel = v === 2 ? 127 : 100, micro = Math.round((pr.u || 0) * stepDur / 16), r = pr.r || 1;
      let n = 1; for (; n < tr.len; n++) if (tr.steps[(s + n) % tr.len]) break;
      const gate = Math.round(n * stepDur * 0.95);
      for (let k = 0; k < r; k++) this.push({ at: at + micro + Math.round(k * stepDur / r), kind: 'trig', t, s, vel: k ? Math.round(vel * 0.85) : vel, lk: tr.locks[s] || {}, gate: r > 1 ? Math.round(stepDur / r * 0.9) : gate, first: k === 0 });
    }
    // la caja con anticipación empieza a crecer antes de sus pasos
    const ahead = this.ex.lab_ahead(1);
    if (ahead > 0 && pk % MULT[pat[1].spd] === 0) {
      const tr = pat[1], stepDur = this.base16() * MULT[tr.spd] / 2, L = Math.round(ahead * stepDur);
      for (let j = 1; j <= ahead + 1; j++) {
        const s = (this.pos[1] + j) % tr.len; if (!tr.steps[s]) continue;
        const key = this.cur * 1e6 + this.loops[1] * 64 + (this.pos[1] + j); if (this.preDone.has(key)) continue;
        const hitAt = at + j * stepDur, start = Math.max(at, hitAt - L);
        this.preDone.add(key); this.push({ at: start, kind: 'pre', t: 1, len: Math.round(hitAt - start) });
      }
      if (this.preDone.size > 64) this.preDone = new Set([...this.preDone].slice(-32));
    }
  }
  runEvent(e) {
    const ex = this.ex;
    switch (e.kind) {
      case 'apply': {
        const t = e.t, a = e.auto;
        if (a) for (const i in a) { ex.lab_param(t, this.model[t], +i, a[i]); this.pbase[t][this.model[t]][+i] = a[i]; }
        for (let i = 0; i < MIXN; i++) {
          const v = e.lk[8 + i];
          if (v != null) { ex.lab_mix(t, i, v); this.mixLocked[t] |= 1 << i; }
          else if (this.mixLocked[t] & (1 << i)) { ex.lab_mix(t, i, this.mix[t][i]); this.mixLocked[t] &= ~(1 << i); }
        }
        this.port.postMessage({ type: 'step', t, s: e.s, auto: a && Object.keys(a).length ? a : null });
        break; }
      case 'trig': this.fire(e.t, e.vel, e.lk, e.gate, false); if (e.first) this.port.postMessage({ type: 'hit', t: e.t, vel: e.vel, lk: e.lk }); break;
      case 'manual': this.fire(e.t, e.vel, e.lk, e.gate, true); break;
      case 'pre': ex.lab_pre(e.t, e.len); this.port.postMessage({ type: 'pre', t: e.t, ms: e.len / sampleRate * 1000 }); break;
      case 'mutes': this.applyMute(e.p.mute, e.p.solo); break;
    }
  }
  restoreLfo(t) { const [dest] = this.lfo[t], m = this.model[t]; if (this.lfoLast[t][0] >= 0 && this.ex) this.ex.lab_param(t, m, dest, this.pbase[t][m][dest]); this.lfoLast[t] = [-1, -1]; }
  runLfo(n) {
    for (let t = 0; t < T; t++) {
      const [dest, rate, depth, shape] = this.lfo[t];
      if (!depth) { if (this.lfoLast[t][0] >= 0) this.restoreLfo(t); continue; }
      const hz = 0.05 * Math.pow(2, rate / 127 * 9), ph0 = this.lfoPh[t];
      let ph = ph0 + hz * n / sampleRate; if (ph >= 1) { ph -= Math.floor(ph); this.lfoSH[t] = Math.random() * 2 - 1; }
      this.lfoPh[t] = ph;
      const w = shape === 0 ? Math.sin(ph * 6.2832) : shape === 1 ? 1 - 4 * Math.abs(ph - 0.5) : shape === 2 ? 1 - 2 * ph : shape === 3 ? (ph < 0.5 ? 1 : -1) : this.lfoSH[t];
      const m = this.model[t], v = Math.max(0, Math.min(127, Math.round(this.pbase[t][m][dest] + w * depth / 2)));
      if (v !== this.lfoLast[t][1]) { this.ex.lab_param(t, m, dest, v); this.lfoLast[t] = [dest, v]; }
    }
  }
  renderTo(L, R, o, k) {
    if (k <= 0) return;
    const p = this.ex.lab_render(k), buf = new Float32Array(this.ex.memory.buffer, p, k * 2);
    for (let j = 0; j < k; j++) { L[o + j] = buf[2 * j]; R[o + j] = buf[2 * j + 1]; }
  }
  process(_, outputs) {
    const out = outputs[0], L = out[0], R = out[1] || out[0], n = L.length;
    if (!this.ex) return true;
    const end = this.clock + n;
    if (this.playing) while (this.tickAt < end) {
      const at = this.tickAt + this.dTick(this.tick) + this.dTick(this.tick + 1);
      this.decide(this.tick + 2, at); this.tickAt += this.dTick(this.tick); this.tick++;
    }
    this.runLfo(n);
    const due = [], keep = [];
    for (const e of this.ev) (e.at < end ? due : keep).push(e);
    this.ev = keep; due.sort((a, b) => a.at - b.at);
    let o = 0;
    for (const e of due) {
      const at = Math.min(n, Math.max(o, Math.round(e.at - this.clock)));
      this.renderTo(L, R, o, at - o); o = at; this.runEvent(e);
    }
    this.renderTo(L, R, o, n - o);
    this.clock = end;
    if (++this.blocks % 4 === 0) {
      const lv = new Int32Array(this.ex.memory.buffer, this.ex.lab_levels(), T), rows = [];
      for (let t = 4; t < 8; t++) rows.push(this.ex.lab_row(t));
      this.port.postMessage({ type: 'levels', lv: Array.from(lv, x => x / 32768), rows });
    }
    return true;
  }
}
registerProcessor('caja', Caja);
