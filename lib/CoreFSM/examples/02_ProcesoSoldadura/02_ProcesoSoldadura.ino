/* =============================================================================
 *  EJEMPLO 2  -  Estacion de soldadura (el proyecto original, ya en CoreFSM)
 * -----------------------------------------------------------------------------
 *  MONTAJE (Arduino Nano, el mismo de tu Wokwi)
 *      D2  pulsador de marcha  -> GND
 *      D3  switch a la derecha -> GND   (final de carrera de TRABAJO)
 *      D4  switch a la izquierda -> GND (final de carrera de REPOSO)
 *      D5  pulsador de rearme  -> GND
 *      D12 LED azul  = motor del carro
 *      D13 LED verde = soldador
 *
 *  SECUENCIA
 *      Switch a la izquierda (reposo) -> pulsa marcha -> el motor avanza ->
 *      desliza el switch a la derecha -> suelda 2 s -> devuelve el switch a
 *      la izquierda -> ciclo completado.
 *
 *  PARA VER UNA ALARMA
 *      Pulsa marcha y NO toques el switch. A los 5 s salta el timeout y el
 *      monitor dice exactamente que fallo: "el carro no avanza".
 * ========================================================================== */

#include <CoreFSM.h>
#include "ProcesoSoldadura.h"

/* --- Objetos de campo ----------------------------------------------------- */
DigitalSensor btnMarcha(2, true, 25);
DigitalSensor fcTrabajo(3, true, 10);
DigitalSensor fcReposo(4,  true, 10);
DigitalSensor btnRearme(5, true, 25);
DigitalOutput ledMotor(12);
DigitalOutput ledSoldador(13);

DeviceManager<6> io;

/* --- Logica --------------------------------------------------------------- */
BlockManager<2>  manager;
ProcesoSoldadura estacion;

/* --- Diagnostico ---------------------------------------------------------- */
StepTracer      tracer(estacion, Serial);
AlarmManager<6> alarmas;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== CoreFSM - Estacion de soldadura ==="));

  io.registerDevice(&btnMarcha,   F("MARCHA"));
  io.registerDevice(&fcTrabajo,   F("FC_TRABAJO"));
  io.registerDevice(&fcReposo,    F("FC_REPOSO"));
  io.registerDevice(&btnRearme,   F("REARME"));
  io.registerDevice(&ledMotor,    F("MOTOR"));
  io.registerDevice(&ledSoldador, F("SOLDADOR"));
  io.beginAll();

  manager.registerBlock(&estacion, F("SOLDADURA"));
  manager.beginAll();

  /* Ajuste de los parametros de proceso. En una maquina de verdad esto
   * vendria de una receta o de la configuracion persistente, no del codigo. */
  estacion.tiempoSoldaduraMs = 2000;
  estacion.timeoutAvanceMs   = 5000;

  estacion.ST.cfgw.enable = true;
  estacion.start();
  Serial.println(F("Pon el switch en REPOSO (izquierda) y pulsa marcha."));
}

void loop() {
  /* 1. PAE */
  io.readAllInputs();

  /* 2. Planta -> interfaz del bloque */
  estacion.pulsadorMarcha      = btnMarcha.hasRisen();
  estacion.finDeCarreraTrabajo = fcTrabajo.isTriggered();
  estacion.finDeCarreraReposo  = fcReposo.isTriggered();

  if (btnRearme.hasRisen()) {
    estacion.reset();
    alarmas.ackAll();
    if (estacion.isIdle()) estacion.start();
  }

  /* 3. Scan */
  manager.updateAll();

  /* 4. Salidas y diagnostico */
  ledMotor.set(estacion.motorMarcha);

  /* El soldador parpadea rapido mientras suelda: se ve desde lejos que la
   * estacion esta en el paso critico. */
  ledSoldador.setMode(estacion.actuadorTrabajo ? OUT_BLINK_FAST : OUT_OFF);

  alarmas.raiseIf(estacion.getErrorCode() == ALM_CARRO_NO_AVANZA,
                  ALM_CARRO_NO_AVANZA, F("El carro no llega a trabajo"));
  alarmas.raiseIf(estacion.getErrorCode() == ALM_CARRO_NO_RETORNA,
                  ALM_CARRO_NO_RETORNA, F("El carro no vuelve a reposo"));
  alarmas.raiseIf(estacion.getErrorCode() == ALM_POSICION_IMPOSIBLE,
                  ALM_POSICION_IMPOSIBLE, F("Dos finales de carrera a la vez"), ALARM_CRITICAL);
  alarmas.raiseIf(estacion.hayAvisoNoEnReposo(),
                  CFSM_ERR_USER_BASE + 13, F("Carro fuera de reposo: no se admite marcha"), ALARM_WARNING);

  tracer.update();

  /* Volcado de alarmas bajo peticion: escribe 'a' en el monitor serie. */
  if (Serial.available() && Serial.read() == 'a') alarmas.printAll(Serial);

  /* 5. PAA */
  io.writeAllOutputs();
}
