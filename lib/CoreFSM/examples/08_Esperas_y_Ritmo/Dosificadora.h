#ifndef DOSIFICADORA_H
#define DOSIFICADORA_H

#include <CoreFSM.h>

/* =============================================================================
 *  La logica de una dosificadora, escrita con los dos conceptos nuevos:
 *  esperas declaradas y vigilancia de paso en dos escalones.
 *
 *  LA IDEA QUE HAY QUE LLEVARSE DE AQUI
 *  ------------------------------------
 *  Una maquina parada puede estarlo por tres motivos MUY distintos, y
 *  confundirlos es lo que hace que las alarmas dejen de significar nada:
 *
 *    1. Esta AVERIADA          -> alarma. Alguien tiene que venir. Rojo.
 *    2. Esta ESPERANDO         -> no es alarma. Esta sana y arrancara sola
 *                                 (o en cuanto el operario haga lo suyo). Ambar.
 *    3. Va LENTA pero produce  -> aviso. Aun no para nada. Ambar intermitente.
 *
 *  Antes, cualquier espera larga acababa en el saco 1 porque el watchdog de
 *  ciclo contaba el tiempo de espera como si fuera tiempo de trabajo.
 * ========================================================================== */

enum PasosDos : uint16_t {
  PASO_REPOSO      = 0,
  PASO_ESPERA_BOTE = 10,
  PASO_LLENAR      = 20,
  PASO_RECARGA     = 30,
  PASO_EXPULSAR    = 40
};

enum AlarmasDos : uint16_t {
  ALM_VALVULA_ATASCADA = CFSM_ERR_USER_BASE + 1
};

class Dosificadora : public SequenceBlock {
  public:
    /* ---- ENTRADAS ---- */
    bool botePresente   = false;   /* llega bote de la cinta de arriba       */
    bool depositoBajo   = false;   /* al deposito de producto le queda poco  */
    bool acuseRecarga   = false;   /* el operario confirma que ha recargado  */

    /* ---- SALIDAS ---- */
    bool valvula        = false;

    /* ---- PARAMETROS ---- */
    uint16_t tiempoLlenadoMs = 2000;
    uint16_t avisoLlenadoMs  = 2500;   /* va lento: la valvula pierde caudal */
    uint16_t falloLlenadoMs  = 4000;   /* no llena: valvula atascada         */

    /* ---- DIAGNOSTICO PROPIO ---- */
    uint16_t avisosLlenado = 0;

    void begin() override {
      setName(F("DOSIFICADORA"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);

      /* Limite duro del ciclo. Ahora vigila SOLO tiempo productivo, asi que
       * puede ser ajustado sin miedo a que una espera lo dispare. */
      setCycleTimeout(15000);

      /* Y el ritmo al que deberia salir cada bote. Pasarse de aqui no para
       * nada: enciende un aviso y se apunta. Es un dato de produccion. */
      setCycleTarget(6000);
    }

    void update() override {
      if (!updateSequence()) { valvula = false; return; }

      switch (_currentStep) {

        /* Paso de paso. En marcha, el ciclo encadena solo: la orden de
         * marcha y la de paro son start() y stop(), no un paso de la
         * secuencia. Asi completeCycle() puede atender una peticion de paro
         * pendiente y dejar la maquina parada justo al terminar la pieza en
         * curso, que es como para una maquina de verdad: nunca a media pieza. */
        case PASO_REPOSO:
          valvula = false;
          setStep(PASO_ESPERA_BOTE);
          break;

        /* --- ESPERA EXTERNA -------------------------------------------
         * No llega bote. La culpa es de la maquina de arriba, no nuestra.
         * La maquina esta sana y arrancara sola en cuanto llegue. En PackML
         * esto es SUSPENDED, y la baliza se pone en ambar fijo. */
        case PASO_ESPERA_BOTE:
          valvula = false;
          if (suspendWhile(!botePresente)) break;
          setStep(PASO_LLENAR, avisoLlenadoMs, falloLlenadoMs);
          break;

        /* --- PASO VIGILADO EN DOS ESCALONES ---------------------------
         * A los 2,5 s: aviso. La valvula esta perdiendo caudal, alguien
         * deberia mirarla en el proximo mantenimiento, pero seguimos.
         * A los 4 s: alarma. Ya no es deriva, es que no llena. */
        case PASO_LLENAR:
          valvula = true;
          if (getTimeInStep() >= tiempoLlenadoMs) {
            valvula = false;
            setStep(depositoBajo ? PASO_RECARGA : PASO_EXPULSAR);
          }
          break;

        /* --- ESPERA INTERNA -------------------------------------------
         * Al deposito le queda poco. La culpa es nuestra y la solucion la
         * tiene una persona. En PackML esto es HELD, y la baliza pasa a
         * ambar INTERMITENTE: la maquina te esta reclamando. */
        case PASO_RECARGA:
          valvula = false;
          if (holdWhile(!acuseRecarga)) break;
          setStep(PASO_EXPULSAR);
          break;

        case PASO_EXPULSAR:
          valvula = false;
          if (getTimeInStep() >= 500) completeCycle();
          break;
      }
    }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case PASO_REPOSO:      return F("REPOSO");
        case PASO_ESPERA_BOTE: return F("ESPERANDO_BOTE");
        case PASO_LLENAR:      return F("LLENANDO");
        case PASO_RECARGA:     return F("ESPERANDO_RECARGA");
        case PASO_EXPULSAR:    return F("EXPULSANDO");
        default:               return nullptr;
      }
    }

  protected:
    void onStepEntered(uint16_t step) override {
      if (step == PASO_LLENAR) Serial.println(F("[DOS] Llenando"));
    }

    /* El primer escalon de la vigilancia. La maquina NO se para aqui. */
    void onStepWarning(uint16_t step) override {
      if (step == PASO_LLENAR) {
        avisosLlenado++;
        Serial.print(F("[DOS] AVISO: el llenado va lento ("));
        Serial.print(getTimeInStep());
        Serial.println(F(" ms). Revisar la valvula."));
      }
    }

    /* El segundo escalon lo dispara solo la libreria, pero el codigo de error
     * lo elegimos nosotros traduciendo el generico a uno propio. */
    void onTransition(SystemState from, SystemState to) override {
      CFSM_UNUSED(from);
      if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT &&
          _currentStep == PASO_LLENAR) {
        ST.errorCode = ALM_VALVULA_ATASCADA;
      }
    }
};

#endif
