// El cliente de la Estación contra el fg_box de verdad (el C del firmware, por tools/puente.c), sin aparato.
// Uso: estacion/tools/puente.sh  (compila el puente y corre esto)   o   node prueba-caja.mjs <puente>
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { Cliente, OBJ, PAG } from '../js/proto.js';
import { unpack, pack, registro, pasoVacio, unpackPista, F } from '../js/proyecto.js';
import { codificar } from '../js/adpcm.js';
import { Esc } from '../js/proto.js';

const bin = process.argv[2];
if (!bin) { console.error('falta el puente'); process.exit(2); }
const p = spawn(bin, [], { stdio: ['pipe', 'pipe', 'inherit'] });
const tr = { onMensaje: () => {}, enviar: b => p.stdin.write(Buffer.from(b).toString('hex') + '\n') };
createInterface({ input: p.stdout }).on('line', l => { if (l !== '--' && l) tr.onMensaje(Uint8Array.from(Buffer.from(l, 'hex'))); });
const c = new Cliente(tr);
let n = 0;
const ok = (x, m) => { assert.ok(x, m); n++; console.log('  bien:', m); };

const info = await c.info();
ok(info.protocolo === 1 && info.formato === 7 && info.tamRanura === 8180 && info.trozo === 512, `INFO ${info.version}, ranura ${info.tamRanura}, TUYOS ${info.zona}`);
const pistas = await c.meta(0); ok(pistas.length === 8, 'META pistas: ' + pistas.join(' · '));
const mod = await c.meta(1, 0); ok(mod.includes('SAMPLE'), 'META modelos del bombo: ' + mod.join(' · '));
const per = await c.meta(2, 4, PAG.EDIT, 0); ok(per.length === 4 && per[0].nombre, 'META perillas sinte A: ' + per.map(x => `${x.nombre}[${x.min}..${x.max}]`).join(' '));
ok(typeof await c.meta(3, 0, PAG.GLO, 0, 0, 120) === 'string', 'META valor');
const pre = await c.meta(4, 4, 0); ok(pre.length > 3, `META presets (${pre.length}): ${pre.slice(0, 4).join(' · ')}…`);
const smp = await c.meta(5, 0); ok(smp.length && typeof smp[0].lugar === 'number', `META samples BOMBOS: ${smp.length}, primero ${smp[0].lugar} ${smp[0].nombre}`);
ok((await c.meta(6, 1)).length === 8, 'META escalas');
ok((await c.meta(6, 3, 4)).length >= 1, 'META destinos de LFO con pista');
ok((await c.perilla(0, PAG.ENV, 0, 0, 999)) === 127, 'PERILLA acota');
ok((await c.perillas(0, PAG.ENV, 0)).length === 4, 'PERILLAS');

