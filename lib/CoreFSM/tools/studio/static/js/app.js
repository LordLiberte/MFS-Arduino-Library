/* Armazón de la aplicación: rail, árbol, pestañas, cinta y barra de estado.
 *
 * Solo se repinta el marco cuando cambia algo del estado; el área central se
 * repinta únicamente cuando el usuario cambia de vista o de pestaña, porque
 * dentro puede haber un editor con el cursor puesto o una celda en edición y
 * repintar por debajo sería robarle el foco. */

import { h, mount, clear, icon, button, toast, dialog, confirmDialog, makeResizer } from "./ui.js";
import { api } from "./api.js";
import * as S from "./store.js";
import { state, emit, subscribe } from "./store.js";
import { renderPortal, newProject, guidedSteps } from "./views/portal.js";
import { renderHardware } from "./views/hardware.js";
import { renderDevices } from "./views/devices.js";
import { renderData } from "./views/data.js";
import { renderSequence } from "./views/sequence.js";
import { renderPinmap } from "./views/pinmap.js";
import { renderCode, openFile, saveCurrentFile, resetEditor, isGenerated } from "./views/code.js";
import { renderMonitor, refreshPorts, stopPolling } from "./views/monitor.js";
import { renderInspector } from "./inspector.js";

const el = (id) => document.getElementById(id);

const VIEWS = [
  { id: "hardware", label: "Tabla de variables", icon: "table" },
  { id: "devices", label: "Dispositivos", icon: "motor" },
  { id: "pinmap", label: "Vista de dispositivo", icon: "chip" },
  { id: "data", label: "Bloques de datos", icon: "data" },
  { id: "sequence", label: "Secuencia", icon: "flow" },
  { id: "code", label: "Programación", icon: "code" },
  { id: "monitor", label: "Monitor", icon: "monitor" },
];

const actions = {
  openProject, createProject, go, save, generate, build, upload, newProject: () => newProject(actions),
};

/* ================================================================ ARRANQUE */

boot();

async function boot() {
  try {
    const data = await api.bootstrap();
    state.catalog = data;
    state.projects = data.projects || [];
    subscribe(render);
    render({ view: true });
    installShortcuts();
    const last = localStorage.getItem("corefsm.lastProject");
    if (last && state.projects.some((p) => p.id === last)) openProject(last);
  } catch (err) {
    document.body.innerHTML =
      '<div style="display:grid;place-items:center;height:100vh;color:#9aa9bd;font:14px system-ui;text-align:center">' +
      '<div><h1 style="color:#f2635f">No se ha podido arrancar Studio</h1><p>' + err.message + "</p></div></div>";
  }
}

/* ================================================================ PINTADO */

function render(opts = {}) {
  const hasProject = !!state.projectId;
  const workspace = el("workspace");
  workspace.classList.toggle("portal", !hasProject);

  renderTitlebar();
  renderRibbon();
  if (hasProject) {
    renderRail();
    renderTree();
    renderTabs();
    if (!opts.keepFocus) mount(el("inspector"), renderInspector(actions));
    renderBottom();
  }
  renderStatus();
  if (opts.view || !el("viewport").firstChild) renderView();
  makeResizer(el("tree"), "--tree-w", 190, 460);
}

function renderTitlebar() {
  const crumb = el("crumb");
  if (!state.projectId) return mount(crumb, h("span", { class: "muted" }, "Vista del portal"));
  const board = (state.catalog.boards || []).find((b) => b.id === state.model.board);
  const project = state.model.project || {};
  mount(crumb,
    icon("folder"),
    h("strong", {}, project.displayName || state.projectId),
    h("span", { class: "muted mono" }, "projects/" + state.projectId),
    board ? h("span", { class: "chip mono" }, icon("chip"), board.label) : null,
    S.isDirty() ? h("span", { class: "chip warn" }, "modelo sin guardar") : null);
}

