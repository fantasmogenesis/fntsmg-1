// Prueba de igualdad: corre caja/worklet.js (con los motores simulados) y fg_seq compilado a WASM con los mismos
// patrones y los mismos comandos, y compara la lista de eventos muestra por muestra. El azar usa la misma semilla
// en los dos lados (xorshift32 de fg_rand), así que también tiene que coincidir. El LFO se compara aparte con
// tolerancia: el JS usa float y Math.sin; el C, fase de 32 bits y tabla.
//   node test/seq_compare.mjs            (después de ./build.sh)
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const WORKLET = path.join(HERE, '../../caja/worklet.js');
const WASM = path.join(HERE, 'seq.wasm');
const SR = 44118, SEED = 0x2545F491;

const xorshift = s => () => { let x = s; x = (x ^ (x << 13)) >>> 0; x = (x ^ (x >>> 17)) >>> 0; x = (x ^ (x << 5)) >>> 0; s = x; return x; };

// ---------- patrones de prueba (estructura del worklet) ----------
function makePats(seed) {
  const R = xorshift(seed), u = () => R() / 4294967296, ri = (a, b) => a + Math.floor(u() * (b - a + 1));
  const pats = [];
  for (let p = 0; p < 16; p++) {
    const pat = [];
    for (let t = 0; t < 8; t++) {
      const tr = { steps: new Array(32).fill(0), locks: Array.from({ length: 32 }, () => ({})), auto: Array.from({ length: 32 }, () => ({})), props: Array.from({ length: 32 }, () => ({})), len: 16, spd: 1 };
      if (p < 4) {
        tr.len = u() < .5 ? 16 : ri(1, 32); tr.spd = u() < .6 ? 1 : ri(0, 2);
        for (let s = 0; s < 32; s++) {
          if (u() < .45) tr.steps[s] = u() < .25 ? 2 : 1;
          if (u() < .15) tr.props[s].c = ri(1, 11);
          if (u() < .15) tr.props[s].p = ri(0, 99);
          if (u() < .12) tr.props[s].r = ri(2, 4);
          if (u() < .2) { const v = ri(-8, 7); if (v) tr.props[s].u = v; }
          if (u() < .3) for (let k = ri(1, 3); k > 0; k--) { const i = ri(0, 11); tr.locks[s][i] = t >= 4 && i === 0 ? 64 + ri(-14, 14) : ri(0, 127); }
          if (t < 6 && u() < .08) tr.auto[s][ri(0, 6)] = ri(0, 127);
        }
      }
      pat.push(tr);
    }
    pats.push(pat);
  }
  return pats;
}

