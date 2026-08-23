# UltrasonicSensor.h

> Un HC-SR04 con filtro de mediana y control de cadencia. Y el sensor que más puede alargarte el ciclo de scan, dicho sin adornos.

**Ruta:** `src/io/UltrasonicSensor.h`
**Incluye:** `IDevice.h`
**Lo usan:** `examples/06_Robot_4Ruedas/`, y tu código si tienes un sonar.

---

## 1. Qué problema resuelve

Un HC-SR04 conectado a pelo tiene tres problemas:

1. **Miente de vez en cuando.** Una superficie inclinada desvía el eco, una
   esquina lo rebota dos veces, y de pronto el sensor dice 12 cm en mitad de un
   pasillo vacío.
2. **Confunde "muy lejos" con "muy cerca".** Cuando no vuelve eco, `pulseIn()`
   devuelve 0. Interpretado como distancia, eso es **cero centímetros**, o sea
   lo contrario de lo que pasa.
3. **Bloquea el programa.** `pulseIn()` es una espera activa.

Los tres se abordan aquí, y el tercero merece leerse con atención porque no se
resuelve del todo.

## 2. Cómo funciona por dentro

### 2.1 La parte incómoda, dicha claramente

**`pulseIn()` es bloqueante: se queda esperando el eco.** Con el timeout por
defecto de esta clase (20 000 µs, unos 3,4 metros), un obstáculo lejano o
ausente puede **detener el ciclo de scan 20 milisegundos enteros**.

Para poner eso en contexto: un programa CoreFSM normal escanea en 1-2 ms. Un
sonar sin eco multiplica el peor caso por diez. Con `ScanWatchdog` puesto a
20 ms, ese sensor solo ya te come el límite entero.

Se mitiga de dos formas, y conviene entender las dos:

**1. No se mide en cada scan.**

```cpp
if (cfsm_elapsed(_last) < _interval) return;
_last = cfsm_millis();
```

Entre medidas hay un intervalo mínimo, 60 ms por defecto. Y no es solo por
rendimiento: **el propio sensor necesita unos 50 ms** para que se apaguen los
ecos de la medida anterior. Medir más rápido da lecturas falsas por eco
residual. Así que el intervalo es a la vez una optimización y un requisito
físico.

El efecto práctico: de cada ~60 scans, uno es largo y los demás cuestan lo que
cuesta la comparación de arriba, es decir nada.

**2. El timeout es corto y se ajusta al alcance útil.**

```
timeoutUs = 6000    ->  ~1 metro    ->  peor caso 6 ms
timeoutUs = 20000   ->  ~3,4 metros ->  peor caso 20 ms
```

Si en tu robot nada importa más allá de un metro, poner 6000 baja el peor caso a
la tercera parte. Es el ajuste que más rendimiento da y el que nadie toca.

**Y si esos milisegundos son inaceptables** —un eje rápido, una seguridad
exigente— la solución de verdad es medir por **interrupción de cambio de pin**,
que no bloquea nada: se dispara el trigger, se vuelve, y una rutina de
interrupción anota el tiempo cuando llega el eco. Es más código y consume una
interrupción; para un robot móvil, el enfoque de aquí sobra.

Esto es honestidad de diseño: la clase no pretende que el problema no exista, lo
acota y te dice dónde está la salida si te hace falta.

### 2.2 El disparo

```cpp
digitalWrite(_trig, LOW);
delayMicroseconds(3);
digitalWrite(_trig, HIGH);
delayMicroseconds(10);
digitalWrite(_trig, LOW);
```

Los 10 µs de pulso alto son lo que pide la hoja de características del HC-SR04.
Los 3 µs de LOW previo aseguran un flanco limpio si el pin venía de un estado
indeterminado. Son 13 µs de `delayMicroseconds` dentro de la fase PAE — a esta
escala es aceptable, y no hay forma de evitarlo sin temporizadores por hardware.

### 2.3 El cero que no es cero

```cpp
uint32_t us = pulseIn(_echo, HIGH, _timeout);

uint16_t cm = (us == 0) ? CFSM_ULTRASONIC_FAR : (uint16_t)(us / 58);
```

