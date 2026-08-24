#ifndef PROCESO_SOLDADURA_H
#define PROCESO_SOLDADURA_H

#include <CoreFSM.h>

/* ===========================================================================
 *  ProcesoSoldadura.h  -  Estacion de soldadura con carro de avance
 * ---------------------------------------------------------------------------
 *  Es la misma maquina del proyecto original, reescrita con las herramientas
 *  de CoreFSM. Compara lo que cambia:
 *
 *    ANTES                              AHORA
 *    ----------------------------       ----------------------------------
 *    if (_currentState != RUNNING)      if (!updateSequence()) { ... }
 *      { salidas=false; return; }       (ademas gestiona pausa, mando,
 *    if (isStepTimedOut()) fault();      timeouts y palabra de estado)
 *
 *    pulsadorMarcha = digitalRead(..)   HW.Pulsador_Marcha.hasRisen()
 *    == LOW                             (con antirrebote y flanco)
 *
 *    pulsadorMarcha = false;            no hace falta: un flanco ya es un
 *    (consumir el pulso a mano)         pulso de un solo ciclo
 *
 *    fault();                           fault(ALM_CARRO_NO_AVANZA);
 *    (sin saber por que)                (el tecnico sabe que mirar)
 * ======================================================================== */

enum PasosSoldadura : uint16_t {
  PASO_INIT       = 0,
  PASO_AVANZAR    = 10,
  PASO_TRABAJAR   = 20,
  PASO_RETROCEDER = 30,
  PASO_FIN_CICLO  = 40
};

enum AlarmasSoldadura : uint16_t {
  ALM_CARRO_NO_AVANZA   = CFSM_ERR_USER_BASE + 10,
  ALM_CARRO_NO_RETORNA  = CFSM_ERR_USER_BASE + 11,
  ALM_POSICION_IMPOSIBLE= CFSM_ERR_USER_BASE + 12
};

class ProcesoSoldadura : public SequenceBlock {
  public:
    /* ---- ENTRADAS ---- */
    bool pulsadorMarcha      = false;   /* pulso                     */
    bool finDeCarreraTrabajo = false;   /* nivel: carro en posicion  */
    bool finDeCarreraReposo  = false;   /* nivel: carro replegado    */

    /* ---- SALIDAS ---- */
    bool motorMarcha     = false;
    bool actuadorTrabajo = false;

    /* ---- PARAMETROS DE PROCESO (una receta en miniatura) ---- */
    uint16_t tiempoSoldaduraMs = 2000;
    uint16_t timeoutAvanceMs   = 5000;
    uint16_t timeoutRetornoMs  = 5000;

    /* Interfaz de traspaso a la estacion siguiente. */
    /* (handshake ya viene de SequenceBlock) */

    void begin() override {
      setName(F("SOLDADURA"));
      setInitialStep(PASO_INIT);
      setStep(PASO_INIT);
      setCycleTimeout(30000);
    }

    void update() override {
      if (!updateSequence()) { salidasSeguras(); return; }

      /* ---------------------------------------------------------------------
       * COMPROBACION DE COHERENCIA DE SENALES
       * El carro no puede estar a la vez en reposo y en trabajo. Si ambos
       * sensores dan senal, uno esta averiado o mal cableado, y seguir seria
       * moverse a ciegas. Esta comprobacion cuesta dos lineas y evita
       * accidentes reales.
       * ------------------------------------------------------------------ */
      if (finDeCarreraTrabajo && finDeCarreraReposo) {
        fault(ALM_POSICION_IMPOSIBLE);
        salidasSeguras();
        return;
      }

      switch (_currentStep) {

        case PASO_INIT:
          motorMarcha     = false;
          actuadorTrabajo = false;

          if (pulsadorMarcha) {
            /* Interbloqueo: solo se arranca con el carro asegurado en reposo.
             * Si no lo esta, no es una averia; es una orden improcedente. */
            if (finDeCarreraReposo) {
              setStep(PASO_AVANZAR, timeoutAvanceMs);
            } else {
              _avisoNoEnReposo = true;
            }
          }
          break;

        case PASO_AVANZAR:
          motorMarcha      = true;
          _avisoNoEnReposo = false;
          if (finDeCarreraTrabajo) {
            motorMarcha = false;
            setStep(PASO_TRABAJAR, tiempoSoldaduraMs + 2000);
          }
          break;

        case PASO_TRABAJAR:
          actuadorTrabajo = true;
          if (getTimeInStep() >= tiempoSoldaduraMs) {
            actuadorTrabajo = false;
            setStep(PASO_RETROCEDER, timeoutRetornoMs);
          }
          break;

        case PASO_RETROCEDER:
          /* En este montaje el retorno es manual (deslizar el switch). En una
           * maquina real aqui iria motorRetroceso = true. */
          if (finDeCarreraReposo) setStep(PASO_FIN_CICLO);
          break;

        case PASO_FIN_CICLO:
          salidasSeguras();
          if (getTimeInStep() >= 300) {
            completeCycle(PASO_INIT);
          }
          break;
      }
    }

    bool hayAvisoNoEnReposo() const { return _avisoNoEnReposo; }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case PASO_INIT:       return F("ESPERA_MARCHA");
        case PASO_AVANZAR:    return F("AVANZANDO");
        case PASO_TRABAJAR:   return F("SOLDANDO");
        case PASO_RETROCEDER: return F("RETROCEDIENDO");
        case PASO_FIN_CICLO:  return F("FIN_CICLO");
        default:              return nullptr;
      }
    }

  protected:
    void onStepEntered(uint16_t step) override {
      switch (step) {
        case PASO_INIT:       Serial.println(F("[SOLD] Esperando marcha...")); break;
        case PASO_AVANZAR:    Serial.println(F("[SOLD] Avanzando carro"));     break;
        case PASO_TRABAJAR:   Serial.println(F("[SOLD] Soldando"));            break;
        case PASO_RETROCEDER: Serial.println(F("[SOLD] Retrocediendo"));       break;
        case PASO_FIN_CICLO:  Serial.println(F("[SOLD] Ciclo completado"));    break;
      }
    }

    /* Traduce el timeout generico a la causa concreta segun donde ocurrio. */
    void onTransition(SystemState from, SystemState to) override {
      (void)from;
      if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT) {
        if (getLastStep() == PASO_AVANZAR)         ST.errorCode = ALM_CARRO_NO_AVANZA;
        else if (getLastStep() == PASO_RETROCEDER) ST.errorCode = ALM_CARRO_NO_RETORNA;
      }
      if (to == STATE_ERROR) salidasSeguras();
    }

  private:
    bool _avisoNoEnReposo = false;

    void salidasSeguras() { motorMarcha = false; actuadorTrabajo = false; }
};

#endif
