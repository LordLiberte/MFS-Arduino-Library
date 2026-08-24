# SequenceBlock.h

> El secuenciador por pasos. Es el archivo más grande de la librería y donde vive casi todo lo que hace que CoreFSM se parezca a un autómata.

**Ruta:** `src/core/SequenceBlock.h` (692 líneas)
**Incluye:** `FsmBlock.h`, `Handshake.h`
**Lo usan:** todos los bloques de proceso del usuario, `RecipeExecutor.h`, `StepTracer`.

---

## 1. Qué problema resuelve

Escribir una secuencia a mano con `if` anidados y variables de estado sueltas
funciona hasta el cuarto paso. A partir de ahí nadie sabe ya en qué situación
está la máquina, no hay forma de vigilar tiempos sin llenar el código de
cronómetros repetidos, y el día que haga falta intercalar un paso hay que
renumerar todo.

`SequenceBlock` aporta el modelo del GRAFCET / SFC industrial: pasos numerados,
un cronómetro por paso con su vigilancia, un cronómetro de ciclo, contadores de
producción, palabras de mando y estado, y un traspaso formal con la estación
vecina. Todo eso escrito una vez.

## 2. Cómo funciona por dentro

### 2.1 Dos niveles de máquina de estados, no uno

Es la confusión más habitual al empezar, así que conviene dejarla clara:

- El **ESTADO** (`SystemState`: IDLE, RUNNING, ERROR…) describe la situación del
  **equipo**. Lo gestiona `FsmBlock`. Es igual para una cinta que para una
  prensa: toda máquina está parada, en marcha o averiada.
- El **PASO** (`_currentStep`: 0, 10, 20…) describe por dónde va el **proceso
  concreto**. Lo gestiona esta clase. Es distinto en cada máquina: avanzar
  carro, soldar, retroceder.

**El estado envuelve al paso.** Los pasos solo corren cuando el estado lo
permite; si la máquina se para o falla, el paso se congela donde estaba y se
retoma exactamente ahí al reanudar.

Los pasos van **de 10 en 10** por costumbre heredada del GRAFCET: numerar 0, 10,
20, 30 deja hueco para intercalar un paso 15 el día que haga falta sin renumerar
toda la secuencia ni tocar los mensajes de diagnóstico.

### 2.2 SC — datos de control de secuencia

Copia el esquema de los SEQCTL industriales. Es una estructura **pública** a
propósito: se lee desde el `.ino`, se vuelca por telemetría y, puntualmente, se
escribe (por ejemplo `SC.cycleStartTime`).

| Campo | Tipo | Bytes | Para qué |
|---|---|---:|---|
| `step` | `uint16_t` | 2 | Paso activo |
| `lastStep` | `uint16_t` | 2 | Paso del que se viene. Nace en `CFSM_NO_STEP` (`0xFFFF`) |
| `stepStartTime` | `cfsm_time_t` | 4 | Marca de entrada al paso |
| `stepWarnTime` | `cfsm_time_t` | 4 | *(2.1)* Primer escalón de vigilancia. `0` = sin aviso |
| `stepTimeout` | `cfsm_time_t` | 4 | Segundo escalón. `0` = sin vigilancia |
| `cycleStartTime` | `cfsm_time_t` | 4 | Marca de inicio del ciclo productivo |
| `cycleTimeout` | `cfsm_time_t` | 4 | Límite duro del ciclo. `0` = sin vigilancia |
| `cycleTarget` | `cfsm_time_t` | 4 | *(2.1)* Takt objetivo. Solo aviso |
| `lastCycleTimeMs` | `cfsm_time_t` | 4 | Ciclo anterior, **solo productivo** |
| `blockedTime` | `cfsm_time_t` | 4 | *(2.1)* Espera acumulada del ciclo en curso |
| `lastBlockedTime` | `cfsm_time_t` | 4 | *(2.1)* Espera del ciclo anterior |
| `cycleCount` | `uint32_t` | 4 | Piezas / ciclos completos |
| `initialStep` | `uint16_t` | 2 | Paso al que vuelve el arranque y el rearme |

**46 bytes.**

### 2.3 ST — estado y mando

