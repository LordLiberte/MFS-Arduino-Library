# TowerLight.h

> La columna luminosa. Ciento diez líneas cuyo verdadero contenido no es código: es una norma de señalización que hace que un operario que no conoce tu máquina sepa leerla desde la puerta.

**Ruta:** `src/io/TowerLight.h`
**Incluye:** `DigitalOutput.h`
**Lo usan:** `examples/03_Conveyor_Semaforo/`, `examples/08_Esperas_y_Ritmo/`, y cualquier máquina con baliza.

---

## 1. Qué problema resuelve

Una baliza mal gobernada es peor que no tener baliza. Si el rojo y el verde
pueden estar encendidos a la vez, el operario mira, no entiende, y **deja de
mirarla**. A partir de ese momento tienes una lámpara cara que no comunica nada.

Este archivo hace dos cosas: aplica el código de colores normalizado, y **hace
imposible** encender dos colores a la vez.

## 2. Cómo funciona por dentro

### 2.1 El código de colores no es arbitrario

Está normalizado (IEC 60204-1 / ANSI). Un operario que entra en una nave que no
conoce sabe leer las balizas sin preguntar, y por eso conviene respetarlo aunque
sea tu máquina y tu mando:

| Señal | Significado |
|---|---|
| **ROJO fijo** | Avería. La máquina está parada y necesita intervención |
| **ROJO intermitente** | Emergencia o situación peligrosa. Actúa ya |
| **ÁMBAR fijo** | En espera, en pausa, o lista pero sin orden de marcha |
| **ÁMBAR intermitente** | Aviso: algo va a pasar (falta material, temperatura subiendo, mantenimiento próximo). Aún produce |
| **VERDE fijo** | Produciendo con normalidad |
| **VERDE intermitente** | Arrancando o terminando ciclo |

La regla de oro es la **prioridad**: solo se enciende un color a la vez, y el
rojo gana siempre.

### 2.2 Cómo se hace imposible el error

Todos los métodos públicos pasan por el mismo sitio:

```cpp
void apply(OutputMode r, OutputMode y, OutputMode g, bool horn) {
  _red.setMode(r);
  _yellow.setMode(y);
  _green.setMode(g);
  if (_buzzer) _buzzer->setMode((horn && !_muted) ? OUT_BLINK_FAST : OUT_OFF);
  if (!horn) _muted = false;
}
```

`apply()` escribe **los tres** colores en cada llamada. No hay ninguna vía para
tocar uno solo. Por construcción, no existe un estado con dos encendidos.

Y los métodos son declarativos, no imperativos: no dices "enciende el rojo",
dices `setFault()`. La traducción a colores es cosa de la baliza.

```cpp
void setRunning()  { apply(OUT_OFF, OUT_OFF, OUT_ON,         false); }
void setStarting() { apply(OUT_OFF, OUT_OFF, OUT_BLINK_SLOW, false); }
void setIdle()     { apply(OUT_OFF, OUT_ON,  OUT_OFF,        false); }
void setSuspended(){ apply(OUT_OFF, OUT_ON,  OUT_OFF,        false); }
void setHeld()     { apply(OUT_OFF, OUT_BLINK_SLOW, OUT_OFF, false); }
void setPaused()   { apply(OUT_OFF, OUT_BLINK_SLOW, OUT_OFF, false); }
void setWarning()  { apply(OUT_OFF, OUT_BLINK_FAST, OUT_OFF, false); }
void setFault()    { apply(OUT_ON,  OUT_OFF, OUT_OFF, true);  }
void setEmergency(){ apply(OUT_BLINK_FAST, OUT_OFF, OUT_OFF, true); }
void setOff()      { apply(OUT_OFF, OUT_OFF, OUT_OFF, false); }
```

Fíjate en que `setSuspended()` y `setIdle()` dan lo mismo (ámbar fijo) y
`setHeld()` y `setPaused()` también (ámbar lento). Son métodos distintos con el
mismo cuerpo **a propósito**: el nombre documenta la intención en el sitio donde
se llama, y si algún día el criterio cambia —por ejemplo, dar al `HELD` un ritmo
propio— se toca aquí y no en cada máquina.

### 2.3 Los pilotos son referencias, no pines

```cpp
TowerLight(DigitalOutput& red, DigitalOutput& yellow, DigitalOutput& green,
           DigitalOutput* buzzer = nullptr)
```

Se pasan por referencia: son `DigitalOutput` normales que **también están
registrados en el `DeviceManager`**. La baliza solo decide **qué modo** tiene
cada uno; quien los escribe físicamente sigue siendo la fase PAA.

Eso mantiene la separación de fases intacta y tiene dos consecuencias prácticas:
los pilotos de la baliza se pueden forzar como cualquier otra salida, y aparecen
en `printIoTable()` igual que las demás.

