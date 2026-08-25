#ifndef PROCESO_H
#define PROCESO_H

#include <CoreFSM.h>
#include "LedIndicator.h"

enum Pasos : uint16_t {
  PASO_REPOSO   = 0,
  PASO_TRABAJO  = 10
};

class Proceso : public SequenceBlock {
  public:
    /* Salida lógica de alto nivel */
    LedMode modoLed = LedMode::OFF;

    void begin() override {
      setName(F("PROCESO"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);
      setCycleTimeout(30000);
    }

    void update() override {
      if (!updateSequence()) { 
        modoLed = LedMode::OFF; 
        return; 
      }

      switch (_currentStep) {
        case PASO_REPOSO:
          modoLed = LedMode::BREATHING;
          if (getTimeInStep() >= 5000) {
            setStep(PASO_TRABAJO);
          }
          break;

        case PASO_TRABAJO:
          if (getTimeInStep() >= 5000) {
            setStep(PASO_REPOSO);
          }
          break;
      }
    }
};

#endif
