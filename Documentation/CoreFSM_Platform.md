# CoreFSM_Platform.h

> El único archivo de toda la librería que sabe sobre qué microcontrolador está corriendo.

**Ruta:** `src/core/CoreFSM_Platform.h`
**Incluye:** `<Arduino.h>`, `<stdint.h>`, `<stddef.h>`, `<string.h>`
**Lo usan:** absolutamente todos. Es el primer `#include` de `CoreFSM.h`.

---

## 1. Qué problema resuelve

Un Arduino Nano tiene 2 KB de RAM y una EEPROM real de 1 KB grabada en silicio.
Un ESP32 tiene cientos de KB de RAM y **no tiene EEPROM física**: la emula sobre
un sector de flash, y para que un cambio se haga permanente hay que llamar
explícitamente a `commit()`. Si esa diferencia estuviera repartida por los
veinte archivos que guardan algo, cada uno tendría su `#if defined(ESP32)` y
añadir una placa nueva sería una cacería.

Aquí se aísla. El resto de la librería no pregunta nunca en qué placa está: usa
las macros que salen de este archivo. Añadir soporte para un micro nuevo es
tocar este archivo y solo este archivo.

## 2. Cómo funciona por dentro

### 2.1 Identificación de la familia

Una cadena de `#elif` sobre las macros que define el propio core de Arduino:

| Condición | Define | `CFSM_IS_CONSTRAINED` |
|---|---|---|
| `ARDUINO_ARCH_AVR` o `__AVR__` | `CFSM_ARCH_AVR` | **1** |
| `ESP32` o `ARDUINO_ARCH_ESP32` | `CFSM_ARCH_ESP32` | 0 |
| `ESP8266` o `ARDUINO_ARCH_ESP8266` | `CFSM_ARCH_ESP8266` | 0 |
| `ARDUINO_ARCH_RP2040` | `CFSM_ARCH_RP2040` | 0 |
| `ARDUINO_ARCH_SAMD` | `CFSM_ARCH_SAMD` | 0 |
| ninguna | `CFSM_ARCH_UNKNOWN` | 0 |

`CFSM_IS_CONSTRAINED` vale 1 solo en AVR y significa *la RAM es tan escasa que
el diseño estático es obligatorio*. No es decorativo: es la bandera que
justifica que no haya un solo `new` ni un solo `String` dinámico en la librería.

`CFSM_ARCH_UNKNOWN` es la rama que hace que el banco de pruebas del PC compile.
Un `g++` de escritorio no define ninguna de esas macros, cae aquí, y la librería
se comporta como una plataforma genérica sin memoria no volátil.

### 2.2 La capa de memoria no volátil

Dos macros describen el mundo entero:

```c
CFSM_HAS_NVM            // ¿existe algo que sobreviva al corte de tensión?
CFSM_NVM_NEEDS_COMMIT   // ¿hay que llamar a commit() para consolidar?
```

| Plataforma | `HAS_NVM` | `NEEDS_COMMIT` | `NVM_SIZE_DEFAULT` |
|---|---|---|---|
| `CFSM_DISABLE_NVM` definido | 0 | 0 | — |
| AVR | 1 | **0** — EEPROM real, la escritura es inmediata | 512 |
| ESP32 / ESP8266 / RP2040 | 1 | **1** — EEPROM emulada sobre flash | 1024 |
| resto | 0 | 0 | — |

`CFSM_DISABLE_NVM` va primero en la cadena a propósito: definiéndolo antes de
incluir la librería se apaga la persistencia entera. Sirve para dos cosas
distintas —probar en un PC sin EEPROM, y ahorrar flash en una máquina que no usa
recetas ni configuración— y por eso está en el sitio donde gana a todo lo demás.
Es la macro que usa el banco de pruebas: `-DCFSM_DISABLE_NVM`.

### 2.3 El reloj, y la resta que sobrevive al desbordamiento

Esta es la parte que más se malinterpreta de toda la librería.

```c
typedef uint32_t cfsm_time_t;

static inline cfsm_time_t cfsm_millis() { return (cfsm_time_t)millis(); }

static inline cfsm_time_t cfsm_elapsed(cfsm_time_t since) {
  return (cfsm_time_t)(cfsm_millis() - since);
}
```

`millis()` es un contador de 32 bits sin signo de milisegundos. Se desborda a
los 2³² ms, que son **49 días, 17 horas y pico**. Una máquina de producción está
encendida mucho más que eso, así que el desbordamiento no es un caso teórico:
va a pasar, y va a pasar de madrugada.

