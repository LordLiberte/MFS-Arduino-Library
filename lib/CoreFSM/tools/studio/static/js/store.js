/* Estado de la aplicación.
 *
 * Un único objeto observable. El modelo del proyecto se edita en memoria y solo
 * viaja al servidor cuando el usuario guarda: así una tabla con un nombre a
 * medio escribir no dispara mil validaciones ni mil escrituras en disco. */

const listeners = new Set();

export const state = {
  catalog: null,          // placas, presets, tipos de dispositivo, métodos
  projects: [],           // lista del portal
  projectId: null,
  model: null,            // modelo editable (el que se guarda)
  saved: null,            // copia del último modelo confirmado por el servidor
  validation: { valid: true, errors: [], warnings: [], resources: [] },
  files: [],
  generated: {},
  view: "portal",
  tabs: [],               // [{key, kind, label, path}]
  activeTab: null,
  openFiles: new Map(),   // path -> {content, revision, dirty, saved}
  selection: null,        // {kind, index} para el inspector
  bottomTab: "diag",
  bottomOpen: false,
  console: [],
  monitor: {
    available: null, connected: false, port: "", baud: 9600,
    lines: [], cursor: 0, blocks: new Map(), lastStep: null, scan: null, timer: null,
  },
  busy: false,
};

export function subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); }

/* opts.view    → repinta también el área central
 * opts.keepFocus → no toca el inspector (lo está editando el usuario)
 *
 * Los avisos se agrupan por fotograma. Escribir en una celda dispara un emit
 * por tecla y repintar el marco entero cada vez es justo lo que se nota como
 * falta de fluidez; agrupándolos, veinte pulsaciones seguidas repintan una vez. */
let pending = null;
let frame = 0;

export function emit(opts = {}) {
  if (pending) {
    pending.view = pending.view || opts.view;
    pending.keepFocus = pending.keepFocus && opts.keepFocus;
    return;
  }
  pending = { ...opts };
  frame = requestAnimationFrame(flush);
}

function flush() {
  frame = 0;
  const merged = pending;
  pending = null;
  if (!merged) return;
  for (const fn of listeners) fn(merged);
}

/* Para lo que no puede esperar al siguiente fotograma (cambiar de vista o de
 * pestaña). Cancela el fotograma pendiente para no repintar dos veces ni
 * dejar una llamada huérfana sin datos. */
export function emitNow(opts = {}) {
  if (frame) { cancelAnimationFrame(frame); frame = 0; }
  pending = null;
  for (const fn of listeners) fn(opts);
}

export function set(patch) { Object.assign(state, patch); emit({ view: true }); }

export function isDirty() {
  if (!state.model || !state.saved) return false;
  return JSON.stringify(state.model) !== JSON.stringify(state.saved);
}

export function anyFileDirty() {
  for (const f of state.openFiles.values()) if (f.dirty) return true;
  return false;
}

export function adoptProject(payload) {
  state.projectId = payload.id;
  state.model = deepCopy(payload.model);
  state.saved = deepCopy(payload.model);
  state.validation = payload.validation || { valid: true, errors: [], warnings: [], resources: [] };
  state.files = payload.files || [];
  state.generated = payload.generated || {};
  emit();
}

export function deepCopy(value) { return JSON.parse(JSON.stringify(value)); }

export function logConsole(text, kind = "") {
  const stamp = new Date().toLocaleTimeString("es-ES", { hour12: false });
  state.console.push({ stamp, text, kind });
  if (state.console.length > 900) state.console.splice(0, state.console.length - 900);
}

/* Devuelve los errores de validación que afectan a una colección, indexados por
 * fila, para que cada rejilla pinte solo lo suyo. */
export function errorsFor(collection) {
  const map = new Map();
  for (const err of state.validation.errors || []) {
    const match = /^([A-Za-z]+)\[(\d+)\]\.?(.*)$/.exec(err.path || "");
    if (!match || match[1] !== collection) continue;
    const row = Number(match[2]);
    if (!map.has(row)) map.set(row, []);
    map.get(row).push({ field: match[3], message: err.message });
  }
  return map;
}

/* Pestañas ------------------------------------------------------------- */

export function openTab(tab) {
  const found = state.tabs.find((t) => t.key === tab.key);
  if (!found) state.tabs.push(tab);
  state.activeTab = tab.key;
  state.view = tab.kind === "file" ? "code" : tab.kind;
  emit();
}

export function closeTab(key) {
  const index = state.tabs.findIndex((t) => t.key === key);
  if (index < 0) return;
  const [gone] = state.tabs.splice(index, 1);
  if (gone.kind === "file") state.openFiles.delete(gone.path);
  if (state.activeTab === key) {
    const next = state.tabs[Math.min(index, state.tabs.length - 1)];
    state.activeTab = next ? next.key : null;
    state.view = next ? (next.kind === "file" ? "code" : next.kind) : "hardware";
  }
  emit();
}

export const tabByKey = (key) => state.tabs.find((t) => t.key === key) || null;
