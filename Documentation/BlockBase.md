# BlockBase.h

> El contrato que cumple todo bloque funcional. Es lo que permite que el orquestador meta en el mismo array una soldadura, un semáforo y una cámara, y les llame a todos igual.

**Ruta:** `src/core/BlockBase.h`
**Incluye:** `CoreFSM_Platform.h`, `ControlWords.h`
**Lo usan:** `FsmBlock.h` (hereda), `BlockManager.h` (guarda punteros a esto), `Telemetry.h`.

---

## 1. Qué problema resuelve

Un "bloque" es aquí lo que en un autómata es un **bloque de función (FB)**: una
porción de lógica con sus variables internas, sus entradas y sus salidas, que se
ejecuta una vez por ciclo de scan y que **nunca bloquea la CPU**.

Si necesita esperar, no espera: se lo apunta, devuelve el control, y en el
siguiente ciclo mira si ya ha pasado el tiempo. Esa es la diferencia de fondo
entre un programa de automatización y un script secuencial, y es la razón de que
`delay()` no aparezca en ninguna línea de esta librería.

`BlockBase` existe porque `BlockManager` necesita poder guardar en un mismo array
cosas que no se parecen en nada y llamarles a todas `update()` sin saber qué son.
Eso es polimorfismo: el manager habla con la interfaz, no con la implementación.
Es exactamente lo que hace el OB1 de un PLC cuando llama en orden a todos los FB
que le has colgado.

## 2. Cómo funciona por dentro

### 2.1 Los cinco métodos obligatorios

Son virtuales **puras** (`= 0`), así que el compilador impide instanciar un
bloque al que le falte alguno. Es una red de seguridad barata y eficaz: el error
sale al compilar, no en la máquina.

| Método | Cuándo se llama | Qué debe hacer |
|---|---|---|
| `begin()` | Una vez, en `beginAll()` | Valores de arranque, paso inicial, parámetros por defecto |
| `update()` | Cada scan | El cuerpo del bloque. **Jamás** `delay()`, `while` de espera ni `Serial.println()` incondicional |
| `getState()` | Cuando alguien pregunta | El estado como número. Cada familia decide su significado; `FsmBlock` usa `SystemState` |
| `isFaulted()` | El manager, para propagar alarmas | `true` si el bloque no debe considerarse operativo |
| `reset()` | En el rearme | Devolver el bloque a un estado seguro conocido |

Si intentas declarar un bloque sin implementar alguno, el compilador dice:

```
error: cannot declare variable 'p' to be of abstract type 'MiBloque'
note:   because the following virtual functions are pure within 'MiBloque':
note:     'virtual void BlockBase::begin()'
```

La segunda nota te dice exactamente cuál falta. No es un error críptico: es la
red funcionando.

### 2.2 Los comandos en difusión, y por qué son virtuales aquí

```cpp
virtual void start()  {}
virtual void stop()   {}
virtual void hold()   {}
virtual void resume() {}
virtual void abort(uint16_t code) { CFSM_UNUSED(code); }
virtual void onEmergencyStop() {}
```

No hacen nada por defecto. Están **en la clase base** para que el orquestador
pueda mandar a todos los bloques a la vez sin convertir punteros a la fuerza.

Esto merece detenerse, porque la alternativa es un fallo grave y silencioso. Si
`BlockManager::startAll()` hiciera

```cpp
static_cast<FsmBlock*>(_blocks[i])->start();   // NUNCA HACER ESTO
```

funcionaría mientras todos los bloques registrados heredasen de `FsmBlock`. El
día que alguien registre un bloque que hereda **directamente** de `BlockBase`
—perfectamente legal—, esa conversión es comportamiento indefinido: la llamada
se sale de la tabla de funciones virtuales y salta a una dirección arbitraria de
la flash.

Y el sitio donde eso ocurriría es el peor posible: la parada de emergencia. De
ahí que se resuelva con polimorfismo de verdad, aunque cueste seis entradas de
vtable en bloques que no las usan.

### 2.3 Identificación

```cpp
void setName(const __FlashStringHelper* n);
const __FlashStringHelper* getName() const;
void setId(uint8_t id);
uint8_t getId() const;
```

Sirve para que el diagnóstico diga `ESTACION_SOLDADURA` en vez de `bloque 2`.
El nombre se guarda como puntero a **memoria de programa**, de modo que cuesta
2 bytes de RAM en lugar de toda la longitud de la cadena. Con `F("...")`, veinte
nombres de estación cuestan 40 bytes en vez de 400.

