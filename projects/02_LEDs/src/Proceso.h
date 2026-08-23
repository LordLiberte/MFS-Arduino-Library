#ifndef PROCESO_H
#define PROCESO_H

#include <CoreFSM.h>

enum Pasos : uint16_t {
  Inizalizacion = 0,
  Waiting_CMD   = 10,
  ON_GreenLed   = 20,
  ON_RedLed     = 30
};

enum Alarmas : uint16_t {
  ALM_EJEMPLO = CFSM_ERR_USER_BASE + 1
};

class Proceso : public SequenceBlock {
  public:

    /* Variables internas */
    

    /* ---- ENTRADAS ---- */
    bool ordenMarcha = false;

    /* ---- SALIDAS ---- */
    bool GreenLed = false;
    bool RedLed   = false;

    /* ---- PARAMETROS DE PROCESO ---- */
    uint16_t tiempoTrabajoMs = 1500;

    void begin() override {
      setName(F("PROCESO"));
      setInitialStep(Inizalizacion);
      setStep(Inizalizacion);
      setCycleTimeout(100000);
    }

    void update() override {
      if (!updateSequence()) { GreenLed = false; RedLed = false; return; }

      switch (_currentStep) {

        case Inizalizacion:
          GreenLed = false;
          RedLed   = false;
          setStep(Waiting_CMD);
          break;

        case Waiting_CMD:
          GreenLed = false;
          RedLed   = false;
          if (ordenMarcha) {
            setStep(ON_GreenLed);
          }

          break;

        case ON_GreenLed:
          GreenLed = true;
          RedLed   = false;
          if (getTimeInStep() >= tiempoTrabajoMs) {
            setStep(ON_RedLed);
          }
          break;

        case ON_RedLed:
          GreenLed = false;
          RedLed   = true;
          if (getTimeInStep() >= tiempoTrabajoMs) {
            setStep(Waiting_CMD);
          }
          break;
      }
    }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case Inizalizacion: return F("INICIALIZACION");
        case Waiting_CMD:   return F("ESPERANDO_COMANDO");
        case ON_GreenLed:   return F("ENCENDIENDO_LED_VERDE");
        case ON_RedLed:     return F("ENCENDIENDO_LED_ROJO");
        default:            return nullptr;
      }
    }

  protected:
    void onStepEntered(uint16_t step) override {
      switch (step) {
        case Inizalizacion: Serial.println(F("[PROCESO] En inicializacion")); break;
        case Waiting_CMD:   Serial.println(F("[PROCESO] Esperando comando")); break;
        case ON_GreenLed:   Serial.println(F("[PROCESO] Encendiendo LED verde")); break;
        case ON_RedLed:     Serial.println(F("[PROCESO] Encendiendo LED rojo")); break;
      }
    }
};

#endif