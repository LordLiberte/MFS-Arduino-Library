#ifndef COREFSM_ANALOG_SENSOR_H
#define COREFSM_ANALOG_SENSOR_H

#include "IDevice.h"

/* ===========================================================================
 *  AnalogSensor.h  -  Entrada analogica con filtro, escalado y alarmas
 * ---------------------------------------------------------------------------
 *  QUE ANADE SOBRE analogRead()
 *  ----------------------------
 *  1. FILTRO. Una lectura analogica cruda salta constantemente por el ruido
 *     electrico. Si comparas directamente contra un umbral, la senal cruzara
 *     el umbral arriba y abajo decenas de veces por segundo (efecto conocido
 *     como chattering) y el actuador asociado se volvera loco.
 *     Se usa un filtro exponencial de primer orden (EMA):
 *
 *         valor = valor + (nuevo - valor) / 2^alpha
 *
 *     Se implementa solo con desplazamientos de bits, sin division ni coma
 *     flotante, que en un AVR de 8 bits sin unidad de coma flotante es la
 *     diferencia entre microsegundos y decenas de microsegundos.
 *
 *  2. ESCALADO A UNIDADES DE INGENIERIA. Nadie quiere razonar en cuentas del
 *     conversor. Quieres bar, grados o milimetros. El escalado lineal traduce
 *     el rango del ADC al rango fisico del sensor, exactamente como el bloque
 *     SCALE de un automata.
 *
 *  3. HISTERESIS EN LOS UMBRALES. Un umbral simple oscila cuando la senal se
 *     queda justo encima. Con histeresis, el umbral de activacion y el de
 *     desactivacion son distintos, y la senal tiene que alejarse de verdad
 *     para conmutar. Es lo mismo que hace el termostato de una caldera para
 *     no arrancar y parar cada dos segundos.
 *
 *  4. DETECCION DE ROTURA DE HILO. En un lazo industrial de 4-20 mA, el cero
 *     util es 4 mA, no 0. Si llegan 0 mA, no es que la medida sea minima: es
 *     que el cable esta cortado. Poder distinguir "vale cero" de "no hay
 *     sensor" evita accidentes.
 * ======================================================================== */

class AnalogSensor : public IDevice {
  public:
    /* pin        : pin analogico (A0, A1...)
     * filterAlpha: intensidad del filtro, 0 = sin filtro, 4-5 = habitual,
     *              8 = muy suave pero lento en responder. */
    AnalogSensor(uint8_t pin, uint8_t filterAlpha = 3)
      : _pin(pin), _alpha(filterAlpha), _raw(0), _filtered(0),
        _scaleMinRaw(0), _scaleMaxRaw(1023),
        _scaleMinEng(0), _scaleMaxEng(1023),
        _simValue(0) {}

    void begin() override {
      _raw = analogRead(_pin);
      _filtered = ((uint32_t)_raw) << 8;   /* punto fijo interno: 8 bits de fraccion */
    }

    void readInputs() override {
      _raw = _forced ? _simValue : (uint16_t)analogRead(_pin);

      if (_alpha == 0) {
        _filtered = ((uint32_t)_raw) << 8;
      } else {
        /* EMA en punto fijo: filtered += (raw - filtered) >> alpha */
        int32_t target = ((int32_t)_raw) << 8;
        _filtered += (target - (int32_t)_filtered) >> _alpha;
      }

      /* Registro de extremos, util para dimensionar y para diagnostico. */
      uint16_t v = value();
      if (v < _minSeen) _minSeen = v;
      if (v > _maxSeen) _maxSeen = v;
    }

    /* -----------------------------------------------------------------------
     *  LECTURA
     * -------------------------------------------------------------------- */
    uint16_t raw()   const { return _raw; }                       /* sin filtrar */
    uint16_t value() const { return (uint16_t)(_filtered >> 8); } /* filtrado    */

