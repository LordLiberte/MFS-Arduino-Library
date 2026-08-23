/* =============================================================================
 *  02_cinta
 * -----------------------------------------------------------------------------
 *  Esqueleto generado. Compila y funciona tal cual con un pulsador y un LED.
 *
 *  EL CICLO DE SCAN son las tres fases del final de este archivo, siempre en
 *  ese orden. Todo lo demas son bloques que se registran.
 *
 *  RECUERDA, que aqui no estamos en el IDE de Arduino:
 *    - hay que incluir <Arduino.h> a mano
 *    - hay que declarar los prototipos antes de usar las funciones
 * ========================================================================== */

#include <Arduino.h>
#include "HardwareConfig.h"     // tabla generada desde diagram.json
#include "Proceso.h"

/* Crea la instancia global HW con todos los objetos de la tabla. */
CFSM_DEFINE_HARDWARE

BlockManager<4> manager;
Proceso         proceso;

StepTracer            tracer(proceso, Serial);
MaintenanceConsole<4> consola(manager, Serial);

/* --- Prototipos ----------------------------------------------------------- */
void leerEntradas();
void escribirSalidas();

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  Serial.println(F("=== 02_cinta ==="));

  HW.begin();                                  // pines e imagen de proceso
  manager.registerBlock(&proceso, F("PROCESO"));
  manager.beginAll();
  proceso.start();

  Serial.println(F("Listo. Escribe '?' para ver los comandos."));
}

void loop() {
  HW.readInputs();        // FASE 1 - PAE: foto de todas las entradas
  leerEntradas();         //          planta -> interfaz del bloque
  manager.updateAll();    // FASE 2 - OB1: la logica calcula con esa foto
  escribirSalidas();      //          bloque -> planta
  tracer.update();
  consola.update();
  HW.writeOutputs();      // FASE 3 - PAA: volcado de todas las salidas
}

/* -----------------------------------------------------------------------------
 *  Conexion de las senales de planta a la interfaz del bloque.
 *
 *  hasRisen() pasa el FLANCO (el instante de pulsar); isTriggered() pasa el
 *  NIVEL (el hecho de estar activo). Ordenes con flanco, estados con nivel: si
 *  se confunden, mantener el dedo en el boton relanza el ciclo miles de veces
 *  por segundo.
 * -------------------------------------------------------------------------- */
void leerEntradas() {
  proceso.ordenMarcha = HW.Pulsador_Marcha.hasRisen();
}

void escribirSalidas() {
  HW.Piloto_Trabajo.set(proceso.salida);
}
