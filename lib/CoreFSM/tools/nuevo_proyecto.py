#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nuevo_proyecto.py
=================
Crea una carpeta de proyecto nueva bajo `projects/` o `proyectos/` por defecto,
o en la ubicación indicada con `--dir`; queda enlazada con la librería y lista
para compilar sin tocar nada.

POR QUÉ EXISTE
--------------
Un proyecto nuevo necesita varios archivos que siempre dicen casi lo mismo y
rutas hacia la copia compartida de CoreFSM. El script calcula esas rutas desde
la ubicación real. Copiar y pegar el proyecto anterior funciona hasta el día en
que se olvida cambiar el nombre o una ruta en un sitio.

Que lo genere un script tiene además una consecuencia útil aguas abajo: como
todos los proyectos salen con la misma forma, el CI puede descubrirlos solo
buscando `projects/*/platformio.ini` o `proyectos/*/platformio.ini`. No hay que
registrar nada en ningún sitio.

USO
---
    python lib/CoreFSM/tools/nuevo_proyecto.py 03_brazo
    python lib/CoreFSM/tools/nuevo_proyecto.py 04_dosificador --placa esp32
    python lib/CoreFSM/tools/nuevo_proyecto.py 05_simulacion --fuente wokwi
    python lib/CoreFSM/tools/nuevo_proyecto.py 06_manual --fuente manual

Se puede lanzar desde cualquier sitio del repositorio: busca la raíz solo.
"""

import argparse
import json
import os
import re
import subprocess
import sys

# ---------------------------------------------------------------------------
#  Placas admitidas
# ---------------------------------------------------------------------------
#  Cada entrada junta las tres identidades de una misma placa, que no coinciden
#  entre sí y es justo lo que más se confunde:
#    - 'plataforma' y 'board' son lo que entiende PlatformIO
#    - 'tipo' es lo que entiende Wokwi
#    - 'pin_*' son los pines del esquema de ejemplo
# ---------------------------------------------------------------------------
PLACAS = {
    "nano":  {"plataforma": "atmelavr",    "board": "nanoatmega328",
              "tipo": "wokwi-arduino-nano", "id": "nano",
              "pin_btn": "2", "pin_led": "13",
              "wokwi_pin_btn": "2", "wokwi_pin_led": "13",
              "firmware_ext": "hex"},
    "uno":   {"plataforma": "atmelavr",    "board": "uno",
              "tipo": "wokwi-arduino-uno",  "id": "uno",
              "pin_btn": "2", "pin_led": "13",
              "wokwi_pin_btn": "2", "wokwi_pin_led": "13",
              "firmware_ext": "hex"},
    "mega":  {"plataforma": "atmelavr",    "board": "megaatmega2560",
              "tipo": "wokwi-arduino-mega", "id": "mega",
              "pin_btn": "2", "pin_led": "13",
              "wokwi_pin_btn": "2", "wokwi_pin_led": "13",
              "firmware_ext": "hex"},
    "esp32": {"plataforma": "espressif32", "board": "esp32dev",
              "tipo": "board-esp32-devkit-c-v4", "id": "esp",
              "pin_btn": "4", "pin_led": "2",
              "wokwi_pin_btn": "4", "wokwi_pin_led": "2",
              "firmware_ext": "bin"},
}


CARPETAS_PROYECTOS = ("proyectos", "projects")


def carpeta_de_proyectos(raiz, forzada=None):
    """Decide dónde van los proyectos.

    Admite `proyectos/` y `projects/` porque el nombre lo elige quien monta el
    repositorio, y una herramienta que solo entiende uno de los dos obliga a
    renombrar carpetas por un capricho de idioma. Si no existe ninguna, crea
    `proyectos/`.
    """
    if forzada:
        return os.path.join(raiz, forzada)
    for nombre in CARPETAS_PROYECTOS:
        if os.path.isdir(os.path.join(raiz, nombre)):
            return os.path.join(raiz, nombre)
    return os.path.join(raiz, CARPETAS_PROYECTOS[0])


def raiz_del_repo(desde):
    """Sube por el árbol de carpetas hasta encontrar la raíz del repositorio.

    Se reconoce por tener `lib/CoreFSM/`. Se comprueba eso y no `.git`, porque
    el script tiene que funcionar igual en una copia descargada como ZIP, sin
    historial de Git.
    """
    d = os.path.abspath(desde)
    while True:
        if os.path.isdir(os.path.join(d, "lib", "CoreFSM")):
            return d
        padre = os.path.dirname(d)
        if padre == d:
            raise SystemExit(
                "ERROR: no encuentro la raiz del repositorio.\n"
                "       Tiene que existir una carpeta lib/CoreFSM/ por encima\n"
                "       de donde estas. Lanza el script desde dentro del repo.")
        d = padre


def nombre_valido(n):
    """El nombre acaba siendo parte de rutas, de un identificador de entorno de
    PlatformIO y de una referencia en el CI. Se restringe a lo que sobrevive a
    los tres sitios sin sorpresas."""
    if not re.fullmatch(r"[A-Za-z0-9_-]+", n):
        raise SystemExit(
            "ERROR: '%s' no vale como nombre de proyecto.\n"
            "       Solo letras, numeros, guion y guion bajo. Sin espacios ni\n"
            "       acentos: acaba siendo parte de rutas y de nombres de\n"
            "       entorno de PlatformIO." % n)
    return n


def ruta_desde(origen, destino):
    """Devuelve una ruta portable desde ``origen`` hasta ``destino``.

    En Windows no existe una ruta relativa entre unidades distintas. En ese
    caso se conserva la ruta absoluta, que PlatformIO también admite.
    """
    try:
        ruta = os.path.relpath(destino, origen)
    except ValueError:
        ruta = os.path.abspath(destino)
    return ruta.replace("\\", "/")


# ---------------------------------------------------------------------------
#  Plantillas
# ---------------------------------------------------------------------------
PLATFORMIO_INI = """; =============================================================================
;  {nombre}
; -----------------------------------------------------------------------------
;  Generado por nuevo_proyecto.py. Abre ESTA carpeta en VS Code, no la raiz del
;  repositorio: PlatformIO busca el platformio.ini en la raiz de lo que abras.
; =============================================================================

