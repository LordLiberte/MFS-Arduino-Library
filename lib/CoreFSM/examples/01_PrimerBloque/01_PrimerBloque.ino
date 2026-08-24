/* =============================================================================
 *  EJEMPLO 1  -  El bloque mas simple posible
 * -----------------------------------------------------------------------------
 *  Empieza por aqui. En 60 lineas utiles estan las tres ideas que sostienen
 *  toda la libreria; el resto son detalles.
 *
 *  IDEA 1: EL CICLO DE SCAN DE TRES FASES
 *      leer todas las entradas -> calcular -> escribir todas las salidas
 *    Siempre en ese orden, siempre las mismas tres lineas en el loop().
 *
 *  IDEA 2: NADA BLOQUEA
 *    No hay un solo delay(). En lugar de "espera 2 segundos", se dice "si han
 *    pasado 2 segundos desde que entre aqui, cambia de paso". La diferencia es
 *    que durante esos 2 segundos la maquina sigue actualizando entradas e
 *    interbloqueos logicos; con delay(), no.
 *
 *  IDEA 3: LA LOGICA NO TOCA PINES
 *    El bloque enciende y apaga VARIABLES. Quien las lleva al cobre es el
 *    .ino. Por eso el mismo bloque vale para un LED, un rele o una simulacion.
 *
 *  MONTAJE (Arduino Nano o Uno)
 *      D2  -> pulsador -> GND
 *      D13 -> LED (o el LED que ya lleva la placa)
 * ========================================================================== */

#include <CoreFSM.h>

/* ---------------------------------------------------------------------------
 *  Los objetos de campo. Cada uno sabe leerse o escribirse solo, con su
 *  antirrebote y sus flancos ya resueltos.
 * ------------------------------------------------------------------------ */
DigitalSensor pulsador(2, true, 25);   /* pin 2, conmuta a masa, 25 ms de filtro */
DigitalOutput piloto(13);              /* pin 13 */

DeviceManager<2> io;                   /* la imagen de proceso */

/* ===========================================================================
 *  El bloque de proceso: un parpadeo con arranque y parada ordenados.
 * ======================================================================== */
enum Pasos : uint16_t {
  PASO_APAGADO   = 0,
  PASO_ENCENDIDO = 10
};

class Parpadeo : public SequenceBlock {
  public:
    /* Entrada del bloque. */
    bool ordenArranque = false;

    /* Salida del bloque. Fijate: es un bool, no un pin. */
    bool luz = false;

    /* Parametro de proceso: cuanto dura cada mitad del parpadeo. */
    uint16_t periodoMs = 500;

    void begin() override {
      setName(F("PARPADEO"));
      setInitialStep(PASO_APAGADO);
      setStep(PASO_APAGADO);
    }

    void update() override {
      /* Si la maquina no esta en marcha, la salida se apaga y se sale.
       * Una sola linea que garantiza que parar de verdad para. */
      if (!updateSequence()) { luz = false; return; }

      switch (_currentStep) {
        case PASO_APAGADO:
          luz = false;
          if (getTimeInStep() >= periodoMs) setStep(PASO_ENCENDIDO);
          break;

        case PASO_ENCENDIDO:
          luz = true;
          if (getTimeInStep() >= periodoMs) {
            completeCycle(PASO_APAGADO); /* cuenta un parpadeo completo */
          }
          break;
      }
    }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      return (s == PASO_ENCENDIDO) ? F("ENCENDIDO") : F("APAGADO");
    }
};

Parpadeo        parpadeo;
BlockManager<2> manager;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }

  /* --- Hardware --- */
  io.registerDevice(&pulsador, F("PULSADOR"));
  io.registerDevice(&piloto,   F("PILOTO"));
  io.beginAll();

  /* --- Logica --- */
  manager.registerBlock(&parpadeo, F("PARPADEO"));
  manager.beginAll();

  /* La habilitacion ya viene puesta a true, asi que basta con arrancar. Se
   * deja escrita expresamente porque es lo que hay que bajar el dia que
   * gobiernes la maquina por bus: perderla equivale a una parada ordenada. */
  parpadeo.ST.cfgw.enable = true;
  parpadeo.start();

  Serial.println(F("Pulsa el boton para parar y volver a arrancar."));
}

void loop() {
  /* --- FASE 1: PAE --------------------------------------------------- */
  io.readAllInputs();

  /* --- FASE 2: logica ------------------------------------------------ */
  /* hasRisen() es el INSTANTE de la pulsacion, no el hecho de estar pulsado.
   * Con el nivel, el codigo de abajo se ejecutaria miles de veces mientras el
   * dedo siguiera encima. */
  if (pulsador.hasRisen()) {
    if (parpadeo.isRunning()) {
      parpadeo.stop();
      Serial.println(F("-> PARO"));
    } else {
      parpadeo.start();
      Serial.println(F("-> MARCHA"));
    }
  }

  manager.updateAll();

  /* Variable logica -> salida fisica. Es el unico punto donde se cruzan. */
  piloto.set(parpadeo.luz);

  /* --- FASE 3: PAA --------------------------------------------------- */
  io.writeAllOutputs();

  /* --- Diagnostico: solo cuando cambia el numero de ciclos ----------- */
  static uint32_t ultimo = 0;
  if (parpadeo.getCycleCount() != ultimo) {
    ultimo = parpadeo.getCycleCount();
    Serial.print(F("Parpadeos: "));
    Serial.println(ultimo);
  }
}
