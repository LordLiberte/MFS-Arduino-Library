# DataBlock.h

> Bloque de datos global con copia persistente, detección de cambios y
> autoguardado limitado por tiempo.

**Ruta:** `src/data/DataBlock.h`  
**Depende de:** [`ConfigStore`](ConfigStore.md).

## 1. Qué representa

`DataBlock<T>` expone una estructura `data` en RAM y mantiene un CRC del último
estado guardado o cargado. Es apropiado para contadores, acumulados y ajustes
remanentes; no para pasos, órdenes o temporizadores en curso que podrían
reanudar una máquina en una posición desconocida tras un corte.

```cpp
struct Produccion {
  uint32_t piezas;
  uint32_t horas;
};

DataBlock<Produccion, 1, 0> db;

void setup() {
  db.data = {0, 0};
  db.begin(true);
  db.setMinSaveInterval(300000); // 5 min
}
```

## 2. Flujo

```text
begin() ─▶ cargar ConfigStore ─▶ snapshot CRC
                         │
programa modifica data ──┴─▶ isDirty()
                                  │
                    save() / autoSave()
```

`autoSave()` devuelve `true` solo cuando realizó un guardado correcto. Puede
llamarse cíclicamente: antes del intervalo o sin cambios devuelve `false`.

## 3. API

| Miembro | Uso |
|---|---|
| `data` | Acceso directo a la estructura RAM |
| `begin(loadFromNvm=true)` | Inicializa y opcionalmente carga |
| `save()` | Guardado inmediato |
| `autoSave()` | Guarda si hay cambios y venció el intervalo |
| `isDirty()` | CRC actual distinto del snapshot |
| `revert()` | Descarta RAM y recupera la copia válida |
| `factoryReset()` | Invalida persistencia |
| `setMinSaveInterval(ms)` | Intervalo, 60 s por defecto |
| `lastResult()` / `resultText()` | Diagnóstico del store interno |
| `footprint()` / `nextAddress()` | Planificación de NVM |

## 4. Semántica del intervalo

`_lastSave` parte de cero. El primer autoguardado solo puede ocurrir cuando han
pasado `_minInterval` milisegundos desde el arranque o último guardado. Si al
vencer el intervalo no hubo cambios, se actualiza la marca de tiempo y vuelve a
esperar otro intervalo.

## 5. Inicialización sin carga

`begin(false)` no lee NVM, pero sí captura el CRC de los valores actuales y la
hora de inicio. Por tanto, `isDirty()` empieza a `false` y el intervalo mínimo
se cuenta desde esa llamada, igual que tras una carga normal.

## 6. Límites y buenas prácticas

- El CRC se recalcula sobre `sizeof(T)`; estructuras grandes encarecen cada
  llamada a `isDirty()` y `autoSave()`.
- No modifica `data` de forma atómica respecto a interrupciones.
- Un intervalo de 60 s aún puede producir cientos de miles de escrituras al año.
- Guarda de forma explícita en paradas ordenadas y usa autoguardado como respaldo.
- No solapes su dirección con recetas u otros `ConfigStore`.
- Inicializa `data` antes de `begin()`, pues esos valores son el fallback.

## 7. Ejemplo de uso

```cpp
void alCompletarPieza() {
  db.data.piezas++;
}

void loop() {
  // lógica...
  db.autoSave();
}
```

No uses el resultado de `autoSave()` como indicación de «datos válidos»: solo
indica que en esa llamada hubo escritura correcta.
