# IOManager.h

> La vía ligera: vincula un pin directamente a la dirección de memoria de un `bool` que ya existe dentro de tu bloque. Cuatro bytes por señal en vez de dieciséis.

**Ruta:** `src/io/IOManager.h`
**Incluye:** `../core/CoreFSM_Platform.h`
**Lo usan:** nadie dentro de la librería. Es una alternativa a `IOTable.h`, para cuando vas justo de RAM.

---

## 1. Qué problema resuelve

Hay dos formas de resolver la imagen de proceso, con distinto equilibrio:

| | RAM por señal | Qué aporta |
|---|---:|---|
| **`IOTable` + `CfsmHardware`** *(recomendado)* | ~12-16 B | Objeto completo: antirrebote, flancos, forzado, diagnóstico |
| **`IOManager`** *(este archivo)* | **4 B** | Solo el mapeo pin ↔ variable |

`IOTable` es lo que quieres el 90 % de las veces y lo que genera el script de
Wokwi. `IOManager` tiene sentido en tres casos concretos: cuando vas muy justo de
RAM en un ATmega328, cuando las señales vienen de sensores electrónicos que no
rebotan (una salida de otro circuito, un optoacoplador), o cuando quieres mapear
variables que ya tienes declaradas dentro de tus bloques.

## 2. Cómo funciona por dentro

### 2.1 Las dos tablas de mapeo

```cpp
struct InputMapping  { uint8_t pin; bool*       target; bool activeLow; };
struct OutputMapping { uint8_t pin; const bool* source; bool activeLow; };
```

Cuatro bytes cada una en AVR (1 + 2 del puntero + 1). Fíjate en el `const` del
lado de salida: `IOManager` **lee** la variable de tu bloque y la escribe al pin,
así que no necesita —ni debe— poder modificarla.

### 2.2 El mapeo

```cpp
io.mapInput(2, &estacion.pulsadorMarcha);
```

El operador `&` obtiene la **dirección en RAM** de esa variable concreta dentro
de ese objeto concreto. `IOManager` guarda la pareja `[pin, dirección]`. En cada
`readInputs()` recorre la tabla, lee el pin y escribe el resultado directamente
en esa dirección.

Tu bloque encuentra la variable ya actualizada **sin que nadie se la haya pasado
explícitamente**. Es exactamente lo que hace la imagen de proceso de un autómata:
el programa lee `%I0.0` y alguien, antes, se ha encargado de que ese bit valga lo
que vale.

### 2.3 Las dos fases

```cpp
void readInputs() {
  for (uint8_t i = 0; i < _inCount; i++) {
    bool level = (digitalRead(_inputs[i].pin) == LOW);
    *(_inputs[i].target) = _inputs[i].activeLow ? level : !level;
  }
}

void writeOutputs() {
  for (uint8_t i = 0; i < _outCount; i++) {
    bool v = *(_outputs[i].source);
    digitalWrite(_outputs[i].pin, (v != _outputs[i].activeLow) ? HIGH : LOW);
  }
}
```

La lógica de `activeLow` en la entrada: `level` es cierto cuando el pin está a
nivel bajo. Con `activeLow = true` —el cableado estándar de conmutación a masa
con pull-up— nivel bajo significa "activo", así que se copia tal cual. Con
`activeLow = false` se invierte.

En la salida, `(v != activeLow)` es un XOR escrito con booleanos: si la salida es
activa a alto, `v` verdadero da `HIGH`; si es activa a bajo, `v` verdadero da
`LOW`.

### 2.4 `begin()`

```cpp
pinMode(pin, activeLow ? INPUT_PULLUP : INPUT);
...
pinMode(pin, OUTPUT);
digitalWrite(pin, activeLow ? HIGH : LOW);
```

Las salidas se dejan **en su estado inactivo** al configurarlas. Sin esa segunda
línea, un módulo de relé activo a bajo arrancaría pegado durante el tiempo que
tarda el primer `writeOutputs()`, que no es cero.

### 2.5 El aviso sobre punteros colgantes

Es el peligro real de este archivo y merece leerse entero.

**Los objetos apuntados deben vivir tanto como el `IOManager`.** Mapea siempre
variables de objetos **globales**, declarados fuera de cualquier función. Si
mapeas una variable local de `setup()`, al salir de `setup()` esa memoria se
reutiliza para la pila, y el `IOManager` seguirá escribiendo ahí en cada scan.

El síntoma es un programa que se comporta de forma aleatoria e imposible de
depurar: variables que cambian solas, retornos de función corruptos, reinicios
sin causa. Y no hay ningún aviso ni del compilador ni de la ejecución.

Esta clase de fallo es exactamente lo que `IOTable` evita por construcción, y es
la razón principal de que `IOManager` sea la opción secundaria.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| `mapInput` | `bool mapInput(uint8_t pin, bool* target, bool activeLow = true)` | Pin → variable |
| `mapOutput` | `bool mapOutput(uint8_t pin, const bool* source, bool activeLow = false)` | Variable → pin |
| `begin()` | | `pinMode` de todo y salidas a inactivo |
| `readInputs()` | | **Fase PAE** |
| `writeOutputs()` | | **Fase PAA** |
| `inputCount()` / `outputCount()` | | Cuántos hay mapeados |

La plantilla es `IOManager<MAX_INPUTS = 8, MAX_OUTPUTS = 8>`.

Fíjate en los valores por defecto de `activeLow`: `true` en las entradas (el
cableado normal a masa con pull-up) y `false` en las salidas (un LED normal). Son
los casos frecuentes; los módulos de relé necesitan `true` en la salida.

## 4. Ejemplos

### 4.1 Una máquina entera con 4 bytes por señal

