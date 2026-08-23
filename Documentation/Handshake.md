# Handshake.h

> Cuatro booleanos y una danza de cuatro pasos para que dos estaciones se pasen una pieza sin que ninguna pueda adelantarse.

**Ruta:** `src/core/Handshake.h`
**Incluye:** `CoreFSM_Platform.h`
**Lo usan:** `SequenceBlock.h` (cada bloque lleva uno público), `examples/04_DosEstaciones_Handshake/`.

---

## 1. Qué problema resuelve

Dos estaciones en línea. A termina una pieza y B tiene que recogerla. La forma
ingenua es una variable compartida:

```cpp
if (A.terminada) { B.empezar(); A.terminada = false; }   // MAL
```

Falla de tres maneras distintas, y las tres pasan en máquinas reales:

1. **B no estaba lista.** A bajó el aviso igualmente y la pieza se pierde: nadie
   sabe que existió.
2. **B se enteró dos veces.** Si A tarda un scan en bajar el aviso y B lo mira en
   los dos scans, procesa la misma pieza dos veces.
3. **Se para la línea y nadie sabe por qué.** No hay ningún bit que diga quién
   espera a quién.

El traspaso formal resuelve los tres. Es el mismo protocolo que usan las líneas
de montaje de verdad, y la razón por la que existe es exactamente esta: en un
traspaso, quien entrega no puede darse por libre hasta que quien recoge confirma.

## 2. Cómo funciona por dentro

### 2.1 Las señales

`Handshake` es un `struct` plano —sin herencia, sin métodos virtuales— con cinco
banderas y un dato:

| Campo | Dirección | Significado |
|---|---|---|
| `cmdStart` | Maestro → Esclavo | "empieza tu ciclo" |
| `cmdAck` | Maestro → Esclavo | "recibido, puedes soltar" |
| `statusBusy` | Esclavo → Maestro | "estoy trabajando" |
| `statusDone` | Esclavo → Maestro | "he terminado, la pieza es tuya" |
| `statusError` | Esclavo → Maestro | "estoy en fallo" |
| `payload` | Ambos | `uint16_t` que viaja con el testigo |

**"Maestro" y "esclavo" son papeles, no clases.** La misma estación es esclava
respecto de la de aguas arriba y maestra respecto de la de aguas abajo. Cada
`SequenceBlock` lleva **su propio** `handshake` público, y quien lo lee es el
vecino.

`payload` evita tener que montar un canal de comunicación aparte para un solo
valor: un identificador de pieza, el resultado de una inspección, el número de
receta con el que se ha fabricado.

### 2.2 La danza, paso a paso

```
A (entrega)                        B (recoge)
───────────                        ──────────
statusDone = true      ────────>   ve statusDone
                                   cmdAck = true      (acepta el relevo)
ve cmdAck              <────────
statusDone = false
vuelve a reposo        ────────>   ve statusDone bajado
                                   cmdAck = false     (cierra el ciclo)
```

Fíjate en el detalle que hace que esto funcione: **A no vuelve a reposo hasta ver
el acuse, y B no baja el acuse hasta ver que A bajó el aviso.** Con eso, ninguna
de las dos puede adelantarse ni perderse un traspaso.

El precio son cuatro booleanos y **dos ciclos de scan extra**. En una máquina que
scanea a 1 kHz, dos milisegundos por pieza. Es de las cosas más baratas que se
pueden comprar.

Compáralo con los tres fallos del apartado 1:

- B no estaba lista → A se queda con `statusDone = true` esperando el acuse
  indefinidamente. La pieza no se pierde: la línea se para y el bit dice por qué.
- B se entera dos veces → imposible: `cmdAck` solo se levanta cuando
  `statusDone && !cmdAck`, y la transición solo se completa una vez.
- Nadie sabe quién espera a quién → los cuatro bits lo dicen (ver §2.5).

### 2.3 Las utilidades del lado esclavo

