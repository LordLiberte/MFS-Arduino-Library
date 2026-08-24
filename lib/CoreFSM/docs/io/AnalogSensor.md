# AnalogSensor.h

> Una entrada analógica con filtro, escalado a unidades de ingeniería, umbral con histéresis y detección de rotura de hilo. Todo en enteros, sin una sola operación en coma flotante.

**Ruta:** `src/io/AnalogSensor.h`
**Incluye:** `IDevice.h`
**Lo usan:** `IOTable.h` (una fila `CFSM_TABLE_AI` genera uno), `MotorDrive.h` cuando el eje usa potenciómetro como realimentación.

---

## 1. Qué problema resuelve

Un `analogRead()` crudo devuelve cuentas entre 0 y `CFSM_ADC_MAX` que **no
significan una magnitud física**, tiemblan y no distinguen "vale cero" de "el
cable está cortado". Este
archivo añade las cuatro capas que un canal analógico de un autómata tiene de
serie:

1. **Filtro**, porque un ADC de 10 bits sin filtrar tiembla en el último bit o
   dos aunque la magnitud física esté quieta.
2. **Escalado**, para convertir cuentas del ADC en bares, milímetros o grados —
   exactamente el bloque SCALE de un PLC.
3. **Histéresis en los umbrales**, para que un valor que se queda justo en el
   límite no haga conmutar la salida cada dos scans.
4. **Diagnóstico de lazo**, para distinguir una medida baja de un sensor
   desconectado.

## 2. Cómo funciona por dentro

### 2.1 El filtro: media móvil exponencial en punto fijo

```cpp
void readInputs() override {
  _raw = _forced ? _simValue : (uint16_t)analogRead(_pin);

  if (_alpha == 0) {
    _filtered = ((uint32_t)_raw) << 8;
  } else {
    int32_t target = ((int32_t)_raw) << 8;
    _filtered += (target - (int32_t)_filtered) >> _alpha;
  }
  ...
}
```

Es una **media móvil exponencial** (EMA), la misma idea que el filtro de primer
orden de un canal analógico industrial: cada muestra acerca el valor filtrado un
poquito hacia la lectura nueva, y cuánto de poquito lo decide `_alpha`.

Dos decisiones dentro de esas tres líneas:

**Punto fijo 24.8.** `_filtered` no guarda cuentas, guarda cuentas multiplicadas
por 256 (`<< 8`). Sin esa resolución extra, la división por desplazamiento
perdería la parte fraccionaria en cada iteración y el filtro se **atascaría**:
con `alpha = 4`, una diferencia menor de 16 daría desplazamiento cero y el valor
no se movería nunca. Los 8 bits de fracción son lo que permite que converja.

**Desplazamiento en vez de división.** `>> _alpha` equivale a dividir entre
2^alpha, y en un AVR de 8 bits un desplazamiento cuesta un ciclo por bit
mientras que una división cuesta decenas.

Qué significa `alpha` en la práctica:

| `alpha` | Divide entre | Comportamiento |
|---:|---:|---|
| 0 | — | Sin filtro. La lectura cruda |
| 1-2 | 2-4 | Muy rápido, filtra poco |
| **3-5** | 8-32 | **Lo habitual**. Estable y con respuesta razonable |
| 6-8 | 64-256 | Muy suave, pero lento en responder |

El constructor y `setFilter()` limitan valores superiores a 8 para evitar
desplazamientos inválidos o filtros prácticamente inmóviles.

Un `alpha` alto en una señal que hay que seguir rápido (un lazo de posición) hace
que el lazo oscile, porque el filtro introduce retraso. Un `alpha` bajo en una
temperatura hace que el termostato conmute con el ruido. No hay un valor bueno
para todo.

### 2.2 El escalado, y por qué no hay `float`

```cpp
int32_t scaled() const {
  int32_t v = (int32_t)value();
  int32_t rawSpan = (int32_t)_scaleMaxRaw - (int32_t)_scaleMinRaw;
  if (rawSpan == 0) return _scaleMinEng;
  int32_t engSpan = _scaleMaxEng - _scaleMinEng;
  return _scaleMinEng + ((v - (int32_t)_scaleMinRaw) * engSpan) / rawSpan;
}
```