[env:{placa}]
platform      = {plataforma}
board         = {board}
framework     = arduino
monitor_speed = 115200

; La libreria se comparte con los demas proyectos del repositorio.
; lib_extra_dirs apunta a la carpeta que CONTIENE librerias, no a la libreria.
; Arreglar un fallo en lib/CoreFSM lo arregla para todos los proyectos a la vez.
lib_extra_dirs = {lib_dir}

{generation_block}

build_flags =
  -Wall
  ; -D CFSM_LOG_LEVEL=0        ; quita todas las trazas (libera flash y RAM)
  ; -D CFSM_RECIPE_AXES=2      ; tamano de las recetas
  ; -D CFSM_RECIPE_MAX_STEPS=4

; Si la carga falla con un timeout en un Nano fabricado a partir de 2018,
; cambia la linea 'board' de arriba por:  board = nanoatmega328new
"""

WOKWI_TOML = """# Simulacion dentro de VS Code (extension "Wokwi for VS Code").
# Compila primero, y luego Ctrl+Mayus+P -> "Wokwi: Start Simulator".
# Si cambias el nombre del entorno en platformio.ini, cambialo tambien aqui.
[wokwi]
version  = 1
firmware = '.pio/build/{placa}/firmware.{firmware_ext}'
elf      = '.pio/build/{placa}/firmware.elf'
"""

GENERATION_BLOCK = """; 'pre:' se ejecuta ANTES de compilar. La fuente elegida en
; corefsm.json se convierte en include/HardwareConfig.h.
extra_scripts  = pre:{corefsm_dir}/tools/corefsm_gen.py"""

MANUAL_GENERATION_BLOCK = """; HardwareConfig.h se mantiene a mano en este proyecto.
; No hay generador previo a la compilacion."""

HARDWARE_CSV = """node,name,role,target,pullup,active_low,debounce_ms,filter,safe
main,{tag_btn},DI,gpio.{pin_btn},true,,20,,
main,{tag_led},DO,gpio.{pin_led},,false,,,false
"""

MANUAL_HARDWARE_CONFIG = """/* =============================================================================
 *  HardwareConfig.h  -  CONFIGURACION MANUAL
 * -----------------------------------------------------------------------------
 *  Este archivo es la fuente de verdad del cableado. Edita las filas y conserva
 *  la aridad de cada tabla.
 * ========================================================================== */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

