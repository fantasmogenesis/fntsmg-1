// La tabla local de nombres de la Estación, sacada de las tablas del firmware (motores/src). La app la usa sin aparato
// y mientras el META del aparato no llega; con el aparato, los nombres de META mandan. Cuando exista fg_meta, esto
// lo lee de ahí. Uso: node estacion/tools/gen-meta.mjs [carpeta del firmware]  ->  estacion/js/meta-datos.js
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const AQUI = join(dirname(fileURLToPath(import.meta.url)), '..');
const R = process.argv[2] || join(AQUI, '..');  // de dónde se leen las tablas; se escribe siempre en esta estacion/
const box = readFileSync(join(R, 'motores/src/fg_box.c'), 'utf8');
const pre = readFileSync(join(R, 'motores/src/fg_presets.h'), 'utf8');
const smp = readFileSync(join(R, 'motores/src/fg_samples.c'), 'utf8');

/* un inicializador de C ({…} con cadenas y números) a JSON */
function llaves(src, name) {
  const at = src.search(new RegExp(`\\b${name}\\b[^=;]*=\\s*\\{`));
  if (at < 0) throw new Error('no está ' + name);
  let i = src.indexOf('{', at), d = 0, j = i;
  for (; j < src.length; j++) { const c = src[j]; if (c === '"') { j = src.indexOf('"', j + 1); continue; } if (c === '{') d++; else if (c === '}' && --d === 0) break; }
  return src.slice(i, j + 1).replace(/\/\*[\s\S]*?\*\//g, '');
}
function init(src, name) {
  const body = llaves(src, name).replace(/\{/g, '[').replace(/\}/g, ']');
  /* los identificadores (máscaras de C) no hacen falta aquí: valen 0; de las cadenas solo se quita el \1 (no se traduce) */
  const json = body.split(/("(?:[^"\\]|\\.)*")/).map((s, k) => k % 2 ? s.replace(/\\1/g, '') : s.replace(/[A-Za-z_]\w*/g, '0').replace(/(\d+)u\b/g, '$1')).join('');
  return JSON.parse(json.replace(/,\s*\]/g, ']'));
}

/* una tabla de números con nombres de C (CAND, ORD_Y…): cada nombre vale lo que dice su enum y CYN es su #define */
const cand = readFileSync(join(R, 'motores/src/fg_cand.c'), 'utf8');
const enumDe = (src, primero) => src.match(new RegExp(`enum\\s*\\{([^}]*\\b${primero}\\b[^}]*)\\}`))[1].split(',').map(x => x.trim()).filter(Boolean);
const VAL = { CN_OLD: 255, CN_PLACA: 254 };
enumDe(readFileSync(join(R, 'motores/src/fg_cand.h'), 'utf8'), 'FGC_IMPACTO').forEach((n, i) => VAL[n] = i);
enumDe(readFileSync(join(R, 'motores/src/fg_synth.h'), 'utf8'), 'FGY_SH').forEach((n, i) => VAL[n] = i);
const CYN = box.match(/#define\s+CYN\s+(\{[^\n]*\})/)[1];
function numeros(src, name) {
  const body = llaves(src, name).replace(/\bCYN\b/g, CYN).replace(/[A-Za-z_]\w*/g, n => { if (!(n in VAL)) throw new Error(name + ': no sé cuánto vale ' + n); return VAL[n]; });
  return JSON.parse(body.replace(/\{/g, '[').replace(/\}/g, ']').replace(/,\s*\]/g, ']'));
}
/* v9: los motores por lugar. Uno que no está en el orden de ALGO ya salió: su nombre queda vacío (como en META 1) */
const NSTORE = numeros(box, 'NSTORE'), CAND = numeros(box, 'CAND'), ORD_D = numeros(box, 'ORD_D'), NORD_D = numeros(box, 'NORD_D'), ORD_Y = numeros(box, 'ORD_Y');
const MN = init(box, 'MNAME'), YN = init(box, 'YNAME'), PD = init(box, 'PLAB_D'), PY = init(box, 'PLAB_Y'), LAB = init(cand, 'FGC_LAB');
const orden = Array.from({ length: 8 }, (_, t) => t < 4 ? ORD_D[t].slice(0, NORD_D[t]) : ORD_Y);
const lugares = f => Array.from({ length: 8 }, (_, t) => Array.from({ length: NSTORE[t] }, (_, m) => f(t, m)));
const modelos = lugares((t, m) => orden[t].includes(m) ? (t < 4 ? MN[t][m] : YN[m]) : '');
const perillas = lugares((t, m) => { const c = CAND[t][m] ?? 255; return c === 254 ? PD[2][1] : c !== 255 ? LAB[c].slice(0, 7) : t >= 4 ? PY[m] : PD[t][m]; });

const presets = n => init(pre, n).map(([nombre, p]) => ({ nombre, p }));
const samples = n => [...smp.match(new RegExp(`${n}\\[\\] = \\{(.*)\\};`))[1].matchAll(/"([^"]*)"\}/g)].map(m => m[1]).slice(0, +(smp.match(new RegExp(`${n}_N = (\\d+)`)) || [0, 999])[1]);   /* un banco vacío trae un relleno: cuenta _N */

const M = {
  pistas: init(box, 'TN'),
  modelos, orden, perillas,
  mezcla: init(box, 'MIXL'),
  fx: init(box, 'FXL'),
  fxDef: init(box, 'FXDEF'),
  fxPagina: init(box, 'FXPAGE'),
  condiciones: init(box, 'CONDS'),
  formas: init(box, 'SHAPES'),
  escalas: init(box, 'SCALEN'),
  performance: init(box, 'PERF').map(x => x[0]),
  presetsPerc: [['PR_BARRIDO', 'PR_MEMB', 'PR_KMUESTRA'], ['PR_CAJA', 'PR_APLAUSO', 'PR_SMUESTRA'], ['PR_GRANIZO', 'PR_PLACA'], ['PR_GOTA', 'PR_CHATARRA']].map(r => r.map(presets)),
  presetsSinte: ['PR_SH', 'PR_ACIDO', 'PR_FM', 'PR_CUERDAS', 'PR_TEXTURA', 'PR_DRON', 'PR_DUB', 'PR_RESON'].map(presets),
  samples: [samples('FG_SMP_KICK'), samples('FG_SMP_SNR'), samples('FG_SMP_TEX')],
};
const out = `// Generado por estacion/tools/gen-meta.mjs desde motores/src (fg_box.c, fg_cand.c, fg_presets.h, fg_samples.c): no editar a mano.
export default ${JSON.stringify(M)};
`;
writeFileSync(join(AQUI, 'js/meta-datos.js'), out);
console.log('meta-datos.js:', out.length, 'B');
