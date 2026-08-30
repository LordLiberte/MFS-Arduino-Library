/* Tabla de símbolos del proyecto.
 *
 * El editor no adivina: pregunta aquí. Cada objeto que existe en el proyecto
 * (una señal de la tabla de E/S, un motor, un bloque de datos, un paso) se
 * traduce a un símbolo con tipo, y el tipo decide qué métodos se ofrecen al
 * escribir un punto. Es el mismo mecanismo que usa un IDE de verdad, solo que
 * la fuente de verdad es el modelo del proyecto en lugar de un compilador. */

const CPP_KEYWORDS = [
  "if", "else", "switch", "case", "break", "default", "return", "while", "for", "do",
  "const", "static", "void", "bool", "true", "false", "class", "struct", "public",
  "private", "protected", "override", "enum", "namespace", "using", "auto", "new",
  "delete", "this", "nullptr", "continue", "sizeof", "typedef", "inline", "constexpr",
];

const CPP_TYPES = [
  "uint8_t", "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "float", "bool",
  "SequenceBlock", "FsmBlock", "BlockBase", "BlockManager", "MotorDrive", "DifferentialChassis",
  "FourWheelChassis", "UltrasonicSensor", "DigitalOutput", "DigitalSensor", "AnalogSensor",
  "TowerLight", "AlarmManager", "ConfigStore", "StepTracer", "Servo",
];

/* Métodos heredados que están disponibles dentro de un SequenceBlock aunque no
 * se escriban sobre ningún objeto: se ofrecen al pulsar Ctrl+Espacio. */
const IMPLICIT = [
  { label: "updateSequence()", insert: "updateSequence()", detail: "bool · aplica mando, pausa, fallo y timeouts. Primera línea de update()" },
  { label: "setStep(paso)", insert: "setStep(", detail: "Cambia de paso y reinicia su cronómetro" },
  { label: "setStep(paso, faultMs)", insert: "setStep(", detail: "Cambia de paso con vigilancia de tiempo" },
  { label: "setStep(paso, warnMs, faultMs)", insert: "setStep(", detail: "Aviso blando y fallo duro" },
  { label: "getTimeInStep()", insert: "getTimeInStep()", detail: "uint32_t · ms dentro del paso, sin contar esperas" },
  { label: "completeCycle(paso)", insert: "completeCycle(", detail: "Cierra el ciclo, cuenta y cambia de paso" },
  { label: "requestStop()", insert: "requestStop()", detail: "Solicita una parada ordenada" },
  { label: "fault(codigo)", insert: "fault(", detail: "Lleva el bloque a fallo con el código indicado" },
  { label: "suspendWhile(cond)", insert: "suspendWhile(", detail: "bool · espera por causa externa. Patrón: if (suspendWhile(c)) break;" },
  { label: "holdWhile(cond)", insert: "holdWhile(", detail: "bool · espera por causa interna o del operario" },
  { label: "isFaulted()", insert: "isFaulted()", detail: "bool · el bloque está en fallo" },
  { label: "isRunning()", insert: "isRunning()", detail: "bool · en marcha, sin contar esperas" },
  { label: "getStep()", insert: "getStep()", detail: "uint16_t · paso actual" },
  { label: "setCycleTimeout(ms)", insert: "setCycleTimeout(", detail: "Límite duro del ciclo productivo; al superarlo, alarma" },
  { label: "setCycleTarget(ms)", insert: "setCycleTarget(", detail: "Takt objetivo; solo avisa, no para" },
  { label: "setName(F(\"\"))", insert: "setName(F(\"\"))", detail: "Nombre del bloque para la telemetría" },
  { label: "setInitialStep(paso)", insert: "setInitialStep(", detail: "Paso al que vuelve tras un rearme" },
];

/* Fragmentos: la respuesta a «no sé por dónde empezar» dentro del editor. */
export const SNIPPETS = [
  { label: "paso", detail: "Un caso del switch con su transición",
    body: "case ${PASO}:\n  \n  if (getTimeInStep() >= 1000) {\n    setStep(${SIGUIENTE});\n  }\n  break;\n" },
  { label: "espera", detail: "Espera por causa externa sin congelar el ciclo",
    body: "if (suspendWhile(!condicion)) break;\n" },
  { label: "update", detail: "Esqueleto de update() con el enclavamiento",
    body: "void update() override {\n  if (!updateSequence()) { /* salidas a estado seguro */ return; }\n\n  switch (_currentStep) {\n    \n  }\n}\n" },
  { label: "onStepEntered", detail: "Código que corre una vez al entrar en un paso",
    body: "void onStepEntered(uint16_t step) override {\n  switch (step) {\n    \n  }\n}\n" },
  { label: "stepName", detail: "Nombres legibles para la telemetría",
    body: "const __FlashStringHelper* stepName(uint16_t s) const override {\n  switch (s) {\n    \n    default: return nullptr;\n  }\n}\n" },
];