El `_id` lo asigna `BlockManager::registerBlock()` automáticamente, en orden de
registro empezando por 0.

### 2.4 Habilitación

```cpp
void enable();  void disable();  void setEnabled(bool);  bool isEnabled() const;
```

Un bloque deshabilitado **sigue registrado** en el manager pero se salta en el
scan. Equivale a desactivar la llamada a un FB en el OB1: sirve para poner fuera
de servicio una estación averiada sin recompilar, o para aislar un bloque
durante la puesta en marcha.

**El aviso importa:** deshabilitar un bloque **congela sus salidas** en el último
valor que tuvieran. Si eso es peligroso, llama antes a `stop()` o a algo que las
ponga en estado seguro. Deshabilitar no es apagar.

### 2.5 El cronómetro de estado

```cpp
cfsm_time_t getTimeInState() const { return cfsm_elapsed(_stateStartTime); }
```

`_stateStartTime` es `protected`: lo actualizan las clases derivadas cada vez que
cambian de estado (`FsmBlock::transitionTo()` lo hace). La resta va en aritmética
sin signo, así que es inmune al desbordamiento de `millis()` a los 49,7 días —
ver [CoreFSM_Platform.md](CoreFSM_Platform.md) §2.3.

### 2.6 `describe()`

```cpp
virtual void describe(Print& out) const {
  out.print('[');
  if (_name) out.print(_name); else out.print(_id);
  out.print(CFSM_FSTR("] estado="));  out.print(getState());
  out.print(CFSM_FSTR(" t="));        out.print(getTimeInState());
  out.print(CFSM_FSTR("ms"));
  if (!_enabled)   out.print(CFSM_FSTR(" (DESHABILITADO)"));
  if (isFaulted()) out.print(CFSM_FSTR(" (FALLO)"));
}
```

Es virtual a propósito. Los hijos la amplían llamando primero a la del padre:

```cpp
void describe(Print& out) const override {
  BlockBase::describe(out);
  out.print(F(" piezas=")); out.print(piezasHoy);
}
```

Así la tabla de observación de `BlockManager::printWatchTable()` sale coherente
para todos los bloques y cada uno añade lo suyo.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| `begin()` | `virtual void begin() = 0` | Inicialización única |
| `update()` | `virtual void update() = 0` | Cuerpo del bloque, cada scan |
| `getState()` | `virtual uint8_t getState() const = 0` | Estado como número |
| `isFaulted()` | `virtual bool isFaulted() const = 0` | ¿En fallo? |
| `reset()` | `virtual void reset() = 0` | Rearme |
| `start()` `stop()` `hold()` `resume()` | `virtual void ...()` | Difusión. Vacíos por defecto |
| `abort(code)` | `virtual void abort(uint16_t)` | Difusión |
| `onEmergencyStop()` | `virtual void onEmergencyStop()` | Difusión desde la seta |
| `setName(F("..."))` / `getName()` | | Nombre en flash |
| `setId(n)` / `getId()` | | Id, lo pone el manager |
| `enable()` / `disable()` / `setEnabled(b)` / `isEnabled()` | | Habilitación |
| `getTimeInState()` | `cfsm_time_t` | ms en el estado actual |
| `describe(Print&)` | `virtual void` | Volcado legible |

## 4. Ejemplos

### 4.1 Un bloque desde cero, sin heredar de `FsmBlock`

Cuando lo que quieres no tiene estados de máquina —un contador de producción, un
supervisor, un filtro— `BlockBase` sola basta:

```cpp
class ContadorTurno : public BlockBase {
  uint32_t piezas = 0;
  uint32_t inicioTurno = 0;
  bool ultimoDone = false;

 public:
  SequenceBlock* vigilado = nullptr;

  void begin() override {
    setName(F("CONTADOR"));
    inicioTurno = cfsm_millis();
  }

  void update() override {
    if (!vigilado) return;
    bool done = vigilado->ST.stw.done;
    if (done && !ultimoDone) piezas++;      // cuenta el flanco, no el nivel
    ultimoDone = done;
  }

  uint8_t getState() const override { return 0; }
  bool isFaulted() const override   { return false; }
  void reset() override             { piezas = 0; inicioTurno = cfsm_millis(); }

  void describe(Print& out) const override {
    BlockBase::describe(out);
    out.print(F(" piezas=")); out.print(piezas);
    out.print(F(" turno="));  out.print(cfsm_elapsed(inicioTurno) / 60000UL);
    out.print(F("min"));
  }
};
```