```cpp
struct StatusData {
  StatusWord stw;                 // lo que la estación responde   (2 B)
  ConfigWord cfgw;                // lo que se le ordena           (2 B)
  uint16_t   errorCode;           // la causa del fallo            (2 B)
} ST;
```

Seis bytes que contienen todo lo que un HMI o un maestro de línea necesita
saber. Ver [ControlWords.md](ControlWords.md) para el detalle bit a bit.

### 2.4 `_currentStep`, la referencia que cuesta dos bytes

```cpp
SequenceBlock() : _currentStep(SC.step) { ... }
```

`_currentStep` es una **referencia** a `SC.step`. Existe por una sola razón:
para que el código que escribes dentro del bloque pueda decir

```cpp
switch (_currentStep) { ... }
```

que se lee mucho mejor que `switch (SC.step)`. Cuesta 2 bytes por instancia en
AVR y ahorra ruido en el sitio donde más se lee el código. Es un intercambio
consciente: memoria por legibilidad, en el punto donde la legibilidad se paga
todos los días.

### 2.5 `updateSequence()` fase a fase

Es el corazón. Se llama **siempre** como primera línea del `update()` del bloque
hijo y devuelve `true` si toca ejecutar la lógica de pasos:

```cpp
void update() override {
  if (!updateSequence()) { salidasSeguras(); return; }
  switch (_currentStep) { ... }
}
```

Por dentro hace esto, en este orden exacto:

#### Fase 1 — Palabra de mando

`processControlWord()` traduce los bits de CFGW a las llamadas equivalentes y
los consume. Va primero porque una orden recibida por bus tiene que surtir
efecto en **este** scan, no en el siguiente.

#### Fase 1b — Liberación automática de la espera

```cpp
if (isWaiting() && !_waitRequested) transitionTo(STATE_RUNNING);
_waitRequested = false;
```

`_waitRequested` lo levanta `suspendWhile()` / `holdWhile()` durante la lógica
del paso, es decir **después** de esta función. Lo que se mira aquí es, por
tanto, si en el scan **anterior** alguien pidió seguir esperando.

Si nadie lo pidió —porque la condición se cumplió, porque se cambió de paso, o
sencillamente porque se dejó de llamar—, la máquina vuelve sola a `RUNNING`. Es
la red de seguridad que impide quedarse colgado en una espera por un olvido.

#### Fase 2 — Máquina de estados de alto nivel

`bool active = updateFsm();` Devuelve `isActive()`, es decir `RUNNING` o
cualquiera de las dos esperas.

#### Fase 3 — Retención en modo paso a paso

```cpp
bool retenido = active && ST.cfgw.singleStep && !_stepAuthorised;
```

Se evalúa **antes** de las vigilancias de tiempo, y ahí está el motivo de que
esta fase esté aquí y no más abajo: si se hiciera después, el técnico que tarda
cinco segundos en pulsar "siguiente paso" vería saltar el watchdog sin que se
haya ejecutado una sola línea del paso, y el modo de puesta en marcha sería
inservible.

#### Fase 4 — Congelación de relojes

```cpp
bool esperando = isWaiting();
bool congelado = retenido || (_currentState == STATE_PAUSED) || esperando;
```

Los cronómetros de paso y de ciclo se detienen mientras la máquina está en
pausa, retenida o esperando. Sin esto, una pausa de treinta segundos haría
saltar el watchdog del paso en el instante de reanudar: la máquina caería en
alarma **por haber estado parada**, que es justo lo contrario de lo que debe
pasar.

El mecanismo tiene tres piezas:

```cpp
if (congelado) {
  if (!_frozen) {
    _frozen = true; _freezeStart = cfsm_millis(); _freezeWasWait = esperando;
  } else if (_freezeWasWait != esperando) {
    closeFreezeChunk();                    // cambió el motivo a media parada
    _freezeStart = cfsm_millis(); _freezeWasWait = esperando;
  }
} else if (_frozen) {
  closeFreezeChunk();
  _frozen = false;
}
```

La rama del medio existe para un caso real: el operario pulsa pausa **mientras**
la máquina esperaba pieza. Se cierra el tramo anterior con su etiqueta y se abre
uno nuevo, para que el tiempo de espera y el de pausa no acaben en el mismo
saco. Está cubierto por la prueba P8 del banco.

`closeFreezeChunk()` desplaza las marcas de origen hacia delante:

