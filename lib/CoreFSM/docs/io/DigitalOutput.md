# DigitalOutput.h

> Una salida digital con parpadeo no bloqueante, forzado, límite de tiempo
> activo y estado seguro. Al final del archivo, `AnalogOutput` añade PWM y rampa.

**Ruta:** `src/io/DigitalOutput.h`
**Incluye:** `IDevice.h`
**Lo usan:** `IOTable.h` (una fila `CFSM_TABLE_DO` genera un `DigitalOutput`), `TowerLight.h`, `MotorDrive.h`.

---

## 1. Qué problema resuelve

Un `digitalWrite()` crudo no sabe hacer tres cosas que una máquina real necesita:

1. **Parpadear sin bloquear.** Un piloto de aviso tiene que parpadear mientras
   el resto del programa sigue corriendo. Con `delay()` no se puede.
2. **Limitar la excitación mientras el scan funciona.** Una electroválvula
   excitada dos horas se quema. Si el firmware se bloquea por completo, este
   límite software tampoco se ejecuta: hace falta protección independiente.
3. **Dejarse forzar.** Igual que las entradas.

Y en el caso del PWM, hay una cuarta que no es evidente hasta que pasa: arrancar
un motor de golpe **reinicia el microcontrolador**.

## 2. Cómo funciona por dentro

### 2.1 Los cinco modos

```cpp
enum OutputMode : uint8_t {
  OUT_OFF        = 0,   /* apagada                           */
  OUT_ON         = 1,   /* fija                              */
  OUT_BLINK_SLOW = 2,   /* 500 ms / 500 ms  - aviso          */
  OUT_BLINK_FAST = 3,   /* 150 ms / 150 ms  - alarma         */
  OUT_FLASH      = 4    /* 80 ms on / 1200 ms off - destello */
};
```

Los tiempos no son arbitrarios. Lento para "atiende cuando puedas", rápido para
"atiende ahora", y el destello para "estoy vivo pero en reposo" — es el latido
que distingue una máquina apagada de una máquina esperando.

El orden numérico se usa: `if (_mode >= OUT_BLINK_SLOW)` en `describe()` detecta
"es intermitente" sin enumerar los tres.

### 2.2 El parpadeo, en la fase PAA

```cpp
void writeOutputs() override {
  bool desired;
  if (_safeLatched) {
    desired = _safeValue;
  } else if (_forced) {
    desired = _simValue;
  } else {
    switch (_mode) {
      case OUT_ON:         desired = true;                        break;
      case OUT_BLINK_SLOW: _blink.setPeriod(500, 500);
                           desired = _blink.update(true);         break;
      case OUT_BLINK_FAST: _blink.setPeriod(150, 150);
                           desired = _blink.update(true);         break;
      case OUT_FLASH:      _blink.setPeriod(80, 1200);
                           desired = _blink.update(true);         break;
      default:             _blink.update(false);
                           desired = false;                       break;
    }
  }
  ...
}
```

El parpadeo lo lleva un `Blink` (de `logic/`), que es un temporizador no
bloqueante: cada llamada mira el reloj y decide. Cero `delay()`.

Fíjate en el `default`: cuando la salida está apagada, `_blink.update(false)` se
llama igualmente. Es lo que **rearma** el temporizador, de modo que al volver a
parpadear empiece por el principio del ciclo y no a mitad.

Y fíjate en que el forzado **puentea el switch entero**: una salida forzada no
parpadea, se queda en el valor impuesto. Es lo que significa forzar.

### 2.3 El watchdog de salida, y por qué ENCLAVA

Esta es la parte que hay que entender bien, porque la versión ingenua no protege
de nada.

```cpp
if (!_safeLatched && _maxOnTimeMs > 0) {
  if (_timedOut) {
    desired = false;                        // enclavado: sigue cortada
  } else if (desired) {
    if (!_physical) _onSince = cfsm_millis();   // flanco de encendido
    if (cfsm_elapsed(_onSince) >= _maxOnTimeMs) {
      desired   = false;
      _timedOut = true;                     // ENCLAVA
    }
  }
}
```

