# RecipeExecutor.h

> `SequenceBlock` genérico que interpreta una `RecipeRecord` sobre un conjunto
> de ejes `IAxis` y una herramienta definida por el usuario.

**Ruta:** `src/data/RecipeExecutor.h`  
**Hereda:** [`SequenceBlock`](../core/SequenceBlock.md).

## 1. Fases internas

```text
RX_IDLE → RX_LOAD → RX_MOVE → RX_SETTLE → RX_TOOL → RX_NEXT
                                                        │
                                        más pasos ──────┘
                                        fin → RX_FINISHED
```

- `LOAD` valida homing y envía consignas.
- `MOVE` evalúa el `StepTrigger` y el timeout configurado con `setStep`.
- `SETTLE` detiene ejes y completa la permanencia que no se consumió en MOVE.
- `TOOL` llama `applyTool()` una vez al entrar y comprueba feedback.
- `NEXT` avanza el índice.
- `FINISHED` cierra el ciclo y vuelve a reposo.

## 2. Preparación

```cpp
class MiEjecutor : public RecipeExecutor<2> {
 protected:
  void applyTool(uint8_t mask) override {
    pinza.set(mask & 0x01);
    vacio.set(mask & 0x02);
  }

  bool toolFeedback(uint8_t mask) override {
    return !(mask & 0x01) || sensorPinza.isTriggered();
  }
};

MiEjecutor ejecutor;
ejecutor.attachAxis(0, &ejeX);
ejecutor.attachAxis(1, &ejeY);
ejecutor.setRecipe(&banco.active);
```

Registra el ejecutor en `BlockManager`, llama `beginAll()` y después `start()`.
Actualiza los objetos `IAxis` desde el programa; el ejecutor no lee sensores ni
ejecuta el lazo de cada eje automáticamente.

## 3. API

| Método | Uso |
|---|---|
| `attachAxis(i,IAxis*)` | Asocia índice de receta y eje |
| `setRecipe(RecipeRecord*)` / `recipe()` | Receta apuntada |
| `stepIndex()` | Índice de paso de receta, no fase FSM |
| `toolMask()` | Última máscara aplicada |
| `setSensorState(id,value)` | Imagen de hasta 8 triggers externos |
| `advanceManual()` | Autoriza `TRIG_MANUAL` |
| `teachStep(...)` | Captura posiciones y rellena un paso |
| `axisAt(i)` | Acceso protegido para clases derivadas |
| `applyTool(mask)` | Hook de salida; vacío por defecto |
| `toolFeedback(mask)` | Hook; `true` por defecto |
| `onInactive()` | Hook; detiene todos los ejes por defecto |

## 4. Dwell y timeout

Para `TRIG_TIMER` y `TRIG_POSITION_TIME`, `dwellMs` se consume en `RX_MOVE` y no
se repite en `RX_SETTLE`. En los demás disparadores, `RX_SETTLE` aplica la
permanencia después de cumplirse la condición.

El timeout del paso se toma de `transition.timeoutMs`. Cero desactiva esa
vigilancia. Al llamar a `setRecipe()`, la implementación actual copia
`RecipeHeader.maxCycleMs` a `setCycleTimeout()`; cero desactiva también esa
vigilancia de ciclo.

## 5. Teach-in

`teachStep()` copia `IAxis::position()` a cada eje conectado y asigna valores de
velocidad, tolerancia, herramienta, dwell y timeout. Amplía `totalSteps` si
procede, pero no guarda NVM: llama después `RecipeBank::saveSlot()`.

Realiza aprendizaje únicamente con la máquina en un modo controlado y sin
ejecutar simultáneamente la receta.

## 6. Validaciones necesarias

El ejecutor rechaza puntero nulo, cero pasos y exceso de pasos. Un
`static_assert` impide que `NUM_AXES` supere `CFSM_RECIPE_AXES`. Antes de mover,
todo eje habilitado debe estar conectado y referenciado; de lo contrario genera
`CFSM_ERR_RECIPE_INVALID` o `CFSM_ERR_NOT_HOMED`. El cierre usa
`completeCycle(RX_IDLE)`, conservando la semántica normal de fin de ciclo de
`SequenceBlock`.

## 7. Límites

- La receta se usa por puntero; no la reemplaces mientras el bloque está activo.
- `applyTool()` vacío significa que la máscara no acciona nada, sin generar error.
- Solo hay ocho sensores lógicos de transición.
- No se verifican colisiones, límites geométricos ni zonas prohibidas.
- `hold()` de cada eje define qué significa detenerlo; puede no retirar energía.
- Las recetas y teach-in no son funciones de seguridad.

Consulta [RecipeTypes.md](RecipeTypes.md), [`IAxis`](../core/IAxis.md) y
[SAFETY.md](../../../../SAFETY.md).