// ---------- comandos ----------
const M = a => a.map(Boolean);
function scenario(model) {
  const c = [];
  const at = (b, cmd) => c.push([b, cmd]);
  at(0, { type: 'play', on: true });
  at(40, { type: 'azar', t: 0, v: 60 }); at(41, { type: 'azar', t: 5, v: 127 }); at(42, { type: 'azar', t: 7, v: 33 });
  at(100, { type: 'fill', on: true }); at(170, { type: 'fill', on: false });
  at(200, { type: 'queue', p: 1 });
  at(330, { type: 'chain', list: [2, 3, 1] });
  at(500, { type: 'perf', flags: 0, seq: { rep4: 1 } }); at(545, { type: 'perf', flags: 0, seq: {} });
  at(600, { type: 'muteq', on: true });
  at(610, { type: 'mute', mute: M([0, 1, 0, 0, 1, 0, 0, 0]), solo: M([0, 0, 0, 0, 0, 0, 0, 0]) });
  at(700, { type: 'mute', mute: M([0, 0, 0, 0, 0, 0, 0, 0]), solo: M([0, 0, 1, 0, 0, 0, 0, 0]), now: true });
  at(760, { type: 'mute', mute: M([0, 0, 0, 0, 0, 0, 0, 0]), solo: M([0, 0, 0, 0, 0, 0, 0, 0]), now: true });
  at(800, { type: 'bpm', bpm: 97 });
  at(900, { type: 'swing', swing: 37 });
  at(1000, { type: 'transp', v: 5 });
  at(1100, { type: 'param', t: 4, m: model[4], i: 0, v: 80 });
  at(1150, { type: 'mix', t: 2, i: 0, v: 70 });
  at(1200, { type: 'lfo', t: 6, v: [2, 90, 60, 1] });
  at(1250, { type: 'lfo', t: 7, v: [3, 127, 127, 0] });
  at(1300, { type: 'model', t: 6, m: 2 });
  at(1340, { type: 'lfo', t: 6, v: [2, 50, 90, 2] });
  at(1400, { type: 'trig', t: 3, vel: 110, locks: { 0: 30, 2: 99 } });
  at(1410, { type: 'trig', t: 5, vel: 90, locks: { 0: 70 } });
  at(1500, { type: 'perf', flags: 0, seq: { rep8: 1 } }); at(1560, { type: 'perf', flags: 0, seq: { soloperc: 1, nokick: 1 } }); at(1600, { type: 'perf', flags: 0, seq: {} });
  at(1650, { type: 'lfo', t: 7, v: [3, 127, 0, 0] });
  at(1700, { type: 'play', on: false }); at(1712, { type: 'play', on: true });
  at(1800, { type: 'bpm', bpm: 163 }); at(1801, { type: 'swing', swing: 75 });
  at(1900, { type: 'muteq', on: false });
  at(2000, { type: 'queue', p: 0 });
  at(2100, { type: 'transp', v: -12 });
  at(2200, { type: 'bpm', bpm: 60 }); at(2201, { type: 'swing', swing: 0 });
  at(2600, { type: 'chain', list: [0, 1] });
  at(3000, { type: 'bpm', bpm: 200 });
  return c;
}

// ---------- lado JS: el worklet real con motores simulados ----------
function runJS(pats, cmds, blocks, ahead) {
  // el S&H del LFO saca de otro generador (como en fg_seq): el worklet llama Math.random() en cada vuelta del LFO,
  // aunque la forma no sea AZAR, y eso movería el azar de los pasos según en qué bloque cae la vuelta
  const rnd = xorshift(SEED), rndLfo = xorshift(1);
  let Cls = null, cursor = 0, inLfo = false;
  const rec = [], lfo = [];
  const mem = new ArrayBuffer(1 << 16), LK = 4096;
  const post = m => {
    if (m.type === 'step') rec.push(`${cursor} STEP t${m.t} s${m.s}`);
    else if (m.type === 'pat') rec.push(`${cursor} PAT ${m.cur} ${m.queued < 0 ? 255 : m.queued} ${m.chainPos}`);
    else if (m.type === 'muted') rec.push(`${cursor} MUTE ${mask(m.mute)} ${mask(m.solo)}`);
  };
  const ctx = vm.createContext({ AudioWorkletProcessor: class { constructor() { this.port = { postMessage: post }; } }, registerProcessor: (n, c) => { Cls = c; }, sampleRate: SR, __rnd: () => (inLfo ? rndLfo() : rnd()) / 4294967296 });
  vm.runInContext(fs.readFileSync(WORKLET, 'utf8') + '\nMath.random = __rnd;', ctx);
  const w = new Cls();
  w.ex = {
    memory: { buffer: mem },
    lab_locks: t => LK + t * 8, lab_ahead: t => (t === 1 ? ahead : 0), lab_levels: () => 2048, lab_row: () => -1,
    lab_trig(t, vel, m, gate) { const b = new Uint8Array(mem, LK + t * 8, 8); rec.push(`${cursor} TRIG t${t} v${vel} g${gate} ${locks(m, b)}`); },
    lab_pre(t, len) { rec.push(`${cursor} PRE t${t} ${len}`); },
    lab_param(t, m, i, v) { if (inLfo) lfo.push([cursor, t, m, i, v]); else rec.push(`${cursor} PARAM t${t} m${m} i${i} v${v}`); },
    lab_mix(t, i, v) { rec.push(`${cursor} MIX t${t} i${i} v${v}`); },
    lab_render(k) { cursor += k; return 0; },
    lab_mute() {}, lab_tempo() {}, lab_perf() {}, lab_model() {},
  };
  const runLfo = w.runLfo.bind(w);
  w.runLfo = n => { inLfo = true; runLfo(n); inLfo = false; };
  w.pats = structuredClone(pats);
  const L = new Float32Array(256), Rr = new Float32Array(256);
  let ci = 0;
  for (let b = 0; b < blocks.length; b++) {
    cursor = w.clock;
    while (ci < cmds.length && cmds[ci][0] === b) w.msg(structuredClone(cmds[ci++][1]));
    cursor = w.clock;
    w.process([], [[L.subarray(0, blocks[b]), Rr.subarray(0, blocks[b])]]);
  }
  return { rec, lfo };
}

