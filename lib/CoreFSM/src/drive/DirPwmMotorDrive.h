#ifndef COREFSM_DIR_PWM_MOTOR_DRIVE_H
#define COREFSM_DIR_PWM_MOTOR_DRIVE_H

#include "../io/IDevice.h"
#include "../core/ControlWords.h"

/* ===========================================================================
 *  DirPwmMotorDrive.h  -  Motor gobernado por un pin DIR y un pin PWM
 * ---------------------------------------------------------------------------
 *  Algunos shields de robot (por ejemplo, varios Keyestudio) no exponen las
 *  dos entradas de un puente en H. Exponen una direccion logica y una entrada
 *  de velocidad. No se deben adaptar pasando dos veces el mismo pin a
 *  MotorDrive: el frenado activo de ese driver pondria PWM a 255 durante una
 *  inversion y, en un interfaz DIR+PWM, eso significa velocidad maxima.
 *
 *  Esta clase aplica una secuencia especifica y segura al invertir:
 *
 *      PWM = 0  ->  esperar el tiempo muerto  ->  cambiar DIR  ->  acelerar
 *
 *  El cambio de DIR siempre ocurre con PWM a cero. Ademas se deja al menos
 *  una fase PAA completa entre el cambio de direccion y la primera subida de
 *  PWM. Es una proteccion de software; no sustituye la proteccion electrica
 *  ni un corte de potencia apropiado.
 * ======================================================================== */

class DirPwmMotorDrive : public IDevice {
  public:
    /* directionInverted=false: HIGH es avance y LOW es retroceso. */
    DirPwmMotorDrive(uint8_t dirPin, uint8_t pwmPin,
                     bool directionInverted = false)
      : _dirPin(dirPin), _pwmPin(pwmPin),
        _directionInverted(directionInverted) {}

    struct {
      DriveStatusWord  stw;
      DriveControlWord cfgw;
      uint8_t  setpointSpeed = 0;
      uint8_t  actualSpeed   = 0;
      uint16_t errorCode     = CFSM_ERR_NONE;
    } ST;

    void begin() override {
      pinMode(_dirPin, OUTPUT);
      pinMode(_pwmPin, OUTPUT);

      /* El orden importa: cortar PWM antes de fijar una direccion evita un
       * impulso si el latch del pin conserva un valor de un arranque previo. */
      analogWrite(_pwmPin, 0);
      digitalWrite(_dirPin, LOW);

      ST.stw.raw  = 0;
      ST.cfgw.raw = 0;
      ST.cfgw.quickStop = true;
      ST.stw.readyToSwitchOn = true;
      ST.setpointSpeed = ST.actualSpeed = 0;
      ST.errorCode = CFSM_ERR_NONE;

      _activeDir = _pendingDir = 0;
      _applied = 0;
      _lastRamp = cfsm_millis();
      _deadStart = _lastRamp;
    }

    void readInputs() override {}

