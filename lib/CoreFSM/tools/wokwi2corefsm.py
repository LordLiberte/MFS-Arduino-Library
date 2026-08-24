#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Compatibilidad con el antiguo generador exclusivo de Wokwi.

Los proyectos existentes pueden conservar tanto su comando como la linea
``extra_scripts``. La implementacion real vive en :mod:`corefsm_gen`.
"""

from __future__ import print_function

import argparse
import os
import sys

from corefsm_gen import (
    ConfigError,
    emit_legacy_wokwi_header,
    generate,
    normalize_pin,
    parse_wokwi_source,
    role_from_prefix,
    role_from_type,
    sanitize_identifier,
    strip_role_prefix,
)


def parse_diagram(diagram, overrides, node=None):
    """Conserva la API historica para herramientas que importaban la funcion."""
    import json
    import tempfile

    with tempfile.TemporaryDirectory() as directory:
        diagram_path = os.path.join(directory, "diagram.json")
        with open(diagram_path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(diagram, handle)
        model = parse_wokwi_source(diagram_path, overrides or {}, node)
    signals = []
    for signal in model.signals:
        entry = {
            "pin": signal.pin,
            "name": signal.name,
            "role": signal.role,
            "type": "",
        }
        if signal.role == "DI":
            entry.update(pullup=signal.pullup, debounce=signal.debounce_ms)
        elif signal.role == "DO":
            entry.update(activeLow=signal.active_low)
        else:
            entry.update(filter=signal.filter)
        signals.append(entry)
    return model.board, signals, model.warnings


def emit_header(board, signals, src):
    """Conserva la firma historica del emisor Wokwi."""
    from corefsm_gen import HardwareModel, Signal

    converted = []
    for entry in signals:
        role = str(entry["role"]).upper()
        converted.append(Signal(
            board, entry["name"], role, pin=str(entry["pin"]),
            pullup=entry.get("pullup"),
            active_low=entry.get("activeLow"),
            debounce_ms=entry.get("debounce"),
            filter_value=entry.get("filter"), safe=False,
        ))
    model = HardwareModel(board, board, converted, [], src, "wokwi")
    return emit_legacy_wokwi_header(model)


def run(diagram_path="diagram.json", out_path="HardwareConfig.h",
        check_only=False, quiet=False, node=None):
    project = os.path.abspath(os.path.dirname(diagram_path) or ".")
    return generate(
        project_dir=project,
        input_path=os.path.abspath(diagram_path),
        output_path=os.path.abspath(out_path),
        source_format="wokwi",
        node=node,
        check_only=check_only,
        quiet=quiet,
        legacy_wokwi=True,
    )


def _cli(argv=None):
    parser = argparse.ArgumentParser(
        description="Genera HardwareConfig.h desde diagram.json (compatibilidad).")
    parser.add_argument("-i", "--input", default="diagram.json")
    parser.add_argument("-o", "--output", default="HardwareConfig.h")
    parser.add_argument("--node", default=None,
                        help="placa a generar si diagram.json contiene varias")
    parser.add_argument("--check", action="store_true",
                        help="valida y avisa, pero no escribe")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args(argv)
    try:
        return run(args.input, args.output, args.check, args.quiet, args.node)
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
            node=selected_node,
            legacy_wokwi=True,
        )
    except ConfigError as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        raise SystemExit(2)
    if code:
        raise SystemExit(code)


_EN_PLATFORMIO = ("Import" in globals()) or ("DefaultEnvironment" in globals())

if _EN_PLATFORMIO:
    _platformio_entry()
elif __name__ == "__main__":
    sys.exit(_cli())
