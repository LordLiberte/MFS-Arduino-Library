# Mcp23017Backend.h

> Backend digital I2C de 16 canales para MCP23017, sin una biblioteca externa
> aparte de `Wire`.

**Ruta:** `src/io/Mcp23017Backend.h`  
**Hereda:** [`IDigitalBackend`](DigitalBackend.md).

`CoreFSM.h` no incluye esta cabecera porque introduce la dependencia de
`Wire.h`; inclúyela explícitamente o usa un `HardwareConfig.h` generado que lo
haga por ti.

## Construcción

```cpp
Mcp23017Backend(TwoWire& wire = Wire,
                uint8_t address = 0x20,
                bool manageWire = true);
```

- `address` debe corresponder al cableado A0..A2, normalmente `0x20`..`0x27`.
- Con `manageWire=true`, `begin()` llama a `wire.begin()`.
- Usa `false` cuando la aplicación ya inicializa ese bus, necesita pines I2C
  personalizados o comparte su configuración con otros dispositivos.

## Ejemplo manual

```cpp
#include <CoreFSM.h>
#include <io/Mcp23017Backend.h>

Mcp23017Backend exp1(Wire, 0x20);
DigitalSensor finalCarrera(exp1, 0, true, 10);
DigitalOutput valvula(exp1, 8, false, false);
DeviceManager<2, 1> io;

void setup() {
  io.registerDevice(&finalCarrera, F("FC"));
  io.registerDevice(&valvula, F("VALVULA"));
  io.registerBackend(&exp1);
  io.beginAll();
}
```

Los dispositivos se inicializan primero para acumular dirección y pull-up de
cada canal. Después el backend configura ambos bancos, toma una primera imagen
de entradas y deja preparada la imagen de salidas.

Al arrancar, el backend normaliza `IOCON` a `BANK=0` y activa el autoincremento.
Así también recupera un expansor que haya conservado otro mapa de registros
durante un reinicio exclusivo del microcontrolador. Después, antes de cambiar
`IODIR`, escribe la imagen prevista en `OLAT`. Si falla la normalización o esa
precarga, no cambia `IODIR`; cualquier fallo posterior deja el backend sin
salud y se reintenta en el scan siguiente. Incluso si falla la configuración de
pull-ups después de aplicar `IODIR`, el latch ya contiene el valor previsto.
Esto evita el pulso de arranque que produciría el latch `0` de fábrica en una
carga activa a nivel bajo. Llamar otra vez a `begin()` fuerza toda esta
secuencia y permite recuperar un expansor que se haya reiniciado por separado.

## API y comportamiento

| Método | Comportamiento |
|---|---|
| `configure(channel,mode)` | canales 0..15; admite `OUTPUT`, `INPUT` e `INPUT_PULLUP` |
| `read(channel)` | devuelve un bit de la última captura GPIOA/GPIOB |
| `write(channel,level)` | cambia la sombra OLAT y la marca como pendiente |
| `sampleInputs()` | aplica configuración pendiente y captura los 16 GPIO |
| `commitOutputs()` | aplica configuración y escribe OLAT solo si cambió |
| `healthy()` | resultado de la última operación agrupada |
| `errorCount()` | contador acumulado de canales inválidos y errores I2C |
| `inputImage()` / `outputImage()` | imágenes de 16 bits para diagnóstico |
| `address()` | dirección configurada |

Los pares de registros se transfieren en orden bajo/alto: banco A en los bits
0..7 y banco B en 8..15. Al normalizar el mapa se desactivan interrupciones
heredadas; el backend no ofrece ni utiliza interrupciones, inversión de
polaridad o comparación de entradas del MCP23017.

## Uso mediante `IOTable`

```cpp
#include <io/Mcp23017Backend.h>

#define CFSM_TABLE_BACKEND(ROW) \
  ROW(Mcp23017Backend, exp1, Wire, 0x20)
#define CFSM_TABLE_DI_BACKEND(ROW) \
  ROW(exp1, 0, Puerta_Cerrada, true, 10)
#define CFSM_TABLE_DO_BACKEND(ROW) \
  ROW(exp1, 8, Valvula, false, false)

#include <io/IOTable.h>
```

El [generador neutral](../hardware/sources.md) puede producir estas macros a
partir de destinos como `EXP1.3`.

## Fallos y seguridad

Un error deja `healthy()` a `false` y conserva la última imagen válida en RAM.
El chip puede conservar físicamente una salida anterior si el bus falla; la
clase no puede garantizar su retirada. Un fallo I2C transitorio puede
recuperarse, mientras `errorCount()` conserva el historial. Una dirección o un
canal inválidos son errores de configuración enclavados para no aparentar salud
en scans posteriores.

No uses este expansor para PWM, step/dir, encoders rápidos ni una parada de
emergencia. Diseña el estado sin alimentación y el fallo de bus en el circuito,
y consulta [SAFETY.md](../../../../SAFETY.md).