export function buildSymbols(model, catalog) {
  const methods = (catalog && catalog.methods) || {};
  const deviceTypes = (catalog && catalog.deviceTypes) || {};
  const objects = new Map();   // nombre -> {kind, members|methodGroup, detail}
  const globals = [];          // completions de primer nivel

  /* HW: la imagen de proceso. Sus miembros son las señales de la tabla de E/S. */
  const hwMembers = new Map();
  for (const signal of (model.hardware || [])) {
    if (!signal.name) continue;
    hwMembers.set(signal.name, {
      kind: "io", role: signal.role,
      methodGroup: signal.role,
      detail: (signal.description || signal.group || "") + " · pin " + (signal.target || "?"),
    });
  }
  objects.set("HW", { kind: "image", members: hwMembers, detail: "Imagen de proceso (tabla de E/S)" });
  globals.push({ label: "HW", insert: "HW", icon: "o", detail: "Imagen de proceso · " + hwMembers.size + " señales" });

  /* Dispositivos: motores, sonar, chasis, servo. */
  for (const device of (model.devices || [])) {
    if (!device.name) continue;
    const type = deviceTypes[device.kind] || {};
    objects.set(device.name, { kind: "device", methodGroup: type.methodGroup || device.kind, detail: type.label || device.kind });
    globals.push({ label: device.name, insert: device.name, icon: "o", detail: (type.label || device.kind) + (device.label ? " · " + device.label : "") });
  }

  /* Bloques de datos: sus variables son los ajustes y los datos de proceso. */
  for (const block of (model.dataBlocks || [])) {
    if (!block.name) continue;
    const members = new Map();
    for (const variable of (block.variables || [])) {
      if (!variable.name) continue;
      members.set(variable.name, { kind: "var", type: variable.type, detail: variable.type + (variable.comment ? " · " + variable.comment : "") });
    }
    objects.set(block.name, { kind: "db", members, detail: block.description || "Bloque de datos" });
    globals.push({ label: block.name, insert: block.name, icon: "o", detail: "Bloque de datos · " + members.size + " variables" });
  }

  /* Pasos: constantes del enum generado. */
  for (const step of (model.states || [])) {
    if (!step.symbol) continue;
    globals.push({ label: step.symbol, insert: step.symbol, icon: "s",
      detail: "Paso " + step.id + (step.label ? " · " + step.label : "") });
  }

  for (const item of IMPLICIT) globals.push({ label: item.label, insert: item.insert, icon: "m", detail: item.detail });
  for (const type of CPP_TYPES) globals.push({ label: type, insert: type, icon: "k", detail: "tipo" });
  for (const word of CPP_KEYWORDS) globals.push({ label: word, insert: word, icon: "k", detail: "palabra clave" });

  return { objects, globals, methods, model };
}

/* Resuelve la cadena que hay delante del cursor: "HW.Pulsador." o "Sonar." */
export function resolveChain(symbols, textBeforeCursor) {
  const match = /([A-Za-z_][A-Za-z0-9_]*(?:\s*\.\s*[A-Za-z_][A-Za-z0-9_]*)*)\s*\.\s*([A-Za-z0-9_]*)$/
    .exec(textBeforeCursor);
  if (!match) return null;
  const parts = match[1].split(".").map((p) => p.trim()).filter(Boolean);
  const prefix = match[2] || "";

  let current = symbols.objects.get(parts[0]);
  if (!current) return null;
  for (let i = 1; i < parts.length; i++) {
    if (!current.members) return null;
    const member = current.members.get(parts[i]);
    if (!member) return null;
    current = member.methodGroup
      ? { kind: "leaf", methodGroup: member.methodGroup, detail: member.detail }
      : { kind: "leaf", type: member.type, detail: member.detail };
  }

  const items = [];
  if (current.members) {
    for (const [name, info] of current.members) {
      items.push({ label: name, insert: name, icon: info.methodGroup ? "v" : "v", detail: info.detail || "" });
    }
  }
  if (current.methodGroup && symbols.methods[current.methodGroup]) {
    for (const method of symbols.methods[current.methodGroup]) {
      items.push({ label: method.label, insert: method.insert, icon: "m", detail: method.detail || "" });
    }
  }
  return { items, prefix, owner: parts.join(".") };
}

/* Filtro difuso ligero: subsecuencia, con premio por prefijo y por inicio de
 * palabra. Suficiente para listas de decenas de símbolos y sin dependencias. */
export function fuzzy(items, query) {
  if (!query) return items.slice(0, 200);
  const needle = query.toLowerCase();
  const scored = [];
  for (const item of items) {
    const hay = item.label.toLowerCase();
    if (hay.startsWith(needle)) { scored.push({ item, score: 1000 - hay.length, hits: range(0, needle.length) }); continue; }
    let index = 0; const hits = [];
    for (let i = 0; i < hay.length && index < needle.length; i++) {
      if (hay[i] === needle[index]) { hits.push(i); index++; }
    }
    if (index === needle.length) scored.push({ item, score: 500 - hits[0] - hay.length, hits });
  }
  scored.sort((a, b) => b.score - a.score);
  return scored.slice(0, 60).map((s) => ({ ...s.item, hits: s.hits }));
}

const range = (from, to) => Array.from({ length: to - from }, (_, i) => from + i);

/* Expresión sugerida al arrastrar una señal al editor o a una condición. */
export function dropExpression(kind, name, role) {
  if (kind === "io") {
    if (role === "DI") return "HW." + name + ".isTriggered()";
    if (role === "DO") return "HW." + name + ".set(true)";
    if (role === "AI") return "HW." + name + ".scaled()";
    return "HW." + name;
  }
  return name;
}
