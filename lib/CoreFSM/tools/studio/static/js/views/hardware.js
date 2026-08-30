/* Tabla de variables de E/S — el equivalente a la tabla de variables PLC.
 *
 * Es el sitio donde se declara el hardware una sola vez: nombre simbólico, tipo,
 * pin y comportamiento eléctrico. A partir de aquí, ni la lógica ni el resto de
 * la aplicación vuelven a mencionar un número de pin. */

import { h, icon, button, toast } from "../ui.js";
import { createGrid, gridToolbar } from "../grid.js";
import { state, emit } from "../store.js";
import { errorsFor } from "../store.js";
import { dropExpression } from "../symbols.js";
import { openImporter } from "./importer.js";

/* El generador rechaza un CSV con atributos que el rol no admite. En vez de
 * dejar que el usuario lo descubra al compilar, la tabla apaga esas celdas. */
const applies = (field) => (row) =>
  ((state.catalog.roleFields || {})[row.role] || []).includes(field);

const ROLES = [
  { value: "DI", label: "DI · entrada digital" },
  { value: "DO", label: "DO · salida digital" },
  { value: "AI", label: "AI · entrada analógica" },
];

export function boardOf() {
  const id = state.model && state.model.board;
  return (state.catalog.boards || []).find((b) => b.id === id) || state.catalog.boards[0];
}

export function pinChoices() {
  const board = boardOf();
  return [...(board.digitalPins || []), ...(board.analogPins || [])];
}

export function renderHardware() {
  let filter = "";

  const grid = createGrid({
    rows: () => visible(),
    errors: () => remap(errorsFor("hardware"), visible()),
    selected: () => (state.selection && state.selection.kind === "hardware" ? indexOf(state.selection.index) : -1),
    onSelect: (i) => { state.selection = { kind: "hardware", index: realIndex(i) }; grid.render(); emit(); },
    onChange: (row, key) => {
      if (row && key === "role") normalizeRole(row);
      grid.render();
      if (bar.setCount) bar.setCount(state.model.hardware.length);
      touch();
    },
    addLabel: "Añadir señal",
    emptyHint: "Todavía no hay señales. Cada fila que añadas aquí se convierte en un objeto de HW que podrás usar en el código.",
    dragPayload: (row) => row.name
      ? { text: dropExpression("io", row.name, row.role), kind: "io", name: row.name, role: row.role }
      : null,
    newRow: () => ({
      name: "", role: "DI", target: "", pullup: true, activeLow: "",
      debounceMs: 20, filter: "", safe: "", group: "", description: "",
    }),
    columns: [
      { key: "name", label: "Nombre", width: "20%", mono: true, placeholder: "Pulsador_Marcha",
        help: "Nombre C++ válido. Es como lo llamarás en el código: HW.Nombre" },
      { key: "role", label: "Tipo", width: "132px", type: "select", options: ROLES,
        render: (row) => h("span", { class: "tag-role " + (row.role || "") }, row.role || "?"),
        help: "DI entrada digital, DO salida digital, AI entrada analógica" },
      { key: "target", label: "Dirección", width: "90px", mono: true, cellClass: "pin-cell",
        datalist: "pin-list", datalistValues: pinChoices(), placeholder: "2 · A0",
        help: "Pin de la placa. Se valida contra el catálogo de la placa elegida" },
      { key: "pullup", label: "Pull-up", appliesTo: applies("pullup"), width: "80px", type: "bool3", center: true,
        help: "Resistencia interna de elevación. Habitual en pulsadores a masa" },
      { key: "activeLow", label: "Activo bajo", appliesTo: applies("activeLow"), width: "92px", type: "bool3", center: true,
        help: "El nivel eléctrico bajo significa señal activa" },
      { key: "debounceMs", label: "Antirreb.", appliesTo: applies("debounceMs"), width: "80px", type: "number", center: true,
        help: "Milisegundos de filtro antirrebote para entradas digitales" },
      { key: "filter", label: "Filtro", appliesTo: applies("filter"), width: "70px", type: "number", center: true,
        help: "Filtro exponencial para entradas analógicas (0-255)" },
      { key: "safe", label: "Est. seguro", appliesTo: applies("safe"), width: "92px", type: "bool3", center: true,
        help: "Valor al que va la salida cuando actúa el interbloqueo" },
      { key: "group", label: "Grupo", width: "13%",
        help: "Agrupación libre: Mando, Señalización, Seguridad…" },
      { key: "description", label: "Comentario" },
    ],
  });

  function visible() {
    if (!filter) return state.model.hardware;
    const needle = filter.toLowerCase();
    return state.model.hardware.filter((r) =>
      [r.name, r.group, r.description, r.target, r.role].join(" ").toLowerCase().includes(needle));
  }
  const realIndex = (i) => state.model.hardware.indexOf(visible()[i]);
  const indexOf = (real) => visible().indexOf(state.model.hardware[real]);

  const bar = gridToolbar({
    title: "HW",
    count: state.model.hardware.length,
    onSearch: (value) => { filter = value; grid.render(); },
    actions: [
      { label: "Importar…", icon: "upload", small: true, class: "ghost",
        title: "Traer los pines desde Wokwi, un CSV o un hardware.json",
        onClick: () => openImporter() },
      { label: "Plantilla de mando", icon: "wand", small: true, class: "ghost",
        title: "Añade marcha, paro, seta y piloto con los ajustes habituales",
        onClick: () => addTemplate(grid) },
    ],
  });

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Tabla de variables"),
        h("p", {}, "Declara aquí cada señal física una sola vez. El código usará ",
          h("code", { class: "mono" }, "HW.Nombre"), " y no volverá a ver un número de pin. Puedes escribirlas a mano, arrastrar una fila al editor para insertar su expresión, o traerlas de Wokwi o de un CSV con «Importar».")),
      h("span", { class: "spacer" })),
    h("div", { class: "view-body table-view" }, bar, grid.el));
}

