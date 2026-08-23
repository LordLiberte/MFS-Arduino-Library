#ifndef COREFSM_MOTOR_DRIVE_H
#define COREFSM_MOTOR_DRIVE_H

#include "../io/IDevice.h"
#include "../core/ControlWords.h"
#include "../logic/Timers.h"

/* ===========================================================================
 *  MotorDrive.h  -  Accionamiento de un motor (Nivel 1: el musculo)
 * ---------------------------------------------------------------------------
 *  EL MODELO DE VARIADOR INDUSTRIAL
 *  --------------------------------
 *  Un motor industrial no se enciende "poniendo el pin a HIGH". Sigue un
 *  protocolo de habilitacion en capas, definido en PROFIdrive y CiA 402:
 *
 *      1. El variador arranca en "listo para conectar" (readyToSwitchOn).
 *      2. El control da ENABLE. El variador conecta la etapa de potencia y
 *         responde ENABLED. Hasta aqui hay tension pero no movimiento.
 *      3. Solo entonces se admite una orden de marcha (runFwd / runRev).
 *
 *  Parece burocracia, y es exactamente lo contrario: es lo que impide que un
 *  bit perdido, un puntero mal escrito o un arranque en caliente pongan un eje
 *  en movimiento sin que nadie lo haya pedido. La orden de movimiento no basta
 *  por si sola; hace falta que alguien haya habilitado antes.
 *
 *  LOS TRES ENCLAVAMIENTOS QUE IMPLEMENTA ESTA CLASE
 *  -------------------------------------------------
 *  1. ENCLAVAMIENTO DE SENTIDO. Si por un error de logica se piden marcha
 *     adelante y marcha atras a la vez, NO se elige una: se para el motor.
 *     En un puente en H, activar las dos ramas simultaneamente pone Vcc en
 *     cortocircuito con masa a traves de los transistores, y eso destruye el
 *     puente en milisegundos. Es un fallo de software que rompe hardware.
 *
 *  2. TIEMPO MUERTO EN LA INVERSION (dead time). Al cambiar de sentido, se
 *     apagan ambas ramas y se espera unos milisegundos antes de energizar la
 *     contraria. Los transistores tardan en cortar, y sin esa pausa hay un
 *     instante en que las dos ramas conducen. Ademas, invertir un motor en
 *     marcha sin frenar antes provoca un pico de corriente de dos a tres veces
 *     la de arranque.
 *
 *  3. RAMPA DE ACELERACION. Arrancar a plena potencia hunde la tension de la
 *     bateria y reinicia el microcontrolador. Es la causa numero uno de
 *     "el robot se reinicia solo cuando arranca".
 * ======================================================================== */

class MotorDrive : public IDevice {
  public:
    /* Cableado tipico de un L298N / TB6612:
     *   in1, in2 -> sentido de giro
     *   pwm      -> velocidad (patilla EN del driver)
     * Si tu driver solo tiene DIR + PWM, pasa el mismo pin en in2 con
     * setSingleDirectionPin(). */
    MotorDrive(uint8_t in1, uint8_t in2, uint8_t pwm)
      : _in1(in1), _in2(in2), _pwm(pwm),
        _setpoint(0), _applied(0), _rampStep(0), _deadTimeMs(30) {}

    /* ---------------- Interfaz de mando y estado (como un variador) ------- */
    struct {
      DriveStatusWord  stw;
      DriveControlWord cfgw;
      uint8_t  setpointSpeed = 0;   /* consigna 0..255                       */
      uint8_t  actualSpeed   = 0;   /* lo que realmente se esta aplicando    */
      uint16_t errorCode     = CFSM_ERR_NONE;
    } ST;

    void begin() override {
      pinMode(_in1, OUTPUT);
      pinMode(_in2, OUTPUT);
      pinMode(_pwm, OUTPUT);
      ST.stw.raw  = 0;
      ST.cfgw.raw = 0;
      ST.cfgw.quickStop      = true;   /* activo a bajo: true = sin parada */
      ST.stw.readyToSwitchOn = true;
      coast();
    }

