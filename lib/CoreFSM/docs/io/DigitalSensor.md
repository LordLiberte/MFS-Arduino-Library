# DigitalSensor.h

> Un pulsador o un final de carrera, con antirrebote, flancos, forzado y contador de maniobras. El objeto de campo que más se usa y donde más errores se cometen.

**Ruta:** `src/io/DigitalSensor.h`
**Incluye:** `IDevice.h`
**Lo usan:** `IOTable.h` (lo genera para cada fila `CFSM_TABLE_DI`), y tu `.ino` si montas la E/S a mano.

---

## 1. Qué problema resuelve

Un `digitalRead()` crudo tiene tres problemas que no se ven hasta que la máquina
está montada:

1. **Rebota.** Un pulsador mecánico no cambia de estado limpiamente: genera
   decenas de transiciones en los primeros milisegundos. Si esa señal dispara un
   ciclo, disparas decenas de ciclos.
2. **No distingue nivel de flanco.** "El botón está pulsado" y "acaban de pulsar
   el botón" son cosas distintas, y confundirlas es el error número uno.
3. **No se puede forzar.** En una puesta en marcha necesitas mentirle a la
   máquina para probar la secuencia sin el armario cableado.

`DigitalSensor` resuelve los tres por 16 bytes.

## 2. Cómo funciona por dentro

### 2.1 Por qué `activeLow` vale `true` por defecto

El cableado industrial estándar de una entrada digital es: contacto entre el pin
y masa, con resistencia de pull-up. Es decir, **contacto CERRADO = pin en LOW**,
eléctricamente al revés de lo intuitivo.

```cpp
bool readPhysical() const {
  return (digitalRead(_pin) == (_activeLow ? LOW : HIGH));
}
```

El parámetro le da la vuelta, de modo que `isTriggered()` devuelve `true` cuando
el sensor está activado, sin que tengas que pensar en tensiones.

Y no es una comodidad, es seguridad. **Si el cable se corta o se suelta un
borne**, el pull-up lleva el pin a nivel alto, la señal se lee como "sensor no
activado" y la máquina se para. Con lógica directa, un cable cortado se leería
como "sensor activado" y la máquina seguiría como si nada.

En seguridad de máquinas esto se llama **principio de corriente de reposo** y no
es opcional. Es el mismo criterio que hace que `quickStop` sea activo a nivel
bajo en las palabras de mando.

### 2.2 El antirrebote, muestra a muestra

```cpp
void readInputs() override {
  bool sample = _forced ? _simValue : readPhysical();
  if (_invertLogic) sample = !sample;

  _rising = _falling = false;

  /* Cada cambio en la señal cruda reinicia el reloj de estabilidad. */
  if (sample != _lastRaw) {
    _lastRaw        = sample;
    _lastChangeTime = cfsm_millis();
  }

  /* Solo cuando lleva quieta el tiempo exigido se da por buena. */
  if (cfsm_elapsed(_lastChangeTime) >= _debounceMs) {
    if (sample != _state) {
      _state   = sample;
      _rising  =  _state;
      _falling = !_state;
      _changeCount++;
    }
  }
  _raw = sample;
}
```

Es un filtro de **estabilidad**, no un temporizador de retardo. La diferencia
importa: no espera 20 ms y luego mira; exige que la señal lleve 20 ms **sin
cambiar**. Un rebote que dura 8 ms reinicia el reloj tantas veces como haga
falta, y el estado no se acepta hasta que la señal se calma de verdad.

Traducido a una línea de tiempo con `debounceMs = 20`:

```
crudo    ─┐ ┌┐ ┌─┐ ┌──────────────────────  (rebota 12 ms y se calma)
          └─┘└─┘ └─┘
reloj     0 3 5 8 11 ────────────── 31 ms
_state   ────────────────────────────┐      (cambia a los 31, no a los 20)
                                     └───
hasRisen()                           ▲ un solo scan
```