```cpp
void closeFreezeChunk() {
  cfsm_time_t parado = cfsm_elapsed(_freezeStart);
  SC.stepStartTime  += parado;
  SC.cycleStartTime += parado;
  if (_freezeWasWait) SC.blockedTime += parado;
}
```

Y `descontarCongelado()` hace que los cronómetros digan la verdad **durante** la
congelación, no solo después:

```cpp
cfsm_time_t descontarCongelado(cfsm_time_t desde) const {
  cfsm_time_t v = cfsm_elapsed(desde);
  if (_frozen) {
    cfsm_time_t parado = cfsm_elapsed(_freezeStart);
    v = (v > parado) ? (v - parado) : 0;
  }
  return v;
}
```

Sin esto, tu código de usuario que consulta `getTimeInStep()` dentro de un paso
vería pasar el tiempo de una pausa que, por definición, no debe contar.

#### Fase 5 — Vigilancias de tiempo

```cpp
bool enReposo = (SC.step == SC.initialStep);

if (active && !congelado && !ST.cfgw.bypassTimer) {
  const cfsm_time_t tPaso  = getTimeInStep();
  const cfsm_time_t tCiclo = getCycleTime();
  ...
}
```

Los dos cronómetros se leen **una vez**. Cada lectura es una resta de 32 bits,
que en un AVR de 8 bits no es gratis ni en tiempo ni en flash, y aquí se
consultan hasta cuatro veces seguidas.

`enReposo` es la condición que impide el falso positivo más molesto de la 2.0:
parada en su paso inicial esperando orden, la máquina **no está dentro de ningún
ciclo**, así que la vigilancia de ciclo no aplica. Sin esta condición, una
máquina encendida y sin trabajo caería en alarma sola al cabo de
`setCycleTimeout()`. Es la prueba P2 del banco, y es lo que hace que el código
escrito para la 2.0 mejore sin tocar una línea.

Dentro, tres bloques:

```cpp
// 5a. Aviso de paso: primer escalón. No para la máquina.
if (SC.stepWarnTime > 0 && !_stepWarnFired && tPaso >= SC.stepWarnTime) {
  _stepWarnFired = true; ST.stw.stepWarn = true; onStepWarning(SC.step);
}

// 5b. Takt objetivo: tampoco para la máquina.
if (!enReposo && SC.cycleTarget > 0 && tCiclo >= SC.cycleTarget) _cycleWarn = true;

// 5c. Los dos límites duros, que sí paran.
if (SC.stepTimeout > 0 && tPaso >= SC.stepTimeout) {
  ST.stw.stepTimeout = true; fault(CFSM_ERR_STEP_TIMEOUT); active = false;
}
else if (!enReposo && SC.cycleTimeout > 0 && tCiclo >= SC.cycleTimeout) {
  fault(CFSM_ERR_CYCLE_TIMEOUT); active = false;
}
```

`_stepWarnFired` garantiza que `onStepWarning()` se llama **una sola vez** por
paso, no en cada scan. Se rearma en `setStep()`.

El `else if` de 5c no es casual: si un paso agota su tiempo, la causa que
interesa es el paso, no el ciclo. Se reporta la más específica.

#### Fase 6 — Palabra de estado

`syncStatusWord()` refleja el estado interno en la STW. `warning` se recalcula
entero cada scan como la unión de los avisos concretos, para que no se quede
enganchado cuando su causa desaparece.

#### Fase 7 — Indicador de primer scan

```cpp
if (retenido) return false;

if (active) {
  if (_firstScanSeen) _firstScan = false;
  else                _firstScanSeen = true;
}
return active;
```

`_firstScan` tiene que seguir en pie **después** de que el bloque hijo haya
ejecutado la lógica de su paso, porque es ahí donde se consulta con
`isFirstScanInStep()`. Por eso no se apaga aquí sin más: se anota que ya ha
corrido un scan de lógica, y se apaga en el siguiente.

### 2.6 `setStep()` y el bug que encontró el banco

