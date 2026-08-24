#ifndef COREFSM_SCAN_WATCHDOG_H
#define COREFSM_SCAN_WATCHDOG_H

#include "../core/CoreFSM_Platform.h"

#if defined(CFSM_ARCH_AVR)
  #include <avr/wdt.h>
#endif

/* ===========================================================================
 *  ScanWatchdog.h  -  Vigilancia del tiempo de ciclo de scan
 * ---------------------------------------------------------------------------
 *  QUE VIGILA ESTO, Y POR QUE NO ES LO MISMO QUE setCycleTimeout()
 *  ---------------------------------------------------------------
 *  Cuidado con la palabra "ciclo", que en automatizacion significa dos cosas
 *  distintas y se confunden constantemente:
 *
 *    CICLO DE SCAN       Una pasada completa del programa: leer entradas,
 *                        calcular, escribir salidas. En un S7 lo vigila la
 *                        Zykluszeitueberwachung, que viene de fabrica en unos
 *                        150 ms; si el programa se pasa, salta el OB de error
 *                        de tiempo y la CPU se va a STOP. Es ESTO.
 *
 *    CICLO DE PRODUCCION De pieza a pieza. Lo vigila SequenceBlock con
 *                        setCycleTimeout() y setCycleTarget(). NO es esto.
 *
 *  POR QUE HACE FALTA EN UN ARDUINO MAS QUE EN UN PLC
 *  --------------------------------------------------
 *  Un PLC te impone el modelo ciclico. Aqui nada te impide escribir un
 *  delay(1000) en mitad de un paso, dejar un Serial.println() dentro del
 *  switch en vez de en onStepEntered(), o llamar a un sensor de ultrasonidos
 *  que sin eco se come 25 ms el solo esperando el rebote.
 *
 *  Cuando el scan se alarga, la maquina no se para: se vuelve mentirosa. Los
 *  antirrebotes empiezan a perder flancos porque muestrean mas despacio, los
 *  tiempos de los pasos pierden resolucion, y un pulsador rapido deja de
 *  detectarse a veces. Son los fallos mas dificiles de encontrar que hay,
 *  porque no dan error: dan comportamiento raro e intermitente.
 *
 *  Con esto puesto, en cambio, el problema tiene nombre y numero.
 *
 *  USO
 *  ---
 *      ScanWatchdog scan(20);            // 20 ms de scan maximo
 *
 *      void loop() {
 *        scan.begin();
 *        HW.readInputs();
 *        manager.updateAll();
 *        HW.writeOutputs();
 *        scan.end();
 *      }
 *
 *  Y cuando quieras ver como va:
 *
 *      scan.report(Serial);
 *      >> scan ult=1832us med=1790us max=4120us limite=20ms excesos=0 n=54211
 *
 *  Mirar ese max de vez en cuando dice mas del estado real del programa que
 *  cualquier otra cosa. Si el maximo se acerca al limite, el programa esta
 *  pidiendo que lo repartas en mas scans.
 * ======================================================================== */

class ScanWatchdog {
  public:
    /* limitMs = 0 desactiva la vigilancia, pero se siguen tomando las medidas:
     * util para caracterizar un programa antes de decidir el limite. */
    explicit ScanWatchdog(uint16_t limitMs = 20)
      : _limitUs((uint32_t)limitMs * 1000UL) {}

    /* --- Marcas, una al principio y otra al final del loop() ------------- */

    void begin() { _t0 = micros(); }

    void end() {
      uint32_t dt = micros() - _t0;      /* la resta sin signo sobrevive al
                                            desbordamiento de micros()       */
      _lastUs = dt;
      if (dt > _maxUs) _maxUs = dt;
      if (dt < _minUs) _minUs = dt;

      /* Media movil exponencial con desplazamientos: no acumula, no se
       * desborda nunca y cuesta una resta y un shift. Una media aritmetica
       * necesitaria un acumulador que reventaria a las pocas horas. */
      _avgUs += ((int32_t)dt - (int32_t)_avgUs) >> 3;

      _count++;

      _overrun = (_limitUs > 0 && dt > _limitUs);
      if (_overrun) {
        _overruns++;
        _worstUs = (dt > _worstUs) ? dt : _worstUs;
        if (_onOverrun) _onOverrun(dt);
      }

#if defined(CFSM_ARCH_AVR)
      if (_hwArmed) wdt_reset();
#endif
    }

    /* --- Consulta -------------------------------------------------------- */

    uint32_t lastUs()     const { return _lastUs; }
    uint32_t maxUs()      const { return _maxUs;  }
    uint32_t minUs()      const { return _minUs == 0xFFFFFFFFUL ? 0 : _minUs; }
    uint32_t avgUs()      const { return _avgUs;  }
    uint32_t scanCount()  const { return _count;  }
    uint32_t overruns()   const { return _overruns; }
    uint32_t worstOverrunUs() const { return _worstUs; }

