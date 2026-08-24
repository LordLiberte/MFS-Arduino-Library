# IAxis.h

> Cuarenta y dos líneas cuyo único trabajo es que el ejecutor de recetas no sepa qué hay al otro lado.

**Ruta:** `src/core/IAxis.h`
**Incluye:** `CoreFSM_Platform.h`
**Lo usan:** `data/RecipeExecutor.h`, y quien implemente un eje: `drive/` o código tuyo.

---

## 1. Qué problema resuelve

El ejecutor de recetas necesita mover ejes. Pero **no debe saber** si detrás hay
un motor de continua con un potenciómetro como realimentación, un servo de
modelismo, un paso a paso, o un cilindro neumático con dos finales de carrera.

Sin esta interfaz, `RecipeExecutor` tendría que incluir `MotorDrive.h`, y con él
`ControlWords.h`, y el PWM, y la rampa, y el tiempo muerto de inversión. Un
archivo de datos acabaría dependiendo de la capa de potencia. Y peor: el día que
cambies un motor de continua por un servo de verdad, habría que tocar el ejecutor
y **todas las recetas guardadas dejarían de valer**.

Con la interfaz, el ejecutor habla siempre el mismo idioma —"vete a la posición
120 a velocidad 180 y avísame cuando llegues"— y cada tipo de eje lo resuelve
como sepa. Las recetas guardadas en EEPROM siguen valiendo tal cual.

Es el mismo motivo por el que un PLC direcciona un eje por su bloque tecnológico
y no por los bits del variador.

## 2. Cómo funciona por dentro

No hay "por dentro": es una clase abstracta pura, sin un solo dato miembro. Lo
único que aporta es el contrato.

```cpp
class IAxis {
  public:
    virtual ~IAxis() {}
    virtual void    moveTo(int16_t target, uint8_t speed) = 0;
    virtual bool    inPosition(int16_t tolerance) const = 0;
    virtual int16_t position() const = 0;
    virtual void    hold() = 0;
    virtual bool    isHomed() const { return true; }
};
```

Cinco métodos, y cada uno está donde está por una razón:

**`moveTo(target, speed)` no bloquea.** Solo fija la consigna y vuelve. Quien
mueve de verdad es el `update()` del eje, en su turno del scan. Si `moveTo()`
esperase a llegar, todo el modelo de scan se vendría abajo.

**`inPosition(tolerance)` recibe la tolerancia como parámetro** en vez de ser una
propiedad del eje. Es deliberado: la misma máquina puede querer 2 unidades de
tolerancia en un posicionamiento fino y 20 en un movimiento de aproximación, y la
receta es quien lo sabe, no el eje.

**`position()` devuelve `int16_t`**, con signo, porque un eje puede tener
posiciones negativas respecto de su origen. Y de 16 bits porque en un AVR un
`int32_t` por eje, multiplicado por los ejes de una receta, se nota.

**`hold()` detiene manteniendo la posición**, que no es lo mismo que soltar. Un
eje vertical que "para" soltando se cae.

**`isHomed()` tiene implementación por defecto que devuelve `true`.** Es el único
método con cuerpo, y está pensado así para que un eje que no necesita referencia
—un servo con realimentación absoluta, un cilindro con finales de carrera— no
tenga que escribir nada. Los que sí la necesitan lo sobrescriben, y entonces el
ejecutor puede negarse a ejecutar una receta: **una coordenada no significa nada
para un eje que no sabe dónde está**.

El destructor virtual está porque la interfaz se usa por puntero. Sin él, borrar
un `IAxis*` que apunta a un eje concreto no llamaría al destructor del hijo.

## 3. API completa

| Método | Firma | Qué debe hacer tu implementación |
|---|---|---|
| `moveTo` | `virtual void moveTo(int16_t target, uint8_t speed) = 0` | Fijar consigna y velocidad. **No bloquear** |
| `inPosition` | `virtual bool inPosition(int16_t tolerance) const = 0` | ¿Estoy dentro de esa tolerancia del objetivo? |
| `position` | `virtual int16_t position() const = 0` | Posición actual según la realimentación |
| `hold` | `virtual void hold() = 0` | Parar **manteniendo** la posición |
| `isHomed` | `virtual bool isHomed() const` | ¿Referenciado? Por defecto `true` |

## 4. Ejemplos

### 4.1 Un eje neumático de dos posiciones

El caso más simple que existe, y el que enseña que "eje" no significa
necesariamente algo que se mueve de forma continua:

```cpp
class EjeNeumatico : public IAxis {
  DigitalOutput& _valvula;
  DigitalSensor& _fcAtras;
  DigitalSensor& _fcAdelante;
  int16_t _consigna = 0;

 public:
  EjeNeumatico(DigitalOutput& v, DigitalSensor& atras, DigitalSensor& adelante)
    : _valvula(v), _fcAtras(atras), _fcAdelante(adelante) {}

  void moveTo(int16_t target, uint8_t speed) override {
    CFSM_UNUSED(speed);                 // un cilindro va a la velocidad que va
    _consigna = target;
    _valvula.set(target >= 50);         // 0 = atrás, 100 = adelante
  }

  bool inPosition(int16_t tolerance) const override {
    CFSM_UNUSED(tolerance);             // aquí no hay tolerancia: o llega o no
    return (_consigna >= 50) ? _fcAdelante.isTriggered()
                             : _fcAtras.isTriggered();
  }

  int16_t position() const override {
    if (_fcAdelante.isTriggered()) return 100;
    if (_fcAtras.isTriggered())    return 0;
    return 50;                          // en tránsito
  }

  void hold() override { /* un cilindro no se para a medias */ }
};
```

