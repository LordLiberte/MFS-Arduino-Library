#ifndef COREFSM_FSM_BLOCK_H
#define COREFSM_FSM_BLOCK_H

#include "BlockBase.h"
#include "ControlWords.h"

/* ===========================================================================
 *  FsmBlock.h  -  Motor de estados de alto nivel
 * ---------------------------------------------------------------------------
 *  QUE RESUELVE
 *  ------------
 *  Todo equipo automatico, sea una cinta, una prensa o un robot, atraviesa
 *  siempre el mismo ciclo de vida: esta parado, arranca, produce, se para,
 *  y de vez en cuando falla. Escribir ese ciclo a mano en cada bloque nuevo
 *  es repetitivo y es donde se cuelan los errores.
 *
 *  FsmBlock lo implementa una sola vez. Los bloques hijos heredan los estados,
 *  las transiciones legales y los comandos, y solo escriben lo que su maquina
 *  hace de verdad.
 *
 *  EL MODELO DE ESTADOS
 *  --------------------
 *  Es un subconjunto pragmatico de PackML (ISA-88 / OMAC), el estandar de
 *  estados de maquina mas extendido en la industria. Se ha recortado a los
 *  estados que de verdad se usan en una maquina pequena, porque los once
 *  estados completos de PackML son un exceso para un Arduino.
 *
 *      IDLE ──start──> STARTING ──(auto)──> RUNNING
 *       ^                                    │  │
 *       │                                    │  └──hold──> PAUSED ──resume──┐
 *       │                                    │                              │
 *       │                                    ▼                              │
 *       └──(auto)── STOPPED <──(auto)── STOPPING <────────stop───────────────┘
 *
 *      Desde CUALQUIER estado: fault() ──> ERROR ──reset()──> IDLE
 *
 *  Las transiciones ilegales se ignoran en silencio en lugar de provocar un
 *  comportamiento raro. Llamar a start() con la maquina ya en marcha no hace
 *  nada; llamar a resume() sin estar en pausa tampoco. Eso hace que el codigo
 *  del .ino pueda ser descuidado sin consecuencias: puedes llamar a start()
 *  en cada vuelta del scan mientras el operario tenga el dedo en el pulsador
 *  y la maquina solo arrancara una vez.
 * ======================================================================== */

enum SystemState : uint8_t {
  STATE_IDLE     = 0,  /* Parada, sin fallos, lista para recibir orden.       */
  STATE_STARTING = 1,  /* Secuencia de arranque (precalentar, presurizar...). */
  STATE_RUNNING  = 2,  /* Produciendo. Es el unico estado donde corre la
                          logica de proceso del bloque hijo.                  */
  STATE_PAUSED   = 3,  /* Pausa en caliente. El paso actual se conserva y las
                          salidas peligrosas se apagan, pero no se pierde el
                          punto de la secuencia: se puede reanudar.           */
  STATE_STOPPING = 4,  /* Secuencia ordenada de parada (frenar, replegar...). */
  STATE_STOPPED  = 5,  /* Parada completada. Transita solo a IDLE.            */
  STATE_ERROR    = 6   /* Alarma. Retenido hasta que alguien rearme.          */
};

/* Nombre legible del estado, en memoria de programa. */
inline const __FlashStringHelper* cfsmStateName(uint8_t s) {
  switch (s) {
    case STATE_IDLE:     return CFSM_FSTR("IDLE");
    case STATE_STARTING: return CFSM_FSTR("STARTING");
    case STATE_RUNNING:  return CFSM_FSTR("RUNNING");
    case STATE_PAUSED:   return CFSM_FSTR("PAUSED");
    case STATE_STOPPING: return CFSM_FSTR("STOPPING");
    case STATE_STOPPED:  return CFSM_FSTR("STOPPED");
    case STATE_ERROR:    return CFSM_FSTR("ERROR");
    default:             return CFSM_FSTR("???");
  }
}

class FsmBlock : public BlockBase {
  public:
    FsmBlock()
      : _currentState(STATE_IDLE), _previousState(STATE_IDLE),
        _errorCode(CFSM_ERR_NONE),
        _startupTimeMs(0), _shutdownTimeMs(0) {}

