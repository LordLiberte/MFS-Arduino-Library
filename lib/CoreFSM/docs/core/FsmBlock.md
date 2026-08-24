# FsmBlock.h

> El ciclo de vida de cualquier equipo automático, escrito una sola vez: parado, arrancando, produciendo, esperando, parándose, averiado.

**Ruta:** `src/core/FsmBlock.h`
**Incluye:** `BlockBase.h`, `ControlWords.h`
**Lo usan:** `SequenceBlock.h` (hereda de él), `MotorDrive.h`, `Chassis.h`, y cualquier bloque de usuario que no necesite pasos.

---

## 1. Qué problema resuelve

Una cinta, una prensa y un robot no se parecen en nada por dentro. Pero los tres
atraviesan siempre el mismo ciclo de vida: están parados, arrancan, producen, se
paran, y de vez en cuando fallan. Escribir ese ciclo a mano en cada bloque nuevo
es repetitivo, y es exactamente donde se cuelan los errores: el rearme que
funciona con la seta pulsada, la pausa que no pausa, el arranque que se dispara
dos veces.

`FsmBlock` lo implementa una vez. Los bloques hijos heredan los estados, las
transiciones legales y los comandos, y solo escriben lo que su máquina hace de
verdad.

## 2. Cómo funciona por dentro

### 2.1 Los nueve estados

`SystemState` es un `uint8_t` con nombre. Es un subconjunto pragmático de
**PackML (ISA-TR88.00.02 / OMAC)**, recortado a lo que de verdad se usa en una
máquina pequeña, porque los diecisiete estados completos son un exceso para un
Arduino.

| Valor | Estado | Qué significa |
|---:|---|---|
| 0 | `STATE_IDLE` | Parada, sin fallos, lista para recibir orden |
| 1 | `STATE_STARTING` | Secuencia de arranque: precalentar, presurizar, esperar aire |
| 2 | `STATE_RUNNING` | Produciendo. **El único donde corre la lógica de proceso** en el modelo clásico |
| 3 | `STATE_PAUSED` | Pausa en caliente. El paso se conserva y las salidas peligrosas se apagan |
| 4 | `STATE_STOPPING` | Parada ordenada: frenar, replegar, cerrar |
| 5 | `STATE_STOPPED` | Parada completada. Transita sola a `IDLE` |
| 6 | `STATE_ERROR` | Alarma. Retenido hasta que alguien rearme |
| 7 | `STATE_SUSPENDED` | *(2.1)* Espera por causa **externa**. Se reanuda **sola** |
| 8 | `STATE_HELD` | *(2.1)* Espera por causa **interna** o del operario |

```
IDLE ──start()──▶ STARTING ──▶ RUNNING ──hold()──▶ PAUSED ──resume()──┐
 ▲                                │  ▲                                 │
 │                                │  └─────────────────────────────────┘
 │                                │
 │                                ├──suspendWhile(c)──▶ SUSPENDED
 │                                └──holdWhile(c)─────▶ HELD
 │                                      (vuelven solos a RUNNING)
 └── STOPPED ◀── STOPPING ◀──stop()┘

  Desde CUALQUIER estado:  fault(código) ──▶ ERROR ──reset()──▶ IDLE
```

Los dos estados de espera no son pausa ni avería. Son **una máquina sana que no
puede producir ahora mismo**, y separarlos de `RUNNING` es lo que permite que un
paso espere indefinidamente sin que salte ningún watchdog. La diferencia entre
los dos viene de PackML: `SUSPENDED` es causa externa —no llega pieza, la
estación siguiente está llena— y se resuelve sola; `HELD` es causa interna o
decisión del operario —recargar, control de calidad— y necesita que alguien haga
algo.

`cfsmStateName()` devuelve el nombre en flash. `StepTracer` lo usa, así que los
estados nuevos aparecen en la traza sin tocar nada.

### 2.2 Las transiciones ilegales se ignoran en silencio

