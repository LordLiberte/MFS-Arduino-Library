#ifndef COREFSM_EDGES_H
#define COREFSM_EDGES_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  Edges.h  -  Deteccion de flancos y biestables (IEC 61131-3)
 * ---------------------------------------------------------------------------
 *  EL PROBLEMA DEL FLANCO
 *  ----------------------
 *  Un ciclo de scan dura microsegundos. Cuando alguien pulsa un boton, lo
 *  mantiene apretado unos 200 ms: para el programa, eso son miles de ciclos
 *  con la senal a true. Si escribes:
 *
 *      if (pulsador) contador++;
 *
 *  el contador sube varios miles con una sola pulsacion. Es el fallo mas
 *  clasico del principiante, y produce sintomas desconcertantes: "he pulsado
 *  una vez y ha hecho el ciclo veinte veces".
 *
 *  Lo que casi siempre quieres no es el NIVEL de la senal, sino el INSTANTE
 *  en que cambia: el flanco. R_TRIG detecta el flanco de subida (el momento
 *  de pulsar) y F_TRIG el de bajada (el momento de soltar).
 *
 *      IN   ___┌─────────────┐_______
 *      R_TRIG __┐_____________________   (un solo scan a true)
 *      F_TRIG _______________┐_______   (un solo scan a true)
 *
 *  NOTA SOBRE EL ANTIRREBOTE
 *  -------------------------
 *  Detectar el flanco NO elimina los rebotes mecanicos del contacto. Un
 *  pulsador real, al cerrarse, produce durante unos milisegundos una rafaga de
 *  aperturas y cierres, y cada una es un flanco. Para eso esta el filtro
 *  antirrebote que ya lleva incorporado DigitalSensor. Si lees el pin a pelo
 *  con digitalRead y le pones un R_TRIG, tendras rebotes.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  R_TRIG - Flanco de subida
 * ------------------------------------------------------------------------ */
struct RTrig {
  bool Q = false;

  bool update(bool IN) {
    Q = IN && !_last;
    _last = IN;
    return Q;
  }

  void reset() { _last = false; Q = false; }

  private:
    bool _last = false;
};

/* ---------------------------------------------------------------------------
 *  F_TRIG - Flanco de bajada
 * ------------------------------------------------------------------------ */
struct FTrig {
  bool Q = false;

  bool update(bool IN) {
    Q = !IN && _last;
    _last = IN;
    return Q;
  }

  void reset() { _last = false; Q = false; }

  private:
    bool _last = false;
};

/* ---------------------------------------------------------------------------
 *  Detector de ambos flancos
 * ------------------------------------------------------------------------ */
struct EdgeDetect {
  bool rising  = false;
  bool falling = false;
  bool changed = false;

  bool update(bool IN) {
    rising  = IN && !_last;
    falling = !IN && _last;
    changed = rising || falling;
    _last   = IN;
    return changed;
  }

  void reset() { _last = false; rising = falling = changed = false; }

  private:
    bool _last = false;
};

/* ---------------------------------------------------------------------------
 *  SR - Biestable con prioridad a SET (Set dominante)
 * ---------------------------------------------------------------------------
 *  Un enclavamiento: una vez activado, se queda activado hasta que alguien lo
 *  desactive expresamente. Con S1 y R1 simultaneos, gana S1.
 *
 *  Uso tipico: memorizar que una alarma ha ocurrido aunque su causa ya haya
 *  desaparecido. Que un fallo se autoborre porque el sensor volvio a su sitio
 *  es justo lo que no quieres: el operario tiene que enterarse de que paso.
 * ------------------------------------------------------------------------ */
struct SR {
  bool Q1 = false;

  bool update(bool S1, bool R) {
    if (R)  Q1 = false;
    if (S1) Q1 = true;      /* S1 se evalua el ultimo: por eso domina */
    return Q1;
  }

  void reset() { Q1 = false; }
};

/* ---------------------------------------------------------------------------
 *  RS - Biestable con prioridad a RESET (Reset dominante)
 * ---------------------------------------------------------------------------
 *  Con S y R1 simultaneos, gana R1. Es el que se usa siempre que la seguridad
 *  esta en juego: si la orden de marcha y la de parada llegan a la vez, tiene
 *  que ganar la parada. Esta es la unica diferencia entre SR y RS, y no es un
 *  detalle menor.
 * ------------------------------------------------------------------------ */
struct RS {
  bool Q1 = false;

  bool update(bool S, bool R1) {
    if (S)  Q1 = true;
    if (R1) Q1 = false;     /* R1 se evalua el ultimo: por eso domina */
    return Q1;
  }

  void reset() { Q1 = false; }
};

/* ---------------------------------------------------------------------------
 *  Toggle - Conmutador por pulsacion
 * ---------------------------------------------------------------------------
 *  Cada flanco de subida invierte la salida. El clasico interruptor de un solo
 *  boton: pulsas y enciende, vuelves a pulsar y apaga.
 * ------------------------------------------------------------------------ */
struct Toggle {
  bool Q = false;

  bool update(bool IN) {
    if (_trig.update(IN)) Q = !Q;
    return Q;
  }

  void set(bool v) { Q = v; }
  void reset()     { Q = false; _trig.reset(); }

  private:
    RTrig _trig;
};

#endif /* COREFSM_EDGES_H */