/* el proyecto real: leerlo, abrirlo con proyecto.js y que vuelva idéntico */
const b = await c.leerObjeto(OBJ.PROYECTO, 0);
const pr = unpack(b);
ok(pack(pr).every((v, i) => v === b[i]) && pack(pr).length === b.length, `proyecto real (${b.length} B) ida y vuelta byte por byte`);
/* dos traslados pedidos a la vez (releer el proyecto mientras Sonido lee el banco): uno espera al otro, sin OCUPADO */
const [d1, d2] = await Promise.all([c.leerObjeto(OBJ.PROYECTO, 0), c.leerObjeto(OBJ.BANCO, 0)]);
ok(d1.length === b.length && d2[0] === 0x83, 'dos traslados a la vez: en fila, sin OCUPADO');
/* la persona gira una perilla en el aparato: llega CAMBIO (no propio) con el bit 0 */
let cam = null; const offc = c.onAviso((cmd, d, propio) => { if (cmd === 0x12 && !propio) cam = d; });
await c.escucha(2); p.stdin.write('k 3 4\n'); await new Promise(r => setTimeout(r, 400)); offc();
ok(cam && cam.que & 1 && cam.contador > 0, `CAMBIO al girar una perilla en el aparato: contador ${cam?.contador}, qué ${cam?.que}`);
/* HOLD = SOSTENER de cada sinte (ARP, perilla 4), también con el ARP apagado */
ok((await c.perilla(4, PAG.ARP, 0, 3, 1)) === 1 && (await c.perillas(4, PAG.ARP, 0))[3] === 1, 'HOLD: SOSTENER del sinte A prendido');
ok((await c.meta(2, 4, PAG.ARP, 0))[3].nombre === 'SOSTENER', 'META: la perilla se llama SOSTENER');
await c.perilla(4, PAG.ARP, 0, 3, 0);
/* PASO con locks y automatización, leído de vuelta con PISTA */
const s = pasoVacio(); s.v = 2; s.p = 60; s.r = 3; s.u = -2; s.lk[1] = 90; s.lk[9] = 20; s.au = [[2, 70]];
await c.paso(3, 4, 5, registro(s));
const pi = await c.leerPista(3, 4), [tk] = unpackPista(pi.bytes, 0), x = tk.st[5];
ok(x.v === 2 && x.p === 60 && x.r === 3 && x.u === -2 && x.lk[1] === 90 && x.lk[9] === 20 && x.au[0]?.[1] === 70, 'PASO y PISTA: locks, probabilidad, ratchet, micro y automatización');
/* escribir el proyecto cambiado y releerlo */
pr.song[F.bpm] = 97; pr.pats[2][1].st[3].v = 1;
await c.escribirObjeto(OBJ.PROYECTO, 0, pack(pr));
const pr2 = unpack(await c.leerObjeto(OBJ.PROYECTO, 0));
ok(pr2.song[F.bpm] === 97 && pr2.pats[2][1].st[3].v === 1, 'proyecto escrito por la app');
/* ranuras */
await c.escribirObjeto(OBJ.RANURA, 6, pack(pr2));
const lr = await c.lista(OBJ.RANURA); ok(lr.length === 17 && lr[6].largo > 0, 'ranura 6 escrita, LISTA de 17');
/* banco */
const bk = await c.leerObjeto(OBJ.BANCO, 0); ok(bk[0] === 0x83, `banco v3 (${bk[1]} sonidos)`);
const nb = Uint8Array.from([0x83, 1, 4, 1, ...new TextEncoder().encode('PRUEBA'), 0, 0, 0, 0, 0, 40, 30, 110, 90, 60, 100, 40, 64]);
await c.escribirObjeto(OBJ.BANCO, 0, nb);
ok((await c.lista(OBJ.BANCO))[0] === 'PRUEBA', 'banco escrito y listado');
await c.nombre(0, 'OTRO'); ok((await c.lista(OBJ.BANCO))[0] === 'OTRO', 'NOMBRE');
/* un sample codificado aquí */
const y = Int16Array.from({ length: 6400 }, (_, i) => Math.round(Math.exp(-i / 900) * 20000 * Math.sin(i / 9)));
const a = codificar(y);
await c.escribirObjeto(OBJ.SAMPLE, 2, a, { nombre: 'APP', extra: new Esc().u21(44100).u21(y.length).u7(60).u7(0).u21(0).u21(0).b });
const ls = await c.lista(OBJ.SAMPLE), e = ls.samples.find(z => z.indice === 2);
ok(e && e.nombre === 'APP' && e.muestras === 6400 && e.bytes === a.length, `sample: LISTA ${e?.nombre} ${e?.muestras} muestras, libre ${ls.libre}`);
const tu = await c.meta(5, 3); ok(tu.some(z => z.lugar === 98 && z.nombre === 'APP'), 'META TUYOS: lugar 98');
const back = await c.leerObjeto(OBJ.SAMPLE, 2); ok(back.every((v, i) => v === a[i]), 'el sample vuelve igual');
/* transporte y ajuste */
await c.transporte(0, 1);
/* el espejo: con ESCUCHA, ESTADO trae al final el paso de cada pista y «tocando» (74 bytes de F0 a F7) */
let est = null; const off = c.onAviso((cmd, d) => { if (cmd === 2) est = d; });
await c.escucha(1); p.stdin.write('t 400\n'); await new Promise(r => setTimeout(r, 300)); off();
ok(est && est.length + 6 === 74 && est[67] === 1 && [...est.subarray(59, 67)].some(v => v !== 127), `ESTADO de ${est && est.length + 6} bytes, tocando ${est?.[67]}, pasos ${est && [...est.subarray(59, 67)].join(' ')}`);
await assert.rejects(c.escribirObjeto(OBJ.RANURA, 7, pack(pr2)), err => err.codigo === 'TOCANDO'); n++; console.log('  bien: escribir tocando = TOCANDO');
await c.transporte(0, 0);
ok((await c.ajuste(0, 2)) === 2, 'AJUSTE idioma português');
ok((await c.meta(0)).join() !== pistas.join(), 'META pistas en portugués: ' + (await c.meta(0)).join(' · '));
await c.ajuste(0, 0);
await assert.rejects(c.leerObjeto(OBJ.RANURA, 12), err => err.codigo === 'VACIO'); n++; console.log('  bien: ranura vacía = VACIO');
console.log(`todo bien: ${n} pruebas contra el fg_box real`);
p.stdin.end(); process.exit(0);