```cpp
void setStep(uint16_t newStep, cfsm_time_t warnMs, cfsm_time_t faultMs) {
  endFreeze();                                    // <-- ESTA LÍNEA

  if (SC.step == SC.initialStep && newStep != SC.initialStep) {
    SC.cycleStartTime = cfsm_millis();            // el ciclo empieza al salir del reposo
  }
  if (SC.step != newStep) onStepExited(SC.step);
  SC.lastStep = SC.step;
  SC.step = newStep;
  SC.stepStartTime = cfsm_millis();
  SC.stepWarnTime = warnMs;
  SC.stepTimeout = faultMs;
  ST.stw.stepTimeout = false;
  ST.stw.stepWarn = false;
  _stepWarnFired = false;
  ST.stw.done = false;
  _stepAuthorised = false;
  _firstScan = true;
  _firstScanSeen = false;
  onStepEntered(newStep);
}
```

`endFreeze()` es la línea que más costó llegar a escribir, y merece los números.

Un cambio de paso puede venir **desde dentro de una espera**: llega la pieza y
la secuencia avanza. Ahí la congelación sigue abierta. Sin `endFreeze()`:

```
t=0        entra en la espera, _freezeStart = 0
t=600000   llega la pieza. setStep() pone SC.stepStartTime = 600000
t=600001   updateSequence() ve que ya no está congelado y llama a
           closeFreezeChunk(), que hace:
              SC.stepStartTime += cfsm_elapsed(600000... no, += 600001)
              SC.stepStartTime = 600000 + 600001 = 1 200 001

           getTimeInStep() = 600001 - 1200001 = -600000
                           = 4 294 367 296  en uint32_t
```

El cronómetro se va al futuro por desbordamiento sin signo, el paso parece
llevar cuarenta y nueve días, y todos los watchdogs saltan a la vez. Con
`endFreeze()` primero, el tramo se cierra y se contabiliza **antes** de poner la
marca nueva, y el resultado es 0 como debe ser.

La versión de dos argumentos delega:

```cpp
void setStep(uint16_t newStep, cfsm_time_t timeoutMs = 0) {
  setStep(newStep, 0, timeoutMs);
}
```

Así `setStep(PASO)` y `setStep(PASO, 5000)` siguen significando exactamente lo
que significaban en la 2.0.

### 2.7 Las esperas declaradas

```cpp
bool suspendWhile(bool condition) { return waitWhile(condition, STATE_SUSPENDED); }
bool holdWhile(bool condition)    { return waitWhile(condition, STATE_HELD); }

bool waitWhile(bool condition, SystemState waitState) {
  if (condition) {
    _waitRequested = true;
    if (_currentState == STATE_RUNNING) transitionTo(waitState);
    return isWaiting();
  }
  if (_currentState == waitState) transitionTo(STATE_RUNNING);
  return false;
}
```

Devuelven `true` **mientras** hay que esperar. El patrón es siempre el mismo:

```cpp
case PASO_ESPERAR_PIEZA:
  cinta = false;
  if (suspendWhile(!piezaPresente)) break;   // sigue esperando
  setStep(PASO_COGER, 3000);                 // ya hay pieza
  break;
```

Cuál usar: `suspendWhile()` cuando la causa es **externa** (no llega pieza, la
estación siguiente está llena, el almacén está vacío) — la máquina está sana y
arrancará sola. `holdWhile()` cuando la causa es **interna** o del operario
(recarga de material, control de calidad, esperar a que alguien pulse marcha).

### 2.8 `completeCycle()`

```cpp
void completeCycle(uint16_t nextStep) {
  SC.cycleCount++;
  SC.lastCycleTimeMs = getCycleTime();       // solo productivo
  SC.lastBlockedTime = getBlockedTime();     // y aparte, lo esperado
  SC.cycleStartTime  = cfsm_millis();
  SC.blockedTime     = 0;
  if (_frozen) _freezeStart = cfsm_millis();
  _cycleWarn = false;

  if (ST.cfgw.stop) { ST.cfgw.stop = false; stop(); }
  else              { setStep(nextStep); }

  ST.stw.done = true;                        // DESPUÉS del cambio de paso
}
```

Dos detalles:

- El bit `done` se levanta **después** del cambio de paso, porque `setStep()` lo
  borra. Así queda a 1 mientras la máquina descansa en su paso inicial y cae en
  cuanto vuelve a trabajar. El maestro de línea puede distinguir "ciclo recién
  terminado" de "ciclo en curso".
