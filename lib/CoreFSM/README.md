# CoreFSM 2.1

**Framework de automatización para Arduino.** Traslada el modelo de programación de
un autómata industrial —ciclo de scan determinista, imagen de proceso, bloques
funcionales, secuencias por pasos, palabras de mando y estado, recetas y
alarmas— a C++ sobre microcontroladores.

Compatible con AVR (Nano, Uno, Mega), ESP32, ESP8266, RP2040 y SAMD.
Sin memoria dinámica. Sin `delay()`. Sin dependencias externas.

---

## Índice

1. [Por qué existe](#1-por-qué-existe)
2. [Instalación](#2-instalación)
3. [La idea central: el ciclo de scan](#3-la-idea-central-el-ciclo-de-scan)
4. [Las tres capas](#4-las-tres-capas)
5. [Tu primer bloque, paso a paso](#5-tu-primer-bloque-paso-a-paso)
6. [Los tres relojes: scan, ciclo y espera](#6-los-tres-relojes-scan-ciclo-y-espera)
7. [Referencia rápida](#7-referencia-rápida)
8. [Wokwi como única fuente de verdad](#8-wokwi-como-única-fuente-de-verdad)
9. [Recetas](#9-recetas)
10. [Diagnóstico](#10-diagnóstico)
11. [Consumo de memoria](#11-consumo-de-memoria)
12. [Errores frecuentes](#12-errores-frecuentes)
13. [Mapa de archivos](#13-mapa-de-archivos)

---

## 1. Por qué existe

Si vienes de programar autómatas, al llegar a Arduino echas de menos cosas que
allí venían de serie y que aquí, sencillamente, no están:

| En un PLC | En Arduino a pelo |
|---|---|
| El firmware lee todas las entradas antes del programa (PAE) | Haces `digitalRead()` donde te pilla |
| Tabla de variables con nombres simbólicos | Constantes con números de pin repartidas por el código |
| El OB1 llama a tus bloques cíclicamente | Un `loop()` que crece hasta ser inmanejable |
| Bloques de función reutilizables con interfaz | Todo mezclado en el mismo archivo |
| Temporizadores TON/TOF normalizados | `millis()` y variables sueltas de estado |
| Tabla de observación en tiempo real | `Serial.println()` y adivinar |
| Palabras de estado que un HMI puede leer | Nada |
| Recetas separadas del programa | Números clavados en el código |

CoreFSM devuelve todo eso. No es una capa de azúcar sintáctico: es una forma
distinta de estructurar el programa, la misma que lleva cuarenta años
funcionando en fábricas.

**Lo que ganas en la práctica**

- La lógica de proceso no toca pines, así que la puedes probar en el simulador,
  en el PC o en otra placa sin cambiar una línea.
- Ninguna espera bloquea: mientras un cilindro tarda 2 segundos en salir, la
  máquina sigue vigilando la seta de emergencia.
- Cuando algo falla, el sistema dice **qué** falló, **dónde** y **cuándo**, en
  lugar de quedarse colgado en silencio.
- Añadir una segunda estación no obliga a tocar la primera.

---

## 2. Instalación

### Arduino IDE

Copia la carpeta `CoreFSM` completa en tu carpeta de librerías:

```
Windows   Documentos\Arduino\libraries\CoreFSM\
macOS     ~/Documents/Arduino/libraries/CoreFSM/
Linux     ~/Arduino/libraries/CoreFSM/
```

Debe quedar así:

```
libraries/CoreFSM/
├── library.properties      <- archivo, NO carpeta
├── keywords.txt
├── README.md
├── src/                    <- carpeta
│   ├── CoreFSM.h
│   ├── core/  io/  logic/  drive/  data/  diag/  comms/
├── examples/
└── tools/
```

Cierra y vuelve a abrir el IDE para que indexe la librería. Los ejemplos
aparecerán en **Archivo → Ejemplos → CoreFSM**.

> **Aviso para Windows.** Si al crear archivos a mano no ves las extensiones,
> el explorador te está engañando: puede que hayas creado `library.properties.txt`
> o `BlockBase.h.ino`, y entonces el IDE ignora la librería entera.
> Activa **Ver → Mostrar → Extensiones de nombre de archivo** antes de nada.
> Para quitar una extensión sobrante a varios archivos, desde PowerShell:
>
> ```powershell
> Get-ChildItem -Filter *.ino | Rename-Item -NewName { $_.Name -replace '\.ino$','' }
> ```

### PlatformIO

```ini
[env:nanoatmega328]
platform      = atmelavr
board         = nanoatmega328
framework     = arduino
monitor_speed = 115200
lib_deps      = file://../CoreFSM
extra_scripts = pre:tools/wokwi2corefsm.py   ; genera la tabla desde Wokwi
```

### Wokwi

Wokwi no lee carpetas de librerías locales: todos los archivos van planos en el
proyecto. Copia los `.h` que uses y cambia `<CoreFSM.h>` por `"CoreFSM.h"`
(comillas en vez de ángulos: le dice al compilador que busque al lado, no en las
librerías del sistema).

### Primer arranque

Compila **Ejemplo 1 → 01_PrimerBloque**. Si compila, la instalación es correcta.

### Si vienes de la versión 1

**Tu proyecto anterior sigue funcionando sin tocar una línea.** Está comprobado
compilando y ejecutando el `ProyectoTest_CoreFSM` original contra esta versión.

- `#include <BlockManager.h>`, `<SequenceBlock.h>`, `<FsmBlock.h>` y
  `<BlockBase.h>` siguen valiendo: los archivos reales viven ahora en `src/core/`
  y en la raíz quedan puentes que redirigen.
- `_currentState`, `_currentStep`, `setStep()`, `getTimeInStep()`,
  `isStepTimedOut()`, `fault()` y `onStepEntered()` conservan su significado.
- `start()` pasa directamente a `RUNNING` mientras no configures una fase de
  arranque con `setStartupTime()`, igual que antes.

Cuando quieras aprovechar lo nuevo, el cambio es de una línea: sustituye

```cpp
if (_currentState != STATE_RUNNING) { salidas = false; return; }
if (isStepTimedOut()) { fault(); return; }
```

por

```cpp
if (!updateSequence()) { salidas = false; return; }
```

Con eso ganas de golpe pausa en caliente, modo paso a paso, palabras de mando y
estado, vigilancia del ciclo completo y contador de piezas.

---

## 3. La idea central: el ciclo de scan

Todo lo demás se deriva de aquí. Un autómata no ejecuta tu programa "cuando pasa
algo": lo ejecuta **entero, una y otra vez, en tres fases fijas**.

```
        ┌──────────────────────────────────────────────────────┐
        │                                                      │
        ▼                                                      │
  ┌───────────┐      ┌────────────────┐      ┌──────────────┐   │
  │ 1. PAE    │ ──▶  │ 2. OB1         │ ──▶  │ 3. PAA       │ ──┘
  │ leer TODAS│      │ calcular con   │      │ escribir     │
  │ las       │      │ esa foto       │      │ TODAS las    │
  │ entradas  │      │ congelada      │      │ salidas      │
  └───────────┘      └────────────────┘      └──────────────┘
```

En CoreFSM son tres líneas, siempre las mismas:

```cpp
void loop() {
  HW.readInputs();        // PAE
  manager.updateAll();    // OB1
  HW.writeOutputs();      // PAA
}
```

### Por qué importa la coherencia

Como todas las entradas se leen en el mismo instante, la lógica trabaja con una
**foto congelada** de la planta. Si en cambio hicieras `digitalRead()` en medio
del razonamiento, un sensor podría cambiar a mitad de la función y llegarías a
conclusiones imposibles: ver el cilindro en reposo al principio y en trabajo al
final, y activar dos salidas incompatibles.

### Por qué nada puede bloquear

El scan debe durar microsegundos. Todo lo que lo alargue —un `delay()`, un
`while` de espera, un `Serial.println()` que llena el buffer— reduce la
capacidad de reacción de la máquina. Una máquina que tarda 20 ms en enterarse de
que se ha pulsado la seta es una máquina peligrosa.

En lugar de *«espera 2 segundos»*, se escribe *«si han pasado 2 segundos desde
que entré a este paso, cambia»*. Durante esos 2 segundos el scan sigue corriendo.

---

## 4. Las tres capas

```
  ┌──────────────────────────────────────────────────────────────┐
  │ NIVEL 3   ESTRATEGIA        SequenceBlock                    │
  │           "quiero que la pieza llegue al final de la cinta"  │
  │           Sabe: pasos, tiempos, condiciones, interbloqueos   │
  │           Ignora: cuántos motores hay y en qué pines         │
  ├──────────────────────────────────────────────────────────────┤
  │ NIVEL 2   COORDINACIÓN      DifferentialChassis, TowerLight  │
  │           "para girar, la rueda izquierda a 200 y la         │
  │            derecha a 110"                                    │
  │           Sabe: geometría, prioridades, reparto              │
  │           Ignora: por qué se quiere girar                    │
  ├──────────────────────────────────────────────────────────────┤
  │ NIVEL 1   ACTUACIÓN         MotorDrive, DigitalOutput        │
  │           "pin 6 a HIGH, pin 7 a LOW, PWM 200"               │
  │           Sabe: pines, PWM, protección del puente en H       │
  │           Ignora: si es una rueda, una cinta o un eje        │
  └──────────────────────────────────────────────────────────────┘
```

El día que cambies los motores de continua por servos, solo reescribes el nivel
1. Si cambias dos ruedas por cuatro, solo el nivel 2. El nivel 3 —donde está
todo el trabajo intelectual— no se toca.

---

## 5. Tu primer bloque, paso a paso

### 5.1 Dibuja la secuencia antes de escribir código

Coge papel. Anota:

- **Entradas**: qué sensores intervienen.
- **Salidas**: qué actuadores mueves.
- **Pasos**: numéralos de 10 en 10 (0, 10, 20, 30). Deja hueco para intercalar
  un paso 15 el día que haga falta, sin renumerar toda la secuencia.
- **Transiciones**: qué condición hace saltar de un paso al siguiente.
- **Vigilancias**: cuánto es *demasiado* en cada paso. Si el cilindro no llega
  en 5 segundos, algo va mal.

### 5.2 Escribe la lógica en un `.h`

Este archivo **no sabe nada de pines**.

```cpp
#ifndef MI_PROCESO_H
#define MI_PROCESO_H
#include <CoreFSM.h>

enum Pasos : uint16_t {
  PASO_REPOSO  = 0,
  PASO_AVANZAR = 10,
  PASO_TRABAJO = 20
};

class MiProceso : public SequenceBlock {
  public:
    // ENTRADAS: variables lógicas, no pines
    bool ordenMarcha = false;
    bool sensorFinal = false;

    // SALIDAS: variables lógicas, no pines
    bool motor    = false;
    bool actuador = false;

    void begin() override {
      setName(F("MI_PROCESO"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);
    }

    void update() override {
      // Enclavamiento general: si no está en marcha, salidas seguras y fuera.
      // Es LA línea más importante del bloque.
      if (!updateSequence()) { motor = false; actuador = false; return; }

      switch (_currentStep) {
        case PASO_REPOSO:
          motor = false;
          if (ordenMarcha) setStep(PASO_AVANZAR, 5000);   // 5 s de vigilancia
          break;

        case PASO_AVANZAR:
          motor = true;
          if (sensorFinal) { motor = false; setStep(PASO_TRABAJO); }
          break;

        case PASO_TRABAJO:
          actuador = true;
          if (getTimeInStep() >= 2000) {      // 2 s de proceso
            actuador = false;
            completeCycle();                  // cuenta la pieza
            setStep(PASO_REPOSO);
          }
          break;
      }
    }

    // Para que la telemetría diga "AVANZAR" y no "10"
    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case PASO_AVANZAR: return F("AVANZAR");
        case PASO_TRABAJO: return F("TRABAJO");
        default:           return F("REPOSO");
      }
    }

  protected:
    // Los mensajes van AQUÍ, no dentro del switch: esto corre una sola vez
    // por cambio de paso. Dentro del switch correría miles de veces por
    // segundo y bloquearía la CPU al llenarse el buffer serie.
    void onStepEntered(uint16_t step) override {
      if (step == PASO_AVANZAR) Serial.println(F("Avanzando..."));
    }
};
#endif
```

### 5.3 Conecta el hardware en el `.ino`

Aquí y **solo aquí** aparecen los números de pin.

```cpp
#include <CoreFSM.h>
#include "MiProceso.h"

DigitalSensor btnMarcha(2, true, 25);   // pin, conmuta a masa, 25 ms antirrebote
DigitalSensor fcFinal(3,   true, 10);
DigitalOutput salidaMotor(12);
DigitalOutput salidaActuador(13);
DeviceManager<4> io;

BlockManager<2> manager;
MiProceso       proceso;
StepTracer      tracer(proceso, Serial);

void setup() {
  Serial.begin(115200);
  io.registerDevice(&btnMarcha,      F("MARCHA"));
  io.registerDevice(&fcFinal,        F("FC_FINAL"));
  io.registerDevice(&salidaMotor,    F("MOTOR"));
  io.registerDevice(&salidaActuador, F("ACTUADOR"));
  io.beginAll();

  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();

  proceso.start();                 // la habilitación ya viene puesta
}

void loop() {
  io.readAllInputs();                              // 1. PAE

  proceso.ordenMarcha = btnMarcha.hasRisen();      // 2. planta -> bloque
  proceso.sensorFinal = fcFinal.isTriggered();     //    (flanco vs nivel)

  manager.updateAll();                             // 3. OB1

  salidaMotor.set(proceso.motor);                  // 4. bloque -> planta
  salidaActuador.set(proceso.actuador);
  tracer.update();

  io.writeAllOutputs();                            // 5. PAA
}
```

### 5.4 Nivel vs flanco: la distinción que más errores evita

```cpp
proceso.ordenMarcha = btnMarcha.hasRisen();     // el INSTANTE de pulsar
proceso.sensorFinal = fcFinal.isTriggered();    // el HECHO de estar activo
```

- **`hasRisen()`** es `true` durante **un solo ciclo de scan**. Úsalo para
  órdenes: marcha, rearme, cambio de receta.
- **`isTriggered()`** es `true` mientras la condición dure. Úsalo para estados
  del mundo: hay pieza, el cilindro está fuera, la puerta está cerrada.

Si pasas el nivel donde tocaba el flanco, mantener el dedo en el botón relanza
el ciclo miles de veces por segundo. Es el fallo número uno.

---

## 6. Los tres relojes: scan, ciclo y espera

Si solo te llevas una idea de todo este documento, que sea esta: **en
automatización la palabra "ciclo" significa dos cosas distintas**, y confundirlas
es el origen de la mayoría de las alarmas que nadie entiende.

| Reloj | Qué mide | Si se pasa | Quién lo vigila |
|---|---|---|---|
| **Scan** | Una pasada del programa: leer, calcular, escribir | Fallo de CPU | `ScanWatchdog` |
| **Ciclo de producción** | De pieza a pieza, solo tiempo **productivo** | Aviso, o alarma si es un límite duro | `SequenceBlock` |
| **Espera** | Máquina sana que no puede producir | **Nada** | `SequenceBlock` |

### 6.1 El watchdog de scan

Es el watchdog de un PLC. En un S7 se llama vigilancia del tiempo de ciclo,
viene de fábrica en unos 150 ms, y si el programa se pasa la CPU se va a STOP.
Aquí no había nada equivalente, y en un Arduino hace más falta que en un PLC:
nada te impide meter un `delay()` en mitad de un paso, dejar un
`Serial.println()` dentro del `switch`, o llamar a un ultrasonidos que sin eco
se come 25 ms él solo.

Cuando el scan se alarga la máquina no se para: **se vuelve mentirosa**. Los
antirrebotes muestrean más despacio y empiezan a perder flancos, los tiempos de
paso pierden resolución, y un pulsador rápido deja de detectarse a veces. Es el
tipo de fallo más difícil de encontrar que existe, porque no da error: da
comportamiento raro e intermitente.

```cpp
ScanWatchdog scan(20);            // 20 ms de scan máximo

void loop() {
  scan.begin();
  HW.readInputs();
  manager.updateAll();
  HW.writeOutputs();
  scan.end();
}
```

Y cuando quieras saber cómo va de holgado:

```
scan.report(Serial);
>> scan ult=1832us med=1790us max=4120us limite=20ms excesos=0 n=54211
```

Mirar ese `max` de vez en cuando dice más del estado real de un programa que
cualquier otra cosa. Si se acerca al límite, el programa te está pidiendo que
repartas el trabajo en más scans.

`enableHardwareWatchdog()` engancha además el watchdog físico del AVR, que
resetea la placa si el programa se cuelga de verdad. **Nace apagado y no se
activa solo**, por un motivo serio: en placas con el bootloader antiguo —muchos
Nano clónicos anteriores a 2018— un reset por watchdog deja la placa en un bucle
de reinicio del que no se sale ni cargando otro programa. Lee el aviso completo
en `diag/ScanWatchdog.h` antes de usarlo.

### 6.2 Esperar no es ir lento

Una máquina parada puede estarlo por tres motivos muy distintos, y meterlos en
el mismo saco es lo que hace que las alarmas dejen de significar algo:

1. Está **averiada** → alarma. Alguien tiene que venir. Baliza roja.
2. Está **esperando** → no es alarma. Está sana. Baliza ámbar.
3. Va **lenta pero produce** → aviso. No para nada. Ámbar intermitente.

La separación viene de PackML (ISA-TR88.00.02), el modelo de estados de máquina
de OMAC, y `SequenceBlock` toma de él sus dos estados de espera:

| Estado | Causa | Cómo sale |
|---|---|---|
| `STATE_SUSPENDED` | **Externa**: no llega pieza, la estación siguiente está llena | Sola, cuando la condición desaparece |
| `STATE_HELD` | **Interna** o del operario: recargar, control de calidad, ajuste | Cuando se cumple la condición propia |

Se declaran dentro del paso, y devuelven `true` mientras haya que esperar:

```cpp
case PASO_ESPERA_BOTE:
  valvula = false;
  if (suspendWhile(!botePresente)) break;    // causa externa
  setStep(PASO_LLENAR, 2500, 4000);
  break;

case PASO_RECARGA:
  if (holdWhile(!acuseRecarga)) break;       // causa interna
  setStep(PASO_EXPULSAR);
  break;
```

Mientras la espera está declarada, los cronómetros de paso y de ciclo se
**congelan**, así que ningún watchdog puede saltar por mucho que dure; el tiempo
se acumula aparte en `getBlockedTime()`; la STW dice `suspended` o `held`, de
modo que el HMI, la baliza y el maestro de línea saben *por qué* la máquina no
produce; y la lógica del paso sigue corriendo, que es justo lo que permite
volver. Si dejas de llamarlas, la máquina vuelve sola a `RUNNING` al scan
siguiente: no hay forma de quedarse colgado en una espera por olvido.

También descansar en el paso inicial está exento, y eso no hay que declararlo:
una máquina encendida sin trabajo no está dentro de ningún ciclo.

### 6.3 Vigilar un paso en dos escalones

Un solo umbral obliga a elegir entre avisar pronto (y llenarte de falsas
alarmas) o parar tarde. Los sistemas industriales usan dos, y `setStep()`
también:

```cpp
setStep(PASO_BAJAR, 3000, 5000);   // aviso a los 3 s, alarma a los 5 s
setStep(PASO_BAJAR, 5000);         // solo alarma, como siempre
setStep(PASO_BAJAR);               // paso libre, sin vigilancia
```

El primer escalón no para nada: enciende `stw.stepWarn` y llama una sola vez a
`onStepWarning()`. Sirve para **ver venir** la avería —la ventosa que cada vez
tarda un poco más, el cilindro que pierde aire— en vez de enterarte el día que
ya no llega.

### 6.4 El ciclo de producción: avisar y parar

```cpp
setCycleTarget(6000);     // takt objetivo -> AVISO, nunca alarma
setCycleTimeout(15000);   // límite duro   -> ALARMA
```

Que un ciclo tarde de más es un problema de **producción**, no de seguridad: en
una línea real eso enciende un aviso y se apunta para el rendimiento, no para la
máquina. Lo que sí la para es que un movimiento concreto no llegue, y de eso se
encarga la vigilancia de paso.

Entonces, ¿para qué sirve el límite duro de ciclo, si cada paso ya tiene el
suyo? Para cazar un fallo que la vigilancia de paso **no puede ver**: la
secuencia que va rebotando entre dos pasos sin agotar ninguno pero sin terminar
nunca. Cada `setStep()` reinicia el cronómetro del paso, así que ningún watchdog
de paso salta jamás; el del ciclo sí.

Y al cerrar cada pieza tienes los dos tiempos por separado, que es de donde sale
la disponibilidad de un OEE:

```cpp
maquina.getLastCycleTime()     // productivo
maquina.getLastBlockedTime()   // esperando
maquina.getTotalCycleTime()    // reloj de pared: la cadencia real
```

---

## 7. Referencia rápida

### Secuencias (`SequenceBlock`)

| Método | Qué hace |
|---|---|
| `updateSequence()` | Motor de la secuencia. **Primera línea de tu `update()`**. Devuelve `false` si no toca ejecutar pasos. |
| `setStep(paso, faultMs)` | Cambia de paso, reinicia su cronómetro y arma la vigilancia. `0` = sin vigilancia. |
| `setStep(paso, warnMs, faultMs)` | Igual, con dos escalones: el primero avisa, el segundo para. |
| `getTimeInStep()` | Milisegundos en el paso actual. Es tu temporizador no bloqueante. |
| `restartStep()` | Reintenta el paso actual desde cero. |
| `completeCycle()` | Cierra el ciclo: cuenta la pieza, mide la duración, encadena o para. |
| `suspendWhile(cond)` | Espera por causa **externa**. `true` mientras haya que esperar. Congela los relojes. |
| `holdWhile(cond)` | Espera por causa **interna** o del operario. Igual, pero pide intervención. |
| `isWaiting()` | ¿Está la máquina en una de las dos esperas? |
| `setCycleTimeout(ms)` | Límite duro del ciclo **productivo** → alarma. Caza la secuencia que no termina nunca. |
| `setCycleTarget(ms)` | Takt objetivo → **aviso**, nunca alarma. `isOverTakt()` lo consulta. |
| `getCycleCount()` / `getLastCycleTime()` | Producción y tiempo de ciclo productivo. |
| `getBlockedTime()` / `getLastBlockedTime()` | Tiempo esperando, contado aparte. Es el dato del OEE. |
| `getTotalCycleTime()` | Productivo + espera: la cadencia real de reloj de pared. |
| `onStepWarning(paso)` | Hook del primer escalón de la vigilancia. Se llama una vez, no para la máquina. |
| `handshake` | Interfaz de traspaso a la estación vecina. |

### Watchdog de scan (`ScanWatchdog`)

| Método | Qué hace |
|---|---|
| `begin()` / `end()` | Primera y última línea del `loop()`. |
| `lastUs()` `maxUs()` `avgUs()` | Duración del scan. `max` es el que importa. |
| `overruns()` / `isOverrun()` | Cuántas veces se pasó del límite, y si acaba de pasar. |
| `headroomPct()` | Margen que queda sobre el límite. Por debajo del 20 %, reparte el trabajo. |
| `report(Serial)` | Vuelca todo de una línea. |
| `enableHardwareWatchdog()` | Watchdog físico del AVR. **Lee el aviso antes de usarlo.** |

### Estados de máquina

```
IDLE ──start()──▶ STARTING ──▶ RUNNING ──hold()──▶ PAUSED ──resume()──┐
 ▲                                │  ▲                                 │
 │                                │  └─────────────────────────────────┘
 │                                │
 │                                ├──suspendWhile(c)──▶ SUSPENDED  (causa externa)
 │                                └──holdWhile(c)─────▶ HELD       (causa interna)
 │                                     ambos vuelven solos a RUNNING
 └── STOPPED ◀── STOPPING ◀──stop()┘

  Desde cualquier estado:  fault(código) ──▶ ERROR ──reset()──▶ IDLE
```

`stop()` es una parada ordenada que termina el ciclo. `hold()` congela el paso y
permite reanudar donde estaba. `abort()` corta en seco.

`SUSPENDED` y `HELD` **no son pausa ni avería**: son una máquina sana que no
puede producir ahora. La lógica del paso sigue ejecutándose en ellos —tiene que
hacerlo, es la que reevalúa la condición—, pero los cronómetros están
congelados. Ojo a la diferencia entre `isRunning()`, que es estricto y solo mira
`RUNNING`, e `isActive()`, que incluye las dos esperas.

### Bloques IEC 61131-3

```cpp
Ton  ton;   ton.PT = 2000;  ton.update(condicion);   // retardo a la conexión
Tof  tof;   tof.PT = 1500;  tof.update(condicion);   // retardo a la desconexión
Tp   tp;    tp.PT  = 200;   tp.update(disparo);      // impulso de duración fija
RTrig rt;   rt.update(senal);                        // flanco de subida
FTrig ft;   ft.update(senal);                        // flanco de bajada
SR   sr;    sr.update(set, reset);                   // enclavamiento, SET domina
RS   rs;    rs.update(set, reset);                   // enclavamiento, RESET domina
Ctu  ctu;   ctu.PV = 12;    ctu.update(pulso, rst);  // contador ascendente
Ctud ctud;  ctud.update(sube, baja);                 // contador bidireccional
Blink bl;   bl.setPeriod(500,500);  bl.update();     // onda cuadrada
```

`SR` frente a `RS`: cuando la orden de marcha y la de parada llegan a la vez,
`SR` da la marcha y `RS` da la parada. **Cuando hay seguridad de por medio, `RS`.**

### Objetos de campo

```cpp
DigitalSensor s(2, true, 20);  // pin, conmuta a masa, ms de antirrebote
  s.isTriggered()  s.hasRisen()  s.hasFallen()  s.isStableFor(2000)
  s.force(true)    s.releaseForce()

DigitalOutput o(13, false);    // pin, activo a nivel bajo
  o.turnOn()  o.set(v)  o.setMode(OUT_BLINK_FAST)  o.setMaxOnTime(5000)

AnalogSensor a(A0, 3);         // pin, intensidad del filtro
  a.value()  a.scaled()  a.setScale(102,921, 0,1000)  a.threshold()

UltrasonicSensor u(10, 11);
  u.cm()  u.isObstacle(25)

TowerLight baliza(rojo, amarillo, verde);
  baliza.reflect(bloque.getState())   // el código de colores, en una línea
```

### Motores

```cpp
MotorDrive m(5, 6, 9);      // IN1, IN2, PWM
m.setRamp(4);               // rampa: sin ella el pico de arranque reinicia la placa
m.enable();                 // la potencia se habilita ANTES de mover
m.runForward(200);

DifferentialChassis chasis(izq, der);
chasis.drive(v, w);         // v = avance, w = giro
FourWheelChassis c4(fl, fr, rl, rr);
c4.drive(vx, vy, w);        // vy solo tiene sentido con ruedas mecanum
```

---

## 8. Wokwi como única fuente de verdad

El error más caro de una puesta en marcha no es un fallo de lógica: es que el
plano diga una cosa, el cable esté en otro borne y el software apunte a un
tercer sitio. Tres fuentes de verdad que se desincronizan.

CoreFSM elimina dos de las tres.

```
   diagram.json (Wokwi)              <- dibujas y cableas aquí
          │
          │  wokwi2corefsm.py        <- se ejecuta antes de compilar
          ▼
   HardwareConfig.h                  <- tabla de variables generada
          │
          │  io/IOTable.h (X-Macros)
          ▼
   HW.Pulsador_Marcha.hasRisen()     <- ya existe en tu código
```

### Cómo se usa

1. En Wokwi, **renombra el `id`** de cada componente con el nombre que quieras
   que tenga la variable: `"id": "Pulsador_Arranque_Linea"`.
2. Ejecuta el generador:

```bash
python3 tools/wokwi2corefsm.py -i diagram.json -o HardwareConfig.h
```

3. En tu sketch:

```cpp
#include "HardwareConfig.h"
CFSM_DEFINE_HARDWARE        // crea la instancia global HW

void setup() { HW.begin(); }
void loop()  {
  HW.readInputs();
  if (HW.Pulsador_Arranque_Linea.hasRisen()) { ... }
  HW.writeOutputs();
}
```

Si mueves un cable del pin 2 al pin 8, al recompilar el software lee el pin 8.
No hay nada que actualizar a mano.

### Cómo decide qué es entrada y qué es salida

**Por prefijo en el nombre** (manda siempre):

| Prefijo | Papel |
|---|---|
| `DI_` `I_` `IN_` | entrada digital |
| `DO_` `Q_` `OUT_` | salida digital |
| `AI_` `E_` `AN_` | entrada analógica |

**Por tipo de componente** (respaldo automático): `wokwi-pushbutton` y
`wokwi-slide-switch` son entradas, `wokwi-led` y `wokwi-relay-module` son
salidas, `wokwi-potentiometer` es analógica.

### Ajustes finos: `corefsm.json`

Para lo que el esquema no puede expresar, pon este archivo junto al `diagram.json`:

```json
{
  "defaults": { "debounce": 20 },
  "pins": {
    "3": { "name": "FC_Carro_Trabajo", "role": "DI", "debounce": 5 },
    "7": { "name": "Rele_Bomba",       "role": "DO", "activeLow": true }
  },
  "ignore": ["led_decorativo"]
}
```

El generador avisa de pines duplicados, nombres repetidos y componentes que no
sabe clasificar. Con `--check` valida sin escribir nada.

### ¿Por qué no lo hace el Arduino en tiempo de ejecución?

Porque un microcontrolador corre sobre el metal: no tiene sistema de archivos ni
puede abrir un JSON al arrancar. La traducción tiene que ocurrir en el PC,
mientras se compila. De ahí que sea un script de Python.

---

## 9. Recetas

Sin recetas, la secuencia y los datos están mezclados:

```cpp
case PASO_BAJAR: eje.moveTo(120); ...        // el 120 está clavado en el código
```

Cambiar de producto obliga a recompilar, y cada modelo necesita su propia
versión del programa. Es lo que hace inmanejable una máquina que fabrica más de
una referencia.

Con recetas, el programa deja de contener números y se convierte en un
intérprete genérico: *«lee el paso de la receta activa, mueve los ejes a donde
diga, espera lo que diga, pasa al siguiente»*.

### Dónde vive cada cosa

| Memoria | Qué guarda | Tamaño típico |
|---|---|---|
| **Flash (PROGMEM)** | todas las recetas de fábrica | 30 KB libres en un Nano: caben más de cien |
| **EEPROM** | las que el operario aprende (teach-in) | 1 KB en un Nano: 2-4 recetas |
| **RAM** | **solo la receta activa** | ~90-260 bytes |

Cargar una receta es copiarla de flash o EEPROM al hueco de RAM, exactamente
como hace un centro de mecanizado al seleccionar un programa de pieza.

### Teach-in

Llevas los ejes a mano hasta la posición buena, pulsas un botón y esa posición
queda grabada:

```cpp
manipulador.teachStep(0, HERR_PINZA, 300);   // graba el paso 0
recetas.saveSlot(0);                          // lo guarda en EEPROM
```

Es como se programan de verdad los robots industriales. Nadie calcula
coordenadas a mano si puede llevar el brazo allí y decir *«aquí»*.

Ver **Ejemplo 5**.

---

## 10. Diagnóstico

### Trazador de pasos

Imprime una línea por cambio de paso y nada más. Es lo más parecido a mirar el
GRAFCET animado, y no satura el puerto porque solo escribe cuando pasa algo.

```cpp
StepTracer tracer(miBloque, Serial);
// en el loop, después de updateAll():
tracer.update();
```

```
[ESTADO] CINTA -> RUNNING
[PASO]   CINTA 0 -> 10 (TRANSPORTAR)
[PASO]   CINTA 10 -> 20 (PIEZA_EN_FIN)
[ESTADO] CINTA -> ERROR
   causa: 0x8001 Error de aplicación
```

### Consola de mantenimiento

Un intérprete de un solo carácter sobre el puerto serie. Es la versión mínima de
un panel de operador, y cubre casi todo lo que hace falta en una puesta en
marcha sin gastar un solo pin.

```cpp
MaintenanceConsole<4> consola(manager, Serial);
consola.update();   // en el loop
```

| Tecla | Acción |
|---|---|
| `w` | tabla de observación de bloques |
| `s` / `x` | marcha / paro |
| `p` | pausa / reanudar |
| `r` | rearme |
| `c` | estadísticas de tiempo de ciclo |
| `?` | ayuda |

### Estadísticas de ciclo

```cpp
manager.lastScanTimeUs();   // duración del último scan
manager.maxScanTimeUs();    // el peor caso desde el arranque
```

Si el máximo se dispara, hay algo que bloquea: un `delay()`, un `Serial` saturado
o un bucle de espera. Es el mismo dato que muestra un PLC en su pantalla de
diagnóstico, y sirve para lo mismo.

### Forzado de señales

```cpp
sensor.force(true);       // desconecta el pin y le impone un valor
sensor.releaseForce();
io.releaseAllForces();    // red de seguridad: quita todos los forzados
```

Reproduce el forzado de un PLC: probar la secuencia entera antes de que el
armario esté cableado, o seguir produciendo mientras se cambia un sensor
averiado. Igual que en un PLC, **es peligroso**: una señal forzada miente. La
tabla de observación marca todo lo que esté forzado.

### Alarmas

```cpp
AlarmManager<8> alarmas;
alarmas.raiseIf(condicion, 0x8001, F("Atasco en la cinta"), ALARM_FAULT);
alarmas.printAll(Serial);
```

Los tres estados de una alarma —activa sin acusar, activa acusada, e **inactiva
sin acusar**— existen por una razón: sin el tercero, un fallo intermitente (un
contacto flojo que falla una vez al día) sería invisible para siempre.

---

## 11. Consumo de memoria

Medido con `avr-size` sobre un Arduino Nano (ATmega328P: 30 KB de flash útiles,
2 KB de RAM). Programas completos, ya enlazados:

| Ejemplo | Flash | RAM |
|---|---|---|
| 01 Primer bloque | 9,2 KB (30 %) | 489 B (23 %) |
| 02 Proceso de soldadura | 12,3 KB (40 %) | 822 B (40 %) |
| 03 Cinta + baliza + tabla Wokwi | 13,5 KB (44 %) | 973 B (47 %) |
| 04 Dos estaciones con handshake | 12,8 KB (42 %) | 995 B (48 %) |
| 05 Recetas y configuración | 17,4 KB (58 %) | 1069 B (52 %) |
| 06 Robot de 4 ruedas | 13,7 KB (45 %) | 727 B (35 %) |
| 07 Seguimiento visual | 14,6 KB (48 %) | 870 B (42 %) |
| 08 Esperas y ritmo | 12,9 KB (43 %) | 833 B (40 %) |

**Lo que costó la 2.1.** Separar el tiempo de espera del productivo no es
gratis: son unos **+1,5 KB de flash y +26 B de RAM por programa** respecto a la
2.0. La mayor parte se va en aritmética de 32 bits, que en un micro de 8 bits no
es barata, y unos 0,5 KB corresponden a los dos avisos blandos (el escalón de
aviso del paso y el takt objetivo). Se valoró dejarlos detrás de un `#define`
para poder quitarlos, y se descartó: medio kilobyte no justifica llenar de
compilación condicional una librería cuya principal virtud es que se lee. El
ejemplo más pesado sigue por debajo del 60 % de un Nano.

Coste aproximado por elemento en RAM:

| Elemento | Bytes |
|---|---|
| `DigitalSensor` | ~16 |
| `DigitalOutput` | ~14 |
| `AnalogSensor` | ~28 |
| `MotorDrive` | ~24 |
| `SequenceBlock` (base) | ~40 |
| Receta activa (2 ejes, 4 pasos) | ~90 |
| Receta activa (3 ejes, 8 pasos) | ~260 |

**Si vas justo de RAM en un Nano:**

- Baja `CFSM_RECIPE_AXES` y `CFSM_RECIPE_MAX_STEPS`.
- Usa `IOManager` (4 B/señal) en lugar de la tabla de objetos (16 B/señal),
  a cambio de perder antirrebote y flancos.
- `#define CFSM_LOG_LEVEL 0` antes de incluir la librería: borra todas las
  trazas y sus textos.
- Envuelve **todos** los literales de texto en `F("...")`. En AVR, una cadena
  sin `F()` ocupa RAM permanentemente.

> Con menos de ~200 bytes de RAM libre, un AVR empieza a corromper la pila sin
> avisar: el programa hace cosas inexplicables. Deja siempre margen.

---

## 12. Errores frecuentes

**El programa arranca solo, sin pulsar nada**
Estás usando el nivel donde tocaba el flanco. Usa `hasRisen()`, no
`isTriggered()`, para las órdenes.

**El ciclo se repite mientras mantengo el botón**
Lo mismo. Y si además lees el pin a pelo con `digitalRead()`, no hay antirrebote.

**El monitor serie se congela y la máquina se ralentiza**
Tienes un `Serial.println()` dentro del `switch` de pasos, que corre miles de
veces por segundo. Muévelo a `onStepEntered()`.

**La máquina salta a fallo nada más arrancar**
El timeout del paso es más corto que lo que tarda el movimiento. Si el cilindro
tarda 1,5 s, `setStep(PASO, 500)` salta a fallo a los 0,5 s.

**El Arduino se reinicia solo al arrancar los motores**
Pico de corriente. Añade `motor.setRamp(3)` y alimenta los motores desde la
batería, nunca desde los 5 V de la placa. Une las masas.

**Se quemó el puente en H**
Órdenes de marcha adelante y atrás simultáneas. `MotorDrive` ya lo impide: si
llegan las dos, para. Asegúrate de pasar por él y no escribir los pines a mano.

**Cambié la estructura de configuración y ahora salen valores absurdos**
Los bytes viejos se están reinterpretando con la disposición nueva. Sube el
número de versión de `ConfigStore` y los datos antiguos se descartarán solos.

**`BlockManager.h: No such file or directory`**
El IDE no ve la librería. Casi siempre es que `library.properties` se llama en
realidad `library.properties.txt`, o que los `.h` son carpetas en lugar de
archivos. Activa las extensiones en el explorador y comprueba.

**El robot gira más (o menos) de 90 grados**
El giro por tiempo depende del suelo, del peso y de la carga de la batería.
Calibra `msGiro90`. Para precisión de verdad hacen falta encoders.

---

## 13. Mapa de archivos

```
CoreFSM/
├── library.properties
├── keywords.txt
├── README.md
│
├── src/
│   ├── CoreFSM.h                 include maestro
│   ├── BlockBase.h  FsmBlock.h   puentes de compatibilidad con proyectos
│   ├── SequenceBlock.h           anteriores (#include <BlockManager.h>)
│   ├── BlockManager.h
│   │
│   ├── core/
│   │   ├── CoreFSM_Platform.h    AVR / ESP32 / RP2040, reloj, PROGMEM
│   │   ├── ControlWords.h        CFGW, STW y códigos de error
│   │   ├── BlockBase.h           interfaz de todo bloque de lógica
│   │   ├── FsmBlock.h            estados de máquina (PackML reducido)
│   │   ├── SequenceBlock.h       pasos, timeouts, SC/ST, handshake
│   │   ├── BlockManager.h        ciclo de scan, E-Stop, estadísticas
│   │   ├── Handshake.h           traspaso de 4 fases entre estaciones
│   │   └── IAxis.h               interfaz de eje posicionable
│   │
│   ├── io/
│   │   ├── IDevice.h             interfaz de objeto de campo
│   │   ├── DeviceManager.h       imagen de proceso PAE/PAA
│   │   ├── DigitalSensor.h       antirrebote, flancos, forzado
│   │   ├── DigitalOutput.h       modos, invercion, watchdog + AnalogOutput
│   │   ├── AnalogSensor.h        filtro, escalado, histéresis
│   │   ├── UltrasonicSensor.h    HC-SR04 con filtro de mediana
│   │   ├── TowerLight.h          baliza con código de colores normalizado
│   │   ├── IOTable.h             tabla de variables X-Macro
│   │   └── IOManager.h           imagen de proceso por punteros (ligera)
│   │
│   ├── logic/
│   │   ├── Timers.h              TON, TOF, TP, Blink
│   │   ├── Edges.h               R_TRIG, F_TRIG, SR, RS, Toggle
│   │   └── Counters.h            CTU, CTD, CTUD, cuentahoras
│   │
│   ├── drive/
│   │   ├── MotorDrive.h          puente en H con enclavamientos y rampa
│   │   └── Chassis.h             cinemática 2 y 4 ruedas + eje posicionado
│   │
│   ├── data/
│   │   ├── ConfigStore.h         persistencia con magic + versión + CRC16
│   │   ├── DataBlock.h           DB con remanencia y autoguardado
│   │   ├── AlarmManager.h        alarmas con severidad y acuse
│   │   ├── RecipeTypes.h         estructuras de receta y catálogo
│   │   └── RecipeExecutor.h      intérprete genérico + teach-in
│   │
│   ├── diag/
│   │   ├── Logger.h              trazas por niveles, desactivables
│   │   ├── Telemetry.h           trazador, CSV, consola de mantenimiento
│   │   └── ScanWatchdog.h        vigilancia del tiempo de ciclo de scan
│   │
│   └── comms/
│       └── VisionSensor.h        cámara por serie + servocontrol visual
│
├── tools/
│   ├── wokwi2corefsm.py          diagram.json -> HardwareConfig.h
│   └── platformio.ini.ejemplo
│
└── examples/
    ├── 01_PrimerBloque/            las tres ideas básicas
    ├── 02_ProcesoSoldadura/        tu estación, ya con CoreFSM
    ├── 03_Conveyor_Semaforo/       cinta + baliza + tabla desde Wokwi
    ├── 04_DosEstaciones_Handshake/ dos estaciones coordinadas
    ├── 05_Recetas_y_Config/        recetas, EEPROM y teach-in
    ├── 06_Robot_4Ruedas/           coche con evitación de obstáculos
    ├── 07_Vision_Seguimiento/      seguimiento visual en lazo cerrado
    └── 08_Esperas_y_Ritmo/         esperas, takt objetivo y watchdog de scan
```

---

## Verificación

La librería no es solo "compila y parece que va". Se ha comprobado así:

**Compilación limpia**
Los ocho ejemplos y el proyecto de PlatformIO compilan con `avr-g++ -Wall
-Wextra` contra el núcleo real de Arduino para ATmega328P sin un solo aviso, y
enlazan en un binario completo (las cifras de la tabla anterior son de binarios
enlazados de verdad, no de objetos sueltos). El núcleo compila además con
`g++ -std=c++11` en un PC contra un `Arduino.h` simulado, lo que ejercita los
caminos de las plataformas sin memoria no volátil.

**Pruebas funcionales**
Un banco de pruebas en el PC ejecuta la máquina de estados durante cientos de
ciclos y comprueba que los pasos avanzan, los contadores cuentan, las recetas
se ejecutan de principio a fin y la herramienta se acciona en el paso correcto.
El reloj del banco es una variable, así que se simulan horas de máquina en
milisegundos y de forma reproducible.

**Pruebas del modelo de tiempo (2.1)**
88 comprobaciones sobre doce escenarios, todas en verde. Entre ellas: una espera
declarada de diez minutos que no dispara ninguna alarma y cuyo tiempo va al
contador de espera y no al de ciclo; la plantilla de la 2.0 **sin tocar una sola
línea**, cinco minutos en reposo, que antes caía en `CFSM_ERR_CYCLE_TIMEOUT` a
los 30 s y ahora no; la secuencia que rebota entre dos pasos vigilados, que
sigue disparando la alarma de ciclo porque es el fallo que esa vigilancia existe
para cazar; el paso que avisa a los 2 s y para a los 5, con el hook llamado
exactamente una vez; una pausa pedida en mitad de una espera, comprobando que el
tiempo de pausa y el de espera no se mezclan en el mismo contador; y el
comportamiento del `ScanWatchdog` ante un scan de 25 ms.

**Pruebas de regresión**
Una revisión adversarial del código encontró trece defectos reales —entre ellos
que la pausa no pausaba, que el watchdog de salida no enclavaba, que un motor
podía arrancar solo tras una parada de emergencia durante una inversión de
giro, y que el ejecutor de recetas nunca accionaba la herramienta—. Todos están
corregidos, y cada uno tiene una prueba que reproduce el escenario exacto que
fallaba. Las trece pasan.

Si encuentras un defecto nuevo, lo útil es el escenario: qué se pulsó, en qué
paso estaba y qué hizo la máquina.

---

## Licencia

MIT. Úsala, modifícala y llévatela a donde quieras.

**Autor:** Carlos González Rubio
