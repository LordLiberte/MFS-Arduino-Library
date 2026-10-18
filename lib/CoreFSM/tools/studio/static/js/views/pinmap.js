/* Vista de dispositivo: el mapa de pines de la placa.
 *
 * Sustituye a la comprobación visual que antes hacía falta abrir Wokwi para
 * hacer. Cada pin dice quién lo ocupa, marca en rojo los compartidos y avisa si
 * una salida analógica ha caído en un pin sin PWM por hardware. */

import { h, icon, button, dialog, toast } from "../ui.js";
import { state, emit } from "../store.js";
import { boardOf } from "./hardware.js";

export function renderPinmap() {
  const board = boardOf();
  const owners = ownership();
  const digital = board.digitalPins || [];
  const analog = board.analogPins || [];
  const half = Math.ceil((digital.length + analog.length) / 2);
  const all = [...digital, ...analog];
  const left = all.slice(0, half);
  const right = all.slice(half);

  const clashes = [...owners.entries()].filter(([, list]) => list.length > 1);
  const pwmWarnings = [...owners.entries()].filter(([pin, list]) =>
    list.some((o) => o.needsPwm) && !(board.pwmPins || []).includes(pin));

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Vista de dispositivo"),
        h("p", {}, "Qué hay conectado en cada pin de la ", h("b", {}, board.label),
          ". Pulsa un pin libre para asignarle una señal que aún no tenga dirección.")),
      h("span", { class: "spacer" }),
      h("span", { class: "chip mono" }, owners.size + " / " + all.length + " pines usados"),
      clashes.length ? h("span", { class: "chip fault" }, icon("warn"), clashes.length + " colisión" + (clashes.length > 1 ? "es" : "")) : null),
    h("div", { class: "view-body" },
      h("div", { class: "board" },
        h("div", { class: "pin-col" }, ...left.map((p) => pinRow(p, owners, board, false))),
        h("div", { class: "chip-body" },
          h("div", { class: "t" }, board.label),
          h("div", { class: "s" }, board.family + " · " + board.pioBoard),
          h("div", { class: "s", style: { marginTop: "8px", maxWidth: "150px" } }, board.note)),
        h("div", { class: "pin-col right" }, ...right.map((p) => pinRow(p, owners, board, true)))),
      clashes.length ? warningBox("Pines compartidos", clashes.map(([pin, list]) =>
        pin + " → " + list.map((o) => o.label).join(", ")), "fault") : null,
      pwmWarnings.length ? warningBox("Velocidad sin PWM por hardware", pwmWarnings.map(([pin, list]) =>
        list.filter((o) => o.needsPwm).map((o) => o.label).join(", ") + " usa el pin " + pin +
        ", que no genera PWM en esta placa: la salida solo podrá estar a 0 o a máximo"), "warn") : null,
      legend(board)));
}

function pinRow(pin, owners, board, right) {
  const list = owners.get(pin) || [];
  const clash = list.length > 1;
  const pwm = (board.pwmPins || []).includes(pin);
  const el = h("div", {
    class: ["pin", right ? "right" : "", list.length ? "used" : "", clash ? "clash" : "", pwm ? "pwm" : ""].filter(Boolean).join(" "),
    title: list.length ? list.map((o) => o.label).join("\n") : "Pin libre",
    onclick: () => (list.length ? focusOwner(list[0]) : assign(pin)),
  },
    h("span", { class: "no" }, pin),
    h("span", { class: "who" }, list.length ? list.map((o) => o.label).join(" + ") : "—"));
  return el;
}

function ownership() {
  const map = new Map();
  const claim = (pin, entry) => {
    const key = normalize(pin);
    if (!key) return;
    if (!map.has(key)) map.set(key, []);
    map.get(key).push(entry);
  };
  for (const [index, signal] of (state.model.hardware || []).entries()) {
    claim(signal.target, { label: signal.name || "(sin nombre)", kind: "hardware", index });
  }
  const types = state.catalog.deviceTypes || {};
  for (const [index, device] of (state.model.devices || []).entries()) {
    const spec = types[device.kind] || { pins: [] };
    for (const key of spec.pins || []) {
      claim((device.pins || {})[key], {
        label: (device.name || "(sin nombre)") + "." + key, kind: "devices", index,
        needsPwm: key === "pwm" || key === "signal",
      });
    }
  }
  return map;
}

const normalize = (pin) => String(pin || "").trim().toUpperCase().replace(/^(GPIO|GP|D)(?=\d)/, "");

function focusOwner(owner) {
  state.selection = { kind: owner.kind, index: owner.index };
  emit();
}

function assign(pin) {
  const free = (state.model.hardware || [])
    .map((signal, index) => ({ signal, index }))
    .filter(({ signal }) => !String(signal.target || "").trim());
  if (!free.length) {
    toast("No hay señales sin dirección. Añade una en la tabla de variables y vuelve aquí.", "warn");
    return;
  }
  const body = h("div", {},
    h("p", { class: "muted", style: { marginTop: 0 } }, "Elige qué señal ocupa el pin " + pin + ":"),
    h("div", { class: "picker" }, ...free.map(({ signal, index }) =>
      h("button", { class: "pick", onclick: () => { signal.target = pin; close(); emit({ view: true }); toast(signal.name + " → pin " + pin); } },
        h("b", { class: "mono" }, signal.name || "(sin nombre)"),
        h("span", {}, signal.role + (signal.description ? " · " + signal.description : ""))))));
  let close = () => {};
  const d = dialog({ title: "Asignar el pin " + pin, body, actions: [] });
  close = d.close;
}

function warningBox(title, lines, kind) {
  return h("div", { style: { marginTop: "16px" } },
    h("div", { class: "section-title" }, title),
    h("div", { class: "diag-list" }, ...lines.map((l) =>
      h("div", { class: "diag-row " + (kind === "fault" ? "e" : "w") },
        icon("warn"), h("span", { class: "what" }, l)))));
}

function legend(board) {
  return h("div", { style: { marginTop: "18px", display: "flex", gap: "16px", flexWrap: "wrap", color: "var(--text-3)", fontSize: "12px" } },
    h("span", {}, h("span", { class: "chip mono", style: { marginRight: "6px" } }, "~"), "genera PWM por hardware"),
    h("span", {}, h("span", { class: "chip accent", style: { marginRight: "6px" } }, "verde"), "asignado"),
    h("span", {}, h("span", { class: "chip fault", style: { marginRight: "6px" } }, "rojo"), "dos usuarios en el mismo pin"),
    h("span", { class: "mono" }, "PWM: " + (board.pwmPins || []).join(", ")));
}