```cpp
bool hasStartRequest() const { return cmdStart && !statusBusy && !statusDone; }

void acceptStart() {
  cmdStart   = false;      // <-- el consumo inmediato
  statusBusy = true;
  statusDone = false;
}

void announceDone(uint16_t data = 0) {
  statusBusy = false;
  statusDone = true;
  payload    = data;
}

bool isAcknowledged() const { return cmdAck; }
void clearDone() { statusDone = false; }
```

`acceptStart()` **consume `cmdStart`** en el acto. Eso convierte una señal
mantenida en un pulso, y evita que la estación rearranque sola en cuanto termine
el ciclo. Es el mismo criterio que con el bit `start` de la CFGW: una orden vieja
no puede sobrevivir a su momento.

La guarda `!statusBusy && !statusDone` de `hasStartRequest()` impide aceptar un
arranque mientras aún se está trabajando o mientras hay una pieza sin recoger.

### 2.4 Las utilidades del lado maestro

```cpp
bool isReady()  const { return !statusBusy && !statusDone && !statusError; }
bool isBusy()   const { return statusBusy; }
bool isDone()   const { return statusDone; }
bool hasError() const { return statusError; }

void requestStart(uint16_t data = 0) { cmdStart = true; payload = data; }
void acknowledge()      { cmdAck = true; }
void clearAcknowledge() { cmdAck = false; }
```

### 2.5 El diagnóstico, que es media razón de existir

Si la línea se para, mirando esos cuatro bits sabes exactamente quién espera a
quién:

| Lo que ves | Qué significa | Dónde mirar |
|---|---|---|
| `statusDone=1, cmdAck=0` | A entregó y B no lo acepta | **B** |
| `statusBusy=1` mucho rato | B se quedó colgada en su ciclo | **B** |
| Todo a cero y A parada | Nadie pidió nada | **el maestro** |
| `statusError=1` | La estación está en fallo | esa estación |

Y `describe()` lo vuelca en una línea:

```cpp
void describe(Print& out) const;
// HS[start=0 busy=0 done=1 ack=0 err=0 data=417]
```

Ese renglón dice: la estación entregó la pieza 417 y nadie se la ha aceptado.
Diagnóstico completo sin depurador y sin sonda.

### 2.6 `reset()`

```cpp
void reset() {
  cmdStart = cmdAck = false;
  statusBusy = statusDone = statusError = false;
  payload = 0;
}
```

Se llama en `begin()` y en el rearme tras una alarma. Y esto último importa: si
una avería interrumpe el traspaso a mitad, los flags quedan descolocados —A
esperando un acuse que ya nadie va a dar— y hay que limpiarlos antes de volver a
producir.

Ojo con el matiz que hay en `SequenceBlock::reset()`: **solo se toca el handshake
si se venía de una alarma**. Un `resetAll()` lanzado desde la consola mientras
una estación sana espera el acuse de su vecina no debe borrarle los bits del
traspaso: la vecina se quedaría esperando una pieza que ya pasó. Ese matiz salió
de la revisión adversarial.

### 2.7 `HandshakeMaster`, el remate

Encapsula la secuencia completa del lado maestro, que si se escribe a mano en
cada estación acaba copiándose mal en alguna:

```cpp
inline bool collect(Handshake& upstream) {
  if (upstream.statusDone && !upstream.cmdAck) {
    upstream.acknowledge();       // "acepto el relevo"
    return false;                 // aún no: falta que el vecino lo vea
  }
  if (upstream.cmdAck && !upstream.statusDone) {
    upstream.clearAcknowledge();  // el vecino ya bajó el aviso: cerramos
    return true;                  // traspaso completo
  }
  return false;
}
```

Devuelve `true` **una sola vez**, en el ciclo en que el traspaso queda
completado. Eso es lo que lo hace seguro de llamar desde dentro de un `switch` de
pasos.

Y su pareja, `deliver()`, para el lado que entrega:

```cpp
inline bool deliver(Handshake& own, uint16_t data = 0) {
  if (!own.statusDone) { own.announceDone(data); return false; }
  if (own.isAcknowledged()) { own.clearDone(); return true; }
  return false;
}
```

