/* Vista del portal: la respuesta a «no sabría por dónde empezar».
 *
 * TIA Portal tiene dos caras, una orientada a tareas y otra al proyecto. Esta es
 * la primera: no pregunta qué archivo quieres abrir, pregunta qué quieres hacer,
 * y la lista de pasos guiados se calcula del propio modelo en lugar de fiarse de
 * casillas que el usuario tenga que marcar. */

import { h, icon, button, dialog, toast } from "../ui.js";
import { state, emit } from "../store.js";
import { api } from "../api.js";

export function renderPortal(actions) {
  const projects = state.projects || [];
  const presets = state.catalog.presets || [];

  return h("div", { class: "portal" },
    h("div", { class: "portal-inner" },
      h("h1", {}, "CoreFSM Studio"),
      h("p", { class: "sub" },
        "Entorno de ingeniería para programar hardware como se programa un autómata: " +
        "declaras las señales en una tabla, defines la secuencia por pasos y escribes solo la lógica. " +
        "Los pines, el arranque y el estado seguro los lleva la librería por debajo."),

      h("div", { class: "tasks" },
        task("wand", "Crear un proyecto", "Elige una plantilla y una placa. Studio deja el árbol montado, compilable y con un ejemplo que ya funciona.", () => newProject(actions)),
        task("table", "Configurar el hardware", "Tabla de variables, dispositivos y mapa de pines, con detección de colisiones antes de compilar.", () => openLast(actions, "hardware")),
        task("flow", "Diseñar la secuencia", "Pasos y transiciones con su diagrama. De aquí sale el enum que usarás en el código.", () => openLast(actions, "sequence")),
        task("code", "Escribir la lógica", "Editor con los métodos reales de cada objeto del proyecto al escribir un punto.", () => openLast(actions, "code")),
        task("monitor", "Ver la máquina en marcha", "Conecta el puerto serie y observa el paso activo, los tiempos y las alarmas sin llenar el código de trazas.", () => openLast(actions, "monitor")),
        task("book", "Aprender la librería", "La referencia completa está en lib/CoreFSM/docs, un documento por archivo del núcleo.", () => {
          toast("La documentación está en lib/CoreFSM/docs/README.md dentro del repositorio.", "ok", 6000);
        })),

      h("div", { class: "section-title" }, "Proyectos en este repositorio"),
      projects.length
        ? h("div", { class: "proj-list" }, ...projects.map((project) =>
            h("div", { class: "proj", onclick: () => actions.openProject(project.id) },
              h("div", { class: "ic" }, icon("cube")),
              h("div", {},
                h("div", { class: "t" }, project.name),
                h("div", { class: "p" }, "projects/" + project.id)),
              h("span", { class: "spacer" }),
              project.warnings && project.warnings.length
                ? h("span", { class: "chip warn" }, icon("warn"), project.warnings.length) : null,
              !project.hasStudioModel ? h("span", { class: "chip" }, "sin modelo de Studio") : null,
              h("span", { class: "chip mono" }, project.boardLabel))))
        : h("div", { class: "empty" }, icon("folder"),
            h("p", {}, "Todavía no hay proyectos. Empieza por «Crear un proyecto»."))));
}

function task(iconName, title, description, onClick) {
  return h("button", { class: "task", onclick: onClick },
    h("div", { class: "ic" }, icon(iconName)),
    h("h3", {}, title),
    h("p", {}, description));
}

function openLast(actions, view) {
  const last = state.projectId || (state.projects[0] && state.projects[0].id);
  if (!last) { toast("Crea primero un proyecto.", "warn"); return; }
  actions.openProject(last, view);
}

/* ------------------------------------------------------ proyecto nuevo -- */

export function newProject(actions) {
  const presets = state.catalog.presets || [];
  const boards = state.catalog.boards || [];
  const choice = { preset: presets[0] ? presets[0].id : "starter", board: presets[0] ? presets[0].board : "nano", name: "", displayName: "" };

  const boardSelect = h("select", { onchange: (e) => { choice.board = e.target.value; } },
    ...boards.map((b) => h("option", { value: b.id, selected: b.id === choice.board }, b.label + " · " + b.note)));

  const picker = h("div", { class: "picker" });
  const refresh = () => {
    picker.innerHTML = "";
    for (const preset of presets) {
      picker.appendChild(h("button", {
        class: "pick" + (preset.id === choice.preset ? " on" : ""),
        onclick: () => { choice.preset = preset.id; choice.board = preset.board; boardSelect.value = preset.board; refresh(); },
      }, h("b", {}, preset.label), h("span", {}, preset.description)));
    }
  };
  refresh();

  const nameInput = h("input", { type: "text", placeholder: "MiMaquina", oninput: (e) => {
    choice.name = e.target.value.trim();
    if (!choice.displayName) hint.textContent = choice.name ? "Se creará en projects/" + choice.name : "";
  } });
  const hint = h("span", { class: "hint" }, "");

  const body = h("div", {},
    h("div", { class: "section-title" }, "Plantilla"),
    picker,
    h("div", { class: "section-title" }, "Destino"),
    h("div", { class: "field" }, h("label", {}, "Nombre de la carpeta"), nameInput, hint),
    h("div", { class: "field" }, h("label", {}, "Título del proyecto"),
      h("input", { type: "text", placeholder: "(opcional)", oninput: (e) => { choice.displayName = e.target.value; } })),
    h("div", { class: "field" }, h("label", {}, "Placa"), boardSelect,
      h("span", { class: "hint" }, "Se puede cambiar después: Studio reescribe platformio.ini y revalida los pines.")));

  dialog({
    title: "Nuevo proyecto", wide: true, body,
    actions: [
      { label: "Cancelar", onClick: (close) => close() },
      { label: "Crear", primary: true, icon: "plus", onClick: async (close) => {
        if (!/^[A-Za-z0-9_-]+$/.test(choice.name)) { toast("Usa letras, números, guion o guion bajo en el nombre.", "warn"); return; }
        close();
        await actions.createProject(choice);
      } },
    ],
  });
}

/* -------------------------------------------------------- pasos guiados -- */

export function guidedSteps() {
  const model = state.model;
  if (!model) return [];
  const hasPins = (model.hardware || []).some((s) => s.target) || (model.devices || []).some((d) =>
    Object.values(d.pins || {}).some(Boolean));
  return [
    { key: "hw", view: "hardware", title: "Declarar las señales",
      hint: "Nombre, tipo y pin de cada entrada y salida",
      done: (model.hardware || []).length > 0 || (model.devices || []).length > 0 },
    { key: "pins", view: "pinmap", title: "Asignar las direcciones",
      hint: "Cada señal con su pin, sin colisiones", done: hasPins },
    { key: "db", view: "data", title: "Centralizar los ajustes",
      hint: "Tiempos, umbrales y velocidades en un bloque de datos",
      done: (model.dataBlocks || []).some((b) => (b.variables || []).length > 0) },
    { key: "seq", view: "sequence", title: "Definir la secuencia",
      hint: "Los pasos por los que pasa la máquina",
      done: (model.states || []).length > 1 },
    { key: "code", view: "code", title: "Escribir la lógica",
      hint: "El comportamiento de cada paso, en src/Proceso.h",
      done: (model.transitions || []).length > 0 },
    { key: "build", view: "build", title: "Compilar y cargar",
      hint: "PlatformIO construye y sube el firmware", done: false },
  ];
}
