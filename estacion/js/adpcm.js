// ADPCM IMA por bloques de 64 muestras, el mismo formato de firmware/tools/gen_samples.py y motores/src/fg_smp.c:
// cada bloque son 36 bytes, 4 de cabecera (la muestra anterior en 16 bits con signo, el índice del paso y un relleno)
// y 32 de nibbles (el primero en los 4 bits bajos). La cabecera deja empezar o tocar al revés desde cualquier bloque.

const STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
  143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282,
  1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
  9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767];
const IDX = [-1, -1, -1, -1, 2, 4, 6, 8];
export const BLK = 64, BLK_BYTES = 36;
export const bytesPara = muestras => Math.ceil(muestras / BLK) * BLK_BYTES;

/* x: Int16Array (o números en rango int16) -> Uint8Array */
export function codificar(x) {
  const nb = Math.ceil(x.length / BLK), out = new Uint8Array(nb * BLK_BYTES);
  let prev = 0, idx = 0, o = 0;
  for (let b = 0; b < nb; b++) {
    out[o] = prev & 255; out[o + 1] = (prev >> 8) & 255; out[o + 2] = idx; out[o + 3] = 0;
    let pred = prev;
    for (let k = 0; k < BLK; k++) {
      const s = x[b * BLK + k] ?? 0;
      let st = STEP[idx], d = s - pred, code = 0;
      if (d < 0) { code = 8; d = -d; }
      if (d >= st) { code |= 4; d -= st; }
      if (d >= st >> 1) { code |= 2; d -= st >> 1; }
      if (d >= st >> 2) code |= 1;
      let dq = st >> 3;
      if (code & 4) dq += st;
      if (code & 2) dq += st >> 1;
      if (code & 1) dq += st >> 2;
      pred = Math.max(-32768, Math.min(32767, code & 8 ? pred - dq : pred + dq));
      idx = Math.max(0, Math.min(88, idx + IDX[code & 7]));
      out[o + 4 + (k >> 1)] |= code << ((k & 1) * 4);
    }
    prev = pred; o += BLK_BYTES;
  }
  return out;
}

/* para oír y dibujar lo que va a sonar en el aparato */
export function decodificar(a, n = (a.length / BLK_BYTES) * BLK) {
  const y = new Int16Array(n);
  let pred = 0, idx = 0;
  for (let i = 0; i < n; i++) {
    const blk = (i / BLK | 0) * BLK_BYTES;
    if ((i & 63) === 0) { pred = (a[blk] | a[blk + 1] << 8) << 16 >> 16; idx = a[blk + 2]; }
    const code = (a[blk + 4 + ((i & 63) >> 1)] >> ((i & 1) * 4)) & 15, st = STEP[idx];
    let dq = st >> 3;
    if (code & 4) dq += st;
    if (code & 2) dq += st >> 1;
    if (code & 1) dq += st >> 2;
    pred = Math.max(-32768, Math.min(32767, code & 8 ? pred - dq : pred + dq));
    idx = Math.max(0, Math.min(88, idx + IDX[code & 7]));
    y[i] = pred;
  }
  return y;
}