- Si había una petición de paro pendiente, se atiende **aquí**. Es lo que hace
  que una parada ordenada deje la máquina parada justo al terminar la pieza en
  curso, nunca a mitad.

La sobrecarga sin argumentos llama a `completeCycle(SC.initialStep)`. La
variante con `nextStep` sirve a intérpretes como `RecipeExecutor`: cambia de
paso antes de levantar `done` y evita el patrón incorrecto
`completeCycle(); setStep(x)`.

### 2.9 `processControlWord()`

Traduce los bits de CFGW a comandos y los consume. Ver
[ControlWords.md](ControlWords.md) §2.3 para el detalle de qué se consume y por
qué, incluido el flanco de bajada de `holdRequest` —el defecto por el que "la
pausa no pausaba"— y el `quickStop` activo a nivel bajo.

Perder `enable` ordena `stop()` también si el bloque está pausado o esperando
una condición externa, además de durante arranque o marcha.

## 3. API completa

### Consulta

| Método | Devuelve |
|---|---|
| `getStep()` / `getLastStep()` | Paso actual / anterior |
| `getTimeInStep()` | ms en el paso, descontando congelaciones |
| `getCycleTime()` | ms **productivos** del ciclo en curso |
| `getBlockedTime()` | ms esperando en este ciclo |
| `getTotalCycleTime()` | Productivo + espera: el reloj de pared |
| `getLastCycleTime()` / `getLastBlockedTime()` | Lo mismo, del ciclo anterior |
| `getCycleCount()` | Piezas completadas |
| `getErrorCode()` | `ST.errorCode` (redefine el de `FsmBlock`) |
| `isFirstScanInStep()` | ¿Primer scan dentro de este paso? |
| `isOverTakt()` | ¿Se pasó del takt objetivo? |

### Configuración

| Método | Qué hace |
|---|---|
| `setInitialStep(paso)` | Paso al que vuelven arranque y rearme |
| `setCycleTimeout(ms)` | Límite duro del ciclo productivo → **alarma** |
| `setCycleTarget(ms)` | Takt objetivo → **aviso** |

### Motor y pasos

| Método | Qué hace |
|---|---|
| `updateSequence()` | El motor. Primera línea del `update()` |
| `setStep(paso, faultMs = 0)` | Cambio de paso con un escalón |
| `setStep(paso, warnMs, faultMs)` | Con los dos escalones |
| `restartStep()` | Repite el paso desde cero conservando sus tiempos |
| `isStepTimedOut()` | ¿Venció el límite duro del paso? |
| `completeCycle()` / `completeCycle(nextStep)` | Cierra el ciclo y elige el paso del siguiente |
| `suspendWhile(cond)` / `holdWhile(cond)` | Esperas declaradas |

### Hooks

| Hook | Cuándo |
|---|---|
| `onStepEntered(paso)` | Una vez, al entrar |
| `onStepExited(paso)` | Una vez, al salir |
| `onStepWarning(paso)` | Una vez, al pasar del tiempo de aviso |
| `stepName(paso)` | Nombre legible para la telemetría |

Además hereda todo `FsmBlock` y expone `SC`, `ST` y `handshake` en público.

## 4. Ejemplos

### 4.1 La secuencia mínima que ya está bien hecha

```cpp
enum Pasos : uint16_t { REPOSO = 0, BAJAR = 10, PRENSAR = 20, SUBIR = 30 };

class Prensa : public SequenceBlock {
 public:
  bool marcha = false, abajo = false, arriba = true;
  bool motorBajar = false, motorSubir = false, prensa = false;

  void begin() override {
    setName(F("PRENSA"));
    setInitialStep(REPOSO);
    setStep(REPOSO);
    setCycleTimeout(20000);
    setCycleTarget(8000);
  }

  void update() override {
    if (!updateSequence()) { motorBajar = motorSubir = prensa = false; return; }

    switch (_currentStep) {
      case REPOSO:
        motorBajar = motorSubir = prensa = false;
        if (holdWhile(!marcha)) break;          // espera al operario, sin prisa
        setStep(BAJAR, 2000, 4000);             // aviso a 2 s, alarma a 4 s
        break;

      case BAJAR:
        motorBajar = true;
        if (abajo) { motorBajar = false; setStep(PRENSAR); }
        break;

      case PRENSAR:
        prensa = true;
        if (getTimeInStep() >= 1500) { prensa = false; setStep(SUBIR, 2000, 4000); }
        break;

      case SUBIR:
        motorSubir = true;
        if (arriba) { motorSubir = false; completeCycle(); }
        break;
    }
  }

  const __FlashStringHelper* stepName(uint16_t s) const override {
    switch (s) {
      case REPOSO:  return F("REPOSO");
      case BAJAR:   return F("BAJANDO");
      case PRENSAR: return F("PRENSANDO");
      case SUBIR:   return F("SUBIENDO");
      default:      return nullptr;
    }
  }
};
```

