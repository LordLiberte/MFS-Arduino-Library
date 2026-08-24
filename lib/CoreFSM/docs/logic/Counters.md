# Counters.h

> Contadores de flancos IEC 61131-3 (`CTU`, `CTD`, `CTUD`) y acumulador de tiempo de funcionamiento.

**Ruta:** [`src/logic/Counters.h`](../../src/logic/Counters.h)  
**Incluye:** [`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md),
[`Edges.h`](Edges.md)  
**Tipos públicos:** `Ctu`, `Ctd`, `Ctud`, `RunHourMeter`.

---

## 1. Propósito

Los tres contadores IEC cuentan **flancos ascendentes muestreados**, no scans con
una entrada a nivel alto. Cada objeto contiene sus propios detectores `RTrig`,
de modo que puede recibir directamente el nivel estable de una fotocélula, un
final de carrera o una orden lógica.

`RunHourMeter` no pertenece a IEC 61131-3: acumula segundos completos durante
los que una condición de marcha permanece activa.

## 2. Modelo y semántica

### 2.1 `Ctu`: contador ascendente

- `CU` cuenta una unidad en cada flanco ascendente.
- `RESET` tiene prioridad: pone `CV = 0`, `Q = false` y no cuenta ese scan.
- `Q` se recalcula como `CV >= PV` en cada actualización normal.
- `CV` satura en `65535`; nunca desborda a cero.

Cuando `RESET = true`, el nivel de `CU` sí se muestrea internamente. Si `CU`
permanece alto al retirar el reset, no se inventa un flanco. En cambio, el método
directo `reset()` reinicia el detector; una entrada que siga alta se contará en
la siguiente llamada.

Con `PV = 0`, una actualización normal deja `Q = true`. La rama `RESET`, no
obstante, devuelve `false` y conserva `Q = false` hasta esa actualización normal.

### 2.2 `Ctd`: contador descendente

- `LOAD` tiene prioridad: carga `CV = PV`, deja `Q = false` y no cuenta.
- `CD` resta una unidad por flanco mientras `CV > 0`.
- En una actualización normal, `Q` se recalcula como `CV == 0`.
- El valor satura en cero; no desborda a `65535`.

`update(CD, true)` muestrea `CD` para evitar un flanco fantasma tras la carga.
El método directo `load()` solo copia `PV` y borra `Q`: **no** toca el detector
interno. Con `PV = 0`, la rama `LOAD` deja temporalmente `Q = false`; la siguiente
actualización normal lo corrige a `true`.

`reset()` representa un contador vacío: deja `CV = 0`, `Q = true` y reinicia el
detector.

### 2.3 `Ctud`: contador bidireccional

`CU` incrementa y `CD` decrementa mediante detectores independientes. `QU`
indica `CV >= PV`; `QD`, `CV == 0`.

Prioridades y orden exactos:

1. Si `RESET` o `LOAD` está activo, no se cuenta. `RESET` domina a `LOAD`:
   carga cero si ambos valen `true`; en otro caso `LOAD` carga `PV`.
2. Esa rama muestrea tanto `CU` como `CD`.
3. Sin carga ni reset, se procesa primero el flanco de subida y después el de
   bajada.
4. Finalmente se recalculan siempre `QU` y `QD`.

Dos flancos simultáneos normalmente se compensan. Hay una excepción en el
límite superior: con `CV = 65535`, el incremento queda saturado y el decremento
posterior deja `CV = 65534`.

El método `reset()` deja explícitamente `QU = false`, `QD = true`. Si `PV = 0`,
una llamada posterior a `update()` recalcula `QU = true`.

### 2.4 `RunHourMeter`

Cuando `running` pasa a verdadero, guarda la hora. Mientras siga activo, añade
segundos completos cuando han transcurrido al menos 1000 ms y conserva el resto
subsegundo desplazando la marca temporal en múltiplos de un segundo.

Al observar `running = false`, termina el tramo y descarta la fracción inferior
a un segundo que estuviera pendiente. Al reanudar, empieza desde la hora actual.
`minutes()` devuelve el componente de minutos dentro de la hora (`0..59`), no
los minutos totales.

El contador solo vive en RAM. Guardar `totalSeconds` en memoria no volátil es
responsabilidad de la aplicación.

## 3. API

### 3.1 `Ctu`

| Miembro | Tipo | Función |
|---|---|---|
| `PV` | `uint16_t` | Valor preseleccionado |
| `CV` | `uint16_t` | Cuenta actual |
| `Q` | `bool` | `CV >= PV` tras la última actualización normal |
| `update(bool CU, bool RESET = false)` | `bool` | Actualiza y devuelve `Q` |
| `setPreset(uint16_t v)` | `void` | Cambia `PV`, sin recalcular `Q` ni `CV` |
| `reset()` | `void` | Borra cuenta, salida y memoria de flanco |

### 3.2 `Ctd`

| Miembro | Tipo | Función |
|---|---|---|
| `PV`, `CV`, `Q` | campos públicos | Preset, cuenta y condición de cero |
| `update(bool CD, bool LOAD = false)` | `bool` | Carga o cuenta; devuelve `Q` |
| `setPreset(uint16_t v)` | `void` | Cambia `PV` sin cargarlo |
| `load()` | `void` | Copia `PV` a `CV`; no muestrea `CD` |
| `reset()` | `void` | Deja `CV = 0`, `Q = true` y reinicia el flanco |

### 3.3 `Ctud`

| Miembro | Tipo | Función |
|---|---|---|
| `PV`, `CV` | `uint16_t` | Preset y cuenta actual |
| `QU`, `QD` | `bool` | Límite alto alcanzado y cuenta a cero |
| `update(CU, CD, RESET = false, LOAD = false)` | `void` | Actualiza entradas y salidas |
| `setPreset(uint16_t v)` | `void` | Cambia `PV`; no recalcula salidas |
| `reset()` | `void` | Borra cuenta y ambos detectores |

### 3.4 `RunHourMeter`

```cpp
uint32_t totalSeconds;       // público
void update(bool running);
uint32_t hours() const;      // horas completas totales
uint16_t minutes() const;    // componente 0..59
void reset();
```

## 4. Ejemplo mínimo

```cpp
#include <CoreFSM.h>

Ctu piezas;
Ctd pendientes;
RunHourMeter horasMotor;

void setup() {
  piezas.setPreset(12);
  pendientes.setPreset(12);
  pendientes.load();
}

void loop() {
  // Pasar niveles estables: los contadores detectan el flanco internamente.
  bool cajaCompleta = piezas.update(fotocelula, ordenNuevaCaja);
  bool loteTerminado = pendientes.update(piezaTerminada, cargarNuevoLote);
  horasMotor.update(motorEnMarcha);

  if (cajaCompleta) evacuarCaja = true;
  if (loteTerminado) finDeLote = true;
}
```

Si se usa `pendientes.load()` mientras `piezaTerminada` puede estar alta,
conviene preferir `pendientes.update(piezaTerminada, true)`, que sí conserva el
nivel observado y evita una cuenta falsa al scan siguiente.

## 5. Coste

- Cada actualización es `O(1)`, sin bucles ni memoria dinámica.
- `Ctu` y `Ctd` guardan dos `uint16_t`, dos booleanos lógicos y la memoria del
  flanco. `Ctud` añade un segundo detector y una segunda salida.
- `RunHourMeter` guarda dos `uint32_t` y un booleano lógico; consulta
  `cfsm_millis()` una vez por llamada.
- La alineación cambia entre AVR y plataformas de 32 bits. Usa `sizeof(Ctu)` o
  `sizeof(RunHourMeter)` en el destino cuando el presupuesto de RAM sea crítico.

## 6. Errores frecuentes y limitaciones

- **Pasar pulsos más rápidos que el scan.** Un pulso completo entre llamadas se
  pierde. Para alta frecuencia se necesita un contador o captura hardware.
- **Añadir un `RTrig` sin necesidad.** Los tres contadores ya cuentan flancos.
  Pasarles un pulso de un scan funciona, pero duplica estado sin aportar nada.
- **Cambiar `PV` esperando actualizar `Q`.** `setPreset()` solo cambia el preset;
  las salidas se recalculan en el próximo `update()`.
- **Confundir los métodos directos con las entradas IEC.** `Ctu::reset()` y
  `Ctd::load()` no muestrean el nivel actual; las ramas `RESET`/`LOAD` de
  `update()` sí lo hacen.
- **Esperar persistencia del cuentahoras.** Un reinicio borra `totalSeconds` si
  la aplicación no lo guarda y restaura.
- **Desbordamiento del cuentahoras.** `totalSeconds` no satura; vuelve a cero al
  superar `UINT32_MAX` (aproximadamente 136 años).
- **Ausencias de actualización muy largas.** La resta temporal tolera una vuelta
  de `millis()`, pero no puede reconstruir varias vueltas completas sin llamadas.

## 7. Relación con otros módulos

- [`Edges`](Edges.md) define el `RTrig` que usan internamente `Ctu`, `Ctd` y
  `Ctud`.
- [`DigitalSensor`](../io/DigitalSensor.md) puede filtrar el rebote antes de que
  el contador observe el nivel.
- [`Timers`](Timers.md) mide intervalos breves de proceso; `RunHourMeter` acumula
  segundos de uso a largo plazo.
- [`ConfigStore`](../../src/data/ConfigStore.h) puede servir para persistir
  `totalSeconds`. La política de frecuencia de escritura debe respetar la vida
  útil de EEPROM/flash; el cuentahoras no guarda automáticamente.
- [`SequenceBlock`](../core/SequenceBlock.md) ya mantiene `cycleCount` para ciclos
  completos de una secuencia. Usa `Ctu` cuando el evento contado no coincide con
  el cierre de ciclo.
