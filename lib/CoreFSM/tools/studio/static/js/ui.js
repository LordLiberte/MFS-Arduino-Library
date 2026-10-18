/* Utilidades de interfaz: creación de nodos, iconos, avisos y diálogos.
 * Sin dependencias externas para que Studio funcione sin conexión. */

export function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") el.className = v;
    else if (k === "html") el.innerHTML = v;
    else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
    else if (k.startsWith("on") && typeof v === "function") el.addEventListener(k.slice(2), v);
    else if (k === "dataset") Object.assign(el.dataset, v);
    else if (v === true) el.setAttribute(k, "");
    else el.setAttribute(k, v);
  }
  add(el, kids);
  return el;
}

function add(el, kids) {
  for (const kid of kids.flat(4)) {
    if (kid === null || kid === undefined || kid === false) continue;
    el.appendChild(kid instanceof Node ? kid : document.createTextNode(String(kid)));
  }
}

export function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); return el; }
export function mount(el, ...kids) { clear(el); add(el, kids); return el; }
export function qs(sel, root = document) { return root.querySelector(sel); }

/* --------------------------------------------------------------- iconos -- */
/* Trazo de 1.6 sobre rejilla de 24: suficiente peso para leerse a 15 px. */
const PATHS = {
  home: "M3 10.5 12 3l9 7.5M5.5 9v11h13V9",
  chip: "M7 7h10v10H7zM4 10h3M4 14h3M17 10h3M17 14h3M10 4v3M14 4v3M10 17v3M14 17v3",
  table: "M3 5h18v14H3zM3 10h18M9 10v9M3 14h18",
  data: "M4 6c0-1.7 3.6-3 8-3s8 1.3 8 3-3.6 3-8 3-8-1.3-8-3zM4 6v12c0 1.7 3.6 3 8 3s8-1.3 8-3V6M4 12c0 1.7 3.6 3 8 3s8-1.3 8-3",
  flow: "M9 4h6v4H9zM9 16h6v4H9zM12 8v8M4 10h4v4H4zM8 12h1M16 10h4v4h-4zM15 12h1",
  code: "M8.5 8 4 12l4.5 4M15.5 8 20 12l-4.5 4M13.5 4l-3 16",
  monitor: "M3 5h18v11H3zM8 20h8M12 16v4M6 11l2.5-3 2 3.5L13 7l2 4 3-2.5",
  play: "M7 4.5 19 12 7 19.5z",
  stop: "M6 6h12v12H6z",
  save: "M5 4h11l3 3v13H5zM8 4v6h7V4M8 20v-6h8v6",
  bolt: "M13 2 4 14h6l-1 8 9-12h-6z",
  gear: "M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6zM19.4 13a7.9 7.9 0 0 0 0-2l2-1.6-2-3.4-2.4 1a7.7 7.7 0 0 0-1.7-1L14.9 3H10l-.4 2.9c-.6.3-1.2.6-1.7 1l-2.4-1-2 3.4L3.6 11a7.9 7.9 0 0 0 0 2l-2 1.6 2 3.4 2.4-1c.5.4 1.1.8 1.7 1l.4 3h4.9l.4-2.9c.6-.3 1.2-.6 1.7-1l2.4 1 2-3.4z",
  plus: "M12 5v14M5 12h14",
  trash: "M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13",
  copy: "M9 9h11v11H9zM4 15V4h11v3",
  build: "M14.5 4.5a4.5 4.5 0 0 0-5.8 5.8L3 16v5h5l5.7-5.7a4.5 4.5 0 0 0 5.8-5.8l-3 3-2.5-2.5z",
  upload: "M12 16V4M7 9l5-5 5 5M4 16v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3",
  refresh: "M20 11a8 8 0 1 0-2.3 6.3M20 5v6h-6",
  folder: "M3 6h6l2 2.5h10V19H3z",
  file: "M6 3h8l4 4v14H6zM14 3v5h4",
  search: "M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM16.5 16.5 21 21",
  warn: "M12 3 2 20h20zM12 9v5M12 17.2v.1",
  check: "M4 12.5 9.5 18 20 6",
  x: "M6 6l12 12M18 6 6 18",
  book: "M4 4h7a3 3 0 0 1 3 3v13a2.5 2.5 0 0 0-2.5-2.5H4zM20 4h-3a3 3 0 0 0-3 3v13a2.5 2.5 0 0 1 2.5-2.5H20z",
  wand: "M4 20 16 8M14 4l1 2.5L17.5 8 15 9l-1 2.5L13 9l-2.5-1L13 6.5zM19 13l.7 1.8 1.8.7-1.8.7-.7 1.8-.7-1.8-1.8-.7 1.8-.7z",
  link: "M10 14a4 4 0 0 0 5.7 0l3-3A4 4 0 0 0 13 5.3l-1.7 1.7M14 10a4 4 0 0 0-5.7 0l-3 3A4 4 0 0 0 11 18.7l1.7-1.7",
  motor: "M4 8h9v8H4zM13 10h3v4h-3zM16 12h4M7 8V5h3v3",
  wave: "M2 12h3l2-6 3 12 3-9 2 3h7",
  cube: "M12 2 3 7v10l9 5 9-5V7zM3 7l9 5 9-5M12 12v10",
  list: "M8 6h13M8 12h13M8 18h13M3.5 6h.1M3.5 12h.1M3.5 18h.1",
};