```cpp
void start() override {
  if (_currentState == STATE_IDLE || _currentState == STATE_STOPPED) { ... }
}
```

Si la máquina ya está en marcha, `start()` **no hace nada y no se queja**. Lo
mismo `resume()` sin estar en pausa, o `reset()` sin estar en fallo.

Es deliberado, y tiene una consecuencia práctica grande: el código del `.ino`
puede ser descuidado sin consecuencias. Puedes llamar a `start()` en cada vuelta
del scan mientras el operario tenga el dedo en el pulsador, y la máquina
arrancará una sola vez. La alternativa —devolver un error o entrar en fallo—
obligaría a envolver cada comando en un `if` de guarda, que es justo el tipo de
código que se olvida y luego muerde.

### 2.3 `start()` y el salto directo a RUNNING

```cpp
transitionTo(_startupTimeMs > 0 ? STATE_STARTING : STATE_RUNNING);
```

Si no hay fase de arranque configurada, se salta **directamente** a `RUNNING` en
vez de pasar por `STARTING`. No es solo comodidad: es lo que mantiene vivos los
bloques escritos a la antigua.

El razonamiento: quien saca la máquina de `STARTING` es `updateFsm()`, mirando
si `getTimeInState() >= _startupTimeMs`. Un bloque que gobierna sus pasos a mano
y **no llama nunca a `updateFsm()`** se quedaría colgado en el arranque para
siempre. Con el salto directo, ese bloque funciona igual que antes.

Con `setStartupTime(3000)`, en cambio, la máquina se queda 3 s en `STARTING`
—tiempo para que suba la presión de aire, arranque un ventilador o se caliente
una resistencia— y pasa sola a `RUNNING`.

### 2.4 Las tres consultas que más se confunden

```cpp
bool isRunning() const { return _currentState == STATE_RUNNING; }
bool isWaiting() const { return _currentState == STATE_SUSPENDED ||
                                _currentState == STATE_HELD; }
bool isActive()  const { return _currentState == STATE_RUNNING || isWaiting(); }
```

- **`isRunning()` = PRODUCIENDO.** Estricto. Es el que quieres para encender el
  piloto verde, para el bit `running` de la STW, o para decidir si aceptas una
  receta nueva.
- **`isActive()` = LA LÓGICA DEL PASO DEBE EJECUTARSE.** Incluye las dos
  esperas, **y tiene que incluirlas**: una máquina suspendida sigue teniendo que
  evaluar su condición de espera en cada scan. Si no, no saldría nunca de ella.

Por eso `updateFsm()` termina con `return isActive();` y no con
`return _currentState == STATE_RUNNING;`. Lo que se congela durante una espera
son los cronómetros, no la ejecución. De eso se encarga `updateSequence()`.

### 2.5 `updateFsm()`, transición a transición

```cpp
bool updateFsm() {
  switch (_currentState) {
    case STATE_STARTING:
      if (getTimeInState() >= _startupTimeMs) transitionTo(STATE_RUNNING);
      break;
    case STATE_STOPPING:
      if (getTimeInState() >= _shutdownTimeMs) transitionTo(STATE_STOPPED);
      break;
    case STATE_STOPPED:
      transitionTo(STATE_IDLE);      // un ciclo de gracia y a reposo
      break;
    default: break;
  }
  return isActive();
}
```

`STATE_STOPPED` dura **exactamente un scan**. Existe para que el bloque hijo
pueda reaccionar en `onTransition()` —apagar algo, imprimir una traza, contar
una parada— y después vuelve solo a `IDLE`. Si no existiera, no habría ningún
instante en el que el hijo pudiera enterarse de que la parada terminó.

Fíjate en que `SUSPENDED` y `HELD` caen en el `default`: `updateFsm()` no los
toca. Quien entra y sale de ellos es `waitWhile()` en `SequenceBlock`.

### 2.6 `fault()` conserva la PRIMERA causa

