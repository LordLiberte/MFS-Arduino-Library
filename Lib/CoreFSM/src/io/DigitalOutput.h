#ifndef COREFSM_DIGITAL_OUTPUT_H
#define COREFSM_DIGITAL_OUTPUT_H

#include "IDevice.h"
#include "../logic/Timers.h"

/* ===========================================================================
 *  DigitalOutput.h  -  Salida digital con modos, invercion y proteccion
 * ---------------------------------------------------------------------------
 *  Una salida no es solo un digitalWrite. En una maquina real necesita:
 *
 *    - Logica invertida, porque muchos modulos de rele comerciales son
 *      activos a nivel bajo: les mandas LOW para que cierren. Si no se
 *      contempla, todo funciona exactamente al reves y cuesta media tarde
 *      darse cuenta.
 *
 *    - Modos de intermitencia, porque un piloto tiene mas de dos estados
 *      utiles: fijo significa una cosa, parpadeo lento otra y destello rapido
 *      otra distinta. El operario los lee desde lejos sin acercarse al HMI.
 *
 *    - Temporizador de seguridad (watchdog), para las salidas que no deben
 *      quedarse activas indefinidamente. Si un programa se cuelga con una
 *      electrovalvula excitada, la salida se corta sola pasado el tiempo
 *      maximo configurado.
 *
 *    - Forzado, por lo mismo que en las entradas: probar el cableado del
 *      armario sin necesidad de tener la secuencia terminada.
 * ======================================================================== */

enum OutputMode : uint8_t {
  OUT_OFF        = 0,   /* apagada                          */
  OUT_ON         = 1,   /* fija                             */
  OUT_BLINK_SLOW = 2,   /* 500 ms / 500 ms - aviso          */
  OUT_BLINK_FAST = 3,   /* 150 ms / 150 ms - alarma         */
  OUT_FLASH      = 4    /* 80 ms on / 1200 ms off - destello*/
};

class DigitalOutput : public IDevice {
  public:
    /* pin      : pin fisico
     * activeLow: true si la carga se activa con nivel bajo (reles habituales) */
    DigitalOutput(uint8_t pin, bool activeLow = false)
      : _pin(pin), _activeLow(activeLow), _mode(OUT_OFF),
        _physical(false), _simValue(false), _maxOnTimeMs(0), _onSince(0) {}

    void begin() override {
      pinMode(_pin, OUTPUT);
      _mode = OUT_OFF;
      applyPhysical(false);
    }

    /* Fase PAA. Calcula el nivel que toca segun el modo y lo escribe. */
    void writeOutputs() override {
      bool desired;

      if (_forced) {
        desired = _simValue;
      } else {
        switch (_mode) {
          case OUT_ON:         desired = true;                          break;
          case OUT_BLINK_SLOW: _blink.setPeriod(500, 500);
                               desired = _blink.update(true);           break;
          case OUT_BLINK_FAST: _blink.setPeriod(150, 150);
                               desired = _blink.update(true);           break;
          case OUT_FLASH:      _blink.setPeriod(80, 1200);
                               desired = _blink.update(true);           break;
          default:             _blink.update(false);
                               desired = false;                         break;
        }
      }

      /* -----------------------------------------------------------------------
       *  Watchdog de salida
       *  Una vez que corta, ENCLAVA. Si solo cortara un ciclo, al siguiente
       *  scan la salida ya estaria apagada, se reiniciaria el cronometro y
       *  volveria a energizarse: en la practica la electrovalvula seguiria
       *  excitada casi todo el tiempo, dando saltitos de un scan. La
       *  proteccion no protegeria nada.
       *
       *  El enclavamiento solo se levanta con una orden nueva de verdad: un
       *  flanco de apagado seguido de un encendido. Mantener turnOn() en cada
       *  scan -que es lo normal- no lo rearma.
       * -------------------------------------------------------------------- */
      if (_maxOnTimeMs > 0) {
        if (_timedOut) {
          desired = false;
        } else if (desired) {
          if (!_physical) _onSince = cfsm_millis();
          if (cfsm_elapsed(_onSince) >= _maxOnTimeMs) {
            desired   = false;
            _timedOut = true;
          }
        }
      }

      applyPhysical(desired);
    }

    /* -----------------------------------------------------------------------
     *  MANDO
     * -------------------------------------------------------------------- */
    /* El enclavamiento del watchdog solo se rearma en el FLANCO de encendido:
     * hay que apagar y volver a encender. Llamar a turnOn() en cada vuelta del
     * scan -que es la forma habitual de escribir una salida- no lo levanta. */
    void turnOn()  { if (_mode == OUT_OFF) _timedOut = false; _mode = OUT_ON;  }
    void turnOff() { _mode = OUT_OFF; _timedOut = false; }
    void set(bool v) { v ? turnOn() : turnOff(); }
    void setMode(OutputMode m) {
      if (_mode == OUT_OFF && m != OUT_OFF) _timedOut = false;
      if (m == OUT_OFF) _timedOut = false;
      _mode = m;
    }

