# 00 · Prueba de la librería

Proyecto mínimo para Arduino Nano. Un pulsador entre D2 y GND controla el LED
integrado de D13 mediante una pequeña secuencia CoreFSM.

## Compilar

Abre **esta carpeta** en VS Code y ejecuta `Ctrl+Alt+B`. Para cargar usa
`Ctrl+Alt+U`; para el monitor serie, `Ctrl+Alt+S`.

## Cambiar el cableado

La fuente de hardware es `hardware.csv`. Cada fila declara un nombre, tipo y
destino. Al compilar, el generador valida la tabla y actualiza
`include/HardwareConfig.h`; la lógica solo ve nombres como
`HW.Pulsador_Marcha`.

`diagram.json` y `wokwi.toml` se conservan para simular este mismo circuito,
pero Wokwi no es necesario para generar ni compilar el firmware.

| Archivo | Función |
|---|---|
| `hardware.csv` | asignación canónica de entradas y salidas |
| `corefsm.json` | fuente, nodo, valores por defecto y backends |
| `include/HardwareConfig.h` | cabecera generada y versionada |
| `src/main.cpp` | ciclo de scan y conexión con la planta |
| `src/Proceso.h` | lógica independiente de los pines |
| `diagram.json` | circuito opcional para Wokwi |

Consulta la [guía de CoreFSM](../../lib/CoreFSM/README.md) y el
[aviso de seguridad](../../SAFETY.md).
