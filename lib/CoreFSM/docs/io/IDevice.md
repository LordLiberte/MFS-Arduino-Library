# IDevice.h

> El contrato de todo objeto de campo. Ochenta y cuatro líneas que separan el hardware de la lógica y hacen posible el ciclo de scan de tres fases.

**Ruta:** `src/io/IDevice.h`
**Incluye:** `../core/CoreFSM_Platform.h`
**Lo usan:** `DigitalSensor.h`, `DigitalOutput.h`, `AnalogSensor.h`, `UltrasonicSensor.h`, `DeviceManager.h`, `VisionSensor.h`.

---

## 1. Qué problema resuelve

La librería distingue con claridad **dos familias de objetos**, y el reparto es
el de la norma ISA-88:

| | Qué es | Qué sabe | Qué **no** sabe |
|---|---|---|---|
| **`IDevice`** | Módulo de Control (CM) | Pines, tensiones, rebotes | Nada del proceso |
| **`BlockBase`** | Módulo de Equipo (EM) | El proceso | Nada de pines |

Que sean **interfaces distintas** no es organización cosmética: es lo que
permite que el ciclo de scan tenga las tres fases separadas de un autómata. Si
fueran la misma jerarquía, cada sensor tendría un `update()` vacío y cada
estación un `readInputs()` vacío, y no habría forma de decirle al orquestador
"lee ahora todas las entradas y solo las entradas".

## 2. Cómo funciona por dentro

### 2.1 Las tres fases

```
readInputs()    PAE  — todos los sensores se leen a la vez, al principio
update()        OB1  — la lógica calcula con esa foto congelada
writeOutputs()  PAA  — todas las salidas se escriben a la vez, al final
```

`IDevice` aporta la primera y la tercera. `BlockBase` aporta la segunda.

**La coherencia es lo importante.** Como todas las entradas se leen en el mismo
instante, la lógica trabaja con una foto congelada de la planta. Si en cambio
hicieras `digitalRead()` en medio del código de proceso, un sensor podría cambiar
a mitad del razonamiento y llegarías a conclusiones imposibles: ver el cilindro
en reposo al principio de la función y en trabajo al final, y activar dos salidas
incompatibles. Los PLC hacen exactamente esto, y por el mismo motivo.

Hay además una ganancia de rendimiento que sorprende: `digitalRead` y
`digitalWrite` de Arduino son **lentos**. Hacen búsquedas en tablas de puertos y
deshabilitan interrupciones. Agruparlos en dos ráfagas ordenadas es más rápido y
mucho más predecible que salpicarlos por todo el programa.

### 2.2 Los métodos

```cpp
virtual void begin() = 0;        // pinMode, buses, valores iniciales
virtual void readInputs()  {}    // PAE. Vacío por defecto
virtual void writeOutputs() {}   // PAA. Vacío por defecto
virtual void enterSafeState() {} // orden software de estado seguro
```

Solo `begin()` es virtual pura. Los otros tres métodos tienen cuerpo vacío para
que un dispositivo de solo salida no tenga que escribir un
`readInputs()` que no hace nada, y viceversa. Un `DigitalSensor` implementa la
primera; un `DigitalOutput`, la segunda; un `UltrasonicSensor`, solo la primera
aunque tenga un pin de disparo.

`enterSafeState()` lo invoca el interbloqueo software de `DeviceManager`. Todo
dispositivo que pueda energizar hardware debe sobrescribirlo, retirar órdenes y
aplicar inmediatamente su valor seguro. El cuerpo vacío mantiene compatibles
los dispositivos de entrada, pero también significa que una clase de salida
propia queda sin protección hasta que implemente este método.

### 2.3 El forzado

```cpp
bool isForced() const { return _forced; }
void releaseForce()   { _forced = false; }
```

Reproduce la función de **forzado** de un PLC: desconectar una señal del mundo
físico y darle un valor a mano. Es la herramienta que más se usa en una puesta en
marcha: permite probar la secuencia entera antes de que el armario esté cableado,
o seguir produciendo mientras se cambia un sensor averiado.