    /* Fase PAA. Toda la logica de proteccion vive aqui, de modo que da igual
     * en que orden le llegaron las ordenes durante el scan: lo que se aplica
     * al hardware siempre ha pasado por los tres enclavamientos. */
    void writeOutputs() override {
      /* --- Rearme de averia --- */
      if (ST.cfgw.resetFault && ST.stw.fault) {
        ST.cfgw.resetFault = false;
        ST.stw.fault       = false;
        ST.errorCode       = CFSM_ERR_NONE;
        ST.stw.readyToSwitchOn = true;
      }

      /* --- Corte por averia, parada rapida o falta de habilitacion ---
       * Se limpian TAMBIEN el sentido actual y el pendiente. Si no, una
       * inversion interrumpida a mitad por la seta dejaria _pendingDir
       * enganchado, y al liberar la seta el motor arrancaria solo en el
       * sentido contrario sin que nadie se lo hubiera pedido. Es exactamente
       * lo que este bloque existe para impedir. */
      if (ST.stw.fault || !ST.cfgw.quickStop || !ST.cfgw.enable) {
        coast();
        ST.stw.enabled   = false;
        ST.stw.running   = false;
        ST.stw.fwdActive = false;
        ST.stw.revActive = false;
        _applied    = 0;
        _setpoint   = 0;
        _dir        = 0;
        _pendingDir = 0;
        ST.actualSpeed = 0;
        return;
      }

      ST.stw.enabled = true;

      /* --- Enclavamiento 1: sentido de giro --- */
      bool wantFwd = (ST.cfgw.runFwd || ST.cfgw.jogFwd);
      bool wantRev = (ST.cfgw.runRev || ST.cfgw.jogRev);

      if (wantFwd && wantRev) {
        /* Orden contradictoria. Ante la duda, parar; nunca adivinar. */
        wantFwd = wantRev = false;
        ST.stw.warning = true;
      } else {
        ST.stw.warning = false;
      }

      /* --- Enclavamiento 2: tiempo muerto al invertir --- */
      int8_t wantDir = wantFwd ? 1 : (wantRev ? -1 : 0);
      if (wantDir != 0 && _dir != 0 && wantDir != _dir) {
        /* Se ha pedido invertir estando en marcha: primero frenar. */
        _pendingDir = wantDir;
        _dir        = 0;
        _deadStart  = cfsm_millis();
        _applied    = 0;
        brake();
        ST.actualSpeed = 0;
        ST.stw.running = false;
        return;
      }
      if (_pendingDir != 0) {
        /* Si durante el tiempo muerto se ha retirado la orden de marcha, la
         * inversion pendiente se DESCARTA. Aplicarla igualmente arrancaria el
         * motor en sentido contrario sin orden vigente, que es justo el fallo
         * que estos enclavamientos existen para evitar. */
        if (wantDir == 0) {
          _pendingDir = 0;
        } else if (cfsm_elapsed(_deadStart) < _deadTimeMs) {
          brake();               /* seguimos dentro del tiempo muerto */
          return;
        } else {
          wantDir     = _pendingDir;
          _pendingDir = 0;
        }
      }

      _dir = wantDir;

      /* --- Enclavamiento 3: rampa de velocidad --- */
      _setpoint = (wantDir == 0) ? 0 : ST.setpointSpeed;
      if (_rampStep == 0) {
        _applied = _setpoint;
      } else if (cfsm_elapsed(_lastRamp) >= 1) {
        _lastRamp = cfsm_millis();
        int16_t diff = (int16_t)_setpoint - (int16_t)_applied;
        if      (diff >  (int16_t)_rampStep) _applied += _rampStep;
        else if (diff < -(int16_t)_rampStep) _applied -= _rampStep;
        else                                 _applied  = _setpoint;
      }

      /* --- Aplicacion al hardware --- */
      if (_dir > 0) {
        digitalWrite(_in1, HIGH); digitalWrite(_in2, LOW);
      } else if (_dir < 0) {
        digitalWrite(_in1, LOW);  digitalWrite(_in2, HIGH);
      } else {
        digitalWrite(_in1, LOW);  digitalWrite(_in2, LOW);
      }
      analogWrite(_pwm, _applied);

      ST.actualSpeed   = _applied;
      ST.stw.running   = (_applied > 0 && _dir != 0);
      ST.stw.fwdActive = (_dir > 0);
      ST.stw.revActive = (_dir < 0);
      ST.stw.atSetpoint= (_applied == _setpoint);
    }

    /* -----------------------------------------------------------------------
     *  MANDO DE ALTO NIVEL
     *  Fijate en que ninguno de estos metodos toca un pin: solo escriben bits
     *  en la palabra de mando. El hardware se toca en un unico sitio, la fase
     *  PAA de arriba, donde estan las protecciones. Esa disciplina es lo que
     *  hace que las protecciones no se puedan saltar por accidente.
     * -------------------------------------------------------------------- */
    void enable()  { ST.cfgw.enable = true;  }
    void disable() { ST.cfgw.enable = false; }

