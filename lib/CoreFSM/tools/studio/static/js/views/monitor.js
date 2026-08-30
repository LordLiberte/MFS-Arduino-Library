/* Monitor: la máquina en marcha.
 *
 * Lee el puerto serie y traduce lo que ya emite la librería —las líneas [PASO],
 * [ESTADO] y la tabla de observación— en un Grafcet con el paso activo
 * iluminado. No hace falta tocar el firmware: si el programa usa StepTracer o la
 * consola de mantenimiento, esto funciona tal cual.
 *
 * Los botones de mando envían las mismas teclas que entiende MaintenanceConsole:
 * s marcha, x paro, p pausa, r rearme, w tabla, c estadísticas de scan. */

import { h, icon, button, toast } from "../ui.js";
import { state, emit } from "../store.js";
import { api } from "../api.js";
import { drawGraph } from "./sequence.js";

const RE_STEP = /^\[PASO\]\s+(\S+)\s+(\d+)\s+->\s+(\d+)(?:\s+\(([^)]*)\))?/;
const RE_STATE = /^\[ESTADO\]\s+(\S+)\s+->\s+(\S+)/;
const RE_WATCH = /^\s*(\S+).*?\bpaso=(\d+)/;
const RE_SCAN = /scan:?\s*ult=(\d+)us\s*max=(\d+)us\s*n=(\d+)/;

export function renderMonitor() {
  const monitor = state.monitor;

  if (monitor.available === false) {
    return h("div", { class: "view" },
      h("div", { class: "view-head" }, h("div", {}, h("h2", {}, "Monitor"))),
      h("div", { class: "view-body" },
        h("div", { class: "empty" }, icon("monitor"),
          h("p", {}, "El monitor necesita el módulo pyserial, que no está disponible en el Python que ha arrancado Studio."),
          h("p", { class: "muted" }, "Arranca Studio con el Python de PlatformIO (el archivo «CoreFSM Studio.cmd» ya lo intenta primero) o instala pyserial con  pip install pyserial"))));
  }

  const active = activeStep();
  const bar = h("div", { class: "grid-toolbar" },
    h("span", { class: "chip" },
      h("span", { class: "led " + (monitor.connected ? "run" : "") }),
      monitor.connected ? "conectado" : "desconectado"),
    portSelect(), baudSelect(),
    monitor.connected
      ? button("Desconectar", { icon: "stop", small: true, class: "ghost", onClick: disconnect })
      : button("Conectar", { icon: "play", small: true, primary: true, onClick: connect }),
    h("span", { class: "spacer" }),
    ...COMMANDS.map((command) => button(command.label, {
      small: true, class: "ghost", title: "Envía la tecla «" + command.key + "»",
      disabled: !monitor.connected, onClick: () => send(command.key),
    })));

  const blocks = [...monitor.blocks.values()];
  const right = h("div", { style: { display: "grid", gap: "10px", alignContent: "start", minHeight: 0 } },
    h("div", { class: "section-title", style: { marginTop: 0 } }, "Bloques"),
    blocks.length
      ? h("div", { class: "watch" }, ...blocks.map(blockCard))
      : h("p", { class: "muted", style: { margin: 0, fontSize: "12.5px" } },
          monitor.connected
            ? "Esperando datos. Pulsa «Tabla» para pedir una foto del estado, o «Marcha» para arrancar la secuencia."
            : "Conecta el puerto para ver el estado de los bloques."),
    monitor.scan ? h("div", { class: "chip mono" }, "scan " + monitor.scan.last + " µs · máx " + monitor.scan.max + " µs · " + monitor.scan.count + " ciclos") : null,
    h("div", { class: "section-title" }, "Traza"),
    h("div", { class: "console", id: "monitor-log",
      style: { background: "var(--abyss)", border: "1px solid var(--line)", borderRadius: "5px", height: "260px", overflow: "auto" } },
      monitor.lines.length
        ? monitor.lines.slice(-260).map(logLine)
        : h("span", { class: "dim" }, "Aquí aparece todo lo que la placa escribe por el puerto serie.")));

  return h("div", { class: "view" },
    h("div", { class: "view-head" },
      h("div", {},
        h("h2", {}, "Monitor"),
        h("p", {}, "Lee lo que ya emite la librería por el puerto serie y lo pinta sobre la secuencia. No hay que añadir trazas al código."))),
    h("div", { class: "view-body" },
      bar,
      h("div", { class: "monitor-grid" },
        h("div", { style: { border: "1px solid var(--line)", borderRadius: "5px", background: "var(--panel)", overflow: "auto", maxHeight: "72vh" } },
          drawGraph(state.model, { activeStep: active })),
        right)));
}

const COMMANDS = [
  { key: "s", label: "Marcha" }, { key: "x", label: "Paro" }, { key: "p", label: "Pausa" },
  { key: "r", label: "Rearme" }, { key: "w", label: "Tabla" }, { key: "c", label: "Scan" },
];

function blockCard(block) {
  const stateClass = block.state === "RUNNING" ? "run"
    : block.state === "FAULT" ? "fault"
    : block.state === "HELD" || block.state === "SUSPENDED" ? "warn" : "";
  return h("div", { class: "watch-row", style: { alignItems: "flex-start", flexDirection: "column", gap: "4px" } },
    h("div", { style: { display: "flex", alignItems: "center", gap: "8px", width: "100%" } },
      h("span", { class: "led " + stateClass }),
      h("b", { class: "mono" }, block.name),
      h("span", { class: "spacer", style: { flex: 1 } }),
      h("span", { class: "chip " + (stateClass || "") }, block.state || "?")),
    h("div", { style: { display: "flex", gap: "12px", flexWrap: "wrap", fontSize: "11.5px" } },
      h("span", { class: "mono" }, "paso ", h("b", { style: { color: "var(--accent-2)" } }, block.step ?? "—"),
        block.stepName ? " · " + block.stepName : ""),
      block.timeInStep !== undefined ? h("span", { class: "mono muted" }, block.timeInStep + " ms en paso") : null,
      block.cycles !== undefined ? h("span", { class: "mono muted" }, block.cycles + " ciclos") : null,
      block.error ? h("span", { class: "chip fault mono" }, "ERR " + block.error) : null));
}

