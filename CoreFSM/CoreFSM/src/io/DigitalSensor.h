#ifndef COREFSM_DIGITAL_SENSOR_H
#define COREFSM_DIGITAL_SENSOR_H

#include "IDevice.h"

/* ===========================================================================
 *  DigitalSensor.h  -  Entrada digital con antirrebote y flancos
 * ---------------------------------------------------------------------------
 *  POR QUE NO BASTA CON digitalRead()
 *  ----------------------------------
 *  Un contacto mecanico (pulsador, final de carrera, rele) no cambia de estado
 *  limpiamente. Al cerrarse, las laminillas metalicas rebotan durante 1 a 20
 *  milisegundos, produciendo una rafaga de aperturas y cierres. Como el scan
 *  dura microsegundos, el programa ve esa rafaga entera: para el, una sola
 *  pulsacion son entre cinco y cincuenta pulsaciones.
 *
 *  Sintoma tipico: el contador de piezas sube de tres en tres, o la maquina
 *  arranca y para sola al pulsar marcha. La causa casi nunca es el software:
 *  es el cobre.
 *
 *  El filtro que implementa esta clase es el estandar: una lectura solo se
 *  acepta como valida cuando se ha mantenido estable durante todo el tiempo
 *  de antirrebote. Los rebotes, al ser mas cortos, se descartan solos.
 *
 *  SOBRE INPUT_PULLUP Y LA LOGICA INVERTIDA
 *  ----------------------------------------
 *  El cableado industrial estandar conmuta a masa: un extremo del contacto va
 *  al pin y el otro a GND. La resistencia de pull-up interna mantiene el pin
 *  a nivel alto en reposo, y al cerrar el contacto el pin cae a 0 V.
 *
 *  Es decir: contacto CERRADO = pin en LOW. Electricamente al reves de lo
 *  intuitivo. El parametro activeLow (true por defecto) se encarga de darle
 *  la vuelta, de modo que isTriggered() devuelve true cuando el sensor esta
 *  activado, sin que tengas que pensar en tensiones.
 *
 *  Se hace asi por seguridad: si el cable se corta o se suelta un borne, el
 *  pull-up lo lleva a nivel alto, la senal se lee como "sensor no activado" y
 *  la maquina se para. Con logica directa, un cable cortado se leeria como
 *  "sensor activado" y la maquina seguiria como si nada. En seguridad de
 *  maquinas esto se llama principio de corriente de reposo y no es opcional.
 * ======================================================================== */

class DigitalSensor : public IDevice {
  public:
    /* pin        : pin fisico del microcontrolador
     * activeLow  : true si el contacto conmuta a masa (lo normal)
     * debounceMs : ventana de estabilidad exigida. 20 ms va bien para
     *              pulsadores; para sensores electronicos (inductivos,
     *              fotocelulas) puedes bajar a 2-5 ms porque no rebotan. */
    DigitalSensor(uint8_t pin, bool activeLow = true, uint16_t debounceMs = 20)
      : _pin(pin), _activeLow(activeLow), _debounceMs(debounceMs),
        _state(false), _raw(false), _lastRaw(false),
        _rising(false), _falling(false),
        _lastChangeTime(0), _simValue(false), _invertLogic(false) {}

    void begin() override {
      pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT);

      /* La muestra inicial tiene que pasar por las MISMAS transformaciones que
       * aplica readInputs(): forzado e invercion logica. Si se tomara el pin
       * en crudo y el sensor estuviera invertido, la primera lectura del scan
       * daria un valor distinto y, pasado el antirrebote, se generaria un
       * flanco que nadie ha producido. Si ese flanco alimenta un contador o
       * una orden de marcha, la maquina cuenta o arranca sola en el setup(). */
      bool inicial = _forced ? _simValue : readPhysical();
      if (_invertLogic) inicial = !inicial;

      _raw = _lastRaw = _state = inicial;
      _lastChangeTime = cfsm_millis();
      _rising = _falling = false;
      _changeCount = 0;
    }

    /* Fase PAE. Lee, filtra y calcula los flancos de este ciclo. */
    void readInputs() override {
      bool sample = _forced ? _simValue : readPhysical();
      if (_invertLogic) sample = !sample;

      _rising = _falling = false;

      /* Cada cambio en la senal cruda reinicia el reloj de estabilidad. */
      if (sample != _lastRaw) {
        _lastRaw        = sample;
        _lastChangeTime = cfsm_millis();
      }

      /* Solo cuando la senal lleva quieta el tiempo exigido se da por buena. */
      if (cfsm_elapsed(_lastChangeTime) >= _debounceMs) {
        if (sample != _state) {
          _state   = sample;
          _rising  =  _state;
          _falling = !_state;
          _changeCount++;
        }
      }
      _raw = sample;
    }

    /* -----------------------------------------------------------------------
     *  CONSULTA
     * -------------------------------------------------------------------- */
    bool isTriggered() const { return _state; }      /* nivel filtrado        */
    bool isClear()     const { return !_state; }
    bool hasRisen()    const { return _rising; }     /* flanco de activacion  */
    bool hasFallen()   const { return _falling; }    /* flanco de liberacion  */
    bool rawValue()    const { return _raw; }        /* sin filtrar (diagnostico) */

    /* Cuanto lleva el sensor en su estado actual. Sirve para exigir que una
     * condicion se mantenga: "arranca solo si la barrera lleva 2 s despejada". */
    cfsm_time_t timeInState() const { return cfsm_elapsed(_lastChangeTime); }
    bool isStableFor(cfsm_time_t ms) const { return timeInState() >= ms; }

    /* Numero de conmutaciones desde el arranque. Un sensor que conmuta miles
     * de veces sin que la maquina se mueva esta suelto, sucio o averiado; es
     * un indicador de mantenimiento predictivo muy barato de obtener. */
    uint32_t changeCount() const { return _changeCount; }

    /* -----------------------------------------------------------------------
     *  CONFIGURACION Y FORZADO
     * -------------------------------------------------------------------- */
    void setDebounce(uint16_t ms) { _debounceMs = ms; }

    /* Invierte el significado logico sin tocar el cableado. Util cuando montan
     * un contacto NC donde el plano decia NA y no se puede parar la maquina
     * para cambiarlo. */
    void setInverted(bool inv) { _invertLogic = inv; }

    /* Forzado: desconecta el sensor del pin y le impone un valor. */
    void force(bool value) { _forced = true; _simValue = value; }

    uint8_t pin() const { return _pin; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else { out.print(CFSM_FSTR("DI")); out.print(_pin); }
      out.print(CFSM_FSTR("]="));
      out.print(_state ? '1' : '0');
      if (_forced) out.print(CFSM_FSTR(" *FORZADO*"));
    }

  private:
    uint8_t     _pin;
    bool        _activeLow;
    uint16_t    _debounceMs;

    bool        _state;      /* valor filtrado y estable   */
    bool        _raw;        /* ultima muestra             */
    bool        _lastRaw;    /* muestra anterior           */
    bool        _rising;
    bool        _falling;
    cfsm_time_t _lastChangeTime;
    uint32_t    _changeCount = 0;

    bool        _simValue;
    bool        _invertLogic;

    bool readPhysical() const {
      return (digitalRead(_pin) == (_activeLow ? LOW : HIGH));
    }
};

#endif /* COREFSM_DIGITAL_SENSOR_H */
