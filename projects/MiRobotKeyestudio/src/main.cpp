/* =============================================================================
 *  MiRobotKeyestudio
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
#include "LedIndicator.h"
#include "Proceso.h"

const uint8_t PIN_LED = 9;

LedIndicator led(PIN_LED);
Proceso proceso;

void setup() {
  Serial.begin(9600);
  led.begin();

  led.configureBreathing(5, 30, 15); // Configuración de respiración: brillo mínimo, máximo y velocidad  
  proceso.begin();
  proceso.start();
}

void loop() {
  proceso.update();

  // Se actualiza el modo y el ciclo del LED
  led.setMode(proceso.modoLed);
  led.update();
}