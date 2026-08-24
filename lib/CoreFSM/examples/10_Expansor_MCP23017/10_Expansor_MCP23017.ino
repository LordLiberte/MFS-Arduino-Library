/* Un MCP23017 añade 16 GPIO conservando la misma API HW.Nombre.
 * Las entradas se capturan juntas al principio del scan y las salidas se
 * escriben juntas al final. No uses I2C para funciones de seguridad. */

#include "HardwareConfig.h"
/* El punto y coma evita que el preprocesador de sketches confunda la macro
 * con el tipo de retorno de setup() cuando no hay otra global entre ambos. */
CFSM_DEFINE_HARDWARE;

void setup() {
  HW.begin();
}

void loop() {
  HW.readInputs();
  HW.Rele_Expansor.set(HW.Pulsador_Expansor.isTriggered());
  HW.writeOutputs();
}
