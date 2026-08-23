#ifndef COREFSM_SEQUENCE_BLOCK_H
#define COREFSM_SEQUENCE_BLOCK_H

#include "FsmBlock.h"
#include "Handshake.h"

/* ===========================================================================
 *  SequenceBlock.h  -  Secuenciador por pasos (GRAFCET / SFC) con SC y ST
 * ---------------------------------------------------------------------------
 *  DOS NIVELES DE MAQUINA DE ESTADOS, NO UNO
 *  -----------------------------------------
 *  Es la confusion mas habitual al empezar, asi que conviene dejarla clara:
 *
 *    - El ESTADO (SystemState: IDLE, RUNNING, ERROR...) describe la situacion
 *      del equipo. Lo gestiona FsmBlock. Es igual para una cinta que para una
 *      prensa: toda maquina esta parada, en marcha o averiada.
 *
 *    - El PASO (_currentStep: 0, 10, 20...) describe por donde va el proceso
 *      concreto. Lo gestiona esta clase. Es distinto en cada maquina: avanzar
 *      carro, soldar, retroceder.
 *
 *  El estado ENVUELVE al paso. Los pasos solo corren cuando el estado es
 *  RUNNING. Si la maquina se para o falla, el paso se congela donde estaba.
 *
 *  POR QUE LOS PASOS VAN DE 10 EN 10
 *  ---------------------------------
 *  Costumbre heredada del GRAFCET y de la programacion de automatas: numerar
 *  0, 10, 20, 30 deja hueco para intercalar un paso 15 el dia que haga falta
 *  sin renumerar toda la secuencia ni tocar los mensajes de diagnostico. Con
 *  numeracion 0,1,2,3 cualquier insercion obliga a renumerar.
 *
 *  LAS ESTRUCTURAS SC Y ST
 *  -----------------------
 *  Copian el esquema de los SEQCTL industriales:
 *
 *    SC (Sequence Control) -> donde esta la secuencia: paso, cronometros,
 *                             contador de ciclos.
 *    ST (Status)           -> como esta el equipo: palabra de estado (STW),
 *                             palabra de mando (CFGW), codigo de error.
 *
 *  Tener el mando y el estado empaquetados en dos enteros de 16 bits permite
 *  gobernar y supervisar la maquina desde un HMI, un Modbus o el monitor serie
 *  sin escribir un protocolo a medida.
 *
 *  DOS FORMAS DE MANDAR, LAS DOS VALIDAS
 *  -------------------------------------
 *    estacion.start();                 // llamada directa, comoda en el .ino
 *    estacion.ST.cfgw.start = true;    // por palabra de mando, como un bus
 *
 *  updateSequence() traduce los bits de CFGW a las llamadas equivalentes y los
 *  consume, de modo que ambas vias conviven sin interferirse.
 * ======================================================================== */

/* Valor reservado que significa "todavia no ha habido paso anterior". */
#define CFSM_NO_STEP  0xFFFF

class SequenceBlock : public FsmBlock {
  public:

    /* -----------------------------------------------------------------------
     *  SC - Datos de control de secuencia
     * -------------------------------------------------------------------- */
    struct SequenceData {
      uint16_t    step            = 0;            /* paso activo               */
      uint16_t    lastStep        = CFSM_NO_STEP; /* paso del que se viene     */
      cfsm_time_t stepStartTime   = 0;            /* marca de entrada al paso  */
      cfsm_time_t stepTimeout     = 0;            /* 0 = sin vigilancia        */
      cfsm_time_t cycleStartTime  = 0;            /* marca de inicio de ciclo  */
      cfsm_time_t cycleTimeout    = 0;            /* 0 = sin vigilancia        */
      cfsm_time_t lastCycleTimeMs = 0;            /* duracion del ultimo ciclo */
      uint32_t    cycleCount      = 0;            /* piezas / ciclos completos */
      uint16_t    initialStep     = 0;            /* paso al que vuelve el reset*/
    } SC;

    /* -----------------------------------------------------------------------
     *  ST - Estado y mando
     * -------------------------------------------------------------------- */
    struct StatusData {
      StatusWord stw;                 /* lo que la estacion responde   */
      ConfigWord cfgw;                /* lo que se le ordena           */
      uint16_t   errorCode = CFSM_ERR_NONE;
    } ST;