```cpp
virtual void fault(uint16_t code = CFSM_ERR_INTERLOCK) {
  if (_currentState != STATE_ERROR) {
    _errorCode = code;
    transitionTo(STATE_ERROR);
  }
}
```

La guarda `if (_currentState != STATE_ERROR)` es lo que hace que se conserve el
primer código. Si una avería provoca en cascada otras tres, lo que le interesa
al técnico es **la causa raíz, no el último síntoma**. Es el equivalente del
"primera causa" o *first-out* de un cuadro industrial: se enclava la señal que
disparó primero, porque a los dos segundos todas las demás también están
disparadas y ya no dicen nada.

`abort()` es simplemente `fault(CFSM_ERR_ESTOP)`: salta a `ERROR` sin pasar por
`STOPPING`. No es lo mismo que `stop()`, que es ordenado y termina el ciclo.
`abort()` cambia el estado lógico de inmediato, pero por sí solo no escribe
GPIO ni retira energía. Cada bloque debe llevar sus mandos a seguro y el ciclo
debe aplicar la PAA o `HW.setSafetyInterlock()`. No sustituye una parada de
emergencia cableada.

### 2.7 `reset()` y el permiso del hijo

```cpp
void reset() override {
  if (_currentState != STATE_ERROR) return;
  if (!canReset()) return;            // la causa sigue presente
  _errorCode = CFSM_ERR_NONE;
  transitionTo(STATE_IDLE);
}
```

`canReset()` es un hook virtual que devuelve `true` por defecto. Un bloque hijo
puede negarse mientras la causa física siga ahí:

```cpp
bool canReset() const override {
  return !setaEmergenciaPulsada && puertaCerrada;
}
```

Esto evita rearmar mientras la entrada lógica que representa la causa sigue
activa, y obliga a que el rearme signifique algo. La apertura del circuito de
seguridad físico se resuelve fuera de este bloque.

### 2.8 `getErrorCode()` es virtual, y por qué

```cpp
virtual uint16_t getErrorCode() const { return _errorCode; }
const __FlashStringHelper* getErrorText() const { return cfsmErrorText(getErrorCode()); }
```

Un `SequenceBlock` guarda su código de error dentro de `ST`, que es la
estructura que ve el HMI y la que imprimen `describe()` y el `StepTracer`. Si
este getter devolviera siempre la copia interna `_errorCode`, un bloque que
traduce en `onTransition()` un código genérico de la librería a uno propio de su
máquina acabaría diciendo una cosa por la consola y otra por `getErrorCode()`.

Dos verdades para el mismo dato es justo lo que hace que nadie se fíe del
diagnóstico. Por eso `SequenceBlock` lo redefine y devuelve `ST.errorCode`.

### 2.9 `transitionTo()`

```cpp
void transitionTo(SystemState newState) {
  if (_currentState == newState) return;      // ignora las transiciones a uno mismo
  _previousState  = _currentState;
  _currentState   = newState;
  _stateStartTime = cfsm_millis();
  onTransition(_previousState, _currentState);
}
```

Ignorar la transición a uno mismo importa: significa que llamarlo de más es
inofensivo y **no reinicia el cronómetro de estado por accidente**.

## 3. API completa

### Consulta

| Método | Devuelve |
|---|---|
| `getState()` | El estado como `uint8_t` (implementa `BlockBase`) |
| `state()` / `previousState()` | El estado actual / anterior como `SystemState` |
| `isIdle()` / `isPaused()` / `isFaulted()` | Estado concreto |
| `isRunning()` | **Solo** `RUNNING` |
| `isWaiting()` | `SUSPENDED` o `HELD` |
| `isActive()` | `RUNNING`, `SUSPENDED` o `HELD` |
| `getErrorCode()` (virtual) / `getErrorText()` | Causa del fallo |

### Comandos

