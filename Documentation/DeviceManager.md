# DeviceManager.h

> Setenta y cuatro líneas que convierten un `loop()` lleno de `digitalRead` en el ciclo de tres líneas de un autómata.

**Ruta:** `src/io/DeviceManager.h`
**Incluye:** `IDevice.h`
**Lo usan:** `IOTable.h` (lo mete dentro de `CfsmHardware`), y tu `.ino` si montas la E/S a mano.

---

## 1. Qué problema resuelve

Es el gemelo de [`BlockManager`](../core/BlockManager.md), pero para la E/S en
vez de para la lógica. Guarda todos los objetos de campo y ejecuta sus fases de
lectura y escritura en bloque:

```cpp
void loop() {
  HW.readInputs();      // PAE
  manager.updateAll();  // OB1
  HW.writeOutputs();    // PAA
}
```

Sin él, esas dos líneas serían dos listas de llamadas que crecen con cada sensor
que añades, y que hay que mantener sincronizadas con la lista de declaraciones.
La que se olvida siempre es la de escritura, y el síntoma es una salida que
nunca se enciende sin que nada dé error.

## 2. Cómo funciona por dentro

### 2.1 Array estático dimensionado en la plantilla

```cpp
template <uint8_t MAX_DEVICES = 16>
class DeviceManager {
  ...
  IDevice* _devices[MAX_DEVICES];
  uint8_t  _count;
};
```

Cero memoria dinámica, consumo conocido de antemano. Mismo criterio que
`BlockManager` y por la misma razón: una máquina encendida meses no se puede
permitir la fragmentación del montón.

Cuando lo genera `IOTable.h`, el tamaño no lo eliges tú: sale de contar las filas
de la tabla en tiempo de compilación, así que es **exacto**. Ni un puntero de
más. Ver [IOTable.md](IOTable.md) §2.3.

### 2.2 Registro

```cpp
bool registerDevice(IDevice* dev);
bool registerDevice(IDevice* dev, const __FlashStringHelper* name);
```

Devuelve `false` si el array está lleno o el puntero es nulo. A diferencia de
`BlockManager`, aquí **no** se asigna un id: los dispositivos se identifican por
nombre, y el nombre se lo pone la segunda sobrecarga.

El orden de registro es el orden de lectura y de escritura. Rara vez importa,
pero si tienes un multiplexor y los sensores que cuelgan de él, el multiplexor va
primero.

### 2.3 Las dos fases

```cpp
void readAllInputs()  { for (i...) _devices[i]->readInputs();  }
void writeAllOutputs(){ for (i...) _devices[i]->writeOutputs(); }
```

Dos bucles de llamada virtual, y ya está. Toda la sofisticación —antirrebote,
flancos, parpadeo, filtro, forzado— está dentro de cada dispositivo. Este archivo
solo garantiza que **todos** se ejecutan y que se ejecutan **agrupados**.

Fíjate en que no hay comprobación de `isEnabled()` como en `BlockManager`. Es
deliberado: un sensor "deshabilitado" no tiene sentido — o está o no está. Para
desconectar una señal del mundo físico existe el forzado.

### 2.4 La red de seguridad contra el forzado olvidado

```cpp
void releaseAllForces() { for (i...) _devices[i]->releaseForce(); }
bool hasAnyForce() const { for (i...) if (_devices[i]->isForced()) return true; return false; }
```

`releaseAllForces()` en el arranque y asociado a un comando de mantenimiento es
lo que evita la avería fantasma del lunes por la mañana. `hasAnyForce()` es lo
que hace que `printIoTable()` pueda avisar en mayúsculas.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| `registerDevice` | `bool registerDevice(IDevice*)` | Registra. `false` si está lleno |
| `registerDevice` | `bool registerDevice(IDevice*, const __FlashStringHelper*)` | Registra y nombra |
| `count()` / `at(i)` | | Cuántos hay / el i-ésimo |
| `beginAll()` | | `begin()` de todos. En el `setup()` |
| `readAllInputs()` | | **Fase PAE** |
| `writeAllOutputs()` | | **Fase PAA** |
| `releaseAllForces()` | | Quita todos los forzados |
| `hasAnyForce()` | | ¿Hay alguno forzado? |

