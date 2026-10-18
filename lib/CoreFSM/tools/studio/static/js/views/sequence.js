/* Secuencia: pasos, transiciones y el diagrama tipo Grafcet.
 *
 * El diagrama no es decoración. Es la vista donde se comprueba de un vistazo que
 * no hay pasos huérfanos ni transiciones a ninguna parte, y en modo monitor es
 * donde se ve el paso activo iluminado mientras la máquina corre. */

import { h, icon, button, toast } from "../ui.js";
import { createGrid, gridToolbar } from "../grid.js";
import { state, emit, errorsFor } from "../store.js";

const NS = "http://www.w3.org/2000/svg";
const BOX_W = 216, BOX_H = 52, GAP = 62, LEFT = 60;
/* Margen derecho para las aristas de retorno y su condición. */
const RIGHT = 210;
const VIEW_W = LEFT + BOX_W + RIGHT;

export function renderSequence() {
  const model = state.model;
  const stepsBar = gridToolbar({ title: "Pasos", count: model.states.length });

  const steps = createGrid({
    rows: () => model.states,
    errors: () => errorsFor("states"),
    selected: () => (state.selection && state.selection.kind === "states" ? state.selection.index : -1),
    onSelect: (i) => { state.selection = { kind: "states", index: i }; steps.render(); emit(); },
    onChange: () => { if (stepsBar.setCount) stepsBar.setCount(model.states.length); emit(); },
    addLabel: "Añadir paso",
    emptyHint: "Cada paso es un estado estable de la máquina. Se numeran de 10 en 10 para poder intercalaruno más adelante sin renumerar.",
    dragPayload: (row) => row.symbol ? { text: row.symbol, kind: "step", name: row.symbol } : null,
    newRow: () => {
      const last = model.states[model.states.length - 1];
      const id = last ? Number(last.id) + 10 : 0;
      return { id, symbol: "PASO_" + id, label: "", description: "" };
    },
    columns: [
      { key: "id", label: "Nº", width: "70px", type: "number", mono: true, center: true,
        help: "Número del paso en el enum. De 10 en 10 deja hueco para intercalar." },
      { key: "symbol", label: "Símbolo", width: "24%", mono: true, placeholder: "PASO_TRABAJO",
        help: "Constante C++ que usarás en setStep() y en el switch" },
      { key: "label", label: "Nombre visible", width: "22%", placeholder: "Trabajo",
        help: "Lo que aparece en el diagrama y en la telemetría" },
      { key: "description", label: "Qué ocurre en este paso" },
    ],
  });

  const symbols = () => model.states.map((s) => ({ value: s.symbol, label: s.symbol }));
  const transitions = createGrid({
    rows: () => model.transitions,
    errors: () => errorsFor("transitions"),
    onChange: () => emit(),
    addLabel: "Añadir transición",
    emptyHint: "Una transición documenta qué condición lleva de un paso al siguiente. Arrastra aquí una señal de la tabla de E/S para escribir la condición.",
    newRow: () => ({ from: model.states[0] ? model.states[0].symbol : "", to: "", condition: "" }),
    columns: [
      { key: "from", label: "Desde", width: "24%", type: "select", options: symbols(), mono: true },
      { key: "to", label: "Hasta", width: "24%", type: "select", options: symbols(), mono: true },
      { key: "condition", label: "Condición", mono: true, placeholder: "HW.Sensor.isTriggered()" },
    ],
  });

  acceptDrops(transitions.el);

  const wrap = h("div", { class: "view-body", style: { display: "grid", gridTemplateColumns: "minmax(0, 1fr) minmax(280px, 372px)", gap: "14px", alignItems: "start" } },
    h("div", {},
      stepsBar,
      steps.el,
      h("div", { class: "section-title" }, "Transiciones"),
      transitions.el),
    h("div", {},
      h("div", { class: "section-title", style: { marginTop: "2px" } }, "Diagrama de la secuencia"),
      h("div", { style: { border: "1px solid var(--line)", borderRadius: "5px", background: "var(--panel)", overflowX: "auto" } },
        drawGraph(model))));

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Secuencia"),
        h("p", {}, "Los pasos se generan como un ", h("code", { class: "mono" }, "enum"),
          " en ", h("code", { class: "mono" }, "include/generated/ProjectStates.h"),
          ". El comportamiento de cada paso lo escribes en ", h("code", { class: "mono" }, "src/Proceso.h"), "."))),
    wrap);
}

function acceptDrops(el) {
  el.addEventListener("dragover", (event) => {
    const cell = event.target.closest("td");
    if (cell) { event.preventDefault(); event.dataTransfer.dropEffect = "copy"; }
  });
  el.addEventListener("drop", (event) => {
    const raw = event.dataTransfer.getData("application/x-corefsm");
    const cell = event.target.closest("td[data-row][data-col]");
    if (!raw || !cell) return;
    event.preventDefault();
    const payload = JSON.parse(raw);
    const row = state.model.transitions[Number(cell.dataset.row)];
    if (!row) return;
    row.condition = row.condition ? row.condition + " && " + payload.text : payload.text;
    emit();
  });
}

/* --------------------------------------------------------------- dibujo -- */