| Método | Desde qué estados actúa |
|---|---|
| `start()` | `IDLE`, `STOPPED` |
| `stop()` | `RUNNING`, `STARTING`, `PAUSED`, y las dos esperas |
| `hold()` | `RUNNING` y las dos esperas |
| `resume()` | `PAUSED` |
| `fault(código)` | Cualquiera (si no está ya en `ERROR`) |
| `abort(código)` | Cualquiera. Equivale a `fault(CFSM_ERR_ESTOP)` |
| `reset()` | Solo `ERROR`, y solo si `canReset()` |
| `onEmergencyStop()` | Cualquiera |

### Configuración y motor

| Método | Qué hace |
|---|---|
| `setStartupTime(ms)` / `setShutdownTime(ms)` | Duración de `STARTING` / `STOPPING`. `0` = instantáneas |
| `updateFsm()` | El motor. Primera línea del `update()` del hijo |

### Hooks

| Hook | Cuándo se llama |
|---|---|
| `onTransition(from, to)` | Una vez, en el instante del cambio de estado |
| `canReset()` | Al pedir rearme. `false` lo deniega |

## 4. Ejemplos

### 4.1 Un bloque sin pasos: un ventilador con arranque suave

```cpp
class Extractor : public FsmBlock {
 public:
  bool marcha = false;
  uint8_t pwm = 0;

  void begin() override {
    setName(F("EXTRACTOR"));
    setStartupTime(4000);      // 4 s subiendo revoluciones
    setShutdownTime(2000);     // 2 s frenando
  }

  void update() override {
    updateFsm();     // <-- IMPRESCINDIBLE: es quien saca la máquina de STARTING

    switch (state()) {
      case STATE_STARTING: {
        uint32_t r = (getTimeInState() * 255UL) / 4000UL;       // rampa
        pwm = (r > 255) ? 255 : (uint8_t)r;                     // y se satura
        break;
      }
      case STATE_RUNNING:  pwm = 255; break;
      case STATE_STOPPING: pwm = 128; break;
      default:             pwm = 0;   break;   // IDLE, STOPPED, ERROR
    }
  }
};
```

No hay pasos, no hay `updateSequence()`, no hace falta. El estado ya describe
todo lo que esta máquina sabe hacer.

Las dos líneas marcadas son las que este ejemplo tenía mal en el primer borrador,
y las cazó el banco de pruebas al compilarlo y ejecutarlo:

- **Sin `updateFsm()`**, el extractor se queda en `STARTING` para siempre. Es
  exactamente el error frecuente que se describe más abajo, cometido en el
  ejemplo que lo explica.
- **Sin saturar la rampa**, a los 40 s el cálculo da 2550, y al truncarlo a
  `uint8_t` sale 246: el ventilador se quedaría al 96 % en vez de al 100 %. Los
  desbordamientos de tipo pequeño no dan error, dan comportamiento raro.

### 4.2 Negar el rearme mientras la causa siga presente

```cpp
class Prensa : public SequenceBlock {
 public:
  bool setaPulsada = false;
  bool puertaCerrada = true;

 protected:
  bool canReset() const override {
    return !setaPulsada && puertaCerrada;
  }
};
```

Ahora `manager.resetAll()` desde la consola no consigue nada mientras la seta
esté hundida, y eso es exactamente lo que debe pasar.

### 4.3 Traducir un fallo genérico a uno de tu máquina

```cpp
protected:
  void onTransition(SystemState from, SystemState to) override {
    CFSM_UNUSED(from);
    if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT &&
        _currentStep == PASO_BAJAR) {
      ST.errorCode = ALM_CILINDRO_ATASCADO;
    }
  }
```

"Timeout de paso" le dice al técnico que algo tardó. "Cilindro atascado" le dice
dónde poner la mano. Funciona porque `getErrorCode()` es virtual (ver 2.8).

### 4.4 Reaccionar a la parada, aprovechando el ciclo de gracia de STOPPED

