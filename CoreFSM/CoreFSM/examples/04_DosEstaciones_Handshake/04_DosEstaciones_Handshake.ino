/* =============================================================================
 *  EJEMPLO 4  -  Dos estaciones coordinadas con handshake
 * -----------------------------------------------------------------------------
 *  Aqui es donde se ve por que merecio la pena aislar la logica en bloques:
 *  anadir una segunda estacion no obliga a tocar ni una linea de la primera.
 *  Corren en paralelo dentro del mismo microcontrolador, sin hilos, sin
 *  interrupciones y sin bloquearse, porque ninguna de las dos usa delay().
 *
 *  MONTAJE (Arduino Nano)
 *      D2  pulsador de marcha        -> GND
 *      D3  interruptor "pieza NOK"   -> GND   (para forzar un rechazo)
 *      D5  pulsador de rearme        -> GND
 *      D10 LED = estacion 1 trabajando
 *      D11 LED = evacuacion OK
 *      D12 LED = evacuacion RECHAZO
 *      D13 LED = alarma
 *
 *  QUE OBSERVAR EN EL MONITOR SERIE
 *      Pulsa marcha varias veces seguidas. Veras que la estacion 1 NO
 *      rearranca hasta que la 2 le ha acusado el traspaso: la linea se
 *      autorregula sola, sin ningun contador ni temporizador que lo controle.
 *
 *      Pon el interruptor de NOK y veras la pieza salir por la via de rechazo.
 *
 *      Escribe 'w' para ver la tabla de observacion con los dos bloques y sus
 *      bits de handshake.
 * ========================================================================== */

#include <CoreFSM.h>
#include "Estaciones.h"

/* --- Campo ---------------------------------------------------------------- */
DigitalSensor btnMarcha(2, true, 25);
DigitalSensor swPiezaNok(3, true, 20);
DigitalSensor btnRearme(5, true, 25);
DigitalOutput ledCarga(10);
DigitalOutput ledOk(11);
DigitalOutput ledNok(12);
DigitalOutput ledAlarma(13);

DeviceManager<7> io;

/* --- Logica --------------------------------------------------------------- */
BlockManager<4>    manager;
EstacionCarga      carga;
EstacionInspeccion inspeccion(carga);     /* recibe la referencia a la anterior */

StepTracer            trCarga(carga, Serial);
StepTracer            trInsp(inspeccion, Serial);
MaintenanceConsole<4> consola(manager, Serial);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== CoreFSM - Linea de dos estaciones ==="));

  io.registerDevice(&btnMarcha,  F("MARCHA"));
  io.registerDevice(&swPiezaNok, F("PIEZA_NOK"));
  io.registerDevice(&btnRearme,  F("REARME"));
  io.registerDevice(&ledCarga,   F("L_CARGA"));
  io.registerDevice(&ledOk,      F("L_OK"));
  io.registerDevice(&ledNok,     F("L_NOK"));
  io.registerDevice(&ledAlarma,  F("L_ALARMA"));
  io.beginAll();

  /* El orden de registro es el orden de ejecucion dentro del scan. Registrar
   * la de aguas arriba primero hace que la de abajo vea sus flags en el MISMO
   * ciclo; al reves, los veria con un ciclo de retraso. Un ciclo son
   * microsegundos y aqui da igual, pero conviene saber que existe. */
  manager.registerBlock(&carga,      F("CARGA"));
  manager.registerBlock(&inspeccion, F("INSPECCION"));
  manager.beginAll();

  carga.ST.cfgw.enable      = true;
  inspeccion.ST.cfgw.enable = true;
  carga.start();
  inspeccion.start();

  Serial.println(F("Pulsa marcha. 'w'=tabla de observacion, '?'=ayuda."));
}

void loop() {
  io.readAllInputs();

  carga.ordenMarcha       = btnMarcha.hasRisen();
  inspeccion.piezaBuena   = !swPiezaNok.isTriggered();

  if (btnRearme.hasRisen()) {
    manager.resetAll();
    if (carga.isIdle())      carga.start();
    if (inspeccion.isIdle()) inspeccion.start();
    Serial.println(F(">> REARME"));
  }

  manager.updateAll();

  ledCarga.set(carga.cargando);
  ledOk.set(inspeccion.cintaOk);
  ledNok.set(inspeccion.cintaNok);
  ledAlarma.setMode(manager.hasAnyFault() ? OUT_BLINK_FAST : OUT_OFF);

  trCarga.update();
  trInsp.update();
  consola.update();

  /* Resumen de produccion cada 10 s: cadencia baja a proposito, para no
   * competir con las trazas de eventos por el ancho de banda del puerto. */
  static uint32_t ultimoResumen = 0;
  if (millis() - ultimoResumen >= 10000) {
    ultimoResumen = millis();
    Serial.print(F("[PRODUCCION] OK="));   Serial.print(inspeccion.piezasOk);
    Serial.print(F(" NOK="));              Serial.print(inspeccion.piezasNok);
    Serial.print(F(" ciclo_medio="));      Serial.print(carga.getLastCycleTime());
    Serial.println(F("ms"));
  }

  io.writeAllOutputs();
}