export function drawGraph(model, options = {}) {
  const steps = model.states || [];
  const active = options.activeStep;
  const height = Math.max(140, steps.length * (BOX_H + GAP) + 40);
  const svg = document.createElementNS(NS, "svg");
  svg.setAttribute("class", "graph");
  svg.setAttribute("viewBox", "0 0 " + VIEW_W + " " + height);
  /* Tamaño natural, no estirado: un diagrama escalado al ancho del panel
   * cambia de tipografía aparente cada vez que se mueve el separador. */
  svg.setAttribute("width", VIEW_W);
  svg.setAttribute("height", height);
  svg.setAttribute("preserveAspectRatio", "xMinYMin meet");
  svg.style.margin = "12px";

  const defs = document.createElementNS(NS, "defs");
  defs.innerHTML =
    '<marker id="ah" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">' +
    '<path d="M0 0 L10 5 L0 10 z" fill="#3a4859"/></marker>' +
    '<marker id="ahr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">' +
    '<path d="M0 0 L10 5 L0 10 z" fill="#46d17e"/></marker>';
  svg.appendChild(defs);

  if (!steps.length) {
    svg.appendChild(text(20, 40, "Sin pasos definidos todavía", "step-label"));
    return svg;
  }

  const y = new Map();
  steps.forEach((step, i) => y.set(step.symbol, 24 + i * (BOX_H + GAP)));

  /* Primero las aristas, para que las cajas queden por encima. */
  for (const t of (model.transitions || [])) {
    const from = y.get(t.from), to = y.get(t.to);
    if (from === undefined || to === undefined) continue;
    const live = active !== undefined && String(active) === String(symbolId(model, t.from));
    const forward = to > from;
    const straight = forward && Math.abs(to - from - BOX_H - GAP) < 1;
    const path = document.createElementNS(NS, "path");
    if (straight) {
      const mid = from + BOX_H + GAP / 2;
      path.setAttribute("d", "M" + (LEFT + BOX_W / 2) + " " + (from + BOX_H) + " V" + to);
      svg.appendChild(line(LEFT + BOX_W / 2 - 15, mid, LEFT + BOX_W / 2 + 15, mid, "cond-bar"));
      if (t.condition) svg.appendChild(text(LEFT + BOX_W / 2 + 22, mid + 4, clip(t.condition, 20), "edge-cond"));
    } else {
      const side = LEFT + BOX_W + 42;
      const y1 = from + BOX_H / 2, y2 = to + BOX_H / 2;
      path.setAttribute("d", "M" + (LEFT + BOX_W) + " " + y1 + " H" + side +
        " V" + y2 + " H" + (LEFT + BOX_W + 6));
      if (t.condition) svg.appendChild(text(side + 8, (y1 + y2) / 2, clip(t.condition, 18), "edge-cond"));
    }
    path.setAttribute("class", "edge" + (live ? " active" : ""));
    path.setAttribute("marker-end", "url(#" + (live ? "ahr" : "ah") + ")");
    svg.appendChild(path);
  }

  steps.forEach((step, i) => {
    const top = y.get(step.symbol);
    const isActive = active !== undefined && String(active) === String(step.id);
    const box = document.createElementNS(NS, "rect");
    box.setAttribute("x", LEFT); box.setAttribute("y", top);
    box.setAttribute("width", BOX_W); box.setAttribute("height", BOX_H);
    box.setAttribute("rx", 5);
    box.setAttribute("class", "step-box" + (i === 0 ? " initial" : "") + (isActive ? " active" : ""));
    svg.appendChild(box);
    if (i === 0) {
      const inner = document.createElementNS(NS, "rect");
      inner.setAttribute("x", LEFT + 4); inner.setAttribute("y", top + 4);
      inner.setAttribute("width", BOX_W - 8); inner.setAttribute("height", BOX_H - 8);
      inner.setAttribute("rx", 3); inner.setAttribute("class", "step-box initial");
      inner.setAttribute("fill", "none");
      svg.appendChild(inner);
    }
    svg.appendChild(text(LEFT + 14, top + 22, step.symbol, "step-name"));
    svg.appendChild(text(LEFT + 14, top + 39, step.label || step.description || "", "step-label"));
    svg.appendChild(text(LEFT - 12, top + 30, String(step.id), "step-id", "end"));
  });

  return svg;
}

const symbolId = (model, symbol) => {
  const found = (model.states || []).find((s) => s.symbol === symbol);
  return found ? found.id : null;
};

function text(x, y, content, cls, anchor) {
  const el = document.createElementNS(NS, "text");
  el.setAttribute("x", x); el.setAttribute("y", y); el.setAttribute("class", cls);
  if (anchor) el.setAttribute("text-anchor", anchor);
  el.textContent = content;
  return el;
}
function line(x1, y1, x2, y2, cls) {
  const el = document.createElementNS(NS, "line");
  el.setAttribute("x1", x1); el.setAttribute("y1", y1);
  el.setAttribute("x2", x2); el.setAttribute("y2", y2);
  el.setAttribute("class", cls);
  return el;
}
const clip = (s, n = 22) => (String(s).length > n ? String(s).slice(0, n - 1) + "…" : String(s));