## 3. API completa

### Campos

| Campo | Tipo | Quién escribe |
|---|---|---|
| `cmdStart`, `cmdAck` | `bool` | Maestro |
| `statusBusy`, `statusDone`, `statusError` | `bool` | Esclavo |
| `payload` | `uint16_t` | Ambos |

### Lado esclavo

| Método | Qué hace |
|---|---|
| `hasStartRequest()` | ¿Hay petición pendiente y puedo atenderla? |
| `acceptStart()` | La acepta y **consume** `cmdStart` |
| `announceDone(data)` | Anuncia fin y adjunta el dato |
| `isAcknowledged()` | ¿Ya me han acusado? |
| `clearDone()` | Cierra el traspaso por su lado |

### Lado maestro

| Método | Qué hace |
|---|---|
| `isReady()` / `isBusy()` / `isDone()` / `hasError()` | Consulta |
| `requestStart(data)` | Pide arranque |
| `acknowledge()` / `clearAcknowledge()` | Acuse |

### Común

| Método | Qué hace |
|---|---|
| `reset()` | Todo a reposo |
| `describe(Print&)` | Volcado en una línea |
| `HandshakeMaster::collect(hs)` | Recoge el testigo. `true` una vez |
| `HandshakeMaster::deliver(hs, data)` | Entrega el testigo. `true` una vez |

## 4. Ejemplos

### 4.1 La estación que recibe

```cpp
enum Pasos : uint16_t { ESPERAR = 0, TRABAJAR = 10, ENTREGAR = 20 };

class EstacionB : public SequenceBlock {
 public:
  EstacionA* anterior = nullptr;
  bool herramienta = false;
  uint16_t piezaEnCurso = 0;

  void begin() override {
    setName(F("ESTACION_B"));
    setInitialStep(ESPERAR);
    setStep(ESPERAR);
    handshake.reset();
  }

  void update() override {
    if (!updateSequence()) { herramienta = false; return; }

    switch (_currentStep) {
      case ESPERAR:
        herramienta = false;
        /* Esperar pieza es una causa EXTERNA: la máquina está sana. */
        if (suspendWhile(!anterior->handshake.isDone())) break;
        if (!HandshakeMaster::collect(anterior->handshake)) break;
        piezaEnCurso = anterior->handshake.payload;
        setStep(TRABAJAR, 4000, 8000);
        break;

      case TRABAJAR:
        herramienta = true;
        if (getTimeInStep() >= 3000) { herramienta = false; setStep(ENTREGAR); }
        break;

      case ENTREGAR:
        if (HandshakeMaster::deliver(handshake, piezaEnCurso)) completeCycle();
        break;
    }
  }
};
```

Las dos líneas de `ESPERAR` hacen cosas distintas y las dos hacen falta:
`suspendWhile()` congela los cronómetros mientras no hay pieza —para que el
watchdog de ciclo no cuente esa espera—, y `collect()` ejecuta el protocolo.

### 4.2 Ver la línea entera de un vistazo

```cpp
void volcarLinea() {
  Serial.print(F("A: ")); estacionA.handshake.describe(Serial);
  Serial.print(F("  B: ")); estacionB.handshake.describe(Serial);
  Serial.println();
}
```

```
A: HS[start=0 busy=0 done=1 ack=0 err=0 data=417]  B: HS[start=0 busy=0 done=0 ack=0 err=0 data=0]
```

A tiene la pieza 417 entregada y sin acuse; B está en reposo y no la ha cogido.
El problema está en B: o no llega a su paso de recogida, o está deshabilitada.

### 4.3 Pasar el resultado de una inspección con la pieza

```cpp
// En la estación de visión, al entregar:
uint16_t codigo = (idPieza & 0x3FFF) | (piezaOk ? 0x8000 : 0);
if (HandshakeMaster::deliver(handshake, codigo)) completeCycle();

// En la siguiente estación, al recoger:
uint16_t dato = anterior->handshake.payload;
bool ok  = (dato & 0x8000) != 0;
uint16_t id = dato & 0x3FFF;
setStep(ok ? PASO_MECANIZAR : PASO_EXPULSAR_RECHAZO, 5000);
```

