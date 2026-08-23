# IOTable.h

> La tabla de variables. Escribes una fila por señal y de ahí sale todo: el objeto, su configuración, su registro y su diagnóstico. Es el archivo con más magia de la librería, y aquí se desmonta entera.

**Ruta:** `src/io/IOTable.h`
**Incluye:** `DigitalSensor.h`, `DigitalOutput.h`, `AnalogSensor.h`, `DeviceManager.h`
**Lo usan:** tu `HardwareConfig.h`, y **solo** él. Ver §2.1.
**Lo genera:** `tools/wokwi2corefsm.py` a partir de `diagram.json`.

---

## 1. Qué problema resuelve

El objetivo es tener una tabla de asignación de hardware que se lea como la
tabla de variables de TIA Portal —una fila por señal, con su pin, su nombre
simbólico y sus opciones— y que **todo lo demás salga solo de esa tabla**:
declarar la variable, configurar el pin, registrarla en la imagen de proceso,
leerla y escribirla cada ciclo.

Sin esto, cada señal aparece **tres veces** en tu código: la declaración, el
registro y el uso. Tres sitios que hay que mantener sincronizados a mano. El día
que añades un sensor y te olvidas del registro, ese sensor no se lee nunca y no
hay ningún error: la señal simplemente se queda a `false` para siempre.

Con la tabla, esa desincronización es **imposible por construcción**, porque el
registro se genera de la misma fila que la declaración.

## 2. Cómo funciona por dentro

### 2.1 La guarda de uso, que va primero por un motivo serio

```cpp
#if !defined(CFSM_TABLE_DI) && !defined(CFSM_TABLE_DO) && !defined(CFSM_TABLE_AI)
  #error "io/IOTable.h se incluye desde tu HardwareConfig.h, DESPUES de definir ..."
#endif
```

Este archivo **genera** la definición de `struct CfsmHardware` a partir de tus
macros. Si una segunda unidad de compilación lo incluyera sin esas macros
definidas, vería una estructura **distinta con el mismo nombre**.

El enlazador no dice nada —es una violación silenciosa de la regla de definición
única, la ODR— y el resultado es que un módulo lee los campos del objeto `HW` en
los *offsets equivocados*. Sale basura, y no hay forma humana de relacionar el
síntoma con la causa: un LED que se enciende cuando pulsas otro botón, un
antirrebote que lee el filtro del sensor de al lado.

Por eso el `#error`. Convierte un fallo indepurable en un mensaje que te dice
exactamente qué hacer. Y por eso `CoreFSM.h` **no** incluye `IOTable.h`, aunque
incluya todo lo demás.

Justo después, las tres tablas vacías por defecto, para que no haga falta
declarar las tres si tu máquina no tiene analógicas:

```cpp
#ifndef CFSM_TABLE_DI
  #define CFSM_TABLE_DI(ROW)
#endif
```

### 2.2 Qué es una X-macro

El truco es escribir la tabla como **una macro que recibe otra macro**:

```cpp
#define CFSM_TABLE_DI(ROW)                  \
  ROW(  2, Pulsador_Marcha,  true,  20 )    \
  ROW(  3, FC_Trabajo,       true,   5 )
```

`CFSM_TABLE_DI` no genera nada por sí misma: es un molde. Lo que genera depende
de con qué `ROW` la expandas. Expandiéndola con distintas `ROW` se obtienen cosas
distintas de **la misma tabla**: la declaración de los objetos, el registro en el
gestor, la cuenta de cuántos hay, el volcado de diagnóstico.

Cada expansión es una "pasada". El patrón se llama X-macro, tiene cuarenta años
de uso en C embebido, y su virtud es exactamente la que necesitamos: una sola
fuente, varias salidas, imposible desincronizarlas.

### 2.3 Pasada 1 — contar filas en tiempo de compilación

```cpp
#define CFSM_ROW_COUNT(...) +1

static const uint8_t CFSM_DI_COUNT = 0 CFSM_TABLE_DI(CFSM_ROW_COUNT);
```

Cada fila se expande a `+1`, así que la línea de arriba se convierte
literalmente en:

```cpp
static const uint8_t CFSM_DI_COUNT = 0 +1 +1;      // = 2
```

Se resuelve en tiempo de compilación, de modo que el array del `DeviceManager`
sale **dimensionado exacto**: ni un byte de RAM de más. `CFSM_ROW_COUNT` usa
`(...)` porque tiene que tragarse filas de 4 argumentos (DI), de 3 (DO) y de 2
(AI) sin quejarse.

Y de ahí sale:

```cpp
DeviceManager<CFSM_IO_COUNT ? CFSM_IO_COUNT : 1> devices;
```

