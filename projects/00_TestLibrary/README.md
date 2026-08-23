# 00_TestLibrary

Proyecto de CoreFSM. Placa: **nanoatmega328**.

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
