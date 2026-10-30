// La tabla local de nombres de la Estación, sacada de las tablas del firmware (motores/src). La app la usa sin aparato
// y mientras el META del aparato no llega; con el aparato, los nombres de META mandan. Cuando exista fg_meta, esto
// lo lee de ahí. Uso: node estacion/tools/gen-meta.mjs  ->  estacion/js/meta-datos.js
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const R = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const box = readFileSync(join(R, 'motores/src/fg_box.c'), 'utf8');
const pre = readFileSync(join(R, 'motores/src/fg_presets.h'), 'utf8');
const smp = readFileSync(join(R, 'motores/src/fg_samples.c'), 'utf8');

/* un inicializador de C ({…} con cadenas y números) a JSON */
function init(src, name) {
  const at = src.search(new RegExp(`\\b${name}\\b[^=;]*=\\s*\\{`));
  if (at < 0) throw new Error('no está ' + name);
  let i = src.indexOf('{', at), d = 0, j = i;
  for (; j < src.length; j++) { const c = src[j]; if (c === '"') { j = src.indexOf('"', j + 1); continue; } if (c === '{') d++; else if (c === '}' && --d === 0) break; }
  const body = src.slice(i, j + 1).replace(/\/\*[\s\S]*?\*\//g, '').replace(/\{/g, '[').replace(/\}/g, ']');
  /* los identificadores (máscaras de C) no hacen falta aquí: valen 0; las cadenas no se tocan */
  const json = body.split(/("(?:[^"\\]|\\.)*")/).map((s, k) => k % 2 ? s : s.replace(/[A-Za-z_]\w*/g, '0').replace(/(\d+)u\b/g, '$1')).join('');
  return JSON.parse(json.replace(/,\s*\]/g, ']'));
}

const presets = n => init(pre, n).map(([nombre, p]) => ({ nombre, p }));
const samples = n => [...smp.match(new RegExp(`${n}\\[\\] = \\{(.*)\\};`))[1].matchAll(/"([^"]*)"\}/g)].map(m => m[1]);

const M = {
  pistas: init(box, 'TN'),
  modelosPerc: init(box, 'MNAME'),
  modelosSinte: init(box, 'YNAME'),
  perillasPerc: init(box, 'PLAB_D'),
  perillasSinte: init(box, 'PLAB_Y'),
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
const out = `// Generado por estacion/tools/gen-meta.mjs desde motores/src (fg_box.c, fg_presets.h, fg_samples.c): no editar a mano.
export default ${JSON.stringify(M)};
`;
writeFileSync(join(R, 'estacion/js/meta-datos.js'), out);
console.log('meta-datos.js:', out.length, 'B');