#define CFSM_TABLE_DI(ROW) \\
  ROW( {pin_btn:>4}, {tag_btn:<32}, true ,  20 )

#define CFSM_TABLE_DO(ROW) \\
  ROW( {pin_led:>4}, {tag_led:<32}, false )

#define CFSM_TABLE_AI(ROW) \\
  /* (ninguna) */

#include <io/IOTable.h>

#endif /* HARDWARE_CONFIG_H */
"""

MAIN_CPP = """/* =============================================================================
 *  {nombre}
 * -----------------------------------------------------------------------------
 *  Esqueleto generado. Compila y funciona tal cual con un pulsador y un LED.
 *
 *  EL CICLO DE SCAN son las tres fases del final de este archivo, siempre en
 *  ese orden. Todo lo demas son bloques que se registran.
 *
 *  RECUERDA, que aqui no estamos en el IDE de Arduino:
 *    - hay que incluir <Arduino.h> a mano
 *    - hay que declarar los prototipos antes de usar las funciones
 * ========================================================================== */

#include <Arduino.h>
#include "HardwareConfig.h"     // tabla de hardware: {source_label}
#include "Proceso.h"

/* Crea la instancia global HW con todos los objetos de la tabla. */
CFSM_DEFINE_HARDWARE;

BlockManager<4> manager;
Proceso         proceso;

StepTracer            tracer(proceso, Serial);
MaintenanceConsole<4> consola(manager, Serial);

/* --- Prototipos ----------------------------------------------------------- */
void leerEntradas();
void escribirSalidas();

void setup() {{
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {{ }}
  Serial.println(F("=== {nombre} ==="));

  HW.begin();                                  // pines e imagen de proceso
  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();
  proceso.start();

  Serial.println(F("Listo. Escribe '?' para ver los comandos."));
}}

void loop() {{
  HW.readInputs();        // FASE 1 - PAE: foto de todas las entradas
  leerEntradas();         //          planta -> interfaz del bloque
  manager.updateAll();    // FASE 2 - OB1: la logica calcula con esa foto
  escribirSalidas();      //          bloque -> planta
  tracer.update();
  consola.update();
  /* Interbloqueo global de software. No sustituye una cadena de seguridad ni
   * una parada de emergencia certificada y cableada. */
  HW.setSafetyInterlock(manager.isEmergencyStop());
  HW.writeOutputs();      // FASE 3 - PAA: volcado de todas las salidas
}}

/* -----------------------------------------------------------------------------
 *  Conexion de las senales de planta a la interfaz del bloque.
 *
 *  hasRisen() pasa el FLANCO (el instante de pulsar); isTriggered() pasa el
 *  NIVEL (el hecho de estar activo). Ordenes con flanco, estados con nivel: si
 *  se confunden, mantener el dedo en el boton relanza el ciclo miles de veces
 *  por segundo.
 * -------------------------------------------------------------------------- */
void leerEntradas() {{
  proceso.ordenMarcha = HW.{tag_btn}.hasRisen();
}}

