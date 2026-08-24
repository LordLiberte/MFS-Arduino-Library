# Chassis.h

> Coordinadores de movimiento para dos o cuatro ruedas y un eje DC con
> realimentación de posición.

**Ruta:** `src/drive/Chassis.h`  
**Tipos:** `DifferentialChassis`, `FourWheelChassis`, `PositionAxis`.

## 1. `DifferentialChassis`

Recibe referencias a dos [`MotorDrive`](MotorDrive.md). `drive(v,w)` calcula:

```text
turn  = w × trackWidth / 10
left  = v − turn
right = v + turn
```

Si alguna magnitud supera 255, escala ambas proporcionalmente para conservar la
curvatura.

```cpp
MotorDrive izquierda(2, 4, 3);
MotorDrive derecha(7, 8, 5);
DifferentialChassis chasis(izquierda, derecha, 10);

chasis.enable();
chasis.drive(180, 30);
```

| Método | Acción |
|---|---|
| `drive(v,w)` | Avance y giro, entradas −255…255 |
| `forward(s)` / `backward(s)` | Recta |
| `spinLeft(s)` / `spinRight(s)` | Giro sobre el centro |
| `curve(speed,bias)` | Curva con bias esperado −100…100 |
| `enable()` / `disable()` / `stop()` | Difusión a ambos motores |
| `isMoving()` | Algún motor informa marcha |
| `setTrackWidth(w)` | Ganancia geométrica del giro |

`trackWidth` es un factor entero de calibración, no una medida física con unidad
incorporada.

## 2. `FourWheelChassis`

Distribuye `vx`, `vy` y `w` entre cuatro motores:

```text
FL = vx − vy − w    FR = vx + vy + w
RL = vx + vy − w    RR = vx − vy + w
```

Las cuatro consignas se normalizan con el mismo factor. Para ruedas normales
usa `drive(vx,w)`, que fija `vy=0`. `strafeLeft/Right` solo tiene sentido con
ruedas mecanum u omnidireccionales correctamente orientadas.

```cpp
FourWheelChassis base(fl, fr, rl, rr);
base.enable();
base.drive(150, 0, 40); // vx, vy, w
```

La clase no contiene odometría, encoders ni corrección por diferencias entre
motores.

## 3. `PositionAxis`

Adapta un `MotorDrive` a la interfaz [`IAxis`](../core/IAxis.md). El programa
entrega realimentación y llama `update()` una vez por scan:

```cpp
PositionAxis eje(motor);

void loop() {
  eje.setFeedback(sensor.scaled());
  eje.update();
  motor.writeOutputs();
}
```

El control proporcional usa:

```text
error = target − actual
PWM   = error × kp / 10
```

Fuera de la tolerancia, limita el PWM entre `vMin` y `vMax`; dentro de ella
ordena parar.

| Método | Significado |
|---|---|
| `setFeedback(actual)` | Actualiza posición medida |
| `moveTo(target,speed)` | Fija destino y activa el lazo |
| `update()` | Calcula la orden del motor |
| `inPosition(tolerance)` | Compara destino y realimentación |
| `reached()` | Resultado interno de la última actualización |
| `hold()` | Desactiva lazo y ordena stop |
| `enable()` / `disable()` | Control del motor |
| `tune(kp,tolerance,vMin,vMax)` | Ajuste del regulador |
| `startHoming(direction,speed)` | Inicia búsqueda de origen |
| `updateHoming(limit)` | Continúa homing; `true` al referenciar |
| `isHomed()` | Referencia completada |
| `position()` / `relativePosition()` | cruda antes de homing; `actual − homeOffset` después |

## 4. Homing

Durante homing, `update()` no gobierna el motor; `startHoming()` establece una
consigna firmada y `updateHoming()` la detiene cuando llega el final de carrera.
El programa debe seguir ejecutando la PAA del `MotorDrive`.

Al alcanzar el final, la implementación actual guarda `_homeOffset`, establece
destino cero y marca llegada. A partir de entonces `position()`,
`relativePosition()`, `inPosition()`, `error()` y el lazo usan coordenadas
relativas al origen. Antes de homing, `position()` devuelve la realimentación
cruda.

## 5. Ajuste y límites

- `kp` está en décimas: 15 significa 1,5.
- Un control P puede quedar con error bajo carga y oscilar si la ganancia es alta.
- No existen límites software de recorrido ni timeout dentro de `PositionAxis`.
- La dirección física depende del cableado de motor y sensor.
- La clase no toma una medición automáticamente: `position()` transforma, si ya
  hubo homing, la última lectura entregada a `setFeedback()`.
- La normalización cinemática conserva proporciones, pero no garantiza una
  velocidad física concreta.

La secuencia superior debe vigilar tiempos y finales de carrera. Antes de mover
un eje consulta [SAFETY.md](../../../../SAFETY.md).