**Por qué el enclavamiento.** Si el watchdog solo cortara un ciclo, al scan
siguiente la salida ya estaría apagada (`_physical == false`), se reiniciaría el
cronómetro con `_onSince = cfsm_millis()`, y volvería a energizarse. En la
práctica la electroválvula seguiría excitada **casi todo el tiempo**, dando
saltitos de un scan. La protección no protegería nada — solo añadiría un
zumbido. Fue uno de los trece defectos que encontró la revisión adversarial.

**Cómo se rearma.** Solo con una orden nueva de verdad: un flanco de apagado
seguido de uno de encendido.

```cpp
void turnOn()  { _safeLatched = false; /* ... */ _mode = OUT_ON;  }
void turnOff() { _safeLatched = false; _mode = OUT_OFF; _timedOut = false; }
```

El `if (_mode == OUT_OFF)` es la clave: `turnOn()` solo levanta el enclavamiento
si venía de estar apagada. Mantener `turnOn()` en cada scan —que es la forma
normal de escribir una salida desde un `switch` de pasos— **no** lo rearma. Si
no fuera así, el enclavamiento no duraría ni un ciclo.

Y hay `clearTimeout()` para un botón explícito de "reponer salidas".

El límite se evalúa dentro de `writeOutputs()`: no es un watchdog físico ni
puede actuar si el scan queda bloqueado. Tampoco anula el valor seguro mientras
`_safeLatched` está activo. Al llegar un mando nuevo se abandona ese estado y el
cronómetro comienza desde ese instante, aunque el nivel físico seguro ya fuera
`true`.

### 2.4 `activeLow`

```cpp
void applyPhysical(bool logical) {
  _physical = logical;
  _pin.write(logical != _activeLow);
}
```

Ese `!=` es un XOR escrito con booleanos. Con `activeLow = false` (un LED), `true`
da nivel alto. Con `activeLow = true` (un módulo de relé activo a bajo), `true`
da nivel bajo. `DigitalPin` envía ese nivel al GPIO nativo o a la imagen del
backend.

`begin()` aplica `safeValue` antes de terminar el arranque. El estado queda
enclavado durante un interbloqueo y también después de liberarlo; `turnOn()`,
`turnOff()`, `setMode()` o `force()` son los mandos explícitos que lo sustituyen.
Así una salida cuyo valor seguro lógico sea `true` no cae a `false` solo porque
llegó el siguiente scan ni porque quedó un cronómetro de una orden anterior.

### 2.5 `isOn()` contra `isActive()`

```cpp
bool isOn()     const { return _mode != OUT_OFF; }   // ¿está mandada?
bool isActive() const { return _physical; }          // ¿está encendida AHORA?
```

Con `OUT_BLINK_SLOW`, `isOn()` es siempre `true` e `isActive()` va alternando.
Para "¿le he dado la orden?" quieres la primera; para "¿está el LED encendido en
este instante?" o para contar tiempo real de excitación, la segunda.

### 2.6 `AnalogOutput` y la rampa que evita el reinicio

Al final del archivo hay una segunda clase, para PWM:

```cpp
void writeOutputs() override {
  if (_rampStep > 0 && _current != _target) {
    if (cfsm_elapsed(_lastRamp) >= 1) {          // como mucho un escalón por ms
      _lastRamp = cfsm_millis();
      int16_t diff = (int16_t)_target - (int16_t)_current;
      int16_t step = (int16_t)_rampStep;
      if      (diff >  step) _current += _rampStep;
      else if (diff < -step) _current -= _rampStep;
      else                   _current  = _target;
    }
  } else {
    _current = _target;
  }
  apply();
}
```

**Una rampa no es un adorno.** Arrancar un motor de golpe a plena potencia
provoca un pico de corriente que hunde la tensión de alimentación. En un Arduino
alimentado desde la misma batería que los motores, ese hundimiento cruza el
umbral de brown-out y **reinicia el microcontrolador**. Es la causa número uno
de "el robot se reinicia solo al arrancar", y se diagnostica mal siempre: se
culpa al código, al cable USB o a la librería.

`setRamp(2)` lleva de 0 a 255 en unos 128 ms, que suele bastar para que la
fuente no se hunda.

Los cast a `int16_t` de la resta son necesarios: `_target` y `_current` son
`uint8_t`, y `0 - 255` en aritmética sin signo daría 1, no −255.

