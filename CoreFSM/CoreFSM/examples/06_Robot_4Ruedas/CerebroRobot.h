#ifndef CEREBRO_ROBOT_H
#define CEREBRO_ROBOT_H

#include <CoreFSM.h>

/* ===========================================================================
 *  CerebroRobot.h  -  Nivel 3 de la jerarquia: la estrategia
 * ---------------------------------------------------------------------------
 *  Este bloque decide ADONDE ir. No sabe cuantos motores hay, ni en que pines
 *  estan, ni como se reparte la potencia entre las ruedas para trazar una
 *  curva. Todo eso lo resuelve el nivel 2 (FourWheelChassis) y el nivel 1
 *  (MotorDrive).
 *
 *  El resultado practico: el dia que cambies las ruedas por orugas, o pases de
 *  cuatro motores a dos, este archivo no se toca.
 *
 *  ESTRATEGIA DE NAVEGACION
 *  ------------------------
 *  Es la que usan los robots de exploracion sencillos, y funciona bien:
 *
 *      EXPLORAR ── obstaculo ──> FRENAR ──> MIRAR_IZQ ──> MIRAR_DER
 *          ^                                                  |
 *          |                                                  v
 *          └──── GIRAR (hacia el lado mas despejado) <──── DECIDIR
 *                                                             |
 *                                        ambos lados cerrados |
 *                                                             v
 *                                                          ESCAPAR
 *
 *  Sin servo para orientar el sensor, el "mirar" se hace pivotando el propio
 *  robot: gira un poco, mide, gira al otro lado, mide, y compara. Es mas lento
 *  que mover solo el sensor, pero no necesita un servo y mide la distancia en
 *  la direccion real en la que el robot avanzaria, que es mas fiable.
 * ======================================================================== */

enum PasosRobot : uint16_t {
  ROB_PARADO       = 0,
  ROB_EXPLORAR     = 10,
  ROB_FRENAR       = 20,
  ROB_MIRAR_IZQ    = 30,
  ROB_MEDIR_IZQ    = 40,
  ROB_MIRAR_DER    = 50,
  ROB_MEDIR_DER    = 60,
  ROB_DECIDIR      = 70,
  ROB_GIRAR        = 80,
  ROB_ESCAPAR      = 90
};

class CerebroRobot : public SequenceBlock {
  public:
    CerebroRobot(FourWheelChassis& chasis) : _chasis(chasis) {}

    /* ---- ENTRADAS ---- */
    uint16_t distanciaCm = 999;   /* la alimenta el sonar desde el .ino */
    bool     ordenMarcha = false;

    /* ---- PARAMETROS ---- */
    uint16_t distanciaCritica = 25;   /* cm a los que se considera obstaculo */
    uint8_t  velocidadCrucero = 170;
    uint8_t  velocidadGiro    = 190;
    uint16_t msPivote         = 350;  /* cuanto pivota para mirar a un lado  */
    uint16_t msGiro90         = 420;  /* calibra esto con tu robot y bateria */

    void begin() override {
      setName(F("CEREBRO"));
      setInitialStep(ROB_PARADO);
      setStep(ROB_PARADO);
      _chasis.disable();
    }

