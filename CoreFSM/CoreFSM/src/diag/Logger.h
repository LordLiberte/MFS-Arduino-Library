#ifndef COREFSM_LOGGER_H
#define COREFSM_LOGGER_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  Logger.h  -  Trazas por puerto serie sin estropear el ciclo de scan
 * ---------------------------------------------------------------------------
 *  EL PELIGRO DE Serial.println EN UNA MAQUINA
 *  -------------------------------------------
 *  Serial.print no es instantaneo. Copia el texto a un buffer de transmision
 *  (64 bytes en AVR) y el hardware lo va sacando a la velocidad configurada.
 *  A 115200 baudios, cada caracter tarda unos 87 microsegundos.
 *
 *  El problema aparece cuando el buffer se llena: entonces Serial.print SE
 *  QUEDA BLOQUEADO esperando sitio. Una linea de 40 caracteres puede detener
 *  el programa 3,5 milisegundos. Si eso pasa dentro del switch de una
 *  secuencia, que se ejecuta miles de veces por segundo, el buffer no se vacia
 *  nunca, el scan pasa de microsegundos a milisegundos y la maquina deja de
 *  reaccionar a tiempo. Con suerte se nota; con mala suerte, solo falla el dia
 *  que la pieza va rapida.
 *
 *  REGLAS PARA NO CAER EN ELLO
 *  ---------------------------
 *   1. Nunca imprimir desde dentro de un case del switch de pasos.
 *   2. Imprimir en onStepEntered() y onTransition(), que corren una sola vez.
 *   3. Imprimir en respuesta a un evento (una alarma nueva, un cambio de
 *      estado), nunca por nivel ("mientras esto sea true, imprime").
 *   4. Compilar sin trazas en produccion.
 *
 *  DESACTIVACION TOTAL EN COMPILACION
 *  ----------------------------------
 *  Define CFSM_LOG_LEVEL antes de incluir la libreria:
 *
 *      #define CFSM_LOG_LEVEL 0    // ninguna traza
 *      #define CFSM_LOG_LEVEL 1    // solo errores
 *      #define CFSM_LOG_LEVEL 2    // errores y avisos
 *      #define CFSM_LOG_LEVEL 3    // + informacion (por defecto)
 *      #define CFSM_LOG_LEVEL 4    // + depuracion
 *
 *  Las macros por encima del nivel elegido se convierten en NADA: el
 *  compilador las borra por completo, y con ellas los textos que llevaban
 *  dentro. En un AVR eso libera flash y RAM de verdad, no es cosmetico.
 * ======================================================================== */

#ifndef CFSM_LOG_LEVEL
  #define CFSM_LOG_LEVEL 3
#endif

enum CfsmLogLevel : uint8_t {
  CFSM_LOG_NONE  = 0,
  CFSM_LOG_ERROR = 1,
  CFSM_LOG_WARN  = 2,
  CFSM_LOG_INFO  = 3,
  CFSM_LOG_DEBUG = 4
};

class CfsmLogger {
  public:
    /* Por defecto no hay salida: hay que llamar a begin() expresamente.
     * Asi, una libreria que se usa sin monitor serie no gasta nada. */
    static void begin(Print& out, bool timestamps = true) {
      _out = &out;
      _timestamps = timestamps;
    }
    static void end() { _out = nullptr; }
    static bool ready() { return _out != nullptr; }
    static Print* out() { return _out; }

    static void prefix(char level) {
      if (!_out) return;
      if (_timestamps) {
        cfsm_time_t t = cfsm_millis();
        _out->print('[');
        _out->print(t / 1000);
        _out->print('.');
        cfsm_time_t ms = t % 1000;
        if (ms < 100) _out->print('0');
        if (ms < 10)  _out->print('0');
        _out->print(ms);
        _out->print(CFSM_FSTR("] "));
      }
      _out->print(level);
      _out->print(CFSM_FSTR(": "));
    }

  private:
    static Print* _out;
    static bool   _timestamps;
};

/* Definicion de los miembros estaticos. Al ser una libreria de solo cabeceras,
 * se marcan con __attribute__((weak)) para que varias unidades de compilacion
 * que incluyan este archivo no den error de simbolo duplicado. */
__attribute__((weak)) Print* CfsmLogger::_out = nullptr;
__attribute__((weak)) bool   CfsmLogger::_timestamps = true;

/* ---------------------------------------------------------------------------
 *  Macros de traza
 *  Envuelven todo en un do{}while(0) para que se comporten como una sentencia
 *  y no rompan un if sin llaves.
 * ------------------------------------------------------------------------ */
#define CFSM_LOG_LINE(lvl, ch, msg) \
  do { if (CFSM_LOG_LEVEL >= (lvl) && CfsmLogger::ready()) { \
         CfsmLogger::prefix(ch); CfsmLogger::out()->println(msg); } } while (0)

#define CFSM_LOG_KV(lvl, ch, msg, val) \
  do { if (CFSM_LOG_LEVEL >= (lvl) && CfsmLogger::ready()) { \
         CfsmLogger::prefix(ch); CfsmLogger::out()->print(msg); \
         CfsmLogger::out()->println(val); } } while (0)

#if CFSM_LOG_LEVEL >= 1
  #define CFSM_ERROR(m)      CFSM_LOG_LINE(1, 'E', m)
  #define CFSM_ERROR_V(m,v)  CFSM_LOG_KV(1, 'E', m, v)
#else
  #define CFSM_ERROR(m)      do{}while(0)
  #define CFSM_ERROR_V(m,v)  do{}while(0)
#endif

#if CFSM_LOG_LEVEL >= 2
  #define CFSM_WARN(m)       CFSM_LOG_LINE(2, 'W', m)
  #define CFSM_WARN_V(m,v)   CFSM_LOG_KV(2, 'W', m, v)
#else
  #define CFSM_WARN(m)       do{}while(0)
  #define CFSM_WARN_V(m,v)   do{}while(0)
#endif

#if CFSM_LOG_LEVEL >= 3
  #define CFSM_INFO(m)       CFSM_LOG_LINE(3, 'I', m)
  #define CFSM_INFO_V(m,v)   CFSM_LOG_KV(3, 'I', m, v)
#else
  #define CFSM_INFO(m)       do{}while(0)
  #define CFSM_INFO_V(m,v)   do{}while(0)
#endif

#if CFSM_LOG_LEVEL >= 4
  #define CFSM_DEBUG(m)      CFSM_LOG_LINE(4, 'D', m)
  #define CFSM_DEBUG_V(m,v)  CFSM_LOG_KV(4, 'D', m, v)
#else
  #define CFSM_DEBUG(m)      do{}while(0)
  #define CFSM_DEBUG_V(m,v)  do{}while(0)
#endif

#endif /* COREFSM_LOGGER_H */