function renderRibbon() {
  const bar = clear(el("ribbon"));
  if (!state.projectId) {
    bar.appendChild(h("div", { class: "group" },
      button("Nuevo proyecto", { icon: "plus", primary: true, onClick: () => newProject(actions) })));
    bar.appendChild(h("div", { class: "group" },
      button("Recargar", { icon: "refresh", onClick: reload })));
    return;
  }
  bar.appendChild(h("div", { class: "group" },
    button("Portal", { icon: "home", title: "Volver a la vista del portal", onClick: closeProject })));
  bar.appendChild(h("div", { class: "group" },
    button("Guardar y generar", {
      icon: "save", primary: true, class: S.isDirty() ? "dirty" : "",
      title: "Escribe hardware.csv, corefsm.json y los archivos generados (Ctrl+S)",
      onClick: save, disabled: state.busy,
    }),
    button("Regenerar", { icon: "refresh", title: "Vuelve a generar sin cambiar el modelo", onClick: generate, disabled: state.busy })));
  bar.appendChild(h("div", { class: "group" },
    button("Compilar", { icon: "build", onClick: build, disabled: state.busy }),
    button("Cargar", { icon: "upload", title: "Compila y sube el firmware a la placa", onClick: upload, disabled: state.busy })));
  bar.appendChild(h("div", { class: "group" },
    button("Monitor", { icon: "monitor", onClick: () => go("monitor") })));
  bar.appendChild(h("span", { class: "spacer" }));
  const errors = (state.validation.errors || []).length;
  const warnings = (state.validation.warnings || []).length;
  bar.appendChild(h("div", { class: "group" },
    h("span", { class: "chip " + (errors ? "fault" : warnings ? "warn" : "run") },
      h("span", { class: "led " + (errors ? "fault" : warnings ? "warn" : "run") }),
      errors ? errors + " error" + (errors > 1 ? "es" : "") : warnings ? warnings + " aviso" + (warnings > 1 ? "s" : "") : "modelo válido")));
}

function renderRail() {
  const rail = clear(el("rail"));
  rail.appendChild(h("button", { title: "Portal", onclick: closeProject }, icon("home")));
  for (const view of VIEWS) {
    const errors = view.id === "hardware" || view.id === "devices" ? countErrors(view.id) : 0;
    rail.appendChild(h("button", {
      class: state.view === view.id ? "on" : "", title: view.label,
      dataset: errors ? { badge: errors } : {},
      onclick: () => go(view.id),
    }, icon(view.icon)));
  }
  rail.appendChild(h("div", { class: "gap" }));
  rail.appendChild(h("button", { title: "Documentación de la librería",
    onclick: () => toast("La referencia por módulos está en lib/CoreFSM/docs/README.md", "ok", 6000) }, icon("book")));
}

const countErrors = (kind) =>
  (state.validation.errors || []).filter((e) => (e.path || "").startsWith(kind + "[")).length;

function renderTree() {
  const tree = el("tree");
  mount(tree, h("div", { class: "pane-head" }, "Árbol del proyecto"),
    h("div", { class: "tree-body", id: "tree-body" }));
  const body = el("tree-body");

  const node = (label, iconName, opts = {}) => h("div", {
    class: "node" + (opts.active ? " on" : "") + (opts.cls ? " " + opts.cls : ""),
    style: opts.pad ? { "--pad": opts.pad } : null,
    onclick: opts.onClick,
  }, iconName ? icon(iconName) : null, h("span", { class: "label" }, label),
     opts.count !== undefined ? h("span", { class: "count" }, opts.count) : null);

  body.appendChild(node("Configuración de hardware", null, { cls: "group" }));
  body.appendChild(node("Tabla de variables", "table", { pad: "18px", active: state.view === "hardware",
    count: state.model.hardware.length, onClick: () => go("hardware") }));
  body.appendChild(node("Dispositivos", "motor", { pad: "18px", active: state.view === "devices",
    count: state.model.devices.length, onClick: () => go("devices") }));
  body.appendChild(node("Vista de dispositivo", "chip", { pad: "18px", active: state.view === "pinmap",
    onClick: () => go("pinmap") }));

  body.appendChild(node("Datos", null, { cls: "group" }));
  for (const [i, block] of state.model.dataBlocks.entries()) {
    body.appendChild(node(block.name, "data", { pad: "18px",
      active: state.view === "data" && state.dbIndex === i, count: (block.variables || []).length,
      onClick: () => { state.dbIndex = i; go("data"); } }));
  }
  body.appendChild(node("Tipos de usuario", "cube", { pad: "18px", count: state.model.udts.length,
    onClick: () => go("data") }));

  body.appendChild(node("Programa", null, { cls: "group" }));
  body.appendChild(node("Secuencia", "flow", { pad: "18px", active: state.view === "sequence",
    count: state.model.states.length, onClick: () => go("sequence") }));

  const sources = state.files.filter((f) => !f.generated && /\.(h|hpp|cpp|c|ino)$/i.test(f.path));
  for (const file of sources) {
    body.appendChild(node(file.path, "code", { pad: "18px",
      active: state.activeTab === "file:" + file.path, onClick: () => openFileTab(file.path) }));
  }

  body.appendChild(node("Generado automáticamente", null, { cls: "group" }));
  const generated = [...Object.keys(state.generated), "include/HardwareConfig.h"];
  for (const path of generated) {
    body.appendChild(node(path, "bolt", { pad: "18px", cls: "gen",
      active: state.activeTab === "file:" + path, onClick: () => openFileTab(path) }));
  }

  body.appendChild(node("Configuración", null, { cls: "group" }));
  for (const file of state.files.filter((f) => /(platformio\.ini|corefsm\.json|hardware\.csv|README\.md)$/.test(f.path))) {
    body.appendChild(node(file.path, "file", { pad: "18px",
      active: state.activeTab === "file:" + file.path, onClick: () => openFileTab(file.path) }));
  }
}

