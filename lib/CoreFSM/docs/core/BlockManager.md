# BlockManager.h

> El OB1. Guarda los bloques, los ejecuta en orden una vez por scan, les manda órdenes en difusión y cronometra lo que tardan.

**Ruta:** `src/core/BlockManager.h`
**Incluye:** `BlockBase.h`
**Lo usan:** tu `.ino` o `main.cpp`, y `Telemetry.h` (`MaintenanceConsole`).

---

## 1. Qué problema resuelve

En un PLC, el OB1 es el bloque de organización que se ejecuta cíclicamente y
llama en orden a todos los bloques de función que le has colgado. No hace lógica:
orquesta.

`BlockManager` es eso. Sin él, tu `loop()` sería una lista creciente de llamadas
a mano —`estacion1.update(); estacion2.update(); semaforo.update();`— y cada
comando global (paro general, rearme, interbloqueo) obligaría a repetir esa
lista otra vez.

## 2. Cómo funciona por dentro

### 2.1 El array es estático, y eso decide todo

```cpp
template <uint8_t MAX_BLOCKS = 8>
class BlockManager {
  ...
  private:
    BlockBase* _blocks[MAX_BLOCKS];
    uint8_t    _count;
};
```

El tamaño va en la plantilla, así que el array se reserva **en tiempo de
compilación**. `BlockManager<4> manager;` son cuatro punteros, 8 bytes en AVR, y
ni un solo `new`.

Es la misma razón por la que un PLC no tiene memoria dinámica: en una máquina que
tiene que estar encendida meses, la fragmentación del montón es una avería con
fecha desconocida. Aquí no puede pasar porque no hay montón.

El precio es que hay que decidir el número por adelantado. Si te quedas corto,
`registerBlock()` devuelve `false`.

### 2.2 El registro, y el fallo silencioso que evita

```cpp
bool registerBlock(BlockBase* block) {
  if (_begun || _count >= MAX_BLOCKS || block == nullptr) return false;
  for (uint8_t i = 0; i < _count; i++)
    if (_blocks[i] == block) return false;
  block->setId(_count);
  _blocks[_count++] = block;
  return true;
}
```

Asigna el `_id` en orden de registro empezando por 0, así que el orden en que
registras es el orden en que se ejecutan. Eso importa: si el bloque A calcula
algo que el bloque B lee en el mismo scan, A tiene que ir primero o B leerá el
valor del scan anterior.

**Conviene comprobar el `false`.** También se rechazan duplicados y registros
posteriores a `beginAll()`. Un bloque omitido simplemente no se ejecuta, un
fallo silencioso muy incómodo de localizar.

Hay una sobrecarga que registra y nombra de una vez:

```cpp
manager.registerBlock(&estacion, F("SOLDADURA"));
```

### 2.3 `updateAll()`, que es la fase OB1

```cpp
void updateAll() {
  uint32_t t0 = micros();

  if (_emergencyStop) {
    for (uint8_t i = 0; i < _count; i++) _blocks[i]->onEmergencyStop();
  } else {
    for (uint8_t i = 0; i < _count; i++) {
      if (_blocks[i]->isEnabled()) _blocks[i]->update();
    }
  }

  _lastScanUs = micros() - t0;
  if (_lastScanUs > _maxScanUs) _maxScanUs = _lastScanUs;
  if (_lastScanUs < _minScanUs) _minScanUs = _lastScanUs;
  _scanCount++;
}
```

Con el interbloqueo activo no se ejecuta lógica de proceso: se notifica a cada
bloque mediante `onEmergencyStop()`. `BlockManager` no conoce pines ni puede
garantizar por sí solo el estado físico. Conecta además
`HW.setSafetyInterlock(manager.isEmergencyStop())`; para proteger personas o
maquinaria utiliza un circuito de seguridad independiente.

La resta `micros() - t0` es sin signo, así que sobrevive al desbordamiento de
`micros()` (cada ~71 minutos en AVR).