**El coste es latencia.** Un antirrebote de 20 ms retrasa 20 ms la detección. Por
eso los sensores electrónicos —inductivos, fotocélulas, finales de carrera de
estado sólido— van con 2-5 ms: no rebotan, y filtrarlos de más solo hace la
máquina más lenta. Un contacto mecánico, incluso uno de seguridad, puede
rebotar: el filtrado de una entrada de supervisión se elige con los datos del
fabricante y el tiempo de respuesta requerido. La parada física no debe
depender de este filtro ni del microcontrolador.

### 2.3 Los flancos duran un solo scan

`_rising` y `_falling` se ponen a `false` al principio de cada `readInputs()` y
solo se levantan en el scan en que el estado filtrado cambia. Eso significa que
`hasRisen()` devuelve `true` **exactamente una vez** por pulsación.

Esa es la propiedad que hace que se pueda escribir:

```cpp
proceso.ordenMarcha = HW.Pulsador_Marcha.hasRisen();
```

y que mantener el dedo en el botón **no** relance el ciclo. Con
`isTriggered()` en su lugar, lo relanzaría en cada scan: miles de veces por
segundo.

La regla, que conviene tatuarse: **órdenes con flanco, estados con nivel.** Una
orden de marcha es un flanco. Un final de carrera o un interbloqueo lógico
monitorizado es un nivel.

### 2.4 El flanco fantasma del arranque

```cpp
void begin() override {
  _pin.configure(_activeLow ? INPUT_PULLUP : INPUT);
  if (_pin.isNative()) synchronize(currentSample());
  else                 _initialized = false;
  _rising = _falling = false;
  _changeCount = 0;
}
```

La muestra inicial pasa por **las mismas transformaciones** que aplica
`readInputs()`: forzado e inversión lógica. Si se tomara el pin en crudo y el
sensor estuviera invertido con `setInverted(true)`, la primera lectura del scan
daría un valor distinto al inicial y, pasado el antirrebote, se generaría un
**flanco que nadie ha producido**.

En un backend agrupado la captura válida llega después de configurar todos sus
canales. Por eso `begin()` aplaza `synchronize()` hasta el primer
`readInputs()`; esa primera foto fija nivel y relojes sin producir flanco.

Si ese flanco alimenta un contador, la máquina cuenta una pieza fantasma. Si
alimenta una orden de marcha, la máquina **arranca sola en el `setup()`**. Fue
uno de los defectos que encontró la revisión adversarial.

### 2.5 El contador de maniobras

```cpp
uint32_t changeCount() const { return _changeCount; }
```

Se incrementa en cada cambio **filtrado**. Un sensor que conmuta miles de veces
sin que la máquina se mueva está suelto, sucio o averiado.

Es mantenimiento predictivo por cuatro bytes: comparar el contador de un final
de carrera con el contador de ciclos te dice si hace más maniobras de las que
debería. Un final de carrera que hace 3 maniobras por ciclo en vez de 1 está
vibrando.

### 2.6 `isStableFor()`

```cpp
cfsm_time_t timeInState() const { return cfsm_elapsed(_stateSince); }
bool isStableFor(cfsm_time_t ms) const { return timeInState() >= ms; }
```

Sirve para exigir que una condición se **mantenga**: "arranca solo si la barrera
lleva 2 s despejada". `_stateSince` cambia solo cuando se acepta un estado
filtrado nuevo; un rebote rechazado no borra la antigüedad del estado que la
lógica sigue viendo. `rawValue()` permite observar la última muestra, pero la
clase no expone un cronómetro separado de estabilidad eléctrica cruda.

### 2.7 `setInverted()`

```cpp
void setInverted(bool inv) { _invertLogic = inv; }
```

