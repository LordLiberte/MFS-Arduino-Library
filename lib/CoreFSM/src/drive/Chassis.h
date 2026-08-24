#ifndef COREFSM_CHASSIS_H
#define COREFSM_CHASSIS_H

#include "MotorDrive.h"
#include "../core/IAxis.h"

/* ===========================================================================
 *  Chassis.h  -  Coordinadores cinematicos (Nivel 2: el que sabe de geometria)
 * ---------------------------------------------------------------------------
 *  LA JERARQUIA DE TRES NIVELES
 *  ----------------------------
 *  Nivel 1  MotorDrive        Sabe de pines, PWM y proteccion del puente.
 *                             NO sabe si es una rueda, una cinta o un eje.
 *  Nivel 2  Chassis (esto)    Sabe de geometria: cuantas ruedas hay, a que
 *                             distancia y como repartir el movimiento.
 *                             NO sabe por que el robot quiere girar.
 *  Nivel 3  SequenceBlock     Sabe de proceso: "sigue la linea", "ve al punto
 *                             B". NO sabe cuantos motores hay.
 *
 *  El valor de esta separacion se ve el dia que cambias las ruedas por orugas,
 *  o pasas de dos motores a cuatro: solo se reescribe el Nivel 2. La secuencia
 *  que decide adonde ir no cambia ni una linea.
 * ======================================================================== */

/* ===========================================================================
 *  DifferentialChassis  -  Dos ruedas motrices (tanque / diferencial)
 * ---------------------------------------------------------------------------
 *  CINEMATICA INVERSA
 *  ------------------
 *  Se quiere que el robot avance a velocidad lineal v y gire a velocidad
 *  angular w. Con dos ruedas separadas una distancia L (ancho de via), cada
 *  rueda debe ir a:
 *
 *      v_izq = v - (w * L / 2)
 *      v_der = v + (w * L / 2)
 *
 *  La intuicion detras es sencilla: para girar a la derecha, la rueda derecha
 *  tiene que recorrer menos camino que la izquierda en el mismo tiempo. La
 *  diferencia de velocidad entre ambas es lo que produce el giro, y el ancho
 *  de via decide cuanta diferencia hace falta para un giro dado: un robot
 *  ancho necesita mas diferencia que uno estrecho para girar lo mismo.
 *
 *  Casos limite utiles:
 *      w = 0             -> ambas ruedas iguales: linea recta.
 *      v = 0, w != 0     -> ruedas opuestas: giro sobre el propio centro.
 *      v_izq = 0         -> giro pivotando sobre la rueda izquierda parada.
 *
 *  POR QUE HAY QUE NORMALIZAR
 *  --------------------------
 *  Si vas a v = 240 y pides w = 60, la rueda exterior deberia ir a 300. Pero
 *  el PWM se satura en 255. Si simplemente recortas, la rueda exterior se
 *  queda en 255 y la interior en 180: la diferencia real ya no es la pedida y
 *  el robot gira menos de lo que debia. El sintoma es un robot que sigue bien
 *  las curvas despacio y se sale en las rapidas.
 *
 *  La solucion es escalar las DOS ruedas por el mismo factor, conservando la
 *  proporcion. Se pierde velocidad absoluta, pero la trayectoria es la
 *  correcta. Entre ir mas despacio y no ir por donde toca, se elige lo primero.
 * ======================================================================== */

class DifferentialChassis {
  public:
    /* trackWidth: ancho de via en decimas de unidad arbitraria. Solo importa
     * su relacion con las velocidades, asi que se puede ajustar por prueba:
     * si el robot gira mas de lo pedido, subelo; si gira menos, bajalo. */
    DifferentialChassis(MotorDrive& left, MotorDrive& right, uint8_t trackWidth = 10)
      : _left(left), _right(right), _track(trackWidth) {}

    void enable()  { _left.enable();  _right.enable();  }
    void disable() { _left.disable(); _right.disable(); }
    void stop()    { _left.stop();    _right.stop();    }

