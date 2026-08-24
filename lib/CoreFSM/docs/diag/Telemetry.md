# Telemetry.h

> Herramientas de observación por puerto serie: trazador de pasos, muestreo CSV y consola de mantenimiento.

**Ruta:** [`src/diag/Telemetry.h`](../../src/diag/Telemetry.h)  
**Incluye:** [`BlockManager.h`](../core/BlockManager.md),
[`SequenceBlock.h`](../core/SequenceBlock.md), [`Logger.h`](Logger.md)  
**Tipos públicos:** `StepTracer`, `CsvLogger<COLS>`,
`MaintenanceConsole<MAX_BLOCKS>`.

---

## 1. Propósito

Las tres utilidades cubren formas distintas de observar una máquina sin HMI:

- `StepTracer` emite eventos legibles al cambiar el estado o el paso de un
  `SequenceBlock`.
- `CsvLogger` toma muestras periódicas de valores enteros para analizarlas
  después.
- `MaintenanceConsole` recibe comandos de un carácter y opera sobre un
  `BlockManager`.

Ninguna crea tareas, reserva memoria dinámica ni configura el puerto. Todas son
cooperativas: la aplicación llama a `update()` o `tick()` desde su scan.

No existe una cola de salida. Aunque la lógica de disparo sea acotada, las
operaciones sobre `Print`/`Stream` pueden esperar si el transporte se satura.

## 2. Modelo y semántica

### 2.1 `StepTracer`

El constructor conserva referencias al bloque y al destino. Internamente
recuerda el último estado (`uint8_t`) y paso (`uint16_t`) observados. En cada
`update()` consulta ambos y solo imprime los que difieren.

La primera llamada es especial: los valores previos son sentinelas, por lo que
se emiten siempre una línea de estado y otra de paso. En esa primera línea de
paso, el origen se muestra como `0`, aunque no represente un paso observado
realmente.

Formato aproximado:

```text
[ESTADO] PRENSA -> RUNNING
[PASO]   PRENSA 10 -> 20 (BAJAR)
```

Si el bloque no tiene nombre, se muestra `#<id>`. Si `stepName(step)` devuelve
un texto, se añade entre paréntesis. Al entrar en `STATE_ERROR`, imprime además
el código hexadecimal de `ST.errorCode` y su texto.

Debe llamarse después de `BlockManager::updateAll()` para observar el resultado
del scan recién ejecutado. Si el bloque cambia varias veces entre dos llamadas,
solo se ve el valor final. El trazador no modifica el bloque y no usa
`CfsmLogger`: su salida no tiene filtro de compilación ni timestamp automático.

### 2.2 `CsvLogger<COLS>`

`COLS` es el número de **variables**, sin contar la primera columna temporal que
el logger añade siempre. El valor por defecto es seis. Cada variable se guarda
como `int32_t` y nace en cero.

El objeto nace deshabilitado. `enable(true)` inicia un intervalo nuevo desde la
hora actual. En cada `tick()` habilitado:

1. comprueba si han transcurrido al menos `_period` milisegundos;
2. si aún no toca, no escribe;
3. si toca, fija la referencia a la hora actual;
4. imprime `cfsm_millis(),v0,v1,...` y un salto de línea.

Solo genera una fila por llamada. Si el scan se retrasa tres periodos, no rellena
las dos muestras perdidas. Como la nueva referencia se toma en el momento real
de emisión, el plan de muestreo puede derivar con el jitter del scan.

`set(col, value)` ignora silenciosamente índices `col >= COLS`. Las variables no
se borran al deshabilitar o volver a habilitar. La cabecera es texto libre y la
clase no comprueba que su número de nombres coincida con las columnas reales.

### 2.3 `MaintenanceConsole<MAX_BLOCKS>`

La consola conserva una referencia a un `BlockManager<MAX_BLOCKS>` y al
`Stream`. `update()` comprueba `available()` y procesa **como máximo un carácter
por scan**:

| Carácter | Acción |
|---|---|
| `w` | `printWatchTable()` |
| `s` | `startAll()` y mensaje `MARCHA` |
| `x` | `stopAll()` y mensaje `PARO` |
| `p` | alterna entre `holdAll()` y `resumeAll()` |
| `r` | `resetAll()` y mensaje `REARME` |
| `c` | imprime estadísticas del manager |
| `?` | imprime la ayuda |
| otro | llama al manejador extra, si existe |

Los comandos distinguen mayúsculas y minúsculas. Los finales de línea enviados
por un terminal (`'\r'`, `'\n'`) también son “otro”: se ignoran sin manejador o
se entregan al manejador registrado.

La pausa se recuerda en un booleano privado de la consola, no se deduce del
estado real de los bloques. Si otro módulo llama a `holdAll()`/`resumeAll()`, o
si los bloques no aceptan la orden, la siguiente tecla `p` puede no coincidir
con lo que el operador espera.

`x` solicita un paro normal mediante `stopAll()`. No es una parada inmediata de
hardware ni una función de seguridad.

## 3. API

### 3.1 `StepTracer`

```cpp
StepTracer(SequenceBlock& blk, Print& out);
void update();
```

El bloque y el destino deben vivir más que el trazador. No hay método para
cambiarlos ni para reiniciar la memoria observada.

### 3.2 `CsvLogger<COLS>`

