#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
nuevo_proyecto.py
=================
Crea una carpeta de proyecto nueva bajo `proyectos/`, ya cableada contra la
librería y lista para compilar sin tocar nada.

POR QUÉ EXISTE
--------------
Un proyecto nuevo necesita seis archivos que siempre dicen casi lo mismo, y dos
rutas relativas (`../../lib`) que si te equivocas dan un error de compilación
que no se parece en nada a la causa. Copiar y pegar el proyecto anterior
funciona hasta el día en que se te olvida cambiar el nombre en un sitio.

Que lo genere un script tiene además una consecuencia útil aguas abajo: como
todos los proyectos salen con la misma forma, el CI puede descubrirlos solo
buscando `proyectos/*/platformio.ini`. No hay que registrar nada en ningún
sitio.

USO
---
    python lib/CoreFSM/tools/nuevo_proyecto.py 03_brazo
    python lib/CoreFSM/tools/nuevo_proyecto.py 04_dosificador --placa esp32
    python lib/CoreFSM/tools/nuevo_proyecto.py 05_prueba --sin-wokwi

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
              "pin_btn": "2", "pin_led": "13"},
    "uno":   {"plataforma": "atmelavr",    "board": "uno",
              "tipo": "wokwi-arduino-uno",  "id": "uno",
              "pin_btn": "2", "pin_led": "13"},
    "mega":  {"plataforma": "atmelavr",    "board": "megaatmega2560",
              "tipo": "wokwi-arduino-mega", "id": "mega",
              "pin_btn": "2", "pin_led": "13"},
    "esp32": {"plataforma": "espressif32", "board": "esp32dev",
              "tipo": "board-esp32-devkit-c-v4", "id": "esp",
              "pin_btn": "D4", "pin_led": "D2"},
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

; La libreria vive dos niveles mas arriba, compartida con los demas proyectos.
; lib_extra_dirs apunta a la carpeta que CONTIENE librerias, no a la libreria.
; Arreglar un fallo en lib/CoreFSM lo arregla para todos los proyectos a la vez.
lib_extra_dirs = ../../lib

; 'pre:' = se ejecuta ANTES de compilar. Lee diagram.json y reescribe
; include/HardwareConfig.h, de modo que mover un cable en el esquema basta para
; que el software lea el pin nuevo.
extra_scripts  = pre:../../lib/CoreFSM/tools/wokwi2corefsm.py

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
firmware = '.pio/build/{placa}/firmware.hex'
elf      = '.pio/build/{placa}/firmware.elf'
"""

COREFSM_JSON = """{{
  "defaults": {{ "debounce": 20 }},
  "pins": {{
    "_comentario": "Ajustes finos por PIN de placa. Ejemplos abajo, borra esto.",
    "_ejemplo_entrada": {{ "name": "FC_Trabajo", "role": "DI", "debounce": 5 }},
    "_ejemplo_salida":  {{ "name": "Rele_Bomba", "role": "DO", "activeLow": true }}
  }},
  "ignore": []
}}
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
#include "HardwareConfig.h"     // tabla generada desde diagram.json
#include "Proceso.h"

/* Crea la instancia global HW con todos los objetos de la tabla. */
CFSM_DEFINE_HARDWARE

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
            completeCycle();          /* cuenta la pieza y cierra el ciclo */
            setStep(PASO_REPOSO);
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

## Flujo de trabajo

1. Dibuja el circuito en [wokwi.com](https://wokwi.com) y **ponle nombre a cada
   componente**: el `id` de Wokwi se convierte en el nombre de la variable.
2. Pega el resultado en `diagram.json`.
3. Compila. El generador reescribe `include/HardwareConfig.h` solo, y ya puedes
   escribir `HW.Mi_Sensor.hasRisen()`.

Ajustes que el esquema no puede expresar (antirrebote de un sensor concreto, un
relé activo a nivel bajo, un pin a ignorar) van en `corefsm.json`.

## Archivos

| Archivo | Qué es |
|---|---|
| `src/main.cpp` | El ciclo de scan y la conexión con el hardware |
| `src/Proceso.h` | La lógica del proceso. **Aquí va tu trabajo.** |
| `diagram.json` | El esquema. La única fuente de verdad del cableado. |
| `corefsm.json` | Ajustes finos del generador |
| `include/HardwareConfig.h` | **Generado.** No lo edites: se reescribe solo. |

La guía completa de la librería está en `../../lib/CoreFSM/README.md`.
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
            ["%s:%s" % (p["id"], p["pin_btn"]), "%s:1.l" % tag_btn, "green", ["v0"]],
            ["%s:GND.1" % p["id"],              "%s:2.l" % tag_btn, "black", ["v0"]],
            ["%s:%s" % (p["id"], p["pin_led"]), "%s:A"   % tag_led, "green", ["v0"]],
            ["%s:GND.2" % p["id"],              "%s:C"   % tag_led, "black", ["v0"]],
        ],
    }, indent=2, ensure_ascii=False) + "\n"


