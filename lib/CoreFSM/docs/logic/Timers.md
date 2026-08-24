# Timers.h

> Temporizadores cooperativos IEC 61131-3 (`TON`, `TOF`, `TP`) y un generador de parpadeo. No usan `delay()` ni interrumpen el ciclo de scan.

**Ruta:** [`src/logic/Timers.h`](../../src/logic/Timers.h)  
**Incluye:** [`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md)  
**Tipos públicos:** `Ton`, `Tof`, `Tp`, `Blink`.

---

## 1. Propósito

Estos bloques expresan temporizaciones sin bloquear el microcontrolador. El
programa entrega el nivel de entrada una vez por scan mediante `update()` y el
bloque conserva únicamente la marca temporal y el estado necesarios para
calcular la salida.

- `Ton`: retrasa la conexión.
- `Tof`: retrasa la desconexión.
- `Tp`: genera un impulso de duración fija al detectar un flanco ascendente.
- `Blink`: alterna una salida con tiempos de encendido y apagado configurables.

Son objetos independientes: cada instancia mantiene su propio estado. No hay
planificador global, interrupciones, memoria dinámica ni llamadas a `delay()`.

## 2. Modelo temporal y semántica

Todos los tiempos están en milisegundos y usan `cfsm_time_t` (`uint32_t`). La
resta se realiza mediante `cfsm_elapsed()`, por lo que una temporización en curso
tolera el desbordamiento de `millis()` siempre que no se deje pasar un intervalo
completo de `2^32` ms sin muestrearla.

La salida solo puede cambiar cuando se llama a `update()`. Por tanto, un plazo
vence en el **primer scan observado** para el que `ET >= PT`; no es una alarma de
hardware con precisión sub-scan. Un scan lento puede alargar el efecto visible
hasta el siguiente muestreo.

### 2.1 `Ton`: retardo a la conexión

```text
IN  ____|‾‾‾‾‾‾‾‾|____
Q   _________|‾‾‾|____
        < PT >
```

Al observar `IN = true` por primera vez, guarda la hora de arranque. Mientras la
entrada permanezca activa, actualiza `ET`; al alcanzar `PT`, satura `ET` en
`PT` y pone `Q = true`. Cualquier `IN = false` cancela la temporización y deja
`ET = 0`, `Q = false`.

Con `PT = 0`, una entrada activa pone `Q` a `true` en esa misma llamada.

### 2.2 `Tof`: retardo a la desconexión

```text
IN  ____|‾‾‾‾|________
Q   ____|‾‾‾‾‾‾‾‾|____
             < PT >
```

Mientras `IN` sea `true`, `Q` también lo es y `ET` permanece a cero. El primer
`false` posterior a un `true` inicia el retardo. Durante él, `Q` sigue activa;
al llegar a `PT`, `ET` se satura y `Q` cae.

Una instancia recién creada con entrada siempre falsa no genera ningún pulso.
Con `PT = 0`, la salida cae en la misma llamada que observa el flanco de bajada.
Si la entrada vuelve a subir durante el retardo, este se cancela y `Q` sigue
activa.

### 2.3 `Tp`: impulso

```text
IN  ____|‾|____________
Q   ____|‾‾‾‾‾|________
         < PT >
```

Un flanco ascendente, si no hay otro pulso en curso, pone `Q = true` y fija el
origen temporal. Los cambios posteriores de `IN` no recortan el impulso. Los
flancos adicionales mientras `Q` está activa se ignoran: `Tp` no es
redisparable.

Tras vencer, una entrada que continúe alta no inicia otro pulso; primero debe
observarse `IN = false` y después un nuevo flanco ascendente. Mientras la entrada
permanece alta tras el pulso, `ET` conserva `PT`; vuelve a cero al observar la
entrada baja. Con `PT = 0`, el pulso empieza y termina dentro de la misma llamada,
por lo que `update()` devuelve `false`.

### 2.4 `Blink`: onda rectangular

`Blink` empieza con `Q = false`. Usa `offTime` para decidir cuándo encender y
`onTime` para decidir cuándo apagar. `update(false)` apaga la salida y mueve la
marca temporal al instante de esa llamada. Al habilitarlo, el tiempo apagado se
mide desde la llamada deshabilitada más reciente; si desde entonces ya pasó
`offTime`, puede encender en esa misma actualización.

Solo realiza una conmutación por llamada. Si un scan dura varios periodos, no
intenta recuperar las conmutaciones perdidas. Cambiar el periodo tampoco
reinicia la fase en curso.

La marca inicial vale cero. En objetos globales esto hace que el primer intervalo
apagado se mida desde el arranque. Si se construye un `Blink` mucho después del
arranque y se habilita directamente, puede conmutar en la primera llamada porque
ya ha transcurrido `offTime`; una llamada previa a `update(false)` inicia una fase
nueva de forma explícita.

## 3. API

### 3.1 API común de `Ton`, `Tof` y `Tp`

| Miembro | Tipo | Significado |
|---|---|---|
| `PT` | `cfsm_time_t` | Plazo configurado, en ms. Nace en `0` |
| `Q` | `bool` | Salida actual |
| `ET` | `cfsm_time_t` | Tiempo transcurrido; se satura en `PT` al vencer |
| `update(bool IN)` | `bool` | Muestrea la entrada, actualiza el bloque y devuelve `Q` |
| `setPreset(cfsm_time_t ms)` | `void` | Cambia `PT`; no reinicia una temporización en curso |
| `reset()` | `void` | Borra salida, tiempo y memoria interna del bloque |

Los tres campos son públicos. Se pueden observar desde telemetría o modificar
directamente, aunque `setPreset()` deja más clara la intención.

### 3.2 API de `Blink`

| Miembro | Tipo | Significado |
|---|---|---|
| `onTime` | `cfsm_time_t` | Duración de la fase encendida; por defecto, 500 ms |
| `offTime` | `cfsm_time_t` | Duración de la fase apagada; por defecto, 500 ms |
| `Q` | `bool` | Salida actual; nace apagada |
| `update(bool enable = true)` | `bool` | Actualiza una fase y devuelve `Q`; deshabilitado fuerza `false` |
| `setPeriod(on, off)` | `void` | Define por separado ambos tiempos |
| `setPeriod(halfPeriod)` | `void` | Define una onda simétrica |

`Blink` no ofrece `reset()`. Para apagarlo e iniciar de nuevo la fase apagada,
se usa `update(false)`.

## 4. Ejemplo mínimo

```cpp
#include <CoreFSM.h>

Ton confirmacion;
Tof ventilacion;
Tp  disparo;
Blink piloto;

void setup() {
  confirmacion.setPreset(100);      // sensor estable durante 100 ms
  ventilacion.setPreset(3000);      // mantener 3 s al retirar la orden
  disparo.setPreset(50);            // impulso de 50 ms
  piloto.setPeriod(100, 400);       // 100 ms ON, 400 ms OFF
}

void loop() {
  bool sensorConfirmado = confirmacion.update(sensorBruto);
  bool ventilador       = ventilacion.update(motorEnMarcha);
  bool salidaDisparo    = disparo.update(ordenDisparo);
  bool luzAlarma        = piloto.update(alarmaActiva);

  // Aplicar las cuatro variables a la imagen de salidas.
}
```

La aplicación debe llamar a cada `update()` una vez por scan. Llamarlo varias
veces consume varios muestreos lógicos y, en `Tp`, también actualiza varias veces
la memoria de flanco; el contrato de estos bloques es un muestreo por vuelta.

## 5. Coste

- Tiempo de ejecución constante, `O(1)`, sin bucles ni reservas dinámicas.
- Cada actualización activa lee el reloj una o dos veces y realiza unas pocas
  comparaciones y asignaciones.
- Cada instancia almacena sus campos públicos más una marca temporal y uno o
  dos booleanos privados. El tamaño final depende de la alineación del ABI;
  puede medirse en la placa con `sizeof(Ton)`, `sizeof(Tof)`, etc.
- Los cuatro tipos son adecuados para instancias globales o miembros de un
  bloque; no crean tareas ni consumen pila entre scans.

## 6. Errores frecuentes y limitaciones

- **Confundir `PT = 0` con desactivado.** Es un vencimiento inmediato. Para
  desactivar el efecto, no llames al bloque con una entrada activa o usa una
  condición de habilitación externa.
- **No llamar a `update()` en todos los scans.** Los plazos se miden con el reloj
  real, pero la salida no cambia hasta la próxima llamada.
- **Esperar precisión de hardware.** El jitter y la resolución son los del scan
  y de `millis()`.
- **Usar `Tp` como redisparable.** Un flanco durante el pulso se descarta.
- **Usar `Blink` para generar un reloj de protocolo.** Omite periodos si el scan
  se retrasa; está pensado para señalización, no para comunicaciones.
- **Modificar `PT` durante una temporización.** El nuevo valor se compara con el
  tiempo ya acumulado; `setPreset()` no reinicia el bloque. En particular, subir
  el `PT` de un `Ton` cuya `Q` ya está activa no vuelve a apagarlo: hay que
  aplicar `reset()` o retirar `IN` para rearmarlo.
- **Intervalos de 49,7 días o más.** Un contador de 32 bits en milisegundos no
  puede distinguir más de una vuelta completa del reloj.

## 7. Relación con otros módulos

- [`CoreFSM_Platform`](../core/CoreFSM_Platform.md) aporta `cfsm_time_t`,
  `cfsm_millis()` y la resta segura `cfsm_elapsed()`.
- [`Edges`](Edges.md) aporta detectores de flanco explícitos. `Tp` ya incluye su
  propia memoria de flanco y no necesita un `RTrig` adicional.
- [`Counters`](Counters.md) aplica el mismo modelo cooperativo y cuenta flancos
  muestreados por scan.
- [`SequenceBlock`](../core/SequenceBlock.md) ofrece watchdogs de paso y ciclo.
  Esos límites supervisan una secuencia completa; no sustituyen a `Ton`, `Tof`
  o `Tp` para lógica de proceso.