    /* Interfaz de traspaso con las estaciones vecinas. */
    Handshake handshake;

    /* -----------------------------------------------------------------------
     *  Constructor
     *  _currentStep es una referencia a SC.step. Existe para que el codigo que
     *  escribas dentro del bloque pueda seguir diciendo switch (_currentStep),
     *  que se lee mucho mejor que switch (SC.step). Cuesta 2 bytes por
     *  instancia y ahorra ruido en el sitio donde mas se lee el codigo.
     * -------------------------------------------------------------------- */
    SequenceBlock() : _currentStep(SC.step) {
      ST.stw.raw  = 0;
      ST.cfgw.raw = 0;
      ST.cfgw.quickStop = true;   /* activo a bajo: true = sin parada rapida */

      /* La habilitacion nace a TRUE. Es lo que hace que la via de mando
       * directa (bloque.start()) funcione sin ceremonias, que es como se usa
       * la libreria el 90% de las veces.
       *
       * Si gobiernas la maquina por bus o por HMI, baja este bit desde tu
       * codigo: perderlo equivale a una parada ordenada, y una trama perdida
       * o un cable cortado -que llegan como ceros- paran la maquina sola. */
      ST.cfgw.enable = true;
    }

    /* -----------------------------------------------------------------------
     *  CONSULTA
     * -------------------------------------------------------------------- */
    uint16_t    getStep() const        { return SC.step; }
    uint16_t    getLastStep() const    { return SC.lastStep; }
    cfsm_time_t getTimeInStep() const  { return cfsm_elapsed(SC.stepStartTime); }
    cfsm_time_t getCycleTime() const   { return cfsm_elapsed(SC.cycleStartTime); }
    cfsm_time_t getLastCycleTime() const { return SC.lastCycleTimeMs; }
    uint32_t    getCycleCount() const  { return SC.cycleCount; }

    /* Es el primer ciclo de scan dentro de este paso? Sirve para hacer una
     * accion puntual sin necesidad de sobrescribir onStepEntered(). */
    bool isFirstScanInStep() const { return _firstScan; }

    /* -----------------------------------------------------------------------
     *  COMANDOS
     * -------------------------------------------------------------------- */
    void start() override {
      if (_currentState == STATE_IDLE || _currentState == STATE_STOPPED) {
        setStep(SC.initialStep);
        SC.cycleStartTime = cfsm_millis();
        ST.stw.done       = false;    /* empieza un ciclo nuevo */
        /* Sin fase de arranque configurada se va directo a RUNNING. Ver la
         * explicacion en FsmBlock::start(): es lo que mantiene vivos los
         * bloques escritos sin updateSequence(). */
        transitionTo(_startupTimeMs > 0 ? STATE_STARTING : STATE_RUNNING);
      }
    }

    void reset() override {
      /* Solo se toca el paso y el handshake si se venia de una alarma. Un
       * resetAll() lanzado desde la consola mientras una estacion sana espera
       * el acuse de su vecina no debe borrarle los bits del traspaso: la
       * vecina se quedaria esperando una pieza que ya paso. */
      bool veniaDeFallo = (_currentState == STATE_ERROR);
      FsmBlock::reset();
      if (veniaDeFallo && _currentState == STATE_IDLE) {
        setStep(SC.initialStep);
        ST.errorCode = CFSM_ERR_NONE;
        handshake.reset();
      }
    }

    void fault(uint16_t code = CFSM_ERR_INTERLOCK) override {
      if (_currentState != STATE_ERROR) {
        ST.errorCode = code;
        handshake.statusError = true;
        handshake.statusBusy  = false;
      }
      FsmBlock::fault(code);
    }

    /* Paso al que vuelve la secuencia al arrancar y al rearmar. Por defecto 0. */
    void setInitialStep(uint16_t s) { SC.initialStep = s; }

    /* Vigilancia del ciclo completo, ademas de la de cada paso. Detecta el
     * caso perverso de una secuencia que va saltando entre dos pasos sin
     * agotar ninguno pero sin terminar nunca. */
    void setCycleTimeout(cfsm_time_t ms) { SC.cycleTimeout = ms; }