```cpp
Proceso proceso;                          // GLOBAL. Es imprescindible.
IOManager<4, 3> io;

void setup() {
  io.mapInput(2, &proceso.pulsadorMarcha);        // pull-up, activo a bajo
  io.mapInput(3, &proceso.fcTrabajo);
  io.mapInput(4, &proceso.fcReposo);
  io.mapInput(5, &proceso.setaEmergencia);

  io.mapOutput(12, &proceso.motorCarro);          // LED o driver, activo a alto
  io.mapOutput(13, &proceso.soldador);
  io.mapOutput(11, &proceso.releAspiracion, true); // módulo de relé, activo a bajo

  io.begin();
  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();
}

void loop() {
  io.readInputs();       // PAE: las variables del bloque ya están al día
  manager.updateAll();   // OB1
  io.writeOutputs();     // PAA: lo que el bloque escribió sale a los pines
}
```

Siete señales: 28 bytes. Con `IOTable` serían unos 110. La diferencia importa en
un ATmega328 con un ejecutor de recetas cargado.

### 4.2 Lo que pierdes, y cómo recuperarlo a mano

Sin antirrebote, un pulsador barato genera decenas de transiciones al pulsarlo.
Si esa señal dispara un ciclo, disparas decenas de ciclos. Y sin flanco, mantener
el dedo puesto relanza el ciclo cada scan.

Ambas cosas se pueden montar con [`logic/`](../logic/Timers.md):

```cpp
class Proceso : public SequenceBlock {
 public:
  bool pulsadorMarcha = false;      // <-- la mapea el IOManager, cruda
  bool ordenMarcha    = false;      // <-- la que usa la secuencia

 private:
  Ton    _filtro;                   // antirrebote a mano
  R_Trig _flanco;                   // detección de flanco

 public:
  void begin() override { _filtro.PT = 25; }

  void update() override {
    _filtro.update(pulsadorMarcha);        // 25 ms estable
    ordenMarcha = _flanco.update(_filtro.Q);   // y solo el flanco de subida
    if (!updateSequence()) { ... return; }
    ...
  }
};
```

Son 4 bytes de mapeo más lo que ocupen el `Ton` y el `R_Trig`. Si acabas
haciendo esto en cinco señales, ya no estás ahorrando: usa `IOTable`.

### 4.3 Convivir con `IOTable`

Nada impide usar las dos: la tabla para las señales que necesitan antirrebote y
flancos, y el `IOManager` para las que son solo bits.

```cpp
void loop() {
  HW.readInputs();       // las señales con objeto completo
  io.readInputs();       // las señales crudas
  manager.updateAll();
  io.writeOutputs();
  HW.writeOutputs();
}
```

El orden entre las dos lecturas da igual: las dos ocurren dentro de la fase PAE,
que es lo único que importa para la coherencia.

## 5. Decisiones de diseño

**Punteros crudos y no referencias ni `std::function`.** Una referencia no se
puede guardar en un array y reasignar; `std::function` no existe en AVR sin
sobrecoste. El puntero crudo es lo que hay, con su aviso al lado.

**No hereda de `IDevice`.** Podría, y entonces se registraría en el
`DeviceManager` como uno más. Se dejó independiente porque es una vía alternativa
completa —tiene su propio `begin()`, `readInputs()` y `writeOutputs()`— y
mezclarla habría sugerido que se usan juntas por defecto, cuando lo normal es
elegir una.

**No aporta forzado.** Sería fácil (un tercer campo por fila) y se descartó: el
forzado necesita que alguien pueda consultar `isForced()`, y sin objeto por señal
no hay dónde preguntar. Quien necesita forzar, necesita `IOTable`.

**Dos plantillas separadas para entradas y salidas.** Una máquina suele tener
más de unas que de otras, y un solo `MAX` obligaría a dimensionar por el mayor.

## 6. Errores frecuentes

**Mapear una variable local.** Ver §2.5. Es el fallo grave de este archivo.

**Olvidar `activeLow = true` en un módulo de relé.** El relé arranca pegado y se
queda invertido toda la ejecución. Se ve enseguida, pero desconcierta.

**Esperar antirrebote.** No lo hay. Un pulsador mecánico mapeado directamente
genera basura. Ver §4.2.

**Mapear la misma variable dos veces, o el mismo pin.** No hay comprobación:
compila y se pisan.

**Mapear como salida una variable que el bloque no actualiza cada scan.** El pin
refleja siempre el último valor que tenga esa variable, así que si tu lógica solo
la escribe en un paso concreto, en los demás se queda congelada. Con
`DigitalOutput` pasa lo mismo, pero al menos hay un `setMaxOnTime()` que lo caza.

## 7. Coste

| | Bytes |
|---|---:|
| Por entrada mapeada | 4 |
| Por salida mapeada | 4 |
| `_inCount`, `_outCount` | 2 |

`IOManager<8,8>` son **66 bytes** con las dos tablas llenas. Comparado con ocho
`DigitalSensor` y ocho `DigitalOutput` (unos 240 bytes), el ahorro es de unos
170 bytes: el 8 % de la RAM de un Nano.

## 8. Relación con el resto

```
   Tus bloques (variables bool públicas)
        ▲                    │
        │ escribe            │ lee
        │                    ▼
   IOManager<NI, NO>   ──▶  digitalRead / digitalWrite
        │
        └── alternativa ligera a:

   IOTable.h ──▶ CfsmHardware ──▶ DeviceManager ──▶ IDevice
                 (objetos completos, forzado, antirrebote, flancos)
```

Las dos vías resuelven lo mismo. Elige `IOTable` salvo que la RAM te obligue.
