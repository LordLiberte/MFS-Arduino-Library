# Edges.h

> Detectores de flanco, biestables dominantes y un conmutador por pulsación para lógica cíclica.

**Ruta:** [`src/logic/Edges.h`](../../src/logic/Edges.h)  
**Incluye:** [`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md)  
**Tipos públicos:** `RTrig`, `FTrig`, `EdgeDetect`, `SR`, `RS`, `Toggle`.

---

## 1. Propósito

En un programa de scan, una señal puede permanecer activa durante cientos de
vueltas. Estos bloques separan dos conceptos:

- el **nivel**, que sigue valiendo `true` mientras la señal está activa;
- el **flanco**, que vale `true` solo en la llamada que observa el cambio.

El archivo también aporta memorias `SR`/`RS` y un `Toggle`. Todos son objetos
cooperativos, sin interrupciones ni memoria dinámica.

## 2. Modelo y semántica

### 2.1 Detectores de flanco

Cada detector compara `IN` con el último nivel que él mismo observó:

```text
IN       ____|‾‾‾‾‾|____
RTrig.Q  ____|‾|_________
FTrig.Q  __________|‾|___
```

- `RTrig` activa `Q` al pasar de falso a verdadero.
- `FTrig` activa `Q` al pasar de verdadero a falso.
- `EdgeDetect` calcula a la vez `rising`, `falling` y `changed`.

La memoria previa nace en `false`. Por eso la **primera** llamada con
`IN = true` es un flanco ascendente válido. La primera llamada con entrada falsa
no produce un flanco de bajada.

La salida de flanco permanece válida hasta la próxima llamada a `update()`. Si
se llama dos veces seguidas con el mismo nivel, solo la primera puede devolver
`true`. Si la entrada cambia dos veces entre scans, ambos cambios se pierden: el
bloque solo conoce los niveles que se le entregan.

### 2.2 Biestables `SR` y `RS`

Los biestables memorizan una orden incluso después de desaparecer su causa:

| Tipo | Entradas simultáneas | Resultado |
|---|---|---|
| `SR` | `S1 = true`, `R = true` | `Q1 = true`; domina SET |
| `RS` | `S = true`, `R1 = true` | `Q1 = false`; domina RESET |

No detectan flancos: sus entradas son niveles evaluados en cada llamada. La
prioridad se obtiene por el orden exacto de asignación dentro de `update()`.

`RS` es útil cuando una orden lógica de paro debe dominar una orden lógica de
marcha, pero sigue siendo software ordinario. **No es un relé de seguridad ni
convierte el microcontrolador en un sistema de parada certificada.**

### 2.3 `Toggle`

`Toggle` contiene un `RTrig`. Cada flanco ascendente invierte `Q`; mantener la
entrada alta no vuelve a conmutar. `set(bool)` cambia la salida, pero conserva la
memoria del nivel de entrada. `reset()` pone `Q` y esa memoria a falso.

Esta diferencia importa: después de `set(false)`, un pulsador que ya estaba
mantenido no genera un flanco nuevo; después de `reset()`, la siguiente llamada
con ese mismo nivel alto sí se interpreta como flanco.

## 3. API

### 3.1 Flancos

| Tipo | Estado público | Métodos | Retorno de `update()` |
|---|---|---|---|
| `RTrig` | `bool Q` | `bool update(bool IN)`, `reset()` | `Q`, flanco ascendente |
| `FTrig` | `bool Q` | `bool update(bool IN)`, `reset()` | `Q`, flanco descendente |
| `EdgeDetect` | `rising`, `falling`, `changed` | `bool update(bool IN)`, `reset()` | `changed` |

`reset()` deja la muestra anterior en falso y borra todas las salidas públicas.

### 3.2 Memorias

| Tipo | Estado público | Método principal | Prioridad |
|---|---|---|---|
| `SR` | `bool Q1` | `bool update(bool S1, bool R)` | SET |
| `RS` | `bool Q1` | `bool update(bool S, bool R1)` | RESET |

Ambos ofrecen `void reset()`, que deja `Q1 = false`.

### 3.3 Conmutador

```cpp
bool Toggle::update(bool IN);  // devuelve Q
void Toggle::set(bool v);      // cambia Q sin tocar el detector interno
void Toggle::reset();          // Q=false y detector interno reiniciado
```

## 4. Ejemplo mínimo

```cpp
#include <CoreFSM.h>

RTrig  ordenUnaVez;
RS     marchaMemorizada;
Toggle luzPorPulsacion;

void loop() {
  // Las entradas deben ser niveles ya muestreados y, si son contactos,
  // preferiblemente filtrados por DigitalSensor.
  if (ordenUnaVez.update(ordenCiclo)) {
    lanzarUnaOperacion();
  }

  bool motor = marchaMemorizada.update(pulsadorMarcha, pulsadorParo);
  bool luz   = luzPorPulsacion.update(pulsadorLuz);

  // Escribir motor y luz en la imagen de salidas.
}
```

Si `pulsadorMarcha` y `pulsadorParo` llegan activos en el mismo scan, el `RS`
del ejemplo deja `motor = false`.

## 5. Coste

- Todas las actualizaciones son `O(1)` y solo realizan operaciones booleanas.
- No hay reloj, bucles, asignación dinámica ni acceso a pines.
- El estado lógico es mínimo: dos booleanos en `RTrig`/`FTrig`, cuatro en
  `EdgeDetect`, uno en `SR`/`RS`, y la salida más un `RTrig` en `Toggle`.
- El tamaño físico puede incluir relleno de alineación; `sizeof(Toggle)` en la
  arquitectura de destino ofrece la cifra real.

## 6. Errores frecuentes y limitaciones

- **Confundir flanco con antirrebote.** Un contacto mecánico puede producir
  varios flancos reales en pocos milisegundos. Usa un
  [`DigitalSensor`](../io/DigitalSensor.md) o un filtro equivalente antes del
  detector.
- **Actualizar el mismo detector desde dos sitios.** La primera llamada consume
  el cambio; la segunda observa el nivel ya guardado.
- **Compartir una instancia entre dos señales.** La memoria anterior pertenece
  al objeto, no al argumento; cada señal necesita su detector.
- **Muestrear demasiado despacio.** Los pulsos completos entre dos llamadas no
  pueden detectarse. Para señales más rápidas que el scan hacen falta captura
  por interrupción o periféricos hardware.
- **Resetear con la entrada alta.** Como el estado anterior vuelve a falso, la
  siguiente actualización genera un flanco ascendente artificial desde el
  punto de vista del proceso.
- **Tratar `RS` como seguridad funcional.** Su prioridad de reset solo resuelve
  lógica. La parada física debe imponerse por una cadena de seguridad adecuada.

## 7. Relación con otros módulos

- [`DigitalSensor`](../io/DigitalSensor.md) ya incorpora antirrebote y expone
  eventos de subida/bajada; estos detectores son útiles para otras variables
  booleanas o señales ya filtradas.
- [`Counters`](Counters.md) contiene sus propios `RTrig`: hay que pasarles el
  nivel, no es necesario encadenar otro detector salvo que se busque una lógica
  distinta.
- [`Timers`](Timers.md) trabaja con niveles a lo largo del tiempo; combinar un
  `RTrig` con `Tp` suele ser redundante porque `Tp` detecta internamente el
  flanco ascendente.
- [`SequenceBlock`](../core/SequenceBlock.md) usa pasos persistentes. Los
  detectores son apropiados para convertir órdenes externas mantenidas en
  eventos antes de modificar una secuencia.
