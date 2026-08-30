/* Catálogo de dispositivos: motores, sonar, servos y chasis.
 *
 * Un dispositivo no es una señal: agrupa varios pines con un significado
 * (in1/in2/pwm) y aporta métodos de alto nivel. Por eso tiene tabla propia y no
 * cabe en la de variables. */

import { h, icon, button, dialog, toast } from "../ui.js";
import { createGrid, gridToolbar } from "../grid.js";
import { state, emit, errorsFor } from "../store.js";
import { pinChoices } from "./hardware.js";

export function renderDevices() {
  const types = state.catalog.deviceTypes || {};

  const grid = createGrid({
    rows: () => state.model.devices,
    errors: () => errorsFor("devices"),
    selected: () => (state.selection && state.selection.kind === "devices" ? state.selection.index : -1),
    onSelect: (i) => { state.selection = { kind: "devices", index: i }; grid.render(); emit(); },
    onChange: () => emit(),
    addLabel: "Añadir dispositivo",
    emptyHint: "Un dispositivo agrupa varios pines con un significado y te da métodos listos: forward(), cm(), drive()…",
    dragPayload: (row) => row.name ? { text: row.name, kind: "device", name: row.name } : null,
    newRow: () => ({ name: "", kind: "motor_drive", label: "", pins: {}, refs: {}, options: {} }),
    columns: [
      { key: "name", label: "Nombre", width: "18%", mono: true, placeholder: "MotorIzquierdo" },
      { key: "kind", label: "Tipo", width: "190px", type: "select",
        options: Object.entries(types).map(([value, info]) => ({ value, label: info.label })) },
      { key: "label", label: "Descripción", width: "24%" },
      { key: "__pins", label: "Pines", type: "readonly",
        render: (row) => pinSummary(row, types) },
      { key: "__refs", label: "Referencias", type: "readonly", width: "22%",
        render: (row) => refSummary(row) },
    ],
  });

  const bar = gridToolbar({
    title: "Dispositivos", count: state.model.devices.length,
    actions: [{ label: "Editar pines", icon: "gear", small: true, class: "ghost",
      title: "Abre el detalle del dispositivo seleccionado",
      onClick: () => {
        const index = state.selection && state.selection.kind === "devices" ? state.selection.index : 0;
        if (state.model.devices[index]) editDevice(index, grid);
        else toast("Añade primero un dispositivo.", "warn");
      } }],
  });

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Dispositivos"),
        h("p", {}, "Motores, sonares, servos y chasis. Doble clic en la fila, o el botón «Editar pines», abre el detalle con cada conexión."))),
    h("div", { class: "view-body" }, bar,
      wireDoubleClick(grid, (i) => editDevice(i, grid))));
}

function wireDoubleClick(grid, handler) {
  grid.el.addEventListener("dblclick", (event) => {
    const cell = event.target.closest("td[data-row]");
    if (cell) handler(Number(cell.dataset.row));
  });
  return grid.el;
}

function pinSummary(row, types) {
  const expected = (types[row.kind] || {}).pins || [];
  if (!expected.length) return h("span", { class: "muted" }, "sin pines propios");
  return h("span", {}, ...expected.map((key) => {
    const value = (row.pins || {})[key];
    return h("span", { class: "chip mono", style: { marginRight: "5px" } },
      key, h("b", { style: { color: value ? "var(--accent-2)" : "var(--fault)" } }, value || "?"));
  }));
}

function refSummary(row) {
  const refs = Object.entries(row.refs || {});
  if (!refs.length) return h("span", { class: "muted" }, "—");
  return h("span", { class: "mono", style: { fontSize: "11.5px" } },
    refs.map(([k, v]) => k + "=" + v).join("  "));
}