Ignorar `speed` y `tolerance` con `CFSM_UNUSED` es lo correcto aquí, no un
apaño: la interfaz ofrece más de lo que este eje puede usar, y eso está bien.
Lo que importa es que una receta escrita para un servo se ejecuta igual sobre
este cilindro, con la salvedad de que las posiciones intermedias no existen.

### 4.2 Un eje de servo de modelismo

```cpp
class EjeServo : public IAxis {
  Servo _servo;
  int16_t _consigna = 90;
  uint8_t _pin;

 public:
  explicit EjeServo(uint8_t pin) : _pin(pin) {}
  void begin() { _servo.attach(_pin); _servo.write(_consigna); }

  void moveTo(int16_t target, uint8_t speed) override {
    CFSM_UNUSED(speed);                 // un servo no admite consigna de velocidad
    _consigna = constrain(target, 0, 180);
    _servo.write(_consigna);
  }

  /* Un servo de modelismo no tiene realimentación: se le supone llegado. */
  bool inPosition(int16_t tolerance) const override { CFSM_UNUSED(tolerance); return true; }
  int16_t position() const override { return _consigna; }
  void hold() override { _servo.write(_consigna); }
};
```

**El aviso honesto de este ejemplo:** `inPosition()` devuelve siempre `true`
porque un servo de modelismo no dice si ha llegado. Es una mentira benévola que
funciona si las recetas llevan un `dwellMs` suficiente, y que falla si el servo
se atasca. Un eje sin realimentación no puede detectar un atasco, y eso hay que
tenerlo presente al escribir la receta.

### 4.3 Un eje con referencia obligatoria

```cpp
class EjeConHome : public IAxis {
  bool _homed = false;
 public:
  void hacerHome() { /* ... buscar el detector de origen ... */ _homed = true; }
  bool isHomed() const override { return _homed; }
  // ... el resto ...
};
```

Con esto, `RecipeExecutor` puede negarse a arrancar y dar
`CFSM_ERR_NOT_HOMED` en vez de mandar el eje a una coordenada que no significa
nada.

### 4.4 Registrar los ejes en el ejecutor

```cpp
EjeServo      hombro(9), codo(10);
EjeNeumatico  pinza(valvulaPinza, fcPinzaAbierta, fcPinzaCerrada);

IAxis* ejes[3] = { &hombro, &codo, &pinza };
```

El ejecutor recorre ese array sin saber que dos son servos y el tercero un
cilindro.

## 5. Decisiones de diseño

**Cinco métodos y no quince.** La tentación es añadir `setLimits()`,
`setAcceleration()`, `getVelocity()`, `enable()`… Cada uno obligaría a todos los
tipos de eje a implementarlo, y un cilindro neumático no tiene aceleración
configurable. La interfaz se queda con lo que **todo** eje puede contestar.

**`isHomed()` no es pura.** Es la única concesión, y se hizo para que el caso
común —un eje que no necesita referencia— no tenga que escribir una línea.

**La tolerancia va en la llamada, no en el eje.** Ver §2.

**No hereda de `BlockBase`.** Un eje no es un bloque: es un actuador que alguien
mueve. Quien lo actualiza cada scan es su dueño (un `MotorDrive`, tu `.ino`), no
el `BlockManager`. Mezclarlo habría obligado a que todo eje tuviera `begin()`,
`getState()`, `isFaulted()` y `reset()`, que aquí no pintan nada.

## 6. Errores frecuentes

**Bloquear dentro de `moveTo()`.** Es la tentación grande: `while (!llegado);`.
Cuelga el scan entero. `moveTo()` fija la consigna y vuelve; llegar es cosa del
`update()` del eje.

**Devolver `true` en `inPosition()` sin realimentación.** Es admisible —lo hace
el ejemplo 4.2— pero solo si eres consciente de que ese eje no puede detectar un
atasco, y compensas con `dwellMs` en la receta.

**Olvidar el `virtual` en el `override`.** Si escribes `void moveTo(int16_t,
uint8_t)` sin `override`, y te equivocas en un tipo, creas una función nueva en
vez de sobrescribir, la clase sigue siendo abstracta y no compila. Pon siempre
`override`: convierte un error silencioso en un error del compilador.

**Confundir `hold()` con "apagar".** `hold()` mantiene la posición. Un eje
vertical que apaga se cae.

## 7. Coste

Cero RAM propia: no tiene datos miembro. Cada implementación paga **2 bytes** de
puntero a vtable, más lo que ocupen sus propios campos. En flash, una entrada de
vtable por método virtual y por clase implementadora.

## 8. Relación con el resto

```
                        IAxis.h
                     (el contrato)
                           ▲
          ┌────────────────┼────────────────┐
          │                │                │
    EjeServo          EjeNeumatico     MotorDrive con
    (tuyo)            (tuyo)           potenciómetro
          │                │                │
          └────────────────┼────────────────┘
                           │
                  RecipeExecutor.h
             (habla solo con IAxis*, no sabe
              qué hay debajo ni le importa)
```

Y esa flecha que no existe es la clave: **no hay ninguna dependencia de
`RecipeExecutor` hacia `drive/`**. Por eso las recetas sobreviven a un cambio de
hardware.