## 4. Ejemplos

### 4.1 E/S a mano, sin tabla

Es lo que hacen los ejemplos 01 y 02, y va bien cuando son pocas señales:

```cpp
DigitalSensor btnMarcha(2, true, 25);
DigitalSensor fcTrabajo(3, true, 10);
DigitalOutput ledMotor(12);
DigitalOutput ledSoldador(13);

DeviceManager<4> io;

void setup() {
  io.registerDevice(&btnMarcha,   F("MARCHA"));
  io.registerDevice(&fcTrabajo,   F("FC_TRABAJO"));
  io.registerDevice(&ledMotor,    F("MOTOR"));
  io.registerDevice(&ledSoldador, F("SOLDADOR"));
  io.beginAll();
  io.releaseAllForces();          // por si acaso
}

void loop() {
  io.readAllInputs();
  estacion.pulsadorMarcha = btnMarcha.hasRisen();
  manager.updateAll();
  ledMotor.set(estacion.motorMarcha);
  io.writeAllOutputs();
}
```

Cuatro señales, cuatro declaraciones, cuatro registros. A partir de diez o doce,
esa duplicación empieza a doler y es cuando compensa la tabla de
[IOTable.h](IOTable.md).

### 4.2 Avisar de que hay señales forzadas

```cpp
void loop() {
  ...
  /* Un parpadeo lento en el piloto de "modo prueba" mientras haya forzados.
   * Que se vea desde fuera de la máquina que algo está mintiendo. */
  ledPrueba.setMode(io.hasAnyForce() ? OUT_BLINK_SLOW : OUT_OFF);
  io.writeAllOutputs();
}
```

### 4.3 Recorrer los dispositivos para un volcado propio

```cpp
void volcarIO(Print& out) {
  for (uint8_t i = 0; i < io.count(); i++) {
    IDevice* d = io.at(i);
    out.print(' ');
    if (d->getName()) out.print(d->getName()); else out.print(i);
    if (d->isForced()) out.print(F(" [FORZADO]"));
    out.println();
  }
}
```

`at()` devuelve `nullptr` si el índice se sale, así que el bucle es seguro
incluso si te equivocas en el límite.

## 5. Decisiones de diseño

**No hay `isEnabled()` por dispositivo.** Ver §2.3.

**No mide el tiempo de las fases.** `BlockManager` sí cronometra OB1, pero aquí
se prefirió no gastar los 20 bytes por manager: la fase PAE y la PAA quedan
cubiertas por la diferencia entre el máximo de
[`ScanWatchdog`](../diag/ScanWatchdog.md) y el de `BlockManager`. Ver
[BlockManager.md](../core/BlockManager.md) §2.4.

**El nombre se guarda en el `IDevice`, no en el manager.** Así un dispositivo
sabe cómo se llama aunque no esté registrado, y `describe()` funciona igual.

## 6. Errores frecuentes

**Registrar el dispositivo y olvidar `beginAll()`.** Los pines se quedan sin
configurar: las entradas flotan y las salidas no salen. No da error.

**Llamar a `beginAll()` antes de `Serial.begin()` si tu `begin()` imprime.** Se
pierde el mensaje. Poca cosa, pero desconcierta al depurar.

**Quedarse corto en `MAX_DEVICES`.** `registerDevice()` devuelve `false` y ese
sensor no se lee nunca. Comprueba el retorno, al menos en desarrollo.

**Escribir las salidas antes de `updateAll()`.** El orden PAE → OB1 → PAA no es
decorativo: al revés, las salidas van siempre un scan por detrás de la lógica.

## 7. Coste

Con `MAX_DEVICES = N`, en AVR: **2·N + 1 bytes**. Un `DeviceManager<16>` son 33
bytes. Cuando lo genera `IOTable.h` el tamaño es exactamente el número de filas
de tus tablas.

## 8. Relación con el resto

```
   IDevice ◀── DigitalSensor, DigitalOutput, AnalogSensor, Ultrasonic...
      ▲
      │ guarda IDevice* y llama por vtable
      │
   DeviceManager<N>
      ▲
      │ va dentro de
      │
   struct CfsmHardware   (lo genera IOTable.h)
      │
      └──▶ HW.readInputs() / HW.writeOutputs()
```