def escribir(ruta, contenido):
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(contenido)
    print("   + %s" % os.path.relpath(ruta))


def main():
    ap = argparse.ArgumentParser(
        description="Crea un proyecto nuevo de CoreFSM bajo proyectos/.")
    ap.add_argument("nombre", help="nombre de la carpeta, p. ej. 03_brazo")
    ap.add_argument("--placa", default="nano", choices=sorted(PLACAS),
                    help="placa de destino (por defecto: nano)")
    ap.add_argument("--sin-wokwi", action="store_true",
                    help="no crear diagram.json ni wokwi.toml")
    ap.add_argument("--forzar", action="store_true",
                    help="sobrescribir si la carpeta ya existe")
    ap.add_argument("--dir", default=None,
                    help="carpeta donde crear el proyecto "
                         "(por defecto: proyectos/ o projects/, la que exista)")
    a = ap.parse_args()

    nombre = nombre_valido(a.nombre)
    placa = PLACAS[a.placa]
    raiz = raiz_del_repo(os.getcwd())
    base = carpeta_de_proyectos(raiz, a.dir)
    destino = os.path.join(base, nombre)

    if os.path.exists(destino) and not a.forzar:
        raise SystemExit(
            "ERROR: ya existe %s\n"
            "       Elige otro nombre, o usa --forzar para sobrescribir."
            % os.path.relpath(destino, raiz))

    tag_btn = "Pulsador_Marcha"
    tag_led = "Piloto_Trabajo"

    print("Creando proyecto '%s' (placa %s)" % (nombre, a.placa))
    os.makedirs(os.path.join(destino, "src"), exist_ok=True)
    os.makedirs(os.path.join(destino, "include"), exist_ok=True)

    campos = dict(nombre=nombre, placa=a.placa, tag_btn=tag_btn, tag_led=tag_led,
                  **placa)

    escribir(os.path.join(destino, "platformio.ini"), PLATFORMIO_INI.format(**campos))
    escribir(os.path.join(destino, "src", "main.cpp"), MAIN_CPP.format(**campos))
    escribir(os.path.join(destino, "src", "Proceso.h"), PROCESO_H.format(**campos))
    escribir(os.path.join(destino, "README.md"), README_MD.format(**campos))

    if not a.sin_wokwi:
        escribir(os.path.join(destino, "diagram.json"),
                 plantilla_diagrama(placa, tag_btn, tag_led))
        escribir(os.path.join(destino, "corefsm.json"), COREFSM_JSON.format())
        escribir(os.path.join(destino, "wokwi.toml"), WOKWI_TOML.format(**campos))

    # Git ignora las carpetas vacias, asi que include/ desapareceria al clonar.
    escribir(os.path.join(destino, "include", ".gitkeep"), "")

    # Generar ya la tabla de hardware, para que el proyecto compile de entrada.
    generador = os.path.join(raiz, "lib", "CoreFSM", "tools", "wokwi2corefsm.py")
    if not a.sin_wokwi and os.path.exists(generador):
        print("Generando la tabla de hardware...")
        subprocess.run([sys.executable, generador,
                        "-i", os.path.join(destino, "diagram.json"),
                        "-o", os.path.join(destino, "include", "HardwareConfig.h")],
                       check=False)

    rel = os.path.relpath(destino, raiz).replace("\\", "/")
    print("\nListo.\n"
          "  1. Abre la carpeta %s en VS Code (no la raiz del repo).\n"
          "  2. Ctrl+Alt+B para compilar.\n"
          "  3. Cuando funcione, dibuja tu circuito en wokwi.com y pega el\n"
          "     resultado en %s/diagram.json\n" % (rel, rel))


if __name__ == "__main__":
    main()
