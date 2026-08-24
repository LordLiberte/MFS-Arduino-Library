# MotorDrive.h

> Mando de un motor DC mediante puente en H, con habilitación, palabras de
> control/estado, rampa y tiempo muerto de inversión.

**Ruta:** `src/drive/MotorDrive.h`  
**Hereda:** [`IDevice`](../io/IDevice.md)  
**Incluye:** `ControlWords.h`, `Timers.h`.

## 1. Modelo de ejecución

Los métodos de mando modifican `ST.cfgw` y la consigna. Los pines solo se tocan
en `writeOutputs()`, durante la PAA. Por tanto, una orden no se aplica hasta que
el programa llama `DeviceManager::writeAllOutputs()` o `writeOutputs()`
directamente.

```cpp
MotorDrive motor(5, 6, 9); // IN1, IN2, PWM

void setup() {
  motor.begin();
  motor.enable();
  motor.setRamp(4);
}

void loop() {
  motor.runForward(180);
  motor.writeOutputs();
}
```

El constructor actual representa un driver con dos entradas de sentido y una
entrada PWM. No hay una API pública específica para drivers `DIR + PWM`.

## 2. Estado público `ST`

| Campo | Tipo | Uso |
|---|---|---|
| `ST.cfgw` | `DriveControlWord` | enable, sentidos, jog, quick-stop y rearme |
| `ST.stw` | `DriveStatusWord` | listo, habilitado, marcha, sentido, aviso y fallo |
| `ST.setpointSpeed` | `uint8_t` | consigna 0…255 |
| `ST.actualSpeed` | `uint8_t` | PWM aplicado tras rampa/protecciones |
| `ST.errorCode` | `uint16_t` | primera avería enclavada |

`quickStop` es activo a nivel bajo: al terminar `begin()` vale `true`, que
significa «sin parada rápida».

## 3. Protecciones

### Orden contradictoria

Si avance y retroceso están pedidos a la vez, ambos se cancelan y
`ST.stw.warning` se activa. La clase no intenta adivinar un sentido.

### Inversión

Al invertir con el motor en marcha:

1. frena y pone PWM a cero en el estado interno;
2. espera `_deadTimeMs` —30 ms por defecto—;
3. aplica el sentido que siga vigente.

Retirar o cambiar otra vez la orden durante la ventana cancela o reinicia la
inversión pendiente.

Durante toda la ventana, `running`, `fwdActive`, `revActive` y `atSetpoint`
permanecen a cero: la palabra de estado describe el puente frenado, no la
dirección que estaba activa antes de invertir.

### Rampa

`setRamp(stepPerMs)` limita el cambio de PWM a un escalón por milisegundo. Cero
desactiva la rampa. La temporización usa resta sin signo y tolera el
desbordamiento de `millis()`.

### Fallo, enable y quick-stop

Si hay fallo, falta enable o quick-stop está activo, `writeOutputs()` ejecuta
coast, borra sentidos y consignas y deja la velocidad a cero. `resetFault()`
genera una petición que se consume en la siguiente PAA; no queda latente.

## 4. API

| Método | Efecto |
|---|---|
| `begin()` | Configura pines, palabras y salida libre |
| `enable()` / `disable()` | Habilitación lógica del driver |
| `runForward(v)` / `runReverse(v)` | Orden de sentido y velocidad |
| `setSignedSpeed(v)` | −255…255; signo = sentido |
| `stop()` | Retira órdenes de marcha y jog |
| `quickStop(active=true)` | Activa/desactiva parada rápida |
| `fault(code)` | Enclava la primera avería |
| `resetFault()` | Solicita rearme en la siguiente PAA |
| `enterSafeState()` | Borra órdenes, consignas y sentidos; aplica coast inmediatamente |
| `setRamp(step)` | Rampa PWM por milisegundo |
| `setDeadTime(ms)` | Tiempo de inversión |
| `isEnabled()` / `isRunning()` / `isFaulted()` | Consultas de STW |
| `speed()` | Velocidad aplicada |
| `enterSafeState()` | Borra órdenes y lleva GPIO a coast inmediatamente |
| `describe(Print&)` | Estado legible |

`stop()` es una parada por rampa si esta está configurada. `disable()`,
`quickStop()` y `enterSafeState()` cortan la orden mediante ramas distintas.

## 5. Frenado y rueda libre

`coast()` pone IN1/IN2 a LOW y PWM a 0: el motor queda en rueda libre.
`brake()` pone IN1/IN2 a LOW y PWM a 255 durante la inversión. Comprueba que esa
tabla de verdad corresponde a tu driver; no todos los puentes interpretan las
mismas combinaciones de forma idéntica.

## 6. Límites y errores frecuentes

- Los pines son GPIO nativos `uint8_t`; esta clase no usa expansores digitales.
- El estado seguro software es coast; si la aplicación requiere frenado,
  retirada de energía o retención controlada debe resolverlo en el diseño del
  accionamiento y el análisis de riesgos.
- No mide corriente, velocidad real, temperatura ni fallo físico del driver.
- `actualSpeed` es PWM aplicado, no velocidad mecánica.
- La rampa no sustituye un lazo de velocidad ni un limitador de corriente.
- Llama `writeOutputs()` en cada scan; de lo contrario las palabras cambian,
  pero el hardware no.
- Alimenta motores desde una fuente adecuada y une referencias solo según el
  diseño eléctrico. Nunca desde el pin de 5 V de la placa.

## 7. Relación

[`DifferentialChassis`, `FourWheelChassis` y `PositionAxis`](Chassis.md) mandan
uno o varios `MotorDrive`. Las palabras se describen en
[ControlWords.md](../core/ControlWords.md). Las limitaciones de seguridad están
en [SAFETY.md](../../../../SAFETY.md).
