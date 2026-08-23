#ifndef COREFSM_IO_TABLE_H
#define COREFSM_IO_TABLE_H

#include "DigitalSensor.h"
#include "DigitalOutput.h"
#include "AnalogSensor.h"
#include "DeviceManager.h"

/* ===========================================================================
 *  IOTable.h  -  Tabla de variables visual (patron X-Macro)
 * ---------------------------------------------------------------------------
 *  QUE ES UNA X-MACRO Y POR QUE AQUI
 *  ---------------------------------
 *  El objetivo es tener una tabla de asignacion de hardware que se lea como
 *  la tabla de variables de TIA Portal: una fila por senal, con su pin, su
 *  nombre simbolico y sus opciones. Y que TODO lo demas (declarar la variable,
 *  configurar el pin, registrarla en la imagen de proceso, leerla y escribirla
 *  cada ciclo) salga solo de esa tabla, sin escribir una linea mas.
 *
 *  El truco es escribir la tabla como una macro que recibe OTRA macro:
 *
 *      #define CFSM_TABLE_DI(ROW)                       \
 *        ROW(  2, Pulsador_Marcha,  true,  20 )         \
 *        ROW(  3, FC_Trabajo,       true,   5 )
 *
 *  Ahora, expandiendo CFSM_TABLE_DI con distintas ROW se obtienen cosas
 *  distintas de una misma tabla: la declaracion de los objetos, el registro en
 *  el gestor, la cuenta de cuantos hay... Cada "pasada" genera una parte.
 *
 *  Se llama patron X-Macro, tiene cuarenta anos de uso en C embebido, y su
 *  virtud es que hace IMPOSIBLE la desincronizacion: no puedes anadir un
 *  sensor y olvidarte de registrarlo, porque el registro se genera de la misma
 *  fila que la declaracion.
 *
 *  COMO SE USA
 *  -----------
 *  En tu proyecto, crea HardwareConfig.h (o deja que lo genere el script de
 *  Wokwi) con este contenido:
 *
 *      #ifndef HARDWARE_CONFIG_H
 *      #define HARDWARE_CONFIG_H
 *
 *      //        PIN | NOMBRE SIMBOLICO      | PULL-UP | ANTIRREBOTE(ms)
 *      #define CFSM_TABLE_DI(ROW)                            \
 *        ROW(   2,   Pulsador_Marcha,          true,   20 )  \
 *        ROW(   3,   FC_Carro_Trabajo,         true,    5 )
 *
 *      //        PIN | NOMBRE SIMBOLICO      | ACTIVO A BAJO
 *      #define CFSM_TABLE_DO(ROW)                            \
 *        ROW(  13,   Luz_Roja,                 false )       \
 *        ROW(  12,   Luz_Verde,                false )
 *
 *      //        PIN | NOMBRE SIMBOLICO      | FILTRO(0-8)
 *      #define CFSM_TABLE_AI(ROW)                            \
 *        ROW(  A0,   Potenciometro_Velocidad,  3 )
 *
 *      #include <io/IOTable.h>
 *      #endif
 *
 *  Y en el .ino:
 *
 *      #include "HardwareConfig.h"
 *      CFSM_DEFINE_HARDWARE          // crea la instancia global HW
 *
 *      void setup() { HW.begin(); }
 *      void loop() {
 *        HW.readInputs();
 *        ...
 *        HW.writeOutputs();
 *      }
 *
 *  A partir de ahi, cada fila de la tabla existe como objeto completo:
 *
 *      HW.Pulsador_Marcha.hasRisen()      // flanco, ya filtrado
 *      HW.FC_Carro_Trabajo.isTriggered()  // nivel estable
 *      HW.Luz_Roja.setMode(OUT_BLINK_FAST)
 *      HW.Potenciometro_Velocidad.scaled()
 *
 *  Anadir una senal nueva es anadir una fila. Nada mas.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  Guarda de uso
 * ---------------------------------------------------------------------------
 *  Este archivo GENERA la definicion de struct CfsmHardware a partir de las
 *  macros de tabla. Si una segunda unidad de compilacion lo incluyera sin
 *  esas macros definidas, veria una estructura DISTINTA con el mismo nombre.
 *  El enlazador no dice nada -es una violacion silenciosa de la regla de
 *  definicion unica- y el resultado es que un modulo lee los campos del objeto
 *  HW en los offsets equivocados. Sale basura, y no hay forma humana de
 *  relacionar el sintoma con la causa.
 *
 *  Por eso solo se puede incluir desde el HardwareConfig.h del proyecto, y
 *  siempre despues de declarar las tablas.
 * ------------------------------------------------------------------------ */
#if !defined(CFSM_TABLE_DI) && !defined(CFSM_TABLE_DO) && !defined(CFSM_TABLE_AI)
  #error "io/IOTable.h se incluye desde tu HardwareConfig.h, DESPUES de definir CFSM_TABLE_DI / CFSM_TABLE_DO / CFSM_TABLE_AI. No lo incluyas suelto."
#endif

