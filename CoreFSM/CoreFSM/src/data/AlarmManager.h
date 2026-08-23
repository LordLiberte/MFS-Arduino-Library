#ifndef COREFSM_ALARM_MANAGER_H
#define COREFSM_ALARM_MANAGER_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  AlarmManager.h  -  Gestion de alarmas al estilo de un HMI industrial
 * ---------------------------------------------------------------------------
 *  POR QUE NO BASTA CON UN Serial.println
 *  --------------------------------------
 *  Cuando una maquina falla de madrugada, lo unico que ve el tecnico a la
 *  manana siguiente es la maquina parada. Un mensaje impreso hace seis horas
 *  ya no esta en ninguna parte. Lo que hace falta es una LISTA DE ALARMAS que
 *  responda a cuatro preguntas:
 *
 *      QUE fallo         -> codigo y texto
 *      CUANDO            -> marca de tiempo
 *      SIGUE FALLANDO    -> activa o ya resuelta pero sin acusar
 *      QUIEN SE ENTERO   -> acusada o no
 *
 *  LOS TRES ESTADOS DE UNA ALARMA
 *  ------------------------------
 *  Es el modelo clasico y hay una razon para cada estado:
 *
 *      1. ACTIVA + SIN ACUSAR   La causa esta presente y nadie la ha visto.
 *                               Piloto rojo intermitente, zumbador sonando.
 *      2. ACTIVA + ACUSADA      El operario la ha visto y ha callado la
 *                               sirena, pero el problema sigue ahi.
 *                               Piloto rojo fijo, sin zumbador.
 *      3. INACTIVA + SIN ACUSAR La causa desaparecio sola, pero nadie llego a
 *                               verla. ESTE ES EL ESTADO IMPORTANTE: sin el,
 *                               un fallo intermitente (un contacto flojo que
 *                               falla una vez al dia) seria invisible para
 *                               siempre. La alarma se queda en la lista hasta
 *                               que alguien la acusa expresamente.
 *
 *  Una alarma solo desaparece de la lista cuando esta inactiva Y acusada.
 *
 *  SOBRE LA SEVERIDAD
 *  ------------------
 *  No todo fallo debe parar la maquina. Distinguir tres niveles evita el
 *  extremo de una maquina que se para por cualquier cosa y el contrario de
 *  una que no avisa hasta que se rompe:
 *
 *      INFO      Solo se registra. Fin de turno, cambio de receta.
 *      WARNING   Avisa pero se sigue produciendo. Nivel de material bajo,
 *                mantenimiento proximo, temperatura subiendo.
 *      FAULT     Para la estacion afectada. Timeout, sensor incoherente.
 *      CRITICAL  Para todo. Emergencia, riesgo de dano.
 * ======================================================================== */

enum AlarmSeverity : uint8_t {
  ALARM_INFO     = 0,
  ALARM_WARNING  = 1,
  ALARM_FAULT    = 2,
  ALARM_CRITICAL = 3
};

struct AlarmEntry {
  uint16_t      code      = 0;
  const __FlashStringHelper* text = nullptr;
  AlarmSeverity severity  = ALARM_INFO;
  bool          active    = false;   /* la causa sigue presente        */
  bool          acked     = false;   /* alguien la ha visto            */
  cfsm_time_t   firstTime = 0;       /* cuando aparecio por primera vez*/
  cfsm_time_t   lastTime  = 0;       /* ultima vez que se disparo      */
  uint16_t      count     = 0;       /* cuantas veces ha ocurrido      */
};

template <uint8_t MAX_ALARMS = 12>
class AlarmManager {
  public:
    AlarmManager() : _count(0) {}

    /* -----------------------------------------------------------------------
     *  DISPARAR UNA ALARMA
     *  Se puede llamar en cada scan sin problema: si la alarma ya estaba
     *  activa, solo se actualiza la marca de tiempo. El contador solo sube en
     *  las apariciones nuevas, que es lo que interesa para detectar fallos
     *  intermitentes ("este sensor ha fallado 47 veces esta semana").
     * -------------------------------------------------------------------- */
    bool raise(uint16_t code, const __FlashStringHelper* text,
               AlarmSeverity sev = ALARM_FAULT) {
      AlarmEntry* e = find(code);
      if (e) {
        if (!e->active) {           /* reaparicion */
          e->active = true;
          e->acked  = false;        /* una alarma que vuelve exige acuse nuevo */
          e->count++;
        }
        e->lastTime = cfsm_millis();
        return true;
      }
      if (_count >= MAX_ALARMS) { _overflow = true; return false; }
      AlarmEntry& n = _alarms[_count++];
      n.code = code; n.text = text; n.severity = sev;
      n.active = true; n.acked = false;
      n.firstTime = n.lastTime = cfsm_millis();
      n.count = 1;
      return true;
    }

    /* La causa ha desaparecido. La alarma NO se borra: se queda en la lista
     * hasta que alguien la acuse. */
    void clear(uint16_t code) {
      AlarmEntry* e = find(code);
      if (e) e->active = false;
    }

    /* Version comoda: raiseIf(condicion, ...) dispara o limpia segun toque. */
    void raiseIf(bool condition, uint16_t code,
                 const __FlashStringHelper* text, AlarmSeverity sev = ALARM_FAULT) {
      if (condition) raise(code, text, sev);
      else           clear(code);
    }