El `? : 1` está porque **un array de tamaño cero es ilegal en C++**. Si no
declaras ninguna señal, el ternario deja un array de un puntero que no se usa,
y el archivo sigue compilando.

### 2.4 Pasada 2 — declarar los objetos

```cpp
#define CFSM_ROW_DECL_DI(pin, name, pullup, debounce) \
  DigitalSensor name{pin, pullup, debounce};
```

Aquí ocurre lo bonito: **el segundo argumento de la fila se convierte en el
nombre de la variable en C++**. `name` no es una cadena, es un identificador que
el preprocesador pega tal cual.

### 2.5 Pasada 3 — registrar, y la doble vida del nombre

```cpp
#define CFSM_ROW_REG_DI(pin, name, pullup, debounce) \
  devices.registerDevice(&name, F(#name));
```

El `#name` es el operador de **cadenización** del preprocesador: convierte el
identificador en una cadena literal. Así el mismo texto de la tabla vive dos
veces, como identificador de C++ y como texto de diagnóstico, **y no pueden
divergir jamás**. Renombras la fila y se renombran los dos.

### 2.6 El antes y el después, con una tabla de tres filas

Esto es lo que escribes:

```cpp
/*      PIN | NOMBRE SIMBOLICO | PULL-UP | ANTIRREBOTE(ms) */
#define CFSM_TABLE_DI(ROW)                        \
  ROW(   2,   Pulsador_Marcha,   true,   20 )     \
  ROW(   3,   FC_Trabajo,        true,    5 )

/*      PIN | NOMBRE SIMBOLICO | ACTIVO A BAJO */
#define CFSM_TABLE_DO(ROW)                        \
  ROW(  13,   Luz_Roja,          false )

#include <io/IOTable.h>
```

Y esto es, literalmente, el C++ que ve el compilador:

```cpp
static const uint8_t CFSM_DI_COUNT = 0 +1 +1;    // 2
static const uint8_t CFSM_DO_COUNT = 0 +1;       // 1
static const uint8_t CFSM_AI_COUNT = 0;          // 0
static const uint8_t CFSM_IO_COUNT = 3;

struct CfsmHardware {
  DigitalSensor Pulsador_Marcha{2, true, 20};
  DigitalSensor FC_Trabajo{3, true, 5};
  DigitalOutput Luz_Roja{13, false};

  DeviceManager<CFSM_IO_COUNT ? CFSM_IO_COUNT : 1> devices;   // vale 3

  void begin() {
    devices.registerDevice(&Pulsador_Marcha, F("Pulsador_Marcha"));
    devices.registerDevice(&FC_Trabajo,      F("FC_Trabajo"));
    devices.registerDevice(&Luz_Roja,        F("Luz_Roja"));
    devices.beginAll();
  }

  void readInputs()   { devices.readAllInputs();   }
  void writeOutputs() { devices.writeAllOutputs(); }

  void printIoTable(Print& out) {
    out.println(F("---- IMAGEN DE PROCESO ----"));
    out.print(F(" DI: "));
    { Pulsador_Marcha.describe(out); out.print(' '); }
    { FC_Trabajo.describe(out);      out.print(' '); }
    out.println();
    ...
  }
};
```

Tres filas escritas, veinte líneas generadas. Y la línea que más importa es la
que **no** puedes olvidar: el `registerDevice` de cada una.

Ese bloque no es una reconstrucción a ojo: es lo que devuelve `gcc -E` sobre esa
misma tabla, con el formato arreglado. Si quieres verlo tú mismo con la tuya:

```bash
g++ -std=gnu++11 -E -I ruta/a/CoreFSM/src -I . mi_ino.cpp | sed -n '/struct CfsmHardware/,/^};/p'
```

Un detalle de esa salida que conviene entender: `CFSM_IO_COUNT` aparece tal cual
en el texto expandido, sin sustituir por `3`. Es porque **no es una macro**, es
un `static const uint8_t`. El preprocesador no lo toca; quien lo resuelve a 3 es
el compilador, y por eso vale igualmente como argumento de plantilla.

### 2.7 La instancia global

```cpp
#define CFSM_DEFINE_HARDWARE  CfsmHardware HW;
extern CfsmHardware HW;
```

El `extern` va en la cabecera: **declara** que existe un `HW` en algún sitio, sin
crearlo. La macro `CFSM_DEFINE_HARDWARE` es la que lo **crea**, y va una sola vez
en tu `.ino` o `main.cpp`. Es el reparto clásico de declaración y definición: si
el objeto se creara en la cabecera, cada unidad de compilación tendría el suyo y
el enlazador se quejaría de símbolos duplicados.

## 3. API completa

### Lo que defines tú