La forma ingenua de medir un plazo es la que falla:

```c
if (millis() > marca + timeout) { ... }   // MAL
```

Si `marca + timeout` se desborda, la suma da un número pequeño, `millis()` es
grande, y la condición se cumple **de inmediato**. Un temporizador de 5 segundos
vence al instante y la máquina se vuelve loca durante un rato.

La resta sí funciona, y funciona por aritmética modular: en `uint32_t` toda
operación es módulo 2³². Con números:

```
marca  = 4 294 967 290    (0xFFFFFFFA, faltan 6 ms para el desbordamiento)
ahora  =            10    (ya se desbordó y lleva 10 ms)

ahora - marca = 10 - 4294967290 = -4294967280
                mod 2³²         =            16    <- correcto: 16 ms
```

El resultado es exacto **siempre que el tiempo transcurrido real sea menor que
2³² ms**, es decir, mientras no midas plazos de más de 49 días. Ninguna
vigilancia de esta librería lo hace.

Por eso todos los cronómetros de CoreFSM guardan una **marca de origen** y
restan, nunca guardan un instante de vencimiento. `SC.stepStartTime`,
`SC.cycleStartTime`, `_freezeStart`, el `_t0` del `ScanWatchdog`: todos siguen
el mismo patrón.

### 2.4 El gancho de reloj propio

```c
#if defined(CFSM_CUSTOM_CLOCK)
  extern cfsm_time_t cfsm_millis();   // lo aportas tú
#else
  static inline cfsm_time_t cfsm_millis() { return (cfsm_time_t)millis(); }
#endif
```

Definiendo `CFSM_CUSTOM_CLOCK` te haces cargo del reloj. Es lo que permite
adelantarlo a voluntad en un banco de pruebas, o engancharlo a un RTC.

### 2.5 Cadenas en memoria de programa

```c
#define CFSM_FSTR(s)   F(s)
```

En AVR, una cadena literal se copia a RAM al arrancar salvo que se marque para
quedarse en flash. Con 2 KB de RAM, veinte mensajes de diagnóstico se comen un
cuarto de la memoria sin hacer nada. `CFSM_FSTR` envuelve la `F()` de Arduino y
existe como macro propia para poder redefinirla en plataformas donde `F()` no
tiene sentido.

Lo acompañan `CFSM_PROGMEM` y `CFSM_READ_PTR(addr)`, que en AVR se resuelven a
`PROGMEM` y a un `pgm_read_word`, y fuera de AVR a nada y a un desreferenciado
normal. Es lo que permite escribir una tabla de punteros a texto una sola vez y
que funcione en las dos familias.

### 2.6 Utilidades

```c
#define CFSM_UNUSED(x)      (void)(x)
#define CFSM_ARRAY_LEN(a)   (sizeof(a) / sizeof((a)[0]))
```

`CFSM_UNUSED` está en todos los hooks virtuales vacíos. No es cosmética: la
librería compila con `-Wall -Wextra` y un parámetro sin usar es un aviso. Cero
avisos es una política, no una casualidad — el día que un aviso significa algo,
quieres que se vea entre los demás.

### 2.7 Versión

```c
#define CFSM_VERSION_MAJOR  2
#define CFSM_VERSION_MINOR  1
#define CFSM_VERSION_PATCH  0
#define CFSM_VERSION_STR    "2.1.0"
```

## 3. API completa

| Símbolo | Tipo | Qué es |
|---|---|---|
| `CFSM_ARCH_AVR` / `_ESP32` / `_ESP8266` / `_RP2040` / `_SAMD` / `_UNKNOWN` | macro | Solo una está definida |
| `CFSM_ARCH_NAME` | cadena | `"AVR"`, `"ESP32"`, … |
| `CFSM_IS_CONSTRAINED` | 0/1 | 1 solo en AVR |
| `CFSM_HAS_NVM` | 0/1 | Hay memoria no volátil |
| `CFSM_NVM_NEEDS_COMMIT` | 0/1 | Hace falta `commit()` |
| `CFSM_NVM_SIZE_DEFAULT` | entero | Bytes reservados por defecto |
| `cfsm_time_t` | `uint32_t` | El tipo de todos los tiempos en ms |
| `cfsm_millis()` | función | El reloj |
| `cfsm_elapsed(desde)` | función | Tiempo desde una marca, a prueba de desbordamiento |
| `CFSM_FSTR(s)` | macro | Cadena en flash |
| `CFSM_PROGMEM`, `CFSM_READ_PTR(a)` | macro | Tablas en flash |
| `CFSM_UNUSED(x)`, `CFSM_ARRAY_LEN(a)` | macro | Utilidades |
| `CFSM_VERSION_*` | macro | Versión |