Una regla de tres, con la guarda de `rawSpan == 0` para que un escalado sin
configurar no divida entre cero.

**Se trabaja en enteros escalados —centésimas, décimas— en vez de con `float`.**
En un AVR sin unidad de coma flotante, cada operación en `float` cuesta cientos
de ciclos de reloj y se emula por software. Un ADC de 10 bits tiene 1024 valores
distintos. En ESP32 el valor por defecto es de
12 bits. Guardar bares en centésimas (`1000` = 10,00 bar) mantiene el cálculo en
aritmética entera.

Ejemplo real, un sensor de presión de 0-10 bar que entrega 0,5-4,5 V sobre un ADC
de 10 bits alimentado a 5 V:

```
0,5 V ─▶ 102 cuentas        4,5 V ─▶ 921 cuentas
setScale(102, 921, 0, 1000);        // 1000 = 10,00 bar
```

Fíjate en que el mínimo del sensor **no es 0 cuentas**: es 102. Esos 0,5 V de
offset son deliberados del fabricante, y son los que permiten el punto 4.

### 2.3 El umbral con histéresis

```cpp
bool threshold() {
  uint16_t v = value();
  if (!_thState && v >= _thOn)  _thState = true;
  if ( _thState && v <= _thOff) _thState = false;
  return _thState;
}
```

Dos umbrales distintos, no uno. Con `setThreshold(700, 600)`:

```
valor  ──────╭───╮────╭──╮─────────
   700 ─ ─ ─ ┼ ─ ┼ ─ ─┼ ─┼─ ─ ─ ─ ─
   600 ─ ─ ─ ┼ ─ ┼ ─ ─┼ ─┼─ ─ ─ ─ ─
             │   │    │  │
salida  ─────┘   └────┘  └─────      conmuta solo al cruzar de verdad
```

Un umbral simple oscila cuando la señal se queda justo encima: el ruido del ADC
la hace cruzar arriba y abajo decenas de veces por segundo. Con histéresis, la
señal tiene que **alejarse** para conmutar. Es exactamente lo que hace el
termostato de una caldera para no arrancar y parar cada dos segundos.

Nota de uso: `threshold()` **no es `const`** porque muta `_thState`. Llámalo una
vez por scan y guarda el resultado; llamarlo dos veces en el mismo scan no rompe
nada, pero es señal de que estás recalculando algo que ya tenías.

### 2.4 El diagnóstico de lazo

```cpp
void setValidRange(uint16_t lo, uint16_t hi);
bool isValid() const { uint16_t v = value(); return v >= _validLo && v <= _validHi; }
```

En un lazo industrial de 4-20 mA, **el cero útil es 4 mA, no 0**. Si llegan
0 mA, no es que la medida sea mínima: es que el cable está cortado. Poder
distinguir "vale cero" de "no hay sensor" evita accidentes — una máquina que
cree que la presión es baja actúa; una máquina que sabe que no tiene lectura
para.

Lo mismo por arriba: una señal pegada al fondo de escala suele ser un
cortocircuito, no una medida.

Con el sensor de presión del ejemplo, cuyo rango físico va de 102 a 921 cuentas:

```cpp
presion.setValidRange(80, 960);   // margen por tolerancias; fuera de ahí, avería
```

### 2.5 El registro de extremos

```cpp
uint16_t v = value();
if (v < _minSeen) _minSeen = v;
if (v > _maxSeen) _maxSeen = v;
```

Cuatro bytes que dicen mucho. Sirven para dimensionar —"¿de verdad uso todo el
rango del sensor o me sobra la mitad de la escala?"— y para diagnosticar picos
que ocurren entre dos consultas. `resetMinMax()` los rearma para medir por
turnos o por ciclos.

## 3. API completa

