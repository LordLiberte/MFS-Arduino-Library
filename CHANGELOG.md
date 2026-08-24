# Historial de cambios

Este proyecto sigue [Semantic Versioning](https://semver.org/).

## 2.2.0 · 2026-08-23

### Añadido

- Fuente de hardware neutral mediante `hardware.csv` y `hardware.json`.
- Selección explícita de nodo y validación común para CSV, JSON y Wokwi.
- Configuración manual de `HardwareConfig.h` como flujo de primera clase.
- Abstracción `IDigitalBackend` y pines digitales sobre GPIO o backend.
- Backend MCP23017 con imagen de 16 bits y operaciones I2C agrupadas por scan.
- Estado seguro configurable por salida e interbloqueo global de software.
- Transporte de paquetes COBS/CRC y snapshots digitales entre nodos con timeout.
- Ejemplos de dos placas por UART y expansión MCP23017.
- Referencia técnica organizada por módulos, guías de hardware/red y aviso de
  seguridad.
- Pruebas del generador y banco host para E/S, backends y comunicaciones.

### Cambiado

- `nuevo_proyecto.py` usa CSV por defecto y permite `csv`, `json`, `wokwi` o
  `manual`; `--sin-wokwi` se conserva como alias obsoleto de CSV.
- Wokwi pasa de requisito a adaptador opcional compatible.
- El proyecto de referencia usa CSV como fuente canónica y conserva Wokwi solo
  para simulación.
- `DeviceManager` separa captura de backends, lectura de objetos, escritura de
  objetos y commit agrupado.

### Corregido

- `--sin-wokwi` ya genera un proyecto completo y compilable.
- `--dir` calcula las rutas de librería, hook y documentación desde la ubicación
  real del proyecto, incluso fuera de `projects/`.
- Un diagrama con varias placas ya no selecciona la primera silenciosamente.
- `completeCycle()` ya puede cambiar de paso sin borrar el pulso `done`.
- Correcciones de regresión en ejes, inversión de motor, recetas, alarmas,
  entradas con rebote/forzado, ADC, visión, watchdog y datos persistentes.
- El MCP23017 normaliza `IOCON` tras reinicios parciales y precarga el latch
  antes de habilitar salidas para evitar pulsos al arrancar.
- La integración continua compila también un proyecto ESP32 recién generado.

### Seguridad

- La API histórica `setEmergencyStop()` se documenta correctamente como
  interbloqueo de software, no como parada de emergencia certificada.
- Las plantillas conectan el interbloqueo lógico con el estado seguro de las
  salidas y evitan restaurar órdenes antiguas al liberarlo.

## 2.1.0

- Estados de espera `SUSPENDED` y `HELD`, objetivo de takt, avisos de paso y
  vigilancia del tiempo de scan.

## 2.0.0

- Reorganización modular del núcleo, imagen de proceso, recetas, datos,
  diagnóstico y compatibilidad con la API anterior.