Cada dispositivo concreto añade su propio `force(valor)` con el tipo que le
corresponda; lo que vive aquí es la bandera y la liberación, que es lo común.

**Y es peligrosa, igual que en un PLC: una señal forzada miente.** Por eso existe
`isForced()`, por eso `DeviceManager` tiene `hasAnyForce()` y `releaseAllForces()`,
y por eso `printIoTable()` marca en mayúsculas si hay algo forzado. Nunca dejes un
forzado puesto al terminar; el clásico es el que alguien se dejó el viernes por la
tarde y aparece el lunes como una avería fantasma.

## 3. API completa

| Método | Firma | Quién lo implementa |
|---|---|---|
| `begin()` | `virtual void begin() = 0` | **Obligatorio** en todos |
| `readInputs()` | `virtual void readInputs() {}` | Los que leen algo |
| `writeOutputs()` | `virtual void writeOutputs() {}` | Los que escriben algo |
| `enterSafeState()` | `virtual void enterSafeState() {}` | Toda salida energizable |
| `setName(F("..."))` / `getName()` | | Heredado, para diagnóstico |
| `isForced()` / `releaseForce()` | | Heredado |

Y `protected`: `_name` y `_forced`, que las clases hijas manipulan.

## 4. Ejemplos

### 4.1 Un dispositivo propio: un relé con contador de maniobras

```cpp
class ReleContado : public IDevice {
  uint8_t  _pin;
  bool     _estado = false, _anterior = false;
  uint32_t _maniobras = 0;

 public:
  explicit ReleContado(uint8_t pin) : _pin(pin) {}

  void begin() override { pinMode(_pin, OUTPUT); digitalWrite(_pin, LOW); }

  /* Solo salida: no implementa readInputs(). */
  void writeOutputs() override {
    if (_estado != _anterior) { _maniobras++; _anterior = _estado; }
    digitalWrite(_pin, _estado ? HIGH : LOW);
  }

  void enterSafeState() override {
    _forced = false;
    _estado = false;
    digitalWrite(_pin, LOW);
  }

  void set(bool v)   { if (!_forced) _estado = v; }
  void force(bool v) { _forced = true; _estado = v; }
  uint32_t maniobras() const { return _maniobras; }
};
```

Fíjate en dos cosas. Primero, `set()` respeta el forzado: mientras la señal esté
forzada, la lógica no puede cambiarla — eso es lo que significa forzar. Segundo,
el contador se incrementa en `writeOutputs()`, no en `set()`: así cuenta
maniobras físicas reales aunque la lógica escriba el mismo valor cien veces por
segundo.

Un relé industrial aguanta del orden de cien mil maniobras mecánicas. Saber
cuántas lleva es mantenimiento predictivo por dos euros de código.

### 4.2 Un dispositivo de entrada por bus

```cpp
class TermoparI2C : public IDevice {
  uint8_t _dir;
  int16_t _tempDecimas = 0;

 public:
  explicit TermoparI2C(uint8_t dir) : _dir(dir) {}
  void begin() override { Wire.begin(); }

  /* Solo entrada: no implementa writeOutputs(). */
  void readInputs() override {
    if (_forced) return;                  // forzado: no se lee el bus
    Wire.requestFrom(_dir, (uint8_t)2);
    if (Wire.available() >= 2) {
      _tempDecimas = (int16_t)((Wire.read() << 8) | Wire.read());
    }
  }

  int16_t temperatura() const { return _tempDecimas; }
  void force(int16_t t) { _forced = true; _tempDecimas = t; }
};
```

Al estar dentro de `readInputs()`, la transacción I2C ocurre siempre en la fase
PAE, nunca en medio de la lógica. Si el bus se cuelga, se nota en el tiempo de
scan y `ScanWatchdog` lo caza.

### 4.3 Poner en marcha una secuencia sin el armario cableado