| Macro | Firma de cada fila |
|---|---|
| `CFSM_TABLE_DI(ROW)` | `ROW(pin, Nombre, pullup, antirreboteMs)` |
| `CFSM_TABLE_DO(ROW)` | `ROW(pin, Nombre, activoBajo)` |
| `CFSM_TABLE_AI(ROW)` | `ROW(pin, Nombre, filtro)` |
| `CFSM_DEFINE_HARDWARE` | Crea `HW`. Una sola vez, en el `.ino` |

### Lo que obtienes

| Símbolo | Qué es |
|---|---|
| `HW.<Nombre>` | El objeto completo de cada fila |
| `HW.begin()` | Registra y configura todo |
| `HW.readInputs()` / `HW.writeOutputs()` | Fases PAE y PAA |
| `HW.releaseAllForces()` / `HW.hasAnyForce()` | Forzados |
| `HW.printIoTable(Print&)` | Volcado de la imagen de proceso |
| `CFSM_DI_COUNT`, `CFSM_DO_COUNT`, `CFSM_AI_COUNT`, `CFSM_IO_COUNT` | Cuentas, en compilación |

## 4. Ejemplos

### 4.1 Un `HardwareConfig.h` completo

```cpp
#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

/*      PIN | NOMBRE SIMBOLICO        | PULL-UP | ANTIRREBOTE (ms) */
#define CFSM_TABLE_DI(ROW)                                  \
  ROW(   2,   Pulsador_Marcha,           true,    25 )      \
  ROW(   3,   FC_Carro_Trabajo,          true,     5 )      \
  ROW(   4,   FC_Carro_Reposo,           true,     5 )      \
  ROW(   5,   Seta_Emergencia,           true,     0 )

/*      PIN | NOMBRE SIMBOLICO        | ACTIVA A NIVEL BAJO */
#define CFSM_TABLE_DO(ROW)                                  \
  ROW(  12,   Motor_Carro,               false )            \
  ROW(  13,   Soldador,                  false )            \
  ROW(  11,   Rele_Aspiracion,           true  )

/*      PIN | NOMBRE SIMBOLICO        | FILTRO (0 crudo .. 8 muy suave) */
#define CFSM_TABLE_AI(ROW)                                  \
  ROW(  A0,   Consigna_Velocidad,        3 )

#include <io/IOTable.h>
#endif
```

Fíjate en el `0` de antirrebote de la seta: una parada de emergencia **no se
filtra**. Cada milisegundo de antirrebote es un milisegundo de retraso en parar
la máquina, y una seta es un contacto de acción positiva que no rebota como un
pulsador barato.

Y en el `true` del relé: los módulos de relé chinos se activan con nivel bajo. Si
te lo dejas, el relé arranca pegado.

### 4.2 El `.ino` que lo usa

```cpp
#include <Arduino.h>
#include "HardwareConfig.h"
#include "Proceso.h"

CFSM_DEFINE_HARDWARE           // <-- una sola vez, aquí

BlockManager<2> manager;
Proceso proceso;

void setup() {
  Serial.begin(115200);
  HW.begin();                  // pines e imagen de proceso
  HW.releaseAllForces();
  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();
  proceso.start();
}

void loop() {
  HW.readInputs();                                       // PAE
  proceso.marcha  = HW.Pulsador_Marcha.hasRisen();       // flanco
  proceso.enTrabajo = HW.FC_Carro_Trabajo.isTriggered(); // nivel
  manager.setEmergencyStop(HW.Seta_Emergencia.isTriggered());
  manager.updateAll();                                   // OB1
  HW.Motor_Carro.set(proceso.motorMarcha);
  HW.Soldador.setMode(proceso.soldando ? OUT_BLINK_FAST : OUT_OFF);
  HW.writeOutputs();                                     // PAA
}
```

### 4.3 Añadir una señal

```diff
 #define CFSM_TABLE_DI(ROW)                                  \
   ROW(   2,   Pulsador_Marcha,           true,    25 )      \
   ROW(   3,   FC_Carro_Trabajo,          true,     5 )      \
   ROW(   4,   FC_Carro_Reposo,           true,     5 )      \
-  ROW(   5,   Seta_Emergencia,           true,     0 )
+  ROW(   5,   Seta_Emergencia,           true,     0 )      \
+  ROW(   6,   Puerta_Cerrada,            true,    50 )
```

Una línea. Ya está declarada, configurada, registrada, leída cada scan y
apareciendo en `printIoTable()`. **Cuidado con la barra invertida:** la fila que
antes era la última tenía que llevarla al añadir otra debajo. Es el error
número uno de este archivo.

### 4.4 El volcado de la imagen de proceso