// ---------- lado C ----------
async function runC(pats, cmds, blocks, ahead) {
  const NOOP = new Proxy({}, { get: () => () => 0 });
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(WASM), new Proxy({}, { get: () => NOOP }));
  const ex = instance.exports;
  if (ex._initialize) ex._initialize();
  ex.t_init(SR, SEED);
  pats.forEach((pat, p) => pat.forEach((tr, t) => {
    ex.t_track(p, t, tr.len, tr.spd);
    for (let s = 0; s < 32; s++) {
      const pr = tr.props[s];
      ex.t_step(p, t, s, tr.steps[s], pr.c || 0, pr.p ?? 100, pr.r || 1, pr.u || 0);
      for (const i in tr.locks[s]) ex.t_lock(p, t, s, +i, tr.locks[s][i]);
      for (const i in tr.auto[s]) ex.t_auto(p, t, s, +i, tr.auto[s][i]);
    }
  }));
  ex.t_ahead(1, ahead);
  const buf = () => new Uint8Array(ex.memory.buffer, ex.t_buf(), 64);
  const rec = [], lfo = [];
  let ci = 0;
  for (let b = 0; b < blocks.length; b++) {
    const clock = ex.t_clock() >>> 0;
    while (ci < cmds.length && cmds[ci][0] === b) {
      const d = cmds[ci++][1];
      switch (d.type) {
        case 'play': ex.t_play(d.on ? 1 : 0); break;
        case 'fill': ex.t_fill(d.on ? 1 : 0); break;
        case 'bpm': ex.t_bpm(d.bpm); break;
        case 'swing': ex.t_swing(d.swing); break;
        case 'transp': ex.t_transp(d.v); break;
        case 'azar': ex.t_azar(d.t, d.v); break;
        case 'queue': ex.t_queue(d.p); break;
        case 'chain': buf().set(d.list); ex.t_chain(d.list.length); break;
        case 'muteq': ex.t_muteq(d.on ? 1 : 0); break;
        case 'mute': ex.t_mutes(mask(d.mute), mask(d.solo), d.now ? 1 : 0); break;
        case 'perf': ex.t_perf((d.seq.rep4 ? 1 : 0) | (d.seq.rep8 ? 2 : 0) | (d.seq.soloperc ? 4 : 0) | (d.seq.nokick ? 8 : 0)); break;
        case 'param': rec.push(`${clock} PARAM t${d.t} m${d.m} i${d.i} v${d.v}`); ex.t_param(d.t, d.m, d.i, d.v); break;
        case 'mix': rec.push(`${clock} MIX t${d.t} i${d.i} v${d.v}`); ex.t_mix(d.t, d.i, d.v); break;
        case 'lfo': buf().set(d.v); ex.t_lfo(d.t); break;
        case 'model': ex.t_model(d.t, d.m); break;
        case 'trig': { const b8 = buf(); b8.fill(255, 0, 8); for (const i in d.locks) b8[+i] = d.locks[i]; ex.t_audition(d.t, d.vel); break; }
      }
    }
    const n = ex.t_process(blocks[b]), r = new Int32Array(ex.memory.buffer, ex.t_rec(), n * 20);
    for (let k = 0; k < n; k++) {
      const e = r.subarray(k * 20, k * 20 + 20), smp = clock + e[0];
      switch (e[1]) {
        case 0: rec.push(`${smp} TRIG t${e[2]} v${e[8]} g${e[10]} ${locks(e[9], e.subarray(11, 19))}`); break;
        case 1: rec.push(`${smp} PRE t${e[2]} ${e[10]}`); break;
        case 2: if (e[7] & 4) lfo.push([smp, e[2], e[4], e[5], e[6]]); else rec.push(`${smp} PARAM t${e[2]} m${e[4]} i${e[5]} v${e[6]}`); break;
        case 3: rec.push(`${smp} MIX t${e[2]} i${e[5]} v${e[6]}`); break;
        case 4: rec.push(`${smp} MUTE ${e[4]} ${e[5]}`); break;
        case 5: rec.push(`${smp} STEP t${e[2]} s${e[3]}`); break;
        case 6: rec.push(`${smp} PAT ${e[4]} ${e[5]} ${e[6]}`); break;
      }
    }
  }
  return { rec, lfo };
}