```cpp
void setup() {
  ...
  HW.FC_Carro_Trabajo.force(false);   // el final de carrera aún no está
  HW.FC_Carro_Reposo.force(true);     // "el carro está en reposo"
}

// y desde la consola, cuando se quiera avanzar la secuencia a mano:
if (c == 't') HW.FC_Carro_Trabajo.force(true);
if (c == 'l') HW.releaseAllForces();
```

Con eso se prueba la secuencia entera en la mesa. Y `printIoTable()` recuerda en
cada volcado que hay señales mintiendo.

## 5. Decisiones de diseño

**Dos jerarquías separadas y no una con todo.** Ver §1. El coste es que un
objeto que fuera las dos cosas —un accionamiento inteligente, por ejemplo—
tendría que heredar de las dos o componerse. Se prefirió eso a que las 30 clases
restantes cargasen con métodos vacíos.

**`readInputs()` y `writeOutputs()` no son puras.** Obligar a implementar las dos
habría llenado la librería de cuerpos vacíos y habría invitado a poner en
`readInputs()` cosas que no son lectura.

**El forzado vive en la interfaz base, pero `force(valor)` no.** El valor es de
un tipo distinto en cada dispositivo (`bool`, `uint16_t`, `int16_t`), y hacerlo
virtual con un tipo común habría obligado a conversiones feas. La bandera es
común; el valor, no.

**No hay `IDevice::update()`.** A propósito. Si un dispositivo necesita lógica
propia entre fases, o la mete en `readInputs()` (si es cálculo sobre lo leído) o
en `writeOutputs()` (si es cálculo sobre lo que va a escribir, como el parpadeo
de `DigitalOutput`). Añadir una fase intermedia habría difuminado justo la
separación que este archivo existe para mantener.

## 6. Errores frecuentes

**Llamar a `digitalRead()` desde la lógica de un bloque.** Rompe la coherencia
de la foto y hace que dos consultas al mismo sensor en el mismo scan puedan dar
resultados distintos. Todo lo físico pasa por la fase PAE.

**Que `set()` no respete `_forced`.** Si tu dispositivo propio ignora la bandera,
el forzado no fuerza nada y encima nadie se entera, porque `isForced()` sigue
diciendo `true`. Es un forzado que miente sobre estar mintiendo.

**Meter un `delay()` o una espera en `readInputs()`.** Es la fase donde más
tienta —esperar un eco, esperar un ADC— y donde peor sienta: alarga el scan
entero. `UltrasonicSensor` es el aviso vivo de esto.

**Olvidar `begin()`.** Es virtual pura: si falta, no compila. Igual que en
`BlockBase`.

**Crear una salida y dejar el `enterSafeState()` heredado.** Compila, pero el
interbloqueo global no cambia ese hardware. Prueba cada clase propia con el
actuador desconectado y documenta su valor seguro.

**Dejar un forzado puesto.** Ver §2.3. Ponte `releaseAllForces()` en el arranque.

## 7. Coste

Por instancia, en AVR:

| Miembro | Bytes |
|---|---:|
| `_name` (puntero a flash) | 2 |
| `_forced` | 1 |
| puntero a vtable | 2 |

**5 bytes**, más lo que añada el dispositivo concreto.

## 8. Relación con el resto

```
                      IDevice.h
                          ▲
      ┌───────────┬───────┴───────┬─────────────┬──────────────┐
 DigitalSensor DigitalOutput  AnalogSensor  Ultrasonic    VisionSensor
      │           │               │             │              │
      └───────────┴───────┬───────┴─────────────┴──────────────┘
                          ▼
                  DeviceManager<N>
                  readAllInputs()  →  fase PAE
                  writeAllOutputs() →  fase PAA
                          ▲
                          │ lo genera IOTable.h a partir de tu tabla
                          │
                    struct CfsmHardware  ──▶  HW

  Y en paralelo, sin tocarse:
                  BlockBase → FsmBlock → SequenceBlock  →  fase OB1
```