    /* -----------------------------------------------------------------------
     *  MOTOR DE LA SECUENCIA
     *  Se llama SIEMPRE al principio del update() del bloque hijo. Devuelve
     *  true si toca ejecutar la logica de pasos.
     *
     *      void update() override {
     *        if (!updateSequence()) { salidasSeguras(); return; }
     *        switch (_currentStep) { ... }
     *      }
     *
     *  Hace, en este orden:
     *    1. Traduce y consume los bits de la palabra de mando CFGW.
     *    2. Avanza la maquina de estados de alto nivel (FsmBlock).
     *    3. Vigila el timeout del paso y el del ciclo.
     *    4. Refleja todo en la palabra de estado STW.
     *    5. Aplica el modo paso a paso si esta activo.
     * -------------------------------------------------------------------- */
    bool updateSequence() {
      /* --- 1. Palabra de mando ------------------------------------------ */
      processControlWord();

      /* --- 2. Maquina de estados de alto nivel -------------------------- */
      bool active = updateFsm();

      /* --- 3. Retencion en modo paso a paso -----------------------------
       * Se evalua ANTES de las vigilancias de tiempo. Si se hiciera despues,
       * el tecnico que tarda cinco segundos en pulsar "siguiente paso" veria
       * saltar el watchdog sin que se haya ejecutado una sola linea del paso,
       * y el modo de puesta en marcha seria inservible. */
      bool retenido = active && ST.cfgw.singleStep && !_stepAuthorised;

      /* --- 4. Congelacion de relojes ------------------------------------
       * Los cronometros del paso y del ciclo se detienen mientras la maquina
       * esta en pausa o retenida. Sin esto, una pausa de treinta segundos
       * haria saltar el watchdog del paso en el instante de reanudar: la
       * maquina caeria en alarma por haber estado parada, que es justo lo
       * contrario de lo que debe pasar. */
      bool congelado = retenido || (_currentState == STATE_PAUSED);
      if (congelado) {
        if (!_frozen) { _frozen = true; _freezeStart = cfsm_millis(); }
      } else if (_frozen) {
        cfsm_time_t parado = cfsm_elapsed(_freezeStart);
        SC.stepStartTime  += parado;    /* se desplazan las marcas de origen */
        SC.cycleStartTime += parado;
        _frozen = false;
      }

      /* --- 5. Vigilancias de tiempo ------------------------------------- */
      if (active && !congelado && !ST.cfgw.bypassTimer) {
        if (isStepTimedOut()) {
          ST.stw.stepTimeout = true;
          fault(CFSM_ERR_STEP_TIMEOUT);
          active = false;
        }
        else if (SC.cycleTimeout > 0 && getCycleTime() >= SC.cycleTimeout) {
          fault(CFSM_ERR_CYCLE_TIMEOUT);
          active = false;
        }
      }

      /* --- 6. Palabra de estado ----------------------------------------- */
      syncStatusWord();

      /* Retenido: la logica del paso no se ejecuta, pero el bloque sigue vivo
       * y su estado sigue siendo RUNNING. */
      if (retenido) return false;

      /* --- 7. Indicador de primer scan ----------------------------------
       * Tiene que seguir en pie DESPUES de que el bloque hijo haya ejecutado
       * la logica de su paso, porque es ahi donde se consulta con
       * isFirstScanInStep(). Por eso no se apaga aqui sin mas: se anota que ya
       * ha corrido un scan de logica, y se apaga en el siguiente. */
      if (active) {
        if (_firstScanSeen) _firstScan = false;
        else                _firstScanSeen = true;
      }

      return active;
    }

    /* -----------------------------------------------------------------------
     *  DIAGNOSTICO
     * -------------------------------------------------------------------- */
    void describe(Print& out) const override {
      BlockBase::describe(out);
      out.print(CFSM_FSTR(" paso="));
      out.print(SC.step);
      const __FlashStringHelper* n = stepName(SC.step);
      if (n) { out.print('('); out.print(n); out.print(')'); }
      out.print(CFSM_FSTR(" t_paso="));
      out.print(getTimeInStep());
      out.print(CFSM_FSTR("ms ciclos="));
      out.print(SC.cycleCount);
      if (ST.errorCode != CFSM_ERR_NONE) {
        out.print(CFSM_FSTR(" ERR=0x"));
        out.print(ST.errorCode, HEX);
        out.print(' ');
        out.print(cfsmErrorText(ST.errorCode));
      }
    }