    /* -----------------------------------------------------------------------
     *  ACUSE
     * -------------------------------------------------------------------- */
    void ack(uint16_t code) {
      AlarmEntry* e = find(code);
      if (e) { e->acked = true; purgeInactive(); }
    }

    void ackAll() {
      for (uint8_t i = 0; i < _count; i++) _alarms[i].acked = true;
      purgeInactive();
    }

    /* -----------------------------------------------------------------------
     *  CONSULTA
     * -------------------------------------------------------------------- */
    bool isActive(uint16_t code) const {
      const AlarmEntry* e = find(code);
      return e && e->active;
    }

    bool hasActive() const {
      for (uint8_t i = 0; i < _count; i++) if (_alarms[i].active) return true;
      return false;
    }

    /* Hay algo que obligue a parar? Es la consulta que va en el enclavamiento
     * general de la maquina. */
    bool hasBlocking() const {
      for (uint8_t i = 0; i < _count; i++)
        if (_alarms[i].active && _alarms[i].severity >= ALARM_FAULT) return true;
      return false;
    }

    /* Hay alarmas nuevas sin acusar? Es lo que hace sonar el zumbador. */
    bool hasUnacked() const {
      for (uint8_t i = 0; i < _count; i++) if (!_alarms[i].acked) return true;
      return false;
    }

    AlarmSeverity highestSeverity() const {
      AlarmSeverity s = ALARM_INFO;
      for (uint8_t i = 0; i < _count; i++)
        if (_alarms[i].active && _alarms[i].severity > s) s = _alarms[i].severity;
      return s;
    }

    /* La alarma activa mas grave: la que hay que mostrar en la pantalla, que
     * solo tiene sitio para una linea. */
    const AlarmEntry* mostSevere() const {
      const AlarmEntry* best = nullptr;
      for (uint8_t i = 0; i < _count; i++) {
        if (!_alarms[i].active) continue;
        if (!best || _alarms[i].severity > best->severity) best = &_alarms[i];
      }
      return best;
    }

    uint8_t           count() const { return _count; }
    const AlarmEntry* at(uint8_t i) const { return (i < _count) ? &_alarms[i] : nullptr; }
    bool overflowed()  const { return _overflow; }

    /* Vacia la lista entera. Solo para mantenimiento; no lo pongas en un
     * boton accesible al operario. */
    void clearAll() { _count = 0; _overflow = false; }

    /* -----------------------------------------------------------------------
     *  VOLCADO
     * -------------------------------------------------------------------- */
    void printAll(Print& out) const {
      out.println(CFSM_FSTR("---- LISTA DE ALARMAS ----"));
      if (_count == 0) { out.println(CFSM_FSTR(" (sin alarmas)")); }
      for (uint8_t i = 0; i < _count; i++) {
        const AlarmEntry& a = _alarms[i];
        out.print(' ');
        out.print(a.active ? '*' : ' ');       /* activa            */
        out.print(a.acked  ? ' ' : '!');       /* pendiente de acuse*/
        out.print(CFSM_FSTR(" 0x"));
        if (a.code < 0x1000) out.print('0');
        if (a.code < 0x100)  out.print('0');
        if (a.code < 0x10)   out.print('0');
        out.print(a.code, HEX);
        out.print(' ');
        out.print(severityName(a.severity));
        out.print(' ');
        if (a.text) out.print(a.text);
        out.print(CFSM_FSTR("  (n="));
        out.print(a.count);
        out.print(CFSM_FSTR(" t="));
        out.print(a.lastTime / 1000);
        out.println(CFSM_FSTR("s)"));
      }
      if (_overflow) out.println(CFSM_FSTR(" *** LISTA LLENA: hay alarmas sin registrar ***"));
      out.println(CFSM_FSTR("--------------------------"));
    }

    static const __FlashStringHelper* severityName(AlarmSeverity s) {
      switch (s) {
        case ALARM_INFO:     return CFSM_FSTR("INFO");
        case ALARM_WARNING:  return CFSM_FSTR("AVISO");
        case ALARM_FAULT:    return CFSM_FSTR("FALLO");
        default:             return CFSM_FSTR("CRITICO");
      }
    }

  private:
    AlarmEntry _alarms[MAX_ALARMS];
    uint8_t    _count;
    bool       _overflow = false;

    AlarmEntry* find(uint16_t code) {
      for (uint8_t i = 0; i < _count; i++) if (_alarms[i].code == code) return &_alarms[i];
      return nullptr;
    }
    const AlarmEntry* find(uint16_t code) const {
      for (uint8_t i = 0; i < _count; i++) if (_alarms[i].code == code) return &_alarms[i];
      return nullptr;
    }

    /* Quita de la lista las que ya estan resueltas Y acusadas, compactando el
     * array. Se hace solo al acusar, nunca en el scan. */
    void purgeInactive() {
      uint8_t w = 0;
      for (uint8_t r = 0; r < _count; r++) {
        if (_alarms[r].active || !_alarms[r].acked) {
          if (w != r) _alarms[w] = _alarms[r];
          w++;
        }
      }
      _count = w;
      if (_count < MAX_ALARMS) _overflow = false;
    }
};

#endif /* COREFSM_ALARM_MANAGER_H */
