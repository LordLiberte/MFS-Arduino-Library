# ScanWatchdog.h

> Cronómetro del ciclo de scan, detector de excesos y activación opcional del watchdog hardware de AVR.

**Ruta:** [`src/diag/ScanWatchdog.h`](../../src/diag/ScanWatchdog.h)  
**Incluye:** [`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md); en AVR,
`<avr/wdt.h>`  
**Tipo público:** `ScanWatchdog`.

---

## 1. Propósito

`ScanWatchdog` mide el tramo de programa delimitado por `begin()` y `end()` en
microsegundos. Conserva la última duración, mínimo, máximo, una media exponencial
y estadísticas de scans que superan un límite configurable.

Tiene dos funciones distintas:

1. **Vigilancia software:** detecta un scan largo cuando finalmente se ejecuta
   `end()` y puede notificarlo mediante un callback.
2. **Watchdog hardware AVR opcional:** reinicia el microcontrolador si el código
   deja de llegar a `end()` durante aproximadamente dos segundos.

La primera función diagnostica lentitud; no puede recuperar un programa que ya
está bloqueado. La segunda puede reiniciarlo, pero un reset no garantiza por sí
solo que el proceso físico quede seguro.

Este “ciclo de scan” tampoco es el ciclo productivo de una pieza. Los límites de
paso/ciclo de [`SequenceBlock`](../core/SequenceBlock.md) supervisan tiempos del
proceso; esta clase supervisa tiempo de CPU entre dos marcas.

## 2. Modelo y semántica

### 2.1 Delimitación de la medida

`begin()` guarda `micros()` en `_t0`. `end()` calcula:

```cpp
uint32_t dt = micros() - _t0;
```

La resta sin signo tolera un desbordamiento de `micros()` siempre que el tramo
medido sea inferior a una vuelta completa del contador de 32 bits (unos 71,6
minutos si la unidad efectiva es un microsegundo).

Solo se mide lo que queda entre ambas llamadas. Código anterior a `begin()`,
posterior al primer `micros()` de `end()` o ejecutado cuando se omite `end()` no
aparece en `lastUs()`.

`end()` actualiza, en este orden conceptual:

- último, mínimo y máximo global;
- media móvil exponencial;
- número de scans;
- condición y estadísticas de exceso;
- callback de exceso, si procede;
- alimentación del watchdog hardware AVR, si está armado.

Un exceso ocurre solo si `limit > 0` y `dt > limit`; la igualdad exacta no cuenta.
`isOverrun()` representa el resultado del **último `end()`** y permanece así
hasta el siguiente. No queda enclavado; `overruns()` conserva el total histórico.

### 2.2 Media

La media usa un filtro exponencial de peso `1/8`:

```cpp
avg += (dt - avg) >> 3;
```

No es la media aritmética de todos los scans. Parte de cero, por lo que las
primeras lecturas están sesgadas hacia abajo y necesita varias muestras para
converger. A cambio, no requiere acumulador creciente y da más peso al
comportamiento reciente.

### 2.3 Límite y margen

El constructor y `setLimit()` reciben milisegundos (`uint16_t`) y los convierten
a microsegundos. El rango configurable es `0..65535` ms. Cero desactiva los
excesos y callbacks, pero las duraciones y estadísticas generales se siguen
actualizando.

`headroomPct()` usa el **máximo histórico**, no la última duración ni la media:

```text
(limite - maximo) / limite * 100
```

Devuelve cero si no hay límite o si el máximo ya lo alcanzó/superó. Antes del
primer scan, con un límite activo, devuelve 100.

### 2.4 Callback

`onOverrun(fn)` registra un puntero a función `void(uint32_t us)`. Se invoca en
cada `end()` excesivo y recibe la duración ya calculada.

La función corre síncronamente dentro de `end()`, pero **después** de capturar
`dt`; su propio coste no se suma a la duración reportada de ese scan. Sí retrasa
el resto del programa y, con watchdog hardware armado, retrasa su alimentación
porque `wdt_reset()` ocurre después del callback.

### 2.5 Watchdog hardware

Solo existe una implementación bajo `CFSM_ARCH_AVR`:

- `enableHardwareWatchdog()` configura `WDTO_2S`, un plazo fijo de 2 s;
- cada `end()` completado ejecuta `wdt_reset()`;
- `disableHardwareWatchdog()` lo desactiva;
- `hardwareWatchdogArmed()` refleja el booleano interno.

El límite software no cambia esos dos segundos. En ESP32, ESP8266, RP2040, SAMD
o arquitectura genérica, los métodos de activar/desactivar son no-op y
`hardwareWatchdogArmed()` permanece falso, aunque la plataforma tenga otro
periférico watchdog.

## 3. API

### 3.1 Construcción y ciclo

```cpp
explicit ScanWatchdog(uint16_t limitMs = 20);
void begin();
void end();
void setLimit(uint16_t limitMs);
void onOverrun(void (*fn)(uint32_t us));
```

No hay comprobación de llamadas emparejadas. Dos `begin()` seguidos sustituyen
la marca; un `end()` sin `begin()` mide desde el valor previo de `_t0` (cero al
construir).

### 3.2 Consultas

| Método | Resultado |
|---|---|
| `lastUs()` | duración del último tramo |
| `maxUs()` | máximo desde la construcción o último `resetStats()` |
| `minUs()` | mínimo; cero si aún no hay muestra desde el reset |
| `avgUs()` | media exponencial, truncada a microsegundos enteros |
| `scanCount()` | número de llamadas a `end()` contabilizadas |
| `overruns()` | número de duraciones estrictamente superiores al límite vigente |
| `worstOverrunUs()` | mayor duración entre los excesos; cero si no hubo |
| `isOverrun()` | resultado del último `end()` |
| `headroomPct()` | margen porcentual respecto al máximo histórico |

Cambiar el límite no reclasifica estadísticas anteriores ni reinicia el máximo.
Así, `headroomPct()` puede seguir reflejando un scan medido con otro límite.

### 3.3 Estadísticas y reporte

```cpp
void resetStats();
void report(Print& out) const;
```

`resetStats()` reinicia la marca, última duración, media, mínimo, máximo,
contadores, peor exceso y el booleano que devuelve `isOverrun()`. La siguiente
pareja `begin()`/`end()` inicia una campaña limpia.

`report()` emite última duración, media, máximo, límite en ms, excesos y número
de scans. No incluye mínimo, peor exceso ni margen.

### 3.4 Watchdog AVR

```cpp
void enableHardwareWatchdog();
void disableHardwareWatchdog();
bool hardwareWatchdogArmed() const;
```

## 4. Ejemplo mínimo

```cpp
#include <CoreFSM.h>

ScanWatchdog scan(20);                 // límite software: 20 ms
volatile bool avisoScanLargo = false;

void scanLargo(uint32_t us) {
  (void)us;
  avisoScanLargo = true;               // callback corto, sin Serial.print()
}

void setup() {
  Serial.begin(115200);
  scan.onOverrun(scanLargo);
}

void loop() {
  scan.begin();

  HW.readInputs();
  manager.updateAll();
  HW.writeOutputs();

  scan.end();

  if (avisoScanLargo) {
    avisoScanLargo = false;
    scan.report(Serial);                // fuera del tramo medido
  }
}
```

El ejemplo deja el watchdog hardware apagado deliberadamente. Si se activa en
AVR, debe hacerse solo después de validar bootloader, arranque seguro y método de
recuperación por ISP.

## 5. Coste

- Estado fijo, sin heap: nueve `uint32_t`, dos booleanos y un puntero a función,
  más la alineación del ABI (40 bytes típicos en AVR).
- `begin()` realiza una lectura de `micros()` y una asignación.
- `end()` es `O(1)`: otra lectura, comparaciones, actualizaciones de enteros y,
  solo si existe un exceso, una llamada indirecta opcional.
- `report()` realiza varias conversiones e impresiones y puede bloquear; no debe
  ejecutarse en cada scan.
- El `wdt_reset()` de AVR añade un coste pequeño y constante cuando está armado.

## 6. Errores frecuentes y limitaciones

- **Omitir `end()` al salir pronto del loop.** No se actualiza ninguna estadística
  ni se alimenta el watchdog hardware.
- **Medir solo `manager.updateAll()` creyendo medir todo el loop.** Coloca las
  marcas alrededor de entradas, lógica, salidas y cualquier otro trabajo que se
  quiera supervisar.
- **Esperar que el detector software rompa un bloqueo.** Sin `end()`, no hay
  detección ni callback; para recuperación hace falta watchdog hardware.
- **Hacer trabajo pesado en el callback.** No forma parte del `dt` ya capturado,
  puede saturar el puerto y puede impedir alimentar a tiempo el watchdog AVR.
- **Interpretar `avgUs()` como promedio total.** Es una media exponencial, no
  la suma de todas las muestras dividida por el contador.
- **Cambiar el límite sin borrar máximos.** El margen se calcula con historial
  anterior; llama a `resetStats()` si se necesita una campaña nueva.
- **Usar límites extremadamente grandes con `headroomPct()`.** En plataformas
  donde `unsigned long` tiene 32 bits, el producto intermedio por 100 puede
  desbordar para márgenes superiores a unos 42,9 millones de microsegundos. Los
  límites habituales de scan están muy por debajo, pero la API admite hasta
  65,535 s.
- **Asumir resolución de 1 µs.** `micros()` tiene resolución dependiente de la
  placa; en AVR clásico suele avanzar en saltos mayores.
- **Activar el watchdog AVR sin probar el bootloader.** Algunos bootloaders Nano
  antiguos pueden entrar en un bucle de reset tras un WDT y requerir ISP para
  recuperarse.
- **Confundir reset con estado seguro.** Tras un watchdog, pines, relés y proceso
  deben tener estados físicos seguros y una política de rearranque explícita.

## 7. Relación con otros módulos

- [`BlockManager`](../core/BlockManager.md) mide internamente solo la duración de
  `updateAll()`. `ScanWatchdog` puede envolver el scan completo y añade media,
  límite, callback y watchdog AVR.
- [`SequenceBlock`](../core/SequenceBlock.md) vigila tiempos de paso/ciclo de
  producción, expresados en milisegundos y descontando pausas; no mide carga de
  CPU.
- [`Logger`](Logger.md) ayuda a informar de eventos, pero imprimir dentro de
  `onOverrun()` puede agravar la latencia. Es preferible levantar una bandera y
  registrar después de `end()`.
- [`Telemetry`](Telemetry.md) puede añadir carga serie significativa. Al
  caracterizar el máximo, mide también la telemetría que vaya a permanecer en
  producción.
- [`AlarmManager`](../../src/data/AlarmManager.h) puede enclavar el evento de
  exceso; `isOverrun()` por sí solo solo refleja la última medida.
