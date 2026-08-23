/* =============================================================================
 *  EJEMPLO 8  -  Esperas, ritmo de produccion y watchdog de scan
 * -----------------------------------------------------------------------------
 *  Es el ejemplo de los tres relojes. Merece la pena tenerlos claros porque la
 *  palabra "ciclo" significa dos cosas distintas en automatizacion y se
 *  confunden constantemente:
 *
 *    1. CICLO DE SCAN        Una pasada del programa: leer, calcular, escribir.
 *                            Lo vigila ScanWatchdog. Es el watchdog del PLC,
 *                            el que en un S7 manda la CPU a STOP a los 150 ms.
 *
 *    2. CICLO DE PRODUCCION  De bote a bote. Lo vigila SequenceBlock, con dos
 *                            umbrales: setCycleTarget() avisa, setCycleTimeout()
 *                            para la maquina.
 *
 *    3. TIEMPO DE ESPERA     Lo que la maquina pasa sana pero sin producir. NO
 *                            es tiempo de ciclo y NO puede disparar alarmas.
 *                            Va a su propio contador, que es el que alimenta
 *                            el calculo de rendimiento de la linea.
 *
 *  MONTAJE (Arduino Nano)
 *      D2  pulsador de marcha    -> GND
 *      D3  detector de bote      -> GND   (simula la cinta de arriba)
 *      D4  nivel de deposito bajo-> GND
 *      D5  acuse de recarga      -> GND
 *      D9  LED rojo    (baliza)
 *      D10 LED ambar   (baliza)
 *      D11 LED verde   (baliza)
 *      D12 LED azul = valvula de llenado
 *
 *  QUE PROBAR, EN ESTE ORDEN
 *      a) Pulsa marcha y NO pongas bote. La baliza se queda AMBAR FIJO y el
 *         monitor dice SUSPENDED. Dejalo diez minutos si quieres: no salta
 *         ninguna alarma, porque esperar no es averiarse. Antes, a los 15 s
 *         habria caido en timeout de ciclo.
 *      b) Pon bote. Llena, expulsa, y el monitor te da los DOS tiempos por
 *         separado: el productivo y el esperado.
 *      c) Activa "deposito bajo" antes de que termine un ciclo. La baliza pasa
 *         a AMBAR INTERMITENTE (HELD): la maquina te reclama a ti. Pulsa acuse.
 *      d) Escribe 's' en el monitor para ver el tiempo de scan. Luego descomenta
 *         el delay(30) del final del loop y vuelve a mirar: veras el exceso
 *         contado, que es justo el fallo que de otro modo no da la cara.
 * ========================================================================== */

#include <CoreFSM.h>
#include "Dosificadora.h"

/* --- Objetos de campo ----------------------------------------------------- */
DigitalSensor btnMarcha(2, true, 25);
DigitalSensor sBote(3,     true, 10);
DigitalSensor sDeposito(4, true, 50);
DigitalSensor btnAcuse(5,  true, 25);
DigitalOutput ledRojo(9);
DigitalOutput ledAmbar(10);
DigitalOutput ledVerde(11);
DigitalOutput valvula(12);

DeviceManager<8> io;
TowerLight       baliza(ledRojo, ledAmbar, ledVerde);

/* --- Logica --------------------------------------------------------------- */
BlockManager<2> manager;
Dosificadora    maquina;

/* --- Diagnostico ---------------------------------------------------------- */
StepTracer   tracer(maquina, Serial);
ScanWatchdog scan(20);          /* 20 ms de scan maximo */

uint32_t ciclosVistos = 0;

/* Prototipo: el IDE de Arduino los genera solo, pero PlatformIO y el CI
 * compilan esto como C++ normal y ahi hacen falta a mano. */