    void runForward(uint8_t speed) {
      ST.setpointSpeed = speed;
      ST.cfgw.runFwd = true;  ST.cfgw.runRev = false;
    }
    void runReverse(uint8_t speed) {
      ST.setpointSpeed = speed;
      ST.cfgw.runRev = true;  ST.cfgw.runFwd = false;
    }
    void stop() { ST.cfgw.runFwd = ST.cfgw.runRev = false;
                  ST.cfgw.jogFwd = ST.cfgw.jogRev = false; }

    /* Parada rapida. Recuerda: quickStop es activo a bajo. */
    void quickStop(bool active = true) { ST.cfgw.quickStop = !active; }

    void fault(uint16_t code = CFSM_ERR_DRIVE_FAULT) {
      if (!ST.stw.fault) { ST.stw.fault = true; ST.errorCode = code; }
    }
    void resetFault() { ST.cfgw.resetFault = true; }

    /* Consigna con signo, comoda para los coordinadores cinematicos:
     * positivo = adelante, negativo = atras, 0 = parar. */
    void setSignedSpeed(int16_t v) {
      if (v > 0)      runForward((uint8_t)(v > 255 ? 255 : v));
      else if (v < 0) runReverse((uint8_t)(-v > 255 ? 255 : -v));
      else            stop();
    }

    /* ---------------- Consulta ---------------- */
    bool isEnabled() const { return ST.stw.enabled; }
    bool isRunning() const { return ST.stw.running; }
    bool isFaulted() const { return ST.stw.fault;   }
    uint8_t speed()  const { return ST.actualSpeed; }

    /* ---------------- Configuracion ---------------- */

    /* Escalon de rampa en unidades PWM por milisegundo. 0 = sin rampa.
     * Empieza por 3-5: con eso el arranque tarda unos 60-80 ms y la fuente
     * no se hunde. */
    void setRamp(uint8_t stepPerMs)  { _rampStep = stepPerMs; }

    /* Tiempo muerto en la inversion de giro. 30 ms es un valor conservador y
     * seguro para un puente en H tipico. */
    void setDeadTime(cfsm_time_t ms) { _deadTimeMs = ms; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else out.print(CFSM_FSTR("MOTOR"));
      out.print(CFSM_FSTR("] v="));
      out.print(ST.actualSpeed);
      out.print(_dir > 0 ? CFSM_FSTR(" FWD") : (_dir < 0 ? CFSM_FSTR(" REV") : CFSM_FSTR(" ---")));
      out.print(CFSM_FSTR(" STW=0x"));
      out.print(ST.stw.raw, HEX);
      if (ST.stw.fault) out.print(CFSM_FSTR(" *AVERIA*"));
    }

  private:
    uint8_t     _in1, _in2, _pwm;
    uint8_t     _setpoint;      /* consigna tras aplicar los enclavamientos */
    uint8_t     _applied;       /* valor que realmente esta en el PWM       */
    uint8_t     _rampStep;
    cfsm_time_t _deadTimeMs;
    cfsm_time_t _deadStart = 0;   /* instante en que empezo el tiempo muerto */
    cfsm_time_t _lastRamp  = 0;
    int8_t      _dir        = 0;
    int8_t      _pendingDir = 0;

    /* Parada libre: las dos ramas abiertas, el motor gira por inercia. */
    void coast() {
      digitalWrite(_in1, LOW);
      digitalWrite(_in2, LOW);
      analogWrite(_pwm, 0);
    }

    /* Frenado activo. En un L298N o un TB6612, poner IN1 e IN2 al MISMO nivel
     * con la patilla de habilitacion ACTIVA cortocircuita el bobinado a traves
     * del puente, y el motor frena en seco.
     *
     * La diferencia con coast() esta en el PWM: con EN a cero el puente queda
     * en alta impedancia y el motor sigue girando por inercia (rueda libre);
     * con EN a 255 frena. Durante el tiempo muerto de una inversion interesa
     * lo segundo, porque el objetivo es precisamente que el eje se detenga
     * antes de energizar la rama contraria. */
    void brake() {
      digitalWrite(_in1, LOW);
      digitalWrite(_in2, LOW);
      analogWrite(_pwm, 255);
    }
};

#endif /* COREFSM_MOTOR_DRIVE_H */