Dos bytes bien repartidos y no hace falta un canal aparte.

### 4.4 Propagar el fallo aguas arriba

```cpp
protected:
  void onTransition(SystemState from, SystemState to) override {
    CFSM_UNUSED(from);
    handshake.statusError = (to == STATE_ERROR);
  }
```

Y en la estación de aguas arriba:

```cpp
case ENTREGAR:
  if (siguiente->handshake.hasError()) {
    fault(CFSM_ERR_HANDSHAKE);      // no entregues a una estación averiada
    break;
  }
  if (HandshakeMaster::deliver(handshake, pieza)) completeCycle();
  break;
```

## 5. Decisiones de diseño

**Es un `struct` plano, sin herencia ni virtuales.** Cabe en 7 bytes y se copia
sin coste. Meterlo en una clase con métodos virtuales habría añadido 2 bytes de
vtable por estación a cambio de nada: aquí no hay nada que sustituir por
polimorfismo.

**Cada bloque lleva su propio handshake, público.** La alternativa —un objeto
`Conexion` entre dos bloques— sería más "correcto" y obligaría a un registro
central de conexiones, con su tabla y su búsqueda. Con el handshake dentro del
bloque, el vecino accede directamente y el coste es cero.

**`payload` es `uint16_t` y no un puntero o una estructura.** Un puntero abriría
la puerta a que una estación lea memoria de otra que ya la reutilizó. Dos bytes
por valor obligan a codificar, y esa restricción es sana: fuerza a decidir qué
información necesita de verdad viajar con la pieza.

**`collect()` devuelve `true` una sola vez.** Se valoró que devolviera el estado
del traspaso, y se descartó: un `true` de un solo ciclo se usa directamente como
condición de transición de paso, que es el 100 % de los casos.

## 6. Errores frecuentes

**Leer `statusDone` directamente en vez de usar `collect()`.** Funciona el primer
día y luego procesas la misma pieza dos veces, porque `statusDone` sigue a `1`
hasta que el vecino ve el acuse. `collect()` existe justo para eso.

**No llamar a `handshake.reset()` en `begin()`.** Al arrancar tras un corte, los
booleanos están a cero por el constructor, así que suele funcionar. Pero tras un
rearme en caliente, no. Ponlo siempre.

**Esperar la pieza sin `suspendWhile()`.** El paso de espera no tiene tiempo, así
que el watchdog de paso no salta — pero el de ciclo sí, si hay uno configurado.
Declara la espera y el problema desaparece.

**Olvidar `statusError`.** Sin él, una estación averiada sigue recibiendo piezas
que se acumulan delante.

**Usar el mismo `Handshake` para las dos direcciones.** Cada bloque lleva el
suyo: el de A describe lo que A entrega, el de B lo que B entrega. Si compartes
uno, las señales de ida y de vuelta se pisan.

## 7. Coste

| Miembro | Bytes |
|---|---:|
| Cinco `bool` | 5 |
| `payload` | 2 |

**7 bytes por estación**, y con el relleno de alineación probablemente 8. Sin
vtable. Las funciones de `HandshakeMaster` son `inline` libres: no cuestan RAM.

## 8. Relación con el resto

```
   SequenceBlock.h
        │  lleva un Handshake público por bloque
        ▼
    Handshake  ◀────── HandshakeMaster::collect() / deliver()
        │                    (funciones libres inline)
        │
        ├──▶ ST.stw.waitingAck    lo calcula syncStatusWord():
        │                          statusDone && !cmdAck
        │
        └──▶ SequenceBlock::reset()  lo limpia SOLO si se venía de alarma

   Y no depende de nada de la librería salvo la plataforma: es un struct
   de datos con utilidades, y por eso se puede volcar por un bus tal cual.
```
