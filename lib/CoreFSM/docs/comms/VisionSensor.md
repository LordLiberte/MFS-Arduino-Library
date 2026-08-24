# VisionSensor.h

> Adaptador no bloqueante para recibir resultados ya procesados de una cámara
> a través de un objeto Arduino `Stream`.

**Ruta:** `src/comms/VisionSensor.h`  
**Incluye:** `../io/IDevice.h`  
**Tipos públicos:** `VisionResult`, `VisionSensor`, `VisualServo`.

## 1. Alcance

La cámara o coprocesador realiza la visión y envía clase, error respecto al
centro, tamaño aparente, confianza y flags. `VisionSensor` no procesa imágenes,
no abre dispositivos y no configura la velocidad del puerto: el programa debe
inicializar el `HardwareSerial`, `SoftwareSerial` u otro `Stream` usado.

Es un `IDevice` de entrada y su `readInputs()` debe ejecutarse durante la PAE.

## 2. Trama recibida

La trama ocupa exactamente ocho bytes:

| Byte | Campo | Interpretación |
|---:|---|---|
| 0 | sincronismo | `0xAA` |
| 1 | `classId` | `0` significa sin clase detectada |
| 2 | `offsetX` | entero con signo, normalmente −100…100 |
| 3 | `offsetY` | entero con signo |
| 4 | `width` | tamaño aparente del objetivo |
| 5 | `confidence` | confianza 0…100 |
| 6 | `flags` | bit 0 OK, bit 1 busy, bit 2 error |
| 7 | checksum | XOR de los bytes 1…6 |

Una trama con XOR incorrecto se descarta. Una trama válida actualiza
`_lastRx`, incluso si `classId` vale cero o la confianza no alcanza el mínimo.

Este formato es un protocolo local sencillo: no lleva versión, dirección,
número de secuencia ni CRC fuerte. No debe reutilizarse como protocolo general
entre nodos sin una envoltura adicional.

## 3. Detección y timeout

```cpp
VisionSensor camara(puerto, 500); // timeout de 500 ms
camara.setMinConfidence(70);
```

`hasTarget()` solo es verdadero cuando:

1. llegó una trama válida antes del timeout;
2. `classId != 0`;
3. `confidence >= minConfidence`;
4. el flag `error` no está activo.

Al vencer el timeout, la implementación actual reinicia todo `VisionResult` y
pone `commsOk=false`; no deja coordenadas ni flags antiguos visibles como datos
vigentes. Aun así, valida `commsOk()` o `hasTarget()` antes de actuar.

## 4. API de `VisionSensor`

| Miembro | Significado |
|---|---|
| `VisionSensor(Stream&, timeoutMs=500)` | Asocia puerto y vigilancia |
| `begin()` | Limpia parser, resultado y validez de comunicación |
| `readInputs()` | Consume bytes disponibles y aplica timeout |
| `hasTarget()` | Objetivo válido y comunicación vigente |
| `commsOk()` | Ha llegado una trama válida dentro del plazo |
| `classId()` | Clase del último resultado |
| `errorX()` / `errorY()` | Desviaciones con signo |
| `targetWidth()` | Tamaño aparente |
| `confidence()` | Confianza recibida |
| `isPieceOk()` / `isBusy()` | Flags del último resultado |
| `isCentered(tolerance=8)` | Objetivo vigente dentro de banda muerta |
| `setMinConfidence(c)` | Umbral usado en decodificaciones posteriores |
| `setMaxBytesPerScan(n)` | Presupuesto RX; cero se normaliza a uno |
| `sendCommand(cmd,arg=0)` | Envía `0x55,cmd,arg,cmd^arg` inmediatamente |
| `raw()` | Referencia constante al `VisionResult` completo |
| `describe(Print&)` | Resumen legible para diagnóstico |

`sendCommand()` escribe directamente sobre el `Stream`; no se difiere hasta la
PAA y puede consumir tiempo según la implementación del puerto.

## 5. Ejemplo mínimo

```cpp
#include <CoreFSM.h>
#include <SoftwareSerial.h>

SoftwareSerial puertoCamara(8, 9); // RX, TX
VisionSensor camara(puertoCamara, 400);
DeviceManager<1> io;

void setup() {
  puertoCamara.begin(115200);
  io.registerDevice(&camara, F("CAMARA"));
  io.beginAll();
}

void loop() {
  io.readAllInputs();

  if (!camara.commsOk()) {
    // Estado seguro: no reutilizar la última consigna.
  } else if (camara.hasTarget()) {
    int8_t correccion = camara.errorX();
    (void)correccion;
  }
}
```

## 6. `VisualServo`

`VisualServo` es un controlador proporcional sin estado dinámico. Calcula:

```text
w = offsetX × kpAngular / 10
v = baseSpeed + (targetWidth − width) × kpDistance / 10
```

Las salidas `v` y `w` se saturan a −255…255. Dentro de `deadBand`, `w` vale
cero. Si no hay objetivo válido, `update()` devuelve `false` y pone ambas
salidas a cero.

```cpp
VisualServo seguimiento;
seguimiento.targetWidth = 45;

if (seguimiento.update(camara)) {
  chasis.drive(seguimiento.v, seguimiento.w);
} else {
  chasis.stop();
}
```

## 7. Límites actuales

- `readInputs()` procesa como máximo 32 bytes por scan por defecto; ajusta el
  límite con `setMaxBytesPerScan()` según baudrate y tiempo de ciclo.
- El XOR detecta algunos errores, pero es más débil que un CRC.
- No hay contador de tramas descartadas, edad pública del dato ni detección de
  duplicados.
- El parser maneja una única trama fija y una única cámara por `Stream`.
- El protocolo no es una función de seguridad ni un transporte multi-nodo.

## 8. Relación con el resto

`VisionSensor` participa en la PAE como cualquier [`IDevice`](../io/IDevice.md).
`VisualServo` puede entregar consignas a los chasis descritos en
[Chassis.md](../drive/Chassis.md). El ejemplo completo está en
`examples/07_Vision_Seguimiento/`.