El zumbador es un **puntero** y no una referencia, porque es opcional. Todo el
código que lo usa comprueba `if (_buzzer)` antes.

### 2.4 El silenciado del zumbador

```cpp
void muteBuzzer() { if (_buzzer) _buzzer->turnOff(); _muted = true; }
void unmute()     { _muted = false; }
```

Es lo que hace el botón de "acuse acústico" de un cuadro: **el operario se ha
enterado, pero la alarma sigue activa y la luz roja debe seguir encendida.**
Silenciar no es acusar la alarma, y menos aún resolverla.

Y hay un detalle en `apply()` que importa:

```cpp
if (!horn) _muted = false;
```

Cualquier cambio de estado que **no** pida bocina rearma el silenciado. Es decir:
silencias una alarma, la máquina se rearma y vuelve a fallar más tarde — y suena
otra vez. Una alarma nueva no hereda el silencio de la anterior. Sin esta línea,
un `muteBuzzer()` de la mañana dejaría la máquina muda el resto del día.

### 2.5 `reflect()`, la baliza en una línea

```cpp
void reflect(uint8_t systemState, bool emergency = false, bool warning = false) {
  if (emergency) { setEmergency(); return; }
  switch (systemState) {
    case 6 /*STATE_ERROR*/:     setFault();    break;
    case 2 /*STATE_RUNNING*/:   warning ? setWarning() : setRunning(); break;
    case 3 /*STATE_PAUSED*/:    setPaused();   break;
    case 7 /*STATE_SUSPENDED*/: setSuspended();break;
    case 8 /*STATE_HELD*/:      setHeld();     break;
    case 1 /*STATE_STARTING*/:
    case 4 /*STATE_STOPPING*/:  setStarting(); break;
    default:                    setIdle();     break;
  }
}
```

Traduce el `SystemState` de cualquier `FsmBlock` al color que corresponde. Con
esto, la baliza de tu máquina se resuelve en **una sola línea del `loop()`** y
siempre dice la verdad, sin cadenas de `if` repartidas que alguien olvida
actualizar.

**Por qué números y no las constantes.** El `switch` usa `6`, `2`, `3`… con el
nombre en comentario en vez de `STATE_ERROR`. Es porque `TowerLight.h` está en
`io/` e incluye solo `DigitalOutput.h`: **no depende de `core/`**. Meter un
`#include "../core/FsmBlock.h"` para tres constantes ataría la capa de hardware a
la capa de lógica, que es exactamente la separación que la librería mantiene con
cuidado. El precio es este comentario y el riesgo de que alguien renumere el
`enum`; el beneficio es que la baliza sigue siendo un objeto de campo puro.

El `emergency` va **antes del `switch`** y sale por `return`: la emergencia gana
a cualquier estado, sin excepciones.

### 2.6 La prueba de lámparas

```cpp
void lampTest() { apply(OUT_ON, OUT_ON, OUT_ON, false); }
```

Es la única llamada que enciende los tres a la vez. Una prueba de lámparas puede
formar parte del diagnóstico exigido por la aplicación, pero la clase no detecta
por sí sola un piloto fundido ni acredita el cumplimiento de una norma. Si se
usa al arrancar, el operario puede comprobar visualmente los tres colores.

## 3. API completa

| Método | Colores | Cuándo |
|---|---|---|
| `setRunning()` | Verde fijo | Produciendo |
| `setStarting()` | Verde lento | Arrancando o parando |
| `setIdle()` | Ámbar fijo | En reposo, sin orden |
| `setSuspended()` | Ámbar fijo | Esperando por causa externa |
| `setHeld()` | Ámbar lento | Esperando al operario |
| `setPaused()` | Ámbar lento | En pausa |
| `setWarning()` | Ámbar rápido | Aviso, aún produce |
| `setFault()` | Rojo fijo + bocina | Avería |
| `setEmergency()` | Rojo rápido + bocina | Emergencia |
| `setOff()` | Todo apagado | |
| `lampTest()` | Los tres fijos | Prueba de lámparas |
| `reflect(estado, emergencia, aviso)` | — | Traduce un `SystemState` |
| `muteBuzzer()` / `unmute()` | — | Acuse acústico |

## 4. Ejemplos

### 4.1 La baliza completa en una línea

```cpp
DigitalOutput ledRojo(9), ledAmbar(10), ledVerde(11), zumbador(8);
TowerLight    baliza(ledRojo, ledAmbar, ledVerde, &zumbador);

void setup() {
  io.registerDevice(&ledRojo,  F("ROJO"));
  io.registerDevice(&ledAmbar, F("AMBAR"));
  io.registerDevice(&ledVerde, F("VERDE"));
  io.registerDevice(&zumbador, F("ZUMBADOR"));
  io.beginAll();

  baliza.lampTest();          // prueba de lamparas
  delay(1000);                // aceptable AQUI: aun no hay ciclo de scan
  baliza.setOff();
}

void loop() {
  io.readAllInputs();
  manager.updateAll();

  baliza.reflect(maquina.getState(),
                 manager.isEmergencyStop(),
                 maquina.ST.stw.warning);

  io.writeAllOutputs();
}
```

