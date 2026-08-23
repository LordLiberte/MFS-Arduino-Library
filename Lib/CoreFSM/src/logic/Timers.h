#ifndef COREFSM_TIMERS_H
#define COREFSM_TIMERS_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  Timers.h  -  Temporizadores IEC 61131-3 (TON, TOF, TP)
 * ---------------------------------------------------------------------------
 *  POR QUE ESTAN AQUI
 *  ------------------
 *  Son los tres temporizadores que existen en TODOS los automatas del mundo,
 *  desde un Siemens LOGO hasta un Allen-Bradley ControlLogix, porque estan
 *  normalizados en la IEC 61131-3. Si vienes de programar autOmatas, ya sabes
 *  usarlos y no tienes que aprender nada nuevo. Y si no vienes de ahi, resuelven
 *  los tres casos de temporizacion que aparecen una y otra vez.
 *
 *  Cada uno se usa igual: en cada vuelta del scan le pasas la condicion de
 *  entrada IN y el temporizador te devuelve la salida Q ya calculada.
 *
 *  DIAGRAMA DE LOS TRES
 *  --------------------
 *
 *    TON (retardo a la conexion) - "espera antes de actuar"
 *      IN  ___┌───────────┐_____
 *      Q   _______┌───────┐_____
 *              |<-PT->|
 *      Q sube PT milisegundos DESPUES de que IN suba.
 *      Si IN cae antes de cumplirse PT, se cancela y el conteo vuelve a cero.
 *      Uso tipico: confirmar un sensor (evita disparos por una chispa),
 *      dar tiempo a que suba la presion, filtrar una alarma intermitente.
 *
 *    TOF (retardo a la desconexion) - "manten un rato mas"
 *      IN  ___┌───────┐_________
 *      Q   ___┌───────────┐_____
 *                     |<-PT->|
 *      Q sube a la vez que IN, pero se mantiene PT despues de que IN caiga.
 *      Uso tipico: un ventilador que sigue girando tras parar el motor, una
 *      luz que tarda en apagarse, mantener un rele pegado durante un microcorte.
 *
 *    TP (impulso) - "un pulso de duracion fija"
 *      IN  ___┌─┐_______┌───────┐
 *      Q   ___┌─────┐___┌─────┐_
 *             |<-PT->|
 *      Q sube al flanco de IN y dura exactamente PT, pase lo que pase con IN.
 *      Uso tipico: pulso de disparo a una camara, golpe de aire de un eyector,
 *      pitido de aviso de duracion fija.
 *
 *  DETALLE DE IMPLEMENTACION
 *  -------------------------
 *  Todos usan cfsm_elapsed(), que hace la resta en aritmetica sin signo y por
 *  tanto es inmune al desbordamiento de millis() a los 49,7 dias.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  TON - Timer On Delay (retardo a la conexion)
 * ------------------------------------------------------------------------ */
struct Ton {
  cfsm_time_t PT = 0;      /* tiempo preseleccionado, en ms  */
  bool        Q  = false;  /* salida                          */
  cfsm_time_t ET = 0;      /* tiempo transcurrido, en ms      */

  /* Llamar una vez por scan. Devuelve Q por comodidad. */
  bool update(bool IN) {
    if (!IN) {
      _running = false;
      ET = 0;
      Q  = false;
      return false;
    }
    if (!_running) {          /* flanco de subida de IN: arranca el conteo */
      _running = true;
      _start   = cfsm_millis();
    }
    ET = cfsm_elapsed(_start);
    if (ET >= PT) { ET = PT; Q = true; }
    return Q;
  }

  void setPreset(cfsm_time_t ms) { PT = ms; }
  void reset() { _running = false; ET = 0; Q = false; }

  private:
    bool        _running = false;
    cfsm_time_t _start   = 0;
};

/* ---------------------------------------------------------------------------
 *  TOF - Timer Off Delay (retardo a la desconexion)
 * ------------------------------------------------------------------------ */
struct Tof {
  cfsm_time_t PT = 0;
  bool        Q  = false;
  cfsm_time_t ET = 0;

  bool update(bool IN) {
    if (IN) {
      _running = false;
      ET = 0;
      Q  = true;
      _wasHigh = true;
      return true;
    }
    if (_wasHigh) {           /* flanco de bajada de IN: arranca el conteo */
      _wasHigh = false;
      _running = true;
      _start   = cfsm_millis();
    }
    if (_running) {
      ET = cfsm_elapsed(_start);
      if (ET >= PT) { ET = PT; Q = false; _running = false; }
    }
    return Q;
  }

  void setPreset(cfsm_time_t ms) { PT = ms; }
  void reset() { _running = false; _wasHigh = false; ET = 0; Q = false; }

  private:
    bool        _running = false;
    bool        _wasHigh = false;
    cfsm_time_t _start   = 0;
};

/* ---------------------------------------------------------------------------
 *  TP - Timer Pulse (impulso de duracion fija)
 * ------------------------------------------------------------------------ */
struct Tp {
  cfsm_time_t PT = 0;
  bool        Q  = false;
  cfsm_time_t ET = 0;

  bool update(bool IN) {
    if (IN && !_lastIN && !Q) {   /* flanco de subida y no hay pulso en curso */
      Q      = true;
      _start = cfsm_millis();
    }
    if (Q) {
      ET = cfsm_elapsed(_start);
      if (ET >= PT) { ET = PT; Q = false; }
    } else if (!IN) {
      ET = 0;
    }
    _lastIN = IN;
    return Q;
  }

  void setPreset(cfsm_time_t ms) { PT = ms; }
  void reset() { Q = false; ET = 0; _lastIN = false; }

  private:
    bool        _lastIN = false;
    cfsm_time_t _start  = 0;
};

/* ---------------------------------------------------------------------------
 *  Blink - Generador de onda cuadrada (no es IEC, pero se usa constantemente)
 * ---------------------------------------------------------------------------
 *  Para pilotos intermitentes, zumbadores y avisos. Permite tiempos distintos
 *  de encendido y apagado, que es lo que distingue visualmente un aviso
 *  (parpadeo lento y simetrico) de una alarma grave (destellos cortos y
 *  rapidos): el operario los diferencia desde el otro lado de la nave sin
 *  tener que acercarse a leer la pantalla.
 * ------------------------------------------------------------------------ */
struct Blink {
  cfsm_time_t onTime  = 500;
  cfsm_time_t offTime = 500;
  bool        Q       = false;

  bool update(bool enable = true) {
    if (!enable) { Q = false; _start = cfsm_millis(); return false; }
    cfsm_time_t target = Q ? onTime : offTime;
    if (cfsm_elapsed(_start) >= target) {
      Q      = !Q;
      _start = cfsm_millis();
    }
    return Q;
  }

  void setPeriod(cfsm_time_t on, cfsm_time_t off) { onTime = on; offTime = off; }
  void setPeriod(cfsm_time_t halfPeriod) { onTime = offTime = halfPeriod; }

  private:
    cfsm_time_t _start = 0;
};

#endif /* COREFSM_TIMERS_H */