| Método | Firma | Qué hace |
|---|---|---|
| constructor | `AnalogSensor(uint8_t pin, uint8_t filterAlpha = 3)` | |
| `raw()` | `uint16_t` | Sin filtrar. Para diagnóstico |
| `value()` | `uint16_t` | Filtrado, en cuentas del ADC |
| `scaled()` | `int32_t` | En unidades de ingeniería |
| `minSeen()` / `maxSeen()` / `resetMinMax()` | | Extremos vistos |
| `setScale(rawMin, rawMax, engMin, engMax)` | | Configura el escalado |
| `setFilter(alpha)` | | Cambia el filtro en caliente |
| `setThreshold(onLevel, offLevel)` | | Umbral con histéresis |
| `threshold()` | `bool` | Evalúa el umbral. **No es `const`** |
| `setValidRange(lo, hi)` / `isValid()` | | Diagnóstico de lazo |
| `force(uint16_t rawValue)` | | Impone una lectura **cruda** |
| `describe(Print&)` | | `[Nombre]=512 (500) *FUERA DE RANGO*` |

## 4. Ejemplos

### 4.1 Un sensor de presión completo

```cpp
AnalogSensor presion(A1, 4);            // filtro medio

void setup() {
  ...
  presion.setScale(102, 921, 0, 1000);  // cuentas -> centésimas de bar
  presion.setValidRange(80, 960);       // fuera de ahí: lazo roto
  presion.setThreshold(400, 350);       // 4,00 bar sube, 3,50 baja
}

void loop() {
  io.readAllInputs();

  proceso.presionOk  = presion.threshold();
  proceso.presionBar = presion.scaled();       // 0..1000 = 0,00..10,00 bar

  alarmas.raiseIf(!presion.isValid(), ALM_LAZO_PRESION,
                  F("Sensor de presion desconectado o en corto"), ALARM_CRITICAL);
  ...
}
```

Y para imprimirlo con coma sin usar `float`:

```cpp
int32_t p = presion.scaled();
Serial.print(p / 100); Serial.print(','); 
if (p % 100 < 10) Serial.print('0');
Serial.println(p % 100);        // "7,25"
```

### 4.2 Un potenciómetro como consigna

```cpp
AnalogSensor consigna(A0, 5);           // filtro alto: una mano tiembla

void setup() { consigna.setScale(0, 1023, 0, 100); }   // 0-100 %

void loop() {
  io.readAllInputs();
  proceso.velocidadPct = (uint8_t)consigna.scaled();
}
```

El filtro alto aquí es lo correcto: nadie necesita que una consigna manual
responda en 2 ms, y sin filtrar el valor bailaría entre 47 y 49 sin parar.

### 4.3 Un potenciómetro como realimentación de posición

```cpp
AnalogSensor realimHombro(A2, 2);       // filtro BAJO: es un lazo cerrado
```

Aquí el criterio se invierte. Un filtro alto mete retraso en el lazo de posición
y el eje **oscila**: llega, se pasa, corrige, se pasa. Si tu brazo tiembla al
llegar a la posición, baja el `alpha` antes de tocar nada más.

### 4.4 Comprobar si estás aprovechando el sensor

```cpp
if (c == 'a') {
  Serial.print(F("Presion vista: ")); Serial.print(presion.minSeen());
  Serial.print(F(" .. "));            Serial.print(presion.maxSeen());
  Serial.print(F(" de 102..921 utiles"));
  Serial.println();
  presion.resetMinMax();
}
```

```
Presion vista: 380 .. 512 de 102..921 utiles
```

Ese renglón dice que estás usando el 16 % de la escala del sensor. Con un sensor
de 0-4 bar en vez de 0-10 tendrías seis veces más resolución en la zona que te
importa.

### 4.5 Forzar una analógica en la puesta en marcha

```cpp
presion.force(600);        // OJO: en cuentas CRUDAS, no en bares
```

`force()` recibe el valor **crudo** porque se inyecta en el mismo sitio donde
entraría el `analogRead()`: así pasa por el filtro y por el escalado igual que
una lectura real, y lo que ves por `scaled()` es coherente con lo que verías con
el sensor puesto.

