# ControlWords.h

> Las dos palabras de 16 bits con las que se gobierna y se supervisa una estación entera.

**Ruta:** `src/core/ControlWords.h`
**Incluye:** `CoreFSM_Platform.h`
**Lo usan:** `FsmBlock.h` (códigos de error), `SequenceBlock.h` (`ST.cfgw` / `ST.stw`), `MotorDrive.h` (palabras de accionamiento), `AlarmManager.h`, `Telemetry.h`.

---

## 1. Qué problema resuelve

En automatización pesada —automoción, packaging, línea blanca— cada estación
expone al maestro de línea exactamente dos registros de 16 bits:

```
CFGW / CTLW  (Control Word)   lo que el maestro ORDENA a la estación
STW          (Status Word)    lo que la estación RESPONDE al maestro
```

Toda la conversación cabe ahí. Y eso tiene tres consecuencias que no son
teóricas:

1. **Un solo acceso transporta el estado completo.** Un registro Modbus, cuatro
   bytes en una trama serie, una posición de memoria compartida. No hacen falta
   veinte variables sueltas ni veinte direcciones de bus.
2. **El diagnóstico es inmediato.** Imprimes la STW en hexadecimal y de un
   vistazo sabes qué bit está reteniendo la máquina. Sin depurador, sin sonda.
3. **La interfaz queda congelada.** Puedes reescribir la estación entera por
   dentro; mientras respete el significado de los bits, el maestro no se entera.

La alternativa —que cada bloque exponga los booleanos que le apetezca— funciona
con una máquina y se derrumba con cinco.

## 2. Cómo funciona por dentro

### 2.1 La unión

```cpp
union ConfigWord {
  uint16_t raw;
  struct {
    uint16_t enable : 1;
    uint16_t start  : 1;
    ...
  };
};
```

Las dos vistas comparten **los mismos dos bytes de RAM**. Escribes por nombre y
lees en bloque:

```cpp
palabra.start = true;                  // cómodo de leer
Serial.println(palabra.raw, HEX);      // cómodo de transmitir
```

Cuatro `static_assert` verifican en compilación que las palabras de bloque y de
drive ocupen exactamente 16 bits. Esto detecta un ABI incompatible, aunque no
convierte el orden interno de bitfields en un formato de red portable.

### 2.2 El aviso sobre el orden de los bits

El estándar de C++ **no garantiza** en qué bit físico cae cada campo de una
estructura de bits: es *implementation-defined*. CoreFSM fija `uint16_t` como
tipo base —evita que ciertos ABI reserven cuatro bytes para campos `bool`— y
detiene la compilación si el resultado no ocupa dos bytes.

Eso valida el ABI usado por la placa, pero no crea por sí solo un formato de
red portable. Para intercambiar la palabra con otro firmware, compón y analiza
el entero mediante las máscaras explícitas `CFGW_BIT_*` / `STW_BIT_*`; no
serialices la estructura C++ en crudo.

Esta es exactamente la clase de detalle que no da error nunca hasta el día que
lo da, y entonces no hay quien lo encuentre.

### 2.3 CFGW — palabra de mando

| Bit | Máscara | Campo | Qué ordena |
|---:|---|---|---|
| 0 | `0x0001` | `enable` | Habilitación general. Sin esto no arranca nada. Perderla en marcha equivale a una parada ordenada. |
| 1 | `0x0002` | `start` | Petición de arranque de ciclo. **Se consume.** |
| 2 | `0x0004` | `stop` | Parada ordenada al terminar el ciclo actual. La atiende `completeCycle()`. |
| 3 | `0x0008` | `resetFault` | Rearme / acuse de alarma. **Se consume.** |
| 4 | `0x0010` | `singleStep` | Modo paso a paso para puesta en marcha. |
| 5 | `0x0020` | `nextStep` | En paso a paso, autoriza avanzar un paso. **Se consume.** |
| 6 | `0x0040` | `bypassTimer` | Ignorar los timeouts. **Solo para depurar.** |
| 7 | `0x0080` | `holdRequest` | Pausa en caliente conservando el paso. Es un **nivel**, no un pulso. |
| 8 | `0x0100` | `abortRequest` | Aborto inmediato a estado seguro. **Se consume.** |
| 9 | `0x0200` | `quickStop` | **ACTIVO A NIVEL BAJO.** `false` = parada rápida. |
| 10-15 | — | `reserved` | Libres para ampliaciones tuyas. |

