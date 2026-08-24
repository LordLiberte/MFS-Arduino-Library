# Expansores de E/S

> CoreFSM incluye una ruta completa para DI/DO agrupadas y un backend concreto
> para MCP23017. No es necesario añadir otro microcontrolador solo por falta de
> GPIO.

## Qué está disponible

| Pieza | Estado |
|---|---|
| `IDigitalBackend` y `DigitalPin` | incluidos en `src/io/DigitalBackend.h` |
| Constructores de backend en `DigitalSensor`/`DigitalOutput` | disponibles |
| Registro y fases agrupadas en `DeviceManager` | disponibles |
| Macros de backend en `IOTable` | disponibles |
| `Mcp23017Backend` | incluido en `src/io/Mcp23017Backend.h` |
| Generación desde CSV/JSON | disponible para MCP23017 |
| PCF8574, 74HC165, 74HC595 | sin backend incluido |

Referencias detalladas: [`DigitalBackend`](../io/DigitalBackend.md) y
[`Mcp23017Backend`](../io/Mcp23017Backend.md).

## Ciclo agrupado

```text
PAE  backend.sampleInputs()   captura una imagen por chip
     sensores.readInputs()   leen bits de RAM
OB1  lógica
PAA  salidas.writeOutputs()  actualizan la sombra
     backend.commitOutputs() escribe solo si cambió
```

`DeviceManager<MAX_DEVICES, MAX_BACKENDS>` ejecuta ese orden. Los dispositivos
declaran sus canales antes de `backend.begin()`, de modo que el MCP23017 aplica
direcciones y pull-ups por bancos.

## Configuración manual con `IOTable`

```cpp
#include <CoreFSM.h>
#include <io/Mcp23017Backend.h>

#define CFSM_TABLE_BACKEND(ROW) \
  ROW(Mcp23017Backend, EXP1, Wire, 0x20)

#define CFSM_TABLE_DI_BACKEND(ROW) \
  ROW(EXP1, 0, Puerta_Cerrada, true, 10)

#define CFSM_TABLE_DO_BACKEND(ROW) \
  ROW(EXP1, 8, Valvula, false, false)

#define CFSM_TABLE_DI(ROW)
#define CFSM_TABLE_DO(ROW)
#define CFSM_TABLE_AI(ROW)

#include <io/IOTable.h>
```

Las firmas son:

| Macro | Fila |
|---|---|
| `CFSM_TABLE_BACKEND` | `ROW(tipo, nombre, argumentos...)` |
| `CFSM_TABLE_DI_BACKEND` | `ROW(backend, canal, nombre, pullup, debounce)` |
| `CFSM_TABLE_DO_BACKEND` | `ROW(backend, canal, nombre, activeLow, safeValue)` |
| `CFSM_TABLE_DO_SAFE` | `ROW(pin, nombre, activeLow, safeValue)` |

`HW.allBackendsHealthy()` resume la salud de los backends registrados y
`printIoTable()` muestra una advertencia si alguno falla.

El mismo resultado puede generarse desde destinos `EXP1.3`; consulta
[fuentes de hardware](sources.md).

## MCP23017: alcance real

- 16 canales por dirección, ocho direcciones posibles por bus.
- `INPUT`, `INPUT_PULLUP` y `OUTPUT`.
- Captura GPIOA/GPIOB por scan y escritura OLATA/OLATB solo ante cambios.
- Contador acumulado de errores y última salud conocida.
- Sin interrupciones, PWM, ADC ni lectura de OLAT como realimentación física.

Con `manageWire=false`, la aplicación es responsable de inicializar `TwoWire`.
Esto es necesario cuando se usan pines I2C no predeterminados o una
configuración compartida.

## Fallo y estado seguro

`healthy()==false` es diagnóstico, no un corte automático. Si I2C falla, el
software conserva la última imagen válida y el expansor puede mantener una
salida física anterior. La aplicación puede activar
`HW.setSafetyInterlock(true)`, que ordena `enterSafeState()` y trata de hacer un
volcado inmediato, pero un bus ya roto puede impedirlo.

Por eso el estado seguro debe existir también en la electrónica: resistencias,
drivers, contactores, alimentación y watchdog apropiados. No transportes una
parada de emergencia por un MCP23017 ni uses sus canales para step/dir,
encoders rápidos o PWM preciso. Consulta [SAFETY.md](../../../../SAFETY.md).

## Cuándo usar otro nodo

Un expansor es apropiado para E/S lentas en el mismo equipo. Usa otro
microcontrolador cuando necesites distancia, aislamiento, CPU o periféricos
independientes; entonces aparecen timeout, identidad y reinicios. Consulta
[multi-controller.md](../net/multi-controller.md).

