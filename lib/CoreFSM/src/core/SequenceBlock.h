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
      cfsm_time_t stepWarnTime    = 0;            /* 0 = sin aviso             */
      cfsm_time_t stepTimeout     = 0;            /* 0 = sin vigilancia        */
      cfsm_time_t cycleStartTime  = 0;            /* marca de inicio de ciclo  */
      cfsm_time_t cycleTimeout    = 0;            /* 0 = sin vigilancia (fallo)*/
      cfsm_time_t cycleTarget     = 0;            /* takt objetivo (solo aviso)*/
      cfsm_time_t lastCycleTimeMs = 0;            /* ciclo anterior, PRODUCTIVO*/
      cfsm_time_t blockedTime     = 0;            /* espera acumulada, ciclo en curso */
      cfsm_time_t lastBlockedTime = 0;            /* espera del ciclo anterior */
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
    /* ST.errorCode es la unica fuente de verdad del fallo en un bloque de
     * secuencia. Puedes reescribirlo desde onTransition() para traducir un
     * codigo generico de la libreria al codigo propio de tu maquina:
     *
     *      void onTransition(SystemState, SystemState to) override {
     *        if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT &&
     *            _currentStep == PASO_BAJAR) ST.errorCode = ALM_CILINDRO_ATASCADO;
     *      }
     *
     * "Timeout de paso" le dice al tecnico que algo tardo; "cilindro atascado"
     * le dice donde poner la mano. */
    uint16_t getErrorCode() const override { return ST.errorCode; }

    uint16_t    getStep() const        { return SC.step; }
    uint16_t    getLastStep() const    { return SC.lastStep; }
    /* Los dos cronometros descuentan el tramo de congelacion QUE ESTA EN
     * CURSO, no solo los ya cerrados. Sin esto, durante una pausa o una espera
     * el tiempo seguiria corriendo a ojos de quien lo consulta y solo se
     * corregiria al reanudar: el codigo de usuario que mira getTimeInStep()
     * dentro de un paso veria pasar el tiempo de una pausa que, por
     * definicion, no debe contar. */
    cfsm_time_t getTimeInStep() const  { return descontarCongelado(SC.stepStartTime); }

    /* -----------------------------------------------------------------------
     *  LOS DOS RELOJES DEL CICLO, QUE NO SON EL MISMO
     * -----------------------------------------------------------------------
     *  getCycleTime()   Tiempo PRODUCTIVO del ciclo en curso. NO cuenta lo que
     *                   la maquina pasa esperando (SUSPENDED / HELD), ni lo que
     *                   pasa en pausa, ni el tiempo de reposo en el paso
     *                   inicial. Es el que vigila setCycleTimeout().
     *
     *  getBlockedTime() Tiempo que la maquina ha pasado esperando en este
     *                   ciclo. Es el numerador de la disponibilidad en un OEE:
     *                   maquina sana que no produce porque le falta pieza.
     *
     *  getTotalCycleTime() La suma de los dos: el tiempo de reloj de pared, que
     *                   es la cadencia real con la que salen las piezas.
     *
     *  Separarlos es lo que permite poner un limite duro al ciclo sin que una
     *  espera legitima lo dispare. Un paso puede esperar lo que haga falta.
     * -------------------------------------------------------------------- */
    cfsm_time_t getCycleTime() const   { return descontarCongelado(SC.cycleStartTime); }
    cfsm_time_t getBlockedTime() const {
      cfsm_time_t v = SC.blockedTime;
      if (_frozen && _freezeWasWait) v += cfsm_elapsed(_freezeStart);
      return v;
    }
    cfsm_time_t getTotalCycleTime() const { return getCycleTime() + getBlockedTime(); }
    cfsm_time_t getLastCycleTime() const { return SC.lastCycleTimeMs; }
    cfsm_time_t getLastBlockedTime() const { return SC.lastBlockedTime; }
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
        SC.blockedTime    = 0;
        _cycleWarn        = false;
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

    /* -----------------------------------------------------------------------
     *  LIMITE DURO DE CICLO  ->  ALARMA
     * -----------------------------------------------------------------------
     *  Vigila el tiempo PRODUCTIVO del ciclo. Existe para cazar un fallo que
     *  la vigilancia de paso no puede ver: la secuencia que va rebotando entre
     *  dos pasos sin agotar ninguno pero sin terminar nunca. Cada setStep()
     *  reinicia el cronometro del paso, asi que ningun watchdog de paso salta;
     *  el del ciclo si.
     *
     *  NO cuenta el tiempo de espera declarado con suspendWhile()/holdWhile(),
     *  ni el reposo en el paso inicial. Puedes esperar lo que quieras.
     * -------------------------------------------------------------------- */
    void setCycleTimeout(cfsm_time_t ms) { SC.cycleTimeout = ms; }

    /* -----------------------------------------------------------------------
     *  TAKT OBJETIVO  ->  AVISO, NUNCA ALARMA
     * -----------------------------------------------------------------------
     *  Que un ciclo tarde de mas es un problema de PRODUCCION, no de seguridad.
     *  En una linea real eso no para la maquina: enciende un aviso, y el dato
     *  se va al calculo del rendimiento. Lo que si para la maquina es que un
     *  movimiento concreto no llegue, y de eso se encarga la vigilancia de
     *  paso. Por eso esto levanta stw.warning y no llama a fault().
     * -------------------------------------------------------------------- */
    void setCycleTarget(cfsm_time_t ms) { SC.cycleTarget = ms; }
    bool isOverTakt() const { return _cycleWarn; }

    /* -----------------------------------------------------------------------
     *  ESPERAS DECLARADAS
     * -----------------------------------------------------------------------
     *  Una maquina que espera no esta produciendo despacio: esta esperando. Son
     *  dos cosas distintas y la industria las separa desde ISA-88 / PackML,
     *  porque de esa separacion sale el calculo de rendimiento de la linea.
     *
     *  Mientras la espera esta declarada:
     *    - los cronometros de paso y de ciclo se CONGELAN, asi que ningun
     *      watchdog puede saltar por mucho que dure;
     *    - el tiempo se acumula en getBlockedTime(), aparte del de ciclo;
     *    - la STW dice suspended o held, de modo que el HMI, la baliza y el
     *      maestro de linea saben POR QUE la maquina no produce;
     *    - la logica del paso sigue corriendo, que es lo que permite volver.
     *
     *  Cual usar:
     *
     *    suspendWhile()  la causa es EXTERNA. No llega pieza, la estacion
     *                    siguiente esta llena, el almacen esta vacio. La
     *                    maquina esta sana y arrancara sola.
     *
     *    holdWhile()     la causa es INTERNA o del operario. Recarga de
     *                    material, control de calidad, ajuste, esperar a que
     *                    alguien pulse marcha.
     *
     *  Las dos devuelven true MIENTRAS hay que esperar, y el patron de uso es
     *  siempre el mismo:
     *
     *      case PASO_ESPERAR_PIEZA:
     *        cinta = false;
     *        if (suspendWhile(!piezaPresente)) break;   // sigue esperando
     *        setStep(PASO_COGER, 3000);                 // ya hay pieza
     *        break;
     *
     *  Si dejas de llamarlas -por ejemplo porque cambiaste de paso- la maquina
     *  vuelve sola a RUNNING al scan siguiente. No hay forma de quedarse
     *  colgado en una espera por olvido.
     * -------------------------------------------------------------------- */
    bool suspendWhile(bool condition) { return waitWhile(condition, STATE_SUSPENDED); }
    bool holdWhile(bool condition)    { return waitWhile(condition, STATE_HELD); }

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

      /* --- 1b. Liberacion automatica de la espera -----------------------
       * _waitRequested lo levanta suspendWhile()/holdWhile() durante la
       * logica del paso, es decir DESPUES de esta funcion. Lo que se mira
       * aqui es, por tanto, si en el scan ANTERIOR alguien pidio seguir
       * esperando. Si nadie lo pidio -porque la condicion se cumplio, porque
       * se cambio de paso, o simplemente porque se dejo de llamar-, la
       * maquina vuelve sola a produccion. Es la red de seguridad que impide
       * quedarse colgado en una espera por un olvido. */
      if (isWaiting() && !_waitRequested) transitionTo(STATE_RUNNING);
      _waitRequested = false;

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
      bool esperando = isWaiting();
      bool congelado = retenido || (_currentState == STATE_PAUSED) || esperando;
      if (congelado) {
        if (!_frozen) {
          _frozen = true; _freezeStart = cfsm_millis(); _freezeWasWait = esperando;
        } else if (_freezeWasWait != esperando) {
          /* Cambio el motivo de la congelacion a media parada (por ejemplo el
           * operario pulsa pausa mientras la maquina esperaba pieza). Se cierra
           * el tramo anterior con su etiqueta y se abre uno nuevo, para que el
           * tiempo de espera y el de pausa no se mezclen en el mismo saco. */
          closeFreezeChunk();
          _freezeStart = cfsm_millis(); _freezeWasWait = esperando;
        }
      } else if (_frozen) {
        closeFreezeChunk();
        _frozen = false;
      }

      /* --- 5. Vigilancias de tiempo -------------------------------------
       * En reposo, es decir parada en su paso inicial esperando orden, la
       * maquina no esta dentro de ningun ciclo: la vigilancia de ciclo no
       * aplica. Sin esta condicion, una maquina encendida y sin trabajo caeria
       * en alarma sola al cabo de setCycleTimeout(), que es exactamente lo
       * contrario de lo que debe pasar. */
      bool enReposo = (SC.step == SC.initialStep);

      if (active && !congelado && !ST.cfgw.bypassTimer) {
        /* Los dos cronometros se leen UNA vez. Cada lectura es una resta de 32
         * bits, que en un AVR de 8 bits no es gratis ni en tiempo ni en flash,
         * y aqui se consultan hasta cuatro veces seguidas. */
        const cfsm_time_t tPaso  = getTimeInStep();
        const cfsm_time_t tCiclo = getCycleTime();

        /* 5a. Aviso de paso: primer escalon. No para la maquina, solo avisa.
         *     Es el "va lento" antes del "no ha llegado". */
        if (SC.stepWarnTime > 0 && !_stepWarnFired && tPaso >= SC.stepWarnTime) {
          _stepWarnFired  = true;
          ST.stw.stepWarn = true;
          onStepWarning(SC.step);
        }

        /* 5b. Takt objetivo: tampoco para la maquina. */
        if (!enReposo && SC.cycleTarget > 0 && tCiclo >= SC.cycleTarget) {
          _cycleWarn = true;
        }

        /* 5c. Los dos limites duros, que si paran la maquina. */
        if (SC.stepTimeout > 0 && tPaso >= SC.stepTimeout) {
          ST.stw.stepTimeout = true;
          fault(CFSM_ERR_STEP_TIMEOUT);
          active = false;
        }
        else if (!enReposo && SC.cycleTimeout > 0 && tCiclo >= SC.cycleTimeout) {
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
      if (SC.blockedTime || _frozen) {
        out.print(CFSM_FSTR(" espera="));
        out.print(getBlockedTime());
        out.print(CFSM_FSTR("ms"));
      }
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
      setStep(newStep, 0, timeoutMs);
    }

    /* Version de DOS ESCALONES, que es como se vigila en la industria:
     *
     *      setStep(PASO_BAJAR, 3000, 5000);   // aviso a 3 s, fallo a 5 s
     *
     *  El primer escalon no para nada: enciende stw.stepWarn, llama a
     *  onStepWarning() y deja que la maquina siga. Sirve para ver venir la
     *  averia -la ventosa que cada vez tarda un poco mas, el cilindro que
     *  pierde aire- en vez de enterarte el dia que ya no llega. El segundo es
     *  el limite duro de siempre: alarma y maquina parada.
     *
     *  Poner 0 en cualquiera de los dos lo desactiva. */
    void setStep(uint16_t newStep, cfsm_time_t warnMs, cfsm_time_t faultMs) {
      /* Un cambio de paso puede venir de dentro de una espera -es el caso
       * normal: llega la pieza y la secuencia avanza-. Ahi la congelacion
       * sigue abierta, y hay que cerrarla ANTES de tocar las marcas. */
      endFreeze();

      /* El reloj de ciclo arranca al ABANDONAR el paso inicial, no al arrancar
       * la maquina. Mientras la secuencia descansa en reposo no hay ciclo que
       * medir, y lo que se mide de mas ahi acaba disparando una alarma que no
       * tiene ninguna causa fisica detras. */
      if (SC.step == SC.initialStep && newStep != SC.initialStep) {
        SC.cycleStartTime = cfsm_millis();
      }
      if (SC.step != newStep) onStepExited(SC.step);
      SC.lastStep      = SC.step;
      SC.step          = newStep;
      SC.stepStartTime = cfsm_millis();
      SC.stepWarnTime  = warnMs;
      SC.stepTimeout   = faultMs;
      ST.stw.stepTimeout = false;
      ST.stw.stepWarn    = false;
      _stepWarnFired   = false;
      ST.stw.done      = false;   /* el ciclo vuelve a estar en marcha       */
      _stepAuthorised  = false;   /* en modo paso a paso hay que reautorizar */
      _firstScan       = true;
      _firstScanSeen   = false;
      onStepEntered(newStep);
    }

    /* Repite el paso actual desde cero (reinicia su cronometro). Util para
     * reintentar una operacion sin salir del paso. */
    void restartStep() { setStep(SC.step, SC.stepWarnTime, SC.stepTimeout); }

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
      SC.lastCycleTimeMs  = getCycleTime();      /* solo tiempo productivo */
      SC.lastBlockedTime  = getBlockedTime();    /* y aparte, lo esperado  */
      SC.cycleStartTime   = cfsm_millis();
      SC.blockedTime      = 0;
      if (_frozen) _freezeStart = cfsm_millis();
      _cycleWarn          = false;

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

    /* Se ejecuta UNA vez cuando el paso se pasa de su tiempo de AVISO, si lo
     * tiene. La maquina sigue produciendo. Es el sitio donde encender el
     * ambar, apuntar la deriva o mandar el aviso al maestro de linea. */
    virtual void onStepWarning(uint16_t step) { CFSM_UNUSED(step); }

    /* Se ejecuta UNA vez al abandonar un paso. Util para apagar de forma
     * fiable lo que ese paso encendio, sin tener que acordarse de hacerlo en
     * cada una de las transiciones de salida. */
    virtual void onStepExited(uint16_t step) { CFSM_UNUSED(step); }

  private:
    bool _stepAuthorised = false;   /* permiso concedido en modo paso a paso  */
    bool _firstScan      = true;    /* primer ciclo dentro del paso actual    */
    bool _firstScanSeen  = false;   /* ya corrio un scan de logica en el paso */
    bool _frozen         = false;   /* relojes detenidos (pausa o retencion)  */
    bool _freezeWasWait  = false;   /* la congelacion en curso es una espera  */
    bool _waitRequested  = false;   /* alguien pidio esperar en este scan     */
    bool _stepWarnFired  = false;   /* el aviso del paso ya se lanzo          */
    bool _cycleWarn      = false;   /* el ciclo se paso del takt objetivo     */
    bool _lastHoldReq    = false;   /* nivel anterior de holdRequest          */
    cfsm_time_t _freezeStart = 0;

    /* Tiempo transcurrido desde una marca, sin contar la congelacion en curso. */
    cfsm_time_t descontarCongelado(cfsm_time_t desde) const {
      cfsm_time_t v = cfsm_elapsed(desde);
      if (_frozen) {
        cfsm_time_t parado = cfsm_elapsed(_freezeStart);
        v = (v > parado) ? (v - parado) : 0;
      }
      return v;
    }

    /* Cierra el tramo de congelacion en curso: desplaza las marcas de origen
     * de los cronometros hacia delante -de modo que el tiempo parado no cuente-
     * y, si el motivo era una espera, lo suma al contador de espera. */
    void closeFreezeChunk() {
      cfsm_time_t parado = cfsm_elapsed(_freezeStart);
      SC.stepStartTime  += parado;
      SC.cycleStartTime += parado;
      if (_freezeWasWait) SC.blockedTime += parado;
    }

    /* Termina la congelacion AHORA, contabilizandola. Hay que llamarlo antes
     * de tocar SC.stepStartTime o SC.cycleStartTime desde fuera del motor: si
     * no, closeFreezeChunk() sumaria despues el tramo entero sobre una marca
     * que ya se habia puesto a cero, y el cronometro se iria al futuro. Es
     * exactamente lo que pasa al hacer setStep() para salir de una espera. */
    void endFreeze() {
      if (!_frozen) return;
      closeFreezeChunk();
      _frozen = false;
    }

    /* Motor comun de suspendWhile() y holdWhile(). */
    bool waitWhile(bool condition, SystemState waitState) {
      if (condition) {
        _waitRequested = true;
        if (_currentState == STATE_RUNNING) transitionTo(waitState);
        return isWaiting();
      }
      if (_currentState == waitState) transitionTo(STATE_RUNNING);
      return false;
    }

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
      ST.stw.suspended  = (_currentState == STATE_SUSPENDED);
      ST.stw.held       = (_currentState == STATE_HELD);
      ST.stw.busy       = (_currentState == STATE_RUNNING  ||
                           _currentState == STATE_STARTING ||
                           _currentState == STATE_STOPPING ||
                           isWaiting());
      ST.stw.waitingAck = handshake.statusDone && !handshake.cmdAck;
      /* El aviso general es la union de los avisos concretos. Se recalcula
       * entero cada scan para que no se quede enganchado cuando su causa
       * desaparece: un bit de aviso que no baja solo deja de significar nada. */
      ST.stw.warning    = ST.stw.stepWarn || _cycleWarn;
      /* 'done' no se toca aqui: lo levanta completeCycle() y lo borra
       * setStep() en cuanto la maquina vuelve a avanzar. */
    }
};

#endif /* COREFSM_SEQUENCE_BLOCK_H */
