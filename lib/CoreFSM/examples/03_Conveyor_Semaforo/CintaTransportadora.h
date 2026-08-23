#ifndef CINTA_TRANSPORTADORA_H
#define CINTA_TRANSPORTADORA_H

#include <CoreFSM.h>

/* ===========================================================================
 *  CintaTransportadora.h  -  La logica del proceso (el "que hace la maquina")
 * ---------------------------------------------------------------------------
 *  Este archivo NO sabe nada de pines, ni de tensiones, ni de que placa hay
 *  debajo. Trabaja solo con variables logicas. Podrias compilarlo tal cual
 *  para un Nano, un ESP32 o un simulador en el PC.
 *
 *  Quien conecta estas variables al mundo real es el .ino, que hace el papel
 *  de la Configuracion de Hardware de un automata.
 * ======================================================================== */

/* Numeracion de 10 en 10: deja hueco para intercalar pasos sin renumerar. */
enum PasosCinta : uint16_t {
  PASO_REPOSO       = 0,
  PASO_TRANSPORTAR  = 10,
  PASO_PIEZA_EN_FIN = 20,
  PASO_EVACUANDO    = 30
};

/* Codigos de alarma propios de esta maquina. Empiezan en CFSM_ERR_USER_BASE
 * para no chocar nunca con los que genera la libreria. */
enum AlarmasCinta : uint16_t {
  ALM_ATASCO        = CFSM_ERR_USER_BASE + 1,
  ALM_PIEZA_NO_SALE = CFSM_ERR_USER_BASE + 2
};

class CintaTransportadora : public SequenceBlock {
  public:
    /* ---- ENTRADAS (las alimenta el .ino desde la imagen de proceso) ---- */
    bool ordenMarcha   = false;   /* pulso: arranca un transporte           */
    bool ordenPausa    = false;   /* pulso: pausa / reanuda                 */
    bool finDeCinta    = false;   /* nivel: hay pieza en el extremo         */
    uint8_t consignaVelocidad = 200;  /* 0..255, viene del potenciometro    */

    /* ---- SALIDAS (las lee el .ino y las vuelca al hardware) ---- */
    bool motorMarcha   = false;
    uint8_t velocidadAplicada = 0;

    /* Cuantas piezas ha transportado. Se puede llevar a un DB remanente. */
    uint32_t piezasTransportadas = 0;

    void begin() override {
      setName(F("CINTA"));
      setInitialStep(PASO_REPOSO);
      setStep(PASO_REPOSO);
      setCycleTimeout(30000);   /* un transporte entero no debe pasar de 30 s */
      motorMarcha = false;
    }

