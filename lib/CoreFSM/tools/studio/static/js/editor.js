/* Editor de código.
 *
 * Un <textarea> transparente encima de un <pre> coloreado. Es la técnica clásica
 * y la única que conserva gratis lo que más se nota si falta: deshacer nativo,
 * selección del sistema, teclados internacionales y acentos. El coloreado y el
 * autocompletado se calculan encima; el texto lo sigue gestionando el navegador.
 *
 * Como la fuente es monoespaciada y la altura de línea fija, la posición del
 * cursor en píxeles es aritmética pura: no hace falta medir nada en cada tecla. */

import { h, clear, esc } from "./ui.js";
import { resolveChain, fuzzy, SNIPPETS } from "./symbols.js";

const LINE_H = 20;
const PAD_TOP = 10;
const PAD_LEFT = 10;
const INDENT = "  ";

const KEYWORDS = new Set(["if", "else", "switch", "case", "break", "default", "return", "while",
  "for", "do", "const", "static", "void", "class", "struct", "public", "private", "protected",
  "override", "enum", "namespace", "using", "auto", "new", "delete", "this", "nullptr", "true",
  "false", "continue", "sizeof", "typedef", "inline", "constexpr", "virtual", "operator", "template"]);

const TYPES = new Set(["bool", "char", "int", "long", "unsigned", "float", "double", "uint8_t",
  "uint16_t", "uint32_t", "int8_t", "int16_t", "int32_t", "size_t", "String", "SequenceBlock",
  "FsmBlock", "BlockBase", "BlockManager", "MotorDrive", "DirPwmMotorDrive", "DifferentialChassis",
  "FourWheelChassis", "UltrasonicSensor", "DigitalOutput", "DigitalSensor", "AnalogSensor",
  "TowerLight", "AlarmManager", "ConfigStore", "StepTracer", "Servo", "Print", "Serial"]);