## 3. API completa

### `DigitalOutput`

| Método | Firma | Qué hace |
|---|---|---|
| constructor | `DigitalOutput(uint8_t pin, bool activeLow = false, bool safeValue = false)` | GPIO nativo |
| constructor backend | `DigitalOutput(IDigitalBackend&, uint8_t channel, bool activeLow = false, bool safeValue = false)` | Canal agrupado |
| `turnOn()` / `turnOff()` / `set(bool)` | | Mando básico |
| `setMode(OutputMode)` | | Los cinco modos |
| `toggle()` | | Conmuta entre `OUT_OFF` y `OUT_ON` |
| `isOn()` | `bool` | ¿Mandada? (modo distinto de OFF) |
| `isActive()` | `bool` | ¿Encendida **ahora mismo**? |
| `mode()` | `OutputMode` | |
| `setMaxOnTime(ms)` | | Límite activo software. `0` = sin límite |
| `hasTimedOut()` / `clearTimeout()` | | Estado y rearme del límite |
| `setSafeValue(bool)` / `safeValue()` | | Estado lógico del interbloqueo |
| `force(bool)` | | Impone valor, puentea el modo |
| `enterSafeState()` | | Borra orden/forzado y aplica `safeValue` inmediatamente |
| `pin()` / `usesNativePin()` / `describe(Print&)` | | |

### `AnalogOutput`

| Método | Firma | Qué hace |
|---|---|---|
| constructor | `AnalogOutput(uint8_t pin, bool activeLow = false)` | |
| `setValue(uint8_t)` | | Consigna, 0-255 |
| `value()` / `setpoint()` | `uint8_t` | Valor actual / consigna |
| `setRamp(uint8_t stepPerMs)` | | Escalón por ms. `0` = sin rampa |
| `force(uint8_t)` | | |
| `enterSafeState()` | | Borra forzado, consigna y PWM aplicado |

## 4. Ejemplos

### 4.1 Los modos, aplicados

```cpp
DigitalOutput pilotoCiclo (11);
DigitalOutput pilotoAviso (12);
DigitalOutput releBomba   (10, true);    // módulo de relé: activo a bajo

void loop() {
  ...
  pilotoCiclo.setMode(proceso.isRunning()  ? OUT_ON         : OUT_FLASH);
  pilotoAviso.setMode(proceso.ST.stw.warning ? OUT_BLINK_SLOW : OUT_OFF);
  releBomba.set(proceso.bombaEnMarcha);
  io.writeAllOutputs();
}
```

El `OUT_FLASH` del piloto de ciclo es el latido: desde la puerta de la nave se
distingue una máquina apagada de una máquina encendida esperando trabajo.

### 4.2 El watchdog en una electroválvula

```cpp
DigitalOutput valvulaLlenado(9);

void setup() {
  ...
  /* El llenado normal son 2 s. Si a los 6 sigue abierta, algo va mal:
   * un sensor de nivel roto, la lógica colgada. Corta y enclava. */
  valvulaLlenado.setMaxOnTime(6000);
}

void loop() {
  ...
  valvulaLlenado.set(proceso.llenando);

  /* Y hay que enterarse de que ha cortado: si no, la máquina hace
   * ciclos vacíos sin llenar nada. */
  alarmas.raiseIf(valvulaLlenado.hasTimedOut(),
                  ALM_VALVULA_PEGADA, F("Valvula de llenado cortada por watchdog"),
                  ALARM_CRITICAL);
  io.writeAllOutputs();
}
```

Y en la consola de mantenimiento, el rearme explícito:

```cpp
if (c == 'v') { valvulaLlenado.clearTimeout(); alarmas.ack(ALM_VALVULA_PEGADA); }
```

### 4.3 La rampa de un motor

```cpp
AnalogOutput motorIzq(5), motorDer(6);

void setup() {
  motorIzq.setRamp(2);      // 0 a 255 en ~128 ms
  motorDer.setRamp(2);
}

void loop() {
  ...
  motorIzq.setValue(robot.pwmIzq);
  motorDer.setValue(robot.pwmDer);
  io.writeAllOutputs();
}
```