## 4. Ejemplos

### 4.1 Un temporizador propio hecho bien

```cpp
class Purga {
  cfsm_time_t _inicio = 0;
  bool _activa = false;
 public:
  void arrancar()      { _inicio = cfsm_millis(); _activa = true; }
  bool haTerminado(cfsm_time_t duracion) const {
    return _activa && cfsm_elapsed(_inicio) >= duracion;
  }
};
```

Guarda el origen y resta. Sobrevive al día 49 sin enterarse.

### 4.2 Código que se adapta a la placa sin `#ifdef` repartido

```cpp
void guardarReceta() {
  escribirBytes();
#if CFSM_NVM_NEEDS_COMMIT
  EEPROM.commit();     // solo se compila en ESP32, ESP8266 y RP2040
#endif
}
```

Fíjate en que la condición no nombra ninguna placa. El día que aparezca un micro
nuevo con flash emulada, basta con clasificarlo arriba.

### 4.3 Compilar la lógica en el PC

```bash
g++ -std=gnu++11 -DCFSM_DISABLE_NVM -I src -I tests/stub mi_prueba.cpp
```

Cae en `CFSM_ARCH_UNKNOWN`, la persistencia desaparece, y la máquina de estados
se puede ejercitar sin placa. Es exactamente lo que hace `tests/ejecutar.sh`.

### 4.4 Adelantar el reloj para simular horas

```cpp
#define CFSM_CUSTOM_CLOCK
#include <CoreFSM.h>

cfsm_time_t g_ms = 0;
cfsm_time_t cfsm_millis() { return g_ms; }

// ... y en la prueba:
for (g_ms = 0; g_ms < 600000UL; g_ms++) bloque.update();   // 10 minutos
```

## 5. Decisiones de diseño

**`cfsm_time_t` es `uint32_t` en todas las plataformas, no `unsigned long`.**
`unsigned long` mide 32 bits en AVR pero 32 o 64 según el compilador en otras;
fijar el tipo hace que las estructuras que se guardan en memoria no volátil
tengan el mismo tamaño en todas partes, y que una configuración escrita por un
ESP32 no se lea mal en un AVR.

**Se detectan las macros del core, no las del compilador.** `__AVR__` la define
el compilador, pero `ARDUINO_ARCH_AVR` la define el core de Arduino. Se
comprueban las dos porque hay entornos que solo ponen una.

**`CFSM_DISABLE_NVM` va antes que la detección de plataforma en la cadena.** Una
opción del usuario tiene que ganar a la detección automática; si estuviera
después, definirlo en un AVR no tendría efecto.

## 6. Errores frecuentes

**Comparar sumando en vez de restando.** Ya explicado en 2.3. Si ves un
`millis() > algo + plazo` en tu código, es un fallo latente con fecha.

**Guardar tiempos en `int` o `unsigned int`.** En AVR son 16 bits: se desbordan
a los 65 segundos. Usa siempre `cfsm_time_t`.

**Llamar a `EEPROM.commit()` incondicionalmente.** En AVR no existe y no compila.
Envuélvelo en `#if CFSM_NVM_NEEDS_COMMIT`.

**Olvidar `CFSM_FSTR` en un mensaje largo.** No da error: te come la RAM en
silencio y la máquina empieza a comportarse raro cuando la pila choca con el
montón. En un Nano, veinte mensajes de 40 caracteres son 800 bytes, el 40 % de
toda la memoria.

## 7. Coste

Cero RAM y cero flash: es un archivo de macros, `typedef` y funciones `inline`.
Lo que ahorra es otra cosa — cada `CFSM_FSTR` mantiene su cadena fuera de la RAM.

## 8. Relación con el resto

```
                    CoreFSM_Platform.h
                            │
         ┌──────────────────┼──────────────────┐
         │                  │                  │
   cfsm_millis()      CFSM_HAS_NVM        CFSM_FSTR
   cfsm_elapsed()     NVM_NEEDS_COMMIT    CFSM_PROGMEM
         │                  │                  │
   todos los          ConfigStore.h        Logger.h
   cronómetros        DataBlock.h          Telemetry.h
   (SequenceBlock,    RecipeExecutor.h     ControlWords.h
   Timers, Counters,                       (textos de error)
   DigitalSensor,
   ScanWatchdog...)
```

Es el único archivo del que dependen todos los demás, y el único que no depende
de ninguno de la librería.
