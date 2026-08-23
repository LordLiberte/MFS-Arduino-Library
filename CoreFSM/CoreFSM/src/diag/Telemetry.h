#ifndef COREFSM_TELEMETRY_H
#define COREFSM_TELEMETRY_H

#include "../core/BlockManager.h"
#include "../core/SequenceBlock.h"
#include "Logger.h"

/* ===========================================================================
 *  Telemetry.h  -  Ver que hace la maquina por dentro
 * ---------------------------------------------------------------------------
 *  La mayor diferencia entre programar un automata y programar un Arduino no
 *  es el lenguaje: es que en el automata puedes ABRIR UNA TABLA DE OBSERVACION
 *  y ver en tiempo real el valor de cualquier variable, forzarla, y seguir el
 *  paso activo mientras la maquina trabaja. Sin eso, depurar es adivinar.
 *
 *  Este archivo recupera esa capacidad con lo unico que hay disponible: el
 *  puerto serie. Ofrece tres herramientas complementarias.
 *
 *  1. StepTracer - trazador de cambios de paso
 *     Imprime UNA linea cada vez que un bloque cambia de paso, y nada mas.
 *     Es lo mas parecido a mirar el GRAFCET animado, y no satura el buffer
 *     porque solo escribe cuando de verdad ha pasado algo.
 *
 *  2. printWatchTable - foto instantanea
 *     El estado de todos los bloques bajo peticion. Se dispara enviando un
 *     caracter por el monitor serie, no ciclicamente.
 *
 *  3. CsvLogger - registro para analizar despues
 *     Vuelca variables numericas en formato CSV a intervalos regulares, para
 *     pegarlo en una hoja de calculo y ver la curva. Es como se ajusta una
 *     ganancia de un lazo de control: mirando la grafica de la respuesta, no
 *     a ojo.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  StepTracer
 * ------------------------------------------------------------------------ */
class StepTracer {
  public:
    StepTracer(SequenceBlock& blk, Print& out)
      : _blk(blk), _out(out), _lastStep(0xFFFF), _lastState(0xFF) {}

    /* Llamalo una vez por scan, DESPUES de manager.updateAll(). Solo imprime
     * cuando algo cambia, asi que es seguro tenerlo siempre puesto. */
    void update() {
      uint16_t s  = _blk.getStep();
      uint8_t  st = _blk.getState();

      if (st != _lastState) {
        _lastState = st;
        _out.print(CFSM_FSTR("[ESTADO] "));
        printName();
        _out.print(CFSM_FSTR(" -> "));
        _out.println(cfsmStateName(st));
        if (st == STATE_ERROR) {
          _out.print(CFSM_FSTR("   causa: 0x"));
          _out.print(_blk.ST.errorCode, HEX);
          _out.print(' ');
          _out.println(cfsmErrorText(_blk.ST.errorCode));
        }
      }

      if (s != _lastStep) {
        _out.print(CFSM_FSTR("[PASO]   "));
        printName();
        _out.print(CFSM_FSTR(" "));
        _out.print(_lastStep == 0xFFFF ? 0 : _lastStep);
        _out.print(CFSM_FSTR(" -> "));
        _out.print(s);
        const __FlashStringHelper* n = _blk.stepName(s);
        if (n) { _out.print(CFSM_FSTR(" (")); _out.print(n); _out.print(')'); }
        _out.println();
        _lastStep = s;
      }
    }

  private:
    SequenceBlock& _blk;
    Print&         _out;
    uint16_t       _lastStep;
    uint8_t        _lastState;

    void printName() {
      const __FlashStringHelper* n = _blk.getName();
      if (n) _out.print(n); else { _out.print('#'); _out.print(_blk.getId()); }
    }
};

/* ---------------------------------------------------------------------------
 *  CsvLogger
 * ---------------------------------------------------------------------------
 *  Uso tipico para ajustar un lazo de posicion:
 *
 *      CsvLogger<4> log(Serial, 50);          // una fila cada 50 ms
 *      log.header(F("t,consigna,real,pwm"));
 *      ...
 *      log.set(0, eje.target());
 *      log.set(1, eje.position());
 *      log.set(2, motor.speed());
 *      log.tick();
 * ------------------------------------------------------------------------ */
template <uint8_t COLS = 6>
class CsvLogger {
  public:
    CsvLogger(Print& out, cfsm_time_t periodMs = 100)
      : _out(out), _period(periodMs), _last(0), _enabled(false) {
      for (uint8_t i = 0; i < COLS; i++) _v[i] = 0;
    }