    void update() override {
      /* -------------------------------------------------------------------
       * 1. ENCLAVAMIENTO GENERAL
       *    updateSequence() devuelve false si la maquina no esta en RUNNING:
       *    parada, en pausa, arrancando o en fallo. En todos esos casos las
       *    salidas peligrosas van a su estado seguro y se sale.
       *
       *    Esta es la linea mas importante de todo el bloque. Sin ella, una
       *    alarma no pararia el motor: solo dejaria de cambiar de paso, y el
       *    motor se quedaria girando con la maquina "parada".
       * ----------------------------------------------------------------- */
      if (!updateSequence()) {
        motorMarcha = false;
        velocidadAplicada = 0;
        return;
      }

      /* -------------------------------------------------------------------
       * 2. PAUSA A PETICION DEL OPERARIO
       *    hold() conserva el paso: al reanudar se sigue exactamente donde
       *    estaba, no se reinicia el ciclo.
       * ----------------------------------------------------------------- */
      if (ordenPausa) {
        ordenPausa = false;      /* consumir el pulso */
        hold();
        return;
      }

      /* -------------------------------------------------------------------
       * 3. SECUENCIA
       * ----------------------------------------------------------------- */
      switch (_currentStep) {

        case PASO_REPOSO:
          motorMarcha = false;
          velocidadAplicada = 0;

          /* Interbloqueo de arranque: no se admite marcha si ya hay una pieza
           * ocupando el extremo. Arrancar en esa situacion amontonaria piezas.
           * Un interbloqueo es una condicion que debe cumplirse ANTES de
           * permitir un movimiento, no una comprobacion posterior. */
          if (ordenMarcha) {
            ordenMarcha = false;
            if (finDeCinta) {
              /* No es una averia: es una orden que no procede. Se avisa y se
               * sigue en reposo. Parar la maquina por esto seria excesivo. */
              _avisoLineaOcupada = true;
            } else {
              setStep(PASO_TRANSPORTAR, 8000);  /* 8 s para llegar al final */
            }
          }
          break;

        case PASO_TRANSPORTAR:
          motorMarcha = true;
          velocidadAplicada = consignaVelocidad;
          _avisoLineaOcupada = false;

          /* Si la pieza no llega en 8 s, isStepTimedOut() dispara solo dentro
           * de updateSequence() y la maquina cae a fallo. No hay que
           * comprobarlo aqui: ya esta vigilado. */
          if (finDeCinta) {
            motorMarcha = false;
            velocidadAplicada = 0;
            piezasTransportadas++;
            setStep(PASO_PIEZA_EN_FIN);
          }
          break;

        case PASO_PIEZA_EN_FIN:
          motorMarcha = false;
          /* Espera a que alguien retire la pieza. Sin timeout a proposito:
           * depende de una persona, y castigar con una alarma a un operario
           * que tarda es una forma segura de que acabe puenteando el sensor. */
          if (!finDeCinta) setStep(PASO_EVACUANDO, 3000);
          break;

        case PASO_EVACUANDO:
          /* Pequena pausa de asentamiento antes de admitir el ciclo siguiente:
           * evita que un rebote del sensor cuente dos piezas. */
          if (getTimeInStep() >= 500) {
            completeCycle();          /* contabiliza y vuelve al paso inicial */
            setStep(PASO_REPOSO);
          }
          break;
      }
    }

    bool hayAvisoLineaOcupada() const { return _avisoLineaOcupada; }

    /* Nombres de paso para que la telemetria sea legible. */
    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case PASO_REPOSO:       return F("REPOSO");
        case PASO_TRANSPORTAR:  return F("TRANSPORTAR");
        case PASO_PIEZA_EN_FIN: return F("PIEZA_EN_FIN");
        case PASO_EVACUANDO:    return F("EVACUANDO");
        default:                return nullptr;
      }
    }

  protected:
    /* Mensajes SOLO aqui: se ejecuta una vez por cambio de paso.
     * Ponerlos dentro del switch saturaria el puerto serie y bloquearia la
     * CPU, porque el switch corre miles de veces por segundo. */
    void onStepEntered(uint16_t step) override {
      switch (step) {
        case PASO_REPOSO:       Serial.println(F("[CINTA] En reposo, esperando marcha")); break;
        case PASO_TRANSPORTAR:  Serial.println(F("[CINTA] Transportando pieza..."));      break;
        case PASO_PIEZA_EN_FIN: Serial.println(F("[CINTA] Pieza en el extremo, retirar")); break;
        case PASO_EVACUANDO:    Serial.println(F("[CINTA] Pieza retirada, cerrando ciclo")); break;
      }
    }

    /* Al entrar en fallo, traducimos el codigo generico de timeout a una causa
     * concreta y util para el tecnico: no es lo mismo que se atasque yendo que
     * que la pieza no salga. */
    void onTransition(SystemState from, SystemState to) override {
      (void)from;
      if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT) {
        if (getLastStep() == PASO_TRANSPORTAR) ST.errorCode = ALM_ATASCO;
        else                                   ST.errorCode = ALM_PIEZA_NO_SALE;
      }
    }

    /* No se admite rearme mientras siga habiendo una pieza atascada al final.
     * Esto es lo que impide el vicio de pulsar rearme veinte veces sin haber
     * resuelto nada. */
    bool canReset() const override { return !finDeCinta; }

  private:
    bool _avisoLineaOcupada = false;
};

#endif