**`0` significa "no volvió ningún eco".** No es distancia cero: es "no hay nada
dentro del alcance", que es exactamente lo contrario. Confundir ambos hace que
**el robot frene en campo abierto**, que es uno de los comportamientos más
desconcertantes que puede tener.

Por eso hay una constante explícita:

```cpp
static const uint16_t CFSM_ULTRASONIC_FAR = 999;
```

y un método para preguntarlo directamente: `hasEcho()`.

El `/58` es la conversión estándar: el sonido va a unos 343 m/s, el eco recorre
el camino de ida y vuelta, y 58 µs por centímetro sale de ahí. Es división
entera, así que la resolución es de 1 cm — más que suficiente para un sensor
cuya precisión real ronda los ±3 cm.

### 2.4 El filtro de mediana de tres

```cpp
_h[2] = _h[1]; _h[1] = _h[0]; _h[0] = cm;
_distance = median3(_h[0], _h[1], _h[2]);
```

Los ultrasonidos dan lecturas disparatadas de vez en cuando. **La media
aritmética no ayuda** porque un valor absurdo la arrastra:

```
medidas:  80, 82, 12        (la de 12 es un eco desviado)
media:    58                <- inventada, no corresponde a nada
mediana:  80                <- correcta
```

La mediana de tres **descarta el valor raro por completo**, que es justo lo que
se necesita, y cuesta tres comparaciones. Es una **red de ordenación** de tres
elementos: tres comparaciones fijas, sin bucles ni ramas impredecibles. Ordena
los tres valores y devuelve el del medio.

El precio: la respuesta tarda hasta tres medidas (unos 180 ms con el intervalo
por defecto) en reflejar un cambio real. Para un robot que avanza a 20 cm/s, son
4 cm de retraso. Tenlo en cuenta al elegir el umbral de frenado.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| constructor | `UltrasonicSensor(uint8_t trig, uint8_t echo, uint16_t intervalMs = 60, uint16_t timeoutUs = 20000)` | |
| `cm()` | `uint16_t` | Distancia filtrada. `999` = nada a la vista |
| `isClear(umbral)` | `bool` | ¿Más lejos que el umbral? |
| `isObstacle(umbral)` | `bool` | ¿Más cerca o igual? |
| `hasEcho()` | `bool` | ¿Volvió eco? |
| `force(distanceCm)` | | Impone una distancia |
| `describe(Print&)` | | `[SONAR]=42cm` o `[SONAR]=---` |
| `CFSM_ULTRASONIC_FAR` | `static const uint16_t` | 999 |

## 4. Ejemplos

### 4.1 Un robot que esquiva

```cpp
UltrasonicSensor sonar(12, 11, 60, 6000);   // 1 metro de alcance util

void loop() {
  scan.begin();
  io.readAllInputs();

  robot.obstaculoCerca = sonar.isObstacle(25);   // 25 cm
  robot.distancia      = sonar.cm();

  manager.updateAll();
  ...
  scan.end();
}
```

Y en la lógica:

```cpp
case AVANZAR:
  chasis.avanzar(velocidad);
  /* isObstacle() ya es false cuando no hay eco, porque 999 > 25. */
  if (obstaculoCerca) setStep(RETROCEDER, 1500);
  break;
```

### 4.2 Distinguir "despejado" de "sensor muerto"

```cpp
case COMPROBAR_SENSOR:
  /* Si el sonar nunca devuelve eco ni siquiera apuntando a la pared de
   * calibración, está desconectado o quemado. */
  if (!sonar.hasEcho() && getTimeInStep() > 2000) {
    fault(ALM_SONAR_MUDO);
    break;
  }
  if (sonar.cm() < 60) setStep(LISTO);
  break;
```

Sin `hasEcho()`, un sonar desconectado se comporta exactamente igual que un
pasillo despejado. Con él, la máquina puede saber que no está viendo.

### 4.3 Medir lo que te cuesta de verdad

```cpp
void informe() {
  Serial.print(F("loop max=")); Serial.print(scan.maxUs());
  Serial.print(F("us   OB1 max=")); Serial.print(manager.maxScanTimeUs());
  Serial.println(F("us"));
}
```

```
loop max=6840us   OB1 max=1120us
```

