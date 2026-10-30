// Lo que la Estación guarda en esta computadora (IndexedDB): el FM-1 de demostración, los respaldos y una copia de
// cada sample mandado al aparato (para devolverlos después de reinstalar). Si el navegador no deja guardar (ventana
// privada, almacenamiento bloqueado), todo sigue funcionando en la memoria y la interfaz lo dice.

let db = null, falla = false;
const mem = new Map();

function abrir() {
  if (db || falla) return Promise.resolve(db);
  return new Promise(ok => {
    try {
      const r = indexedDB.open('fntsmg-estacion', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => { db = r.result; ok(db); };
      r.onerror = r.onblocked = () => { falla = true; ok(null); };
    } catch (e) { falla = true; ok(null); }
  });
}
function tx(modo, fn) {
  return abrir().then(d => d ? new Promise((ok, mal) => {
    const t = d.transaction('kv', modo), s = t.objectStore('kv'), r = fn(s);
    t.oncomplete = () => ok(r?.result); t.onerror = () => mal(t.error);
  }) : null);
}

export const guardaEnMemoria = () => abrir().then(() => falla);
export async function leer(k) { try { const v = await tx('readonly', s => s.get(k)); return v ?? mem.get(k); } catch (e) { return mem.get(k); } }
export async function escribir(k, v) { mem.set(k, v); try { await tx('readwrite', s => s.put(v, k)); } catch (e) { falla = true; } }
export async function borrar(k) { mem.delete(k); try { await tx('readwrite', s => s.delete(k)); } catch (e) {} }
export async function claves(prefijo = '') {
  try { const k = await tx('readonly', s => s.getAllKeys()); if (k) return k.filter(x => String(x).startsWith(prefijo)); } catch (e) {}
  return [...mem.keys()].filter(x => String(x).startsWith(prefijo));
}

/* preferencias chicas de esta persona en este navegador (idioma de la app, tema, pista elegida) */
export function pref(k, v) {
  try { if (v === undefined) return JSON.parse(localStorage.getItem('estacion.' + k)); localStorage.setItem('estacion.' + k, JSON.stringify(v)); } catch (e) {}
  return v ?? null;
}
