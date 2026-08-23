#ifndef COREFSM_COUNTERS_H
#define COREFSM_COUNTERS_H

#include "../core/CoreFSM_Platform.h"
#include "Edges.h"

/* ===========================================================================
 *  Counters.h  -  Contadores IEC 61131-3 (CTU, CTD, CTUD)
 * ---------------------------------------------------------------------------
 *  Cuentan FLANCOS, no niveles. Internamente cada uno lleva su propio R_TRIG,
 *  de modo que puedes pasarle directamente la senal de un sensor mantenido sin
 *  que se dispare miles de veces por segundo.
 *
 *  Usos habituales en una maquina:
 *    - Contar piezas producidas por turno.
 *    - Llenar una caja: cuando CV llega a PV (12 piezas), cerrar y evacuar.
 *    - Contar reintentos de una operacion antes de declarar averia.
 *    - Contar ciclos de un actuador para avisar de mantenimiento preventivo.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  CTU - Contador ascendente
 *    Q sube cuando CV >= PV. Sigue contando por encima de PV hasta saturar.
 * ------------------------------------------------------------------------ */
struct Ctu {
  uint16_t PV = 0;       /* valor preseleccionado */
  uint16_t CV = 0;       /* valor actual          */
  bool     Q  = false;

  bool update(bool CU, bool RESET = false) {
    /* Al poner a cero se sigue MUESTREANDO la entrada (_trig.update en vez de
     * _trig.reset). Olvidar el nivel anterior haria que, si la fotocelula
     * estuviera tapada por una pieza en ese momento, el scan siguiente viera
     * un flanco de subida que no ha ocurrido y el contador arrancara en 1. */
    if (RESET) { CV = 0; Q = false; _trig.update(CU); return false; }
    if (_trig.update(CU) && CV < 0xFFFF) CV++;
    Q = (CV >= PV);
    return Q;
  }

  void setPreset(uint16_t v) { PV = v; }
  void reset() { CV = 0; Q = false; _trig.reset(); }

  private:
    RTrig _trig;
};

/* ---------------------------------------------------------------------------
 *  CTD - Contador descendente
 *    Se carga con LOAD (CV = PV) y cuenta hacia abajo. Q sube al llegar a 0.
 *    Es el contador natural para "quedan N piezas por hacer".
 * ------------------------------------------------------------------------ */
struct Ctd {
  uint16_t PV = 0;
  uint16_t CV = 0;
  bool     Q  = false;

  bool update(bool CD, bool LOAD = false) {
    /* Igual que en CTU: se sigue muestreando la entrada al cargar, para no
     * inventarse un flanco en el scan siguiente. */
    if (LOAD) { CV = PV; Q = false; _trig.update(CD); return false; }
    if (_trig.update(CD) && CV > 0) CV--;
    Q = (CV == 0);
    return Q;
  }

  void setPreset(uint16_t v) { PV = v; }
  void load()  { CV = PV; Q = false; }
  void reset() { CV = 0; Q = true; _trig.reset(); }

  private:
    RTrig _trig;
};

/* ---------------------------------------------------------------------------
 *  CTUD - Contador bidireccional
 *    Dos salidas: QU (CV >= PV) y QD (CV == 0).
 *    Uso clasico: contar piezas que entran y salen de un buffer para saber
 *    cuantas hay dentro en cada momento, y avisar cuando esta lleno o vacio.
 * ------------------------------------------------------------------------ */
struct Ctud {
  uint16_t PV = 0;
  uint16_t CV = 0;
  bool     QU = false;   /* alcanzado el maximo */
  bool     QD = false;   /* llegado a cero      */

  void update(bool CU, bool CD, bool RESET = false, bool LOAD = false) {
    if (RESET || LOAD) {
      CV = RESET ? 0 : PV;
      /* Se muestrean ambas entradas sin contar: asi no aparece un flanco
       * fantasma en el scan siguiente si alguna estaba activa. */
      _trigU.update(CU);
      _trigD.update(CD);
    } else {
      if (_trigU.update(CU) && CV < 0xFFFF) CV++;
      if (_trigD.update(CD) && CV > 0)      CV--;
    }
    QU = (CV >= PV);
    QD = (CV == 0);
  }

  void setPreset(uint16_t v) { PV = v; }
  void reset() { CV = 0; QU = false; QD = true; _trigU.reset(); _trigD.reset(); }

  private:
    RTrig _trigU, _trigD;
};

/* ---------------------------------------------------------------------------
 *  RunHourMeter - Cuentahoras de funcionamiento
 * ---------------------------------------------------------------------------
 *  No es IEC, pero toda maquina real lleva uno. Acumula el tiempo que un
 *  equipo ha estado en marcha para programar el mantenimiento preventivo
 *  ("cambiar la correa cada 500 horas"). Guardalo en la configuracion
 *  persistente para que sobreviva a los apagados: un cuentahoras que se pone
 *  a cero cada vez que se corta la luz no sirve para nada.
 * ------------------------------------------------------------------------ */
struct RunHourMeter {
  uint32_t totalSeconds = 0;

  void update(bool running) {
    cfsm_time_t now = cfsm_millis();
    if (running) {
      if (!_wasRunning) { _wasRunning = true; _last = now; }
      cfsm_time_t delta = (cfsm_time_t)(now - _last);
      if (delta >= 1000) {
        totalSeconds += delta / 1000;
        _last += (delta / 1000) * 1000;   /* conserva el resto: no se pierde tiempo */
      }
    } else {
      _wasRunning = false;
    }
  }

  uint32_t hours()   const { return totalSeconds / 3600UL; }
  uint16_t minutes() const { return (uint16_t)((totalSeconds % 3600UL) / 60UL); }
  void     reset()         { totalSeconds = 0; _wasRunning = false; }

  private:
    bool        _wasRunning = false;
    cfsm_time_t _last       = 0;
};

#endif /* COREFSM_COUNTERS_H */