    void header(const __FlashStringHelper* h) { _out.println(h); }
    void enable(bool e)  { _enabled = e; _last = cfsm_millis(); }
    bool isEnabled() const { return _enabled; }

    void set(uint8_t col, int32_t value) { if (col < COLS) _v[col] = value; }

    /* Llamalo cada scan. Solo escribe cuando toca por periodo. */
    void tick() {
      if (!_enabled) return;
      if (cfsm_elapsed(_last) < _period) return;
      _last = cfsm_millis();
      _out.print(cfsm_millis());
      for (uint8_t i = 0; i < COLS; i++) { _out.print(','); _out.print(_v[i]); }
      _out.println();
    }

  private:
    Print&      _out;
    cfsm_time_t _period;
    cfsm_time_t _last;
    bool        _enabled;
    int32_t     _v[COLS];
};

/* ---------------------------------------------------------------------------
 *  Consola de mantenimiento
 * ---------------------------------------------------------------------------
 *  Un interprete de un solo caracter sobre el puerto serie. Es la version
 *  minima de un panel de operador y, sorprendentemente, cubre casi todo lo que
 *  hace falta en una puesta en marcha sin gastar ni un pin ni una pantalla.
 *
 *      w  tabla de observacion de bloques
 *      s  arrancar          x  parar
 *      p  pausar / reanudar r  rearmar
 *      c  estadisticas de tiempo de ciclo
 *      ?  ayuda
 *
 *  La consola solo conoce el orquestador, asi que no puede saber cuales son
 *  tus sensores ni cual de tus bloques quieres avanzar paso a paso. Para
 *  ampliarla con lo tuyo -imagen de proceso, lista de alarmas, siguiente paso,
 *  liberar forzados- registra un manejador propio:
 *
 *      void misComandos(char c) {
 *        switch (c) {
 *          case 'i': HW.printIoTable(Serial);        break;
 *          case 'a': alarmas.printAll(Serial);       break;
 *          case 'n': miBloque.ST.cfgw.nextStep = true; break;
 *          case 'f': HW.releaseAllForces();          break;
 *        }
 *      }
 *      consola.setExtraHandler(misComandos);
 * ------------------------------------------------------------------------ */
template <uint8_t MAX_BLOCKS>
class MaintenanceConsole {
  public:
    MaintenanceConsole(BlockManager<MAX_BLOCKS>& mgr, Stream& port)
      : _mgr(mgr), _port(port) {}

    /* Llamalo cada scan. Lee como mucho un caracter por vuelta, de modo que
     * nunca puede retener el ciclo. */
    void update() {
      if (!_port.available()) return;
      char c = (char)_port.read();
      switch (c) {
        case 'w': _mgr.printWatchTable(_port);            break;
        case 's': _mgr.startAll();  say(F("MARCHA"));     break;
        case 'x': _mgr.stopAll();   say(F("PARO"));       break;
        case 'p': togglePause();                          break;
        case 'r': _mgr.resetAll();  say(F("REARME"));     break;
        case 'c': printScanStats();                       break;
        case '?': printHelp();                            break;
        default:  if (_extra) _extra(c);                  break;
      }
    }

    /* Comandos adicionales propios de tu maquina. */
    void setExtraHandler(void (*fn)(char)) { _extra = fn; }

  private:
    BlockManager<MAX_BLOCKS>& _mgr;
    Stream& _port;
    bool    _paused = false;
    void (*_extra)(char) = nullptr;

    void say(const __FlashStringHelper* m) {
      _port.print(CFSM_FSTR(">> ")); _port.println(m);
    }

    void togglePause() {
      _paused = !_paused;
      if (_paused) { _mgr.holdAll();   say(F("PAUSA")); }
      else         { _mgr.resumeAll(); say(F("REANUDAR")); }
    }

    void printScanStats() {
      _port.print(CFSM_FSTR("scan ult=")); _port.print(_mgr.lastScanTimeUs());
      _port.print(CFSM_FSTR("us min="));   _port.print(_mgr.minScanTimeUs());
      _port.print(CFSM_FSTR("us max="));   _port.print(_mgr.maxScanTimeUs());
      _port.print(CFSM_FSTR("us ciclos=")); _port.println(_mgr.scanCount());
    }

    void printHelp() {
      _port.println(CFSM_FSTR("w=watch  s=marcha  x=paro  p=pausa  r=rearme  c=scan  ?=ayuda"));
      _port.println(CFSM_FSTR("(otras teclas van a tu manejador, si lo has registrado)"));
    }
};

#endif /* COREFSM_TELEMETRY_H */