function logLine(line) {
  const cls = /\[PASO\]/.test(line) ? "st" : /\[ESTADO\]/.test(line) ? "ok"
    : /ERR|ALARMA|FALLO/i.test(line) ? "er" : /AVISO|WARN/i.test(line) ? "wa" : "";
  return h("div", { class: cls }, line);
}

function activeStep() {
  const blocks = [...state.monitor.blocks.values()];
  const running = blocks.find((b) => b.step !== undefined);
  return running ? running.step : undefined;
}

/* ------------------------------------------------------------ conexión -- */

function portSelect() {
  const monitor = state.monitor;
  const options = (monitor.ports || []).map((p) =>
    h("option", { value: p.device, selected: p.device === monitor.port }, p.device + (p.description ? " · " + p.description : "")));
  return h("select", { style: { width: "270px", height: "26px" }, disabled: monitor.connected,
    onchange: (e) => { monitor.port = e.target.value; } },
    options.length ? options : h("option", { value: "" }, "sin puertos detectados"));
}

function baudSelect() {
  const monitor = state.monitor;
  return h("select", { style: { width: "104px", height: "26px" }, disabled: monitor.connected,
    onchange: (e) => { monitor.baud = Number(e.target.value); } },
    ...[9600, 19200, 38400, 57600, 115200].map((b) =>
      h("option", { value: b, selected: b === monitor.baud }, b + " bd")));
}

export async function refreshPorts() {
  try {
    const result = await api.ports(state.projectId);
    state.monitor.available = true;
    state.monitor.ports = result.ports || [];
    if (!state.monitor.port && result.ports && result.ports.length) state.monitor.port = result.ports[0].device;
    if (result.suggestedBaud) state.monitor.baud = result.suggestedBaud;
    emit();
  } catch (err) {
    if (err.code === "serial_unavailable") state.monitor.available = false;
    emit();
  }
}

async function connect() {
  const monitor = state.monitor;
  if (!monitor.port) { toast("No hay ningún puerto seleccionado.", "warn"); return; }
  try {
    await api.connect({ port: monitor.port, baud: monitor.baud });
    monitor.connected = true;
    monitor.lines = [];
    monitor.cursor = 0;
    monitor.blocks = new Map();
    startPolling();
    emit();
    toast("Conectado a " + monitor.port + " a " + monitor.baud + " baudios");
  } catch (err) {
    toast(err.message, "err", 7000);
  }
}

async function disconnect() {
  stopPolling();
  try { await api.disconnect(); } catch { /* el puerto ya no estaba */ }
  state.monitor.connected = false;
  emit();
}

export function startPolling() {
  stopPolling();
  state.monitor.timer = setInterval(poll, 260);
}
export function stopPolling() {
  if (state.monitor.timer) clearInterval(state.monitor.timer);
  state.monitor.timer = null;
}

async function poll() {
  const monitor = state.monitor;
  try {
    const result = await api.poll(monitor.cursor);
    monitor.cursor = result.cursor;
    if (!result.connected) { monitor.connected = false; stopPolling(); emit(); return; }
    if (!result.lines || !result.lines.length) return;
    for (const line of result.lines) ingest(line);
    monitor.lines.push(...result.lines);
    if (monitor.lines.length > 3000) monitor.lines.splice(0, monitor.lines.length - 3000);
    emit();
    const log = document.getElementById("monitor-log");
    if (log) log.scrollTop = log.scrollHeight;
  } catch (err) {
    stopPolling();
    monitor.connected = false;
    emit();
  }
}

function ingest(line) {
  const monitor = state.monitor;
  const step = RE_STEP.exec(line);
  if (step) {
    const block = ensure(step[1]);
    block.step = Number(step[3]);
    block.stepName = step[4] || "";
    return;
  }
  const status = RE_STATE.exec(line);
  if (status) { ensure(status[1]).state = status[2]; return; }

  const scan = RE_SCAN.exec(line);
  if (scan) { monitor.scan = { last: scan[1], max: scan[2], count: scan[3] }; return; }

  const watch = RE_WATCH.exec(line);
  if (watch) {
    const block = ensure(watch[1]);
    block.step = Number(watch[2]);
    const named = /paso=\d+\(([^)]*)\)/.exec(line);
    if (named) block.stepName = named[1];
    const time = /t_paso=(\d+)ms/.exec(line);
    if (time) block.timeInStep = Number(time[1]);
    const cycles = /ciclos=(\d+)/.exec(line);
    if (cycles) block.cycles = Number(cycles[1]);
    const error = /ERR=0x([0-9A-Fa-f]+)/.exec(line);
    block.error = error ? "0x" + error[1] : null;
    const known = /\b(IDLE|STARTING|RUNNING|PAUSED|HOLDING|HELD|SUSPENDED|STOPPING|STOPPED|FAULT|ABORTED|RESETTING)\b/.exec(line);
    if (known) block.state = known[1];
  }
}

function ensure(name) {
  if (!state.monitor.blocks.has(name)) state.monitor.blocks.set(name, { name });
  return state.monitor.blocks.get(name);
}

async function send(key) {
  try { await api.send(key); } catch (err) { toast(err.message, "err"); }
}
