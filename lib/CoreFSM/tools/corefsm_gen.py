#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generador de la tabla de hardware de CoreFSM.

La entrada puede ser un CSV manual, un JSON explicito o un ``diagram.json``
de Wokwi. Los tres formatos se traducen primero al mismo modelo y comparten
las mismas validaciones; el emisor de C++ no sabe de donde vino cada senal.

El modulo usa solo la biblioteca estandar para poder ejecutarse tanto desde
la terminal como dentro del Python incluido en PlatformIO/SCons.
"""

from __future__ import print_function

import argparse
import csv
import json
import os
import re
import sys


CSV_FIELDS = (
    "node", "name", "role", "target", "pullup", "active_low",
    "debounce_ms", "filter", "safe",
)

ROLES = {"DI", "DO", "AI"}

DIGITAL_INPUTS = {
    "wokwi-pushbutton", "wokwi-pushbutton-6mm",
    "wokwi-slide-switch", "wokwi-dip-switch-8",
    "wokwi-tilt-switch-ball", "wokwi-pir-motion-sensor",
    "wokwi-ir-receiver", "wokwi-reed-switch",
    "wokwi-key-switch", "wokwi-microswitch",
    "wokwi-hall-effect-sensor", "wokwi-tilt-switch",
}

DIGITAL_OUTPUTS = {
    "wokwi-led", "wokwi-led-bar-graph", "wokwi-buzzer",
    "wokwi-relay-module", "wokwi-active-buzzer",
    "wokwi-rgb-led", "wokwi-neopixel", "wokwi-7segment",
}

ANALOG_INPUTS = {
    "wokwi-potentiometer", "wokwi-slide-potentiometer",
    "wokwi-photoresistor-sensor", "wokwi-ntc-temperature-sensor",
    "wokwi-analog-joystick", "wokwi-force-sensor",
}

PWM_OUTPUTS = {"wokwi-servo", "wokwi-dc-motor", "wokwi-stepper-motor"}

ROLE_PREFIXES = (
    ("DI_", "DI"), ("I_", "DI"), ("IN_", "DI"),
    ("DO_", "DO"), ("Q_", "DO"), ("OUT_", "DO"),
    ("AI_", "AI"), ("E_", "AI"), ("AN_", "AI"),
)

POWER_PINS = {
    "GND", "VCC", "5V", "3V3", "3.3V", "VIN", "AREF", "RESET",
    "RST", "VBUS", "VSYS", "AGND", "IOREF", "EN", "VN", "VP",
}

BOARD_HINTS = (
    "wokwi-arduino", "board-", "wokwi-esp32", "wokwi-pi-pico",
    "wokwi-attiny", "wokwi-franzininho", "wokwi-microbit",
)

CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
    "bitor", "bool", "break", "case", "catch", "char", "char16_t",
    "char32_t", "class", "compl", "const", "constexpr", "const_cast",
    "continue", "decltype", "default", "delete", "do", "double",
    "dynamic_cast", "else", "enum", "explicit", "export", "extern",
    "false", "float", "for", "friend", "goto", "if", "inline", "int",
    "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
    "nullptr", "operator", "or", "or_eq", "private", "protected",
    "public", "register", "reinterpret_cast", "return", "short", "signed",
    "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
    "template", "this", "thread_local", "throw", "true", "try", "typedef",
    "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "wchar_t", "while", "xor", "xor_eq",
}


class ConfigError(ValueError):
    """Error de entrada que puede mostrarse directamente al usuario."""


class Backend(object):
    __slots__ = ("node", "name", "driver", "bus", "address", "origin")

    def __init__(self, node, name, driver, bus, address, origin):
        self.node = node
        self.name = name
        self.driver = driver
        self.bus = bus
        self.address = address
        self.origin = origin


class Signal(object):
    __slots__ = (
        "node", "name", "role", "pin", "backend", "channel", "pullup",
        "active_low", "debounce_ms", "filter", "safe", "origin",
    )

    def __init__(self, node, name, role, pin=None, backend=None, channel=None,
                 pullup=None, active_low=None, debounce_ms=None, filter_value=None,
                 safe=None, origin=""):
        self.node = node
        self.name = name
        self.role = role
        self.pin = pin
        self.backend = backend
        self.channel = channel
        self.pullup = pullup
        self.active_low = active_low
        self.debounce_ms = debounce_ms
        self.filter = filter_value
        self.safe = safe
        self.origin = origin

    @property
    def is_backend(self):
        return self.backend is not None

    @property
    def target(self):
        if self.is_backend:
            return "%s.%d" % (self.backend, self.channel)
        return self.pin


class HardwareModel(object):
    __slots__ = (
        "node", "board", "signals", "backends", "source_path",
        "source_format", "warnings",
    )

    def __init__(self, node, board, signals, backends, source_path,
                 source_format, warnings=None):
        self.node = node
        self.board = board
        self.signals = signals
        self.backends = backends
        self.source_path = source_path
        self.source_format = source_format
        self.warnings = list(warnings or [])


def _where(origin, message):
    return "%s: %s" % (origin, message) if origin else message


def _require_mapping(value, origin):
    if not isinstance(value, dict):
        raise ConfigError(_where(origin, "se esperaba un objeto JSON"))
    return value


def _text(value):
    if value is None:
        return ""
    return str(value).strip()


def _pick(mapping, *names):
    for name in names:
        if name in mapping:
            return mapping[name]
    return None


def parse_bool(value, field, origin, default=None):
    if value is None or (isinstance(value, str) and not value.strip()):
        if default is None:
            raise ConfigError(_where(origin, "falta el booleano '%s'" % field))
        return bool(default)
    if isinstance(value, bool):
        return value
    if isinstance(value, int) and value in (0, 1):
        return bool(value)
    token = str(value).strip().lower()
    if token in ("true", "1", "yes", "si", "on"):
        return True
    if token in ("false", "0", "no", "off"):
        return False
    raise ConfigError(_where(
        origin, "'%s' no es un booleano valido para %s" % (value, field)))


def parse_int(value, field, origin, minimum=None, maximum=None, default=None):
    if value is None or (isinstance(value, str) and not value.strip()):
        if default is None:
            raise ConfigError(_where(origin, "falta el entero '%s'" % field))
        result = int(default)
    else:
        try:
            if isinstance(value, bool):
                raise ValueError()
            result = int(str(value).strip(), 0)
        except (TypeError, ValueError):
            raise ConfigError(_where(
                origin, "'%s' no es un entero valido para %s" % (value, field)))
    if minimum is not None and result < minimum:
        raise ConfigError(_where(
            origin, "%s debe ser >= %d" % (field, minimum)))
    if maximum is not None and result > maximum:
        raise ConfigError(_where(
            origin, "%s debe ser <= %d" % (field, maximum)))
    return result


def validate_identifier(value, field, origin, sanitize=False):
    name = sanitize_identifier(_text(value)) if sanitize else _text(value)
    if not name:
        raise ConfigError(_where(origin, "falta '%s'" % field))
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        raise ConfigError(_where(
            origin, "'%s' no es un identificador C++ valido para %s" %
            (name, field)))
    if name in CPP_KEYWORDS:
        raise ConfigError(_where(
            origin, "'%s' es una palabra reservada de C++" % name))
    return name


def sanitize_identifier(name):
    value = re.sub(r"[^0-9a-zA-Z_]", "_", _text(name))
    value = re.sub(r"_+", "_", value).strip("_")
    if not value:
        value = "senal"
    if value[0].isdigit():
        value = "S_" + value
    if value in CPP_KEYWORDS:
        value += "_senal"
    return value


def strip_role_prefix(name):
    upper = name.upper()
    for prefix, _role in ROLE_PREFIXES:
        if upper.startswith(prefix):
            return name[len(prefix):]
    return name


def role_from_prefix(name):
    upper = name.upper()
    for prefix, role in ROLE_PREFIXES:
        if upper.startswith(prefix):
            return role
    return None


def role_from_type(part_type):
    if part_type in DIGITAL_INPUTS:
        return "DI"
    if part_type in DIGITAL_OUTPUTS or part_type in PWM_OUTPUTS:
        return "DO"
    if part_type in ANALOG_INPUTS:
        return "AI"
    return None


def normalize_pin(token):
    """Devuelve la expresion Arduino de un pin nativo o ``None``."""
    value = _text(token).upper()
    value = value.split(".")[0]
    if value in POWER_PINS:
        return None
    if re.fullmatch(r"\d+", value):
        return str(int(value, 10))
    if re.fullmatch(r"A\d+", value):
        return value
    match = re.fullmatch(r"(?:D|GP|GPIO)(\d+)", value)
    if match:
        return str(int(match.group(1), 10))
    return None


def parse_target(value, origin):
    target = _text(value)
    if not target:
        raise ConfigError(_where(origin, "falta 'target'"))

    native = target
    if target.lower().startswith("gpio."):
        native = target[5:]
    pin = normalize_pin(native)
    if pin is not None:
        return pin, None, None

    match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\.(\d+)", target)
    if not match:
        raise ConfigError(_where(
            origin,
            "target '%s' no es valido; usa gpio.2, A0 o BACKEND.3" % target))
    backend = validate_identifier(match.group(1), "backend", origin)
    channel = parse_int(match.group(2), "channel", origin, 0, 255)
    return None, backend, channel


def load_json(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            value = json.load(handle)
    except OSError as exc:
        raise ConfigError("no se puede leer %s: %s" % (path, exc))
    except ValueError as exc:
        raise ConfigError("JSON invalido en %s: %s" % (path, exc))
    return _require_mapping(value, path)


def load_config(path):
    if not path or not os.path.exists(path):
        return {}
    return load_json(path)


def _node_definitions(value, origin):
    """Normaliza ``nodes`` a ``{id: {board: ...}}``.

    Los arrays ``signals`` y ``backends`` anidados se conservan para que el
    cargador JSON pueda expandirlos despues.
    """
    result = {}
    if value is None:
        return result
    if isinstance(value, dict):
        items = []
        for node_id, body in value.items():
            if isinstance(body, str):
                body = {"board": body}
            body = dict(_require_mapping(body, "%s.nodes.%s" % (origin, node_id)))
            body.setdefault("id", node_id)
            items.append(body)
    elif isinstance(value, list):
        items = value
    else:
        raise ConfigError(_where(origin, "'nodes' debe ser un array u objeto"))

    for index, item in enumerate(items, 1):
        item_origin = "%s.nodes[%d]" % (origin, index)
        if isinstance(item, str):
            item = {"id": item}
        item = dict(_require_mapping(item, item_origin))
        node_id = _text(_pick(item, "id", "name", "node"))
        if not node_id:
            raise ConfigError(_where(item_origin, "falta id del nodo"))
        if node_id in result:
            raise ConfigError(_where(item_origin, "nodo '%s' repetido" % node_id))
        result[node_id] = item
    return result


def _merge_node_definitions(first, second):
    merged = dict(first)
    for node_id, body in second.items():
        if node_id not in merged:
            merged[node_id] = body
            continue
        old_board = _text(merged[node_id].get("board"))
        new_board = _text(body.get("board"))
        if old_board and new_board and old_board != new_board:
            raise ConfigError(
                "el nodo '%s' declara dos placas: %s y %s" %
                (node_id, old_board, new_board))
        combined = dict(merged[node_id])
        combined.update(body)
        merged[node_id] = combined
    return merged


def _records_from_nodes(node_defs, key, origin):
    records = []
    for node_id, body in node_defs.items():
        nested = body.get(key, [])
        if nested is None:
            continue
        if not isinstance(nested, list):
            raise ConfigError(_where(
                "%s.nodes.%s" % (origin, node_id), "'%s' debe ser un array" % key))
        for index, record in enumerate(nested, 1):
            row = dict(_require_mapping(
                record, "%s.nodes.%s.%s[%d]" % (origin, node_id, key, index)))
            declared = _text(row.get("node"))
            if declared and declared != node_id:
                raise ConfigError(_where(
                    origin, "un elemento anidado en '%s' declara node='%s'" %
                    (node_id, declared)))
            row["node"] = node_id
            row["__origin"] = "%s.nodes.%s.%s[%d]" % (origin, node_id, key, index)
            records.append(row)
    return records


def _top_level_records(data, key, origin):
    value = data.get(key, [])
    if value is None:
        return []
    if not isinstance(value, list):
        raise ConfigError(_where(origin, "'%s' debe ser un array" % key))
    records = []
    for index, record in enumerate(value, 1):
        row = dict(_require_mapping(record, "%s.%s[%d]" % (origin, key, index)))
        row["__origin"] = "%s.%s[%d]" % (origin, key, index)
        records.append(row)
    return records


def _select_node(node_defs, signal_records, backend_records, selected, origin):
    candidates = set(node_defs)
    for record in signal_records + backend_records:
        node = _text(record.get("node"))
        if node:
            candidates.add(node)

    if selected:
        if candidates and selected not in candidates:
            raise ConfigError(_where(
                origin, "el nodo '%s' no existe; disponibles: %s" %
                (selected, ", ".join(sorted(candidates)))))
        chosen = selected
    elif len(candidates) > 1:
        raise ConfigError(_where(
            origin,
            "hay varios nodos/placas (%s); selecciona uno con --node" %
            ", ".join(sorted(candidates))))
    elif candidates:
        chosen = next(iter(candidates))
    else:
        chosen = "default"

    if len(candidates) > 1:
        for record in signal_records + backend_records:
            if not _text(record.get("node")):
                raise ConfigError(_where(
                    record.get("__origin", origin),
                    "falta 'node' porque el archivo contiene varios nodos"))
    return chosen


def _record_node_matches(record, chosen, candidates_count):
    node = _text(record.get("node"))
    if node:
        return node == chosen
    return candidates_count <= 1


def _parse_backend(record, chosen):
    origin = record.get("__origin", "backend")
    node = _text(record.get("node")) or chosen
    name = validate_identifier(_pick(record, "name", "id"), "name", origin)
    driver_raw = _text(_pick(record, "driver", "type"))
    driver_key = re.sub(r"[^a-z0-9]", "", driver_raw.lower())
    if driver_key != "mcp23017":
        raise ConfigError(_where(
            origin, "backend '%s' no soportado; por ahora usa MCP23017" %
            driver_raw))
    bus = validate_identifier(_pick(record, "bus") or "Wire", "bus", origin)
    address = parse_int(record.get("address"), "address", origin, 0x20, 0x27)
    return Backend(node, name, "Mcp23017Backend", bus, address, origin)


def _nonempty(record, *names):
    return any(name in record and _text(record.get(name)) != "" for name in names)


def _parse_signal(record, chosen, defaults, backends, sanitize_name=False):
    origin = record.get("__origin", "signal")
    node = _text(record.get("node")) or chosen
    name = validate_identifier(record.get("name"), "name", origin, sanitize_name)
    role = _text(record.get("role")).upper()
    if role not in ROLES:
        raise ConfigError(_where(
            origin, "role '%s' no valido; usa DI, DO o AI" % role))

    pin, backend_name, channel = parse_target(record.get("target"), origin)
    if backend_name:
        if backend_name not in backends:
            raise ConfigError(_where(
                origin, "target '%s' usa un backend no declarado" %
                record.get("target")))
        backend = backends[backend_name]
        if channel > 15:
            raise ConfigError(_where(
                origin, "MCP23017 solo tiene canales 0..15 (recibido %d)" % channel))
        if role == "AI":
            raise ConfigError(_where(
                origin, "MCP23017 no admite entradas analogicas"))

    default_debounce = _pick(defaults, "debounce_ms", "debounce")
    if default_debounce is None:
        default_debounce = 20

    if role == "DI":
        if _nonempty(record, "active_low", "activeLow", "filter", "safe"):
            raise ConfigError(_where(
                origin, "DI no admite active_low, filter ni safe"))
        pullup = parse_bool(
            _pick(record, "pullup"), "pullup", origin,
            parse_bool(defaults.get("pullup"), "defaults.pullup", origin, True))
        debounce = parse_int(
            _pick(record, "debounce_ms", "debounce"), "debounce_ms", origin,
            0, 65535, default_debounce)
        return Signal(node, name, role, pin, backend_name, channel,
                      pullup=pullup, debounce_ms=debounce, origin=origin)

    if role == "DO":
        if _nonempty(record, "pullup", "debounce_ms", "debounce", "filter"):
            raise ConfigError(_where(
                origin, "DO no admite pullup, debounce_ms ni filter"))
        active_default = parse_bool(
            _pick(defaults, "active_low", "activeLow"),
            "defaults.active_low", origin, False)
        active_low = parse_bool(
            _pick(record, "active_low", "activeLow"),
            "active_low", origin, active_default)
        safe_default = parse_bool(defaults.get("safe"), "defaults.safe", origin, False)
        safe = parse_bool(record.get("safe"), "safe", origin, safe_default)
        return Signal(node, name, role, pin, backend_name, channel,
                      active_low=active_low, safe=safe, origin=origin)

    if _nonempty(record, "pullup", "active_low", "activeLow", "debounce_ms",
                 "debounce", "safe"):
        raise ConfigError(_where(
            origin, "AI solo admite la opcion filter"))
    filter_default = parse_int(defaults.get("filter"), "defaults.filter", origin,
                               0, 8, 3)
    filter_value = parse_int(record.get("filter"), "filter", origin,
                             0, 8, filter_default)
    return Signal(node, name, role, pin, backend_name, channel,
                  filter_value=filter_value, origin=origin)


def _build_explicit_model(signal_records, backend_records, node_defs, defaults,
                          source_path, source_format, selected_node=None,
                          config_board=None):
    candidates = set(node_defs)
    for record in signal_records + backend_records:
        node = _text(record.get("node"))
        if node:
            candidates.add(node)
    chosen = _select_node(
        node_defs, signal_records, backend_records, selected_node, source_path)
    candidate_count = len(candidates)

    chosen_backend_records = [
        record for record in backend_records
        if _record_node_matches(record, chosen, candidate_count)
    ]
    chosen_signal_records = [
        record for record in signal_records
        if _record_node_matches(record, chosen, candidate_count)
    ]

    backends = []
    backend_by_name = {}
    addresses = {}
    for record in chosen_backend_records:
        backend = _parse_backend(record, chosen)
        if backend.name in backend_by_name:
            raise ConfigError(_where(
                backend.origin, "backend '%s' repetido" % backend.name))
        address_key = (backend.bus, backend.address)
        if address_key in addresses:
            raise ConfigError(_where(
                backend.origin,
                "%s y %s usan la misma direccion %s/0x%02X" %
                (addresses[address_key], backend.name, backend.bus, backend.address)))
        addresses[address_key] = backend.name
        backend_by_name[backend.name] = backend
        backends.append(backend)

    signals = []
    names = set(backend_by_name)
    targets = {}
    for record in chosen_signal_records:
        signal = _parse_signal(record, chosen, defaults, backend_by_name)
        if signal.name in names:
            raise ConfigError(_where(
                signal.origin, "nombre C++ '%s' repetido" % signal.name))
        names.add(signal.name)
        target_key = signal.target.lower() if not signal.is_backend else signal.target
        if target_key in targets:
            raise ConfigError(_where(
                signal.origin, "target '%s' usado tambien por '%s'" %
                (signal.target, targets[target_key])))
        targets[target_key] = signal.name
        signals.append(signal)

    node_body = node_defs.get(chosen, {})
    board = _text(node_body.get("board")) or _text(config_board) or chosen
    return HardwareModel(chosen, board, signals, backends, source_path,
                         source_format)


def parse_csv_source(path, config, selected_node=None):
    try:
        with open(path, "r", encoding="utf-8-sig", newline="") as handle:
            reader = csv.DictReader(handle)
            if reader.fieldnames is None:
                raise ConfigError("%s: CSV vacio" % path)
            fields = [field.strip() if field else "" for field in reader.fieldnames]
            missing = [field for field in CSV_FIELDS if field not in fields]
            if missing:
                raise ConfigError(
                    "%s: faltan columnas CSV: %s" % (path, ", ".join(missing)))
            reader.fieldnames = fields
            records = []
            for line, row in enumerate(reader, 2):
                if None in row:
                    raise ConfigError(
                        "%s:%d: hay mas valores que columnas" % (path, line))
                cleaned = {key: _text(value) for key, value in row.items()}
                if not any(cleaned.values()):
                    continue
                cleaned["__origin"] = "%s:%d" % (path, line)
                records.append(cleaned)
    except OSError as exc:
        raise ConfigError("no se puede leer %s: %s" % (path, exc))

    config_nodes = _node_definitions(config.get("nodes"), "corefsm.json")
    backend_records = _top_level_records(config, "backends", "corefsm.json")
    backend_records += _records_from_nodes(config_nodes, "backends", "corefsm.json")
    return _build_explicit_model(
        records, backend_records, config_nodes, config.get("defaults", {}),
        path, "csv", selected_node, config.get("board"))


def parse_json_source(path, config, selected_node=None):
    data = load_json(path)
    source_nodes = _node_definitions(data.get("nodes"), path)
    config_nodes = _node_definitions(config.get("nodes"), "corefsm.json")
    node_defs = _merge_node_definitions(config_nodes, source_nodes)

    signals = _top_level_records(data, "signals", path)
    signals += _records_from_nodes(source_nodes, "signals", path)
    backends = _top_level_records(data, "backends", path)
    backends += _records_from_nodes(source_nodes, "backends", path)
    backends += _top_level_records(config, "backends", "corefsm.json")
    backends += _records_from_nodes(config_nodes, "backends", "corefsm.json")

    top_node = _text(data.get("node"))
    if top_node:
        for record in signals + backends:
            record.setdefault("node", top_node)
        node_defs.setdefault(top_node, {"id": top_node, "board": data.get("board")})

    defaults = dict(data.get("defaults", {}))
    defaults.update(config.get("defaults", {}))
    board = config.get("board") or data.get("board")
    return _build_explicit_model(
        signals, backends, node_defs, defaults, path, "json",
        selected_node, board)


def parse_wokwi_source(path, config, selected_node=None):
    diagram = load_json(path)
    parts = {}
    for part in diagram.get("parts", []):
        if not isinstance(part, dict):
            raise ConfigError("%s: cada elemento de parts debe ser un objeto" % path)
        part_id = _text(part.get("id"))
        if part_id:
            parts[part_id] = _text(part.get("type"))

    boards = [
        part_id for part_id, part_type in parts.items()
        if any(part_type.startswith(hint) for hint in BOARD_HINTS)
    ]
    legacy_board = _text(config.get("board"))
    chosen = selected_node or legacy_board
    if chosen:
        if chosen not in boards:
            raise ConfigError(
                "%s: la placa/nodo '%s' no existe; disponibles: %s" %
                (path, chosen, ", ".join(sorted(boards)) or "ninguna"))
    elif len(boards) > 1:
        raise ConfigError(
            "%s: hay varias placas (%s); selecciona una con --node" %
            (path, ", ".join(sorted(boards))))
    elif not boards:
        raise ConfigError(
            "%s: no se ha encontrado ninguna placa Wokwi" % path)
    else:
        chosen = boards[0]

    ignore = set(_text(item) for item in config.get("ignore", []))
    pin_overrides = {str(key): value for key, value in config.get("pins", {}).items()}
    defaults = config.get("defaults", {})

    links = []
    connections = diagram.get("connections", [])
    if not isinstance(connections, list):
        raise ConfigError("%s: connections debe ser un array" % path)
    for index, connection in enumerate(connections, 1):
        if not isinstance(connection, list) or len(connection) < 2:
            raise ConfigError(
                "%s.connections[%d]: conexion invalida" % (path, index))
        first, second = str(connection[0]), str(connection[1])
        if ":" not in first or ":" not in second:
            raise ConfigError(
                "%s.connections[%d]: falta ':' en un extremo" % (path, index))
        part_a, pin_a = first.split(":", 1)
        part_b, pin_b = second.split(":", 1)
        if part_a == chosen and part_b != chosen:
            board_pin, component, component_pin = pin_a, part_b, pin_b
        elif part_b == chosen and part_a != chosen:
            board_pin, component, component_pin = pin_b, part_a, pin_a
        else:
            continue
        pin = normalize_pin(board_pin)
        if pin is None or component in ignore:
            continue
        links.append((pin, component, component_pin,
                      "%s.connections[%d]" % (path, index)))

    per_component = {}
    for pin, component, component_pin, _origin in links:
        per_component.setdefault(component, []).append((pin, component_pin))

    warnings = []
    records = []
    for pin, component, component_pin, origin in links:
        override = pin_overrides.get("%s.%s" % (chosen, pin),
                                     pin_overrides.get(pin, {}))
        if not isinstance(override, dict):
            raise ConfigError(_where(origin, "override del pin debe ser un objeto"))
        part_type = parts.get(component, "")
        role = (_pick(override, "role") or role_from_prefix(component) or
                role_from_type(part_type))
        if role is None:
            warnings.append(_where(
                origin,
                "componente '%s' de tipo '%s' no reconocido; se omite" %
                (component, part_type)))
            continue

        if "name" in override:
            name = sanitize_identifier(override["name"])
        else:
            base = sanitize_identifier(strip_role_prefix(component))
            if len(per_component[component]) > 1:
                name = "%s_%s" % (base, sanitize_identifier(component_pin))
            else:
                name = base

        record = {
            "node": chosen,
            "name": name,
            "role": _text(role).upper(),
            "target": pin,
            "__origin": origin,
        }
        if record["role"] == "DI":
            record["pullup"] = _pick(override, "pullup")
            record["debounce_ms"] = _pick(override, "debounce_ms", "debounce")
        elif record["role"] == "DO":
            record["active_low"] = _pick(override, "active_low", "activeLow")
            record["safe"] = override.get("safe")
        elif record["role"] == "AI":
            record["filter"] = override.get("filter")
        records.append(record)

    # El formato Wokwi no declara backends de E/S; los definidos en
    # corefsm.json quedan disponibles para futuras fuentes, pero ninguna senal
    # directa del diagrama los referencia implicitamente.
    config_nodes = _node_definitions(config.get("nodes"), "corefsm.json")
    config_nodes.setdefault(chosen, {"id": chosen, "board": chosen})
    backend_records = _top_level_records(config, "backends", "corefsm.json")
    backend_records += _records_from_nodes(config_nodes, "backends", "corefsm.json")
    model = _build_explicit_model(
        records, backend_records, config_nodes, defaults, path, "wokwi",
        chosen, chosen)
    model.warnings.extend(warnings)

    def pin_sort(signal):
        pin = signal.pin
        return (1, int(pin[1:])) if pin.startswith("A") else (0, int(pin))

    model.signals.sort(key=pin_sort)
    return model


def _macro_rows(items, formatter):
    if not items:
        return "  /* (ninguna) */"
    lines = []
    for index, item in enumerate(items):
        suffix = "" if index == len(items) - 1 else " \\"
        lines.append(formatter(item) + suffix)
    return "\n".join(lines)


LEGACY_WOKWI_HEADER = '''/* =============================================================================
 *  HardwareConfig.h  -  GENERADO AUTOMATICAMENTE. NO EDITAR A MANO.
 * -----------------------------------------------------------------------------
 *  Origen : {src}
 *  Placa  : {board}
 *  Senales: {ndi} entradas digitales, {ndo} salidas digitales, {nai} analogicas
 *
 *  Este archivo lo reescribe wokwi2corefsm.py cada vez que compilas. Cualquier
 *  cambio hecho aqui se perdera. Para modificar la asignacion de hardware:
 *
 *    - mueve el cable o renombra el componente en Wokwi (diagram.json), o
 *    - ajusta las opciones finas en corefsm.json
 *
 *  Cada fila de las tablas de abajo se convierte en un objeto completo, con su
 *  antirrebote, sus flancos y su capacidad de forzado:
 *
 *      HW.{example}
 * ========================================================================== */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

/* -----------------------------------------------------------------------------
 *  ENTRADAS DIGITALES (%I)
 *        PIN | NOMBRE SIMBOLICO                | PULL-UP | ANTIRREBOTE (ms)
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_DI(ROW) \\
{di_rows}

/* -----------------------------------------------------------------------------
 *  SALIDAS DIGITALES (%Q)
 *        PIN | NOMBRE SIMBOLICO                | ACTIVA A NIVEL BAJO
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_DO(ROW) \\
{do_rows}

/* -----------------------------------------------------------------------------
 *  ENTRADAS ANALOGICAS (%IW)
 *        PIN | NOMBRE SIMBOLICO                | FILTRO (0 = crudo .. 8 = muy suave)
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_AI(ROW) \\
{ai_rows}

/* Expande las tablas: declara los objetos, los registra y genera la imagen de
 * proceso. Tiene que ir DESPUES de las tres tablas. */
#include <io/IOTable.h>

#endif /* HARDWARE_CONFIG_H */
'''


def _signal_groups(model):
    return {
        "di": [s for s in model.signals if s.role == "DI" and not s.is_backend],
        "di_backend": [s for s in model.signals if s.role == "DI" and s.is_backend],
        "do": [s for s in model.signals
               if s.role == "DO" and not s.is_backend and not s.safe],
        "do_safe": [s for s in model.signals
                    if s.role == "DO" and not s.is_backend and s.safe],
        "do_backend": [s for s in model.signals if s.role == "DO" and s.is_backend],
        "ai": [s for s in model.signals if s.role == "AI"],
    }


def _example(groups):
    for key, suffix in (
            ("di", ".hasRisen()"), ("di_backend", ".hasRisen()"),
            ("do", ".turnOn()"), ("do_safe", ".turnOn()"),
            ("do_backend", ".turnOn()"), ("ai", ".value()")):
        if groups[key]:
            return groups[key][0].name + suffix
    return "(sin senales)"


def emit_legacy_wokwi_header(model):
    groups = _signal_groups(model)
    if model.backends or groups["di_backend"] or groups["do_backend"] or groups["do_safe"]:
        return emit_header(model)
    return LEGACY_WOKWI_HEADER.format(
        src=os.path.basename(model.source_path), board=model.board,
        ndi=len(groups["di"]), ndo=len(groups["do"]), nai=len(groups["ai"]),
        di_rows=_macro_rows(
            groups["di"], lambda s: "  ROW( %4s, %-32s, %-5s, %3d )" %
            (s.pin, s.name, str(s.pullup).lower(), s.debounce_ms)),
        do_rows=_macro_rows(
            groups["do"], lambda s: "  ROW( %4s, %-32s, %-5s )" %
            (s.pin, s.name, str(s.active_low).lower())),
        ai_rows=_macro_rows(
            groups["ai"], lambda s: "  ROW( %4s, %-32s, %d )" %
            (s.pin, s.name, s.filter)),
        example=_example(groups))


def _append_macro(lines, name, rows):
    lines.append("#define %s(ROW) \\" % name)
    lines.extend(rows.splitlines())
    lines.append("")


def emit_header(model):
    groups = _signal_groups(model)
    di_count = len(groups["di"]) + len(groups["di_backend"])
    do_count = len(groups["do"]) + len(groups["do_safe"]) + len(groups["do_backend"])
    ai_count = len(groups["ai"])

    lines = [
        "/* =============================================================================",
        " *  HardwareConfig.h  -  GENERADO AUTOMATICAMENTE. NO EDITAR A MANO.",
        " * -----------------------------------------------------------------------------",
        " *  Origen : %s (%s)" % (os.path.basename(model.source_path), model.source_format),
        " *  Nodo   : %s" % model.node,
        " *  Placa  : %s" % model.board,
        " *  Senales: %d entradas digitales, %d salidas digitales, %d analogicas" %
        (di_count, do_count, ai_count),
        " *",
        " *  Edita la fuente indicada arriba y vuelve a generar. La API del programa",
        " *  permanece estable: HW.Nombre_De_La_Senal.",
        " *",
        " *      HW.%s" % _example(groups),
        " * ========================================================================== */",
        "",
        "#ifndef HARDWARE_CONFIG_H",
        "#define HARDWARE_CONFIG_H",
        "",
        "#include <CoreFSM.h>",
    ]
    if model.backends:
        lines.append("#include <io/Mcp23017Backend.h>")
    lines.append("")

    if model.backends:
        _append_macro(lines, "CFSM_TABLE_BACKEND", _macro_rows(
            model.backends,
            lambda b: "  ROW(Mcp23017Backend, %s, %s, 0x%02X)" %
            (b.name, b.bus, b.address)))

    lines.extend([
        "/* GPIO nativo: entradas digitales. */",
    ])
    _append_macro(lines, "CFSM_TABLE_DI", _macro_rows(
        groups["di"], lambda s: "  ROW( %4s, %-32s, %-5s, %3d )" %
        (s.pin, s.name, str(s.pullup).lower(), s.debounce_ms)))

    if groups["di_backend"]:
        _append_macro(lines, "CFSM_TABLE_DI_BACKEND", _macro_rows(
            groups["di_backend"],
            lambda s: "  ROW(%s, %2d, %-32s, %-5s, %3d)" %
            (s.backend, s.channel, s.name, str(s.pullup).lower(), s.debounce_ms)))

    lines.append("/* GPIO nativo: salidas digitales con estado seguro false. */")
    _append_macro(lines, "CFSM_TABLE_DO", _macro_rows(
        groups["do"], lambda s: "  ROW( %4s, %-32s, %-5s )" %
        (s.pin, s.name, str(s.active_low).lower())))

    if groups["do_safe"]:
        _append_macro(lines, "CFSM_TABLE_DO_SAFE", _macro_rows(
            groups["do_safe"],
            lambda s: "  ROW( %4s, %-32s, %-5s, %-5s )" %
            (s.pin, s.name, str(s.active_low).lower(), str(s.safe).lower())))

    if groups["do_backend"]:
        _append_macro(lines, "CFSM_TABLE_DO_BACKEND", _macro_rows(
            groups["do_backend"],
            lambda s: "  ROW(%s, %2d, %-32s, %-5s, %-5s)" %
            (s.backend, s.channel, s.name, str(s.active_low).lower(),
             str(s.safe).lower())))

    lines.append("/* GPIO nativo: entradas analogicas. */")
    _append_macro(lines, "CFSM_TABLE_AI", _macro_rows(
        groups["ai"], lambda s: "  ROW( %4s, %-32s, %d )" %
        (s.pin, s.name, s.filter)))

    lines.extend([
        "#include <io/IOTable.h>",
        "",
        "#endif /* HARDWARE_CONFIG_H */",
        "",
    ])
    return "\n".join(lines)


def infer_format(path, explicit=None):
    if explicit and explicit != "auto":
        value = explicit.lower()
        if value not in ("csv", "json", "wokwi"):
            raise ConfigError("formato '%s' no soportado" % explicit)
        return value
    name = os.path.basename(path).lower()
    if name == "diagram.json":
        return "wokwi"
    if name.endswith(".csv"):
        return "csv"
    if name.endswith(".json"):
        return "json"
    raise ConfigError(
        "no se puede deducir el formato de %s; usa --format" % path)


def resolve_project_source(project_dir, config, explicit_input=None,
                           explicit_format=None):
    project_dir = os.path.abspath(project_dir)
    if explicit_input:
        path = explicit_input
        if not os.path.isabs(path):
            path = os.path.join(project_dir, path)
        if not os.path.isfile(path):
            raise ConfigError("no existe la fuente de hardware: %s" % path)
        return path, infer_format(path, explicit_format), None

    source = config.get("source")
    source_node = None
    if source:
        if isinstance(source, str):
            source_path = source
            source_format = explicit_format
        elif isinstance(source, dict):
            source_path = _pick(source, "path", "file")
            source_format = explicit_format or source.get("format")
            source_node = _text(source.get("node")) or None
        else:
            raise ConfigError("corefsm.json: 'source' debe ser texto u objeto")
        if not source_path:
            format_name = _text(source_format).lower()
            defaults = {
                "csv": "hardware.csv", "json": "hardware.json",
                "wokwi": "diagram.json",
            }
            source_path = defaults.get(format_name)
        if not source_path:
            raise ConfigError("corefsm.json: source necesita path o format")
        path = source_path
        if not os.path.isabs(path):
            path = os.path.join(project_dir, path)
        if not os.path.isfile(path):
            raise ConfigError("corefsm.json.source no existe: %s" % path)
        return path, infer_format(path, source_format), source_node

    candidates = (
        ("hardware.csv", "csv"),
        ("hardware.json", "json"),
        ("diagram.json", "wokwi"),
    )
    for filename, format_name in candidates:
        path = os.path.join(project_dir, filename)
        if os.path.isfile(path):
            return path, format_name, None
    raise ConfigError(
        "%s: no hay hardware.csv, hardware.json ni diagram.json" % project_dir)


def load_model(input_path, source_format, config=None, node=None):
    config = config or {}
    if source_format == "csv":
        return parse_csv_source(input_path, config, node)
    if source_format == "json":
        return parse_json_source(input_path, config, node)
    if source_format == "wokwi":
        return parse_wokwi_source(input_path, config, node)
    raise ConfigError("formato '%s' no soportado" % source_format)


def _print_summary(model):
    counts = {role: sum(1 for signal in model.signals if signal.role == role)
              for role in ROLES}
    print("[CoreFSM] Nodo '%s', placa '%s': %d DI, %d DO, %d AI" %
          (model.node, model.board, counts["DI"], counts["DO"], counts["AI"]))
    for backend in model.backends:
        print("          BUS  %-4s %-20s %s/0x%02X" %
              ("MCP", backend.name, backend.bus, backend.address))
    for signal in model.signals:
        print("          %-3s  %-4s %-28s %s" %
              (signal.target, signal.role, signal.name,
               "backend" if signal.is_backend else "gpio"))
    if model.warnings:
        print("[CoreFSM] AVISOS:")
        for warning in model.warnings:
            print("  " + warning)


def generate(project_dir=None, input_path=None, output_path=None,
             source_format=None, config_path=None, node=None,
             check_only=False, quiet=False, legacy_wokwi=False):
    project_dir = os.path.abspath(project_dir or os.getcwd())
    if config_path is None:
        config_path = os.path.join(project_dir, "corefsm.json")
    elif not os.path.isabs(config_path):
        config_path = os.path.join(project_dir, config_path)
    config = load_config(config_path)
    resolved_path, resolved_format, source_node = resolve_project_source(
        project_dir, config, input_path, source_format)
    selected_node = node or source_node
    model = load_model(resolved_path, resolved_format, config, selected_node)

    if not quiet:
        _print_summary(model)
    if check_only:
        return 1 if model.warnings else 0

    if output_path is None:
        output_path = os.path.join(project_dir, "include", "HardwareConfig.h")
    elif not os.path.isabs(output_path):
        output_path = os.path.join(project_dir, output_path)

    header = (emit_legacy_wokwi_header(model)
              if legacy_wokwi and resolved_format == "wokwi"
              else emit_header(model))
    directory = os.path.dirname(output_path)
    if directory and not os.path.isdir(directory):
        os.makedirs(directory)

    if os.path.exists(output_path):
        with open(output_path, "r", encoding="utf-8") as handle:
            if handle.read() == header:
                if not quiet:
                    print("[CoreFSM] %s ya estaba al dia." % output_path)
                return 0
    with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(header)
    if not quiet:
        print("[CoreFSM] Escrito %s" % output_path)
    return 0


def build_arg_parser():
    parser = argparse.ArgumentParser(
        description="Genera HardwareConfig.h desde CSV, JSON o Wokwi.")
    parser.add_argument("--project", default=None,
                        help="carpeta del proyecto (por defecto, la actual)")
    parser.add_argument("-i", "--input", default=None,
                        help="fuente explicita; si se omite se descubre sola")
    parser.add_argument("-o", "--output", default=None,
                        help="HardwareConfig.h de salida")
    parser.add_argument("--format", choices=("auto", "csv", "json", "wokwi"),
                        default="auto")
    parser.add_argument("--config", default=None,
                        help="corefsm.json alternativo")
    parser.add_argument("--node", default=None,
                        help="nodo/placa a generar cuando hay varios")
    parser.add_argument("--check", action="store_true",
                        help="valida sin escribir")
    parser.add_argument("-q", "--quiet", action="store_true")
    return parser


def cli(argv=None):
    args = build_arg_parser().parse_args(argv)
    project = os.path.abspath(args.project or os.getcwd())
    try:
        return generate(
            project_dir=project, input_path=args.input,
            output_path=args.output,
            source_format=None if args.format == "auto" else args.format,
            config_path=args.config, node=args.node,
            check_only=args.check, quiet=args.quiet)
    except ConfigError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 2


def _platformio_entry():
    Import("env")  # noqa: F821 - lo inyecta SCons
    project = env.get("PROJECT_DIR")  # noqa: F821
    include = env.get("PROJECT_INCLUDE_DIR")  # noqa: F821
    try:
        selected_node = env.GetProjectOption("custom_corefsm_node", None)  # noqa: F821
    except Exception:
        selected_node = None
    try:
        code = generate(
            project_dir=project,
            output_path=os.path.join(include, "HardwareConfig.h"),
            node=selected_node)
    except ConfigError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        raise SystemExit(2)
    if code:
        raise SystemExit(code)


_EN_PLATFORMIO = ("Import" in globals()) or ("DefaultEnvironment" in globals())

if _EN_PLATFORMIO:
    _platformio_entry()
elif __name__ == "__main__":
    sys.exit(cli())