export function icon(name, cls = "") {
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("fill", "none");
  svg.setAttribute("stroke", "currentColor");
  svg.setAttribute("stroke-width", "1.6");
  svg.setAttribute("stroke-linecap", "round");
  svg.setAttribute("stroke-linejoin", "round");
  svg.setAttribute("class", ("ic-svg " + cls).trim());
  const p = document.createElementNS("http://www.w3.org/2000/svg", "path");
  p.setAttribute("d", PATHS[name] || PATHS.file);
  svg.appendChild(p);
  return svg;
}

export function button(label, opts = {}) {
  const kids = [];
  if (opts.icon) kids.push(icon(opts.icon));
  if (label) kids.push(h("span", {}, label));
  return h("button", {
    class: ["btn", opts.class || "", opts.primary ? "primary" : "", opts.small ? "sm" : "",
            label ? "" : "icon"].filter(Boolean).join(" "),
    title: opts.title || label || "",
    disabled: opts.disabled || false,
    onclick: opts.onClick,
  }, kids);
}

/* --------------------------------------------------------------- avisos -- */

export function toast(message, kind = "ok", ms = 4200) {
  const host = document.getElementById("toasts");
  const el = h("div", { class: "toast " + kind },
    icon(kind === "ok" ? "check" : kind === "err" ? "x" : "warn"),
    h("span", { class: "msg" }, message));
  host.appendChild(el);
  setTimeout(() => {
    el.style.transition = "opacity .2s, transform .2s";
    el.style.opacity = "0"; el.style.transform = "translateX(14px)";
    setTimeout(() => el.remove(), 220);
  }, ms);
  return el;
}

/* ------------------------------------------------------------- diálogos -- */

export function dialog({ title, body, actions = [], wide = false, onClose }) {
  const scrim = h("div", { class: "scrim" });
  const close = () => { scrim.remove(); document.removeEventListener("keydown", key); if (onClose) onClose(); };
  const key = (e) => { if (e.key === "Escape") { e.preventDefault(); close(); } };
  const box = h("div", { class: "dialog" + (wide ? " wide" : "") },
    h("header", {}, h("h2", {}, title), h("span", { class: "spacer" }),
      button("", { icon: "x", small: true, title: "Cerrar", onClick: close })),
    h("div", { class: "body" }, body),
    actions.length ? h("footer", {}, h("span", { class: "spacer" }),
      ...actions.map((a) => button(a.label, { ...a, onClick: () => a.onClick(close) }))) : null);
  scrim.appendChild(box);
  /* Cerrar al pulsar fuera exige que el botón se pulse Y se suelte sobre el
   * fondo, y no acepta nada durante los primeros 300 ms. Sin esto, el diálogo
   * aparece bajo el cursor y el segundo clic de un doble clic —o un clic algo
   * rápido— lo cierra en el acto, que es como si no se hubiera abierto. */
  const openedAt = Date.now();
  let pressedOnScrim = false;
  scrim.addEventListener("mousedown", (e) => { pressedOnScrim = e.target === scrim; });
  scrim.addEventListener("mouseup", (e) => {
    const outside = pressedOnScrim && e.target === scrim;
    pressedOnScrim = false;
    if (outside && Date.now() - openedAt > 300) close();
  });
  document.addEventListener("keydown", key);
  document.body.appendChild(scrim);
  const first = box.querySelector("input, select, textarea, button.primary");
  if (first) setTimeout(() => first.focus(), 30);
  return { close, box };
}

export function confirmDialog(title, message, confirmLabel = "Continuar") {
  return new Promise((resolve) => {
    dialog({
      title,
      body: h("p", { style: { margin: 0, color: "var(--text-2)", lineHeight: "1.6" } }, message),
      actions: [
        { label: "Cancelar", onClick: (c) => { c(); resolve(false); } },
        { label: confirmLabel, primary: true, onClick: (c) => { c(); resolve(true); } },
      ],
      onClose: () => resolve(false),
    });
  });
}

/* ------------------------------------------------------------ separador -- */

export function makeResizer(el, cssVar, min, max, invert = false) {
  el.addEventListener("mousedown", (e) => {
    const rect = el.getBoundingClientRect();
    if (Math.abs(e.clientX - (invert ? rect.left : rect.right)) > 6) return;
    e.preventDefault();
    const root = document.getElementById("workspace");
    const start = e.clientX;
    const base = parseInt(getComputedStyle(root).getPropertyValue(cssVar)) || rect.width;
    const move = (ev) => {
      const delta = invert ? start - ev.clientX : ev.clientX - start;
      root.style.setProperty(cssVar, Math.min(max, Math.max(min, base + delta)) + "px");
    };
    const up = () => { document.removeEventListener("mousemove", move); document.removeEventListener("mouseup", up);
      document.body.style.cursor = ""; };
    document.body.style.cursor = "col-resize";
    document.addEventListener("mousemove", move);
    document.addEventListener("mouseup", up);
  });
}

export const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => (
  { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