    /* Rearme explicito del watchdog, para un boton de "reponer salidas". */
    void clearTimeout() { _timedOut = false; }
    void toggle()          { _mode = (_mode == OUT_OFF) ? OUT_ON : OUT_OFF; }

    /* -----------------------------------------------------------------------
     *  CONSULTA
     * -------------------------------------------------------------------- */
    bool       isOn()      const { return _mode != OUT_OFF; }
    bool       isActive()  const { return _physical; }   /* nivel real ahora mismo */
    OutputMode mode()      const { return _mode; }
    bool       hasTimedOut() const { return _timedOut; }
    uint8_t    pin()       const { return _pin; }

    /* -----------------------------------------------------------------------
     *  CONFIGURACION
     * -------------------------------------------------------------------- */
    /* 0 = sin limite. Distinto de 0 = tiempo maximo de activacion continua.
     * Pontelo a las electrovalvulas, resistencias y todo lo que pueda causar
     * dano si se queda pegado. */
    void setMaxOnTime(cfsm_time_t ms) { _maxOnTimeMs = ms; }

    void force(bool value) { _forced = true; _simValue = value; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else { out.print(CFSM_FSTR("DO")); out.print(_pin); }
      out.print(CFSM_FSTR("]="));
      out.print(_physical ? '1' : '0');
      if (_mode >= OUT_BLINK_SLOW) out.print(CFSM_FSTR(" (intermitente)"));
      if (_forced)   out.print(CFSM_FSTR(" *FORZADO*"));
      if (_timedOut) out.print(CFSM_FSTR(" *CORTADA POR WATCHDOG*"));
    }

  private:
    uint8_t     _pin;
    bool        _activeLow;
    OutputMode  _mode;
    bool        _physical;
    bool        _simValue;
    bool        _timedOut = false;
    cfsm_time_t _maxOnTimeMs;
    cfsm_time_t _onSince;
    Blink       _blink;

    void applyPhysical(bool logical) {
      _physical = logical;
      digitalWrite(_pin, (logical != _activeLow) ? HIGH : LOW);
    }
};

/* ---------------------------------------------------------------------------
 *  AnalogOutput - Salida PWM
 * ---------------------------------------------------------------------------
 *  Con rampa opcional de subida y bajada. Una rampa no es un adorno: arrancar
 *  un motor de golpe a plena potencia provoca un pico de corriente que hunde
 *  la tension de alimentacion, y en un Arduino alimentado desde la misma
 *  bateria que los motores ese hundimiento reinicia el microcontrolador. Es
 *  la causa numero uno de "el robot se reinicia solo al arrancar".
 * ------------------------------------------------------------------------ */
class AnalogOutput : public IDevice {
  public:
    AnalogOutput(uint8_t pin, bool activeLow = false)
      : _pin(pin), _activeLow(activeLow), _target(0), _current(0),
        _rampStep(0), _lastRamp(0) {}

    void begin() override {
      pinMode(_pin, OUTPUT);
      _target = _current = 0;
      apply();
    }

    void writeOutputs() override {
      if (_rampStep > 0 && _current != _target) {
        /* La rampa avanza como maximo un escalon por milisegundo. */
        if (cfsm_elapsed(_lastRamp) >= 1) {
          _lastRamp = cfsm_millis();
          int16_t diff = (int16_t)_target - (int16_t)_current;
          int16_t step = (int16_t)_rampStep;
          if (diff >  step) _current += _rampStep;
          else if (diff < -step) _current -= _rampStep;
          else _current = _target;
        }
      } else {
        _current = _target;
      }
      apply();
    }

    void setValue(uint8_t v) { _target = _forced ? _target : v; }
    uint8_t value()   const  { return _current; }
    uint8_t setpoint()const  { return _target;  }

    /* Escalon de rampa en unidades PWM por milisegundo. 0 = sin rampa.
     * Un valor de 2 lleva de 0 a 255 en unos 128 ms, que suele ser suficiente
     * para que la fuente no se hunda. */
    void setRamp(uint8_t stepPerMs) { _rampStep = stepPerMs; }

    void force(uint8_t v) { _forced = true; _target = _current = v; }

  private:
    uint8_t     _pin;
    bool        _activeLow;
    uint8_t     _target;
    uint8_t     _current;
    uint8_t     _rampStep;
    cfsm_time_t _lastRamp;

    void apply() { analogWrite(_pin, _activeLow ? (255 - _current) : _current); }
};

#endif /* COREFSM_DIGITAL_OUTPUT_H */