    /* -----------------------------------------------------------------------
     *  CONSULTA DE ESTADO
     * -------------------------------------------------------------------- */
    uint8_t getState() const override { return (uint8_t)_currentState; }
    SystemState state() const         { return _currentState; }
    SystemState previousState() const { return _previousState; }

    bool isIdle()    const { return _currentState == STATE_IDLE; }
    bool isRunning() const { return _currentState == STATE_RUNNING; }
    bool isPaused()  const { return _currentState == STATE_PAUSED; }
    bool isFaulted() const override { return _currentState == STATE_ERROR; }

    /* Un bloque esta "activo" si su logica de proceso debe ejecutarse.
     * Es el chequeo que va al principio de todo update() de bloque hijo. */
    bool isActive()  const { return _currentState == STATE_RUNNING; }

    uint16_t getErrorCode() const { return _errorCode; }
    const __FlashStringHelper* getErrorText() const { return cfsmErrorText(_errorCode); }

    /* -----------------------------------------------------------------------
     *  COMANDOS
     *  Todos comprueban primero si la transicion es legal. Una orden que no
     *  procede se descarta sin hacer nada y sin quejarse: eso permite
     *  llamarlos incondicionalmente desde el scan.
     * -------------------------------------------------------------------- */

    /* Arranca.
     *
     * Si no hay fase de arranque configurada (_startupTimeMs == 0), pasa
     * DIRECTAMENTE a RUNNING. Si la hay, se queda en STARTING el tiempo
     * indicado, util para dar tiempo a que suba la presion de aire, arranque
     * un ventilador o se caliente una resistencia.
     *
     * El salto directo no es solo comodidad: hace que un bloque escrito a la
     * antigua -que gobierna sus pasos a mano sin llamar a updateSequence()-
     * siga funcionando. Si start() dejara la maquina en STARTING, quien
     * tendria que sacarla de ahi es updateFsm(), y ese bloque no lo llama
     * nunca: se quedaria colgado en el arranque para siempre. */
    void start() override {
      if (_currentState == STATE_IDLE || _currentState == STATE_STOPPED) {
        transitionTo(_startupTimeMs > 0 ? STATE_STARTING : STATE_RUNNING);
      }
    }

    /* Parada ordenada. Pasa por STOPPING para que el bloque hijo pueda
     * replegar actuadores antes de darse por parado. */
    void stop() override {
      if (_currentState == STATE_RUNNING  ||
          _currentState == STATE_STARTING ||
          _currentState == STATE_PAUSED) {
        transitionTo(STATE_STOPPING);
      }
    }

    /* Pausa en caliente. A diferencia de stop(), NO pierde el punto de la
     * secuencia: al reanudar se continua exactamente donde estaba. Es el
     * boton amarillo de una maquina real. */
    void hold() override {
      if (_currentState == STATE_RUNNING) transitionTo(STATE_PAUSED);
    }

    void resume() override {
      if (_currentState == STATE_PAUSED) transitionTo(STATE_RUNNING);
    }

    /* Declara una alarma. Se puede llamar desde cualquier estado y desde
     * cualquier sitio: es la via de escape universal. */
    virtual void fault(uint16_t code = CFSM_ERR_INTERLOCK) {
      /* Se conserva el PRIMER codigo de error. Si una averia provoca en
       * cascada otras tres, lo que le interesa al tecnico es la causa raiz,
       * no el ultimo sintoma. */
      if (_currentState != STATE_ERROR) {
        _errorCode = code;
        transitionTo(STATE_ERROR);
      }
    }

    /* Rearme. Solo tiene efecto estando en ERROR, y solo si el bloque hijo
     * autoriza el rearme (ver canReset()). Si la causa del fallo sigue
     * presente no hace nada, y es deliberado: pulsar rearme veinte veces con
     * la seta todavia hundida no debe conseguir nada. */
    void reset() override {
      if (_currentState != STATE_ERROR) return;
      if (!canReset()) return;         /* la causa sigue presente */
      _errorCode = CFSM_ERR_NONE;
      transitionTo(STATE_IDLE);
    }