**Qué significa "se consume".** `processControlWord()` pone el bit a `false`
nada más atenderlo. Eso convierte una señal mantenida en un pulso de un solo
scan. Sin ello, dejar `start` a `true` rearrancaría la máquina indefinidamente.

Y hay un detalle fino en `start`: **se consume siempre, también cuando no
procede**. Si se dejara enganchado esperando a que llegue la habilitación, la
máquina arrancaría sola en el instante en que alguien girase el selector a
automático, sin que nadie hubiera vuelto a pedirlo. Una orden vieja no puede
sobrevivir a la condición que la impidió.

`holdRequest` es la excepción deliberada: es un nivel. Mientras esté a `true` la
máquina sigue en pausa. Pero la reanudación se hace en su **flanco de bajada**,
no por el hecho de valer cero. Si se reanudara por nivel, cualquier pausa pedida
por la vía directa —`bloque.hold()`, la tecla `p` de la consola— se desharía
sola al scan siguiente, porque ese bit nunca llegó a subir. El botón de pausa,
sencillamente, no pausaría. Fue uno de los trece defectos que encontró la
revisión adversarial.

### 2.4 STW — palabra de estado

| Bit | Máscara | Campo | Qué responde |
|---:|---|---|---|
| 0 | `0x0001` | `ready` | Condiciones de arranque cumplidas |
| 1 | `0x0002` | `running` | Produciendo. **Estricto**: solo `STATE_RUNNING` |
| 2 | `0x0004` | `done` | Ciclo terminado con éxito |
| 3 | `0x0008` | `fault` | Alarma activa, la máquina está retenida |
| 4 | `0x0010` | `paused` | En pausa, el paso se conserva |
| 5 | `0x0020` | `stepTimeout` | Causa del fallo: venció el watchdog de paso |
| 6 | `0x0040` | `inHomePos` | La máquina está en posición de reposo |
| 7 | `0x0080` | `busy` | Ocupada; no acepta órdenes nuevas |
| 8 | `0x0100` | `waitingAck` | Espera el acuse de la estación siguiente |
| 9 | `0x0200` | `warning` | Aviso no bloqueante. Es la **unión** de los avisos concretos |
| 10 | `0x0400` | `suspended` | *(2.1)* Parada por causa **externa**. Se reanuda sola |
| 11 | `0x0800` | `held` | *(2.1)* Parada por causa **interna** o del operario |
| 12 | `0x1000` | `stepWarn` | *(2.1)* El paso pasó del tiempo de aviso, no del de fallo |
| 13-15 | — | `reserved` | Libres |

El bit `done` tiene una coreografía propia: lo levanta `completeCycle()`
**después** del cambio de paso, porque `setStep()` lo borra. Así queda a 1
mientras la máquina descansa en su paso inicial y cae en cuanto vuelve a
trabajar. El maestro de línea puede así distinguir "ciclo recién terminado" de
"ciclo en curso", que es justamente para lo que sirve.

`warning` se recalcula entero cada scan en `syncStatusWord()`:

```cpp
ST.stw.warning = ST.stw.stepWarn || _cycleWarn;
```

No se pone a mano en ningún sitio. Un bit de aviso que no baja solo cuando su
causa desaparece deja de significar nada a las dos semanas.

### 2.5 Las palabras de accionamiento

Están inspiradas en **PROFIdrive** y **CiA 402**, los dos perfiles de bus de
campo que usan prácticamente todos los variadores y servos industriales. Si
algún día conectas un variador de verdad, el mapeo es casi directo.

`DriveControlWord` (bits 0-7, resto reservado):

| Bit | Campo | Qué ordena |
|---:|---|---|
| 0 | `enable` | Habilitar la etapa de potencia |
| 1 | `runFwd` | Marcha continua directa |
| 2 | `runRev` | Marcha continua inversa |
| 3 | `jogFwd` | Impulso manual directo |
| 4 | `jogRev` | Impulso manual inverso |
| 5 | `quickStop` | **ACTIVO A BAJO** |
| 6 | `resetFault` | Rearme del accionamiento |
| 7 | `brakeRelease` | Liberar freno mecánico |

`DriveStatusWord` (bits 0-7):

