# RecipeTypes.h

> Modelo POD de recetas y banco que mantiene una receta activa en RAM, recetas
> de fábrica en flash y ranuras editables en memoria no volátil.

**Ruta:** `src/data/RecipeTypes.h`  
**Depende de:** [`ConfigStore`](ConfigStore.md).

## 1. Dimensiones de compilación

Define antes de incluir la librería:

```cpp
#define CFSM_RECIPE_AXES 2
#define CFSM_RECIPE_MAX_STEPS 6
#define CFSM_RECIPE_NAME_LEN 16
#include <CoreFSM.h>
```

Estos valores forman parte del layout binario y cambian RAM, flash y tamaño de
cada ranura NVM.

## 2. Estructuras

| Tipo | Contenido |
|---|---|
| `AxisMotion` | target, speed, tolerance, enabled |
| `ToolAction` | máscara de 8 salidas, settle y feedback requerido |
| `StepTransition` | trigger, dwell, timeout y sensorId |
| `RecipeStep` | array de ejes + herramienta + transición |
| `RecipeHeader` | id, nombre fijo, totalSteps y maxCycleMs |
| `RecipeRecord` | cabecera + array fijo de pasos |

No tienen inicializadores de miembro para conservar agregados C++11. Una
variable local sin inicialización contiene basura. Usa llaves completas o:

```cpp
RecipeRecord receta;
cfsmClearRecipe(receta);
```

## 3. Disparadores

| Valor | Condición prevista en `RecipeExecutor` |
|---|---|
| `TRIG_POSITION` | todos los ejes activos en posición |
| `TRIG_TIMER` | `dwellMs` transcurrido |
| `TRIG_SENSOR` | bit `sensorId` activo |
| `TRIG_POSITION_TIME` | posición y tiempo |
| `TRIG_MANUAL` | orden `advanceManual()` |

`timeoutMs=0` desactiva la vigilancia del paso; úsalo solo de forma deliberada.

## 4. Recetas de fábrica

```cpp
const RecipeRecord RECETAS[] CFSM_PROGMEM = {
  // inicialización agregada completa
};

RecipeBank<2, 256> banco;
banco.setFactoryTable(RECETAS, CFSM_ARRAY_LEN(RECETAS));
banco.loadFactory(0);
```

En AVR, `loadFactory()` usa `memcpy_P`; en memoria unificada usa `memcpy`.

## 5. Ranuras NVM

Cada ranura guarda `CfsmStoreHeader + RecipeRecord`. La clase comprueba que el
slot solicitado cabe en `EEPROM.length()`.

| Método | Función |
|---|---|
| `loadSlot(i)` / `saveSlot(i)` | Carga/guarda receta activa |
| `usableSlots()` | Ranuras que caben realmente |
| `nvmFootprint()` / `nvmEndAddress()` | Reserva teórica configurada |
| `loadFactory(i)` / `loadFactoryById(id)` | Copia desde flash |
| `hasActive()` / `activeName()` | Estado de activa |
| `validateActive()` | Verifica actualmente `totalSteps` |

`NVM_SLOTS` es capacidad solicitada; `usableSlots()` puede ser menor.

## 6. Límites de validación

La validación histórica solo comprueba que `totalSteps` esté entre 1 y
`CFSM_RECIPE_MAX_STEPS`. No garantiza:

- terminación NUL del nombre;
- trigger dentro del enum;
- `sensorId < 8`;
- tolerancia, velocidad o timeout razonables;
- coherencia entre ejes habilitados y ejes conectados;
- una versión de esquema específica del `RecipeRecord`.

La cabecera NVM lleva `version=1`; comprueba si tu versión de `loadSlot()` la
valida antes de confiar en migraciones de formato.

## 7. Inicialización de EEPROM

En plataformas con EEPROM emulada, confirma que se llamó `EEPROM.begin()` antes
de `loadSlot/saveSlot`. Las versiones de `RecipeBank` sin método `begin()`
dependen de que otro componente haya inicializado el subsistema.

No cambies `banco.active` mientras un [`RecipeExecutor`](RecipeExecutor.md) está
ejecutándola; el ejecutor conserva un puntero a ese objeto.