Los 5,7 ms de diferencia son el sonar esperando un eco que no llegó. Si con el
timeout a 20 000 vieras 21 ms de diferencia, ya sabes exactamente dónde tocar.

### 4.4 Probar la esquiva sin mover el robot

```cpp
if (c == 'c') sonar.force(10);                                    // obstáculo
if (c == 'l') sonar.force(UltrasonicSensor::CFSM_ULTRASONIC_FAR); // despejado
if (c == 'r') sonar.releaseForce();
```

Con el forzado puesto, `readInputs()` sale por la primera línea y **ni siquiera
dispara el sensor**: el scan vuelve a ser corto. Además de probar la lógica,
sirve para confirmar que el sonar era el culpable del scan largo.

## 5. Decisiones de diseño

**`pulseIn()` bloqueante y no interrupciones.** Es la decisión discutible del
archivo, y está documentada como tal en el propio código. Las interrupciones
darían un scan constante, a cambio de consumir una interrupción de cambio de pin
(que en un ATmega328 son un recurso escaso y compartido por puerto) y de bastante
más complejidad. Para el caso de uso previsto —un robot móvil— el bloqueo acotado
sale más barato. Para un eje rápido, no.

**Mediana de tres y no de cinco.** Cinco filtraría mejor y costaría cuatro bytes
más y una latencia de cinco medidas, o sea 300 ms. A esa velocidad el filtro
empezaría a ser más peligroso que el ruido.

**`FAR = 999` y no `0xFFFF`.** Cabe en tres dígitos al imprimirlo, y como
distancia en centímetros son casi diez metros: cualquier umbral razonable
(`isObstacle(50)`) da `false` sin necesitar un caso especial. Un valor centinela
que se comporta bien en las comparaciones normales evita la mitad de los errores.

**El intervalo entre medidas está en el sensor y no en el usuario.** Podría
haberse dejado que cada uno llamase cuando quisiera. Ponerlo dentro garantiza que
nadie viole el tiempo de recuperación del transductor por accidente.

## 6. Errores frecuentes

**Tratar `cm() == 0` como "muy cerca".** Nunca vale 0: cuando no hay eco vale
999. Pero si vienes de código escrito a pelo con `pulseIn()`, es la trampa en la
que ya has caído.

**Dejar el timeout por defecto en 20 000 µs sin necesitarlo.** Estás regalando
14 ms de peor caso en el scan. Ajústalo a tu alcance útil.

**Bajar `intervalMs` por debajo de 50.** Lecturas falsas por eco residual del
disparo anterior, y no hay forma de distinguirlas de lecturas buenas.

**Poner dos sonares mirando al mismo sitio.** Se oyen entre ellos. Si necesitas
varios, dispáralos alternando (uno cada intervalo) en vez de a la vez.

**Olvidar que el peor caso está en el scan.** Un sonar más un `Serial.println()`
en el sitio equivocado y ya tienes 30 ms de ciclo, antirrebotes perdiendo
flancos, y una máquina que "a veces no detecta el pulsador".

## 7. Coste

| Miembro | Bytes |
|---|---:|
| `_trig`, `_echo` | 2 |
| `_interval`, `_timeout` | 4 |
| `_last` | 4 |
| `_distance` | 2 |
| `_h[3]` | 6 |
| `_simValue` | 2 |
| `IDevice` | 5 |

**Unos 25 bytes.** El coste que importa de este objeto no es la RAM: es el tiempo
de scan. Ver §2.1.

## 8. Relación con el resto

```
   HC-SR04
     trig ◀── pulso de 10 us
     echo ──▶ pulseIn()  ← BLOQUEA hasta _timeout
                 │  fase PAE, una vez cada _interval ms
                 ▼
        us == 0 ? FAR : us/58
                 ▼
        mediana de las 3 ultimas  ──▶ cm() / isObstacle() / hasEcho()
                 │
                 └── es un IDevice ──▶ DeviceManager ──▶ HW.readInputs()

   Y donde se nota:  diag/ScanWatchdog  ──  el peor caso del loop()
```

`IOTable.h` **no** genera ultrasonidos: sus tablas solo cubren DI, DO y AI. Un
sonar se declara a mano y se registra en el `DeviceManager` con
`registerDevice()`.