function addTemplate(grid) {
  const rows = state.model.hardware;
  const names = new Set(rows.map((r) => r.name));
  const template = [
    { name: "Pulsador_Marcha", role: "DI", target: "", pullup: true, activeLow: "", debounceMs: 20, filter: "", safe: "", group: "Mando", description: "Orden de marcha" },
    { name: "Pulsador_Paro", role: "DI", target: "", pullup: true, activeLow: "", debounceMs: 20, filter: "", safe: "", group: "Mando", description: "Orden de paro" },
    { name: "Seta_Emergencia", role: "DI", target: "", pullup: true, activeLow: "", debounceMs: 10, filter: "", safe: "", group: "Seguridad", description: "Contacto NC de la seta" },
    { name: "Piloto_Marcha", role: "DO", target: "", pullup: "", activeLow: false, debounceMs: "", filter: "", safe: false, group: "Señalización", description: "Indicador de ciclo en marcha" },
  ];
  let added = 0;
  for (const row of template) if (!names.has(row.name)) { rows.push(row); added++; }
  touch(); grid.render();
  toast(added ? "Añadidas " + added + " señales. Asigna sus pines en la columna Dirección."
              : "Esas señales ya existen.", added ? "ok" : "warn");
}

export function touch() { emit(); }

/* Deja en la señal solo los atributos que su rol admite, con un valor de
 * partida razonable para los que acaba de estrenar. */
export function normalizeRole(row) {
  const allowed = (state.catalog.roleFields || {})[row.role] || [];
  const defaults = { pullup: true, debounceMs: 20, activeLow: false, safe: false, filter: "" };
  for (const field of ["pullup", "activeLow", "debounceMs", "filter", "safe"]) {
    if (!allowed.includes(field)) row[field] = "";
    else if (row[field] === "" || row[field] === undefined) row[field] = defaults[field];
  }
}

/* Los errores del servidor vienen indexados sobre el modelo completo; la rejilla
 * puede estar filtrada, así que hay que reindexar. */
export function remap(errors, rows) {
  if (!state.model) return errors;
  const full = state.model.hardware;
  if (rows === full) return errors;
  const out = new Map();
  rows.forEach((row, i) => {
    const real = full.indexOf(row);
    if (errors.has(real)) out.set(i, errors.get(real));
  });
  return out;
}