    /* Valor en unidades de ingenieria segun el escalado configurado. */
    int32_t scaled() const {
      int32_t v = (int32_t)value();
      int32_t rawSpan = (int32_t)_scaleMaxRaw - (int32_t)_scaleMinRaw;
      if (rawSpan == 0) return _scaleMinEng;
      int32_t engSpan = _scaleMaxEng - _scaleMinEng;
      return _scaleMinEng + ((v - (int32_t)_scaleMinRaw) * engSpan) / rawSpan;
    }

    uint16_t minSeen() const { return _minSeen; }
    uint16_t maxSeen() const { return _maxSeen; }
    void resetMinMax() { _minSeen = 0xFFFF; _maxSeen = 0; }

    /* -----------------------------------------------------------------------
     *  ESCALADO
     *  Ejemplo, sensor de presion 0-10 bar que entrega 0,5-4,5 V sobre un ADC
     *  de 10 bits alimentado a 5 V:
     *      0,5 V -> 102 cuentas ;  4,5 V -> 921 cuentas
     *      setScale(102, 921, 0, 1000);   // 1000 = 10,00 bar en centesimas
     *  Se trabaja en enteros escalados (centesimas, decimas) en vez de con
     *  float: en un AVR sin FPU, cada operacion en coma flotante cuesta cientos
     *  de ciclos de reloj y no aporta nada aqui.
     * -------------------------------------------------------------------- */
    void setScale(uint16_t rawMin, uint16_t rawMax, int32_t engMin, int32_t engMax) {
      _scaleMinRaw = rawMin; _scaleMaxRaw = rawMax;
      _scaleMinEng = engMin; _scaleMaxEng = engMax;
    }

    void setFilter(uint8_t alpha) { _alpha = alpha; }

    /* -----------------------------------------------------------------------
     *  UMBRAL CON HISTERESIS
     *  onLevel  : valor por encima del cual se activa
     *  offLevel : valor por debajo del cual se desactiva
     *  Con onLevel > offLevel se obtiene el comportamiento de termostato.
     * -------------------------------------------------------------------- */
    void setThreshold(uint16_t onLevel, uint16_t offLevel) {
      _thOn = onLevel; _thOff = offLevel;
    }

    bool threshold() {
      uint16_t v = value();
      if (!_thState && v >= _thOn)  _thState = true;
      if ( _thState && v <= _thOff) _thState = false;
      return _thState;
    }

    /* -----------------------------------------------------------------------
     *  DIAGNOSTICO DE LAZO
     *  Marca la senal como no valida si cae por debajo del minimo fisicamente
     *  posible (rotura de hilo) o se pega al fondo de escala (cortocircuito).
     * -------------------------------------------------------------------- */
    void setValidRange(uint16_t lo, uint16_t hi) { _validLo = lo; _validHi = hi; }
    bool isValid() const {
      uint16_t v = value();
      return v >= _validLo && v <= _validHi;
    }

    void force(uint16_t rawValue) { _forced = true; _simValue = rawValue; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else { out.print(CFSM_FSTR("AI")); out.print(_pin); }
      out.print(CFSM_FSTR("]="));
      out.print(value());
      out.print(CFSM_FSTR(" ("));
      out.print(scaled());
      out.print(')');
      if (!isValid()) out.print(CFSM_FSTR(" *FUERA DE RANGO*"));
      if (_forced)    out.print(CFSM_FSTR(" *FORZADO*"));
    }

  private:
    uint8_t  _pin;
    uint8_t  _alpha;
    uint16_t _raw;
    int32_t  _filtered;          /* punto fijo 24.8 */

    uint16_t _scaleMinRaw, _scaleMaxRaw;
    int32_t  _scaleMinEng, _scaleMaxEng;

    uint16_t _thOn = 0xFFFF, _thOff = 0;
    bool     _thState = false;

    uint16_t _validLo = 0, _validHi = 1023;
    uint16_t _minSeen = 0xFFFF, _maxSeen = 0;

    uint16_t _simValue;
};

#endif /* COREFSM_ANALOG_SENSOR_H */