| Bit | Campo | Qué responde |
|---:|---|---|
| 0 | `readyToSwitchOn` | Listo para recibir `enable` |
| 1 | `enabled` | Potencia aplicada al motor |
| 2 | `running` | El eje se está moviendo |
| 3 | `fault` | Avería (térmico, sobrecarga, bloqueo) |
| 4 | `warning` | Cerca del límite térmico |
| 5 | `fwdActive` | Girando en directo |
| 6 | `revActive` | Girando en inverso |
| 7 | `atSetpoint` | Consigna alcanzada |

### 2.6 quickStop activo a nivel bajo

Aparece en las dos palabras y merece su apartado porque parece un error y no lo
es:

```
quickStop = true   ->  NO hay parada rápida, puedes moverte
quickStop = false  ->  PARADA RÁPIDA AHORA
```

El motivo es disponer de una convención de mando cuyo valor predeterminado sea
detener. Si la aplicación sustituye una comunicación inválida por una palabra a
cero, el software ordenará la parada en el siguiente scan; no debe conservar el
último `true` indefinidamente.

Eso no garantiza que un cable roto produzca por sí solo una palabra a cero, ni
equivale a un circuito de parada de emergencia normalmente cerrado. Hace falta
detectar la pérdida mediante timeout y la seguridad real debe cablearse con
hardware apropiado. Consulta [`SAFETY.md`](../../../../SAFETY.md).

### 2.7 Códigos de error

```cpp
CFSM_ERR_NONE            = 0x0000
CFSM_ERR_STEP_TIMEOUT    = 0x0001   un paso agotó su tiempo máximo
CFSM_ERR_CYCLE_TIMEOUT   = 0x0002   el ciclo productivo agotó su tiempo
CFSM_ERR_INTERLOCK       = 0x0003   condición de enclavamiento incumplida
CFSM_ERR_ESTOP           = 0x0004   interbloqueo lógico de parada activo
CFSM_ERR_HANDSHAKE       = 0x0005   la estación vecina no responde
CFSM_ERR_DRIVE_FAULT     = 0x0006   avería propagada desde un accionamiento
CFSM_ERR_SENSOR_INVALID  = 0x0007   dos sensores excluyentes activos a la vez
CFSM_ERR_RECIPE_INVALID  = 0x0008   receta ausente, vacía o corrupta
CFSM_ERR_CONFIG_CRC      = 0x0009   configuración en memoria no volátil mal
CFSM_ERR_NOT_HOMED       = 0x000A   se pidió automático sin hacer el home
CFSM_ERR_SCAN_OVERRUN    = 0x000B   (2.1) el ciclo de scan se pasó del límite
CFSM_ERR_USER_BASE       = 0x8000   a partir de aquí, errores de tu máquina
```

`cfsmErrorText()` los traduce a texto con `CFSM_FSTR`, así que la tabla de
mensajes vive en flash y no toca la RAM.

**`CFSM_ERR_USER_BASE = 0x8000` es la mitad del rango.** Reservar los 32 768
códigos de arriba para el usuario garantiza que una versión futura de CoreFSM no
pueda chocar nunca con los tuyos.

## 3. API completa

| Símbolo | Qué es |
|---|---|
| `union ConfigWord` | CFGW. Campos + `raw` |
| `union StatusWord` | STW. Campos + `raw` |
| `union DriveControlWord` / `DriveStatusWord` | Palabras de accionamiento |
| `CFGW_BIT_*` / `STW_BIT_*` | Máscaras portables |
| `enum CfsmError` | Códigos normalizados |
| `cfsmErrorText(uint16_t)` | Texto en flash del código |

## 4. Ejemplos

### 4.1 Gobernar por bus en vez de por llamada

```cpp
estacion.ST.cfgw.enable = true;
estacion.ST.cfgw.start  = true;    // se consumirá en el próximo updateSequence()
```

Equivale a `estacion.start()`. Las dos vías conviven porque
`processControlWord()` traduce los bits a las llamadas y luego los consume.

### 4.2 Publicar el estado en cuatro bytes

```cpp
void publicarEnBus() {
  uint8_t trama[4];
  trama[0] = estacion.ST.stw.raw & 0xFF;
  trama[1] = estacion.ST.stw.raw >> 8;
  trama[2] = estacion.ST.errorCode & 0xFF;
  trama[3] = estacion.ST.errorCode >> 8;
  Serial1.write(trama, 4);
}
```

