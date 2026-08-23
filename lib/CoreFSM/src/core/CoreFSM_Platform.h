#ifndef COREFSM_PLATFORM_H
#define COREFSM_PLATFORM_H

/* ===========================================================================
 *  CoreFSM_Platform.h  -  Capa de abstraccion de plataforma
 * ---------------------------------------------------------------------------
 *  Este archivo es el unico sitio de toda la libreria donde se pregunta "sobre
 *  que microcontrolador estoy corriendo". El resto del codigo trabaja siempre
 *  con las macros que se definen aqui, de forma que anadir soporte para una
 *  placa nueva consiste en tocar solo este archivo.
 *
 *  Por que hace falta:
 *  Un Arduino Nano (AVR) tiene 2 KB de RAM y una EEPROM real de 1 KB grabada en
 *  silicio. Un ESP32 tiene cientos de KB de RAM y NO tiene EEPROM fisica: la
 *  emula sobre un sector de la memoria flash, y para que los cambios se hagan
 *  permanentes hay que llamar explicitamente a commit(). Si el codigo de la
 *  libreria mezclara ambos casos, seria ilegible. Aqui se aisla esa diferencia.
 * ======================================================================== */

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 *  1. Identificacion de la familia de microcontrolador
 * ------------------------------------------------------------------------ */
#if defined(ARDUINO_ARCH_AVR) || defined(__AVR__)
  #define CFSM_ARCH_AVR        1
  #define CFSM_ARCH_NAME       "AVR"
  #define CFSM_IS_CONSTRAINED  1   /* RAM muy escasa: diseno estatico obligatorio */
#elif defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  #define CFSM_ARCH_ESP32      1
  #define CFSM_ARCH_NAME       "ESP32"
  #define CFSM_IS_CONSTRAINED  0
#elif defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
  #define CFSM_ARCH_ESP8266    1
  #define CFSM_ARCH_NAME       "ESP8266"
  #define CFSM_IS_CONSTRAINED  0
#elif defined(ARDUINO_ARCH_RP2040)
  #define CFSM_ARCH_RP2040     1
  #define CFSM_ARCH_NAME       "RP2040"
  #define CFSM_IS_CONSTRAINED  0
#elif defined(ARDUINO_ARCH_SAMD)
  #define CFSM_ARCH_SAMD       1
  #define CFSM_ARCH_NAME       "SAMD"
  #define CFSM_IS_CONSTRAINED  0
#else
  #define CFSM_ARCH_UNKNOWN    1
  #define CFSM_ARCH_NAME       "GENERIC"
  #define CFSM_IS_CONSTRAINED  0
#endif

/* ---------------------------------------------------------------------------
 *  2. Persistencia: hay memoria no volatil disponible?
 * ---------------------------------------------------------------------------
 *  CFSM_HAS_NVM        -> existe alguna forma de guardar datos que sobreviven
 *                         al corte de tension.
 *  CFSM_NVM_NEEDS_COMMIT -> hay que llamar a commit() para consolidar.
 *
 *  El usuario puede desactivar la persistencia por completo definiendo
 *  CFSM_DISABLE_NVM antes de incluir la libreria (util para tests en un PC o
 *  para ahorrar flash cuando no se usan recetas ni configuracion).
 * ------------------------------------------------------------------------ */
#if defined(CFSM_DISABLE_NVM)
  #define CFSM_HAS_NVM            0
  #define CFSM_NVM_NEEDS_COMMIT   0
#elif defined(CFSM_ARCH_AVR)
  #define CFSM_HAS_NVM            1
  #define CFSM_NVM_NEEDS_COMMIT   0   /* EEPROM real: la escritura es inmediata */
  #define CFSM_NVM_SIZE_DEFAULT   512
#elif defined(CFSM_ARCH_ESP32) || defined(CFSM_ARCH_ESP8266) || defined(CFSM_ARCH_RP2040)
  #define CFSM_HAS_NVM            1
  #define CFSM_NVM_NEEDS_COMMIT   1   /* EEPROM emulada sobre flash */
  #define CFSM_NVM_SIZE_DEFAULT   1024
#else
  #define CFSM_HAS_NVM            0
  #define CFSM_NVM_NEEDS_COMMIT   0
#endif

/* ---------------------------------------------------------------------------
 *  3. Cadenas de texto en memoria de programa
 * ---------------------------------------------------------------------------
 *  En AVR, una cadena literal ocupa RAM salvo que se marque con F() o PROGMEM.
 *  Con 2 KB de RAM, unas pocas decenas de mensajes de diagnostico se comen la
 *  memoria y el micro empieza a corromper la pila sin previo aviso.
 *  En ESP32 la RAM es abundante y F() es un no-op inofensivo.
 *
 *  CFSM_FSTR("texto")  -> devuelve el tipo correcto para imprimir sin gastar RAM
 *  CFSM_PROGMEM        -> atributo para tablas constantes
 * ------------------------------------------------------------------------ */
#define CFSM_FSTR(s)   F(s)

#if defined(CFSM_ARCH_AVR)
  #include <avr/pgmspace.h>
  #define CFSM_PROGMEM        PROGMEM
  #define CFSM_READ_PTR(addr) ((const char*)pgm_read_word(addr))
#else
  #define CFSM_PROGMEM
  #define CFSM_READ_PTR(addr) (*(addr))
#endif

/* ---------------------------------------------------------------------------
 *  4. Base de tiempos del sistema
 * ---------------------------------------------------------------------------
 *  Toda la libreria mide el tiempo con cfsm_millis(). Centralizarlo permite
 *  dos cosas importantes:
 *    a) Sustituirlo por un reloj falso en tests unitarios sobre un PC, sin
 *       tocar ni una linea de la logica de proceso.
 *    b) Documentar de forma explicita el tratamiento del desbordamiento.
 *
 *  Sobre el desbordamiento de millis(): la funcion devuelve un unsigned long de
 *  32 bits que vuelve a cero cada 49,7 dias. TODAS las comparaciones de tiempo
 *  de esta libreria se escriben como (ahora - inicio) >= plazo y nunca como
 *  ahora >= (inicio + plazo). La primera forma es inmune al desbordamiento
 *  porque la resta en aritmetica sin signo "da la vuelta" correctamente; la
 *  segunda se cuelga durante 49 dias cuando el desbordamiento cae en medio.
 *  Esto no es una sutileza teorica: una maquina que lleva mes y medio en
 *  produccion sin apagarse se para en seco por este motivo.
 * ------------------------------------------------------------------------ */
typedef uint32_t cfsm_time_t;

#if defined(CFSM_CUSTOM_CLOCK)
  extern cfsm_time_t cfsm_millis();   /* el usuario aporta su propio reloj */
#else
  static inline cfsm_time_t cfsm_millis() { return (cfsm_time_t)millis(); }
#endif

/* Tiempo transcurrido desde una marca, a prueba de desbordamiento. */
static inline cfsm_time_t cfsm_elapsed(cfsm_time_t since) {
  return (cfsm_time_t)(cfsm_millis() - since);
}

/* ---------------------------------------------------------------------------
 *  5. Utilidades de compilacion
 * ------------------------------------------------------------------------ */
#define CFSM_UNUSED(x)      (void)(x)
#define CFSM_ARRAY_LEN(a)   (sizeof(a) / sizeof((a)[0]))

/* Version de la libreria, accesible desde el codigo de usuario. */
#define CFSM_VERSION_MAJOR  2
#define CFSM_VERSION_MINOR  0
#define CFSM_VERSION_PATCH  0
#define CFSM_VERSION_STR    "2.0.0"

#endif /* COREFSM_PLATFORM_H */
