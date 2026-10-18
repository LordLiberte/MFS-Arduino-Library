/* Importar la configuración de pines desde otra herramienta.
 *
 * El generador ya sabe leer el diagram.json de Wokwi, un hardware.csv y un
 * hardware.json; lo que faltaba era una puerta de entrada. Wokwi no es un
 * requisito: es un importador más, y cualquier programa que sepa exportar una
 * lista de nombre/pin a CSV entra por la misma puerta. */

import { h, icon, button, dialog, toast } from "../ui.js";
import { state, emit } from "../store.js";
import { api } from "../api.js";

const FORMATS = {
  wokwi: "diagram.json de Wokwi",
  csv: "tabla CSV",
  json: "hardware.json",
};

export function openImporter() {
  if (!state.projectId) { toast("Abre un proyecto antes de importar.", "warn"); return; }

  let parsed = null;
  const preview = h("div", { style: { marginTop: "14px" } });
  const status = h("div", { class: "muted", style: { fontSize: "12px", marginTop: "8px" } });

  const textarea = h("textarea", {
    rows: 7, spellcheck: "false",
    placeholder: "…o pega aquí el contenido del archivo",
    style: { fontFamily: "var(--mono)", fontSize: "12px" },
    oninput: () => { schedule(textarea.value, "pegado"); },
  });

  const drop = h("label", {
    class: "dropzone",
    ondragover: (event) => { event.preventDefault(); drop.classList.add("over"); },
    ondragleave: () => drop.classList.remove("over"),
    ondrop: (event) => {
      event.preventDefault(); drop.classList.remove("over");
      const file = event.dataTransfer.files[0];
      if (file) readFile(file);
    },
  },
    icon("upload"),
    h("div", {},
      h("b", {}, "Arrastra aquí el archivo"),
      h("span", {}, "o pulsa para elegirlo · diagram.json, hardware.csv, hardware.json")),
    h("input", {
      type: "file", accept: ".json,.csv,.txt", style: { display: "none" },
      onchange: (event) => { const file = event.target.files[0]; if (file) readFile(file); },
    }));

  function readFile(file) {
    const reader = new FileReader();
    reader.onload = () => { textarea.value = String(reader.result); schedule(textarea.value, file.name); };
    reader.onerror = () => toast("No se ha podido leer el archivo.", "err");
    reader.readAsText(file);
  }

  let timer = null;
  function schedule(content, filename) {
    if (timer) clearTimeout(timer);
    timer = setTimeout(() => analyse(content, filename), 250);
  }

  async function analyse(content, filename) {
    parsed = null;
    if (!content.trim()) { preview.innerHTML = ""; status.textContent = ""; return; }
    status.textContent = "Analizando…";
    try {
      const result = await api.importHardware(state.projectId, { content, filename, format: "auto" });
      parsed = result;
      status.textContent = "";
      renderPreview(result);
    } catch (err) {
      preview.innerHTML = "";
      status.innerHTML = "";
      status.appendChild(h("span", { style: { color: "var(--fault)" } }, err.message));
    }
  }

  function renderPreview(result) {
    const rows = result.signals || [];
    const existing = new Set(state.model.hardware.map((s) => s.name));
    const clashes = rows.filter((r) => existing.has(r.name));
    preview.innerHTML = "";
    preview.appendChild(h("div", { class: "section-title" }, "Lo que se ha encontrado"));
    preview.appendChild(h("div", { style: { display: "flex", gap: "7px", flexWrap: "wrap", marginBottom: "9px" } },
      h("span", { class: "chip accent" }, FORMATS[result.format] || result.format),
      result.node ? h("span", { class: "chip mono" }, "nodo " + result.node) : null,
      result.board ? h("span", { class: "chip mono" }, "placa " + result.board) : null,
      h("span", { class: "chip" }, rows.length + (rows.length === 1 ? " señal" : " señales")),
      clashes.length ? h("span", { class: "chip warn" }, clashes.length + " con nombre repetido") : null));

    if (!rows.length) {
      preview.appendChild(h("p", { class: "muted", style: { margin: 0 } },
        "El archivo se ha leído pero no contiene señales reconocibles. En un diagrama de Wokwi, cada componente debe tener un id con el nombre que quieras darle a la señal y estar cableado a un pin de la placa."));
      return;
    }

    const table = h("table", { class: "grid" },
      h("thead", {}, h("tr", {},
        h("th", {}, "Nombre"), h("th", { style: { width: "60px" } }, "Tipo"),
        h("th", { style: { width: "80px" } }, "Pin"), h("th", {}, "Estado"))),
      h("tbody", {}, ...rows.map((row) => h("tr", {},
        h("td", {}, h("div", { class: "cell name-cell" }, row.name)),
        h("td", {}, h("div", { class: "cell center" }, h("span", { class: "tag-role " + row.role }, row.role))),
        h("td", {}, h("div", { class: "cell pin-cell" }, row.target)),
        h("td", {}, h("div", { class: "cell" }, existing.has(row.name)
          ? h("span", { class: "chip warn" }, "ya existe")
          : h("span", { class: "muted" }, "nueva")))))));
    preview.appendChild(h("div", { class: "grid-wrap", style: { maxHeight: "260px", overflowY: "auto" } }, table));

    for (const warning of (result.warnings || [])) {
      preview.appendChild(h("div", { class: "diag-row w" }, icon("warn"), h("span", { class: "what" }, warning)));
    }
  }

  const body = h("div", {},
    h("p", { class: "muted", style: { margin: "0 0 12px", lineHeight: "1.55" } },
      "Studio no depende de ninguna herramienta de dibujo concreta: lee el ",
      h("code", { class: "mono" }, "diagram.json"), " de Wokwi, un ",
      h("code", { class: "mono" }, "hardware.csv"), " escrito a mano o exportado desde cualquier programa de esquemáticos, y un ",
      h("code", { class: "mono" }, "hardware.json"), ". Lo que entra siempre acaba en la misma tabla de variables."),
    drop, textarea, status, preview);

  dialog({
    title: "Importar configuración de hardware", wide: true, body,
    actions: [
      { label: "Cancelar", onClick: (close) => close() },
      { label: "Añadir a la tabla", icon: "plus", onClick: (close) => apply(close, false) },
      { label: "Reemplazar la tabla", icon: "refresh", primary: true, onClick: (close) => apply(close, true) },
    ],
  });

  function apply(close, replace) {
    if (!parsed || !(parsed.signals || []).length) {
      toast("Todavía no hay nada que importar.", "warn");
      return;
    }
    const incoming = parsed.signals;
    if (replace) {
      state.model.hardware.splice(0, state.model.hardware.length, ...incoming);
    } else {
      const existing = new Set(state.model.hardware.map((s) => s.name));
      let added = 0;
      for (const row of incoming) {
        if (existing.has(row.name)) continue;   // no se pisa lo que ya hay
        state.model.hardware.push(row);
        added++;
      }
      if (!added) { toast("Todas esas señales ya estaban en la tabla.", "warn"); return; }
    }
    if (parsed.board && parsed.board !== state.model.board) {
      state.model.board = parsed.board;
      toast("Placa cambiada a " + parsed.board + " según la fuente importada.", "ok", 6000);
    }
    close();
    emit({ view: true });
    toast(replace
      ? "Tabla reemplazada con " + incoming.length + " señales. Revisa y pulsa «Guardar y generar»."
      : "Señales añadidas. Revisa y pulsa «Guardar y generar».");
  }
}
