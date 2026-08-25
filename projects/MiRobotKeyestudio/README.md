# MiRobotKeyestudio

Proyecto de CoreFSM. Placa: **nanoatmega328**.

## Compilar

Abre **esta carpeta** en VS Code (no la raíz del repositorio) y pulsa
`Ctrl+Alt+B`. Para cargar, `Ctrl+Alt+U`. Monitor serie, `Ctrl+Alt+S`.

## Configurar el hardware

Edita `hardware.csv`: una fila por señal. `target` acepta `gpio.2`, `A0` o `EXP1.3`; los backends se declaran en `corefsm.json`.

Tras cambiar la asignación, compila. En los modos CSV, JSON y Wokwi el
generador actualiza `include/HardwareConfig.h`; el código de proceso continúa
usando nombres estables como `HW.Mi_Sensor.hasRisen()`.

## Archivos

| Archivo | Qué es |
|---|---|
| `src/main.cpp` | El ciclo de scan y la conexión con el hardware |
| `src/Proceso.h` | La lógica del proceso. **Aquí va tu trabajo.** |
| `hardware.csv` | Tabla manual y fuente de verdad del cableado |
| `corefsm.json` | Fuente, nodo, valores por defecto y backends |
| `include/HardwareConfig.h` | Generado; no lo edites porque se reescribe al compilar. |

La guía completa de la librería está en `../../lib/CoreFSM/README.md`.
