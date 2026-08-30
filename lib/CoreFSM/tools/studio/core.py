"""Filesystem and generation layer for CoreFSM Studio.

Only the standard library is used so the application works with the Python
bundled by PlatformIO.  All paths are constrained to ``projects/`` and writes
are atomic; the HTTP layer never receives a raw command to execute.
"""

from __future__ import annotations

import copy
import csv
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading

from .catalog import BOARDS, DEVICE_TYPES, PRESETS, manifest_for_preset, public_catalog


MODEL_FILE = "corefsm.project.json"
EDITABLE_SUFFIXES = {".h", ".hpp", ".c", ".cpp", ".ino", ".json", ".csv", ".ini", ".md", ".txt"}
GENERATED_PREFIXES = ("include/generated/", "src/generated/")
GENERATED_FILES = {"include/HardwareConfig.h"}
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
PROJECT_RE = re.compile(r"^[A-Za-z0-9_-]+$")
PIN_RE = re.compile(r"^(?:A\d+|(?:GPIO|GP|D)?\d+)$", re.IGNORECASE)
INTEGER_RE = re.compile(r"^[+-]?(?:\d+|0[xX][0-9A-Fa-f]+)$")
FLOAT_RE = re.compile(r"^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[fF]?$|^[+-]?\d+[fF]$")
SAFE_INITIALIZER_RE = re.compile(r"^[A-Za-z0-9_+\-.,{}() ]*$")

# Cada rol acepta unos atributos y rechaza el resto; el generador lo comprueba y
# aborta.  Studio conoce la misma regla para no escribir nunca un CSV inválido y
# para poder apagar en la tabla las columnas que no aplican.
ROLE_FIELDS = {
    "DI": ("pullup", "debounce_ms"),
    "DO": ("active_low", "safe"),
    "AI": ("filter",),
}


class StudioError(Exception):
    def __init__(self, message, code="studio_error", status=400, details=None):
        super().__init__(message)
        self.message = message
        self.code = code
        self.status = status
        self.details = details or []

    def as_dict(self):
        return {"error": self.code, "message": self.message, "details": self.details}


def _repo_root(start):
    current = Path(start).resolve()
    for candidate in (current,) + tuple(current.parents):
        if (candidate / "lib" / "CoreFSM").is_dir():
            return candidate
    raise StudioError("No se encuentra la raíz de MFS-Arduino-Library", "repo_not_found", 500)


def _atomic_write(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = content.encode("utf-8") if isinstance(content, str) else content
    handle = tempfile.NamedTemporaryFile(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent), delete=False)
    temporary = Path(handle.name)
    try:
        with handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(str(temporary), str(path))
    finally:
        if temporary.exists():
            temporary.unlink()


def _json_text(value):
    return json.dumps(value, indent=2, ensure_ascii=False) + "\n"


def _revision(content):
    if isinstance(content, str):
        content = content.encode("utf-8")
    return hashlib.sha256(content).hexdigest()


def _bool_cell(value):
    if value in (None, ""):
        return ""
    return "true" if bool(value) else "false"


def _read_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except FileNotFoundError:
        return copy.deepcopy(default)
    except (OSError, ValueError) as exc:
        raise StudioError("JSON inválido en %s: %s" % (path, exc), "invalid_json", 422)


def _cpp_comment(value):
    return str(value or "").replace("\r", " ").replace("\n", " ").replace("*/", "* /").strip()


def _normalize_pin(pin):
    value = str(pin or "").strip().upper()
    if value.startswith("GPIO"):
        value = value[4:]
    elif value.startswith("GP"):
        value = value[2:]
    elif value.startswith("D") and value[1:].isdigit():
        value = value[1:]
    return value