Cuatro bytes por estación, treinta veces por segundo, y el maestro sabe todo lo
que necesita saber de cada una.

### 4.3 Diagnosticar sin depurador

```cpp
Serial.print(F("STW=0x")); Serial.println(estacion.ST.stw.raw, HEX);
```

```
STW=0x0480
```

`0x0480` = bits 7 y 10 = `busy` + `suspended`. La máquina está ocupada y
esperando por una causa externa. No hace falta nada más para saber que no está
averiada: está esperando pieza.

### 4.4 Definir alarmas propias sin colisiones

```cpp
enum AlarmasPrensa : uint16_t {
  ALM_SIN_PRESION   = CFSM_ERR_USER_BASE + 1,
  ALM_MOLDE_ABIERTO = CFSM_ERR_USER_BASE + 2,
  ALM_TEMPERATURA   = CFSM_ERR_USER_BASE + 3
};

if (presionBar < 4.0) fault(ALM_SIN_PRESION);
```

### 4.5 Usar las máscaras cuando el otro extremo no es GCC

```cpp
uint16_t recibida = leerRegistroModbus(40001);
bool enMarcha = (recibida & STW_BIT_RUNNING) != 0;
bool esperando = (recibida & STW_BIT_SUSPENDED) != 0;
```

## 5. Decisiones de diseño

**Se documenta el aviso del orden de bits en vez de renunciar a la unión.** La
alternativa segura sería usar solo máscaras y `get`/`set`, pero el código
quedaría ilegible (`palabra.raw |= CFGW_BIT_START` frente a
`palabra.start = true`). Se elige la comodidad para el uso interno y se ofrecen
las máscaras para el uso externo, que es donde el riesgo existe de verdad.

**Los bits nuevos de la 2.1 van en 10, 11 y 12, sobre los reservados.** No se
reordenó nada: cualquier código o HMI que ya leyera los bits 0-9 sigue leyendo
lo mismo.

**Se reservan seis bits en CFGW y tres en STW.** No están de adorno: si un día
necesitas un bit de "modo mantenimiento" propio, lo pones ahí sin tocar la
librería y sin romper a nadie.

## 6. Errores frecuentes

**Poner `quickStop = true` creyendo que se pide una parada rápida.** Hace lo
contrario. Para parar: `quickStop = false`.

**Escribir `ST.stw.warning` a mano.** Se recalcula cada scan en
`syncStatusWord()` y tu escritura se pierde. Si quieres un aviso propio, usa
`AlarmManager` con severidad `ALARM_WARNING`.

**Dejar `bypassTimer` a `true` en producción.** Apaga todas las vigilancias de
tiempo. Está para una puesta en marcha con el técnico delante, no para
callar una alarma que molesta.

**Mantener `start` a `true` esperando que la máquina arranque cuando pueda.** No
lo hará: el bit se consume igualmente. Es deliberado, y está explicado arriba.

**Confundir `running` con `busy`.** `running` es estricto y cae en cuanto la
máquina entra en una espera; `busy` incluye arranque, parada y las dos esperas.
Para el piloto verde quieres `running`; para "no me mandes trabajo nuevo",
`busy`.

## 7. Coste

`ConfigWord` y `StatusWord`: **2 bytes cada una**. Un `SequenceBlock` lleva las
dos más `errorCode`, o sea 6 bytes de `ST` en total. Las palabras de
accionamiento, otros 2 bytes cada una dentro de `MotorDrive`.

`cfsmErrorText()` gasta flash proporcional al número de mensajes, y cero RAM
gracias a `CFSM_FSTR`.

## 8. Relación con el resto

```
   ControlWords.h
        │
        ├── FsmBlock.h          usa CfsmError y cfsmErrorText()
        │
        ├── SequenceBlock.h     ST.cfgw / ST.stw / ST.errorCode
        │      │                processControlWord() consume CFGW
        │      └                syncStatusWord() recalcula STW
        │
        ├── MotorDrive.h        DriveControlWord / DriveStatusWord
        │
        ├── AlarmManager.h      códigos de usuario sobre CFSM_ERR_USER_BASE
        │
        └── Telemetry.h         imprime STW y el texto del error
```