### 2.4 Qué mide exactamente el cronómetro, y qué NO mide

Esta distinción es la que más se confunde, así que va en negrita:

**`BlockManager` mide solo la fase OB1.** El `t0` se toma al entrar en
`updateAll()` y el cierre al salir. Lo que queda fuera es todo lo demás del
`loop()`: la lectura de entradas (PAE), la escritura de salidas (PAA), tu
telemetría, la consola, y cualquier cosa que hayas metido entre medias.

Para medir **el `loop()` entero** —que es lo que de verdad determina si pierdes
flancos— está [`diag/ScanWatchdog.h`](../diag/ScanWatchdog.md), que además tiene
límite y cuenta los excesos. `BlockManager` te dice *cuánto tarda tu lógica*;
`ScanWatchdog` te dice *cuánto tarda tu ciclo*. Son dos preguntas distintas y las
dos son útiles:

```
loop() ├─ HW.readInputs()     ─┐
       ├─ manager.updateAll() ─┤ solo esto mide BlockManager
       ├─ HW.writeOutputs()   ─┤
       ├─ tracer.update()     ─┤
       └─ consola.update()    ─┘ todo esto mide ScanWatchdog
```

Si `maxScanTimeUs()` se dispara, hay algo que bloquea dentro de un bloque: un
`delay()`, un `Serial` saturado, un bucle de espera. Si el máximo de
`ScanWatchdog` se dispara pero el del manager no, el problema está fuera de la
lógica: en un sensor lento, en la telemetría, o en el propio `Serial`.

### 2.5 Comandos en difusión

```cpp
void startAll()  { for (...) _blocks[i]->start();  }
void stopAll()   { for (...) _blocks[i]->stop();   }
void holdAll()   { for (...) _blocks[i]->hold();   }
void resumeAll() { for (...) _blocks[i]->resume(); }
void resetAll()  { if (!_emergencyStop) for (...) _blocks[i]->reset(); }
```

Van por llamada virtual, no por conversión de puntero. Un bloque que hereda
directamente de `BlockBase` simplemente los ignora —los tiene vacíos— sin que eso
rompa nada. El porqué de esta decisión está en
[BlockBase.md](BlockBase.md) §2.2, y no es un detalle de estilo: la alternativa
es comportamiento indefinido justo en el camino de un interbloqueo.

Fíjate en que `startAll()` no comprueba `isEnabled()`. Es deliberado: habilitar y
mandar son cosas distintas, y un bloque deshabilitado que recibe `start()` se
limita a cambiar de estado sin ejecutarse.

### 2.6 Interbloqueo de software

```cpp
void setEmergencyStop(bool active) { _emergencyStop = active; }
bool isEmergencyStop() const       { return _emergencyStop; }
```

El nombre histórico de la API se conserva por compatibilidad. Es un **nivel**,
no un pulso: mientras esté activo, ningún bloque ejecuta lógica y todos reciben
el evento de fallo. `resetAll()` se ignora mientras siga activo y liberarlo no
rearranca nada; hay que rearmar explícitamente.

Esto es comportamiento de control general. No convierte el firmware, la placa
ni sus GPIO en una parada de emergencia certificada. Consulta
[`SAFETY.md`](../../../../SAFETY.md).

### 2.7 Diagnóstico global

```cpp
bool       hasAnyFault() const;    // ¿hay alguno en fallo?
BlockBase* firstFaulted() const;   // el PRIMERO de la lista que está en fallo
bool       allIdle() const;        // ¿están todos en STATE_IDLE?
```

`firstFaulted()` devuelve el primero **en orden de registro**, no el primero en
fallar cronológicamente. Para la causa raíz temporal, lo que quieres es el
`errorCode` de cada bloque, que sí conserva la primera causa (ver
[FsmBlock.md](FsmBlock.md) §2.6).

### 2.8 `printWatchTable()`

Es la tabla de observación de TIA Portal:

```
---- TABLA DE OBSERVACION ----
 [SOLDADURA] estado=2 t=1240ms paso=20(SOLDAR) t_paso=840ms ciclos=17
 [SEMAFORO] estado=2 t=45210ms
 scan: ult=1832us max=4120us n=54211
------------------------------
```

Cada línea sale del `describe()` virtual del bloque, así que cada uno añade lo
suyo. **Llámala solo de vez en cuando** —al recibir un carácter por el puerto
serie, por ejemplo—, nunca en cada vuelta del scan: son varios cientos de bytes
por el `Serial` y eso se nota en el tiempo de ciclo.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| `registerBlock` | `bool registerBlock(BlockBase*)` | Registra. `false` si está lleno |
| `registerBlock` | `bool registerBlock(BlockBase*, const __FlashStringHelper*)` | Registra y nombra |
| `count()` / `at(i)` | | Cuántos hay / el i-ésimo |
| `beginAll()` | | Llama a `begin()` de todos. En el `setup()` |
| `updateAll()` | | **La fase OB1.** Una vez por `loop()` |
| `startAll` `stopAll` `holdAll` `resumeAll` `resetAll` | | Difusión |
| `setEmergencyStop(bool)` / `isEmergencyStop()` | | Interbloqueo lógico, por nivel |
| `hasAnyFault()` / `firstFaulted()` / `allIdle()` | | Diagnóstico global |
| `lastScanTimeUs()` `maxScanTimeUs()` `minScanTimeUs()` `scanCount()` | `uint32_t` | Estadísticas **de OB1** |
| `resetScanStats()` | | Pone máximo y mínimo a cero |
| `printWatchTable(Print&)` | | Volcado de todos los bloques |

## 4. Ejemplos

### 4.1 El esqueleto completo

```cpp
BlockManager<4>   manager;
EstacionSoldadura estacion;
Semaforo          semaforo;
ContadorTurno     contador;

void setup() {
  Serial.begin(115200);
  HW.begin();

  if (!manager.registerBlock(&estacion, F("SOLDADURA")))
    Serial.println(F("ERROR: no cabe SOLDADURA en el manager"));
  manager.registerBlock(&semaforo, F("SEMAFORO"));
  manager.registerBlock(&contador, F("CONTADOR"));

  manager.beginAll();
  manager.startAll();
}

void loop() {
  HW.readInputs();                              // PAE
  manager.setEmergencyStop(HW.Seta.isTriggered());
  manager.updateAll();                          // OB1
  HW.writeOutputs();                            // PAA
}
```

El orden de registro es el de ejecución: el contador va el último porque lee lo
que la estación acaba de calcular en este mismo scan.

### 4.2 Distinguir dónde se te va el scan

```cpp
ScanWatchdog scan(20);

void loop() {
  scan.begin();
  HW.readInputs();
  manager.updateAll();
  HW.writeOutputs();
  tracer.update();
  scan.end();
}

void informe() {
  Serial.print(F("loop entero: max=")); Serial.print(scan.maxUs());
  Serial.print(F("us   solo OB1: max=")); Serial.print(manager.maxScanTimeUs());
  Serial.println(F("us"));
}
```

```
loop entero: max=24800us   solo OB1: max=1900us
```

Ese renglón dice que la lógica tarda 1,9 ms y el `loop()` casi 25. Los 23 ms que
faltan no están en tus bloques: están en la E/S o en la telemetría. Con un
ultrasonidos sin eco en la lista, ya sabes dónde mirar.

### 4.3 Conectar el interbloqueo lógico y las salidas

```cpp
void loop() {
  HW.readInputs();

  manager.setEmergencyStop(HW.Seta.isTriggered());

  manager.updateAll();

  /* Liberar el nivel no rearma los bloques. */
  if (HW.BtnRearme.hasRisen() && !manager.isEmergencyStop()) {
    manager.resetAll();
    if (manager.allIdle()) manager.startAll();
  }

  /* Fuerza el valor seguro configurado y lo vuelca incluso sin update(). */
  HW.setSafetyInterlock(manager.isEmergencyStop());
  HW.writeOutputs();
}
```