Ese `delay(1000)` del `setup()` es la única excepción legítima de la librería:
todavía no ha empezado el ciclo de scan, así que no hay nada que retrasar.

### 4.2 Los cinco casos que ahora se distinguen

Con los estados de espera de la 2.1, la baliza dice cosas distintas para
situaciones distintas:

```
VERDE fijo        la maquina esta produciendo
VERDE lento       arrancando o parando ordenadamente
AMBAR fijo        no llega pieza. Esta sana, arrancara sola
AMBAR lento       te esta esperando a ti: recarga, control de calidad
AMBAR rapido      va lenta o hay un aviso, pero sigue produciendo
ROJO fijo         averia
ROJO rapido       emergencia
```

Antes de separar `SUSPENDED` de `HELD`, los dos primeros ámbares eran el mismo y
el operario no podía saber desde lejos si la máquina le reclamaba o simplemente
esperaba material.

### 4.3 El botón de acuse acústico

```cpp
if (btnAcuseAcustico.hasRisen()) {
  baliza.muteBuzzer();          // calla la bocina
  /* la luz roja SIGUE encendida y la alarma SIGUE activa */
}

if (btnRearme.hasRisen()) {
  manager.resetAll();           // esto sí intenta resolver la alarma
  alarmas.ackAll();
}
```

Dos botones distintos para dos cosas distintas. Juntarlos —silenciar y rearmar
con la misma tecla— es un vicio habitual y hace que la gente rearme sin mirar.

### 4.4 Una baliza sin zumbador

```cpp
TowerLight baliza(ledRojo, ledAmbar, ledVerde);   // sin cuarto argumento
```

Todo funciona igual; `muteBuzzer()` simplemente no hace nada audible.

## 5. Decisiones de diseño

**Métodos declarativos y no acceso a los colores.** No hay `setRed(bool)`. Es lo
que hace imposible la baliza incoherente. Si algún día necesitas un color suelto,
tienes los `DigitalOutput` a mano — pero entonces estás saltándote la baliza a
propósito, que es distinto de hacerlo por descuido.

**Números literales en `reflect()` en vez de las constantes de `core/`.** Ver
§2.5. Es la decisión más discutible del archivo y está tomada a conciencia.

**Métodos distintos con el mismo cuerpo.** `setIdle()` y `setSuspended()` hacen
lo mismo hoy. Ver §2.2.

**El silenciado se rearma solo.** Ver §2.4. La alternativa —que el silencio dure
hasta que alguien lo quite— produce máquinas mudas.

**No hereda de `IDevice`.** No tiene pines propios: gobierna tres objetos que sí
los tienen. Registrarla en el `DeviceManager` no tendría sentido porque no hay
nada que leer ni escribir en su fase.

## 6. Errores frecuentes

**No registrar los pilotos en el `DeviceManager`.** La baliza les cambia el modo
y nadie escribe los pines. No se enciende nada y no hay error.

**Llamar a `reflect()` antes de `updateAll()`.** Reflejas el estado del scan
anterior. Poca cosa, pero en una transición rápida se nota.

**Inventarse el código de colores.** Verde para "esperando" y ámbar para
"produciendo" funciona en tu cabeza y en la de nadie más. La norma existe para
que la máquina se lea sin manual.

**Saltarse la prueba de lámparas.** Un rojo fundido no se descubre nunca, porque
justo cuando haría falta es cuando no está.

**Usar `muteBuzzer()` como si fuera un acuse de alarma.** Silencia y ya. La
alarma sigue activa y la máquina sigue parada.

## 7. Coste

| Miembro | Bytes |
|---|---:|
| Tres referencias (`_red`, `_yellow`, `_green`) | 6 |
| `_buzzer` (puntero) | 2 |
| `_muted` | 1 |

**9 bytes**, más los cuatro `DigitalOutput` que gobierna (unos 25 cada uno, pero
esos los pagas de todos modos porque son salidas reales). Sin vtable: no es
polimórfica.

## 8. Relación con el resto

```
   FsmBlock::getState()  ──┐
   manager.isEmergencyStop()├──▶ baliza.reflect(...)
   ST.stw.warning         ──┘         │
                                      ▼
                                  apply(r, y, g, horn)
                                      │
                    ┌─────────┬───────┴────┬──────────┐
                    ▼         ▼            ▼          ▼
              DigitalOutput  DigitalOutput  ...   zumbador (opcional)
                    │
                    └──▶ DeviceManager ──▶ fase PAA

   TowerLight NO depende de core/: solo incluye DigitalOutput.h.
   Por eso reflect() usa números y no las constantes de SystemState.
```
