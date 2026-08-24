/* =============================================================================
 *  EJEMPLO 3  -  Cinta transportadora con baliza de tres colores
 *                y tabla de hardware generada desde Wokwi
 * -----------------------------------------------------------------------------
 *  QUE DEMUESTRA
 *    - La tabla de variables sale sola del diagram.json: no hay ni un solo
 *      numero de pin escrito en este archivo.
 *    - El loop() son tres fases fijas, como el ciclo de scan de un automata.
 *    - La baliza traduce el estado interno de la maquina a los tres colores
 *      normalizados sin una sola cadena de if.
 *
 *  MONTAJE EN WOKWI
 *    Pega el diagram.json de esta carpeta en la pestana diagram.json de tu
 *    proyecto. Los nombres de los componentes son los que se convierten en
 *    nombres de variable, asi que cambiarlos alli los cambia aqui.
 *
 *  COMO PROBARLO
 *    1. Play. La baliza se pone AMARILLA (en reposo, lista).
 *    2. Boton verde (D2): arranca. Baliza VERDE, el LED azul del motor se
 *       enciende.
 *    3. Desliza el switch (D4): llega la pieza. El motor para, la baliza
 *       vuelve a amarillo y el monitor pide retirarla.
 *    4. Devuelve el switch: se cierra el ciclo y el contador sube.
 *    5. Para provocar una alarma: arranca y NO toques el switch. A los 8 s
 *       salta el timeout, la baliza se pone ROJA y suena el aviso.
 *    6. Boton azul (D5): rearme.
 *    7. Escribe 'w' en el monitor serie: tabla de observacion completa.
 * ========================================================================== */

#include "HardwareConfig.h"        // tabla generada desde diagram.json
#include "CintaTransportadora.h"

/* Crea la instancia global HW con todos los objetos de la tabla. */
CFSM_DEFINE_HARDWARE;

/* --- Bloques de logica ---------------------------------------------------- */
BlockManager<4>     manager;
CintaTransportadora cinta;

/* --- Baliza: agrupa los tres pilotos y les aplica el codigo de colores ---- */
TowerLight baliza(HW.Luz_Roja, HW.Luz_Amarilla, HW.Luz_Verde);

/* --- Diagnostico ---------------------------------------------------------- */
StepTracer                 tracer(cinta, Serial);
MaintenanceConsole<4>      consola(manager, Serial);
AlarmManager<8>            alarmas;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }

  Serial.println(F("=== CoreFSM - Cinta transportadora ==="));

  /* 1. Hardware: configura pines y registra la imagen de proceso. */
  HW.begin();

  /* 2. Logica: registra el bloque en el scan y arranca. */
  manager.registerBlock(&cinta, F("CINTA"));
  manager.beginAll();

  /* 3. Habilitacion. Es el equivalente al selector de "automatico" de un
   *    cuadro: viene puesta de fabrica, y bajarla equivale a una parada
   *    ordenada. Si gobiernas la maquina por bus o por HMI, este es el bit
   *    que manda el maestro de linea. */
  cinta.ST.cfgw.enable = true;
  cinta.start();

  baliza.lampTest();               /* prueba de lamparas al arrancar */
  Serial.println(F("Listo. Pulsa el boton verde para transportar. '?' = ayuda."));
}

void loop() {
  /* =========================================================================
   *  FASE 1 - PAE: imagen de proceso de entradas
   *  Se leen TODOS los sensores de golpe y se filtran los rebotes. A partir de
   *  aqui, la logica trabaja con una foto congelada y coherente de la planta.
   * ====================================================================== */
  HW.readInputs();

  /* =========================================================================
   *  FASE 2 - Conexion de las senales de planta a la interfaz del bloque
   *  Es el equivalente exacto a cablear los parametros de un bloque de funcion
   *  en el OB1 de TIA Portal. El bloque usa nombres genericos (ordenMarcha);
   *  la planta usa nombres concretos (Pulsador_Marcha). Gracias a esa
   *  separacion, el mismo bloque vale para otra cinta con otros nombres.
   *
   *  Fijate en hasRisen(): se pasa el FLANCO, no el nivel. Si se pasara el
   *  nivel, mantener el dedo en el boton relanzaria el ciclo sin parar.
   * ====================================================================== */
  cinta.ordenMarcha       = HW.Pulsador_Marcha.hasRisen();
  cinta.ordenPausa        = HW.Pulsador_Pausa.hasRisen();
  cinta.finDeCinta        = HW.FC_Fin_Cinta.isTriggered();
  cinta.consignaVelocidad = (uint8_t)(HW.Consigna_Velocidad.value() >> 2);  /* 0..1023 -> 0..255 */

  /* Rearme: solo tiene efecto si hay fallo Y la causa ya no esta presente
   * (lo decide canReset() dentro del bloque). */
  if (HW.Pulsador_Rearme.hasRisen()) {
    cinta.reset();
    alarmas.ackAll();
    if (cinta.isIdle()) cinta.start();
  }

  /* =========================================================================
   *  FASE 3 - OB1: ejecucion de la logica
   * ====================================================================== */
  manager.updateAll();

  /* =========================================================================
   *  FASE 4 - Diagnostico y senalizacion
   * ====================================================================== */

  /* Registro de alarmas: raiseIf dispara o limpia segun la condicion, asi que
   * se puede llamar en cada scan sin llenar la lista de duplicados. */
  alarmas.raiseIf(cinta.getErrorCode() == ALM_ATASCO,
                  ALM_ATASCO, F("Atasco: la pieza no llega al final"), ALARM_FAULT);
  alarmas.raiseIf(cinta.hayAvisoLineaOcupada(),
                  ALM_PIEZA_NO_SALE, F("Linea ocupada: retirar la pieza"), ALARM_WARNING);

  /* Una sola linea traduce el estado de la maquina al codigo de colores. */
  baliza.reflect(cinta.getState(),
                 manager.isEmergencyStop(),
                 alarmas.highestSeverity() == ALARM_WARNING);

  /* Salidas calculadas por el proceso -> tags de planta. */
  HW.Motor_Cinta.set(cinta.motorMarcha);

  tracer.update();     /* imprime solo cuando cambia de paso o de estado */
  consola.update();    /* atiende los comandos del monitor serie         */

  /* =========================================================================
   *  FASE 5 - PAA: imagen de proceso de salidas
   *  Todas las salidas se escriben juntas, al final. Es tambien donde se
   *  calculan las intermitencias de la baliza.
   * ====================================================================== */
  HW.writeOutputs();
}