export function createEditor(options = {}) {
  const state = {
    symbols: options.symbols || null,
    markers: options.markers || [],
    readOnly: !!options.readOnly,
    comp: null,          // {items, index, from, prefix, el}
    sig: null,
  };

  const gutterInner = h("div", {});
  const gutter = h("div", { class: "gutter-lines" }, gutterInner);
  const hl = h("pre", { class: "code-hl", "aria-hidden": "true" });
  const input = h("textarea", {
    class: "code-input", spellcheck: "false", autocapitalize: "off",
    autocomplete: "off", autocorrect: "off", wrap: "off",
    readonly: state.readOnly || false,
  });
  const sizer = h("div", { class: "code-sizer" }, hl, input);
  const area = h("div", { class: "code-area" }, sizer);
  const root = h("div", { class: "editor" + (state.readOnly ? " readonly" : "") }, gutter, area);

  input.value = options.value || "";

  /* ---------------------------------------------------------- coloreado -- */

  const TOKEN = /(\/\/[^\n]*)|(\/\*[\s\S]*?\*\/)|(^[ \t]*#[^\n]*)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(\b\d[\w.]*\b)|([A-Za-z_]\w*)/gm;

  function highlight(text) {
    const { names: symbolNames, steps } = sets();
    let out = "";
    let last = 0;
    text.replace(TOKEN, (match, line, block, pre, str, num, word, offset) => {
      out += esc(text.slice(last, offset));
      last = offset + match.length;
      if (line || block) out += '<span class="t-com">' + esc(match) + "</span>";
      else if (pre) out += '<span class="t-pre">' + esc(match) + "</span>";
      else if (str) out += '<span class="t-str">' + esc(match) + "</span>";
      else if (num) out += '<span class="t-num">' + esc(match) + "</span>";
      else {
        const after = text[offset + match.length];
        if (KEYWORDS.has(word)) out += '<span class="t-key">' + esc(word) + "</span>";
        else if (TYPES.has(word)) out += '<span class="t-typ">' + esc(word) + "</span>";
        else if (steps.has(word)) out += '<span class="t-stp">' + esc(word) + "</span>";
        else if (symbolNames.has(word)) out += '<span class="t-sym">' + esc(word) + "</span>";
        else if (after === "(") out += '<span class="t-fun">' + esc(word) + "</span>";
        else out += esc(word);
      }
      return match;
    });
    out += esc(text.slice(last));
    return out;
  }

  /* Los conjuntos de símbolos no cambian mientras se teclea: calcularlos en
   * cada repintado era trabajo tirado. */
  let symbolCache = { source: null, names: new Map(), steps: new Set() };
  function sets() {
    if (symbolCache.source !== state.symbols) {
      const model = state.symbols && state.symbols.model ? state.symbols.model : { states: [] };
      symbolCache = {
        source: state.symbols,
        names: state.symbols ? state.symbols.objects : new Map(),
        steps: new Set((model.states || []).map((s) => s.symbol).filter(Boolean)),
      };
    }
    return symbolCache;
  }

  /* Un repintado por fotograma como mucho. Escribir rápido ya no encola un
   * coloreado completo por pulsación. */
  let frame = null;
  let lastLineCount = -1;
  function render() {
    if (frame) return;
    frame = requestAnimationFrame(() => { frame = null; paint(); });
  }

  function paint() {
    const text = input.value;
    const lines = text.split("\n");
    hl.innerHTML = highlight(text) + "\n";

    /* El textarea se dimensiona al contenido y el contenedor es el que hace
     * scroll: así el coloreado y el texto no pueden desincronizarse nunca. */
    const longest = lines.reduce((max, l) => Math.max(max, l.length), 0);
    sizer.style.height = (lines.length * LINE_H + PAD_TOP * 2) + "px";
    sizer.style.width = "max(100%, " + (longest * 7.53 + PAD_LEFT + 40) + "px)";
    input.style.height = sizer.style.height;

    paintGutter(lines.length);
  }

  /* La columna de números solo se reconstruye si cambia el número de líneas;
   * mover el cursor únicamente mueve la marca de línea actual. */
  function paintGutter(count) {
    const bad = new Set(state.markers.map((m) => m.line));
    if (count !== lastLineCount || bad.size) {
      lastLineCount = count;
      let html = "";
      for (let i = 0; i < count; i++) {
        html += '<div class="ln' + (bad.has(i + 1) ? " bad" : "") + '">' + (i + 1) + "</div>";
      }
      gutterInner.innerHTML = html;
    }
    const cur = lineOf(input.selectionStart);
    const previous = gutterInner.querySelector(".ln.cur");
    if (previous) previous.classList.remove("cur");
    const line = gutterInner.children[cur];
    if (line) line.classList.add("cur");
  }

  const lineOf = (pos) => (input.value.slice(0, pos).match(/\n/g) || []).length;
  const lineStart = (pos) => input.value.lastIndexOf("\n", pos - 1) + 1;

  area.addEventListener("scroll", () => {
    gutterInner.style.transform = "translateY(" + -area.scrollTop + "px)";
  });

  /* ------------------------------------------------------------- cursor -- */

  function caretXY() {
    const pos = input.selectionStart;
    const line = lineOf(pos);
    const col = pos - lineStart(pos);
    return {
      x: PAD_LEFT + col * 7.53 - area.scrollLeft,
      y: PAD_TOP + (line + 1) * LINE_H - area.scrollTop,
      line, col,
    };
  }

  function replaceRange(from, to, text, caretOffset) {
    input.setRangeText(text, from, to, "end");
    if (caretOffset !== undefined) input.selectionStart = input.selectionEnd = from + caretOffset;
    fire();
  }

  function fire() {
    render();
    if (options.onChange) options.onChange(input.value);
  }

  /* ----------------------------------------------------- autocompletado -- */

  function closeComp() {
    if (state.comp && state.comp.el) state.comp.el.remove();
    state.comp = null;
  }
  function closeSig() {
    if (state.sig) { state.sig.remove(); state.sig = null; }
  }

  function openComp(force) {
    if (state.readOnly || !state.symbols) return;
    const pos = input.selectionStart;
    const before = input.value.slice(0, pos);

    const chain = resolveChain(state.symbols, before);
    let items, from, prefix;
    if (chain) {
      items = chain.items;
      prefix = chain.prefix;
      from = pos - prefix.length;
    } else {
      const word = /([A-Za-z_]\w*)$/.exec(before);
      prefix = word ? word[1] : "";
      if (!force && prefix.length < 2) return closeComp();
      from = pos - prefix.length;
      items = state.symbols.globals.concat(
        SNIPPETS.map((s) => ({ label: s.label, insert: s.body, icon: "s", detail: s.detail, snippet: true })));
    }

    const filtered = fuzzy(items, prefix);
    if (!filtered.length) return closeComp();

    closeComp();
    const list = h("div", { class: "acomp" });
    state.comp = { items: filtered, index: 0, from, prefix, el: list, member: !!chain };
    filtered.forEach((item, i) => {
      list.appendChild(h("div", {
        class: "it" + (i === 0 ? " on" : ""),
        onmousedown: (e) => { e.preventDefault(); accept(i); },
      },
        h("span", { class: "ico " + (item.icon || "m") }, (item.icon || "m").toUpperCase()),
        h("span", { class: "lbl", html: mark(item.label, item.hits) }),
        item.detail ? h("span", { class: "det" }, item.detail) : null));
    });

    const { x, y } = caretXY();
    list.style.left = Math.max(4, Math.min(x + 48, root.clientWidth - 320)) + "px";
    list.style.top = (y + 4) + "px";
    root.appendChild(list);
    if (y + 250 > root.clientHeight) {
      list.style.top = "auto";
      list.style.bottom = (root.clientHeight - y + LINE_H + 2) + "px";
    }
  }

  function mark(label, hits) {
    if (!hits || !hits.length) return esc(label);
    const set = new Set(hits);
    let out = "";
    for (let i = 0; i < label.length; i++) out += set.has(i) ? "<em>" + esc(label[i]) + "</em>" : esc(label[i]);
    return out;
  }

  function move(delta) {
    if (!state.comp) return;
    const items = state.comp.el.querySelectorAll(".it");
    items[state.comp.index].classList.remove("on");
    state.comp.index = (state.comp.index + delta + items.length) % items.length;
    const next = items[state.comp.index];
    next.classList.add("on");
    next.scrollIntoView({ block: "nearest" });
  }

  function accept(index) {
    if (!state.comp) return;
    const item = state.comp.items[index === undefined ? state.comp.index : index];
    const from = state.comp.from;
    const to = input.selectionStart;
    let text = item.insert || item.label;
    let caret = text.length;

    if (item.snippet) {
      const indent = input.value.slice(lineStart(from), from).match(/^[ \t]*/)[0];
      text = text.split("\n").join("\n" + indent).replace(/\n\s+$/, "\n" + indent);
      caret = text.length;
    } else if (text.endsWith("()")) {
      caret = text.length;                    // método sin argumentos: fuera del paréntesis
    } else if (text.endsWith("(")) {
      text += ")"; caret = text.length - 1;   // con argumentos: dentro
    } else if (text.endsWith("(, )")) {
      text = text.slice(0, -4) + "(, )"; caret = text.length - 3;
    }

    closeComp();
    replaceRange(from, to, text, caret);
    if (!item.snippet && (text.endsWith(")") && !text.endsWith("()"))) showSignature();
  }

  /* ------------------------------------------------------ ayuda de firma -- */

  function showSignature() {
    closeSig();
    if (!state.symbols) return;
    const pos = input.selectionStart;
    const before = input.value.slice(0, pos);
    const open = /([A-Za-z_]\w*)\s*\([^()]*$/.exec(before);
    if (!open) return;
    const name = open[1];
    let found = null;
    for (const group of Object.values(state.symbols.methods)) {
      const hit = group.find((m) => m.label.split("(")[0] === name);
      if (hit) { found = hit; break; }
    }
    if (!found) return;
    const box = h("div", { class: "sighelp" },
      h("b", {}, found.label),
      found.detail ? h("span", { class: "doc" }, found.detail) : null);
    const { x, y } = caretXY();
    box.style.left = Math.max(4, Math.min(x + 48, root.clientWidth - 300)) + "px";
    box.style.top = (y + 4) + "px";
    root.appendChild(box);
    state.sig = box;
  }

  /* ------------------------------------------------------------ teclado -- */

  const PAIRS = { "(": ")", "[": "]", "{": "}", '"': '"' };

  input.addEventListener("keydown", (e) => {
    if (state.comp) {
      if (e.key === "ArrowDown") { e.preventDefault(); return move(1); }
      if (e.key === "ArrowUp") { e.preventDefault(); return move(-1); }
      if (e.key === "Enter" || e.key === "Tab") { e.preventDefault(); return accept(); }
      if (e.key === "Escape") { e.preventDefault(); return closeComp(); }
    }
    if (e.key === "Escape") { closeSig(); return; }

    if ((e.ctrlKey || e.metaKey) && e.key === " ") { e.preventDefault(); return openComp(true); }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "s") {
      e.preventDefault(); if (options.onSave) options.onSave(); return;
    }
    if ((e.ctrlKey || e.metaKey) && e.key === "/") { e.preventDefault(); return toggleComment(); }
    if (state.readOnly) return;

    if (e.key === "Tab") {
      e.preventDefault();
      const { selectionStart: a, selectionEnd: b } = input;
      if (a !== b || e.shiftKey) return indentBlock(e.shiftKey);
      return replaceRange(a, b, INDENT);
    }

    if (e.key === "Enter") {
      e.preventDefault();
      const pos = input.selectionStart;
      const line = input.value.slice(lineStart(pos), pos);
      let indent = (line.match(/^[ \t]*/) || [""])[0];
      const opens = /[{(]\s*$/.test(line);
      if (opens) indent += INDENT;
      const closesNext = /^\s*[})]/.test(input.value.slice(input.selectionEnd));
      if (opens && closesNext) {
        const outer = indent.slice(0, -INDENT.length);
        return replaceRange(pos, input.selectionEnd, "\n" + indent + "\n" + outer, 1 + indent.length);
      }
      return replaceRange(pos, input.selectionEnd, "\n" + indent);
    }

    if (PAIRS[e.key]) {
      const { selectionStart: a, selectionEnd: b } = input;
      if (a !== b) {
        e.preventDefault();
        const inner = input.value.slice(a, b);
        return replaceRange(a, b, e.key + inner + PAIRS[e.key], 1 + inner.length);
      }
      const next = input.value[a];
      if (!next || /[\s)\]};,]/.test(next)) {
        e.preventDefault();
        return replaceRange(a, b, e.key + PAIRS[e.key], 1);
      }
    }

    if (e.key === "Backspace") {
      const pos = input.selectionStart;
      if (pos === input.selectionEnd && PAIRS[input.value[pos - 1]] === input.value[pos]) {
        e.preventDefault();
        return replaceRange(pos - 1, pos + 1, "");
      }
    }
  });

  input.addEventListener("input", () => {
    fire();
    const ch = input.value[input.selectionStart - 1];
    if (ch === ".") openComp(true);
    else if (ch === "(") showSignature();
    else if (ch === ")" || ch === ";") closeSig();
    else if (state.comp) openComp(false);
    else if (/[A-Za-z_]/.test(ch || "")) openComp(false);
  });

  input.addEventListener("click", () => { closeComp(); closeSig(); paintGutter(lastLineCount); });
  input.addEventListener("blur", () => { closeComp(); closeSig(); });
  input.addEventListener("keyup", (e) => {
    if (["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Home", "End"].includes(e.key)) {
      paintGutter(lastLineCount);
    }
  });

  /* Arrastrar una variable desde una tabla al código. El navegador ya inserta
   * el texto en la posición correcta; solo hay que repintar después. */
  input.addEventListener("drop", () => setTimeout(fire, 0));

  function indentBlock(out) {
    const a = lineStart(input.selectionStart);
    let b = input.value.indexOf("\n", input.selectionEnd);
    if (b < 0) b = input.value.length;
    const block = input.value.slice(a, b);
    const next = out
      ? block.split("\n").map((l) => l.replace(/^ {1,2}|\t/, "")).join("\n")
      : block.split("\n").map((l) => INDENT + l).join("\n");
    input.setRangeText(next, a, b, "select");
    fire();
  }

  function toggleComment() {
    const a = lineStart(input.selectionStart);
    let b = input.value.indexOf("\n", input.selectionEnd);
    if (b < 0) b = input.value.length;
    const lines = input.value.slice(a, b).split("\n");
    const commented = lines.every((l) => !l.trim() || l.trimStart().startsWith("//"));
    const next = lines.map((l) => commented
      ? l.replace(/^(\s*)\/\/ ?/, "$1")
      : (l.trim() ? l.replace(/^(\s*)/, "$1// ") : l)).join("\n");
    input.setRangeText(next, a, b, "select");
    fire();
  }

  paint();

  return {
    el: root,
    focus: () => input.focus(),
    getValue: () => input.value,
    setValue(text) { input.value = text; lastLineCount = -1; paint(); },
    setReadOnly(flag) {
      state.readOnly = flag; input.readOnly = flag;
      root.classList.toggle("readonly", flag);
    },
    setSymbols(symbols) {
      if (state.symbols === symbols) return;
      state.symbols = symbols; render();
    },
    setMarkers(markers) { state.markers = markers || []; lastLineCount = -1; render(); },
    insert(text) {
      input.focus();
      const a = input.selectionStart;
      replaceRange(a, input.selectionEnd, text, text.length);
    },
    goToLine(line) {
      const lines = input.value.split("\n");
      let pos = 0;
      for (let i = 0; i < Math.min(line - 1, lines.length); i++) pos += lines[i].length + 1;
      input.focus();
      input.selectionStart = input.selectionEnd = pos;
      area.scrollTop = Math.max(0, (line - 6) * LINE_H);
      paintGutter(lastLineCount);
    },
  };
}