class StudioWorkspace:
    def __init__(self, repo_root=None):
        self.root = _repo_root(repo_root or Path(__file__).resolve())
        self.projects_root = self.root / "projects"
        self.projects_root.mkdir(exist_ok=True)
        self._locks = {}
        self._locks_guard = threading.Lock()
        self._generator_module = None

    # ------------------------------------------------------------------
    # Discovery and safe paths
    # ------------------------------------------------------------------
    def _lock_for(self, project_name):
        with self._locks_guard:
            return self._locks.setdefault(project_name, threading.RLock())

    def _project_path(self, name, must_exist=True):
        if not isinstance(name, str) or not PROJECT_RE.fullmatch(name):
            raise StudioError("Nombre de proyecto no válido", "invalid_project_name", 422)
        candidate = (self.projects_root / name).resolve()
        try:
            candidate.relative_to(self.projects_root.resolve())
        except ValueError:
            raise StudioError("La ruta sale de projects/", "unsafe_path", 403)
        if must_exist and not candidate.is_dir():
            raise StudioError("No existe el proyecto '%s'" % name, "project_not_found", 404)
        return candidate

    def _file_path(self, project_name, relative, must_exist=True):
        project = self._project_path(project_name)
        relative = str(relative or "").replace("\\", "/").lstrip("/")
        candidate = (project / relative).resolve()
        try:
            candidate.relative_to(project.resolve())
        except ValueError:
            raise StudioError("La ruta sale del proyecto", "unsafe_path", 403)
        if must_exist and not candidate.is_file():
            raise StudioError("No existe el archivo '%s'" % relative, "file_not_found", 404)
        if candidate.suffix.lower() not in EDITABLE_SUFFIXES:
            raise StudioError("Tipo de archivo no permitido", "unsupported_file", 415)
        return candidate, relative

    def list_projects(self):
        result = []
        for directory in sorted(self.projects_root.iterdir(), key=lambda item: item.name.lower()):
            if not directory.is_dir():
                continue
            if not ((directory / "platformio.ini").exists() or (directory / MODEL_FILE).exists()):
                continue
            target = self._target_info(directory)
            manifest = _read_json(directory / MODEL_FILE, {}) or {}
            project_meta = manifest.get("project", {}) if isinstance(manifest, dict) else {}
            result.append({
                "id": directory.name,
                "name": project_meta.get("displayName") or directory.name,
                "board": target["board"],
                "boardLabel": BOARDS.get(target["board"], {}).get("label", target["board"]),
                "preset": project_meta.get("preset", "legacy"),
                "hasStudioModel": (directory / MODEL_FILE).exists(),
                "warnings": target["warnings"],
            })
        return result

    def bootstrap(self):
        value = public_catalog()
        value.update({
            "app": {"name": "CoreFSM Studio", "version": "0.1.0", "repository": str(self.root)},
            "projects": self.list_projects(),
        })
        return value

    # ------------------------------------------------------------------
    # Existing project import
    # ------------------------------------------------------------------
    def _target_info(self, directory):
        config = _read_json(directory / "corefsm.json", {}) or {}
        config_board = ""
        nodes = config.get("nodes", []) if isinstance(config, dict) else []
        if isinstance(nodes, list) and nodes:
            config_board = str(nodes[0].get("board", ""))
        elif isinstance(nodes, dict) and nodes:
            first = next(iter(nodes.values()))
            config_board = str(first.get("board", "")) if isinstance(first, dict) else str(first)

        pio_board = ""
        platform = ""
        ini_path = directory / "platformio.ini"
        if ini_path.exists():
            text = ini_path.read_text(encoding="utf-8", errors="replace")
            match = re.search(r"(?mi)^\s*board\s*=\s*([^;\s]+)", text)
            if match:
                pio_board = match.group(1).strip()
            match = re.search(r"(?mi)^\s*platform\s*=\s*([^;\s]+)", text)
            if match:
                platform = match.group(1).strip()
        by_pio = next((key for key, value in BOARDS.items() if value["pioBoard"] == pio_board), "")
        board = config_board if config_board in BOARDS else by_pio
        if not board:
            board = "nano"
        warnings = []
        if config_board and by_pio and config_board != by_pio:
            warnings.append("corefsm.json declara %s pero PlatformIO compila para %s" % (config_board, by_pio))
        expected = BOARDS.get(board)
        if expected and pio_board and expected["pioBoard"] != pio_board:
            warnings.append("La placa de PlatformIO (%s) no coincide con %s" % (pio_board, expected["label"]))
        return {"board": board, "pioBoard": pio_board, "platform": platform, "warnings": warnings}

    def _hardware_from_csv(self, directory):
        path = directory / "hardware.csv"
        if not path.exists():
            return []
        result = []
        try:
            with path.open("r", encoding="utf-8-sig", newline="") as handle:
                for row in csv.DictReader(handle):
                    result.append({
                        "name": (row.get("name") or "").strip(),
                        "role": (row.get("role") or "DI").strip().upper(),
                        "target": (row.get("target") or "").replace("gpio.", "").strip(),
                        "pullup": self._parse_optional_bool(row.get("pullup")),
                        "activeLow": self._parse_optional_bool(row.get("active_low")),
                        "debounceMs": self._parse_optional_int(row.get("debounce_ms")),
                        "filter": self._parse_optional_int(row.get("filter")),
                        "safe": self._parse_optional_bool(row.get("safe")),
                        "group": "E/S importada",
                        "description": "",
                    })
        except (OSError, csv.Error) as exc:
            raise StudioError("No se puede leer hardware.csv: %s" % exc, "invalid_hardware", 422)
        return result

    @staticmethod
    def _parse_optional_bool(value):
        text = str(value or "").strip().lower()
        if not text:
            return ""
        return text in ("true", "1", "yes", "si", "on")

    @staticmethod
    def _parse_optional_int(value):
        text = str(value or "").strip()
        if not text:
            return ""
        try:
            return int(text, 0)
        except ValueError:
            return text

    def _legacy_manifest(self, directory):
        manifest = manifest_for_preset("empty", directory.name)
        manifest["project"].update({"preset": "legacy", "description": "Proyecto existente importado sin modificar"})
        manifest["hardware"] = self._hardware_from_csv(directory)
        return manifest

    def load_project(self, name):
        directory = self._project_path(name)
        manifest = _read_json(directory / MODEL_FILE, None)
        if not isinstance(manifest, dict):
            manifest = self._legacy_manifest(directory)
        else:
            manifest = self._normalize_manifest(manifest)
        target = self._target_info(directory)
        manifest["board"] = target["board"]
        validation = self.validate_model(manifest, target_warnings=target["warnings"])
        return {
            "id": name,
            "model": manifest,
            "validation": validation,
            "files": self.list_files(name),
            "generated": self.generated_preview(manifest),
        }

    @staticmethod
    def _normalize_manifest(manifest):
        value = copy.deepcopy(manifest)
        value.setdefault("schemaVersion", 1)
        value.setdefault("project", {})
        value.setdefault("hardware", [])
        value.setdefault("devices", [])
        value.setdefault("udts", [])
        value.setdefault("dataBlocks", [])
        value.setdefault("states", [])
        value.setdefault("transitions", [])
        value.setdefault("workspace", {"activeFile": "src/Proceso.h", "completedSteps": []})
        return value

    # ------------------------------------------------------------------
    # Validation
    # ------------------------------------------------------------------
    def validate_model(self, model, target_warnings=None):
        errors = []
        warnings = list(target_warnings or [])
        resources = {}

        board_id = str(model.get("board") or "")
        if board_id not in BOARDS:
            errors.append({"path": "board", "message": "Placa no soportada"})
        board_pins = set()
        if board_id in BOARDS:
            board_pins.update(BOARDS[board_id]["digitalPins"])
            board_pins.update(BOARDS[board_id]["analogPins"])

        names = set()
        for index, signal in enumerate(model.get("hardware", [])):
            path = "hardware[%d]" % index
            name = str(signal.get("name", "")).strip()
            role = str(signal.get("role", "")).upper()
            pin = _normalize_pin(signal.get("target"))
            if not IDENTIFIER_RE.fullmatch(name):
                errors.append({"path": path + ".name", "message": "Nombre C++ no válido"})
            elif name in names:
                errors.append({"path": path + ".name", "message": "Nombre repetido"})
            names.add(name)
            if role not in ("DI", "DO", "AI"):
                errors.append({"path": path + ".role", "message": "Usa DI, DO o AI"})
            if not PIN_RE.fullmatch(pin):
                errors.append({"path": path + ".target", "message": "Pin no válido"})
            elif board_pins and pin not in board_pins:
                warnings.append("%s usa %s, que no figura en el catálogo de %s" % (name or path, pin, BOARDS[board_id]["label"]))
            self._claim_resource(resources, pin, name or path)

        device_names = set()
        for index, device in enumerate(model.get("devices", [])):
            path = "devices[%d]" % index
            name = str(device.get("name", "")).strip()
            kind = str(device.get("kind", ""))
            if not IDENTIFIER_RE.fullmatch(name):
                errors.append({"path": path + ".name", "message": "Nombre C++ no válido"})
            elif name in names or name in device_names:
                errors.append({"path": path + ".name", "message": "Nombre repetido"})
            device_names.add(name)
            if kind not in DEVICE_TYPES:
                errors.append({"path": path + ".kind", "message": "Tipo de dispositivo no soportado"})
                continue
            expected_pins = DEVICE_TYPES[kind]["pins"]
            pins = device.get("pins", {}) or {}
            for key in expected_pins:
                pin = _normalize_pin(pins.get(key))
                if not PIN_RE.fullmatch(pin):
                    errors.append({"path": "%s.pins.%s" % (path, key), "message": "Pin obligatorio no válido"})
                else:
                    if board_pins and pin not in board_pins:
                        warnings.append("%s.%s usa %s fuera del catálogo de la placa" % (name, key, pin))
                    self._claim_resource(resources, pin, "%s.%s" % (name, key))

        for pin, owners in resources.items():
            if len(owners) > 1:
                errors.append({"path": "resources.%s" % pin, "message": "Pin %s compartido por %s" % (pin, ", ".join(owners))})

        udt_names = set()
        for index, udt in enumerate(model.get("udts", [])):
            name = str(udt.get("name", "")).strip()
            if not IDENTIFIER_RE.fullmatch(name):
                errors.append({"path": "udts[%d].name" % index, "message": "Nombre de UDT no válido"})
            elif name in udt_names:
                errors.append({"path": "udts[%d].name" % index, "message": "UDT repetido"})
            udt_names.add(name)
            self._validate_fields(udt.get("fields", []), "udts[%d].fields" % index, udt_names, errors)

        db_names = set()
        known_types = {"bool", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t", "float"} | udt_names
        for index, block in enumerate(model.get("dataBlocks", [])):
            name = str(block.get("name", "")).strip()
            if not IDENTIFIER_RE.fullmatch(name):
                errors.append({"path": "dataBlocks[%d].name" % index, "message": "Nombre de DB no válido"})
            elif name in db_names:
                errors.append({"path": "dataBlocks[%d].name" % index, "message": "DB repetido"})
            db_names.add(name)
            self._validate_fields(block.get("variables", []), "dataBlocks[%d].variables" % index, known_types, errors)

        state_ids = set()
        state_symbols = set()
        for index, state in enumerate(model.get("states", [])):
            symbol = str(state.get("symbol", "")).strip()
            try:
                state_id = int(state.get("id"))
            except (TypeError, ValueError):
                errors.append({"path": "states[%d].id" % index, "message": "El número de paso debe ser entero"})
                state_id = None
            if state_id in state_ids:
                errors.append({"path": "states[%d].id" % index, "message": "Número de paso repetido"})
            state_ids.add(state_id)
            if not IDENTIFIER_RE.fullmatch(symbol):
                errors.append({"path": "states[%d].symbol" % index, "message": "Símbolo de paso no válido"})
            elif symbol in state_symbols:
                errors.append({"path": "states[%d].symbol" % index, "message": "Símbolo repetido"})
            state_symbols.add(symbol)
        for index, transition in enumerate(model.get("transitions", [])):
            for key in ("from", "to"):
                if transition.get(key) not in state_symbols:
                    errors.append({"path": "transitions[%d].%s" % (index, key), "message": "El paso no existe"})

        preset = model.get("project", {}).get("preset")
        if preset == "legacy":
            warnings.append("Proyecto importado: guardar añadirá %s sin reemplazar tu código" % MODEL_FILE)
        return {
            "valid": not errors,
            "errors": errors,
            "warnings": list(dict.fromkeys(warnings)),
            "resources": [{"pin": pin, "owners": owners} for pin, owners in sorted(resources.items())],
        }

    @staticmethod
    def _claim_resource(resources, pin, owner):
        if pin:
            resources.setdefault(pin, []).append(owner)

    @staticmethod
    def _validate_fields(fields, path, known_types, errors):
        names = set()
        for index, field in enumerate(fields or []):
            item_path = "%s[%d]" % (path, index)
            name = str(field.get("name", "")).strip()
            data_type = str(field.get("type", "")).strip()
            initial = str(field.get("initial", "")).strip()
            if not IDENTIFIER_RE.fullmatch(name):
                errors.append({"path": item_path + ".name", "message": "Nombre de variable no válido"})
            elif name in names:
                errors.append({"path": item_path + ".name", "message": "Variable repetida"})
            names.add(name)
            if data_type not in known_types:
                errors.append({"path": item_path + ".type", "message": "Tipo de dato no soportado"})
            if initial and not SAFE_INITIALIZER_RE.fullmatch(initial):
                errors.append({"path": item_path + ".initial", "message": "Inicializador no seguro"})
            if data_type == "bool" and initial and initial not in ("true", "false"):
                errors.append({"path": item_path + ".initial", "message": "Un bool usa true o false"})
            elif data_type and ("int" in data_type) and initial and not INTEGER_RE.fullmatch(initial):
                errors.append({"path": item_path + ".initial", "message": "Se esperaba un entero"})
            elif data_type == "float" and initial and not FLOAT_RE.fullmatch(initial):
                errors.append({"path": item_path + ".initial", "message": "Se esperaba un número decimal"})

    # ------------------------------------------------------------------
    # Save and code generation
    # ------------------------------------------------------------------
    def save_model(self, name, model):
        model = self._normalize_manifest(model)
        validation = self.validate_model(model)
        if not validation["valid"]:
            raise StudioError("El modelo contiene errores; no se ha escrito ningún archivo", "validation_failed", 422, validation["errors"])
        directory = self._project_path(name)
        with self._lock_for(name):
            persisted = copy.deepcopy(model)
            board = persisted.pop("board", "nano")
            _atomic_write(directory / MODEL_FILE, _json_text(persisted))
            self._write_hardware(directory, persisted.get("hardware", []))
            self._update_core_config(directory, board)
            self._update_platformio(directory, board)
            generated = self._write_generated(directory, persisted)
            generator_warnings = self._generate_hardware(directory)
        target = self._target_info(directory)
        model["board"] = board
        validation = self.validate_model(model, target["warnings"] + generator_warnings)
        return {"saved": True, "validation": validation, "generatedFiles": generated, "project": self.load_project(name)}

    def _write_hardware(self, directory, signals):
        output = io.StringIO(newline="")
        fieldnames = ["node", "name", "role", "target", "pullup", "active_low", "debounce_ms", "filter", "safe"]
        writer = csv.DictWriter(output, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        for signal in signals:
            target = str(signal.get("target", "")).strip()
            if not target.upper().startswith("A") and "." not in target:
                target = "gpio." + target
            role = str(signal.get("role", "")).upper()
            allowed = ROLE_FIELDS.get(role, ())
            row = {
                "node": "main",
                "name": signal.get("name", ""),
                "role": role,
                "target": target,
                "pullup": _bool_cell(signal.get("pullup")),
                "active_low": _bool_cell(signal.get("activeLow")),
                "debounce_ms": signal.get("debounceMs", ""),
                "filter": signal.get("filter", ""),
                "safe": _bool_cell(signal.get("safe")),
            }
            # Red de seguridad: un proyecto importado puede traer columnas que
            # su rol no admite y que harían fallar la generación entera.
            for column in ("pullup", "active_low", "debounce_ms", "filter", "safe"):
                if column not in allowed:
                    row[column] = ""
            writer.writerow(row)
        _atomic_write(directory / "hardware.csv", output.getvalue())

    def _update_core_config(self, directory, board):
        config = _read_json(directory / "corefsm.json", {}) or {}
        config["source"] = {"format": "csv", "path": "hardware.csv"}
        config.setdefault("defaults", {"debounce_ms": 20})
        config["nodes"] = [{"id": "main", "board": board}]
        config.setdefault("backends", [])
        _atomic_write(directory / "corefsm.json", _json_text(config))

    def _update_platformio(self, directory, board):
        info = BOARDS[board]
        path = directory / "platformio.ini"
        if not path.exists():
            return
        content = path.read_text(encoding="utf-8", errors="replace")
        content = re.sub(r"(?mi)^(\s*platform\s*=\s*)[^;\r\n]+", r"\g<1>" + info["platform"], content, count=1)
        content = re.sub(r"(?mi)^(\s*board\s*=\s*)[^;\r\n]+", r"\g<1>" + info["pioBoard"], content, count=1)
        _atomic_write(path, content)

    def _load_generator(self):
        if self._generator_module is not None:
            return self._generator_module
        path = self.root / "lib" / "CoreFSM" / "tools" / "corefsm_gen.py"
        spec = importlib.util.spec_from_file_location("corefsm_studio_generator", str(path))
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self._generator_module = module
        return module

    def _generate_hardware(self, directory):
        module = self._load_generator()
        try:
            config = module.load_config(str(directory / "corefsm.json"))
            source, source_format, source_node = module.resolve_project_source(str(directory), config)
            model = module.load_model(source, source_format, config, source_node)
            module.generate(project_dir=str(directory), quiet=True)
            return list(model.warnings)
        except Exception as exc:
            if exc.__class__.__name__ == "ConfigError":
                raise StudioError(str(exc), "hardware_generation_failed", 422)
            raise

    def generated_preview(self, model):
        return {
            "include/generated/ProjectData.h": self._render_data_header(model),
            "src/generated/ProjectData.cpp": self._render_data_source(model),
            "include/generated/ProjectStates.h": self._render_states_header(model),
            "include/generated/ProjectDevices.h": self._render_devices_header(model),
            "src/generated/ProjectDevices.cpp": self._render_devices_source(model),
        }

    def _write_generated(self, directory, model):
        previews = self.generated_preview(model)
        for relative, content in previews.items():
            _atomic_write(directory / relative, content)
        return list(previews)

    def _render_data_header(self, model):
        lines = [
            "// Generated by CoreFSM Studio. Do not edit: change the Data tables instead.",
            "#ifndef COREFSM_STUDIO_PROJECT_DATA_H",
            "#define COREFSM_STUDIO_PROJECT_DATA_H",
            "",
            "#include <CoreFSM.h>",
            "",
        ]
        for udt in model.get("udts", []):
            if udt.get("description"):
                lines.append("// " + _cpp_comment(udt.get("description")))
            lines.append("struct %s {" % udt.get("name"))
            for field in udt.get("fields", []):
                lines.append(self._field_line(field))
            lines.extend(["};", ""])
        retained = []
        for block in model.get("dataBlocks", []):
            name = block.get("name")
            if block.get("description"):
                lines.append("// " + _cpp_comment(block.get("description")))
            lines.append("struct %s_Data {" % name)
            for field in block.get("variables", []):
                lines.append(self._field_line(field))
            lines.append("};")
            if block.get("retained"):
                version = int(block.get("version", 1))
                address = int(block.get("address", 0))
                lines.append("extern DataBlock<%s_Data, %d, %d> %sStore;" % (name, version, address, name))
                lines.append("#define %s (%sStore.data)" % (name, name))
                retained.append(name)
            else:
                lines.append("extern %s_Data %s;" % (name, name))
            lines.append("")
        lines.extend([
            "void ProjectDataBegin();",
            "void ProjectDataAutoSave();",
            "",
            "#endif  // COREFSM_STUDIO_PROJECT_DATA_H",
            "",
        ])
        return "\n".join(lines)

    @staticmethod
    def _field_line(field):
        initial = str(field.get("initial", "")).strip()
        suffix = " = %s" % initial if initial else "{}"
        comment = _cpp_comment(field.get("comment"))
        return "  %s %s%s;%s" % (
            field.get("type"), field.get("name"), suffix,
            ("  // " + comment) if comment else "",
        )

    def _render_data_source(self, model):
        lines = [
            "// Generated by CoreFSM Studio.",
            '#include "generated/ProjectData.h"',
            "",
        ]
        retained = []
        for block in model.get("dataBlocks", []):
            name = block.get("name")
            if block.get("retained"):
                lines.append("DataBlock<%s_Data, %d, %d> %sStore;" % (
                    name, int(block.get("version", 1)), int(block.get("address", 0)), name))
                retained.append(name)
            else:
                lines.append("%s_Data %s;" % (name, name))
        lines.extend(["", "void ProjectDataBegin() {"])
        if retained:
            lines.extend("  %sStore.begin();" % name for name in retained)
        else:
            lines.append("  // No retained data blocks in this project.")
        lines.extend(["}", "", "void ProjectDataAutoSave() {"])
        if retained:
            lines.extend("  %sStore.autoSave();" % name for name in retained)
        else:
            lines.append("  // Nothing to persist.")
        lines.extend(["}", ""])
        return "\n".join(lines)

    def _render_states_header(self, model):
        lines = [
            "// Generated by CoreFSM Studio. State behavior remains in src/Proceso.h.",
            "#ifndef COREFSM_STUDIO_PROJECT_STATES_H",
            "#define COREFSM_STUDIO_PROJECT_STATES_H",
            "",
            "#include <stdint.h>",
            "",
            "enum ProjectState : uint16_t {",
        ]
        states = model.get("states", [])
        if states:
            for index, state in enumerate(states):
                comma = "," if index < len(states) - 1 else ""
                comment = _cpp_comment(state.get("label") or state.get("description"))
                lines.append("  %-28s = %s%s%s" % (
                    state.get("symbol"), state.get("id"), comma,
                    ("  // " + comment) if comment else ""))
        else:
            lines.append("  PASO_REPOSO = 0")
        lines.extend(["};", "", "#endif  // COREFSM_STUDIO_PROJECT_STATES_H", ""])
        return "\n".join(lines)

    def _render_devices_header(self, model):
        devices = model.get("devices", [])
        needs_servo = any(device.get("kind") == "servo" for device in devices)
        lines = [
            "// Generated by CoreFSM Studio. Change pins in the Hardware table.",
            "#ifndef COREFSM_STUDIO_PROJECT_DEVICES_H",
            "#define COREFSM_STUDIO_PROJECT_DEVICES_H",
            "",
            "#include <CoreFSM.h>",
        ]
        if needs_servo:
            lines.append("#include <Servo.h>")
        lines.append("")
        declarations = {
            "motor_drive": "extern MotorDrive %s;",
            "dir_pwm_motor": "extern DirPwmMotorDrive %s;",
            "ultrasonic": "extern UltrasonicSensor %s;",
            "servo": "extern Servo %s;",
            "chassis_diff": "extern DifferentialChassis %s;",
            "chassis_4wd": "extern FourWheelChassis %s;",
        }
        for device in devices:
            template = declarations.get(device.get("kind"))
            if template:
                lines.append(template % device.get("name"))
        lines.extend([
            "",
            "void ProjectDevicesBegin();",
            "void ProjectDevicesReadInputs();",
            "void ProjectDevicesWriteOutputs();",
            "void ProjectDevicesEnterSafeState();",
            "",
            "#endif  // COREFSM_STUDIO_PROJECT_DEVICES_H",
            "",
        ])
        return "\n".join(lines)

    def _render_devices_source(self, model):
        devices = model.get("devices", [])
        lines = ["// Generated by CoreFSM Studio.", '#include "generated/ProjectDevices.h"', ""]
        for device in devices:
            name = device.get("name")
            kind = device.get("kind")
            pins = device.get("pins", {}) or {}
            options = device.get("options", {}) or {}
            refs = device.get("refs", {}) or {}
            if kind == "motor_drive":
                lines.append("MotorDrive %s(%s, %s, %s);" % (name, pins.get("in1"), pins.get("in2"), pins.get("pwm")))
            elif kind == "dir_pwm_motor":
                inverted = "true" if options.get("directionInverted") else "false"
                lines.append("DirPwmMotorDrive %s(%s, %s, %s);" % (name, pins.get("dir"), pins.get("pwm"), inverted))
            elif kind == "ultrasonic":
                lines.append("UltrasonicSensor %s(%s, %s, %s, %s);" % (
                    name, pins.get("trig"), pins.get("echo"), options.get("intervalMs", 60), options.get("timeoutUs", 12000)))
            elif kind == "servo":
                lines.append("Servo %s;" % name)
            elif kind == "chassis_diff":
                lines.append("DifferentialChassis %s(%s, %s, %s);" % (
                    name, refs.get("left"), refs.get("right"), options.get("trackWidth", 10)))
            elif kind == "chassis_4wd":
                lines.append("FourWheelChassis %s(%s, %s, %s, %s);" % (
                    name, refs.get("fl"), refs.get("fr"), refs.get("rl"), refs.get("rr")))
        lines.extend(["", "void ProjectDevicesBegin() {"])
        begin_lines = []
        for device in devices:
            name, kind = device.get("name"), device.get("kind")
            options = device.get("options", {}) or {}
            pins = device.get("pins", {}) or {}
            if kind in ("motor_drive", "dir_pwm_motor", "ultrasonic"):
                begin_lines.append("  %s.begin();" % name)
            if kind in ("motor_drive", "dir_pwm_motor") and options.get("ramp") is not None:
                begin_lines.append("  %s.setRamp(%s);" % (name, options.get("ramp")))
            if kind == "servo":
                begin_lines.append("  %s.attach(%s);" % (name, pins.get("signal")))
                begin_lines.append("  %s.write(%s);" % (name, options.get("initialAngle", 90)))
        lines.extend(begin_lines or ["  // No compound devices configured."])
        lines.extend(["}", "", "void ProjectDevicesReadInputs() {"])
        read_lines = ["  %s.readInputs();" % device.get("name") for device in devices if device.get("kind") == "ultrasonic"]
        lines.extend(read_lines or ["  // No compound input devices."])
        lines.extend(["}", "", "void ProjectDevicesWriteOutputs() {"])
        output_lines = ["  %s.writeOutputs();" % device.get("name") for device in devices if device.get("kind") in ("motor_drive", "dir_pwm_motor")]
        lines.extend(output_lines or ["  // No compound output devices."])
        lines.extend(["}", "", "void ProjectDevicesEnterSafeState() {"])
        safe_lines = ["  %s.enterSafeState();" % device.get("name") for device in devices if device.get("kind") in ("motor_drive", "dir_pwm_motor")]
        lines.extend(safe_lines or ["  // No compound devices require a safe state."])
        lines.extend(["}", ""])
        return "\n".join(lines)

    # ------------------------------------------------------------------
    # Importación de una fuente externa
    #
    # El generador ya sabe leer Wokwi, CSV y JSON; aquí solo se le da el
    # contenido en un archivo temporal y se traduce su modelo a filas de la
    # tabla de variables.  No se escribe nada en el proyecto: quien decide si
    # reemplazar o añadir es el usuario, en la interfaz.
    # ------------------------------------------------------------------
    IMPORT_FORMATS = ("wokwi", "csv", "json")

    @staticmethod
    def sniff_format(content, filename=""):
        name = (filename or "").lower()
        text = content.lstrip()
        if name.endswith(".csv") or (text and not text.startswith(("{", "["))):
            return "csv"
        try:
            value = json.loads(content)
        except ValueError:
            return "csv"
        if isinstance(value, dict) and ("parts" in value or "connections" in value):
            return "wokwi"
        return "json"

    def import_hardware(self, project_name, content, fmt="auto", filename="", node=None):
        if not isinstance(content, str) or not content.strip():
            raise StudioError("El archivo está vacío", "invalid_request", 422)
        if len(content.encode("utf-8")) > 2 * 1024 * 1024:
            raise StudioError("El archivo supera los 2 MB", "file_too_large", 413)
        directory = self._project_path(project_name)
        resolved = self.sniff_format(content, filename) if fmt in ("auto", "", None) else str(fmt)
        if resolved not in self.IMPORT_FORMATS:
            raise StudioError("Formato no soportado: %s" % resolved, "invalid_format", 422)

        suffix = ".csv" if resolved == "csv" else ".json"
        module = self._load_generator()
        temporary = Path(tempfile.mkdtemp(prefix="corefsm-import-")) / ("origen" + suffix)
        try:
            temporary.write_text(content, encoding="utf-8")
            config = _read_json(directory / "corefsm.json", {}) or {}
            try:
                model = module.load_model(str(temporary), resolved, config, node)
            except Exception as exc:
                if exc.__class__.__name__ == "ConfigError":
                    raise StudioError(str(exc), "import_failed", 422)
                raise StudioError("No se ha podido leer la fuente: %s" % exc, "import_failed", 422)
        finally:
            shutil.rmtree(str(temporary.parent), ignore_errors=True)

        signals = [self._signal_to_row(signal, resolved) for signal in model.signals]
        board = model.board if model.board in BOARDS else ""
        by_pio = next((key for key, value in BOARDS.items()
                       if value["pioBoard"] == model.board), "")
        return {
            "format": resolved,
            "node": model.node,
            "board": board or by_pio,
            "signals": signals,
            "warnings": list(model.warnings),
            "backends": [getattr(backend, "name", str(backend)) for backend in model.backends],
        }

    @staticmethod
    def _signal_to_row(signal, source):
        """Traduce una señal del generador a una fila de la tabla de variables."""
        allowed = ROLE_FIELDS.get(signal.role, ())
        row = {
            "name": signal.name,
            "role": signal.role,
            "target": str(signal.target or ""),
            "pullup": "" if signal.pullup is None else bool(signal.pullup),
            "activeLow": "" if signal.active_low is None else bool(signal.active_low),
            "debounceMs": "" if signal.debounce_ms is None else int(signal.debounce_ms),
            "filter": "" if signal.filter is None else int(signal.filter),
            "safe": "" if signal.safe is None else bool(signal.safe),
            "group": "Importado de " + source,
            "description": "",
        }
        mapping = {"pullup": "pullup", "active_low": "activeLow", "debounce_ms": "debounceMs",
                   "filter": "filter", "safe": "safe"}
        for column, key in mapping.items():
            if column not in allowed:
                row[key] = ""
        return row

    # ------------------------------------------------------------------
    # Project creation
    # ------------------------------------------------------------------
    def create_project(self, name, board="nano", preset="starter", display_name=None):
        if not PROJECT_RE.fullmatch(str(name or "")):
            raise StudioError("Usa letras, números, guion o guion bajo en el nombre", "invalid_project_name", 422)
        if preset not in PRESETS:
            raise StudioError("Preset desconocido", "invalid_preset", 422)
        if board not in BOARDS:
            raise StudioError("Placa no soportada", "invalid_board", 422)
        destination = self._project_path(name, must_exist=False)
        if destination.exists():
            raise StudioError("Ya existe un proyecto con ese nombre", "project_exists", 409)
        script = self.root / "lib" / "CoreFSM" / "tools" / "nuevo_proyecto.py"
        command = [sys.executable, str(script), name, "--placa", board, "--fuente", "csv", "--dir", "projects"]
        result = subprocess.run(command, cwd=str(self.root), capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            raise StudioError("No se pudo crear el proyecto", "creation_failed", 500, [result.stderr.strip() or result.stdout.strip()])
        manifest = manifest_for_preset(preset, display_name or name)
        manifest["board"] = board
        if preset in ("keyestudio_ks0192", "robot_4wd_reference"):
            _atomic_write(destination / "src" / "main.cpp", self._robot_main(preset))
            _atomic_write(destination / "src" / "Proceso.h", self._robot_process())
        saved = self.save_model(name, manifest)
        return {"created": True, "id": name, "output": result.stdout, "project": saved["project"]}

    @staticmethod
    def _robot_main(preset):
        reference = preset == "robot_4wd_reference"
        input_code = """
  if (HW.Pulsador_Marcha.hasRisen()) {
    if (proceso.getStep() == ROB_PARADO) proceso.ordenMarcha = true;
    else proceso.requestStop();
  }""" if reference else """
  while (Serial.available()) {
    const char command = (char)Serial.read();
    if (command == 's' || command == 'S') proceso.ordenMarcha = true;
    if (command == 'x' || command == 'X') proceso.requestStop();
  }"""
        output_code = """
  if (proceso.isFaulted()) HW.Led_Estado.setMode(OUT_BLINK_FAST);
  else if (proceso.getStep() == ROB_PARADO) HW.Led_Estado.setMode(OUT_BLINK_SLOW);
  else HW.Led_Estado.setMode(OUT_ON);""" if reference else ""
        return '''#include <Arduino.h>
#include "HardwareConfig.h"
#include "generated/ProjectData.h"
#include "generated/ProjectDevices.h"
#include "generated/ProjectStates.h"
#include "Proceso.h"

CFSM_DEFINE_HARDWARE;

BlockManager<4> manager;
Proceso proceso(Chasis);
StepTracer tracer(proceso, Serial);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  HW.begin();
  ProjectDataBegin();
  ProjectDevicesBegin();
  manager.registerBlock(&proceso, F("ROBOT"));
  manager.beginAll();
  proceso.start();
  Serial.println(F("CoreFSM Robot listo: S=marcha, X=paro"));
}

void loop() {
  HW.readInputs();
  ProjectDevicesReadInputs();
  DB_Robot.distanciaActual = Sonar.cm();
%s
  manager.updateAll();
%s
  tracer.update();
  ProjectDataAutoSave();
  HW.setSafetyInterlock(manager.isEmergencyStop());
  if (manager.isEmergencyStop()) ProjectDevicesEnterSafeState();
  HW.writeOutputs();
  ProjectDevicesWriteOutputs();
}
''' % (input_code, output_code)

    @staticmethod
    def _robot_process():
        return '''#ifndef PROCESO_H
#define PROCESO_H

#include <CoreFSM.h>
#include "generated/ProjectData.h"
#include "generated/ProjectStates.h"

class Proceso : public SequenceBlock {
 public:
  explicit Proceso(DifferentialChassis& chassis) : _chassis(chassis) {}

  bool ordenMarcha = false;

  void begin() override {
    setName(F("ROBOT"));
    setInitialStep(ROB_PARADO);
    setStep(ROB_PARADO);
    _chassis.disable();
  }

  void requestStop() {
    ordenMarcha = false;
    _chassis.stop();
    _chassis.disable();
    setStep(ROB_PARADO);
  }

  void update() override {
    if (!updateSequence()) { _chassis.stop(); _chassis.disable(); return; }

    switch (_currentStep) {
      case ROB_PARADO:
        _chassis.stop();
        if (ordenMarcha) {
          ordenMarcha = false;
          _chassis.enable();
          setStep(ROB_EXPLORAR);
        }
        break;

      case ROB_EXPLORAR:
        _chassis.forward(DB_Robot.velocidadCrucero);
        if (DB_Robot.distanciaActual <= DB_Robot.distanciaCritica) {
          _chassis.stop();
          setStep(ROB_FRENAR);
        }
        break;

      case ROB_FRENAR:
        _chassis.stop();
        if (getTimeInStep() >= 250) setStep(ROB_MIRAR_IZQ);
        break;

      case ROB_MIRAR_IZQ:
        _chassis.spinLeft(DB_Robot.velocidadGiro);
        if (getTimeInStep() >= DB_Robot.msPivote) {
          _chassis.stop();
          _distIzq = DB_Robot.distanciaActual;
          setStep(ROB_MIRAR_DER);
        }
        break;

      case ROB_MIRAR_DER:
        _chassis.spinRight(DB_Robot.velocidadGiro);
        if (getTimeInStep() >= (cfsm_time_t)DB_Robot.msPivote * 2) {
          _chassis.stop();
          _distDer = DB_Robot.distanciaActual;
          setStep(ROB_DECIDIR);
        }
        break;

      case ROB_DECIDIR:
        if (_distIzq <= DB_Robot.distanciaCritica && _distDer <= DB_Robot.distanciaCritica) {
          setStep(ROB_ESCAPAR);
        } else {
          _giroDerecha = _distDer >= _distIzq;
          setStep(ROB_GIRAR);
        }
        break;

      case ROB_GIRAR:
        if (_giroDerecha) _chassis.spinRight(DB_Robot.velocidadGiro);
        else _chassis.spinLeft(DB_Robot.velocidadGiro);
        if (getTimeInStep() >= DB_Robot.msGiro90) {
          _chassis.stop();
          completeCycle(ROB_EXPLORAR);
        }
        break;

      case ROB_ESCAPAR:
        if (getTimeInStep() < 700) _chassis.backward(DB_Robot.velocidadCrucero);
        else if (getTimeInStep() < 700 + (cfsm_time_t)DB_Robot.msGiro90 * 2)
          _chassis.spinRight(DB_Robot.velocidadGiro);
        else {
          _chassis.stop();
          setStep(ROB_EXPLORAR);
        }
        break;
    }
  }

 private:
  DifferentialChassis& _chassis;
  uint16_t _distIzq = 999;
  uint16_t _distDer = 999;
  bool _giroDerecha = true;
};

#endif
'''

    # ------------------------------------------------------------------
    # Files and build actions
    # ------------------------------------------------------------------
    def list_files(self, project_name):
        directory = self._project_path(project_name)
        result = []
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or ".pio" in path.parts:
                continue
            if path.suffix.lower() not in EDITABLE_SUFFIXES:
                continue
            relative = path.relative_to(directory).as_posix()
            content = path.read_bytes()
            result.append({
                "path": relative,
                "name": path.name,
                "size": len(content),
                "revision": _revision(content),
                "generated": relative in GENERATED_FILES or relative.startswith(GENERATED_PREFIXES),
            })
        return result

    def read_file(self, project_name, relative):
        path, relative = self._file_path(project_name, relative)
        content = path.read_text(encoding="utf-8", errors="replace")
        return {
            "path": relative,
            "content": content,
            "revision": _revision(content),
            "generated": relative in GENERATED_FILES or relative.startswith(GENERATED_PREFIXES),
        }

    def write_file(self, project_name, relative, content, expected_revision=None):
        path, relative = self._file_path(project_name, relative, must_exist=False)
        if relative in GENERATED_FILES or relative.startswith(GENERATED_PREFIXES):
            raise StudioError("Ese archivo se genera desde una tabla; edita su modelo", "generated_file", 409)
        if not isinstance(content, str) or len(content.encode("utf-8")) > 1024 * 1024:
            raise StudioError("El archivo supera el límite de 1 MB", "file_too_large", 413)
        with self._lock_for(project_name):
            if path.exists() and expected_revision:
                current = _revision(path.read_bytes())
                if current != expected_revision:
                    raise StudioError("El archivo cambió fuera de Studio; recárgalo antes de guardar", "revision_conflict", 409)
            _atomic_write(path, content)
        return self.read_file(project_name, relative)

    def run_action(self, project_name, action):
        if action == "generate":
            project = self.load_project(project_name)
            return self.save_model(project_name, project["model"])
        if action not in ("build", "upload"):
            raise StudioError("Acción desconocida", "invalid_action", 400)
        directory = self._project_path(project_name)
        executable = self._find_platformio()
        if not executable:
            raise StudioError("PlatformIO no está instalado o no se encuentra", "platformio_not_found", 503)
        command = [executable, "run", "-d", str(directory)]
        if action == "upload":
            command.extend(["-t", "upload"])
        with self._lock_for(project_name):
            try:
                result = subprocess.run(command, cwd=str(self.root), capture_output=True, text=True, timeout=300)
            except subprocess.TimeoutExpired:
                raise StudioError("PlatformIO superó el tiempo máximo de 5 minutos", "action_timeout", 504)
        output = (result.stdout or "") + ("\n" + result.stderr if result.stderr else "")
        return {"action": action, "ok": result.returncode == 0, "returnCode": result.returncode, "output": output[-200000:]}

    @staticmethod
    def _find_platformio():
        for command in ("pio", "platformio"):
            value = shutil.which(command)
            if value:
                return value
        home = Path.home()
        candidates = [
            home / ".platformio" / "penv" / "Scripts" / "platformio.exe",
            home / ".platformio" / "penv" / "bin" / "platformio",
        ]
        return str(next((path for path in candidates if path.exists()), ""))

