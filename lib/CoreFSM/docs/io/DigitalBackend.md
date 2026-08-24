# DigitalBackend.h

> Abstracción mínima para que una entrada o salida digital use indistintamente
> un GPIO nativo o un canal de una imagen de E/S agrupada.

**Ruta:** `src/io/DigitalBackend.h`  
**Tipos:** `IDigitalBackend`, `DigitalPin`.

## `IDigitalBackend`

La interfaz separa el acceso lógico a un canal de la transacción física del
bus:

```cpp
class IDigitalBackend {
 public:
  virtual void configure(uint8_t channel, uint8_t mode) = 0;
  virtual bool read(uint8_t channel) const = 0;
  virtual void write(uint8_t channel, bool level) = 0;
  virtual void begin() {}
  virtual void sampleInputs() {}
  virtual void commitOutputs() {}
  virtual bool healthy() const { return true; }
};
```

`read()` y `write()` operan sobre imágenes en RAM. El trabajo de bus se agrupa
en `sampleInputs()` al comenzar la PAE y `commitOutputs()` al terminar la PAA.
[`DeviceManager`](DeviceManager.md) respeta ese orden para todos los backends
registrados.

`healthy()` informa del backend completo, no de la calidad de un canal. La
interfaz no prescribe timeout, edad de muestra ni política de recuperación: la
implementación concreta debe documentarlos.

## `DigitalPin`

```cpp
DigitalPin(uint8_t nativePin);
DigitalPin(IDigitalBackend& provider, uint8_t channel);
```

`DigitalPin` contiene un puntero al backend y un número de canal. Con puntero
nulo llama directamente a `pinMode`, `digitalRead` y `digitalWrite`; con backend
delega en sus métodos. `isNative()` permite distinguir ambas rutas.

`DigitalSensor` y `DigitalOutput` ofrecen constructores equivalentes:

```cpp
DigitalSensor entrada(expansor, 3, true, 10);
DigitalOutput salida(expansor, 8, false, false);
```

La referencia no toma propiedad del backend. Este debe existir durante toda la
vida de los dispositivos que lo usan y debe registrarse antes de
`DeviceManager::beginAll()`.

## Orden de un scan

```text
backend.sampleInputs()
dispositivos.readInputs()
lógica
dispositivos.writeOutputs()
backend.commitOutputs()
```

No llames al bus desde `read()` o `write()` en una implementación agrupada: se
perderían la coherencia de la imagen y el límite de transacciones por scan.

## Límites

- Solo modela señales digitales; no PWM, ADC, interrupciones ni contadores.
- No valida de forma general el rango de canal.
- No sincroniza accesos desde ISR o tareas concurrentes.
- Un fallo de `healthy()` no activa por sí solo el interbloqueo de la máquina.
- Es una abstracción de control general, no una interfaz de seguridad.

Implementación incluida: [`Mcp23017Backend`](Mcp23017Backend.md). Véase también
la [guía de expansores](../hardware/io-expanders.md) y
[SAFETY.md](../../../../SAFETY.md).