function renderTabs() {
  const strip = clear(el("tabstrip"));
  for (const tab of state.tabs) {
    const file = tab.kind === "file" ? state.openFiles.get(tab.path) : null;
    strip.appendChild(h("button", {
      class: "tab" + (state.activeTab === tab.key ? " on" : "") + (file && file.dirty ? " mod" : ""),
      onclick: () => { state.activeTab = tab.key; state.view = tab.kind === "file" ? "code" : tab.kind; emit({ view: true }); },
    }, icon(tab.icon || "file"), h("span", {}, tab.label),
       h("span", { class: "x", onclick: (e) => { e.stopPropagation(); S.closeTab(tab.key); emit({ view: true }); } }, "×")));
  }
}

function renderView() {
  const viewport = el("viewport");
  if (!state.projectId) return mount(viewport, renderPortal(actions));
  switch (state.view) {
    case "hardware": return mount(viewport, renderHardware());
    case "devices": return mount(viewport, renderDevices());
    case "pinmap": return mount(viewport, renderPinmap());
    case "data": return mount(viewport, renderData());
    case "sequence": return mount(viewport, renderSequence());
    case "monitor": return mount(viewport, renderMonitor());
    case "code": return mount(viewport, renderCode(saveActive));
    case "build": state.bottomOpen = true; state.bottomTab = "out"; return mount(viewport, renderCode(saveActive));
    default: return mount(viewport, renderHardware());
  }
}

/* --------------------------------------------------------- panel inferior */

function renderBottom() {
  const bottom = el("bottom");
  bottom.classList.toggle("collapsed", !state.bottomOpen);
  const errors = (state.validation.errors || []).length;
  const warnings = (state.validation.warnings || []).length;

  const tabs = clear(el("bottom-tabs"));
  const btab = (id, label, extra) => h("button", {
    class: "btab" + (state.bottomTab === id ? " on" : ""),
    onclick: () => { state.bottomTab = id; state.bottomOpen = true; emit(); },
  }, label, extra);
  tabs.appendChild(btab("diag", "Diagnóstico",
    errors ? h("span", { class: "pill fault" }, errors) : warnings ? h("span", { class: "pill warn" }, warnings) : null));
  tabs.appendChild(btab("out", "Salida", state.console.length ? h("span", { class: "pill" }, state.console.length) : null));
  tabs.appendChild(h("span", { class: "spacer" }));
  tabs.appendChild(button("", { icon: state.bottomOpen ? "x" : "list", small: true,
    title: state.bottomOpen ? "Ocultar el panel" : "Mostrar el panel",
    onClick: () => { state.bottomOpen = !state.bottomOpen; emit(); } }));

  const body = clear(el("bottom-body"));
  if (!state.bottomOpen) return;
  if (state.bottomTab === "diag") {
    const rows = [
      ...(state.validation.errors || []).map((e) => ({ kind: "e", where: e.path, what: e.message })),
      ...(state.validation.warnings || []).map((w) => ({ kind: "w", where: "", what: w })),
    ];
    if (!rows.length) {
      body.appendChild(h("div", { class: "empty", style: { padding: "26px" } },
        icon("check"), h("p", {}, "Sin errores ni avisos. El modelo se puede generar y compilar.")));
    } else {
      body.appendChild(h("div", { class: "diag-list" }, ...rows.map((row) =>
        h("div", { class: "diag-row " + row.kind, onclick: () => jumpTo(row.where) },
          icon(row.kind === "e" ? "warn" : "warn"),
          h("span", { class: "where mono" }, row.where || "modelo"),
          h("span", { class: "what" }, row.what)))));
    }
  } else {
    body.appendChild(h("div", { class: "console" }, ...state.console.map((line) =>
      h("div", { class: line.kind },
        h("span", { class: "dim" }, line.stamp + "  "), line.text))));
    body.scrollTop = body.scrollHeight;
  }
}