export function editDevice(index, grid) {
  const device = state.model.devices[index];
  if (!device) return;
  const types = state.catalog.deviceTypes || {};
  const info = types[device.kind] || { pins: [], label: device.kind };
  const pins = pinChoices();
  const motorNames = state.model.devices.filter((d) => d.kind === "motor_drive" || d.kind === "dir_pwm_motor").map((d) => d.name);

  const body = h("div", {});
  body.appendChild(h("div", { class: "section-title" }, "Identificación"));
  body.appendChild(field("Nombre", input(device.name, (v) => { device.name = v; })));
  body.appendChild(field("Descripción", input(device.label || "", (v) => { device.label = v; })));

  if (info.pins.length) {
    body.appendChild(h("div", { class: "section-title" }, "Conexiones"));
    for (const key of info.pins) {
      device.pins = device.pins || {};
      body.appendChild(field(pinLabel(key), select(pins, device.pins[key] || "",
        (v) => { device.pins[key] = v; }, "— sin asignar —")));
    }
  }

  const refKeys = device.kind === "chassis_diff" ? ["left", "right"]
    : device.kind === "chassis_4wd" ? ["fl", "fr", "rl", "rr"] : [];
  if (refKeys.length) {
    body.appendChild(h("div", { class: "section-title" }, "Motores del chasis"));
    for (const key of refKeys) {
      device.refs = device.refs || {};
      body.appendChild(field(refLabel(key), select(motorNames, device.refs[key] || "",
        (v) => { device.refs[key] = v; }, "— elige un motor —")));
    }
  }

  body.appendChild(h("div", { class: "section-title" }, "Ajustes"));
  const optionSpec = OPTIONS[device.kind] || [];
  if (!optionSpec.length) body.appendChild(h("p", { class: "muted", style: { margin: 0 } }, "Este dispositivo no tiene ajustes."));
  for (const spec of optionSpec) {
    device.options = device.options || {};
    body.appendChild(field(spec.label,
      input(device.options[spec.key] ?? spec.def, (v) => { device.options[spec.key] = v === "" ? "" : Number(v); }, "number"),
      spec.help));
  }

  dialog({
    title: "Dispositivo · " + (info.label || device.kind),
    body,
    actions: [{ label: "Hecho", primary: true, onClick: (close) => { close(); emit(); if (grid) grid.render(); } }],
  });
}

const OPTIONS = {
  motor_drive: [{ key: "ramp", label: "Rampa (PWM/ms)", def: 3, help: "Cuánto puede variar el PWM cada milisegundo. Suaviza arranques y evita picos de corriente." }],
  ultrasonic: [
    { key: "intervalMs", label: "Periodo de medida (ms)", def: 60, help: "Un HC-SR04 necesita ~60 ms entre disparos para no oír su propio eco." },
    { key: "timeoutUs", label: "Tiempo máximo de eco (µs)", def: 12000, help: "12000 µs ≈ 2 m. Pasado ese tiempo se considera que no hay obstáculo." },
  ],
  servo: [{ key: "initialAngle", label: "Ángulo inicial (°)", def: 90 }],
  chassis_diff: [{ key: "trackWidth", label: "Ancho de vía (cm)", def: 10, help: "Separación entre ruedas. Se usa para convertir giro en diferencia de velocidad." }],
};

const PIN_LABELS = { in1: "IN1 · sentido A", in2: "IN2 · sentido B", pwm: "ENA/PWM · velocidad",
  trig: "TRIG · disparo", echo: "ECHO · retorno", signal: "Señal PWM" };
const pinLabel = (k) => PIN_LABELS[k] || k;
const REF_LABELS = { left: "Lado izquierdo", right: "Lado derecho", fl: "Delantera izquierda",
  fr: "Delantera derecha", rl: "Trasera izquierda", rr: "Trasera derecha" };
const refLabel = (k) => REF_LABELS[k] || k;

function field(label, control, hint) {
  return h("div", { class: "field" }, h("label", {}, label), control,
    hint ? h("span", { class: "hint" }, hint) : null);
}
function input(value, onInput, type = "text") {
  return h("input", { type, value: value ?? "", oninput: (e) => onInput(e.target.value) });
}
function select(values, current, onChange, placeholder) {
  return h("select", { onchange: (e) => onChange(e.target.value) },
    h("option", { value: "", selected: !current }, placeholder),
    ...values.map((v) => h("option", { value: v, selected: String(v) === String(current) }, v)));
}