Invierte el significado lógico **sin tocar el cableado ni el `activeLow`**. Útil
cuando montan un contacto NC donde el plano decía NA y no se puede parar la
máquina para cambiarlo. Es distinto de `activeLow`, que describe la electrónica;
esto describe la semántica.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| constructor | `DigitalSensor(uint8_t pin, bool activeLow = true, uint16_t debounceMs = 20)` | |
| constructor backend | `DigitalSensor(IDigitalBackend&, uint8_t channel, bool activeLow = true, uint16_t debounceMs = 20)` | Canal agrupado |
| `isTriggered()` / `isClear()` | `bool` | **Nivel** filtrado |
| `hasRisen()` / `hasFallen()` | `bool` | **Flanco**, un solo scan |
| `rawValue()` | `bool` | Sin filtrar. Para diagnóstico |
| `timeInState()` | `cfsm_time_t` | ms desde el último cambio filtrado aceptado |
| `isStableFor(ms)` | `bool` | ¿Lleva estable ese tiempo? |
| `changeCount()` | `uint32_t` | Conmutaciones desde el arranque |
| `setDebounce(ms)` | | Cambia la ventana en caliente |
| `setInverted(bool)` | | Invierte el significado lógico |
| `force(bool)` | | Desconecta del pin e impone valor |
| `releaseForce()` / `isForced()` | | Heredados de `IDevice` |
| `pin()` / `usesNativePin()` | `uint8_t` / `bool` | Canal y tipo de origen |
| `describe(Print&)` | | `[Nombre]=1 *FORZADO*` |

## 4. Ejemplos

### 4.1 Los tres tipos de señal de una máquina

```cpp
DigitalSensor btnMarcha (2, true, 25);   // pulsador: rebota, filtra 25 ms
DigitalSensor fcTrabajo (3, true,  5);   // inductivo: no rebota, 5 ms basta
DigitalSensor setaEstado(4, true,  0);   // solo supervisión lógica; no es el circuito de parada

void loop() {
  io.readAllInputs();

  proceso.ordenMarcha = btnMarcha.hasRisen();      // ORDEN  -> flanco
  proceso.enTrabajo   = fcTrabajo.isTriggered();   // ESTADO -> nivel
  manager.setEmergencyStop(setaEstado.isTriggered()); // interbloqueo software
  ...
}
```

`setaEstado` solo refleja el contacto auxiliar en la lógica y el diagnóstico.
No debe abrir la energía peligrosa: esa función corresponde al circuito de
parada cableado. No fijes `debounceMs = 0` por una regla genérica; valida tanto
la latencia como el rebote del contacto concreto. Consulta
[`SAFETY.md`](../../../../SAFETY.md).

### 4.2 Enclavamiento que exige estabilidad

```cpp
case PASO_REPOSO:
  /* No arranca si la barrera acaba de despejarse: exige 2 s limpios.
   * Alguien que sale de la zona todavía tiene el brazo dentro. */
  if (holdWhile(!(btnMarcha.hasRisen() && barrera.isClear()
                  && barrera.isStableFor(2000)))) break;
  setStep(PASO_BAJAR, 2000, 4000);
  break;
```

### 4.3 Mantenimiento predictivo con el contador

```cpp
void informeSensores() {
  uint32_t ciclos = proceso.getCycleCount();
  if (ciclos < 100) return;                     // muestra insuficiente

  float porCiclo = (float)fcTrabajo.changeCount() / ciclos;
  Serial.print(F("FC_Trabajo: "));
  Serial.print(porCiclo, 2);
  Serial.println(F(" maniobras/ciclo"));
  if (porCiclo > 2.5f)
    Serial.println(F("  AVISO: deberia ser 2. El final de carrera vibra."));
}
```

Un final de carrera que debería hacer dos maniobras por ciclo (entrar y salir) y
hace cinco, está rebotando mecánicamente. Se detecta meses antes de que falle.

### 4.4 Probar la secuencia sin armario

```cpp
void setup() {
  ...
  HW.begin();
  HW.FC_Carro_Trabajo.force(false);
  HW.FC_Carro_Reposo.force(true);      // "el carro está en reposo"
}

void consola(char c) {
  if (c == 't') { HW.FC_Carro_Trabajo.force(true);  HW.FC_Carro_Reposo.force(false); }
  if (c == 'r') { HW.FC_Carro_Trabajo.force(false); HW.FC_Carro_Reposo.force(true);  }
  if (c == 'l') HW.releaseAllForces();
}
```

Con dos teclas se recorre la secuencia entera en la mesa. Y `printIoTable()`
recuerda en cada volcado que hay señales mintiendo.

### 4.5 El contacto que montaron al revés

```cpp
void setup() {
  HW.begin();
  /* El plano decía NA y el electricista montó un NC. No se para la línea
   * por esto: se invierte por software y se anota en el parte. */
  HW.Puerta_Cerrada.setInverted(true);
}
```