    /* Aborto inmediato: salta a ERROR sin pasar por STOPPING. Es lo que hace
     * una seta de emergencia. No es lo mismo que stop(): stop() es ordenado
     * y termina el ciclo; abort() corta en seco. */
    void abort(uint16_t code = CFSM_ERR_ESTOP) override { fault(code); }

    /* Traduccion de la seta de emergencia a alarma. Se llama a todos los
     * bloques por igual, incluidos los que no son FsmBlock. */
    void onEmergencyStop() override { if (!isFaulted()) fault(CFSM_ERR_ESTOP); }

    /* -----------------------------------------------------------------------
     *  PARAMETROS DE LAS FASES DE ARRANQUE Y PARADA
     *  Si valen 0 (por defecto), las fases son instantaneas.
     * -------------------------------------------------------------------- */
    void setStartupTime(cfsm_time_t ms)  { _startupTimeMs  = ms; }
    void setShutdownTime(cfsm_time_t ms) { _shutdownTimeMs = ms; }

    /* -----------------------------------------------------------------------
     *  MOTOR DE LA MAQUINA DE ESTADOS
     *  El bloque hijo llama a esto al principio de su update(). Gestiona las
     *  transiciones automaticas (STARTING->RUNNING, STOPPING->STOPPED,
     *  STOPPED->IDLE) y devuelve true si la logica de proceso debe ejecutarse.
     *
     *  Patron de uso en un bloque hijo:
     *
     *      void update() override {
     *        if (!updateFsm()) { apagarSalidas(); return; }
     *        ... logica de proceso ...
     *      }
     * -------------------------------------------------------------------- */
    bool updateFsm() {
      switch (_currentState) {
        case STATE_STARTING:
          if (getTimeInState() >= _startupTimeMs) transitionTo(STATE_RUNNING);
          break;

        case STATE_STOPPING:
          if (getTimeInState() >= _shutdownTimeMs) transitionTo(STATE_STOPPED);
          break;

        case STATE_STOPPED:
          /* Un ciclo de gracia en STOPPED para que el hijo pueda reaccionar
           * en onTransition, y despues vuelve solo a reposo. */
          transitionTo(STATE_IDLE);
          break;

        default:
          break;
      }
      return _currentState == STATE_RUNNING;
    }

  protected:
    SystemState _currentState;
    SystemState _previousState;
    uint16_t    _errorCode;

    cfsm_time_t _startupTimeMs;
    cfsm_time_t _shutdownTimeMs;

    /* Cambio de estado. Reinicia el cronometro de estado y avisa al hijo.
     * Ignora las transiciones a uno mismo, de modo que llamarlo de mas es
     * inofensivo y no reinicia el temporizador por accidente. */
    void transitionTo(SystemState newState) {
      if (_currentState == newState) return;
      _previousState  = _currentState;
      _currentState   = newState;
      _stateStartTime = cfsm_millis();
      onTransition(_previousState, _currentState);
    }

    /* -----------------------------------------------------------------------
     *  HOOKS PARA EL BLOQUE HIJO
     * -------------------------------------------------------------------- */

    /* Se ejecuta UNA sola vez en el instante del cambio de estado. Es el sitio
     * correcto para imprimir por serie, poner una salida a su valor seguro o
     * inicializar variables. Nunca metas aqui esperas ni bucles. */
    virtual void onTransition(SystemState from, SystemState to) {
      CFSM_UNUSED(from); CFSM_UNUSED(to);
    }

    /* Autorizacion de rearme. Por defecto siempre se autoriza, pero un bloque
     * hijo puede negarse mientras la causa fisica del fallo siga presente:
     *
     *      bool canReset() const override {
     *        return !setaEmergenciaPulsada && puertaCerrada;
     *      }
     *
     * Esto evita el vicio clasico de darle al boton de rearme cincuenta veces
     * con la seta todavia pulsada. */
    virtual bool canReset() const { return true; }
};

#endif /* COREFSM_FSM_BLOCK_H */