function jumpTo(path) {
  const match = /^([A-Za-z]+)\[(\d+)\]/.exec(path || "");
  if (!match) return;
  const map = { hardware: "hardware", devices: "devices", states: "sequence", transitions: "sequence",
    dataBlocks: "data", udts: "data" };
  const view = map[match[1]];
  if (!view) return;
  state.selection = { kind: match[1], index: Number(match[2]) };
  go(view);
}

function renderStatus() {
  const bar = clear(el("statusbar"));
  if (!state.projectId) {
    bar.appendChild(h("span", { class: "item" }, "CoreFSM Studio · entorno local"));
    bar.appendChild(h("span", { class: "spacer" }));
    bar.appendChild(h("span", { class: "item mono" }, (state.projects || []).length + " proyectos"));
    return;
  }
  const errors = (state.validation.errors || []).length;
  bar.appendChild(h("span", { class: "item click", onclick: () => { state.bottomTab = "diag"; state.bottomOpen = true; emit(); } },
    h("span", { class: "led " + (errors ? "fault" : "run") }),
    errors ? errors + " error" + (errors > 1 ? "es" : "") : "sin errores"));
  const done = guidedSteps().filter((s) => s.done).length;
  bar.appendChild(h("span", { class: "item" }, "guía " + done + "/" + guidedSteps().length));
  bar.appendChild(h("span", { class: "spacer" }));
  if (state.monitor.connected) {
    bar.appendChild(h("span", { class: "item" }, h("span", { class: "led run" }),
      state.monitor.port + " · " + state.monitor.baud + " bd"));
  }
  if (S.isDirty()) bar.appendChild(h("span", { class: "item" }, h("span", { class: "led warn" }), "cambios sin guardar"));
  bar.appendChild(h("span", { class: "item mono" }, state.view));
}

/* =============================================================== ACCIONES */

function go(view) {
  if (view === "monitor" && state.monitor.available === null) refreshPorts();
  state.view = view;
  const known = VIEWS.find((v) => v.id === view);
  if (known && view !== "code") {
    S.openTab({ key: "view:" + view, kind: view, label: known.label, icon: known.icon });
  }
  emit({ view: true });
}

async function openProject(id, view) {
  try {
    state.busy = true; emit();
    const payload = await api.project(id);
    S.adoptProject(payload);
    state.openFiles = new Map();
    state.tabs = [];
    state.activeTab = null;
    state.selection = null;
    state.dbIndex = 0;
    resetEditor();
    localStorage.setItem("corefsm.lastProject", id);
    state.busy = false;
    go(view || "hardware");
    if (payload.validation && payload.validation.warnings.length) {
      S.logConsole("Proyecto abierto con " + payload.validation.warnings.length + " aviso(s).", "wa");
    }
  } catch (err) {
    state.busy = false;
    toast(err.message, "err", 7000);
    emit({ view: true });
  }
}

function closeProject() {
  if (S.isDirty() || S.anyFileDirty()) {
    confirmDialog("Hay cambios sin guardar",
      "Si vuelves al portal se perderán los cambios que no hayas guardado. ¿Continuar?", "Salir sin guardar")
      .then((ok) => { if (ok) reallyClose(); });
    return;
  }
  reallyClose();
}

function reallyClose() {
  stopPolling();
  state.projectId = null; state.model = null; state.saved = null;
  state.tabs = []; state.openFiles = new Map(); state.activeTab = null;
  resetEditor();
  reload();
}

async function reload() {
  const data = await api.bootstrap();
  state.catalog = data;
  state.projects = data.projects || [];
  emit({ view: true });
}

