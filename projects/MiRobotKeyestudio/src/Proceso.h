#ifndef PROCESO_H
#define PROCESO_H

#include <CoreFSM.h>

/* =============================================================================
 *  La logica del proceso.
 *
 *  Este archivo NO sabe nada de pines, ni de tensiones, ni de que placa hay
 *  debajo: trabaja solo con variables logicas. Podrias compilarlo tal cual para
 *  otro microcontrolador, o probarlo en el PC sin hardware.
 *
 *  Quien conecta estas variables al mundo real es main.cpp.
 * ========================================================================== */

/* Numeracion de 10 en 10: deja hueco para intercalar un paso 15 el dia que
 * haga falta, sin renumerar toda la secuencia. */
enum Pasos : uint16_t {
  PASO_REPOSO   = 0,
  PASO_TRABAJO  = 10
};

/* Codigos de alarma propios de esta maquina. Empiezan en CFSM_ERR_USER_BASE
 * para no chocar nunca con los que genera la libreria. */
enum Alarmas : uint16_t {
  ALM_EJEMPLO = CFSM_ERR_USER_BASE + 1
};

class Proceso : public SequenceBlock {
  public:
    /* ---- ENTRADAS (las alimenta main.cpp) ---- */

    /* ---- SALIDAS (las lee main.cpp) ---- */
    bool LedMiniPCB = false;

    /* ---- PARAMETROS DE PROCESO ---- */
    uint16_t tiempoTrabajoMs = 1500;

    void begin() override {
      setName(F("PROCESO"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);
      setCycleTimeout(30000);      /* el ciclo entero no debe pasar de 30 s */
    }

    void update() override {
      /* Enclavamiento general. Si la maquina no esta en marcha -parada, en
       * pausa, arrancando o en fallo-, las salidas peligrosas van a su estado
       * seguro y se sale. Es LA linea mas importante del bloque: sin ella, una
       * alarma no pararia el actuador, solo dejaria de cambiar de paso. */
      if (!updateSequence()) { LedMiniPCB = false; return; }

      switch (_currentStep) {

        case PASO_REPOSO:
          LedMiniPCB = false;
          if (getTimeInStep() >= 5000) {  /* 5 s de vigilancia */
            setStep(PASO_TRABAJO, 5000);
          }
          break;

        case PASO_TRABAJO:
          LedMiniPCB = true;
          if (getTimeInStep() >= tiempoTrabajoMs) {
            completeCycle(PASO_REPOSO); /* cuenta, cierra y cambia de paso */
          }
          break;
      }
    }

    /* Nombres de paso, para que la telemetria diga TRABAJO y no 10. */
    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case PASO_REPOSO:  return F("REPOSO");
        case PASO_TRABAJO: return F("TRABAJO");
        default:           return nullptr;
      }
    }

  protected:
    /* Los mensajes van AQUI, no dentro del switch: esto corre una sola vez por
     * cambio de paso. Dentro del switch correria miles de veces por segundo y
     * saturaria el buffer serie hasta bloquear la CPU. Es el error que mas
     * veces se comete al empezar. */
    void onStepEntered(uint16_t step) override {
      switch (step) {
        case PASO_REPOSO:  Serial.println(F("[PROCESO] En reposo")); break;
        case PASO_TRABAJO: Serial.println(F("[PROCESO] Trabajando")); break;
      }
    }
};

#endif