Si el robot se te reinicia al arrancar los motores, **antes de tocar el código**
prueba esto y mira si desaparece. Y si desaparece, ya sabes que el problema era
eléctrico y no lógico.

### 4.4 Distinguir "mandada" de "encendida"

```cpp
/* Contar el tiempo REAL de excitación de una resistencia, para mantenimiento.
 * Con isOn() contarías también los huecos del parpadeo. */
if (resistencia.isActive()) msExcitada += msPorScan;
```

## 5. Decisiones de diseño

**El watchdog enclava y solo se rearma con flanco.** Ver §2.3. La alternativa
—cortar un ciclo— es la implementación evidente y no protege de nada.

**Los tiempos de parpadeo están fijos en el `switch`, no son configurables.** A
propósito: si cada salida pudiera tener su propio ritmo, dos pilotos "de aviso"
de la misma máquina acabarían parpadeando distinto y el operario dejaría de leer
el ritmo como información. Es el mismo criterio que la normalización de colores
de la baliza.

**`AnalogOutput` vive en este archivo y no en uno propio.** Comparte casi todo el
planteamiento con `DigitalOutput` y separarlo habría duplicado los comentarios de
`activeLow` y de forzado. Es discutible; el criterio fue mantener juntas las dos
cosas que escriben en pines de salida.

**La rampa avanza como mucho un escalón por milisegundo, no por scan.** Si
avanzara por scan, la duración de la rampa dependería de lo rápido que corra tu
programa: la misma máquina rampearía distinto según cuántos bloques tenga. Atarlo
al reloj lo hace predecible.

## 6. Errores frecuentes

**Olvidar `activeLow = true` en un módulo de relé.** Arranca pegado y queda
invertido toda la ejecución. Se ve enseguida pero desconcierta.

**Esperar que `turnOn()` rearme el watchdog.** No lo hace si ya estaba
encendida, y es deliberado. Usa `clearTimeout()`.

**Poner `setMaxOnTime()` a un piloto.** El piloto de "máquina en marcha" se
apagaría solo tras el plazo y parecería una avería. El watchdog es para lo que
puede causar daño: electroválvulas, resistencias, bobinas, motores.

**No enterarse de que el watchdog ha cortado.** La máquina sigue haciendo ciclos
que no hacen nada. Engánchalo siempre a una alarma. Ver §4.2.

**Escribir la salida y no llamar a `writeOutputs()`.** `set()` solo cambia el
modo: quien escribe el pin es la fase PAA.

**Confundir `safeValue` lógico con nivel eléctrico.** La clase aplica después
`activeLow`; verifica con el actuador desconectado qué tensión aparece realmente.

**PWM sin rampa alimentando motores desde la misma batería.** Ver §2.6.

**Usar `isOn()` para medir tiempo de excitación.** Con parpadeo cuentas también
los huecos. Ver §4.4.

## 7. Coste

`DigitalOutput`, en AVR:

| Miembro | Bytes |
|---|---:|
| `_pin` (`DigitalPin`), flags y modo | depende del puntero y alineación |
| `_maxOnTimeMs`, `_onSince` | 8 |
| `_blink` | lo que ocupe `Blink` (unos 6) |
| `IDevice` | 5 |

La cifra depende ahora del tamaño de `DigitalPin` y de la alineación de la
arquitectura. Obtén el tamaño con el toolchain de destino; `AnalogOutput` sigue
siendo más ligero porque no contiene `DigitalPin`, `Blink` ni watchdog digital.

## 8. Relación con el resto

```
   Tu lógica  ──set()/setMode()──▶  _mode
                                      │  fase PAA
                                      ▼
                              writeOutputs()
                                │        │
                          Blink │        │ watchdog (_maxOnTimeMs)
                        (logic/)│        │ enclava en _timedOut
                                ▼        ▼
                            applyPhysical() ──▶ digitalWrite

   DigitalOutput ── es un IDevice ──▶ DeviceManager ──▶ HW.writeOutputs()
        │
        ├── lo genera IOTable.h de cada fila CFSM_TABLE_DO
        ├── lo usa TowerLight  (tres de ellos + zumbador)
        └── lo usa MotorDrive  (dirección y freno)
```