    /* -----------------------------------------------------------------------
     *  MANDO VECTORIAL - la unica funcion que de verdad importa
     *    v: avance,  positivo adelante   (-255..255)
     *    w: giro,    positivo a derechas (-255..255)
     * -------------------------------------------------------------------- */
    void drive(int16_t v, int16_t w) {
      int16_t turn = (int16_t)(((int32_t)w * _track) / 10);
      int16_t l = v - turn;
      int16_t r = v + turn;
      normalize2(l, r);
      _left.setSignedSpeed(l);
      _right.setSignedSpeed(r);
    }

    /* --- Atajos legibles para las maniobras habituales --- */
    void forward(uint8_t s = 180)  { drive( (int16_t)s, 0); }
    void backward(uint8_t s = 180) { drive(-(int16_t)s, 0); }
    void spinLeft(uint8_t s = 190) { drive(0, -(int16_t)s); }
    void spinRight(uint8_t s= 190) { drive(0,  (int16_t)s); }

    /* Curva: avanza girando. bias va de -100 (todo a la izquierda) a +100. */
    void curve(uint8_t speed, int8_t bias) {
      drive((int16_t)speed, ((int16_t)speed * bias) / 100);
    }

    bool isMoving() const { return _left.isRunning() || _right.isRunning(); }

    void setTrackWidth(uint8_t w) { _track = w; }

  private:
    MotorDrive& _left;
    MotorDrive& _right;
    uint8_t     _track;

    /* Escalado proporcional para no perder la geometria del giro. */
    static void normalize2(int16_t& a, int16_t& b) {
      int16_t m = max(abs(a), abs(b));
      if (m > 255) {
        a = (int16_t)(((int32_t)a * 255) / m);
        b = (int16_t)(((int32_t)b * 255) / m);
      }
    }
};

/* ===========================================================================
 *  FourWheelChassis  -  Cuatro ruedas independientes
 * ---------------------------------------------------------------------------
 *  Con cuatro motores independientes se pueden hacer dos cosas distintas
 *  segun el tipo de rueda que lleve el chasis:
 *
 *    RUEDAS NORMALES (skid-steer, como un mini excavadora o un tanque):
 *      Las cuatro ruedas de un lado van siempre juntas. El giro se consigue
 *      arrastrando lateralmente los neumaticos, que derrapan. Funciona, gasta
 *      goma y consume bastante. El termino vy debe quedarse a 0.
 *
 *    RUEDAS MECANUM u OMNIDIRECCIONALES:
 *      Los rodillos inclinados a 45 grados de cada rueda descomponen el empuje
 *      en dos componentes. Combinando los cuatro sentidos, el chasis se puede
 *      desplazar LATERALMENTE sin girar, o en diagonal, o girar sobre si mismo
 *      manteniendo la orientacion del avance. Ahi si tiene sentido vy.
 *
 *  LAS ECUACIONES
 *  --------------
 *      v_FL = vx - vy - w        (Front Left,  delantera izquierda)
 *      v_FR = vx + vy + w        (Front Right, delantera derecha)
 *      v_RL = vx + vy - w        (Rear Left,   trasera izquierda)
 *      v_RR = vx - vy + w        (Rear Right,  trasera derecha)
 *
 *  Comprueba el patron de signos:
 *      - vx suma en todas: todas empujan hacia delante -> el robot avanza.
 *      - w suma en las de la derecha y resta en las de la izquierda: el lado
 *        derecho corre mas -> el robot gira a la izquierda.
 *      - vy forma una X (resta en FL y RR, suma en FR y RL): con ruedas
 *        mecanum, esa X produce un desplazamiento lateral puro.
 *
 *  Igual que en el caso de dos ruedas, hay que normalizar las CUATRO por el
 *  mismo factor cuando alguna se pasa de 255, o la trayectoria se deforma.
 * ======================================================================== */

class FourWheelChassis {
  public:
    FourWheelChassis(MotorDrive& fl, MotorDrive& fr, MotorDrive& rl, MotorDrive& rr)
      : _fl(fl), _fr(fr), _rl(rl), _rr(rr) {}

    void enable()  { _fl.enable();  _fr.enable();  _rl.enable();  _rr.enable();  }
    void disable() { _fl.disable(); _fr.disable(); _rl.disable(); _rr.disable(); }
    void stop()    { _fl.stop();    _fr.stop();    _rl.stop();    _rr.stop();    }