void escribirSalidas() {{
  HW.{tag_led}.set(proceso.salida);
}}
"""

PROCESO_H = """#ifndef PROCESO_H
#define PROCESO_H

#include <CoreFSM.h>

/* =============================================================================
 *  La logica del proceso.
 *
 *  Este archivo NO sabe nada de pines, ni de tensiones, ni de que placa hay
 *  debajo: trabaja solo con variables logicas. Podrias compilarlo tal cual para
 *  otro microcontrolador, o probarlo en el PC sin hardware.
 *
 *  Quien conecta estas variables al mundo real es main.cpp.
 * ========================================================================== */

/* Numeracion de 10 en 10: deja hueco para intercalar un paso 15 el dia que
 * haga falta, sin renumerar toda la secuencia. */
enum Pasos : uint16_t {{
  PASO_REPOSO   = 0,
  PASO_TRABAJO  = 10
}};

/* Codigos de alarma propios de esta maquina. Empiezan en CFSM_ERR_USER_BASE
 * para no chocar nunca con los que genera la libreria. */
enum Alarmas : uint16_t {{
  ALM_EJEMPLO = CFSM_ERR_USER_BASE + 1
}};

class Proceso : public SequenceBlock {{
  public:
    /* ---- ENTRADAS (las alimenta main.cpp) ---- */
    bool ordenMarcha = false;

    /* ---- SALIDAS (las lee main.cpp) ---- */
    bool salida = false;

    /* ---- PARAMETROS DE PROCESO ---- */
    uint16_t tiempoTrabajoMs = 1500;