    void update() override {
      /* Con la maquina parada o en fallo, el chasis se desactiva. En un robot
       * movil esto no es opcional: un fallo con los motores energizados es un
       * robot que sigue rodando sin control. */
      if (!updateSequence()) { _chasis.stop(); _chasis.disable(); return; }

      switch (_currentStep) {

        case ROB_PARADO:
          _chasis.stop();
          if (ordenMarcha) {
            ordenMarcha = false;
            _chasis.enable();
            setStep(ROB_EXPLORAR);
          }
          break;

        /* ---------------------------------------------------------------- */
        case ROB_EXPLORAR:
          _chasis.drive((int16_t)velocidadCrucero, 0, 0);
          /* La comprobacion del obstaculo se hace en CADA scan, no cada vez
           * que toca medir. Como el sonar mide cada 60 ms y el scan corre a
           * microsegundos, el robot reacciona en el mismo milisegundo en que
           * llega una medida nueva por debajo del umbral. */
          if (distanciaCm <= distanciaCritica) {
            _chasis.stop();
            setStep(ROB_FRENAR);
          }
          break;

        case ROB_FRENAR:
          _chasis.stop();
          /* Pausa para que el robot se detenga de verdad antes de medir. Medir
           * en movimiento da distancias que ya no son ciertas cuando se usan. */
          if (getTimeInStep() >= 250) setStep(ROB_MIRAR_IZQ);
          break;

        /* ---------------------------------------------------------------- */
        case ROB_MIRAR_IZQ:
          _chasis.spinLeft(velocidadGiro);
          if (getTimeInStep() >= msPivote) { _chasis.stop(); setStep(ROB_MEDIR_IZQ); }
          break;

        case ROB_MEDIR_IZQ:
          _chasis.stop();
          /* Se espera a que el sonar entregue una medida NUEVA tras haber
           * parado. 200 ms cubren de sobra su intervalo de 60 ms. */
          if (getTimeInStep() >= 200) {
            _distIzq = distanciaCm;
            setStep(ROB_MIRAR_DER);
          }
          break;

        case ROB_MIRAR_DER:
          /* El doble de tiempo: hay que deshacer el giro anterior y luego
           * girar lo mismo hacia el otro lado. */
          _chasis.spinRight(velocidadGiro);
          if (getTimeInStep() >= (cfsm_time_t)msPivote * 2) {
            _chasis.stop();
            setStep(ROB_MEDIR_DER);
          }
          break;

        case ROB_MEDIR_DER:
          _chasis.stop();
          if (getTimeInStep() >= 200) {
            _distDer = distanciaCm;
            setStep(ROB_DECIDIR);
          }
          break;

        /* ---------------------------------------------------------------- */
        case ROB_DECIDIR:
          if (_distIzq <= distanciaCritica && _distDer <= distanciaCritica) {
            setStep(ROB_ESCAPAR);           /* encajonado */
          } else {
            _giroDerecha = (_distDer >= _distIzq);
            setStep(ROB_GIRAR);
          }
          break;

        case ROB_GIRAR:
          /* Se parte mirando a la derecha (donde quedo tras ROB_MIRAR_DER),
           * asi que para ir a la izquierda hay que deshacer ese giro ademas
           * del giro nuevo. */
          if (_giroDerecha) _chasis.spinRight(velocidadGiro);
          else              _chasis.spinLeft(velocidadGiro);

          if (getTimeInStep() >= (_giroDerecha ? msGiro90 : (cfsm_time_t)msGiro90 + msPivote)) {
            _chasis.stop();
            completeCycle();
            setStep(ROB_EXPLORAR);
          }
          break;

        /* ---------------------------------------------------------------- */
        case ROB_ESCAPAR:
          /* Callejon sin salida: retroceder y darse media vuelta. */
          if (getTimeInStep() < 700) {
            _chasis.backward(velocidadCrucero);
          } else if (getTimeInStep() < 700 + (cfsm_time_t)msGiro90 * 2) {
            _chasis.spinRight(velocidadGiro);
          } else {
            _chasis.stop();
            setStep(ROB_EXPLORAR);
          }
          break;
      }
    }

    uint16_t ultimaDistIzq() const { return _distIzq; }
    uint16_t ultimaDistDer() const { return _distDer; }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case ROB_PARADO:    return F("PARADO");
        case ROB_EXPLORAR:  return F("EXPLORANDO");
        case ROB_FRENAR:    return F("FRENANDO");
        case ROB_MIRAR_IZQ: return F("PIVOTAR_IZQ");
        case ROB_MEDIR_IZQ: return F("MEDIR_IZQ");
        case ROB_MIRAR_DER: return F("PIVOTAR_DER");
        case ROB_MEDIR_DER: return F("MEDIR_DER");
        case ROB_DECIDIR:   return F("DECIDIENDO");
        case ROB_GIRAR:     return F("GIRANDO");
        case ROB_ESCAPAR:   return F("MANIOBRA_ESCAPE");
        default:            return nullptr;
      }
    }

  protected:
    void onStepEntered(uint16_t step) override {
      if (step == ROB_DECIDIR) {
        Serial.print(F("[RADAR] izq="));  Serial.print(_distIzq);
        Serial.print(F("cm der="));       Serial.print(_distDer);
        Serial.println(F("cm"));
      }
    }

  private:
    FourWheelChassis& _chasis;
    uint16_t _distIzq = 999;
    uint16_t _distDer = 999;
    bool     _giroDerecha = true;
};

#endif