Fíjate en el reparto de vigilancias: los pasos que dependen de un **movimiento
mecánico** llevan tiempo (`BAJAR`, `SUBIR`); el que depende de un **temporizador
propio** no lo necesita; y el que depende de **una persona** usa `holdWhile()`.

### 4.2 Ver venir la avería antes de que pare la máquina

```cpp
protected:
  void onStepWarning(uint16_t paso) override {
    if (paso == BAJAR) {
      Serial.print(F("[PRENSA] AVISO: el carro tarda "));
      Serial.print(getTimeInStep());
      Serial.println(F(" ms en bajar. Revisar el cilindro."));
      avisosBajada++;
    }
  }
```

Con `setStep(BAJAR, 2000, 4000)`, un cilindro que empieza a perder aire tarda
2,1 s en vez de 1,8 y **te avisa**. Antes te enterabas el día que llegaba a 4 s
y la máquina se paraba en medio de un turno.

### 4.3 Los dos relojes al cerrar cada pieza

```cpp
if (prensa.getCycleCount() != ultimoContado) {
  ultimoContado = prensa.getCycleCount();
  Serial.print(F("Pieza ")); Serial.print(ultimoContado);
  Serial.print(F(": trabajo=")); Serial.print(prensa.getLastCycleTime());
  Serial.print(F("ms  espera=")); Serial.print(prensa.getLastBlockedTime());
  Serial.print(F("ms  cadencia="));
  Serial.println(prensa.getLastCycleTime() + prensa.getLastBlockedTime());
}
```

```
Pieza 41: trabajo=6120ms  espera=13480ms  cadencia=19600ms
```

Ese renglón dice que la prensa tarda 6 s en hacer su trabajo y pasa 13 s
esperando al operario. La máquina no es el cuello de botella: lo es la carga
manual. Ese es el dato del que sale la disponibilidad de un OEE, y antes de la
2.1 no existía porque los dos tiempos iban al mismo saco.

### 4.4 Una espera externa a mitad de ciclo

```cpp
case ESPERAR_BOTE:
  cinta = false;
  if (suspendWhile(!boteEnPosicion)) break;
  setStep(LLENAR, 2500, 4000);
  break;
```

Diez minutos sin bote: la baliza en ámbar fijo, `stw.suspended` a 1, cero
alarmas, el reloj de ciclo congelado y los diez minutos contados en
`getBlockedTime()`. Está verificado en la prueba P5 del banco.

### 4.5 Mando por palabra en vez de por llamada

```cpp
void loop() {
  uint16_t recibida = leerRegistroModbus(40001);
  estacion.ST.cfgw.raw = recibida;      // el maestro gobierna la estación entera

  manager.updateAll();

  escribirRegistroModbus(40002, estacion.ST.stw.raw);
  escribirRegistroModbus(40003, estacion.ST.errorCode);
}
```

### 4.6 Usar el primer scan de un paso

```cpp
case DOSIFICAR:
  if (isFirstScanInStep()) contadorInicial = pulsosCaudalimetro;
  valvula = (pulsosCaudalimetro - contadorInicial) < pulsosObjetivo;
  if (!valvula) setStep(EXPULSAR);
  break;
```

Una acción puntual al entrar en un paso, sin tener que sobrescribir
`onStepEntered()` para algo tan pequeño.

## 5. Decisiones de diseño

**`SC` y `ST` son públicas.** Se pueden romper desde fuera, sí. Pero son la
interfaz de bus y de HMI de la estación, y esconderlas detrás de treinta getters
y setters habría convertido un modelo que un automatista lee de un vistazo en un
objeto de biblioteca. Se documenta cuál se puede tocar y cuál no.