## 5. Decisiones de diseño

**EMA y no media de N muestras.** Una media móvil de ventana necesita guardar
las N muestras: con N=16 son 32 bytes por canal. La EMA necesita un solo
acumulador y da un comportamiento equivalente para este uso.

**Punto fijo 24.8 y no `float`.** Ver §2.1 y §2.2.

**`threshold()` no es `const`.** Podría separarse en un `updateThreshold()` que
mutase y un `threshold() const` que consultase. Se dejó en uno solo por
simplicidad, asumiendo el precio de que un método que parece consulta tenga
efecto. Es la parte menos elegante del archivo.

**El escalado es lineal y no admite curva.** Un termopar o una NTC no son
lineales, y aquí no hay forma de meter una tabla de linealización. Si te hace
falta, el sitio es tu bloque: lee `value()` y aplica tu tabla. Meter
linealización aquí habría duplicado el tamaño del archivo para un caso que en un
Arduino es minoritario.

**`minSeen`/`maxSeen` registran el valor FILTRADO, no el crudo.** Así los picos
de ruido de una sola muestra no ensucian el registro. Si quieres ver el ruido
real, mira `raw()`.

## 6. Errores frecuentes

**Poner el mismo `alpha` a todo.** Alto para una consigna manual, bajo para un
lazo de posición. Ver §4.2 y §4.3.

**Olvidar `setScale()` y usar `scaled()`.** Por defecto el escalado es la
identidad (0-`CFSM_ADC_MAX` → 0-`CFSM_ADC_MAX`), así que `scaled()` devuelve
cuentas y parece que funciona. Luego alguien cambia resolución y nada cuadra.

**Cambiar `analogReadResolution()` sin ajustar `CFSM_ADC_MAX`.** Defínelo con la
resolución real antes de incluir CoreFSM; de lo contrario el escalado y rango
válido por defecto no coinciden con el ADC.

**Usar un umbral sin histéresis** (`setThreshold(700, 700)`). Vuelve a oscilar
como si no lo tuvieras.

**Confundir el orden de `setThreshold`.** Es `(onLevel, offLevel)` y para
comportamiento de termostato `onLevel > offLevel`. Al revés, la histéresis
trabaja en contra.

**Forzar en unidades de ingeniería.** `force()` es en cuentas crudas. Ver §4.5.

**Leer `A6`/`A7` de un Nano como entrada digital.** No es cosa de este archivo,
pero se junta: en el ATmega328 en encapsulado TQFP esos dos pines son
**solo analógicos**. Como `AnalogSensor` funcionan; como `DigitalSensor`, no.

## 7. Coste

| Miembro | Bytes |
|---|---:|
| `_pin`, `_alpha` | 2 |
| `_raw` | 2 |
| `_filtered` | 4 |
| `_scaleMinRaw`, `_scaleMaxRaw` | 4 |
| `_scaleMinEng`, `_scaleMaxEng` | 8 |
| `_thOn`, `_thOff`, `_thState` | 5 |
| `_validLo`, `_validHi` | 4 |
| `_minSeen`, `_maxSeen` | 4 |
| `_simValue` | 2 |
| `IDevice` | 5 |

**Unos 40 bytes**, el objeto de campo más caro de la librería. Es el precio de
llevar escalado, histéresis y diagnóstico dentro. Si solo necesitas leer un
potenciómetro, un `analogRead()` en tu bloque cuesta cero.

## 8. Relación con el resto

```
   analogRead(pin)
        │  fase PAE
        ▼
   AnalogSensor
        ├── filtro EMA (punto fijo 24.8) ──▶ value()
        ├── escalado lineal              ──▶ scaled()
        ├── umbral con histéresis        ──▶ threshold()
        ├── rango válido                 ──▶ isValid()  ──▶ AlarmManager
        └── extremos                     ──▶ minSeen()/maxSeen()

        └── es un IDevice ──▶ DeviceManager ──▶ HW.readInputs()

   Lo genera IOTable.h de cada fila CFSM_TABLE_AI, con el alpha
   como tercer parámetro.
```