    /* Nombre legible de un paso. Sobrescribelo en tu bloque para que la
     * telemetria diga "PASO_SOLDAR" en vez de "20":
     *
     *      const __FlashStringHelper* stepName(uint16_t s) const override {
     *        switch (s) {
     *          case PASO_SOLDAR: return F("SOLDAR");
     *          ...
     *        }
     *        return nullptr;
     *      }
     */
    virtual const __FlashStringHelper* stepName(uint16_t step) const {
      CFSM_UNUSED(step);
      return nullptr;
    }

  protected:
    /* Alias de lectura/escritura de SC.step. Ver nota en el constructor. */
    uint16_t& _currentStep;

    /* -----------------------------------------------------------------------
     *  CAMBIO DE PASO
     *  Reinicia el cronometro del paso, fija la vigilancia de tiempo y avisa
     *  al bloque hijo por los hooks de salida y entrada.
     *
     *  timeoutMs = 0 desactiva la vigilancia para ese paso. Usalo en los pasos
     *  que dependen de una accion humana (esperar a que el operario pulse) y
     *  ponlo SIEMPRE en los que dependen de un movimiento mecanico: si el
     *  cilindro no llega en X segundos es que algo va mal, y es mucho mejor
     *  que la maquina se pare y lo diga a que se quede colgada en silencio.
     * -------------------------------------------------------------------- */
    void setStep(uint16_t newStep, cfsm_time_t timeoutMs = 0) {
      if (SC.step != newStep) onStepExited(SC.step);
      SC.lastStep      = SC.step;
      SC.step          = newStep;
      SC.stepStartTime = cfsm_millis();
      SC.stepTimeout   = timeoutMs;
      ST.stw.stepTimeout = false;
      ST.stw.done      = false;   /* el ciclo vuelve a estar en marcha       */
      _stepAuthorised  = false;   /* en modo paso a paso hay que reautorizar */
      _firstScan       = true;
      _firstScanSeen   = false;
      onStepEntered(newStep);
    }

    /* Repite el paso actual desde cero (reinicia su cronometro). Util para
     * reintentar una operacion sin salir del paso. */
    void restartStep() { setStep(SC.step, SC.stepTimeout); }

    /* Ha vencido el tiempo maximo del paso actual? */
    bool isStepTimedOut() const {
      if (SC.stepTimeout == 0) return false;
      return getTimeInStep() >= SC.stepTimeout;
    }

    /* -----------------------------------------------------------------------
     *  CIERRE DE CICLO
     *  Llamalo en el ultimo paso de la secuencia. Contabiliza la pieza, mide
     *  la duracion del ciclo y decide si se encadena otro o se vuelve a
     *  reposo, segun haya o no una peticion de parada pendiente.
     * -------------------------------------------------------------------- */
    void completeCycle() {
      SC.cycleCount++;
      SC.lastCycleTimeMs = getCycleTime();
      SC.cycleStartTime  = cfsm_millis();

      if (ST.cfgw.stop) {
        ST.cfgw.stop = false;
        stop();
      } else {
        setStep(SC.initialStep);
      }

      /* El bit 'done' se levanta DESPUES del cambio de paso, porque setStep()
       * lo borra. Asi queda a 1 mientras la maquina descansa en su paso
       * inicial y cae en cuanto vuelve a trabajar: el maestro de linea puede
       * distinguir "ciclo recien terminado" de "ciclo en curso", que es
       * justamente para lo que sirve. */
      ST.stw.done = true;
    }

    /* -----------------------------------------------------------------------
     *  HOOKS DEL BLOQUE HIJO
     * -------------------------------------------------------------------- */

    /* Se ejecuta UNA vez al entrar a un paso. Es el sitio correcto para
     * imprimir por serie: dentro del switch, un Serial.println se ejecutaria
     * miles de veces por segundo y saturaria el buffer de transmision hasta
     * bloquear la CPU. Este es el error que mas veces se comete al empezar. */
    virtual void onStepEntered(uint16_t step) { CFSM_UNUSED(step); }