```cpp
protected:
  void onTransition(SystemState from, SystemState to) override {
    if (to == STATE_STOPPED) {
      Serial.print(F("[EXTRACTOR] parado tras "));
      Serial.print(paradasHoy++);
      Serial.println(F(" paradas hoy"));
    }
    if (from == STATE_ERROR && to == STATE_IDLE) {
      Serial.println(F("[EXTRACTOR] rearmado"));
    }
  }
```

## 5. Decisiones de diseño

**Nueve estados y no diecisiete.** PackML completo incluye los transitorios
`Holding`, `Unholding`, `Suspending`, `Unsuspending`, `Completing`, `Clearing`,
`Resetting`. Son necesarios cuando una línea entera de fabricantes distintos
tiene que entenderse por bus. En un Nano de 2 KB, con un solo programador, son
ceremonia. Se cogieron los estados que se pagan solos.

**`isRunning()` se dejó estricto al añadir las esperas.** Cambiarlo para incluir
`SUSPENDED` habría roto en silencio el código existente: en el ejemplo 05,
`if (!manipulador.isRunning())` decide si se admite una receta nueva, y admitir
una receta mientras la máquina está suspendida a mitad de ciclo sería un fallo
grave. Se prefirió añadir `isActive()` y documentar la diferencia.

**El hook es `onTransition(from, to)` y no dos hooks separados.** Con `from`
disponible puedes distinguir "entré en IDLE desde un rearme" de "entré en IDLE
desde una parada normal", que son situaciones distintas.

## 6. Errores frecuentes

**Usar `isRunning()` donde hacía falta `isActive()`.** Si escribes tu propio
motor de bloque y compruebas `isRunning()`, tu lógica dejará de correr en cuanto
la máquina entre en una espera, y entonces no habrá quien evalúe la condición
para salir: se queda colgada. Usa `isActive()`.

**Olvidar `updateFsm()` en un bloque que sí tiene fases de arranque.** La
máquina se queda en `STARTING` para siempre. El salto directo solo salva a los
bloques con `_startupTimeMs == 0`.

**Meter esperas o bucles en `onTransition()`.** Corre dentro del scan. Un
`delay()` ahí alarga el ciclo entero y te descoloca todos los antirrebotes.

**Esperar que `fault()` sobrescriba la causa.** No lo hace, a propósito. Si
necesitas cambiarla, hazlo desde `onTransition()` sobre `ST.errorCode`.

**Olvidar `begin()`.** Es virtual **pura** en `BlockBase`, así que un bloque sin
`begin()` no es que se comporte mal: no compila. El error es
`cannot declare variable 'x' to be of abstract type`, y la nota del compilador
te dice exactamente qué función falta.

**Confundir `stop()` con `abort()`.** `stop()` pasa por `STOPPING` y da tiempo a
replegar. `abort()` entra directamente en `ERROR`; úsalo para un fallo lógico
que no admite el cierre ordenado. Ninguno de los dos constituye por sí solo una
parada de emergencia física.

## 7. Coste

Por instancia, en AVR:

| Miembro | Bytes |
|---|---:|
| `_currentState`, `_previousState` | 2 |
| `_errorCode` | 2 |
| `_startupTimeMs`, `_shutdownTimeMs` | 8 |
| `_stateStartTime` (heredado de `BlockBase`) | 4 |
| puntero a vtable | 2 |

**Unos 18 bytes** más lo que aporte `BlockBase`. Los métodos son todos `inline`
dentro de la clase, así que el flash depende de cuáles uses de verdad.

## 8. Relación con el resto

```
        BlockBase.h            (nombre, id, describe(), interfaz común)
             │
             ▼
        FsmBlock.h             ← estás aquí
             │  estados, transiciones, comandos, fault/reset
             │
             ▼
      SequenceBlock.h          añade SC/ST, pasos, relojes y esperas
             │
     ┌───────┴────────┐
     ▼                ▼
 tus bloques    RecipeExecutor.h

  MotorDrive.h y Chassis.h heredan de FsmBlock directamente:
  un motor tiene estados, pero no tiene una secuencia por pasos.
```
