# Asignación manual de pines

> CoreFSM puede usarse sin Wokwi ni ningún generador. La fuente de verdad puede
> ser un `HardwareConfig.h` versionado junto al proyecto.

## 1. Tabla declarativa manual

La misma interfaz `HW.Nombre` usada por el generador se obtiene escribiendo las
X-macros a mano:

```cpp
// include/HardwareConfig.h
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

//        pin  nombre                 pull-up  debounce ms
#define CFSM_TABLE_DI(ROW) \
  ROW(    2,  Pulsador_Marcha,       true,    20 ) \
  ROW(    3,  Final_Carrera,         true,     5 )

//        pin  nombre                 active-low
#define CFSM_TABLE_DO(ROW) \
  ROW(   13,  Piloto_Trabajo,        false ) \
  ROW(   12,  Rele_Bomba,            true  )

//        pin  nombre                 filtro 0..8
#define CFSM_TABLE_AI(ROW) \
  ROW(   A0,  Presion,                3 )

#include <io/IOTable.h>
#endif
```

En una sola unidad de compilación:

```cpp
#include "HardwareConfig.h"

CFSM_DEFINE_HARDWARE;

void setup() {
  HW.begin();
}

void loop() {
  HW.readInputs();

  bool marcha = HW.Pulsador_Marcha.hasRisen();
  HW.Piloto_Trabajo.set(marcha);

  HW.writeOutputs();
}
```

No incluyas `io/IOTable.h` desde ningún otro archivo y no expandas
`CFSM_DEFINE_HARDWARE` más de una vez.

## 2. Tablas vacías

Solo es obligatorio definir las familias usadas. `IOTable.h` crea vacías las
ausentes. Si se prefiere que el contrato quede explícito:

```cpp
#define CFSM_TABLE_DI(ROW)
#define CFSM_TABLE_DO(ROW)
#define CFSM_TABLE_AI(ROW)
```

La implementación actual de `IOTable.h` cubre entradas digitales, salidas
digitales y entradas analógicas. Ultrasonidos, motores, PWM especializado y
otros dispositivos se declaran por separado.

## 3. Objetos declarados directamente

Para proyectos pequeños puede omitirse la tabla:

```cpp
DigitalSensor pulsador(2, true, 20);
DigitalOutput piloto(13);
DeviceManager<2> io;

void setup() {
  io.registerDevice(&pulsador, F("PULSADOR"));
  io.registerDevice(&piloto, F("PILOTO"));
  io.beginAll();
}

void loop() {
  io.readAllInputs();
  piloto.set(pulsador.isTriggered());
  io.writeAllOutputs();
}
```

Esta es la vía que utilizan la mayoría de los ejemplos de la librería. También
es la adecuada para tipos que `IOTable.h` no genera.

## 4. Imagen ligera con `IOManager`

`IOManager` enlaza GPIO directamente con variables `bool` existentes:

```cpp
bool ordenMarcha = false;
bool motor = false;
IOManager<1, 1> ioLigera;

void setup() {
  ioLigera.mapInput(2, &ordenMarcha, true);
  ioLigera.mapOutput(13, &motor, false);
  ioLigera.begin();
}
```

Consume menos RAM, pero no proporciona por sí mismo antirrebote, flancos,
forzado, watchdog de salida ni diagnóstico de objeto. Consulta
[IOManager.md](../io/IOManager.md).

## 5. Integración con PlatformIO

Para un proyecto manual, elimina el `extra_scripts` del generador. PlatformIO
solo necesita localizar la librería:

```ini
[env:nano]
platform = atmelavr
board = nanoatmega328
framework = arduino
lib_extra_dirs = ../../lib
```

Mantén `include/HardwareConfig.h` bajo control de versiones. El CI podrá
compilar el proyecto sin Python, conexión de red ni herramienta gráfica.

## 6. Reglas de revisión

- Un pin físico no debe aparecer en dos señales incompatibles.
- Usa `activeLow=true` solo para salidas cuyo módulo se energiza con nivel bajo.
- En entradas cableadas a masa, combina `activeLow=true` con pull-up.
- No uses una entrada analógica como digital sin comprobar la placa concreta.
- Documenta el estado seguro de cada actuador fuera de la tabla de nombres.
- Tras mover un cable, cambia primero la fuente de verdad y recompila todos los
  proyectos que compartan el hardware.

## 7. Cuándo usar otra fuente

La tabla manual es apropiada cuando el proyecto es pequeño o el esquema no puede
exportarse. Para importadores y manifiestos generados consulta [sources.md](sources.md).
Para ampliar E/S sin añadir otro microcontrolador consulta
[io-expanders.md](io-expanders.md).