void atenderConsola();

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== CoreFSM - Esperas y ritmo de produccion ==="));

  io.registerDevice(&btnMarcha, F("MARCHA"));
  io.registerDevice(&sBote,     F("BOTE"));
  io.registerDevice(&sDeposito, F("DEPOSITO"));
  io.registerDevice(&btnAcuse,  F("ACUSE"));
  io.registerDevice(&ledRojo,   F("ROJO"));
  io.registerDevice(&ledAmbar,  F("AMBAR"));
  io.registerDevice(&ledVerde,  F("VERDE"));
  io.registerDevice(&valvula,   F("VALVULA"));
  io.beginAll();

  manager.registerBlock(&maquina, F("DOSIFICADORA"));
  manager.beginAll();

  Serial.println(F("Pulsa marcha para arrancar (y otra vez para parar al fin de ciclo)."));
  Serial.println(F("Escribe 's' para el scan, 't' para los tiempos."));
}

void loop() {
  scan.begin();                 /* <-- marca de entrada del ciclo de scan */

  /* 1. PAE */
  io.readAllInputs();

  /* 2. Planta -> interfaz del bloque.
   *    Ordenes con FLANCO, estados con NIVEL. El bote presente y el deposito
   *    bajo son estados: mientras la condicion dure, la maquina debe verla. */
  /* Marcha y paro NO son pasos de la secuencia: son comandos al bloque. Un
   * segundo toque en marcha pide el paro, que se hara efectivo al terminar el
   * bote en curso, nunca a mitad. */
  if (btnMarcha.hasRisen()) {
    if (maquina.isIdle() || maquina.getState() == STATE_STOPPED) maquina.start();
    else                                                          maquina.ST.cfgw.stop = true;
  }
  maquina.botePresente = sBote.isTriggered();
  maquina.depositoBajo = sDeposito.isTriggered();
  maquina.acuseRecarga = btnAcuse.hasRisen();

  /* 3. OB1 */
  manager.updateAll();

  /* 4. Bloque -> planta */
  valvula.set(maquina.valvula);

  /* La baliza sale de una sola linea y ahora distingue los cinco casos: rojo
   * averia, ambar fijo esperando material, ambar intermitente reclamando al
   * operario, verde produciendo, verde intermitente arrancando. */
  baliza.reflect(maquina.getState(), false, maquina.ST.stw.warning);

  /* Al cerrar cada bote, los dos tiempos por separado. Esto es, literalmente,
   * el dato del que sale la disponibilidad de un OEE. */
  if (maquina.getCycleCount() != ciclosVistos) {
    ciclosVistos = maquina.getCycleCount();
    Serial.print(F("[CICLO ")); Serial.print(ciclosVistos);
    Serial.print(F("] productivo=")); Serial.print(maquina.getLastCycleTime());
    Serial.print(F("ms  esperando=")); Serial.print(maquina.getLastBlockedTime());
    Serial.print(F("ms  ritmo="));
    Serial.print(maquina.getLastCycleTime() + maquina.getLastBlockedTime());
    Serial.println(F("ms"));
    if (maquina.isOverTakt()) Serial.println(F("   (por encima del takt objetivo)"));
  }

  tracer.update();
  atenderConsola();

  /* 5. PAA */
  io.writeAllOutputs();

  /* Descomenta esta linea para ver como se comporta el watchdog de scan
   * cuando alguien mete un delay() donde no debe. Es EL error clasico. */
  // delay(30);

  scan.end();                   /* <-- marca de salida del ciclo de scan */
}

void atenderConsola() {
  if (!Serial.available()) return;
  char c = (char)Serial.read();
  if (c == 's') {
    scan.report(Serial);
    Serial.print(F("   margen sobre el limite: "));
    Serial.print(scan.headroomPct());
    Serial.println('%');
  } else if (c == 't') {
    Serial.print(F("ciclo productivo en curso=")); Serial.print(maquina.getCycleTime());
    Serial.print(F("ms  esperando="));             Serial.print(maquina.getBlockedTime());
    Serial.print(F("ms  reloj de pared="));        Serial.print(maquina.getTotalCycleTime());
    Serial.print(F("ms  estado="));
    Serial.println(cfsmStateName(maquina.getState()));
  }
}