Los cinco obligatorios están, `getState()` devuelve 0 porque este bloque no tiene
estados, y `describe()` amplía la del padre. Se registra en el manager como
cualquier otro y aparece en la tabla de observación.

### 4.2 Un bloque que se pone seguro ante la seta sin ser un `FsmBlock`

```cpp
class Calefactor : public BlockBase {
 public:
  bool resistencia = false;
  void begin() override { setName(F("CALEFACTOR")); }
  void update() override { /* regulación */ }
  uint8_t getState() const override { return resistencia ? 1 : 0; }
  bool isFaulted() const override { return false; }
  void reset() override { resistencia = false; }

  void onEmergencyStop() override { resistencia = false; }   // <-- aquí
};
```

Sin ese `onEmergencyStop()`, la resistencia se quedaría encendida al pulsar la
seta: recuerda que con la seta activa `updateAll()` **no llama a `update()`**, y
las salidas se congelan en su último valor.

### 4.3 Poner una estación fuera de servicio desde la consola

```cpp
consola.setExtraHandler([](char c) {
  if (c == '1') estacion1.disable();
  if (c == '2') { estacion1.stop(); estacion1.disable(); }   // esto sí es seguro
  if (c == '0') estacion1.enable();
});
```

La primera línea congela las salidas de la estación tal como estaban. La segunda
la para ordenadamente **antes** de sacarla del scan. Salvo que sepas muy bien lo
que haces, quieres la segunda.

## 5. Decisiones de diseño

**`IDevice` y `BlockBase` son jerarquías separadas a propósito.** Un sensor y una
estación no son la misma clase de cosa: el sensor participa en las fases PAE y
PAA (`readInputs()` / `writeOutputs()`), la estación en la fase OB1 (`update()`).
Meterlos en una sola jerarquía obligaría a que cada sensor tuviera un `update()`
vacío y cada bloque un `readInputs()` vacío. Es el reparto CM/EM de ISA-88: el
módulo de control y el módulo de equipo son capas distintas.

**Los comandos de difusión son virtuales y no plantillas.** Una plantilla habría
evitado la vtable, pero entonces el manager no podría guardar tipos distintos en
el mismo array, que es justo lo que necesita.

**`_stateStartTime` es `protected` y no privado con setter.** Lo actualizan las
derivadas en cada transición, en el camino caliente; un setter virtual ahí sería
gasto sin ganancia.

## 6. Errores frecuentes

**Olvidar uno de los cinco obligatorios.** No compila, y el compilador te dice
cuál. Ver §2.1.

**Meter `delay()` en `update()`.** No es un consejo de estilo: un `delay(100)`
en un bloque alarga el scan de todos los demás, los antirrebotes empiezan a
perder flancos y los tiempos de paso pierden resolución. Si crees que necesitas
esperar, lo que necesitas es un paso más o un `Ton`.

**`Serial.println()` incondicional dentro de `update()`.** Se ejecuta miles de
veces por segundo y satura el buffer de transmisión hasta bloquear la CPU. Va en
`onStepEntered()` o detrás de un flanco.

**Registrar un bloque y olvidar que `begin()` lo llama `beginAll()`.** Si
inicializas a mano en el `setup()` antes de `manager.beginAll()`, el `begin()`
del bloque te pisará los valores.

**Deshabilitar un bloque creyendo que apaga sus salidas.** Las congela. Ver §2.4.

**Pasar una cadena normal a `setName()`.** Espera `const __FlashStringHelper*`,
o sea `F("...")`. Sin la `F` no compila, que es lo mejor que podía pasar.

## 7. Coste

Por instancia, en AVR:

| Miembro | Bytes |
|---|---:|
| `_stateStartTime` | 4 |
| `_name` (puntero a flash) | 2 |
| `_id` | 1 |
| `_enabled` | 1 |
| puntero a vtable | 2 |

**10 bytes.** La vtable en sí vive en flash y se comparte entre todas las
instancias de la misma clase.

## 8. Relación con el resto

```
                       BlockBase.h
                            │
              ┌─────────────┴─────────────┐
              ▼                           ▼
         FsmBlock.h                 tus bloques sin estados
              │                     (contadores, supervisores)
              ▼
        SequenceBlock.h
              │
       ┌──────┴───────┐
       ▼              ▼
  tus estaciones  RecipeExecutor.h

  BlockManager guarda BlockBase* y llama por la vtable: le da igual
  qué haya debajo.

  OJO: IDevice.h es OTRA jerarquía, la de la E/S. Un sensor NO es
  un BlockBase, y es a propósito.
```
