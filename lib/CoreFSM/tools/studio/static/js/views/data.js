/* Bloques de datos y tipos de usuario.
 *
 * Aquí vive todo lo que en un firmware suele acabar disperso: constantes en el
 * constructor, tiempos dentro del switch, umbrales en main.cpp. Un DB reúne los
 * ajustes de una máquina en un sitio, con su tipo, su valor inicial y su
 * comentario, y se genera como una estructura C++ con nombre. */

import { h, icon, button, toast, dialog } from "../ui.js";
import { createGrid, gridToolbar } from "../grid.js";
import { state, emit, errorsFor } from "../store.js";

export function renderData() {
  const model = state.model;
  if (!model.dataBlocks.length) return renderEmpty();
  if (state.dbIndex === undefined || state.dbIndex >= model.dataBlocks.length) state.dbIndex = 0;
  const block = model.dataBlocks[state.dbIndex];

  const types = () => (state.catalog.dataTypes || []).concat(model.udts.map((u) => u.name))
    .map((t) => ({ value: t, label: t }));

  const variables = createGrid({
    rows: () => block.variables,
    errors: () => errorsFor("dataBlocks[" + state.dbIndex + "].variables"),
    onChange: () => emit(),
    addLabel: "Añadir variable",
    emptyHint: "Cada fila es un ajuste o un dato del proceso. Se leerá desde el código como " + block.name + ".nombre",
    dragPayload: (row) => row.name ? { text: block.name + "." + row.name, kind: "db", name: row.name } : null,
    newRow: () => ({ name: "", type: "uint16_t", initial: "0", comment: "" }),
    columns: [
      { key: "name", label: "Nombre", width: "26%", mono: true, placeholder: "tiempoTrabajoMs" },
      { key: "type", label: "Tipo", width: "150px", type: "select", options: types(), mono: true },
      { key: "initial", label: "Valor inicial", width: "150px", mono: true, placeholder: "0" },
      { key: "comment", label: "Comentario" },
    ],
  });

  const selector = h("div", { class: "grid-toolbar" },
    ...model.dataBlocks.map((b, i) => button(b.name || "DB" + i, {
      small: true, class: i === state.dbIndex ? "primary" : "ghost",
      onClick: () => { state.dbIndex = i; emit(); },
    })),
    button("", { icon: "plus", small: true, class: "ghost", title: "Nuevo bloque de datos",
      onClick: () => { model.dataBlocks.push(newBlock(model.dataBlocks.length)); state.dbIndex = model.dataBlocks.length - 1; emit(); } }),
    h("span", { class: "spacer" }),
    button("Propiedades", { icon: "gear", small: true, class: "ghost", onClick: () => editBlock(block) }),
    model.dataBlocks.length > 1
      ? button("", { icon: "trash", small: true, class: "ghost danger", title: "Eliminar este bloque",
          onClick: () => { model.dataBlocks.splice(state.dbIndex, 1); state.dbIndex = 0; emit(); } })
      : null);

  const udts = createGrid({
    rows: () => model.udts,
    errors: () => errorsFor("udts"),
    onChange: () => emit(),
    addLabel: "Añadir tipo",
    emptyHint: "Un UDT agrupa campos que se repiten (una estación, un eje) para reutilizarlos en varios bloques.",
    newRow: () => ({ name: "", description: "", fields: [] }),
    columns: [
      { key: "name", label: "Nombre del tipo", width: "26%", mono: true, placeholder: "Estacion" },
      { key: "description", label: "Descripción" },
      { key: "__fields", label: "Campos", width: "110px", type: "readonly", center: true,
        render: (row) => h("span", { class: "chip mono" }, (row.fields || []).length) },
      { key: "__edit", label: "", width: "108px", type: "readonly",
        render: (row) => button("Editar campos", { small: true, class: "ghost", onClick: () => editUdt(row) }) },
    ],
  });

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Bloques de datos"),
        h("p", {}, "El sitio único para los ajustes. En lugar de repartir tiempos y umbrales entre el constructor, ",
          h("code", { class: "mono" }, "main.cpp"), " y la lógica, se declaran aquí y el generador crea la estructura."))),
    h("div", { class: "view-body" },
      selector,
      h("div", { class: "chip", style: { marginBottom: "8px" } }, icon("data"),
        h("span", { class: "mono" }, block.name), " · ",
        block.description || "sin descripción",
        block.retained ? h("span", { class: "chip accent", style: { marginLeft: "6px" } }, "remanente") : null),
      variables.el,
      h("div", { class: "section-title" }, "Tipos de datos de usuario (UDT)"),
      udts.el));
}

function renderEmpty() {
  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Bloques de datos"),
        h("p", {}, "Este proyecto todavía no tiene ninguno."))),
    h("div", { class: "view-body" },
      h("div", { class: "empty" }, icon("data"),
        h("p", {}, "Un bloque de datos reúne los ajustes de la máquina —tiempos, umbrales, velocidades— en un sitio, con su tipo y su comentario, en lugar de repartirlos entre el constructor y la lógica."),
        button("Crear el primer bloque de datos", { icon: "plus", primary: true, onClick: () => {
          state.model.dataBlocks.push(newBlock(0));
          state.dbIndex = 0;
          emit({ view: true });
        } }))));
}

function newBlock(index) {
  return { name: index ? "DB_" + (index + 1) : "DB_Proceso", retained: false, version: 1,
    address: index, description: "Parámetros y datos del proceso", variables: [] };
}

function editBlock(block) {
  const body = h("div", {},
    h("div", { class: "field" }, h("label", {}, "Nombre"),
      h("input", { type: "text", value: block.name, oninput: (e) => { block.name = e.target.value; } })),
    h("div", { class: "field" }, h("label", {}, "Descripción"),
      h("input", { type: "text", value: block.description || "", oninput: (e) => { block.description = e.target.value; } })),
    h("div", { class: "field" },
      h("label", {},
        h("input", { type: "checkbox", checked: !!block.retained, style: { marginRight: "7px" },
          onchange: (e) => { block.retained = e.target.checked; } }),
        "Remanente (se guarda en EEPROM y sobrevive al apagado)"),
      h("span", { class: "hint" }, "Úsalo para contadores de producción y ajustes; no para datos que cambian cada scan: la EEPROM tiene escrituras contadas.")));
  dialog({
    title: "Propiedades del bloque de datos", body,
    actions: [{ label: "Hecho", primary: true, onClick: (c) => { c(); emit({ view: true }); } }],
  });
}

function editUdt(udt) {
  udt.fields = udt.fields || [];
  const grid = createGrid({
    rows: () => udt.fields,
    onChange: () => {},
    addLabel: "Añadir campo",
    newRow: () => ({ name: "", type: "uint16_t", initial: "0", comment: "" }),
    columns: [
      { key: "name", label: "Campo", width: "30%", mono: true },
      { key: "type", label: "Tipo", width: "140px", type: "select",
        options: (state.catalog.dataTypes || []).map((t) => ({ value: t, label: t })), mono: true },
      { key: "initial", label: "Inicial", width: "110px", mono: true },
      { key: "comment", label: "Comentario" },
    ],
  });
  dialog({
    title: "Campos de " + (udt.name || "UDT"), wide: true, body: grid.el,
    actions: [{ label: "Hecho", primary: true, onClick: (c) => { c(); emit({ view: true }); } }],
  });
}