async function createProject(choice) {
  try {
    state.busy = true; emit();
    S.logConsole("Creando projects/" + choice.name + "…");
    const result = await api.createProject({
      name: choice.name, board: choice.board, preset: choice.preset, displayName: choice.displayName || choice.name,
    });
    state.busy = false;
    await reload();
    toast("Proyecto creado en projects/" + choice.name);
    S.logConsole(result.output || "Proyecto creado.", "ok");
    openProject(choice.name, "hardware");
  } catch (err) {
    state.busy = false;
    toast(err.message + (err.details.length ? "\n" + err.details.join("\n") : ""), "err", 9000);
    emit();
  }
}

async function save() {
  if (!state.projectId) return;
  try {
    state.busy = true; emit();
    const result = await api.saveModel(state.projectId, state.model);
    S.adoptProject(result.project);
    state.busy = false;
    S.logConsole("Modelo guardado. Generados: " + (result.generatedFiles || []).join(", "), "ok");
    refreshOpenGenerated();
    toast("Guardado y generado");
    emit({ view: true });
  } catch (err) {
    state.busy = false;
    if (err.code === "validation_failed") {
      state.bottomTab = "diag"; state.bottomOpen = true;
      toast("No se ha escrito nada: el modelo tiene " + err.details.length + " error(es). Mira el panel de diagnóstico.", "err", 7000);
      state.validation = { ...state.validation, errors: err.details };
    } else {
      toast(err.message, "err", 7000);
    }
    S.logConsole(err.message, "er");
    emit({ view: true });
  }
}

/* Un archivo generado que esté abierto debe reflejar la nueva generación. */
function refreshOpenGenerated() {
  for (const [path, file] of state.openFiles) {
    if (isGenerated(path) && state.generated[path] !== undefined) {
      file.content = state.generated[path];
      file.saved = file.content;
      file.dirty = false;
    }
  }
  resetEditor();
}

async function generate() { await runAction("generate", "Regenerando…"); }
async function build() { await runAction("build", "Compilando con PlatformIO…"); }
async function upload() { await runAction("upload", "Compilando y cargando…"); }

async function runAction(action, message) {
  if (!state.projectId) return;
  state.busy = true;
  state.bottomTab = "out"; state.bottomOpen = true;
  S.logConsole(message);
  emit();
  try {
    const result = await api.action(state.projectId, action);
    state.busy = false;
    if (action === "generate") {
      S.adoptProject(result.project);
      refreshOpenGenerated();
      S.logConsole("Generación terminada.", "ok");
      toast("Archivos regenerados");
    } else {
      for (const line of (result.output || "").split("\n")) if (line.trim()) S.logConsole(line, result.ok ? "" : "er");
      S.logConsole(action === "build" ? (result.ok ? "Compilación correcta." : "La compilación ha fallado.")
                                      : (result.ok ? "Firmware cargado." : "La carga ha fallado."), result.ok ? "ok" : "er");
      toast(result.ok ? "Terminado sin errores" : "Ha fallado; mira la salida", result.ok ? "ok" : "err", 6000);
    }
    emit({ view: true });
  } catch (err) {
    state.busy = false;
    S.logConsole(err.message, "er");
    toast(err.message, "err", 8000);
    emit({ view: true });
  }
}

async function openFileTab(path) {
  try {
    await openFile(path);
    S.openTab({ key: "file:" + path, kind: "file", path, label: path.split("/").pop(), icon: isGenerated(path) ? "bolt" : "code" });
    state.view = "code";
    resetEditor();
    emit({ view: true });
  } catch (err) {
    toast(err.message, "err");
  }
}

async function saveActive() {
  const tab = state.tabs.find((t) => t.key === state.activeTab);
  if (tab && tab.kind === "file") { await saveCurrentFile(); return; }
  await save();
}

function installShortcuts() {
  document.addEventListener("keydown", (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "s") {
      if (document.activeElement && document.activeElement.classList.contains("code-input")) return;
      event.preventDefault(); saveActive();
    }
    if (event.key === "F5" && (event.ctrlKey || event.shiftKey)) { event.preventDefault(); build(); }
  });
  window.addEventListener("beforeunload", (event) => {
    if (S.isDirty() || S.anyFileDirty()) { event.preventDefault(); event.returnValue = ""; }
  });
}