function mask(a) { return a.reduce((m, v, i) => m | (v ? 1 << i : 0), 0); }
function locks(m, b) { const o = []; for (let i = 0; i < 8; i++) if (m & (1 << i)) o.push(i + ':' + b[i]); return o.join(','); }

// el LFO: valor vigente por pista (t, parámetro) al final de cada bloque
function lfoTrack(lfo, blocks) {
  const ends = []; let acc = 0; for (const n of blocks) ends.push(acc += n);
  const out = []; let k = 0; const cur = {};
  for (const e of ends) { while (k < lfo.length && lfo[k][0] < e) { const [, t, m, i, v] = lfo[k++]; cur[t] = [m, i, v]; } out.push(JSON.stringify(cur)); }
  return out.map(s => JSON.parse(s));
}

async function run(name, blocks, ahead) {
  const pats = makePats(0xC0FFEE);
  const model = [0, 0, 0, 0, 0, 0, 0, 0];
  const cmds = scenario(model);
  const js = runJS(pats, cmds, blocks, ahead), c = await runC(pats, cmds, blocks, ahead);
  let bad = 0;
  const n = Math.max(js.rec.length, c.rec.length);
  for (let i = 0; i < n && bad < 12; i++) if (js.rec[i] !== c.rec[i]) { if (!bad) console.log(`  primera diferencia en el evento ${i}:`); console.log(`    js: ${js.rec[i]}\n    c:  ${c.rec[i]}`); bad++; }
  const kinds = {}; for (const r of js.rec) { const k = r.split(' ')[1]; kinds[k] = (kinds[k] || 0) + 1; }
  const a = lfoTrack(js.lfo, blocks), b = lfoTrack(c.lfo, blocks);
  // en sierra y cuadrada, si la vuelta cae un bloque antes o después, ese bloque salta todo el rango: se cuentan aparte
  let lfoMax = 0, lfoBad = 0, jumps = 0, n2 = 0;
  for (let i = 0; i < a.length; i++) for (const t in a[i]) {
    const x = a[i][t], y = b[i][t]; n2++;
    if (!y || x[0] !== y[0] || x[1] !== y[1]) { lfoBad++; continue; }
    const d = Math.abs(x[2] - y[2]);
    if (d > 2) jumps++; else lfoMax = Math.max(lfoMax, d);
  }
  const ok = !bad && js.rec.length === c.rec.length && !lfoBad && jumps <= n2 * .002;
  console.log(`${ok ? 'OK ' : 'MAL'} ${name}: ${js.rec.length} eventos js / ${c.rec.length} c (${Object.entries(kinds).map(([k, v]) => k + ' ' + v).join(', ')})`);
  console.log(`    LFO: ${js.lfo.length} cambios js / ${c.lfo.length} c; diferencia máx ${lfoMax}, ${jumps} de ${n2} bloques con la vuelta corrida un bloque${lfoBad ? ', ' + lfoBad + ' sin par' : ''}`);
  return ok;
}

const fixed = new Array(20000).fill(128);
const R = xorshift(77), varied = Array.from({ length: 20000 }, () => 16 + (R() % 241));
const r1 = await run('bloques de 128, sin anticipación', fixed, 0);
const r2 = await run('bloques de 16–256, caja con anticipación 1', varied, 1);
process.exit(r1 && r2 ? 0 : 1);
