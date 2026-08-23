#ifndef ESTACIONES_H
#define ESTACIONES_H

#include <CoreFSM.h>

/* ===========================================================================
 *  Dos estaciones que se pasan una pieza con handshake de cuatro fases
 * ---------------------------------------------------------------------------
 *  El problema que resuelve el handshake, con un ejemplo concreto:
 *
 *  Sin protocolo, la estacion A pondria "piezaLista = true" y la B la
 *  recogeria cuando lo viera. Funciona hasta el dia en que B esta en fallo:
 *  A pone el flag, nadie lo lee, A sigue produciendo y las piezas se
 *  amontonan. O peor: A baja el flag antes de que B lo vea, y B se queda
 *  esperando eternamente una pieza que ya paso.
 *
 *  Con el protocolo de cuatro fases, NADIE avanza hasta que el otro confirma.
 *  A no vuelve a reposo hasta ver el acuse de B; B no baja el acuse hasta ver
 *  que A bajo el aviso. Ninguna de las dos puede adelantarse.
 *
 *  Y cuando la linea se para, los cuatro bits dicen quien espera a quien:
 *      done=1 ack=0  -> A entrego, B no lo coge. El problema esta en B.
 *      busy=1 mucho  -> B se quedo colgada en su ciclo.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  ESTACION 1 (aguas arriba): carga una pieza y la entrega
 * ----------------------------------------------------------------------- */
enum PasosCarga : uint16_t {
  CARGA_REPOSO   = 0,
  CARGA_TRABAJO  = 10,
  CARGA_ENTREGAR = 20
};

class EstacionCarga : public SequenceBlock {
  public:
    bool ordenMarcha = false;    /* pulso de arranque */
    bool cargando    = false;    /* salida: piloto de trabajo */

    uint16_t tiempoCargaMs = 1500;

    void begin() override {
      setName(F("CARGA"));
      setInitialStep(CARGA_REPOSO);
      setStep(CARGA_REPOSO);
      handshake.reset();
    }

    void update() override {
      if (!updateSequence()) { cargando = false; return; }

      switch (_currentStep) {

        case CARGA_REPOSO:
          cargando = false;
          /* Solo se admite marcha si el traspaso anterior quedo cerrado. */
          if (ordenMarcha && handshake.isReady()) {
            ordenMarcha = false;
            handshake.statusBusy = true;
            setStep(CARGA_TRABAJO, tiempoCargaMs + 2000);
          }
          break;

        case CARGA_TRABAJO:
          cargando = true;
          if (getTimeInStep() >= tiempoCargaMs) {
            cargando = false;
            /* Fase 1 del handshake: "he terminado, la pieza es tuya".
             * El payload viaja con el testigo: aqui, el numero de pieza. */
            handshake.announceDone((uint16_t)(getCycleCount() + 1));
            setStep(CARGA_ENTREGAR, 15000);   /* 15 s para que la recojan */
          }
          break;

        case CARGA_ENTREGAR:
          /* Fase 3: espera el acuse. Si no llega en 15 s, la estacion de
           * abajo esta parada o en fallo: se declara alarma en vez de
           * quedarse esperando en silencio para siempre. */
          if (handshake.isAcknowledged()) {
            handshake.clearDone();      /* baja el aviso: cierra el traspaso */
            completeCycle();
            setStep(CARGA_REPOSO);
          }
          break;
      }
    }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case CARGA_REPOSO:   return F("REPOSO");
        case CARGA_TRABAJO:  return F("CARGANDO");
        case CARGA_ENTREGAR: return F("ESPERANDO_ACUSE");
        default:             return nullptr;
      }
    }

  protected:
    void onTransition(SystemState from, SystemState to) override {
      (void)from;
      if (to == STATE_ERROR && ST.errorCode == CFSM_ERR_STEP_TIMEOUT
          && getLastStep() == CARGA_ENTREGAR) {
        ST.errorCode = CFSM_ERR_HANDSHAKE;   /* la de abajo no responde */
      }
    }
};

/* --------------------------------------------------------------------------
 *  ESTACION 2 (aguas abajo): recoge la pieza, la inspecciona y la evacua
 * ----------------------------------------------------------------------- */
enum PasosInspeccion : uint16_t {
  INSP_ESPERANDO   = 0,
  INSP_INSPECCION  = 10,
  INSP_EVACUAR_OK  = 20,
  INSP_EVACUAR_NOK = 30
};

class EstacionInspeccion : public SequenceBlock {
  public:
    /* Referencia a la estacion de aguas arriba. Solo se toca su handshake:
     * la inspeccion no sabe ni le importa como funciona la carga por dentro.
     * Ese es todo el acoplamiento entre ambas: cuatro booleanos. */
    EstacionInspeccion(SequenceBlock& upstream) : _up(upstream) {}

    bool piezaBuena = true;      /* lo diria una camara o un calibre */
    bool cintaOk    = false;     /* salidas: por donde sale la pieza  */
    bool cintaNok   = false;

    uint16_t tiempoInspeccionMs = 1200;
    uint16_t piezasOk  = 0;
    uint16_t piezasNok = 0;

    void begin() override {
      setName(F("INSPECCION"));
      setInitialStep(INSP_ESPERANDO);
      setStep(INSP_ESPERANDO);
      handshake.reset();
    }

    void update() override {
      if (!updateSequence()) { cintaOk = cintaNok = false; return; }

      switch (_currentStep) {

        case INSP_ESPERANDO:
          cintaOk = cintaNok = false;
          /* Fase 2 del handshake: recoge el testigo. collect() devuelve true
           * una sola vez, cuando el traspaso queda completo por ambos lados. */
          if (HandshakeMaster::collect(_up.handshake)) {
            _idPieza = _up.handshake.payload;
            setStep(INSP_INSPECCION, tiempoInspeccionMs + 2000);
          }
          break;

        case INSP_INSPECCION:
          if (getTimeInStep() >= tiempoInspeccionMs) {
            setStep(piezaBuena ? INSP_EVACUAR_OK : INSP_EVACUAR_NOK, 4000);
          }
          break;

        case INSP_EVACUAR_OK:
          cintaOk = true;
          if (getTimeInStep() >= 800) {
            cintaOk = false;
            piezasOk++;
            completeCycle();
            setStep(INSP_ESPERANDO);
          }
          break;

        case INSP_EVACUAR_NOK:
          cintaNok = true;
          if (getTimeInStep() >= 800) {
            cintaNok = false;
            piezasNok++;
            completeCycle();
            setStep(INSP_ESPERANDO);
          }
          break;
      }
    }

    uint16_t idPiezaActual() const { return _idPieza; }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case INSP_ESPERANDO:   return F("ESPERANDO_PIEZA");
        case INSP_INSPECCION:  return F("INSPECCIONANDO");
        case INSP_EVACUAR_OK:  return F("EVACUAR_OK");
        case INSP_EVACUAR_NOK: return F("EVACUAR_RECHAZO");
        default:               return nullptr;
      }
    }

  private:
    SequenceBlock& _up;
    uint16_t _idPieza = 0;
};

#endif
