/* =============================================================================
 *  EJEMPLO 6  -  Robot de cuatro ruedas con evitacion de obstaculos
 * -----------------------------------------------------------------------------
 *  Es el mismo framework industrial de los ejemplos anteriores aplicado a un
 *  kit de coche robot. Lo que se gana frente al codigo tipico de estos kits:
 *
 *    - Ni un solo delay(). Mientras el robot gira sigue leyendo el sonar y el
 *      pulsador de paro. Con delay(), durante esos 400 ms esta ciego y sordo.
 *    - Enclavamiento del puente en H: es imposible que una orden contradictoria
 *      ponga las dos ramas en conduccion y queme el driver.
 *    - Rampa de arranque: sin ella, el pico de corriente hunde la bateria y el
 *      microcontrolador se reinicia. Es LA averia mas comun de estos kits.
 *    - Telemetria: cuando el robot hace algo raro, el monitor serie dice en
 *      que paso esta y que distancias midio. No hay que adivinar.
 *
 *  CABLEADO (Arduino Nano + dos drivers de doble canal, tipo L298N/TB6612)
 *      Rueda delantera izquierda  IN1=2  IN2=4  PWM=3
 *      Rueda delantera derecha    IN1=7  IN2=8  PWM=5
 *      Rueda trasera izquierda    IN1=12 IN2=13 PWM=6
 *      Rueda trasera derecha      IN1=A2 IN2=A3 PWM=9
 *      Sonar HC-SR04              TRIG=10  ECHO=11
 *      Pulsador marcha/paro       D:A0 -> GND
 *      LED de estado              A1
 *
 *  SI TU KIT LLEVA UN SOLO L298N (dos canales, no cuatro)
 *      Ese driver solo gobierna dos canales, asi que las dos ruedas de cada
 *      lado van conectadas en paralelo al mismo canal. En ese caso declara
 *      solo dos MotorDrive y usa DifferentialChassis en vez de
 *      FourWheelChassis. El CerebroRobot no cambia: le da igual, porque solo
 *      habla de avanzar y girar.
 *
 *  ANTES DE PONERLO EN EL SUELO
 *      1. Levanta el robot con las ruedas al aire y comprueba que forward()
 *         hace girar las cuatro hacia delante. Si alguna va al reves, invierte
 *         sus dos cables de motor (o cambia IN1 por IN2 en la declaracion).
 *      2. Alimenta los motores desde la bateria, NUNCA desde los 5 V del
 *         Arduino, y une las masas.
 *      3. Ajusta msGiro90 hasta que un giro sea de 90 grados reales. Depende
 *         del suelo, del peso y de la carga de la bateria.
 * ========================================================================== */

#include <CoreFSM.h>
#include "CerebroRobot.h"

/* --- NIVEL 1: los musculos ------------------------------------------------ */
MotorDrive ruedaFL(2,  4,  3);
MotorDrive ruedaFR(7,  8,  5);
MotorDrive ruedaRL(12, 13, 6);
MotorDrive ruedaRR(A2, A3, 9);

/* --- NIVEL 2: la geometria ------------------------------------------------ */
FourWheelChassis chasis(ruedaFL, ruedaFR, ruedaRL, ruedaRR);

/* --- Sensores y senalizacion ---------------------------------------------- */
UltrasonicSensor sonar(10, 11, 60, 12000);   /* 12000 us ~= 2 metros de alcance */
DigitalSensor    btnMarcha(A0, true, 30);
DigitalOutput    ledEstado(A1);

DeviceManager<7> io;

/* --- NIVEL 3: la estrategia ----------------------------------------------- */
BlockManager<2> manager;
CerebroRobot    cerebro(chasis);
StepTracer      tracer(cerebro, Serial);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== CoreFSM - Robot de 4 ruedas ==="));

  /* Los motores son dispositivos: se registran para que su fase PAA (donde
   * viven todos los enclavamientos) se ejecute sola en cada ciclo. */
  io.registerDevice(&ruedaFL,   F("FL"));
  io.registerDevice(&ruedaFR,   F("FR"));
  io.registerDevice(&ruedaRL,   F("RL"));
  io.registerDevice(&ruedaRR,   F("RR"));
  io.registerDevice(&sonar,     F("SONAR"));
  io.registerDevice(&btnMarcha, F("MARCHA"));
  io.registerDevice(&ledEstado, F("ESTADO"));
  io.beginAll();

  /* Rampa de 3 unidades PWM por milisegundo: de 0 a plena potencia en unos
   * 85 ms. Suficiente para que la bateria no se hunda y el Nano no se
   * reinicie, e imperceptible para el comportamiento del robot. */
  ruedaFL.setRamp(3); ruedaFR.setRamp(3);
  ruedaRL.setRamp(3); ruedaRR.setRamp(3);

  manager.registerBlock(&cerebro, F("CEREBRO"));
  manager.beginAll();

  cerebro.ST.cfgw.enable = true;
  cerebro.start();

  Serial.println(F("Pulsa el boton para arrancar. Vuelve a pulsarlo para parar."));
}

void loop() {
  /* --- 1. PAE: sonar, pulsador ------------------------------------------ */
  io.readAllInputs();

  cerebro.distanciaCm = sonar.cm();

  if (btnMarcha.hasRisen()) {
    if (cerebro.getStep() == ROB_PARADO) {
      cerebro.ordenMarcha = true;
      Serial.println(F(">> MARCHA"));
    } else {
      /* Paro inmediato: se vuelve al paso de reposo, que apaga el chasis. */
      cerebro.requestStop();
      Serial.println(F(">> PARO"));
    }
  }

  /* --- 2. Scan: el cerebro decide y llama al chasis ---------------------- */
  manager.updateAll();

  /* --- 3. Senalizacion --------------------------------------------------- */
  if (cerebro.isFaulted())              ledEstado.setMode(OUT_BLINK_FAST);
  else if (cerebro.getStep() == ROB_PARADO) ledEstado.setMode(OUT_BLINK_SLOW);
  else                                  ledEstado.setMode(OUT_ON);

  tracer.update();

  /* --- 4. PAA: los motores escriben sus pines con las protecciones ------ */
  io.writeAllOutputs();
}
