// Prueba de la capa de datos sin navegador: el cliente contra el FM-1 de demostración, por el protocolo completo.
// Uso: node estacion/tools/prueba.mjs
import assert from 'node:assert/strict';
import { Cliente, OBJ, PAG, pack7, unpack7, crc32 } from '../js/proto.js';
import { TransporteDemo } from '../js/midi.js';
import { AparatoDemo, ejemplo } from '../js/aparato-demo.js';
import { pack, unpack, registro, leerRegistro, pasoVacio, F, off } from '../js/proyecto.js';
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
ok(b[4] === 9 && b.length > 3200 && b.length < 5200, 'tamaño del proyecto v9 ' + b.length);
ok(unpack(b).kits[1][4] === 6 && unpack(b).kits[0][4] !== 6, 'v9: cada patrón trae su sonido');
{ const v8 = unpack(b); v8.ver = 8; const b8 = pack(v8), r = unpack(b8);
  ok(b8[4] === 8 && b8[6] + b8[7] * 256 === 6084 && pack(r).every((v, i) => v === b8[i]), 'v8 ida y vuelta');
  const igual = (o, n) => [...Array(n)].every((_, i) => r.song[o + i] === v8.song[o + i]);
  ok(igual(off.p(4, 0, 0), 8 * 11) && igual(F.mix, F.hand - F.mix) && igual(F.has_edited, F.arp2 + 32 - F.has_edited), 'v8 a v9: perillas y campos en su lugar'); }
{ const v7 = unpack(b); v7.ver = 7; const b7 = pack(v7); ok(b7[4] === 7 && pack(unpack(b7)).every((v, i) => v === b7[i]), 'v7 (0.9) ida y vuelta'); }
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
ok(info.protocolo === 1 && info.pasos === 32 && info.trozo === 512 && /0\.10/.test(info.version) && info.formato === 9, 'INFO ' + info.version);
ok((await c.meta(0)).length === 8, 'META pistas');
{ const ms = await c.meta(1, 4); ok(ms.length === 16 && ms[8] === 'SAMPLE' && ms[3] === '' && ms[15] === 'ÓRGANO', 'META modelos por lugar, los que salieron vacíos'); }
ok((await c.perilla(4, PAG.EDIT, 0, 4, 3)) === 11 && (await c.perilla(1, PAG.EDIT, 0, 4, 1)) === 4, 'ALGO: CUERDAS pasa a TABLA y APLAUSO a PALMAS');
await c.perilla(4, PAG.EDIT, 0, 4, 1); await c.perilla(1, PAG.EDIT, 0, 4, 4);
ok((await c.meta(5, 2)).length === 0, 'TEXTURAS vacío');
const per = await c.meta(2, 4, PAG.EDIT, 0); ok(per[0].nombre && per.length === 4, 'META perillas ' + per.map(p => p.nombre));
ok((await c.meta(3, 5, PAG.GLO, 0, 0, 120)) === '120 BPM', 'META valor');
const smk = await c.meta(5, 0); ok(smk[0].lugar === 0 && smk[0].nombre === 'TECHNO', 'META samples con lugar');
const smc = await c.meta(5, 1); ok(smc[0].lugar === 24, 'META samples: SNARES empiezan en 24');
ok((await c.meta(6, 3, 4)).length === 7, 'META destinos de LFO con pista');
const pe = await c.meta(2, 0, PAG.EDIT, 1); ok(pe[3].nombre === 'AZAR', 'EDIT 2.ª página, 4.ª perilla = AZAR');
await c.perilla(0, PAG.EDIT, 0, 4, 2);
const ps = await c.meta(2, 0, PAG.EDIT, 0); ok(ps[0].nombre === 'AFINACIÓN' && ps[0].min === 40 && ps[0].max === 88 && ['BOMBOS', 'SNARES', 'TEXTURAS', 'TUYOS'].includes(ps[1].nombre), 'SAMPLE: AFINACIÓN 40..88, luego la perilla con el nombre del banco');
ok((await c.perilla(0, PAG.EDIT, 0, 1, 20)) === 9, 'SAMPLE en lugar vacío baja al más cercano con sample');
ok((await c.perilla(0, PAG.GLO, 0, 0, 999)) === 200, 'PERILLA acota');
ok((await c.perillas(0, PAG.GLO, 0))[0] === 200, 'PERILLAS');
await c.paso(0, 2, 5, registro(s));
const pi = await c.leerPista(0, 2);
ok(pi.bytes.at(-1) === 0xFF && pi.golpes === 0 && pi.rotar === 0 && pi.bytes[0] >= 1, 'PISTA leer: euclidiano primero, pista empacada al final');
const pb = await c.leerObjeto(OBJ.PROYECTO, 0);
const lp = unpack(pb); ok(lp.pats[0][2].st[5].r === 3 && lp.song[F.bpm] === 200, 'traslado: proyecto leído con el paso nuevo');
/* sonido por patrón (0.10): el patrón 2 tiene el sinte A en DUB */
await c.transporte(1, 1); ok((await c.perillas(4, PAG.EDIT, 0)).length === 4 && unpack(await c.leerObjeto(OBJ.PROYECTO, 0)).song[F.model + 4] === 6, 'al entrar al patrón 2 su sonido pasa a la canción');
await c.transporte(1, 0);
ok((await c.perilla(4, PAG.ARP, 0, 0, 3)) === 3 && unpack(await c.leerObjeto(OBJ.PROYECTO, 0)).song[F.arp_on] >> 4 & 1, 'ARP MODO 3 prende el arpegiador');
ok((await c.perilla(4, PAG.ARP, 0, 0, 0)) === 0 && !(unpack(await c.leerObjeto(OBJ.PROYECTO, 0)).song[F.arp_on] >> 4 & 1), 'ARP MODO NO lo apaga');
ok((await c.meta(2, 4, PAG.ARP, 1))[1].nombre === 'REPETIR', 'ARP página 2');
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