```cpp
if (Serial.available() && Serial.read() == 'i') HW.printIoTable(Serial);
```

```
---- IMAGEN DE PROCESO ----
 DI: [Pulsador_Marcha]=0 [FC_Carro_Trabajo]=1 [FC_Carro_Reposo]=0 [Seta_Emergencia]=0
 DO: [Motor_Carro]=1 [Soldador]=0 [Rele_Aspiracion]=1
 AI: [Consigna_Velocidad]=512
---------------------------
```

Llámalo **solo bajo petición**, nunca en cada scan: son varios cientos de bytes
por el `Serial` y el tiempo de ciclo se dispara.

## 5. Decisiones de diseño

**X-macro y no plantillas ni generación de código en el script.** Las plantillas
no pueden crear *nombres de miembros* nuevos: `HW.Pulsador_Marcha` como
identificador real solo se consigue con el preprocesador. Y generar el `.h`
entero desde Python habría funcionado, pero entonces la tabla dejaría de ser
legible y editable a mano, que es medio propósito.

**El `#error` en vez de un include guard normal.** Un guard silencioso habría
dejado pasar el fallo de ODR. Se prefiere no compilar.

**`CFSM_DEFINE_HARDWARE` es una macro y no una plantilla.** Podría ser
`CfsmHardware HW;` escrito a mano; la macro existe para que el sitio donde se
crea el objeto sea evidente y grepeable.

**Las tres tablas por separado y no una sola con un campo de tipo.** Cada tipo de
señal tiene un número distinto de parámetros —el antirrebote no significa nada en
una salida— y una tabla única obligaría a rellenar huecos.

**Se apoya en el preprocesador, con lo que eso tiene de malo.** Los errores de
compilación dentro de una macro son crípticos, y depurar la expansión requiere
`gcc -E`. Se aceptó porque la alternativa —escribir cada señal tres veces— es un
fallo silencioso, y un error críptico de compilación siempre es preferible a un
fallo silencioso en la máquina.

## 6. Errores frecuentes

**Olvidar la barra invertida al final de una fila.** El error que sale no
menciona la tabla: normalmente es un `expected ';'` en la línea del `#include`.
Si ves algo incomprensible ahí, cuenta las barras.

**Incluir `<io/IOTable.h>` antes de las tablas, o desde otro sitio.** Salta el
`#error` con el mensaje explicándolo. Es el archivo funcionando bien.

**Poner `CFSM_DEFINE_HARDWARE` en la cabecera o en dos archivos.** El enlazador
se queja de definición múltiple de `HW`. Va una sola vez, en el `.ino`.

**Editar a mano el `HardwareConfig.h` que genera el script de Wokwi.** Lo
reescribe en cada compilación y tus cambios se pierden. Ahí se toca el
`diagram.json` o el `corefsm.json`.

**Usar un nombre de fila que ya existe como símbolo.** `Serial`, `LED_BUILTIN`,
`Wire`… El nombre se convierte en un miembro de la estructura y también en una
cadena; si choca con una macro del core de Arduino, el error es indescifrable.
Usa nombres con guion bajo tipo `Piloto_Verde`.

**Poner antirrebote a una seta de emergencia.** Ver §4.1.

**Meter una fila con un pin que no existe o repetido.** No hay validación:
compila y se comporta mal. Es la carencia que va a resolver la tabla de
variables con definiciones de placa.

## 7. Coste

`IOTable.h` en sí no cuesta nada: es preprocesador. Lo que cuesta es lo que
genera.

| Objeto | RAM aproximada |
|---|---:|
| `DigitalSensor` | ~16 B |
| `DigitalOutput` | ~14 B |
| `AnalogSensor` | ~28 B |
| `DeviceManager<N>` | 2·N + 1 B |

Una máquina con 4 DI, 3 DO y 1 AI: 64 + 42 + 28 + 17 = **unos 151 bytes**, el
7 % de la RAM de un Nano. Es la partida más previsible de todas, y por eso la
cuenta exacta de la pasada 1 merece la pena.

## 8. Relación con el resto

```
   diagram.json (Wokwi)
        │  tools/wokwi2corefsm.py
        ▼
   HardwareConfig.h        ← tu tabla, generada o a mano
        │  define CFSM_TABLE_DI / DO / AI
        │  y luego incluye
        ▼
   io/IOTable.h            ← estás aquí
        │  cuatro pasadas del preprocesador
        ▼
   struct CfsmHardware  ──▶  HW
        │
        ├── DigitalSensor / DigitalOutput / AnalogSensor  (los objetos)
        └── DeviceManager<CFSM_IO_COUNT>                  (la imagen)

   CoreFSM.h NO incluye este archivo. Es deliberado: ver §2.1.
```