    void writeOutputs() override {
      const cfsm_time_t now = cfsm_millis();

      bool resetRequested = ST.cfgw.resetFault;
      ST.cfgw.resetFault = false;
      if (resetRequested && ST.stw.fault) {
        ST.stw.fault = false;
        ST.stw.readyToSwitchOn = true;
        ST.errorCode = CFSM_ERR_NONE;
      }

      if (ST.stw.fault || !ST.cfgw.quickStop || !ST.cfgw.enable) {
        haltMotion(now);
        publishStatus(false, false);
        return;
      }

      bool wantFwd = ST.cfgw.runFwd || ST.cfgw.jogFwd;
      bool wantRev = ST.cfgw.runRev || ST.cfgw.jogRev;
      if (wantFwd && wantRev) {
        /* Una orden contradictoria nunca se resuelve eligiendo un sentido. */
        ST.stw.warning = true;
        haltMotion(now);
        publishStatus(true, false);
        return;
      }
      ST.stw.warning = false;

      int8_t wantDir = wantFwd ? 1 : (wantRev ? -1 : 0);
      if (wantDir == 0 || ST.setpointSpeed == 0) {
        haltMotion(now);
        publishStatus(true, true);
        return;
      }

      /* Una inversion ya iniciada mantiene PWM a cero durante toda la
       * ventana muerta. Si la orden cambia, nunca se aplica la direccion vieja
       * que habia quedado pendiente. */
      if (_pendingDir != 0) {
        if (wantDir != _pendingDir) {
          _pendingDir = 0;
          _activeDir = 0;
          cutPwm();
          setPhysicalDirection(wantDir);
          _activeDir = wantDir;
          _lastRamp = now;
          publishStatus(true, false);
          return;
        }

        if (cfsm_elapsed(_deadStart) < _deadTimeMs) {
          cutPwm();
          publishStatus(true, false);
          return;
        }

        /* Todavia a PWM cero, se conmuta DIR. Se vuelve sin acelerar para que
         * la primera energia solo pueda llegar en una PAA posterior. */
        cutPwm();
        setPhysicalDirection(_pendingDir);
        _activeDir = _pendingDir;
        _pendingDir = 0;
        _lastRamp = now;
        publishStatus(true, false);
        return;
      }

      /* Arranque desde reposo: primero direccion, PWM cero; la rampa empieza
       * en la siguiente llamada a writeOutputs(). */
      if (_activeDir == 0) {
        cutPwm();
        setPhysicalDirection(wantDir);
        _activeDir = wantDir;
        _lastRamp = now;
        publishStatus(true, false);
        return;
      }

      if (wantDir != _activeDir) {
        /* Primer acto de toda inversion: retirar energia. DIR conserva aqui
         * su nivel anterior y solo cambiara cuando venza el tiempo muerto. */
        cutPwm();
        _pendingDir = wantDir;
        _deadStart = now;
        _lastRamp = now;
        publishStatus(true, false);
        return;
      }

      applyRamp(ST.setpointSpeed, now);
      analogWrite(_pwmPin, _applied);
      ST.actualSpeed = _applied;
      publishStatus(true, _applied == ST.setpointSpeed);
    }

    /* --------------------------- Mando ---------------------------------- */
    void enable() { ST.cfgw.enable = true; }

    void disable() {
      ST.cfgw.enable = false;
      stop();                         /* una rehabilitacion no reanuda sola */
    }

    void forward(uint8_t speed = 180) {
      ST.setpointSpeed = speed;
      ST.cfgw.runFwd = true;
      ST.cfgw.runRev = false;
      ST.cfgw.jogFwd = ST.cfgw.jogRev = false;
    }

    void backward(uint8_t speed = 180) {
      ST.setpointSpeed = speed;
      ST.cfgw.runRev = true;
      ST.cfgw.runFwd = false;
      ST.cfgw.jogFwd = ST.cfgw.jogRev = false;
    }

    /* Alias compatibles con el vocabulario de MotorDrive. */
    void runForward(uint8_t speed) { forward(speed); }
    void runReverse(uint8_t speed) { backward(speed); }

    void stop() {
      ST.cfgw.runFwd = ST.cfgw.runRev = false;
      ST.cfgw.jogFwd = ST.cfgw.jogRev = false;
      ST.setpointSpeed = 0;
    }

    void setSignedSpeed(int16_t value) {
      if (value > 0) {
        forward((uint8_t)(value > 255 ? 255 : value));
      } else if (value < 0) {
        int32_t magnitude = -(int32_t)value;  /* tambien seguro para -32768 */
        backward((uint8_t)(magnitude > 255 ? 255 : magnitude));
      } else {
        stop();
      }
    }

    void quickStop(bool active = true) { ST.cfgw.quickStop = !active; }

    void fault(uint16_t code = CFSM_ERR_DRIVE_FAULT) {
      if (!ST.stw.fault) {
        ST.stw.fault = true;
        ST.stw.readyToSwitchOn = false;
        ST.errorCode = code;
      }
    }

    void resetFault() { ST.cfgw.resetFault = true; }

    /* Escalon de PWM por milisegundo. 0 aplica la consigna sin rampa. */
    void setRamp(uint8_t stepPerMs) { _rampStep = stepPerMs; }

    /* Pausa minima con PWM cero antes de cambiar DIR. */
    void setDeadTime(cfsm_time_t milliseconds) { _deadTimeMs = milliseconds; }

