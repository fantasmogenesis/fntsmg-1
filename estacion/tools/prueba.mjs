// Prueba de la capa de datos sin navegador: el cliente contra el FM-1 de demostración, por el protocolo completo.
// Uso: node estacion/tools/prueba.mjs
import assert from 'node:assert/strict';
import { Cliente, OBJ, PAG, pack7, unpack7, crc32 } from '../js/proto.js';
import { TransporteDemo } from '../js/midi.js';
import { AparatoDemo, ejemplo } from '../js/aparato-demo.js';
import { pack, unpack, registro, leerRegistro, pasoVacio, F } from '../js/proyecto.js';
import { codificar, decodificar } from '../js/adpcm.js';

let n = 0;
const ok = (c, m) => { assert.ok(c, m); n++; };

/* empaque 7 en 8 y CRC */
const rnd = Uint8Array.from({ length: 512 }, () => Math.random() * 256 | 0);
ok(pack7(rnd).length === 586, '512 empacados = 586');
ok(unpack7(pack7(rnd)).every((v, i) => v === rnd[i]), 'pack7 ida y vuelta');
ok(crc32(new TextEncoder().encode('123456789')) === 0xCBF43926, 'crc32 de zlib');

/* registro de paso */
const s = pasoVacio(); s.v = 2; s.c = 3; s.p = 40; s.r = 3; s.u = -5; s.lk[0] = 70; s.lk[9] = 10; s.au = [[1, 90], [6, 3]];
const [s2] = leerRegistro(registro(s), 0);
ok(JSON.stringify({ ...s2, lk: [...s2.lk] }) === JSON.stringify({ ...s, lk: [...s.lk] }), 'registro ida y vuelta');

/* proyecto */
const pr = ejemplo(), b = pack(pr);
ok(b[4] === 7 && b.length > 2600 && b.length < 4000, 'tamaño del proyecto ' + b.length);
ok(pack(unpack(b)).every((v, i) => v === b[i]), 'proyecto ida y vuelta');

/* ADPCM */
const x = Int16Array.from({ length: 1000 }, (_, i) => Math.round(Math.sin(i / 7) * 20000));
const a = codificar(x);
ok(a.length === Math.ceil(1000 / 64) * 36, 'ADPCM: 36 bytes por bloque');
const y = decodificar(a, 1000), err = x.reduce((m, v, i) => i < 128 ? m : Math.max(m, Math.abs(v - y[i])), 0);
ok(err < 3000, 'ADPCM decodifica cerca: ' + err);

/* el protocolo contra el aparato de demostración */
const ap = new AparatoDemo(); await ap.iniciar();
const tr = new TransporteDemo(ap), c = new Cliente(tr); await tr.conectar();
const info = await c.info();
ok(info.protocolo === 1 && info.pasos === 32 && info.trozo === 512 && /0\.9/.test(info.version), 'INFO ' + info.version);
ok((await c.meta(0)).length === 8, 'META pistas');
ok((await c.meta(1, 4)).at(-1) === 'SAMPLE', 'META modelos v7');
const per = await c.meta(2, 4, PAG.EDIT, 0); ok(per[0].nombre && per.length === 4, 'META perillas ' + per.map(p => p.nombre));
ok((await c.meta(3, 5, PAG.GLO, 0, 0, 120)) === '120 BPM', 'META valor');
const smk = await c.meta(5, 0); ok(smk[0].lugar === 0 && smk[0].nombre === 'TECHNO', 'META samples con lugar');
const smc = await c.meta(5, 1); ok(smc[0].lugar === 24, 'META samples: CAJAS empiezan en 24');
ok((await c.meta(6, 3, 4)).length === 7, 'META destinos de LFO con pista');
const pe = await c.meta(2, 0, PAG.EDIT, 1); ok(pe[3].nombre === 'AZAR', 'EDIT 2.ª página, 4.ª perilla = AZAR');
await c.perilla(0, PAG.EDIT, 0, 4, 2);
const ps = await c.meta(2, 0, PAG.EDIT, 0); ok(ps[0].nombre === 'AFINA' && ps[0].min === 40 && ps[0].max === 88 && ps[1].nombre === 'SAMPLE', 'SAMPLE rev. 2: AFINA 40..88, luego SAMPLE');
ok((await c.perilla(0, PAG.EDIT, 0, 1, 20)) === 9, 'SAMPLE en lugar vacío baja al más cercano con sample');
ok((await c.perilla(0, PAG.GLO, 0, 0, 999)) === 200, 'PERILLA acota');
ok((await c.perillas(0, PAG.GLO, 0))[0] === 200, 'PERILLAS');
await c.paso(0, 2, 5, registro(s));
const pi = await c.leerPista(0, 2);
ok(pi.bytes.at(-1) === 0xFF && pi.golpes === 0 && pi.rotar === 0 && pi.bytes[0] >= 1, 'PISTA leer: euclidiano primero, pista empacada al final');
const pb = await c.leerObjeto(OBJ.PROYECTO, 0);
const lp = unpack(pb); ok(lp.pats[0][2].st[5].r === 3 && lp.song[F.bpm] === 200, 'traslado: proyecto leído con el paso nuevo');
await c.escribirObjeto(OBJ.RANURA, 5, pb);
ok((await c.lista(OBJ.RANURA))[5].largo === pb.length, 'ranura escrita');
await c.transporte(0, 1);
await assert.rejects(c.escribirObjeto(OBJ.RANURA, 6, pb), e => e.codigo === 'TOCANDO'); n++;
await c.transporte(0, 0);
await c.escribirObjeto(OBJ.SAMPLE, 0, a, { nombre: 'SENO', extra: [0xAC, 0x58, 0x02, 0x68, 0x07, 0, 60, 0, 0, 0, 0, 0, 0, 0] });
const ls = await c.lista(OBJ.SAMPLE); ok(ls.samples[0].nombre === 'SENO' && ls.libre < 136 * 1024, 'sample escrito y listado');
ok((await c.meta(5, 3))[0].lugar === 96, 'TUYOS 1 es el lugar 96');
await assert.rejects(c.leerObjeto(OBJ.RANURA, 9), e => e.codigo === 'VACIO'); n++;
const big = new Uint8Array(140 * 1024);
await assert.rejects(c.escribirObjeto(OBJ.SAMPLE, 1, big, { nombre: 'X', extra: new Array(14).fill(0) }), e => e.codigo === 'NO_CABE'); n++;
ap.detener();
console.log('bien:', n, 'pruebas');
process.exit(0);
