/* Panel de propiedades y catálogo.
 *
 * Dos trabajos en el mismo sitio, como la ventana de inspección de TIA: editar
 * lo que hay seleccionado y, debajo, enseñar qué se puede hacer con ello. La
 * lista de métodos es la misma que usa el autocompletado, así que aprender aquí
 * y escribir allí no divergen nunca. */

import { h, icon, button, toast } from "./ui.js";
import { state, emit } from "./store.js";
import { guidedSteps } from "./views/portal.js";
import { getEditor } from "./views/code.js";

export function renderInspector(actions) {
  const body = h("div", { class: "inspector-body" });
  const selection = state.selection;
  let title = "Propiedades";

  if (!state.model) {
    body.appendChild(h("p", { class: "muted" }, "Abre un proyecto para ver sus propiedades."));
  } else if (selection && selection.kind === "hardware") {
    title = "Señal";
    renderSignal(body, selection.index);
  } else if (selection && selection.kind === "devices") {
    title = "Dispositivo";
    renderDevice(body, selection.index);
  } else if (selection && selection.kind === "states") {
    title = "Paso";
    renderStep(body, selection.index);
  } else {
    title = "Guía del proyecto";
    renderGuide(body, actions);
  }

  return h("div", {},
    h("div", { class: "pane-head" }, title,
      h("span", { class: "spacer" }),
      selection ? button("", { icon: "x", small: true, title: "Quitar selección",
        onClick: () => { state.selection = null; emit(); } }) : null),
    body);
}

/* ---------------------------------------------------------------- señal -- */

function renderSignal(body, index) {
  const signal = state.model.hardware[index];
  if (!signal) return body.appendChild(h("p", { class: "muted" }, "La señal ya no existe."));

  body.appendChild(h("div", { style: { display: "flex", alignItems: "center", gap: "8px", marginBottom: "12px" } },
    h("span", { class: "tag-role " + signal.role }, signal.role),
    h("b", { class: "mono", style: { fontSize: "13px" } }, signal.name || "(sin nombre)")));

  body.appendChild(text("Nombre", signal.name, (v) => { signal.name = v; }));
  body.appendChild(text("Dirección (pin)", signal.target, (v) => { signal.target = v; }));
  body.appendChild(text("Grupo", signal.group, (v) => { signal.group = v; }));
  body.appendChild(text("Comentario", signal.description, (v) => { signal.description = v; }));

  body.appendChild(h("div", { class: "section-title" }, "Comportamiento"));
  const allowed = (state.catalog.roleFields || {})[signal.role] || [];
  if (allowed.includes("pullup")) body.appendChild(tri("Pull-up interno", signal.pullup, (v) => { signal.pullup = v; },
    "Con el pulsador entre el pin y masa, esto evita la resistencia externa."));
  if (allowed.includes("debounceMs")) body.appendChild(text("Antirrebote (ms)", signal.debounceMs, (v) => { signal.debounceMs = v; }, "number"));
  if (allowed.includes("activeLow")) body.appendChild(tri("Activo a nivel bajo", signal.activeLow, (v) => { signal.activeLow = v; },
    "Invierte la orden: la salida aplica 0 V cuando la lógica pide activar. Típico de un relé por transistor."));
  if (allowed.includes("safe")) body.appendChild(tri("Estado seguro", signal.safe, (v) => { signal.safe = v; },
    "Valor que toma la salida cuando actúa el interbloqueo de seguridad."));
  if (allowed.includes("filter")) body.appendChild(text("Filtro (0-8)", signal.filter, (v) => { signal.filter = v; }, "number"));

  methodCard("HW." + (signal.name || "senal"), signal.role, body);
}

/* ---------------------------------------------------------- dispositivo -- */

function renderDevice(body, index) {
  const device = state.model.devices[index];
  if (!device) return body.appendChild(h("p", { class: "muted" }, "El dispositivo ya no existe."));
  const info = (state.catalog.deviceTypes || {})[device.kind] || {};

  body.appendChild(h("div", { style: { marginBottom: "12px" } },
    h("b", { class: "mono", style: { fontSize: "13px" } }, device.name || "(sin nombre)"),
    h("div", { class: "muted", style: { fontSize: "12px" } }, info.label || device.kind)));

  body.appendChild(text("Nombre", device.name, (v) => { device.name = v; }));
  body.appendChild(text("Descripción", device.label, (v) => { device.label = v; }));

  if ((info.pins || []).length) {
    body.appendChild(h("div", { class: "section-title" }, "Conexiones"));
    for (const key of info.pins) {
      device.pins = device.pins || {};
      body.appendChild(text(key, device.pins[key], (v) => { device.pins[key] = v; }));
    }
  }
  methodCard(device.name || "dispositivo", info.methodGroup, body);
}

/* ----------------------------------------------------------------- paso -- */