    /* --------------------------- Estado --------------------------------- */
    bool isEnabled() const { return ST.stw.enabled; }
    bool isRunning() const { return ST.stw.running; }
    bool isFaulted() const { return ST.stw.fault; }
    uint8_t speed() const { return ST.actualSpeed; }
    int8_t direction() const {
      return ST.stw.running ? _activeDir : 0;
    }
    uint8_t dirPin() const { return _dirPin; }
    uint8_t pwmPin() const { return _pwmPin; }
    bool isDirectionInverted() const { return _directionInverted; }

    void enterSafeState() override {
      ST.cfgw.raw = 0;               /* quickStop queda activo a nivel bajo */
      ST.setpointSpeed = 0;
      _activeDir = _pendingDir = 0;
      _applied = 0;

      /* Tambien aqui se corta antes de tocar DIR. */
      analogWrite(_pwmPin, 0);
      digitalWrite(_dirPin, LOW);

      ST.actualSpeed = 0;
      ST.stw.enabled = false;
      ST.stw.running = false;
      ST.stw.fwdActive = false;
      ST.stw.revActive = false;
      ST.stw.atSetpoint = false;
      ST.stw.warning = false;
      _lastRamp = _deadStart = cfsm_millis();
    }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else out.print(CFSM_FSTR("MOTOR_DIR_PWM"));
      out.print(CFSM_FSTR("] v="));
      out.print(ST.actualSpeed);
      out.print(_activeDir > 0 ? CFSM_FSTR(" FWD")
                              : (_activeDir < 0 ? CFSM_FSTR(" REV")
                                                : CFSM_FSTR(" ---")));
      out.print(CFSM_FSTR(" STW=0x"));
      out.print(ST.stw.raw, HEX);
      if (ST.stw.fault) out.print(CFSM_FSTR(" *AVERIA*"));
    }

  private:
    uint8_t _dirPin;
    uint8_t _pwmPin;
    bool    _directionInverted;
    uint8_t _rampStep = 0;
    uint8_t _applied = 0;
    int8_t  _activeDir = 0;
    int8_t  _pendingDir = 0;
    cfsm_time_t _deadTimeMs = 30;
    cfsm_time_t _deadStart = 0;
    cfsm_time_t _lastRamp = 0;

    void cutPwm() {
      _applied = 0;
      ST.actualSpeed = 0;
      analogWrite(_pwmPin, 0);
    }

    void setPhysicalDirection(int8_t direction) {
      bool forwardLevel = !_directionInverted;
      bool level = direction > 0 ? forwardLevel : !forwardLevel;
      digitalWrite(_dirPin, level ? HIGH : LOW);
    }

    void haltMotion(cfsm_time_t now) {
      cutPwm();
      _activeDir = _pendingDir = 0;
      _lastRamp = now;
    }

    void applyRamp(uint8_t target, cfsm_time_t now) {
      if (_rampStep == 0) {
        _applied = target;
        _lastRamp = now;
        return;
      }

      cfsm_time_t elapsed = cfsm_elapsed(_lastRamp);
      _lastRamp = now;               /* no acumular credito estando estable */
      if (elapsed == 0 || _applied == target) return;

      uint16_t allowance = 255;
      cfsm_time_t saturatesAfter = (cfsm_time_t)(255 / _rampStep) + 1;
      if (elapsed < saturatesAfter)
        allowance = (uint16_t)elapsed * _rampStep;

      if (_applied < target) {
        uint16_t next = (uint16_t)_applied + allowance;
        _applied = (uint8_t)(next > target ? target : next);
      } else {
        uint16_t drop = (uint16_t)_applied - target;
        _applied = (drop <= allowance) ? target
                                       : (uint8_t)(_applied - allowance);
      }
    }

    void publishStatus(bool enabled, bool atSetpoint) {
      bool moving = enabled && _pendingDir == 0 &&
                    _activeDir != 0 && _applied > 0;
      ST.stw.enabled = enabled;
      ST.stw.running = moving;
      ST.stw.fwdActive = moving && _activeDir > 0;
      ST.stw.revActive = moving && _activeDir < 0;
      ST.stw.atSetpoint = enabled && atSetpoint &&
                          _pendingDir == 0 && _applied == ST.setpointSpeed;
    }
};

#endif /* COREFSM_DIR_PWM_MOTOR_DRIVE_H */