Una entrada NC y un pull-up no bastan para diagnosticar todos los fallos de
cableado. El corte de energía peligrosa debe resolverse fuera de este firmware.

### 4.4 Saber quién tiró la línea

```cpp
if (manager.hasAnyFault()) {
  BlockBase* culpable = manager.firstFaulted();
  Serial.print(F("Parada por: "));
  culpable->describe(Serial);
  Serial.println();
}
```

```
Parada por: [SOLDADURA] estado=6 t=120ms paso=10(AVANZAR) t_paso=5010ms ciclos=41 ERR=0x0001 Timeout de paso (FALLO)
```

## 5. Decisiones de diseño

**Plantilla con el tamaño, y no un array dinámico.** Ver §2.1. En AVR no hay
discusión posible; en ESP32 podría haberla, pero mantener dos comportamientos
según la plataforma habría partido la librería en dos.

**`updateAll()` no devuelve nada.** Se valoró devolver el tiempo de scan o un
booleano de "todo bien", y se descartó: el `loop()` quedaría con un valor de
retorno que casi nadie usa, y las estadísticas ya están disponibles por getter
cuando hacen falta.

**El interbloqueo se guarda como estado del manager, no como pulso.** Así
`isEmergencyStop()` puede consultarse desde cualquier sitio y el rearme puede
negarse mientras siga activa.

**No hay `updateAll()` con prioridades ni con periodos distintos por bloque.**
Sería útil (un lazo de posición a 5 ms, un supervisor a 500 ms) y complicaría el
modelo mental de golpe. Si un bloque tuyo necesita ir más lento, se lo apunta él
con un `cfsm_elapsed`.

## 6. Errores frecuentes

**No comprobar el retorno de `registerBlock()`.** El bloque no se ejecuta y no
hay ningún aviso. Si una estación "no hace nada", esto es lo primero que mirar.

**Registrar en un orden que no es el de dependencia.** Si B lee lo que A calcula,
A va primero. Si no, B trabaja siempre con datos de un scan de retraso, lo que
produce un desfase de un ciclo difícil de ver.

**Llamar a `beginAll()` antes de configurar el hardware.** `begin()` de los
bloques puede querer leer un estado inicial. `HW.begin()` va primero.

**Creer que `setEmergencyStop()` actúa sobre pines.** No: detiene la lógica y
notifica bloques. Enlaza `HW.setSafetyInterlock()` y diseña el corte físico por
separado. Ver §2.3.

**Llamar a `printWatchTable()` en cada scan.** Varios cientos de bytes por serie
cada vuelta: el tiempo de ciclo se dispara y los antirrebotes empiezan a fallar.
Es justo el fallo que la propia tabla te ayudaría a diagnosticar.

**Usar `maxScanTimeUs()` creyendo que mide el `loop()`.** Mide solo OB1. Ver §2.4.

## 7. Coste

Por instancia, con `MAX_BLOCKS = N`, en AVR:

| Miembro | Bytes |
|---|---:|
| `_blocks[N]` | 2·N |
| `_count` | 1 |
| `_scanCount`, `_scanStartUs`, `_lastScanUs`, `_maxScanUs`, `_minScanUs` | 20 |
| `_emergencyStop` | 1 |

`BlockManager<4>` son **unos 30 bytes**. `BlockManager<8>`, 38. El array crece
2 bytes por bloque; poner un margen generoso es barato.

## 8. Relación con el resto

```
   tu .ino / main.cpp
         │
         │  registerBlock() · beginAll() · updateAll() · setEmergencyStop()
         ▼
   BlockManager<N>
         │  guarda BlockBase* y llama por vtable
         ▼
     BlockBase ◀── FsmBlock ◀── SequenceBlock ◀── tus estaciones
         │
         └── describe()  ──▶  printWatchTable()

   MaintenanceConsole<N> envuelve al manager y expone sus comandos
   por teclas del monitor serie: w s x p r c ?
```
