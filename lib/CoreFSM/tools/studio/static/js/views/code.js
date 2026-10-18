/* Zona de programación.
 *
 * Un editor con el catálogo del proyecto cargado: al escribir un punto tras un
 * objeto aparecen sus métodos reales, con la firma y para qué sirve cada uno.
 * Los archivos generados se abren en solo lectura, porque editarlos ahí sería
 * trabajo que la próxima generación borraría. */

import { h, icon, button, toast, confirmDialog } from "../ui.js";
import { createEditor } from "../editor.js";
import { buildSymbols } from "../symbols.js";
import { state, emit, tabByKey } from "../store.js";
import { api } from "../api.js";

let editor = null;
let currentPath = null;
let symbolCache = null;
let symbolStamp = null;

export function renderCode(onSave) {
  const tab = tabByKey(state.activeTab);
  const path = tab && tab.kind === "file" ? tab.path : null;
  if (!path) return emptyState();

  const file = state.openFiles.get(path);
  if (!file) return h("div", { class: "empty" }, icon("file"), h("p", {}, "Cargando " + path + "…"));

  const generated = isGenerated(path);
  /* buildSymbols recorre el modelo entero: se rehace solo cuando el modelo
   * cambia, no en cada repintado de la vista. */
  const stamp = JSON.stringify([state.model.hardware.map((x) => [x.name, x.role]),
    state.model.devices.map((x) => [x.name, x.kind]),
    state.model.dataBlocks.map((b) => [b.name, (b.variables || []).map((v) => v.name)]),
    state.model.states.map((x) => x.symbol)]);
  if (symbolStamp !== stamp) { symbolCache = buildSymbols(state.model, state.catalog); symbolStamp = stamp; }
  const symbols = symbolCache;

  if (!editor) {
    editor = createEditor({
      value: file.content, symbols, readOnly: generated,
      onChange: (text) => {
        file.content = text;
        const dirty = text !== file.saved;
        if (dirty !== file.dirty) { file.dirty = dirty; emit(); }
      },
      onSave: () => onSave(),
    });
  }
  if (currentPath !== path) {
    currentPath = path;
    editor.setValue(file.content);
    editor.setReadOnly(generated);
  }
  editor.setSymbols(symbols);

  const bar = h("div", { class: "code-bar" },
    icon(generated ? "bolt" : "code"),
    h("span", { class: "path" }, path),
    generated ? h("span", { class: "chip warn" }, "generado · solo lectura") : null,
    file.dirty ? h("span", { class: "chip" }, "sin guardar") : null,
    h("span", { class: "spacer" }),
    h("span", { class: "muted", style: { fontSize: "11.5px" } },
      "Ctrl+Espacio sugerencias · Ctrl+/ comentar · Ctrl+S guardar"),
    generated ? null : button("Guardar", { icon: "save", small: true, class: "ghost", onClick: onSave }));

  return h("div", { class: "code-shell" }, bar, editor.el);
}

export function resetEditor() { editor = null; currentPath = null; symbolStamp = null; }
export function getEditor() { return editor; }

function emptyState() {
  return h("div", { class: "empty" }, icon("code"),
    h("p", {}, "Abre un archivo del árbol de proyecto para empezar a programar."),
    h("p", { class: "muted" }, "La lógica de la máquina vive en src/Proceso.h; el enlace con el hardware, en src/main.cpp."));
}

export function isGenerated(path) {
  return path === "include/HardwareConfig.h" ||
    path.startsWith("include/generated/") || path.startsWith("src/generated/");
}

export async function openFile(path) {
  if (state.openFiles.has(path)) return;
  if (isGenerated(path) && state.generated[path] !== undefined) {
    state.openFiles.set(path, { content: state.generated[path], saved: state.generated[path], dirty: false, revision: null });
    return;
  }
  const file = await api.readFile(state.projectId, path);
  state.openFiles.set(path, { content: file.content, saved: file.content, dirty: false, revision: file.revision });
}

export async function saveCurrentFile() {
  const tab = tabByKey(state.activeTab);
  if (!tab || tab.kind !== "file") return false;
  const file = state.openFiles.get(tab.path);
  if (!file || !file.dirty) return false;
  if (isGenerated(tab.path)) { toast("Ese archivo lo genera Studio; edita su tabla.", "warn"); return false; }
  try {
    const saved = await api.writeFile(state.projectId, tab.path, file.content, file.revision);
    file.revision = saved.revision;
    file.saved = file.content;
    file.dirty = false;
    emit();
    toast("Guardado " + tab.path);
    return true;
  } catch (err) {
    if (err.code === "revision_conflict") {
      const overwrite = await confirmDialog("El archivo cambió fuera de Studio",
        tab.path + " se ha modificado desde otro editor. ¿Quieres sobrescribirlo con lo que tienes aquí? " +
        "Si prefieres conservar la otra versión, cierra la pestaña y vuelve a abrirla.", "Sobrescribir");
      if (overwrite) {
        const saved = await api.writeFile(state.projectId, tab.path, file.content, null);
        file.revision = saved.revision; file.saved = file.content; file.dirty = false;
        emit(); toast("Guardado " + tab.path);
        return true;
      }
      return false;
    }
    toast(err.message, "err");
    return false;
  }
}
