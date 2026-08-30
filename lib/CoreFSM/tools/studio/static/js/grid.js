/* Rejilla de datos editable.
 *
 * Es el componente que más se usa en un entorno de ingeniería, así que manda el
 * teclado: se entra a editar escribiendo, Enter baja, Tab pasa a la derecha y
 * Escape deshace. Las filas se pueden arrastrar a un editor o a una celda de
 * condición para insertar la expresión correspondiente. */

import { h, clear, icon, button } from "./ui.js";

const tabless = (el) => { el.tabIndex = -1; return el; };

export function createGrid(config) {
  const root = h("div", { class: "grid-wrap" });
  const table = h("table", { class: "grid" });
  root.appendChild(table);
  let editing = null;        // sesión de edición en curso
  let pendingRow = null;     // fila recién creada con «Añadir», aún sin confirmar
  let pristineSnapshot = null;

  function columnsOf() { return config.columns.filter((c) => !c.hidden); }
  function selectedIndex() {
    return typeof config.selected === "function" ? config.selected() : config.selected;
  }

  function render() {
    clear(table);
    const columns = columnsOf();
    const errors = config.errors ? config.errors() : new Map();

    const head = h("tr", {}, h("th", { class: "num" }, "#"));
    for (const column of columns) {
      head.appendChild(h("th", { style: column.width ? { width: column.width } : null,
        title: column.help || "" }, column.label));
    }
    if (!config.fixed) head.appendChild(h("th", { style: { width: "34px" } }));
    table.appendChild(h("thead", {}, head));

    const body = h("tbody", {});
    const rows = config.rows();
    rows.forEach((row, index) => {
      const rowErrors = errors.get(index) || [];
      const tr = h("tr", {
        class: (rowErrors.length ? "err " : "") + (selectedIndex() === index ? "sel" : ""),
        onclick: () => { if (config.onSelect) config.onSelect(index); },
      }, h("td", { class: "num" }, index + 1));

      if (config.dragPayload) {
        tr.draggable = true;
        tr.addEventListener("dragstart", (event) => {
          const payload = config.dragPayload(row);
          if (!payload) return event.preventDefault();
          event.dataTransfer.setData("text/plain", payload.text);
          event.dataTransfer.setData("application/x-corefsm", JSON.stringify(payload));
          event.dataTransfer.effectAllowed = "copy";
        });
      }

      columns.forEach((column, ci) => {
        const bad = rowErrors.some((e) => e.field === column.key);
        const off = column.appliesTo && !column.appliesTo(row);
        const td = h("td", {
          class: [column.type === "readonly" || off ? "" : "editable", bad ? "bad" : "",
                  off ? "off" : "", column.cellClass || ""].filter(Boolean).join(" "),
          title: off ? (column.notApplicable || "No aplica a este tipo de señal")
                     : (bad ? rowErrors.filter((e) => e.field === column.key).map((e) => e.message).join(" · ") : ""),
          dataset: { row: index, col: ci },
        });
        td.appendChild(off
          ? h("div", { class: "cell center muted" }, "·")
          : display(column, row, index));
        if (column.type !== "readonly" && !off) {
          td.addEventListener("mousedown", (event) => {
            if (event.detail > 1 || event.target.tagName === "INPUT" || event.target.tagName === "SELECT") return;
            event.preventDefault();
            edit(index, ci);
          });
        }
        tr.appendChild(td);
      });

      if (!config.fixed) {
        tr.appendChild(h("td", { class: "act" },
          h("div", { class: "cell center" },
            tabless(button("", { icon: "trash", small: true, class: "danger", title: "Eliminar fila",
              onClick: (event) => { event.stopPropagation(); remove(index); } })))));
      }
      body.appendChild(tr);
    });

    if (!config.fixed) {
      const span = columns.length + 2;
      body.appendChild(h("tr", { class: "add", onclick: append },
        h("td", { colspan: span },
          h("div", { class: "cell" }, icon("plus"), config.addLabel || "Añadir fila"))));
    }
    if (!rows.length && config.emptyHint) {
      // Una tabla vacía debe decir para qué sirve, no quedarse muda.
      body.insertBefore(h("tr", {}, h("td", { colspan: columns.length + 2 },
        h("div", { class: "cell muted", style: { padding: "10px 8px", whiteSpace: "normal", lineHeight: "1.5", height: "auto" } }, config.emptyHint))), body.firstChild);
    }
    table.appendChild(body);
  }

  function display(column, row, index) {
    const value = row[column.key];
    if (column.render) return h("div", { class: "cell" }, column.render(row, index));
    if (column.type === "bool3") {
      const text = value === true ? "sí" : value === false ? "no" : "—";
      return h("div", { class: "cell center" },
        h("span", { class: value === true ? "" : "muted" }, text));
    }
    if (column.type === "select") {
      const option = (column.options || []).find((o) => o.value === value);
      return h("div", { class: "cell" }, option ? option.label : (value || "—"));
    }
    const text = value === "" || value === null || value === undefined ? "—" : String(value);
    return h("div", { class: "cell" + (column.center ? " center" : "") },
      h("span", { class: (column.mono ? "mono " : "") + (text === "—" ? "muted" : "") }, text));
  }

  function edit(rowIndex, colIndex) {
    commit();
    const column = columnsOf()[colIndex];
    const row = config.rows()[rowIndex];
    if (!column || !row) return;
    const td = table.querySelector('td[data-row="' + rowIndex + '"][data-col="' + colIndex + '"]');
    if (!td) return;
    td.classList.add("editing");
    clear(td);

    let field;
    if (column.type === "select" || column.type === "bool3") {
      const options = column.type === "bool3"
        ? [{ value: "", label: "—" }, { value: "true", label: "sí" }, { value: "false", label: "no" }]
        : (column.options || []);
      field = h("select", {}, ...options.map((o) =>
        h("option", { value: String(o.value), selected: String(row[column.key]) === String(o.value) }, o.label)));
      field.addEventListener("change", () => { if (editing === session) commit(); });
    } else {
      field = h("input", {
        type: column.type === "number" ? "number" : "text",
        value: row[column.key] === null || row[column.key] === undefined ? "" : String(row[column.key]),
        placeholder: column.placeholder || "",
        list: column.datalist || null,
      });
      if (column.datalist) attachDatalist(td, column);
    }
    td.appendChild(field);
    const session = { row: rowIndex, col: colIndex, field, column, previous: row[column.key] };
    editing = session;
    field.focus();
    if (field.select) field.select();

    field.addEventListener("keydown", (event) => {
      if (event.key === "Escape") { event.preventDefault(); cancel(); }
      else if (event.key === "Enter") {
        /* Enter confirma y baja, pero NUNCA crea una fila: crear registros sin
         * pedirlo es la forma más rápida de ensuciar una tabla. Para añadir
         * está el botón «Añadir», que es explícito. */
        event.preventDefault();
        const last = rowIndex >= config.rows().length - 1;
        commit();
        if (!last) move(rowIndex + 1, colIndex);
      }
      else if (event.key === "Tab") {
        event.preventDefault(); commit();
        const total = columnsOf().length;
        const next = event.shiftKey ? colIndex - 1 : colIndex + 1;
        const direction = event.shiftKey ? -1 : 1;
        if (next < 0) move(rowIndex - 1, total - 1, -1);
        else if (next >= total) move(rowIndex + 1, 0, 1);
        else move(rowIndex, next, direction);
      }
    });
    /* Al pasar de celda con Tab, la celda anterior se destruye y dispara blur.
     * Sin esta comprobación, ese blur tardío cerraría el editor que acaba de
     * abrirse en la celda siguiente y el foco se iría a la fila. */
    field.addEventListener("blur", () => setTimeout(() => {
      if (editing === session) commit();
    }, 0));
  }

  function attachDatalist(td, column) {
    if (document.getElementById(column.datalist)) return;
    const list = h("datalist", { id: column.datalist },
      ...(column.datalistValues || []).map((v) => h("option", { value: v })));
    document.body.appendChild(list);
  }

  function move(rowIndex, colIndex, direction = 1) {
    const rows = config.rows();
    if (rowIndex < 0 || colIndex < 0) return;
    if (rowIndex >= rows.length) return;   // el tabulador se para al final
    const columns = columnsOf();
    let ci = colIndex;
    while (ci >= 0 && ci < columns.length) {
      const column = columns[ci];
      const editable = column.type !== "readonly" &&
        !(column.appliesTo && !column.appliesTo(rows[rowIndex]));
      if (editable) return edit(rowIndex, ci);
      ci += direction;
    }
    // No queda ninguna celda editable en esa dirección: pasa de fila.
    if (direction > 0) {
      if (rowIndex + 1 >= rows.length) return;
      return move(rowIndex + 1, 0, 1);
    }
    return move(rowIndex - 1, columns.length - 1, -1);
  }

  function commit() {
    if (!editing) return;
    const { row, column, field } = editing;
    const target = config.rows()[row];
    editing = null;
    if (!target) return render();
    let value = field.value;
    if (column.type === "bool3") value = value === "" ? "" : value === "true";
    else if (column.type === "number") value = value === "" ? "" : Number(value);
    else if (column.transform) value = column.transform(value);
    if (target[column.key] !== value) {
      target[column.key] = value;
      if (target === pendingRow) pendingRow = null;
      if (config.onChange) config.onChange(target, column.key, value);
    }
    render();
  }

  function cancel() {
    if (!editing) return;
    const { row, column, previous } = editing;
    editing = null;
    const rows = config.rows();
    const target = rows[row];
    if (target) target[column.key] = previous;
    /* Escape descarta la fila SOLO si acaba de crearse con «Añadir» y sigue
     * intacta. Si el usuario ya ha escrito algo en cualquier columna, Escape
     * cancela la celda y nada más: borrarle lo escrito sería peor que dejar
     * una fila a medias. */
    if (target && target === pendingRow && isPristine(target)) {
      rows.splice(rows.indexOf(target), 1);
      pendingRow = null;
      if (config.onChange) config.onChange(null, null, null);
    }
    pendingRow = null;
    render();
  }

  /* Una fila está intacta si todas sus columnas editables siguen igual que
   * cuando la creó newRow(). */
  function isPristine(row) {
    if (!pristineSnapshot) return false;
    return columnsOf().every((column) => {
      if (column.type === "readonly") return true;
      return String(row[column.key] ?? "") === String(pristineSnapshot[column.key] ?? "");
    });
  }

  function append() {
    if (!config.newRow) return;
    const rows = config.rows();
    const fresh = config.newRow(rows.length);
    pendingRow = fresh;
    pristineSnapshot = JSON.parse(JSON.stringify(fresh));
    rows.push(fresh);
    if (config.onChange) config.onChange(null, null, null);
    render();
    setTimeout(() => edit(rows.length - 1, 0), 0);
  }

  function remove(index) {
    const rows = config.rows();
    rows.splice(index, 1);
    if (config.onChange) config.onChange(null, null, null);
    render();
  }

  render();
  return { el: root, render, append, edit };
}

/* Barra de herramientas compartida por las vistas de tabla. */
export function gridToolbar({ title, actions = [], onSearch, count }) {
  const bar = h("div", { class: "grid-toolbar" });
  if (title) bar.appendChild(h("span", { class: "chip mono" }, title));
  /* El contador se actualiza solo: si se quedara con el valor del primer
   * pintado diría «2 filas» sobre una tabla de treinta. */
  const counter = count !== undefined ? h("span", { class: "muted" }) : null;
  if (counter) { bar.appendChild(counter); setCount(count); }
  function setCount(value) {
    if (counter) counter.textContent = value + (value === 1 ? " fila" : " filas");
  }
  bar.setCount = setCount;
  bar.appendChild(h("span", { class: "spacer" }));
  if (onSearch) {
    bar.appendChild(h("div", { class: "search" }, icon("search"),
      h("input", { type: "text", placeholder: "Filtrar…", oninput: (e) => onSearch(e.target.value) })));
  }
  for (const action of actions) bar.appendChild(button(action.label, action));
  return bar;
}