    void begin() override {{
      setName(F("PROCESO"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);
      setCycleTimeout(30000);      /* el ciclo entero no debe pasar de 30 s */
    }}

    void update() override {{
      /* Enclavamiento general. Si la maquina no esta en marcha -parada, en
       * pausa, arrancando o en fallo-, las salidas peligrosas van a su estado
       * seguro y se sale. Es LA linea mas importante del bloque: sin ella, una
       * alarma no pararia el actuador, solo dejaria de cambiar de paso. */
      if (!updateSequence()) {{ salida = false; return; }}

      switch (_currentStep) {{

        case PASO_REPOSO:
          salida = false;
          if (ordenMarcha) setStep(PASO_TRABAJO, 5000);   /* 5 s de vigilancia */
          break;

        case PASO_TRABAJO:
          salida = true;
          if (getTimeInStep() >= tiempoTrabajoMs) {{
            salida = false;
            completeCycle(PASO_REPOSO); /* cuenta, cierra y cambia de paso */
          }}
          break;
      }}
    }}

    /* Nombres de paso, para que la telemetria diga TRABAJO y no 10. */
    const __FlashStringHelper* stepName(uint16_t s) const override {{
      switch (s) {{
        case PASO_REPOSO:  return F("REPOSO");
        case PASO_TRABAJO: return F("TRABAJO");
        default:           return nullptr;
      }}
    }}

  protected:
    /* Los mensajes van AQUI, no dentro del switch: esto corre una sola vez por
     * cambio de paso. Dentro del switch correria miles de veces por segundo y
     * saturaria el buffer serie hasta bloquear la CPU. Es el error que mas
     * veces se comete al empezar. */
    void onStepEntered(uint16_t step) override {{
      switch (step) {{
        case PASO_REPOSO:  Serial.println(F("[PROCESO] En reposo")); break;
        case PASO_TRABAJO: Serial.println(F("[PROCESO] Trabajando")); break;
      }}
    }}
}};

#endif
"""

README_MD = """# {nombre}

Proyecto de CoreFSM. Placa: **{board}**.

## Compilar

Abre **esta carpeta** en VS Code (no la raíz del repositorio) y pulsa
`Ctrl+Alt+B`. Para cargar, `Ctrl+Alt+U`. Monitor serie, `Ctrl+Alt+S`.

## Configurar el hardware

{source_help}

Tras cambiar la asignación, compila. En los modos CSV, JSON y Wokwi el
generador actualiza `include/HardwareConfig.h`; el código de proceso continúa
usando nombres estables como `HW.Mi_Sensor.hasRisen()`.

## Archivos

| Archivo | Qué es |
|---|---|
| `src/main.cpp` | El ciclo de scan y la conexión con el hardware |
| `src/Proceso.h` | La lógica del proceso. **Aquí va tu trabajo.** |
{source_row}{config_row}| `include/HardwareConfig.h` | {header_description} |

La guía completa de la librería está en `{guide_path}`.
"""


def plantilla_diagrama(p, tag_btn, tag_led):
    """Esquema mínimo: la placa, un pulsador y un LED. Suficiente para que el
    proyecto compile y parpadee el primer día, y para tener un ejemplo delante
    de cómo se nombran las cosas."""
    return json.dumps({
        "version": 1,
        "author": "",
        "editor": "wokwi",
        "parts": [
            {"type": p["tipo"], "id": p["id"], "top": 0, "left": 0},
            {"type": "wokwi-pushbutton", "id": tag_btn,
             "top": 120, "left": 200, "attrs": {"color": "green"}},
            {"type": "wokwi-led", "id": tag_led,
             "top": -60, "left": 180, "attrs": {"color": "green"}},
        ],
        "connections": [
            ["%s:%s" % (p["id"], p["wokwi_pin_btn"]), "%s:1.l" % tag_btn, "green", ["v0"]],
            ["%s:GND.1" % p["id"],              "%s:2.l" % tag_btn, "black", ["v0"]],
            ["%s:%s" % (p["id"], p["wokwi_pin_led"]), "%s:A"   % tag_led, "green", ["v0"]],
            ["%s:GND.2" % p["id"],              "%s:C"   % tag_led, "black", ["v0"]],
        ],
    }, indent=2, ensure_ascii=False) + "\n"


def plantilla_hardware_json(board, pin_btn, pin_led, tag_btn, tag_led):
    """Fuente explicita equivalente al CSV inicial."""
    return json.dumps({
        "version": 1,
        "nodes": [{
            "id": "main",
            "board": board,
            "signals": [
                {"name": tag_btn, "role": "DI", "target": "gpio.%s" % pin_btn,
                 "pullup": True, "debounce_ms": 20},
                {"name": tag_led, "role": "DO", "target": "gpio.%s" % pin_led,
                 "active_low": False, "safe": False},
            ],
        }],
        "backends": [],
    }, indent=2, ensure_ascii=False) + "\n"


def plantilla_corefsm(fuente, board, board_id):
    config = {
        "source": {
            "format": fuente,
            "path": {
                "csv": "hardware.csv",
                "json": "hardware.json",
                "wokwi": "diagram.json",
            }[fuente],
        },
        "defaults": {"debounce_ms": 20},
    }
    if fuente == "csv":
        config["nodes"] = [{"id": "main", "board": board}]
        config["backends"] = []
    elif fuente == "wokwi":
        # Compatibilidad con el campo historico: tambien selecciona la placa si
        # un diagrama contiene mas de una.
        config["board"] = board_id
        config["pins"] = {}
        config["ignore"] = []
    return json.dumps(config, indent=2, ensure_ascii=False) + "\n"


SOURCE_README = {
    "csv": {
        "source_label": "hardware.csv",
        "source_help": (
            "Edita `hardware.csv`: una fila por señal. `target` acepta `gpio.2`, "
            "`A0` o `EXP1.3`; los backends se declaran en `corefsm.json`."),
        "source_row": "| `hardware.csv` | Tabla manual y fuente de verdad del cableado |\n",
        "config_row": "| `corefsm.json` | Fuente, nodo, valores por defecto y backends |\n",
        "header_description": "Generado; no lo edites porque se reescribe al compilar.",
    },
    "json": {
        "source_label": "hardware.json",
        "source_help": (
            "Edita `hardware.json`. Puede contener varios nodos, backends y "
            "señales; selecciona un nodo con `custom_corefsm_node` si procede."),
        "source_row": "| `hardware.json` | Modelo explícito de nodos, backends y señales |\n",
        "config_row": "| `corefsm.json` | Selección de fuente y valores por defecto |\n",
        "header_description": "Generado; no lo edites porque se reescribe al compilar.",
    },
    "wokwi": {
        "source_label": "diagram.json de Wokwi",
        "source_help": (
            "Dibuja el circuito en [wokwi.com](https://wokwi.com), asigna un `id` "
            "claro a cada componente y guarda el resultado en `diagram.json`."),
        "source_row": "| `diagram.json` | Esquema Wokwi y fuente de verdad del cableado |\n",
        "config_row": "| `corefsm.json` | Selección de placa y ajustes finos |\n",
        "header_description": "Generado; no lo edites porque se reescribe al compilar.",
    },
    "manual": {
        "source_label": "HardwareConfig.h manual",
        "source_help": (
            "Edita directamente `include/HardwareConfig.h`. En este modo no hay "
            "generador ni otro archivo que pueda sobrescribirlo."),
        "source_row": "",
        "config_row": "",
        "header_description": "Fuente de verdad manual; conserva la forma de las tablas.",
    },
}


def escribir(ruta, contenido):
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(contenido)
    print("   + %s" % ruta_desde(os.getcwd(), ruta))


def main():
    ap = argparse.ArgumentParser(
        description="Crea un proyecto nuevo de CoreFSM listo para compilar.")
    ap.add_argument("nombre", help="nombre de la carpeta, p. ej. 03_brazo")
    ap.add_argument("--placa", default="nano", choices=sorted(PLACAS),
                    help="placa de destino (por defecto: nano)")
    ap.add_argument("--fuente", choices=("csv", "json", "wokwi", "manual"),
                    default=None,
                    help="fuente del hardware (por defecto: csv)")
    ap.add_argument("--sin-wokwi", action="store_true",
                    help="alias obsoleto de --fuente csv")
    ap.add_argument("--forzar", action="store_true",
                    help="sobrescribir si la carpeta ya existe")
    ap.add_argument("--dir", default=None,
                    help="carpeta donde crear el proyecto "
                         "(por defecto: proyectos/ o projects/, la que exista)")
    a = ap.parse_args()

    fuente = a.fuente or "csv"
    if a.sin_wokwi:
        if a.fuente not in (None, "csv"):
            ap.error("--sin-wokwi no se puede combinar con --fuente %s" % a.fuente)
        fuente = "csv"
        print("AVISO: --sin-wokwi esta obsoleto; usa --fuente csv.", file=sys.stderr)

    nombre = nombre_valido(a.nombre)
    placa = PLACAS[a.placa]
    raiz = raiz_del_repo(os.getcwd())
    base = carpeta_de_proyectos(raiz, a.dir)
    destino = os.path.join(base, nombre)

    if os.path.exists(destino) and not a.forzar:
        raise SystemExit(
            "ERROR: ya existe %s\n"
            "       Elige otro nombre, o usa --forzar para sobrescribir."
            % ruta_desde(raiz, destino))

    tag_btn = "Pulsador_Marcha"
    tag_led = "Piloto_Trabajo"

    print("Creando proyecto '%s' (placa %s, fuente %s)" %
          (nombre, a.placa, fuente))
    os.makedirs(os.path.join(destino, "src"), exist_ok=True)
    os.makedirs(os.path.join(destino, "include"), exist_ok=True)

    # Las rutas se calculan desde el proyecto real. Así --dir funciona tanto
    # dentro de projects/ como en una carpeta profunda o absoluta.
    lib_dir = ruta_desde(destino, os.path.join(raiz, "lib"))
    corefsm_dir = ruta_desde(destino, os.path.join(raiz, "lib", "CoreFSM"))

    readme_fields = SOURCE_README[fuente]
    generation_template = (MANUAL_GENERATION_BLOCK if fuente == "manual"
                           else GENERATION_BLOCK)
    generation_block = generation_template.format(corefsm_dir=corefsm_dir)
    campos = dict(
        nombre=nombre, placa=a.placa, tag_btn=tag_btn, tag_led=tag_led,
        generation_block=generation_block,
        lib_dir=lib_dir,
        corefsm_dir=corefsm_dir,
        guide_path=corefsm_dir + "/README.md",
        source_label=readme_fields["source_label"],
        source_help=readme_fields["source_help"],
        source_row=readme_fields["source_row"],
        config_row=readme_fields["config_row"],
        header_description=readme_fields["header_description"],
        **placa)

    escribir(os.path.join(destino, "platformio.ini"), PLATFORMIO_INI.format(**campos))
    escribir(os.path.join(destino, "src", "main.cpp"), MAIN_CPP.format(**campos))
    escribir(os.path.join(destino, "src", "Proceso.h"), PROCESO_H.format(**campos))
    escribir(os.path.join(destino, "README.md"), README_MD.format(**campos))

    if fuente == "csv":
        escribir(os.path.join(destino, "hardware.csv"), HARDWARE_CSV.format(**campos))
        escribir(os.path.join(destino, "corefsm.json"),
                 plantilla_corefsm("csv", a.placa, placa["id"]))
    elif fuente == "json":
        escribir(os.path.join(destino, "hardware.json"),
                 plantilla_hardware_json(a.placa, placa["pin_btn"], placa["pin_led"],
                                          tag_btn, tag_led))
        escribir(os.path.join(destino, "corefsm.json"),
                 plantilla_corefsm("json", a.placa, placa["id"]))
    elif fuente == "wokwi":
        escribir(os.path.join(destino, "diagram.json"),
                 plantilla_diagrama(placa, tag_btn, tag_led))
        escribir(os.path.join(destino, "corefsm.json"),
                 plantilla_corefsm("wokwi", a.placa, placa["id"]))
        escribir(os.path.join(destino, "wokwi.toml"), WOKWI_TOML.format(**campos))
    else:
        pin_btn = re.sub(r"^(?:D|GP|GPIO)", "", placa["pin_btn"], flags=re.I)
        pin_led = re.sub(r"^(?:D|GP|GPIO)", "", placa["pin_led"], flags=re.I)
        manual_fields = dict(campos, pin_btn=pin_btn, pin_led=pin_led)
        escribir(os.path.join(destino, "include", "HardwareConfig.h"),
                 MANUAL_HARDWARE_CONFIG.format(**manual_fields))

    # Git ignora las carpetas vacias, asi que include/ desapareceria al clonar.
    escribir(os.path.join(destino, "include", ".gitkeep"), "")

    # Generar ya la tabla de hardware, para que el proyecto compile de entrada.
    generador = os.path.join(raiz, "lib", "CoreFSM", "tools", "corefsm_gen.py")
    if fuente != "manual" and os.path.exists(generador):
        print("Generando la tabla de hardware...")
        subprocess.run([sys.executable, generador,
                        "--project", destino,
                        "-o", os.path.join(destino, "include", "HardwareConfig.h")],
                       check=True)

    rel = ruta_desde(raiz, destino)
    source_path = {
        "csv": "hardware.csv", "json": "hardware.json",
        "wokwi": "diagram.json", "manual": "include/HardwareConfig.h",
    }[fuente]
    print("\nListo.\n"
          "  1. Abre la carpeta %s en VS Code (no la raiz del repo).\n"
          "  2. Edita %s/%s para cambiar el cableado.\n"
          "  3. Ctrl+Alt+B para compilar.\n" % (rel, rel, source_path))


if __name__ == "__main__":
    main()