**Se unificó `SeqCtlBlock` dentro de `SequenceBlock`.** El planteamiento inicial
tenía dos jerarquías paralelas: una para la secuencia y otra para las palabras
SC/ST. Mantenerlas separadas obligaba a herencia múltiple o a duplicar el motor.
Se fusionaron.

**Los cronómetros guardan el origen, nunca el vencimiento.** Es lo que los hace
inmunes al desbordamiento de `millis()` a los 49,7 días. Ver
[CoreFSM_Platform.md](CoreFSM_Platform.md) §2.3.

**El reloj de ciclo arranca al abandonar el paso inicial, no en `start()`.**
Automático, sin declarar nada, y arregla el falso positivo de la 2.0 para todo
el código que ya existía.

**Las esperas hay que declararlas; no se infieren.** Se valoró la regla "un paso
sin `stepTimeout` no cuenta para el ciclo" y se descartó: habría dejado sin
protección de ciclo a quien no pone tiempos de paso, sin avisar. Declarar es más
verboso y mucho más honesto.

## 6. Errores frecuentes

**Poner `Serial.println()` dentro del `switch`.** Se ejecuta miles de veces por
segundo, satura el buffer de transmisión y acaba bloqueando la CPU. Va en
`onStepEntered()`. Es el error que más veces se comete al empezar.

**Olvidar las salidas seguras en el `if (!updateSequence())`.** Sin ellas, una
alarma no apaga el actuador: solo deja de cambiar de paso. La máquina se queda
en fallo con el cilindro empujando.

**Confundir nivel y flanco al alimentar el bloque.** `hasRisen()` pasa el
instante de pulsar; `isTriggered()` pasa el hecho de estar activo. Órdenes con
flanco, estados con nivel. Si se confunden, mantener el dedo en el botón relanza
el ciclo miles de veces por segundo.

**Llamar a `setStep()` después de `completeCycle()`.** `completeCycle()` ya
vuelve al paso inicial, y el `setStep()` posterior borra el bit `done` que
acababa de levantar.

**Poner tiempo de paso a una espera humana.** Un paso que espera a una persona
no debe tener `faultMs`: usa `holdWhile()`.

**Tocar `SC.stepStartTime` o `SC.cycleStartTime` a mano sin llamar antes a
`endFreeze()`.** Es privado precisamente porque hacerlo mal manda el cronómetro
al futuro (ver §2.6). Si crees que lo necesitas, casi seguro querías
`restartStep()` o una espera declarada.

## 7. Coste

Por instancia, en AVR, además de lo que aporta `FsmBlock` (~18 B):

| Bloque | Bytes |
|---|---:|
| `SC` | 46 |
| `ST` | 6 |
| `_currentStep` (referencia) | 2 |
| `_freezeStart` | 4 |
| Nueve banderas (`_frozen`, `_freezeWasWait`, `_waitRequested`, `_stepWarnFired`, `_cycleWarn`, `_stepAuthorised`, `_firstScan`, `_firstScanSeen`, `_lastHoldReq`) | 9 |
| `handshake` | lo que ocupe `Handshake` |

**Unos 67 bytes propios**, más el handshake, más los ~18 de `FsmBlock`. En un
Nano con 2 KB, tres bloques de secuencia se van a algo más de 250 bytes: es la
partida más gorda de RAM de la librería y por eso las estructuras están
apretadas.

En flash, `updateSequence()` se inserta en el `update()` de cada bloque hijo. Es
la razón de que un programa con tres bloques ocupe notablemente más que uno con
uno solo.

## 8. Relación con el resto

```
        FsmBlock.h              estados, transiciones, fault/reset
             │
             ▼
      SequenceBlock.h           ← estás aquí
       ├── SC / ST              ControlWords.h
       ├── handshake            Handshake.h
       ├── cronómetros          CoreFSM_Platform.h (cfsm_elapsed)
       │
       ├──▶ StepTracer          lee getStep(), getState(), stepName()
       ├──▶ BlockManager        lo registra y lo actualiza en OB1
       └──▶ RecipeExecutor      hereda de él: una receta es una secuencia

  Y no depende de io/ en absoluto: un SequenceBlock no sabe qué es un pin.
  Quien une las variables lógicas con la planta es tu .ino.
```