| Método | Efecto |
|---|---|
| `CsvLogger(Print& out, cfsm_time_t periodMs = 100)` | Construye deshabilitado y pone valores a cero |
| `header(const __FlashStringHelper* h)` | Imprime una cabecera inmediatamente |
| `enable(bool e)` | Habilita/deshabilita y reinicia la marca temporal |
| `isEnabled() const` | Consulta la habilitación |
| `set(uint8_t col, int32_t value)` | Actualiza una variable válida; ignora índices fuera de rango |
| `tick()` | Emite, como máximo, una fila cuando vence el periodo |

`header()` acepta el tipo devuelto por `F()`/`CFSM_FSTR()`, no un `const char*`
arbitrario. El periodo queda fijado por el constructor y no tiene setter.

### 3.3 `MaintenanceConsole<MAX_BLOCKS>`

```cpp
MaintenanceConsole(BlockManager<MAX_BLOCKS>& mgr, Stream& port);
void update();
void setExtraHandler(void (*fn)(char));
```

El manejador extra es un puntero a función sin contexto. Puede ser una función
global/estática o una lambda sin capturas convertible a ese tipo.

## 4. Ejemplos mínimos

### 4.1 Trazador y consola

Suponiendo que `Proceso` hereda de `SequenceBlock`:

```cpp
#include <CoreFSM.h>

BlockManager<4> manager;
Proceso proceso;
StepTracer tracer(proceso, Serial);
MaintenanceConsole<4> consola(manager, Serial);

void setup() {
  Serial.begin(115200);
  manager.registerBlock(&proceso, F("PROCESO"));
}

void loop() {
  manager.updateAll();
  tracer.update();             // observa el estado resultante
  consola.update();            // como máximo, un comando
}
```

### 4.2 CSV

```cpp
CsvLogger<2> muestras(Serial, 100);  // timestamp + dos variables

void setupCsv() {
  muestras.header(F("t_ms,paso,estado"));
  muestras.enable(true);
}

void actualizarCsv() {
  muestras.set(0, proceso.getStep());
  muestras.set(1, proceso.getState());
  muestras.tick();
}
```

Para importar el CSV de forma fiable, no mezcles en el mismo puerto las filas
con la salida textual del trazador, la consola o el logger.

## 5. Coste

### `StepTracer`

- Estado fijo: dos referencias, un `uint16_t` y un `uint8_t`, más alineación.
- Cada scan hace dos consultas y comparaciones, `O(1)`.
- El coste de impresión aparece solo al detectar cambios, pero un proceso que
  oscile de estado/paso en cada scan puede saturar igualmente el puerto.

### `CsvLogger<COLS>`

- Reserva estática de `4 * COLS` bytes para valores, además de referencia,
  periodo, marca y habilitación. No usa heap.
- `set()` es `O(1)`; una fila es `O(COLS)` y realiza una conversión/impresión
  por columna.
- Aumentar `COLS` eleva linealmente RAM, longitud de línea y tiempo de emisión.

### `MaintenanceConsole<MAX_BLOCKS>`

- Estado fijo: referencias, un booleano y un puntero a función.
- La consulta de entrada normal es `O(1)`. Los comandos colectivos y la watch
  table recorren los bloques registrados, hasta `MAX_BLOCKS`; imprimir la tabla
  puede dominar ampliamente un scan.

## 6. Errores frecuentes y limitaciones

- **Llamar al trazador antes del manager.** La notificación queda retrasada un
  scan y puede describir un estado anterior.
- **Suponer que “solo al cambiar” significa no bloqueante.** Un único mensaje
  largo también puede llenar el buffer de transmisión.
- **Mezclar texto y CSV.** El resultado deja de ser un CSV limpio.
- **Contar mal las columnas.** `CsvLogger<N>` imprime `N + 1` campos porque el
  timestamp es automático.
- **Usar `COLS = 0`.** Un array de longitud cero no es C++ portátil; usa al menos
  una columna.
- **Esperar tipos distintos de entero.** `set()` almacena `int32_t`; no admite
  directamente coma flotante, cadenas ni valores `uint32_t` superiores a
  `INT32_MAX` sin conversión/pérdida de interpretación.
- **Confiar en una cadencia exacta.** El CSV está cuantizado por el scan y omite
  periodos perdidos.
- **Exponer la consola sin control.** Cualquier emisor con acceso al `Stream`
  puede arrancar, parar, pausar o rearmar todos los bloques. No hay autenticación
  ni confirmación.
- **Confundir `x` con emergencia.** Es un `stopAll()` lógico. La parada segura
  debe resolverse fuera de esta consola.
- **Desincronizar la tecla `p`.** La consola no observa pausas solicitadas por
  otros componentes.

## 7. Relación con otros módulos

- [`SequenceBlock`](../core/SequenceBlock.md) aporta estado, paso, nombres y
  códigos de error que usa `StepTracer`.
- [`BlockManager`](../core/BlockManager.md) es el objetivo de la consola y la
  fuente de sus estadísticas de scan.
- [`Logger`](Logger.md) resuelve mensajes por niveles. Las herramientas de esta
  cabecera escriben directamente y no quedan eliminadas por `CFSM_LOG_LEVEL`.
- [`ScanWatchdog`](ScanWatchdog.md) ofrece medidas más completas del scan total.
  La estadística de `BlockManager` solo cronometra `updateAll()`.
- [`AlarmManager`](../../src/data/AlarmManager.h) puede exponerse mediante un
  comando extra definido por la aplicación; la consola no conoce alarmas ni E/S
  por sí sola.
