# AlarmManager.h

> Lista estática de alarmas con severidad, estado activo, acuse, marcas de
> tiempo y contador de apariciones.

**Ruta:** `src/data/AlarmManager.h`  
**Tipos:** `AlarmSeverity`, `AlarmEntry`, `AlarmManager<MAX_ALARMS>`.

## 1. Ciclo de vida

Una entrada puede estar:

| Estado | Significado |
|---|---|
| activa, sin acusar | causa presente y nueva |
| activa, acusada | vista, pero la causa continúa |
| inactiva, sin acusar | causa desapareció antes de ser vista |
| inactiva, acusada | puede purgarse de la lista |

Una reaparición incrementa `count`, actualiza `lastTime` y exige un acuse nuevo.
`firstTime` conserva la primera aparición mientras la entrada permanezca en la
lista.

## 2. Severidades

`ALARM_INFO`, `ALARM_WARNING`, `ALARM_FAULT` y `ALARM_CRITICAL` se ordenan de 0
a 3. `hasBlocking()` considera bloqueantes solo alarmas activas con severidad
`FAULT` o superior.

## 3. Uso cíclico

```cpp
AlarmManager<8> alarmas;

void loop() {
  alarmas.raiseIf(sensorRoto,
                  CFSM_ERR_SENSOR_INVALID,
                  F("Sensor de presión inválido"),
                  ALARM_FAULT);

  if (alarmas.hasBlocking()) proceso.fault();
}
```

El texto se guarda como puntero; usa `F("...")` o una cadena con vida estática,
no un buffer local temporal.

## 4. API

| Método | Acción |
|---|---|
| `raise(code,text,severity)` | Crea, reactiva o refresca una alarma |
| `clear(code)` | Marca la causa inactiva |
| `raiseIf(condition,...)` | Raise o clear en cada scan |
| `ack(code)` / `ackAll()` | Acusa y purga entradas resueltas |
| `isActive(code)` | Consulta por código |
| `hasActive()` | Alguna causa presente |
| `hasBlocking()` | Alguna activa con severidad ≥ fault |
| `hasUnacked()` | Alguna entrada pendiente de acuse |
| `highestSeverity()` | Mayor severidad activa |
| `mostSevere()` | Puntero a una activa de mayor severidad |
| `count()` / `at(i)` | Recorrido de la lista |
| `overflowed()` | Se intentó superar la capacidad |
| `clearAll()` | Borra lista y overflow sin conservar historial |
| `printAll(Print&)` | Tabla legible |

## 5. Capacidad y overflow

La lista es un array fijo de `MAX_ALARMS`. Si se llena, `raise()` devuelve
`false` y enclava `overflowed()`. El sistema debe tratar una lista llena como un
diagnóstico relevante: pueden existir fallos que ya no se registraron.

Los punteros devueltos por `at()` y `mostSevere()` pueden quedar inválidos tras
un acuse que compacte el array.

## 6. Acuse y limpieza

Una entrada se purga cuando queda simultáneamente inactiva y acusada, con
independencia del orden: `clear()` seguido de `ack()`, o `ack()` mientras sigue
activa seguido de `clear()`. La compactación puede mover las entradas
posteriores, por lo que no conserves punteros obtenidos antes de esas llamadas.

## 7. Límites

- La lista vive en RAM y se pierde al reiniciar.
- No hay cola cronológica separada ni usuario asociado al acuse.
- `lastTime` se refresca en cada `raise()` mientras la alarma está activa; no es
  necesariamente la hora de una nueva aparición.
- Los códigos deben ser únicos; reutilizar uno mezcla causas diferentes.
- `clearAll()` puede ocultar fallos y debe reservarse a mantenimiento controlado.

Para parada física y funciones certificadas consulta [SAFETY.md](../../../../SAFETY.md).