function renderStep(body, index) {
  const step = state.model.states[index];
  if (!step) return body.appendChild(h("p", { class: "muted" }, "El paso ya no existe."));
  body.appendChild(text("Número", step.id, (v) => { step.id = Number(v) || 0; }, "number"));
  body.appendChild(text("Símbolo", step.symbol, (v) => { step.symbol = v; }));
  body.appendChild(text("Nombre visible", step.label, (v) => { step.label = v; }));
  body.appendChild(text("Qué ocurre", step.description, (v) => { step.description = v; }, "textarea"));

  body.appendChild(h("div", { class: "section-title" }, "Insertar en el código"));
  const snippet = "case " + (step.symbol || "PASO") + ":\n  \n  break;\n";
  body.appendChild(h("pre", { class: "console", style: { background: "var(--input)", border: "1px solid var(--line)", borderRadius: "5px", margin: 0 } }, snippet));
  body.appendChild(button("Insertar en el editor", { icon: "code", small: true, class: "ghost",
    onClick: () => insert(snippet) }));
  methodCard("(bloque)", "SequenceBlock", body);
}

/* --------------------------------------------------------------- guía --- */

function renderGuide(body, actions) {
  const steps = guidedSteps();
  const validation = state.validation || {};
  body.appendChild(h("p", { class: "muted", style: { marginTop: 0, fontSize: "12.5px" } },
    "Este es el orden en el que se monta una máquina. Cada punto se marca solo cuando el proyecto lo cumple."));
  body.appendChild(h("div", { class: "steps" }, ...steps.map((step, i) => {
    const isNext = !step.done && steps.slice(0, i).every((s) => s.done);
    return h("div", { class: "step-item" + (step.done ? " done" : "") + (isNext ? " now" : ""),
      onclick: () => actions.go(step.view) },
      h("span", { class: "mark" }, step.done ? "✓" : i + 1),
      h("span", { class: "tx" }, h("b", {}, step.title), h("span", {}, step.hint)));
  })));

  body.appendChild(h("div", { class: "section-title" }, "Estado del modelo"));
  const errors = (validation.errors || []).length;
  const warnings = (validation.warnings || []).length;
  body.appendChild(h("div", { style: { display: "grid", gap: "6px" } },
    h("div", { class: "watch-row" }, h("span", { class: "led " + (errors ? "fault" : "run") }),
      h("span", { class: "k" }, errors ? errors + " error" + (errors > 1 ? "es" : "") + " de validación" : "El modelo es válido")),
    warnings ? h("div", { class: "watch-row" }, h("span", { class: "led warn" }),
      h("span", { class: "k" }, warnings + " aviso" + (warnings > 1 ? "s" : ""))) : null,
    h("div", { class: "watch-row" }, h("span", { class: "k" }, "Señales"),
      h("span", { class: "v" }, (state.model.hardware || []).length)),
    h("div", { class: "watch-row" }, h("span", { class: "k" }, "Dispositivos"),
      h("span", { class: "v" }, (state.model.devices || []).length)),
    h("div", { class: "watch-row" }, h("span", { class: "k" }, "Pasos"),
      h("span", { class: "v" }, (state.model.states || []).length))));
}

/* -------------------------------------------------------------- métodos -- */

function methodCard(owner, group, body) {
  const methods = (state.catalog.methods || {})[group];
  if (!methods || !methods.length) return;
  body.appendChild(h("div", { class: "section-title" }, "Métodos disponibles"));
  body.appendChild(h("p", { class: "muted", style: { margin: "0 0 8px", fontSize: "11.5px" } },
    "Pulsa uno para insertarlo en el editor. Son los mismos que ofrece el autocompletado al escribir un punto."));
  for (const method of methods) {
    body.appendChild(h("div", {
      class: "watch-row", style: { cursor: "pointer", flexDirection: "column", alignItems: "flex-start", gap: "2px" },
      onclick: () => insert(owner + "." + method.insert.replace(/\(\s*,\s*\)/, "(, )")),
    },
      h("span", { class: "k mono", style: { color: "var(--accent-2)" } }, method.label),
      h("span", { class: "muted", style: { fontSize: "11.5px" } }, method.detail || "")));
  }
}

function insert(text) {
  const editor = getEditor();
  if (!editor) { toast("Abre primero un archivo en la zona de programación.", "warn"); return; }
  editor.insert(text);
  toast("Insertado en el editor");
}

/* --------------------------------------------------------------- campos -- */

function text(label, value, onChange, type = "text") {
  const control = type === "textarea"
    ? h("textarea", { rows: 3, oninput: (e) => { onChange(e.target.value); markDirty(); } }, value || "")
    : h("input", { type, value: value ?? "", oninput: (e) => { onChange(e.target.value); markDirty(); } });
  return h("div", { class: "field" }, h("label", {}, label), control);
}

function tri(label, value, onChange, hint) {
  return h("div", { class: "field" }, h("label", {}, label),
    h("select", { onchange: (e) => { onChange(e.target.value === "" ? "" : e.target.value === "true"); markDirty(); } },
      h("option", { value: "", selected: value === "" || value === null || value === undefined }, "— sin definir —"),
      h("option", { value: "true", selected: value === true }, "sí"),
      h("option", { value: "false", selected: value === false }, "no")),
    hint ? h("span", { class: "hint" }, hint) : null);
}

/* El inspector escribe directamente sobre el modelo; solo hay que refrescar la
 * barra de estado y el indicador de cambios sin perder el foco del campo. */
let pending = null;
function markDirty() {
  if (pending) clearTimeout(pending);
  pending = setTimeout(() => { pending = null; emit({ keepFocus: true }); }, 420);
}