    /* vx: avance | vy: desplazamiento lateral (solo mecanum) | w: giro */
    void drive(int16_t vx, int16_t vy, int16_t w) {
      int16_t fl = vx - vy - w;
      int16_t fr = vx + vy + w;
      int16_t rl = vx + vy - w;
      int16_t rr = vx - vy + w;
      normalize4(fl, fr, rl, rr);
      _fl.setSignedSpeed(fl);
      _fr.setSignedSpeed(fr);
      _rl.setSignedSpeed(rl);
      _rr.setSignedSpeed(rr);
    }

    /* Version de dos argumentos para chasis con ruedas convencionales, donde
     * el desplazamiento lateral no es posible. */
    void drive(int16_t vx, int16_t w) { drive(vx, 0, w); }

    void forward(uint8_t s = 180)   { drive( (int16_t)s, 0, 0); }
    void backward(uint8_t s = 180)  { drive(-(int16_t)s, 0, 0); }
    void spinLeft(uint8_t s = 190)  { drive(0, 0, -(int16_t)s); }
    void spinRight(uint8_t s = 190) { drive(0, 0,  (int16_t)s); }
    void strafeLeft(uint8_t s= 180) { drive(0, -(int16_t)s, 0); }  /* solo mecanum */
    void strafeRight(uint8_t s=180) { drive(0,  (int16_t)s, 0); }  /* solo mecanum */

  private:
    MotorDrive& _fl; MotorDrive& _fr;
    MotorDrive& _rl; MotorDrive& _rr;

    static void normalize4(int16_t& a, int16_t& b, int16_t& c, int16_t& d) {
      int16_t m = max(max(abs(a), abs(b)), max(abs(c), abs(d)));
      if (m > 255) {
        a = (int16_t)(((int32_t)a * 255) / m);
        b = (int16_t)(((int32_t)b * 255) / m);
        c = (int16_t)(((int32_t)c * 255) / m);
        d = (int16_t)(((int32_t)d * 255) / m);
      }
    }
};

/* ===========================================================================
 *  PositionAxis  -  Eje posicionado en lazo cerrado
 * ---------------------------------------------------------------------------
 *  Convierte un motor de corriente continua corriente, SIN encoder, en algo
 *  parecido a un servo, usando un potenciometro acoplado al eje como
 *  realimentacion de posicion. Es la solucion clasica para un brazo casero.
 *
 *  EL CONTROL PROPORCIONAL
 *  -----------------------
 *      error    = consigna - posicion_real
 *      velocidad = Kp * error, saturada entre vMin y vMax
 *
 *  Cuanto mas lejos esta del destino, mas rapido va; al acercarse, frena solo.
 *  Es la version mas simple de un PID (solo el termino P) y para posicionar un
 *  eje mecanico suele bastar.
 *
 *  LAS DOS TRAMPAS DE UN LAZO P Y COMO SE RESUELVEN AQUI
 *  ----------------------------------------------------
 *  1. ZONA MUERTA. Al llegar cerca, el error es pequeno, la velocidad tambien,
 *     y el motor no tiene par suficiente para vencer su propio rozamiento: se
 *     queda zumbando sin moverse, calentandose. Por eso hay una tolerancia:
 *     dentro de ella se considera que ha llegado y se corta la corriente.
 *
 *  2. VELOCIDAD MINIMA. Por debajo de cierto PWM el motor simplemente no gira.
 *     Si el calculo da 20 y el motor necesita 60 para arrancar, el eje se
 *     queda parado a medio camino con el error sin corregir. Por eso, fuera de
 *     la tolerancia, la velocidad se eleva siempre al minimo util.
 *
 *  LIMITACION HONESTA: sin termino integral, un eje que trabaja contra la
 *  gravedad se quedara siempre un poco por debajo del destino. Para un brazo
 *  de aprendizaje es perfectamente aceptable; para precision de verdad hace
 *  falta un PID completo y realimentacion con encoder.
 * ======================================================================== */

class PositionAxis : public IAxis {
  public:
    PositionAxis(MotorDrive& motor) : _motor(motor) {}

    /* Realimentacion de posicion. Llamalo cada scan con el valor leido del
     * potenciometro o del encoder. */
    void setFeedback(int16_t actual) { _actual = actual; }

    /* Consigna de destino. Cumple la interfaz IAxis. */
    void moveTo(int16_t target, uint8_t speed) override {
      _target = target;
      _vMax   = speed ? speed : _vMax;
      _active = true;
    }
    void moveTo(int16_t target) { moveTo(target, _vMax); }

