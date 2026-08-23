#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
wokwi2corefsm.py
================
Convierte el `diagram.json` de Wokwi en el `HardwareConfig.h` de CoreFSM.

POR QUE ESTO EXISTE
-------------------
El error mas caro de una puesta en marcha no es un fallo de logica: es que el
plano diga una cosa, el cable este en otro borne y el software apunte a un
tercer sitio. Tres fuentes de verdad que se desincronizan en cuanto alguien
cambia algo con prisa.

Este script elimina dos de las tres. El esquema grafico pasa a ser la UNICA
fuente de verdad: dibujas el componente, le pones nombre, lo cableas, y la
tabla de variables del programa sale sola de ahi. Si mueves un cable del pin 2
al pin 8, al compilar el software ya lee el pin 8 sin que toques una linea.

Es el mismo flujo que en la industria cuando exportas la configuracion de
hardware desde EPLAN o CADe SIMU y la importas en la tabla de variables del
automata.

POR QUE ES UN PASO PREVIO A LA COMPILACION Y NO ALGO EN TIEMPO DE EJECUCION
--------------------------------------------------------------------------
Un microcontrolador corre sobre el metal: no tiene sistema de archivos ni
puede abrir un JSON al arrancar. La traduccion tiene que ocurrir antes, en el
PC, mientras se compila. De ahi que esto sea un script de Python y no codigo
de Arduino.

COMO SE DECIDE QUE ES ENTRADA Y QUE ES SALIDA
---------------------------------------------
Por dos vias, en este orden de prioridad:

  1. PREFIJO EN EL NOMBRE (manda siempre). Poniendole al componente un id que
     empiece por DI_, DO_, AI_, I_, Q_ o E_ decides tu explicitamente. Es la
     via a usar cuando el tipo de componente no basta (un modulo generico, una
     placa de reles, un sensor raro).

        DI_ / I_   entrada digital
        DO_ / Q_   salida digital
        AI_ / E_   entrada analogica

  2. TIPO DE COMPONENTE (respaldo automatico). Wokwi identifica cada pieza con
     un "type" del catalogo, y de ahi se deduce: un pushbutton es entrada, un
     led es salida, un potenciometro es entrada analogica.

AJUSTES FINOS: corefsm.json
---------------------------
Para lo que el esquema no puede expresar (tiempo de antirrebote de un sensor
concreto, un rele activo a nivel bajo, un pin que hay que ignorar), pon un
archivo `corefsm.json` junto al `diagram.json`:

    {
      "defaults": { "debounce": 20 },
      "pins": {
        "3": { "name": "FC_Carro_Trabajo", "role": "DI", "debounce": 5 },
        "4": { "name": "FC_Carro_Reposo",  "role": "DI", "debounce": 5 },
        "7": { "name": "Rele_Bomba",       "role": "DO", "activeLow": true }
      },
      "ignore": ["led_decorativo"]
    }

USO
---
    python3 wokwi2corefsm.py                        # busca diagram.json aqui
    python3 wokwi2corefsm.py -i ruta/diagram.json -o include/HardwareConfig.h
    python3 wokwi2corefsm.py --check                # solo valida, no escribe

Como hook de PlatformIO, en platformio.ini:

    extra_scripts = pre:wokwi2corefsm.py

