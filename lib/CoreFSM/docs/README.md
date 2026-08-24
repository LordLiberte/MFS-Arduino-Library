# Documentación técnica de CoreFSM

Esta referencia sigue la estructura de `src/`: cada documento describe la API,
semántica y límites de una cabecera pública. Para una introducción progresiva
consulta primero el `README.md` de la librería; para verificar un detalle de
comportamiento, contrasta siempre esta guía con el código de la versión usada.

> **Seguridad:** CoreFSM no es software de seguridad certificado. Lee
> [SAFETY.md](../../../SAFETY.md) antes de conectar actuadores o diseñar una
> parada de emergencia.

## Entrada principal

- [`CoreFSM.h`](CoreFSM.md): cabecera agregadora, orden de scan y mapa de módulos.

## Núcleo

| Cabecera | Tema |
|---|---|
| [`CoreFSM_Platform.h`](core/CoreFSM_Platform.md) | plataformas, NVM, tiempo y cadenas en flash |
| [`ControlWords.h`](core/ControlWords.md) | palabras de mando/estado y códigos de error |
| [`BlockBase.h`](core/BlockBase.md) | contrato común de bloques de lógica |
| [`FsmBlock.h`](core/FsmBlock.md) | ciclo de vida de la máquina de estados |
| [`SequenceBlock.h`](core/SequenceBlock.md) | secuencias, pasos, vigilancias y tiempos |
| [`BlockManager.h`](core/BlockManager.md) | orquestación del OB1 y difusión de órdenes |
| [`Handshake.h`](core/Handshake.md) | coordinación local de cuatro fases |
| [`IAxis.h`](core/IAxis.md) | interfaz abstracta de eje |

## Entradas y salidas

| Cabecera | Tema |
|---|---|
| [`IDevice.h`](io/IDevice.md) | contrato de objetos de campo |
| [`DigitalBackend.h`](io/DigitalBackend.md) | GPIO nativo o canal de backend mediante `DigitalPin` |
| [`Mcp23017Backend.h`](io/Mcp23017Backend.md) | expansor I2C MCP23017 con imagen por scan |
| [`DeviceManager.h`](io/DeviceManager.md) | imagen de proceso PAE/PAA |
| [`DigitalSensor.h`](io/DigitalSensor.md) | antirrebote, flancos y forzado |
| [`DigitalOutput.h`](io/DigitalOutput.md) | modos, watchdog y PWM con rampa |
| [`AnalogSensor.h`](io/AnalogSensor.md) | filtro, escalado, umbral y validez |
| [`UltrasonicSensor.h`](io/UltrasonicSensor.md) | HC-SR04 y coste bloqueante de `pulseIn` |
| [`TowerLight.h`](io/TowerLight.md) | baliza y señalización de estados |
| [`IOTable.h`](io/IOTable.md) | X-macros y tabla simbólica de hardware |
| [`IOManager.h`](io/IOManager.md) | mapeo ligero por punteros |

## Lógica IEC 61131-3

| Cabecera | Tema |
|---|---|
| [`Timers.h`](logic/Timers.md) | TON, TOF, TP y Blink |
| [`Edges.h`](logic/Edges.md) | flancos, biestables y toggle |
| [`Counters.h`](logic/Counters.md) | CTU, CTD, CTUD y cuentahoras |

## Accionamientos

| Cabecera | Tema |
|---|---|
| [`MotorDrive.h`](drive/MotorDrive.md) | puente en H, rampa e inversión |
| [`Chassis.h`](drive/Chassis.md) | chasis diferencial, cuatro ruedas y eje P |

## Datos y recetas

| Cabecera | Tema |
|---|---|
| [`ConfigStore.h`](data/ConfigStore.md) | estructura persistente con CRC y versión |
| [`DataBlock.h`](data/DataBlock.md) | DB remanente y autoguardado |
| [`AlarmManager.h`](data/AlarmManager.md) | lista, severidad y acuse de alarmas |
| [`RecipeTypes.h`](data/RecipeTypes.md) | formato de receta y banco flash/NVM/RAM |
| [`RecipeExecutor.h`](data/RecipeExecutor.md) | intérprete de recetas y teach-in |

## Diagnóstico

| Cabecera | Tema |
|---|---|
| [`Logger.h`](diag/Logger.md) | niveles y eliminación de trazas |
| [`Telemetry.h`](diag/Telemetry.md) | trazador, CSV y consola de mantenimiento |
| [`ScanWatchdog.h`](diag/ScanWatchdog.md) | duración completa del scan y watchdog HW |

## Comunicaciones

| Cabecera | Tema |
|---|---|
| [`PacketLink.h`](comms/PacketLink.md) | tramas COBS/CRC sobre `Stream`, buffers y presupuestos |
| [`RemoteIO.h`](comms/RemoteIO.md) | gestor de endpoints e imagen digital remota |
| [`VisionSensor.h`](comms/VisionSensor.md) | resultados de visión y control `VisualServo` |

## Hardware sin dependencia de Wokwi

- [Fuentes de asignación](hardware/sources.md): qué fuentes existen y cuál es
  el contrato de salida.
- [Asignación manual](hardware/manual-pinmap.md): X-macros, objetos directos y
  configuración PlatformIO sin generador.
- [Expansores de E/S](hardware/io-expanders.md): arquitectura agrupada,
  capacidades y estado de implementación.

## Varios controladores

- [Sistemas con varios microcontroladores](net/multi-controller.md): diferencia
  entre expansión local y nodos distribuidos, API serie disponible y límites
  actuales.

## Ejemplos y pruebas

- `examples/01_PrimerBloque` introduce el ciclo de scan.
- `examples/03_Conveyor_Semaforo` muestra `IOTable` generado.
- `examples/04_DosEstaciones_Handshake` coordina dos estaciones en el mismo MCU.
- `examples/05_Recetas_y_Config` cubre persistencia y recetas.
- `examples/06_Robot_4Ruedas` y `07_Vision_Seguimiento` cubren accionamientos.
- `examples/08_Esperas_y_Ritmo` muestra tiempos y watchdog.
- `tests/README.md` explica el banco host reproducible.

## Convenciones

- PAE: lectura y captura de entradas al inicio del scan.
- OB1: ejecución ordenada de bloques.
- PAA: aplicación de salidas al final del scan.
- Los ejemplos de código solo son compilables cuando los tipos citados existen
  en la versión instalada; las propuestas futuras se marcan expresamente.
- Los tamaños indicados para AVR dependen de alineación, compilador y opciones.