    bool inPosition(int16_t tolerance) const override {
      return abs(_target - position()) <= tolerance;
    }
    int16_t position() const override {
      return _isHomed ? (int16_t)(_actual - _homeOffset) : _actual;
    }
    bool isHomed() const override { return _isHomed; }

    void hold() override { _active = false; _motor.stop(); }
    void enable() { _motor.enable();  }
    void disable(){ _active = false; _motor.disable(); }

    /* Motor del lazo. Llamalo cada scan, despues de setFeedback().
     * Durante la busqueda de origen se aparta: el homing gobierna el motor por
     * su cuenta y aqui solo estorbaria. Si no se hiciera, llamar a update() y
     * a updateHoming() en el mismo scan -que es lo que dicen ambas- pararia el
     * motor en cada vuelta y el eje no llegaria nunca al tope. */
    void update() {
      if (_homing) return;
      if (!_active) { _motor.stop(); return; }

      int16_t error = _target - position();

      if (abs(error) <= _tolerance) {      /* zona muerta: hemos llegado */
        _motor.stop();
        _inPosition = true;
        return;
      }
      _inPosition = false;

      int32_t v = ((int32_t)error * _kp) / 10;
      if (v >  _vMax) v =  _vMax;
      if (v < -_vMax) v = -_vMax;
      if (v > 0 && v < _vMin) v =  _vMin;   /* vencer el rozamiento estatico */
      if (v < 0 && v > -_vMin) v = -_vMin;

      _motor.setSignedSpeed((int16_t)v);
    }

    bool    reached()    const { return _inPosition; }
    int16_t target()     const { return _target; }
    int16_t error()      const { return _target - position(); }

    /* kp va en decimas: kp = 15 significa una ganancia de 1,5.
     * Ajuste practico: sube kp hasta que el eje empiece a oscilar al llegar,
     * y luego bajalo a la mitad. */
    void tune(uint8_t kp, int16_t tolerance, uint8_t vMin, uint8_t vMax) {
      _kp = kp; _tolerance = tolerance; _vMin = vMin; _vMax = vMax;
    }

    /* -----------------------------------------------------------------------
     *  BUSQUEDA DE ORIGEN (HOMING)
     *  Un potenciometro da una posicion absoluta, pero su cero mecanico no
     *  coincide con el cero util del eje. El homing lleva el eje despacio
     *  contra su tope, y ahi declara el origen. Todas las coordenadas de las
     *  recetas se cuentan desde ese punto.
     *
     *  Sin homing, las coordenadas guardadas no significan nada: bastaria con
     *  que alguien moviera el brazo con la mano estando apagado para que todas
     *  las posiciones aprendidas apuntaran a otro sitio.
     * -------------------------------------------------------------------- */
    void startHoming(int8_t direction, uint8_t speed = 80) {
      _homing    = true;
      _homingDir = direction;
      _active    = false;
      _motor.enable();
      _motor.setSignedSpeed(direction > 0 ? (int16_t)speed : -(int16_t)speed);
    }

    /* Llamalo cada scan con el estado del final de carrera de origen.
     * Devuelve true en el ciclo en que la referencia queda hecha. */
    bool updateHoming(bool limitSwitchHit) {
      if (!_homing) return false;
      if (limitSwitchHit) {
        _motor.stop();
        _homing     = false;
        _homeOffset = _actual;    /* aqui esta el cero */
        _isHomed    = true;
        _target     = 0;
        _inPosition = true;
        return true;
      }
      return false;
    }

    int16_t relativePosition() const { return position(); }

  private:
    MotorDrive& _motor;
    int16_t _actual     = 0;
    int16_t _target     = 0;
    bool    _active     = false;
    bool    _inPosition = false;

    uint8_t _kp        = 15;   /* decimas: 15 = ganancia 1,5 */
    int16_t _tolerance = 5;
    uint8_t _vMin      = 70;
    uint8_t _vMax      = 220;

    bool    _homing     = false;
    int8_t  _homingDir  = 1;
    bool    _isHomed    = false;
    int16_t _homeOffset = 0;
};

#endif /* COREFSM_CHASSIS_H */