(El script detecta solo si lo ha invocado PlatformIO.)
"""

import json
import os
import re
import sys

# ---------------------------------------------------------------------------
# Catalogo: tipo de componente Wokwi -> papel en el control
# ---------------------------------------------------------------------------
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

# Prefijos que fuerzan el papel (tienen prioridad sobre el catalogo)
ROLE_PREFIXES = [
    ("DI_", "DI"), ("I_", "DI"), ("IN_", "DI"),
    ("DO_", "DO"), ("Q_", "DO"), ("OUT_", "DO"),
    ("AI_", "AI"), ("E_", "AI"), ("AN_", "AI"),
]

# Pines de la placa que nunca son senales de control
POWER_PINS = {
    "GND", "VCC", "5V", "3V3", "3.3V", "VIN", "AREF", "RESET", "RST",
    "VBUS", "VSYS", "AGND", "IOREF", "EN", "VN", "VP",
}

# Prefijos de "type" que identifican una placa controladora
BOARD_HINTS = (
    "wokwi-arduino", "board-", "wokwi-esp32", "wokwi-pi-pico",
    "wokwi-attiny", "wokwi-franzininho", "wokwi-microbit",
)


# ---------------------------------------------------------------------------
def sanitize_identifier(name):
    """Convierte el id de Wokwi en un identificador valido de C++.

    Wokwi permite guiones, espacios y acentos en los ids; C++ no. Ademas, un
    identificador no puede empezar por digito. Se hace la conversion en vez de
    rechazar el nombre para que el usuario no tenga que preocuparse.
    """
    s = re.sub(r"[^0-9a-zA-Z_]", "_", name)
    s = re.sub(r"_+", "_", s).strip("_")
    if not s:
        s = "senal"
    if s[0].isdigit():
        s = "S_" + s
    return s


def strip_role_prefix(name):
    for pref, _role in ROLE_PREFIXES:
        if name.upper().startswith(pref):
            return name[len(pref):]
    return name


def role_from_prefix(name):
    up = name.upper()
    for pref, role in ROLE_PREFIXES:
        if up.startswith(pref):
            return role
    return None


def role_from_type(part_type):
    if part_type in DIGITAL_INPUTS:
        return "DI"
    if part_type in DIGITAL_OUTPUTS:
        return "DO"
    if part_type in ANALOG_INPUTS:
        return "AI"
    if part_type in PWM_OUTPUTS:
        return "DO"
    return None


def normalize_pin(token):
    """Traduce el nombre de pin de Wokwi a algo que entienda el compilador.

    Casos:
      "13"   -> 13          numerico directo
      "A0"   -> A0          constante de Arduino para pines analogicos
      "D5"   -> 5           notacion de placas ESP32 / Pico
      "GP15" -> 15          notacion de Raspberry Pi Pico
    Devuelve None si el pin no es utilizable como senal de control.
    """
    t = token.strip().upper()
    t = t.split(".")[0]          # "GND.1" -> "GND", "2.l" -> "2"
    if t in POWER_PINS:
        return None
    if re.fullmatch(r"\d+", t):
        return t
    if re.fullmatch(r"A\d+", t):
        return t                 # A0, A1... validos tal cual en AVR
    m = re.fullmatch(r"(?:D|GP|GPIO)(\d+)", t)
    if m:
        return m.group(1)
    return None


# ---------------------------------------------------------------------------
def parse_diagram(diagram, overrides):
    parts = {}
    for p in diagram.get("parts", []):
        pid = p.get("id")
        if pid:
            parts[pid] = p.get("type", "")

    # Identificar la placa
    board_ids = [pid for pid, t in parts.items()
                 if any(t.startswith(h) for h in BOARD_HINTS)]
    forced = overrides.get("board")
    if forced:
        board_ids = [forced]
    if not board_ids:
        raise SystemExit(
            "ERROR: no se ha encontrado ninguna placa en diagram.json.\n"
            "       Anade \"board\": \"<id de la placa>\" en corefsm.json.")
    board = board_ids[0]

    ignore = set(overrides.get("ignore", []))
    pin_over = {str(k): v for k, v in overrides.get("pins", {}).items()}
    defaults = overrides.get("defaults", {})
    def_debounce = int(defaults.get("debounce", 20))

    # Recolectar (pin de placa, componente, pin del componente)
    links = []          # (board_pin, comp_id, comp_pin)
    for conn in diagram.get("connections", []):
        if not isinstance(conn, list) or len(conn) < 2:
            continue
        a, b = str(conn[0]), str(conn[1])
        if ":" not in a or ":" not in b:
            continue
        pa, na = a.split(":", 1)
        pb, nb = b.split(":", 1)

        if pa == board and pb != board:
            bpin, comp, cpin = na, pb, nb
        elif pb == board and pa != board:
            bpin, comp, cpin = nb, pa, na
        else:
            continue          # placa-placa o componente-componente: no aplica

        pin = normalize_pin(bpin)
        if pin is None:
            continue          # alimentacion o masa
        if comp in ignore:
            continue
        links.append((pin, comp, cpin))

    # Cuantas senales aporta cada componente: decide si hace falta sufijo
    per_comp = {}
    for pin, comp, cpin in links:
        per_comp.setdefault(comp, []).append((pin, cpin))

    signals = []      # dicts con name/role/pin/opts
    seen_pins = {}
    warnings = []

    for pin, comp, cpin in links:
        ov = pin_over.get(pin, {})
        ptype = parts.get(comp, "")

        # --- papel ---
        role = ov.get("role") or role_from_prefix(comp) or role_from_type(ptype)
        if role is None:
            warnings.append(
                "  pin %-3s  %-24s tipo '%s' no reconocido: se omite.\n"
                "            Ponle prefijo DI_/DO_/AI_ al nombre en Wokwi, o\n"
                "            declaralo en corefsm.json." % (pin, comp, ptype))
            continue
        role = role.upper()

        # --- nombre ---
        if "name" in ov:
            name = sanitize_identifier(ov["name"])
        else:
            base = sanitize_identifier(strip_role_prefix(comp))
            if len(per_comp[comp]) > 1:
                suffix = sanitize_identifier(cpin)
                name = "%s_%s" % (base, suffix)
            else:
                name = base

        # --- conflicto de pin ---
        if pin in seen_pins:
            warnings.append(
                "  pin %-3s  usado por '%s' y por '%s'. Se conserva el primero.\n"
                "            Revisa el cableado o usa \"ignore\" en corefsm.json."
                % (pin, seen_pins[pin], name))
            continue
        seen_pins[pin] = name

        entry = {"pin": pin, "name": name, "role": role, "type": ptype}
        if role == "DI":
            entry["pullup"] = bool(ov.get("pullup", True))
            entry["debounce"] = int(ov.get("debounce", def_debounce))
        elif role == "DO":
            entry["activeLow"] = bool(ov.get("activeLow", False))
        else:
            entry["filter"] = int(ov.get("filter", 3))
        signals.append(entry)

    # Nombres repetidos: en C++ serian dos miembros con el mismo nombre
    names = {}
    for s in signals:
        names.setdefault(s["name"], []).append(s["pin"])
    for n, pins in names.items():
        if len(pins) > 1:
            warnings.append(
                "  nombre '%s' repetido en los pines %s. Renombra los\n"
                "            componentes en Wokwi: el codigo no compilara."
                % (n, ", ".join(pins)))

    def sort_key(s):
        p = s["pin"]
        return (1, int(p[1:])) if p.startswith("A") else (0, int(p))

    signals.sort(key=sort_key)
    return board, signals, warnings


# ---------------------------------------------------------------------------
HEADER_TMPL = '''/* =============================================================================
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
 *      HW.{ejemplo}
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


def emit_header(board, signals, src):
    di = [s for s in signals if s["role"] == "DI"]
    do = [s for s in signals if s["role"] == "DO"]
    ai = [s for s in signals if s["role"] == "AI"]

    def rows(items, fmt):
        if not items:
            return "  /* (ninguna) */"
        out = []
        for i, s in enumerate(items):
            last = (i == len(items) - 1)
            out.append(fmt(s) + ("" if last else " \\"))
        return "\n".join(out)

    di_rows = rows(di, lambda s: "  ROW( %4s, %-32s, %-5s, %3d )"
                   % (s["pin"], s["name"], str(s["pullup"]).lower(), s["debounce"]))
    do_rows = rows(do, lambda s: "  ROW( %4s, %-32s, %-5s )"
                   % (s["pin"], s["name"], str(s["activeLow"]).lower()))
    ai_rows = rows(ai, lambda s: "  ROW( %4s, %-32s, %d )"
                   % (s["pin"], s["name"], s["filter"]))

    ejemplo = "(sin senales)"
    if di:
        ejemplo = "%s.hasRisen()" % di[0]["name"]
    elif do:
        ejemplo = "%s.turnOn()" % do[0]["name"]
    elif ai:
        ejemplo = "%s.value()" % ai[0]["name"]

    return HEADER_TMPL.format(
        src=os.path.basename(src), board=board,
        ndi=len(di), ndo=len(do), nai=len(ai),
        di_rows=di_rows, do_rows=do_rows, ai_rows=ai_rows,
        ejemplo=ejemplo)


# ---------------------------------------------------------------------------
def run(diagram_path, out_path, check_only=False, quiet=False):
    if not os.path.exists(diagram_path):
        print("[CoreFSM] No hay %s: no se genera nada." % diagram_path)
        return 0

    with open(diagram_path, "r", encoding="utf-8") as f:
        diagram = json.load(f)

    overrides = {}
    ov_path = os.path.join(os.path.dirname(diagram_path) or ".", "corefsm.json")
    if os.path.exists(ov_path):
        with open(ov_path, "r", encoding="utf-8") as f:
            overrides = json.load(f)

    board, signals, warnings = parse_diagram(diagram, overrides)
    header = emit_header(board, signals, diagram_path)

    if not quiet:
        di = sum(1 for s in signals if s["role"] == "DI")
        do = sum(1 for s in signals if s["role"] == "DO")
        ai = sum(1 for s in signals if s["role"] == "AI")
        print("[CoreFSM] Placa '%s': %d DI, %d DO, %d AI" % (board, di, do, ai))
        for s in signals:
            print("          %-3s  %-4s %s" % (s["pin"], s["role"], s["name"]))
        if warnings:
            print("[CoreFSM] AVISOS:")
            for w in warnings:
                print(w)

    if check_only:
        return 1 if warnings else 0

    d = os.path.dirname(out_path)
    if d and not os.path.isdir(d):
        os.makedirs(d)

    # No reescribir si no ha cambiado: evita recompilaciones innecesarias.
    if os.path.exists(out_path):
        with open(out_path, "r", encoding="utf-8") as f:
            if f.read() == header:
                if not quiet:
                    print("[CoreFSM] %s ya estaba al dia." % out_path)
                return 0

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header)
    if not quiet:
        print("[CoreFSM] Escrito %s" % out_path)
    return 0


# ---------------------------------------------------------------------------
# Modo hook de PlatformIO
# ---------------------------------------------------------------------------
def _platformio_hook():
    try:
        Import("env")          # noqa: F821  (lo inyecta SCons)
    except NameError:
        return False
    project = env.get("PROJECT_DIR")            # noqa: F821
    include = env.get("PROJECT_INCLUDE_DIR")    # noqa: F821
    run(os.path.join(project, "diagram.json"),
        os.path.join(include, "HardwareConfig.h"))
    return True


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(
        description="Genera HardwareConfig.h de CoreFSM desde el diagram.json de Wokwi.")
    ap.add_argument("-i", "--input", default="diagram.json")
    ap.add_argument("-o", "--output", default="HardwareConfig.h")
    ap.add_argument("--check", action="store_true",
                    help="valida y avisa, pero no escribe nada")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()
    sys.exit(run(a.input, a.output, a.check, a.quiet))
else:
    _platformio_hook()