    /* true solo durante el scan siguiente al que se paso. Para engancharlo a
     * una alarma:  alarmas.raiseIf(scan.isOverrun(), ALM_SCAN, F("Scan largo")); */
    bool isOverrun()      const { return _overrun; }

    /* El margen que queda, en tanto por ciento del limite. Por debajo del 20 %
     * es hora de repartir el trabajo en mas scans. */
    uint8_t headroomPct() const {
      if (_limitUs == 0 || _maxUs >= _limitUs) return 0;
      return (uint8_t)(((_limitUs - _maxUs) * 100UL) / _limitUs);
    }

    void setLimit(uint16_t limitMs) { _limitUs = (uint32_t)limitMs * 1000UL; }

    /* Se llama con la duracion del scan que se paso. Ojo: corre DENTRO del
     * scan, asi que no imprimas medio kilobyte ahi o empeoraras justo lo que
     * intentas medir. */
    void onOverrun(void (*fn)(uint32_t us)) { _onOverrun = fn; }

    void resetStats() {
      _t0 = 0; _lastUs = 0; _maxUs = 0; _minUs = 0xFFFFFFFFUL;
      _avgUs = 0; _count = 0; _overruns = 0; _worstUs = 0;
      _overrun = false;
    }

    void report(Print& out) const {
      out.print(CFSM_FSTR("scan ult="));   out.print(_lastUs);
      out.print(CFSM_FSTR("us med="));     out.print(_avgUs);
      out.print(CFSM_FSTR("us max="));     out.print(_maxUs);
      out.print(CFSM_FSTR("us limite="));  out.print(_limitUs / 1000UL);
      out.print(CFSM_FSTR("ms excesos=")); out.print(_overruns);
      out.print(CFSM_FSTR(" n="));         out.println(_count);
    }

    /* -----------------------------------------------------------------------
     *  WATCHDOG HARDWARE  -  LEE ESTO ENTERO ANTES DE USARLO
     * -----------------------------------------------------------------------
     *  Lo de arriba es un cronometro: se entera de que el scan fue largo, pero
     *  no puede hacer nada si el programa se queda colgado de verdad, porque
     *  entonces end() no se llega a ejecutar. Para eso esta el watchdog del
     *  propio microcontrolador, que resetea la placa si no se le alimenta a
     *  tiempo. Es el equivalente literal de la CPU del PLC yendose a STOP.
     *
     *  EL PELIGRO, que es real y muerde:
     *  En placas antiguas con el bootloader viejo de Arduino (muchos Nano
     *  clonicos anteriores a 2018 lo llevan), el bootloader NO borra el bit
     *  WDRF ni desactiva el watchdog despues de un reset provocado por el. El
     *  resultado es un bucle de reset infinito del que no se sale ni cargando
     *  otro programa: la placa se reinicia antes de que termine la carga.
     *  Recuperarla exige un programador ISP.
     *
     *  POR ESO ESTO NACE APAGADO Y NO SE ACTIVA SOLO. Antes de llamarlo:
     *    1. Comprueba que tu placa arranca con optiboot (Nano "new bootloader").
     *    2. Pruebalo primero con un tiempo generoso, 2 s o mas.
     *    3. Ten a mano como volver a cargar por ISP, por si acaso.
     *
     *  Si tienes cualquier duda, no lo actives: el cronometro de arriba ya te
     *  da el 90 % del valor y no puede dejarte la placa inservible.
     * -------------------------------------------------------------------- */
    void enableHardwareWatchdog() {
#if defined(CFSM_ARCH_AVR)
      wdt_enable(WDTO_2S);
      _hwArmed = true;
#endif
    }

    void disableHardwareWatchdog() {
#if defined(CFSM_ARCH_AVR)
      wdt_disable();
      _hwArmed = false;
#endif
    }

    bool hardwareWatchdogArmed() const { return _hwArmed; }

  private:
    uint32_t _limitUs;
    uint32_t _t0        = 0;
    uint32_t _lastUs    = 0;
    uint32_t _maxUs     = 0;
    uint32_t _minUs     = 0xFFFFFFFFUL;
    uint32_t _avgUs     = 0;
    uint32_t _count     = 0;
    uint32_t _overruns  = 0;
    uint32_t _worstUs   = 0;
    bool     _overrun   = false;
    bool     _hwArmed   = false;
    void (*_onOverrun)(uint32_t) = nullptr;
};

#endif /* COREFSM_SCAN_WATCHDOG_H */