/* Tablas vacias por defecto, para que no haga falta declarar las tres. */
#ifndef CFSM_TABLE_DI
  #define CFSM_TABLE_DI(ROW)
#endif
#ifndef CFSM_TABLE_DO
  #define CFSM_TABLE_DO(ROW)
#endif
#ifndef CFSM_TABLE_AI
  #define CFSM_TABLE_AI(ROW)
#endif

/* ---------------------------------------------------------------------------
 *  Pasada 1: contar filas
 *  Cada fila se expande a "+1", asi que "0 +1 +1 +1" da el total. Se resuelve
 *  en tiempo de compilacion, de modo que el array del DeviceManager sale
 *  dimensionado exacto: ni un byte de RAM de mas.
 * ------------------------------------------------------------------------ */
#define CFSM_ROW_COUNT(...) +1

static const uint8_t CFSM_DI_COUNT = 0 CFSM_TABLE_DI(CFSM_ROW_COUNT);
static const uint8_t CFSM_DO_COUNT = 0 CFSM_TABLE_DO(CFSM_ROW_COUNT);
static const uint8_t CFSM_AI_COUNT = 0 CFSM_TABLE_AI(CFSM_ROW_COUNT);
static const uint8_t CFSM_IO_COUNT = CFSM_DI_COUNT + CFSM_DO_COUNT + CFSM_AI_COUNT;

/* ---------------------------------------------------------------------------
 *  Pasada 2: declarar los objetos
 * ------------------------------------------------------------------------ */
#define CFSM_ROW_DECL_DI(pin, name, pullup, debounce) \
  DigitalSensor name{pin, pullup, debounce};

#define CFSM_ROW_DECL_DO(pin, name, activeLow) \
  DigitalOutput name{pin, activeLow};

#define CFSM_ROW_DECL_AI(pin, name, filter) \
  AnalogSensor name{pin, filter};

/* ---------------------------------------------------------------------------
 *  Pasada 3: registrar en el gestor y poner el nombre para el diagnostico
 * ------------------------------------------------------------------------ */
#define CFSM_ROW_REG_DI(pin, name, pullup, debounce) \
  devices.registerDevice(&name, F(#name));

#define CFSM_ROW_REG_DO(pin, name, activeLow) \
  devices.registerDevice(&name, F(#name));

#define CFSM_ROW_REG_AI(pin, name, filter) \
  devices.registerDevice(&name, F(#name));

/* ---------------------------------------------------------------------------
 *  Pasada 4: volcado de diagnostico
 * ------------------------------------------------------------------------ */
#define CFSM_ROW_DESC(pin, name, ...) \
  { name.describe(out); out.print(' '); }

/* ===========================================================================
 *  El contenedor de hardware
 *  Todos los objetos de campo del proyecto, mas su imagen de proceso.
 * ======================================================================== */
struct CfsmHardware {
  /* --- Objetos generados a partir de la tabla --- */
  CFSM_TABLE_DI(CFSM_ROW_DECL_DI)
  CFSM_TABLE_DO(CFSM_ROW_DECL_DO)
  CFSM_TABLE_AI(CFSM_ROW_DECL_AI)

  /* Dimensionado exacto: no sobra ni falta un puntero. */
  DeviceManager<CFSM_IO_COUNT ? CFSM_IO_COUNT : 1> devices;

  /* Configura todos los pines y registra todos los objetos. */
  void begin() {
    CFSM_TABLE_DI(CFSM_ROW_REG_DI)
    CFSM_TABLE_DO(CFSM_ROW_REG_DO)
    CFSM_TABLE_AI(CFSM_ROW_REG_AI)
    devices.beginAll();
  }

  /* PAE: foto de todas las entradas. Primera linea del loop(). */
  void readInputs()   { devices.readAllInputs(); }

  /* PAA: volcado de todas las salidas. Ultima linea del loop(). */
  void writeOutputs() { devices.writeAllOutputs(); }

  /* Quita todos los forzados. */
  void releaseAllForces() { devices.releaseAllForces(); }
  bool hasAnyForce() const { return devices.hasAnyForce(); }

  /* Tabla de observacion de la E/S. Llamala solo bajo peticion (por ejemplo,
   * al recibir un caracter por el puerto serie), nunca en cada scan. */
  void printIoTable(Print& out) {
    out.println(F("---- IMAGEN DE PROCESO ----"));
    out.print(F(" DI: ")); CFSM_TABLE_DI(CFSM_ROW_DESC) out.println();
    out.print(F(" DO: ")); CFSM_TABLE_DO(CFSM_ROW_DESC) out.println();
    out.print(F(" AI: ")); CFSM_TABLE_AI(CFSM_ROW_DESC) out.println();
    if (hasAnyForce()) out.println(F(" *** HAY SENALES FORZADAS ***"));
    out.println(F("---------------------------"));
  }
};

/* Instancia global. Ponla UNA sola vez, en el .ino. */
#define CFSM_DEFINE_HARDWARE  CfsmHardware HW;

extern CfsmHardware HW;

#endif /* COREFSM_IO_TABLE_H */
