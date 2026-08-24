# CoreFSM.h

> Cabecera de conveniencia que reúne la API pública general de CoreFSM.

**Ruta:** `src/CoreFSM.h`  
**Versión declarada:** `CFSM_VERSION_STR` en [`core/CoreFSM_Platform.h`](core/CoreFSM_Platform.md)  
**Excepción deliberada:** no incluye `io/IOTable.h`.

## 1. Qué incluye

`#include <CoreFSM.h>` incorpora los módulos generales de la librería:

| Grupo | Cabeceras |
|---|---|
| Núcleo | plataforma, palabras de control, bloques, secuencias, manager, handshake y ejes |
| Lógica | temporizadores, flancos y contadores |
| E/S | dispositivos, GPIO/backends, sensores, salidas, baliza, ultrasonidos e imagen ligera |
| Accionamientos | motor, chasis y eje posicionado |
| Datos | configuración, DB, alarmas y recetas |
| Diagnóstico | logger, telemetría y watchdog de scan |
| Comunicaciones | transporte de paquetes, imagen digital remota y sensor de visión por `Stream` |

La cabecera solo agrega `#include`; no crea objetos ni ejecuta inicialización.
Incluirla no configura pines, buses, EEPROM ni puerto serie.

## 2. La excepción: `IOTable.h`

`io/IOTable.h` genera un tipo a partir de las macros `CFSM_TABLE_DI`,
`CFSM_TABLE_DO` y `CFSM_TABLE_AI`. Por eso no puede incluirse antes de que el
proyecto defina esas tablas:

```cpp
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

#define CFSM_TABLE_DI(ROW) \
  ROW(2, PulsadorMarcha, true, 20)

#define CFSM_TABLE_DO(ROW) \
  ROW(13, PilotoMarcha, false)

#define CFSM_TABLE_AI(ROW)

#include <io/IOTable.h>
#endif
```

Después, una sola unidad de compilación debe expandir
`CFSM_DEFINE_HARDWARE`. Véase [IOTable.md](io/IOTable.md).

## 3. Inclusión selectiva

En proyectos ajustados de flash puede incluirse solo lo necesario:

```cpp
#include <core/SequenceBlock.h>
#include <core/BlockManager.h>
#include <io/DigitalSensor.h>
```

Esto reduce el trabajo del compilador y puede permitir que el enlazador descarte
más código. No cambia el modelo de ejecución ni la API de las clases incluidas.

## 4. Ciclo de scan mínimo

```cpp
void loop() {
  HW.readInputs();        // PAE: capturar entradas
  manager.updateAll();    // OB1: ejecutar lógica
  HW.writeOutputs();      // PAA: aplicar salidas
}
```

`CoreFSM.h` no impone estas llamadas: el programa es responsable del orden y de
no introducir esperas bloqueantes entre ellas.

## 5. Inicialización

Una secuencia de arranque típica es:

```cpp
void setup() {
  Serial.begin(115200);
  HW.begin();
  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();
  proceso.start();
}
```

Registra todos los objetos antes de `beginAll()` y configura el hardware antes
de iniciar la lógica que lo consume.

## 6. Lo que no proporciona

- No hay planificador con hilos ni tareas de distinta frecuencia.
- No existe asignación de hardware en tiempo de ejecución.
- Incluir la cabecera no crea una instancia `HW`.
- `PacketLink.h` y `RemoteIO.h` sí forman parte de la cabecera agregadora. La
  aplicación aún debe crear el `Stream`, dar identidad a cada nodo, ejecutar
  las fases de red y definir la política de fallo.
- Los mecanismos de parada son protecciones de software; consulta
  [SAFETY.md](../../../SAFETY.md).

## 7. Mapa de referencia

- [Núcleo y máquina de estados](core/BlockBase.md)
- [E/S e imagen de proceso](io/IDevice.md)
- [Lógica IEC 61131-3](logic/Timers.md)
- [Accionamientos](drive/MotorDrive.md)
- [Datos persistentes y recetas](data/ConfigStore.md)
- [Diagnóstico](diag/Telemetry.md)
- [Visión serie](comms/VisionSensor.md)
- [Transporte de paquetes](comms/PacketLink.md)
- [Imagen digital remota](comms/RemoteIO.md)
- [Fuentes de asignación de hardware](hardware/sources.md)