    /* Se ejecuta UNA vez al abandonar un paso. Util para apagar de forma
     * fiable lo que ese paso encendio, sin tener que acordarse de hacerlo en
     * cada una de las transiciones de salida. */
    virtual void onStepExited(uint16_t step) { CFSM_UNUSED(step); }

  private:
    bool _stepAuthorised = false;   /* permiso concedido en modo paso a paso  */
    bool _firstScan      = true;    /* primer ciclo dentro del paso actual    */
    bool _firstScanSeen  = false;   /* ya corrio un scan de logica en el paso */
    bool _frozen         = false;   /* relojes detenidos (pausa o retencion)  */
    bool _lastHoldReq    = false;   /* nivel anterior de holdRequest          */
    cfsm_time_t _freezeStart = 0;

    /* Traduce los bits de la palabra de mando a comandos y los consume.
     * Consumir el bit (ponerlo a false) es lo que convierte una senal
     * mantenida en un pulso de un solo ciclo: sin eso, mantener el bit start
     * a true rearrancaria la maquina indefinidamente. */
    void processControlWord() {
      if (ST.cfgw.abortRequest) { ST.cfgw.abortRequest = false; abort(CFSM_ERR_ESTOP); return; }
      if (ST.cfgw.resetFault)   { ST.cfgw.resetFault   = false; reset(); }

      /* El bit de marcha se consume SIEMPRE, tambien cuando no procede. Si se
       * dejara enganchado a la espera de la habilitacion, la maquina
       * arrancaria sola en el instante en que alguien girase el selector a
       * automatico, sin que nadie hubiera vuelto a pedirlo. Una orden vieja no
       * puede sobrevivir a la condicion que la impidio. */
      if (ST.cfgw.start) {
        ST.cfgw.start = false;
        if (ST.cfgw.enable) start();
      }

      if (ST.cfgw.nextStep) { ST.cfgw.nextStep = false; _stepAuthorised = true; }

      /* holdRequest es un NIVEL, no un pulso: mientras este a true la maquina
       * permanece en pausa. Pero la reanudacion se hace solo en su FLANCO DE
       * BAJADA, no por el simple hecho de que el bit valga cero.
       *
       * Si se reanudara por nivel, cualquier pausa pedida por la via directa
       * -bloque.hold(), manager.holdAll(), la tecla 'p' de la consola- se
       * desharia sola al scan siguiente, porque ese bit nunca llego a subir.
       * El boton de pausa, sencillamente, no pausaria. */
      if (ST.cfgw.holdRequest && _currentState == STATE_RUNNING) hold();
      if (!ST.cfgw.holdRequest && _lastHoldReq && _currentState == STATE_PAUSED) resume();
      _lastHoldReq = ST.cfgw.holdRequest;

      /* Perder la habilitacion en marcha equivale a una parada ordenada. */
      if (!ST.cfgw.enable && (_currentState == STATE_RUNNING ||
                              _currentState == STATE_STARTING)) {
        stop();
      }

      /* quickStop es activo a bajo: si cae, es parada inmediata. */
      if (!ST.cfgw.quickStop && _currentState != STATE_ERROR &&
                                _currentState != STATE_IDLE) {
        abort(CFSM_ERR_ESTOP);
      }
    }

    /* Refleja el estado interno en la palabra STW, que es lo que leen el HMI,
     * el bus y la telemetria. */
    void syncStatusWord() {
      ST.stw.ready      = (_currentState == STATE_IDLE ||
                           _currentState == STATE_STOPPED) && ST.cfgw.enable;
      ST.stw.running    = (_currentState == STATE_RUNNING);
      ST.stw.paused     = (_currentState == STATE_PAUSED);
      ST.stw.fault      = (_currentState == STATE_ERROR);
      ST.stw.busy       = (_currentState == STATE_RUNNING  ||
                           _currentState == STATE_STARTING ||
                           _currentState == STATE_STOPPING);
      ST.stw.waitingAck = handshake.statusDone && !handshake.cmdAck;
      /* 'done' no se toca aqui: lo levanta completeCycle() y lo borra
       * setStep() en cuanto la maquina vuelve a avanzar. */
    }
};

#endif /* COREFSM_SEQUENCE_BLOCK_H */