## 5. Decisiones de diseño

**El antirrebote es de estabilidad y no de retardo.** Un retardo simple
—"ignorar cambios durante 20 ms tras el primero"— es más barato pero deja pasar
el rebote que llega en el milisegundo 21. Exigir estabilidad cuesta un
`cfsm_elapsed` por scan y no deja pasar nada.

**Los flancos se calculan en la fase PAE, no bajo demanda.** Si `hasRisen()`
calculara el flanco al consultarse, dos consultas en el mismo scan darían
resultados distintos. Calculándolo una vez y guardándolo, `hasRisen()` es
consultable tantas veces como quieras dentro del mismo scan y siempre dice lo
mismo — que es lo que garantiza la coherencia de la imagen de proceso.

**`activeLow` y `setInverted()` son dos cosas separadas.** Podrían haber sido
una, pero describen niveles distintos: la primera es eléctrica y va en la tabla
de hardware; la segunda es semántica y suele ser un parche de obra. Mezclarlas
haría imposible saber si una inversión viene del cableado o de un apaño.

**El forzado se aplica antes de la inversión.** Así `force(true)` significa
siempre "el sensor está activado", independientemente de cómo esté cableado o
invertido. Forzar en unidades eléctricas sería una trampa.

## 6. Errores frecuentes

**Usar `isTriggered()` donde iba `hasRisen()`.** El síntoma es espectacular: la
máquina relanza el ciclo miles de veces por segundo mientras tengas el dedo
puesto. Ver §2.3.

**Usar `hasRisen()` donde iba `isTriggered()`.** El síntoma es el contrario y
más sutil: la condición se cumple un solo scan y si tu lógica no estaba mirando
justo en ese scan, se pierde. Los finales de carrera van con nivel.

**Usar esta entrada como única parada de emergencia.** Aunque el filtro sea
cero, sigue dependiendo del MCU y del firmware. Úsala para supervisión lógica;
la retirada de energía peligrosa debe ser independiente. Consulta
[SAFETY.md](../../../../SAFETY.md).

**Poner 20 ms a todo por costumbre.** En un inductivo son 20 ms de latencia
regalados. En un encoder o un sensor rápido, es perder cuentas.

**Leer un sensor fuera de la fase PAE.** Si llamas a `readInputs()` a mano en
medio de la lógica, recalculas los flancos y el que ya se consumió desaparece.

**Olvidar `begin()`.** El `pinMode` no se hace, la entrada flota y el sensor lee
ruido. Con `IOTable` lo llama `HW.begin()`; a mano, `io.beginAll()`.

**Dejar un forzado puesto.** El clásico del viernes por la tarde.

## 7. Coste

Por instancia, en AVR:

| Miembro | Bytes |
|---|---:|
| `_pin` (`DigitalPin`), `_activeLow` | depende del tamaño de puntero y alineación |
| `_debounceMs` | 2 |
| `_state`, `_raw`, `_lastRaw`, `_rising`, `_falling` | 5 |
| `_lastChangeTime`, `_stateSince` | 8 |
| `_changeCount` | 4 |
| `_simValue`, `_invertLogic` | 2 |
| `IDevice` (nombre, forzado, vtable) | 5 |

El total cambió al introducir `DigitalPin` y también incluye el flag de
inicialización. Mídelo con el compilador de la arquitectura objetivo; no
uses las cifras de versiones anteriores para dimensionar RAM. Si vas muy justo,
[`IOManager`](IOManager.md) reduce funciones y estado por señal.

## 8. Relación con el resto

```
   digitalRead(pin)
        │  fase PAE
        ▼
   DigitalSensor  ── readInputs() ──▶  _state (nivel)  ──▶ isTriggered()
        │                              _rising/_falling ──▶ hasRisen()
        │                              _changeCount     ──▶ mantenimiento
        │
        ├── es un IDevice ──▶ DeviceManager ──▶ HW.readInputs()
        │
        └── lo genera IOTable.h de cada fila CFSM_TABLE_DI

   Tu .ino conecta el nivel o el flanco con las variables del bloque.
   El bloque NUNCA ve el sensor.
```
