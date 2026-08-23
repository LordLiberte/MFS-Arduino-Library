#ifndef COREFSM_CONTROL_WORDS_H
#define COREFSM_CONTROL_WORDS_H

#include "CoreFSM_Platform.h"

/* ===========================================================================
 *  ControlWords.h  -  Palabras de mando y estado de 16 bits
 * ---------------------------------------------------------------------------
 *  DE DONDE VIENE ESTA IDEA
 *  ------------------------
 *  En automatizacion pesada (automocion, packaging, linea blanca) cada estacion
 *  expone al maestro de linea dos registros de 16 bits:
 *
 *    CFGW / CTLW  (Control Word)  -> lo que el maestro ORDENA a la estacion
 *    STW          (Status Word)   -> lo que la estacion RESPONDE al maestro
 *
 *  Toda la conversacion entre el PLC de linea, el HMI y la estacion cabe en
 *  esos dos enteros. Eso tiene tres consecuencias practicas enormes:
 *
 *    1. Un unico acceso de memoria (o un unico registro Modbus, o cuatro bytes
 *       en una trama serie) transporta el estado completo de la maquina. No
 *       hacen falta veinte variables sueltas ni veinte direcciones de bus.
 *    2. El diagnostico es inmediato: imprimes STW en hexadecimal y de un
 *       vistazo sabes que bit esta reteniendo la maquina.
 *    3. La interfaz queda congelada. Puedes reescribir por dentro la estacion
 *       entera; mientras respete el significado de los bits, el maestro de
 *       linea no se entera.
 *
 *  COMO SE IMPLEMENTA EN C++
 *  -------------------------
 *  Con una union entre un uint16_t y una estructura de campos de bit. La union
 *  hace que ambas vistas compartan exactamente los mismos dos bytes de RAM:
 *
 *      palabra.start = true;      // acceso comodo por nombre
 *      Serial.println(palabra.raw, HEX);  // acceso en bloque para el bus
 *
 *  ADVERTENCIA IMPORTANTE SOBRE EL ORDEN DE LOS BITS
 *  -------------------------------------------------
 *  El estandar de C++ NO garantiza en que bit fisico cae cada campo: es
 *  "implementation-defined". En la practica, GCC (que es el compilador de AVR,
 *  ESP32, RP2040 y SAMD por igual) asigna los campos empezando por el bit menos
 *  significativo, asi que 'enable' cae en el bit 0, 'start' en el bit 1, etc.
 *
 *  Como esta libreria solo se compila con GCC, el reparto es estable. Pero si
 *  vas a mandar estas palabras por un bus a un dispositivo de OTRO fabricante
 *  compilado con OTRO compilador, no confies en la union: usa las mascaras
 *  explicitas CFGW_BIT_* / STW_BIT_* que se definen mas abajo, que si son
 *  portables al 100%.
 * ======================================================================== */

/* ---------------------------------------------------------------------------
 *  1. CFGW - Palabra de mando de una secuencia / estacion
 * ------------------------------------------------------------------------ */
union ConfigWord {
  uint16_t raw;
  struct {
    bool enable       : 1;  /* Bit 0: habilitacion general. Sin esto, nada arranca. */
    bool start        : 1;  /* Bit 1: peticion de arranque de ciclo (se consume). */
    bool stop         : 1;  /* Bit 2: parada ordenada al terminar el ciclo actual. */
    bool resetFault   : 1;  /* Bit 3: rearme / acuse de alarma (se consume).      */
    bool singleStep   : 1;  /* Bit 4: modo paso a paso para puesta en marcha.     */
    bool nextStep     : 1;  /* Bit 5: en modo paso a paso, avanzar un paso.       */
    bool bypassTimer  : 1;  /* Bit 6: ignorar los timeouts (SOLO para depurar).   */
    bool holdRequest  : 1;  /* Bit 7: pausa en caliente conservando el paso.      */
    bool abortRequest : 1;  /* Bit 8: aborto inmediato a estado seguro.           */
    bool quickStop    : 1;  /* Bit 9: ACTIVO A NIVEL BAJO. false = parada rapida.
                               Mismo convenio que en los accionamientos: si el
                               bus se cae o el cable se corta, la palabra llega
                               a cero y la maquina se para sola. El fallo debe
                               llevar SIEMPRE al estado seguro.                 */
    uint16_t reserved : 6;  /* Bits 10..15: libres para ampliaciones del usuario. */
  };
};

/* Mascaras portables equivalentes a los campos de arriba. */
#define CFGW_BIT_ENABLE        0x0001
#define CFGW_BIT_START         0x0002
#define CFGW_BIT_STOP          0x0004
#define CFGW_BIT_RESET_FAULT   0x0008
#define CFGW_BIT_SINGLE_STEP   0x0010
#define CFGW_BIT_NEXT_STEP     0x0020
#define CFGW_BIT_BYPASS_TIMER  0x0040
#define CFGW_BIT_HOLD          0x0080
#define CFGW_BIT_ABORT         0x0100
#define CFGW_BIT_QUICK_STOP    0x0200

/* ---------------------------------------------------------------------------
 *  2. STW - Palabra de estado de una secuencia / estacion
 * ------------------------------------------------------------------------ */
union StatusWord {
  uint16_t raw;
  struct {
    bool ready        : 1;  /* Bit 0: condiciones de arranque cumplidas.          */
    bool running      : 1;  /* Bit 1: secuencia en ejecucion.                     */
    bool done         : 1;  /* Bit 2: ciclo terminado con exito.                  */
    bool fault        : 1;  /* Bit 3: alarma activa, la maquina esta retenida.    */
    bool paused       : 1;  /* Bit 4: en pausa / hold, el paso se conserva.       */
    bool stepTimeout  : 1;  /* Bit 5: causa del fallo: venciO el watchdog de paso.*/
    bool inHomePos    : 1;  /* Bit 6: la maquina esta en posicion de reposo.      */
    bool busy         : 1;  /* Bit 7: ocupada; no acepta ordenes nuevas.          */
    bool waitingAck   : 1;  /* Bit 8: espera el acuse de la estacion siguiente.   */
    bool warning      : 1;  /* Bit 9: aviso no bloqueante (mantenimiento, deriva).*/
    bool suspended    : 1;  /* Bit 10: parada por causa EXTERNA (falta pieza, la
                               siguiente estacion llena). La maquina esta sana y
                               lista; se reanuda sola cuando la causa se va.     */
    bool held         : 1;  /* Bit 11: parada por causa INTERNA o por decision
                               del operario (recarga, control de calidad). Se
                               reanuda cuando la condicion propia se cumple.     */
    bool stepWarn     : 1;  /* Bit 12: el paso lleva mas del tiempo de aviso,
                               pero aun no del de fallo. Aun produce.            */
    uint16_t reserved : 3;  /* Bits 13..15: libres.                               */
  };
};

#define STW_BIT_READY          0x0001
#define STW_BIT_RUNNING        0x0002
#define STW_BIT_DONE           0x0004
#define STW_BIT_FAULT          0x0008
#define STW_BIT_PAUSED         0x0010
#define STW_BIT_STEP_TIMEOUT   0x0020
#define STW_BIT_IN_HOME_POS    0x0040
#define STW_BIT_BUSY           0x0080
#define STW_BIT_WAITING_ACK    0x0100
#define STW_BIT_WARNING        0x0200
#define STW_BIT_SUSPENDED      0x0400
#define STW_BIT_HELD           0x0800
#define STW_BIT_STEP_WARN      0x1000

/* ---------------------------------------------------------------------------
 *  3. Palabras de accionamiento (motor / variador)
 * ---------------------------------------------------------------------------
 *  Inspiradas en PROFIdrive y CiA 402, los dos perfiles de bus de campo que
 *  usan practicamente todos los variadores y servos industriales.
 *
 *  Detalle importante de seguridad: quickStop es ACTIVO A NIVEL BAJO. Es decir,
 *  quickStop = true significa "NO hay parada rapida, puedes moverte", y
 *  quickStop = false significa "PARADA RAPIDA AHORA". Puede parecer al reves,
 *  pero es el convenio industrial y existe por una razon: si el cable se corta
 *  o el bus se cae, la palabra llega a cero y el motor para solo. La seguridad
 *  se cablea siempre de forma que el fallo lleve al estado seguro.
 * ------------------------------------------------------------------------ */
union DriveControlWord {
  uint16_t raw;
  struct {
    bool enable       : 1;  /* Bit 0: habilitar la etapa de potencia.             */
    bool runFwd       : 1;  /* Bit 1: marcha continua en sentido directo.         */
    bool runRev       : 1;  /* Bit 2: marcha continua en sentido inverso.         */
    bool jogFwd       : 1;  /* Bit 3: impulso manual directo.                     */
    bool jogRev       : 1;  /* Bit 4: impulso manual inverso.                     */
    bool quickStop    : 1;  /* Bit 5: ACTIVO A BAJO. false = parada rapida.       */
    bool resetFault   : 1;  /* Bit 6: rearme de alarma del accionamiento.         */
    bool brakeRelease : 1;  /* Bit 7: liberar freno mecanico si existe.           */
    uint16_t reserved : 8;  /* Bits 8..15: libres.                                */
  };
};

union DriveStatusWord {
  uint16_t raw;
  struct {
    bool readyToSwitchOn : 1; /* Bit 0: listo para recibir enable.                */
    bool enabled         : 1; /* Bit 1: potencia aplicada al motor.               */
    bool running         : 1; /* Bit 2: el eje se esta moviendo.                  */
    bool fault           : 1; /* Bit 3: averia (termico, sobrecarga, bloqueo).    */
    bool warning         : 1; /* Bit 4: aviso (cerca del limite termico).         */
    bool fwdActive       : 1; /* Bit 5: girando en sentido directo.               */
    bool revActive       : 1; /* Bit 6: girando en sentido inverso.               */
    bool atSetpoint      : 1; /* Bit 7: consigna de velocidad/posicion alcanzada. */
    uint16_t reserved    : 8; /* Bits 8..15: libres.                              */
  };
};

/* ---------------------------------------------------------------------------
 *  4. Codigos de error normalizados
 * ---------------------------------------------------------------------------
 *  Un numero solo no dice nada si no hay una tabla que lo traduzca. Estos son
 *  los codigos que genera la propia libreria; reserva el rango 0x8000..0xFFFF
 *  para los errores especificos de tu maquina, asi nunca chocaran con los
 *  de una version futura de CoreFSM.
 * ------------------------------------------------------------------------ */
enum CfsmError : uint16_t {
  CFSM_ERR_NONE            = 0x0000,
  CFSM_ERR_STEP_TIMEOUT    = 0x0001,  /* un paso agoto su tiempo maximo          */
  CFSM_ERR_CYCLE_TIMEOUT   = 0x0002,  /* el ciclo completo agoto su tiempo       */
  CFSM_ERR_INTERLOCK       = 0x0003,  /* condicion de enclavamiento incumplida   */
  CFSM_ERR_ESTOP           = 0x0004,  /* seta de emergencia pulsada              */
  CFSM_ERR_HANDSHAKE       = 0x0005,  /* la estacion vecina no responde          */
  CFSM_ERR_DRIVE_FAULT     = 0x0006,  /* averia propagada desde un accionamiento */
  CFSM_ERR_SENSOR_INVALID  = 0x0007,  /* dos sensores excluyentes activos a la vez*/
  CFSM_ERR_RECIPE_INVALID  = 0x0008,  /* receta ausente, vacia o corrupta        */
  CFSM_ERR_CONFIG_CRC      = 0x0009,  /* configuracion en memoria no volatil mal */
  CFSM_ERR_NOT_HOMED       = 0x000A,  /* se pidio automatico sin hacer el home   */
  CFSM_ERR_SCAN_OVERRUN    = 0x000B,  /* el ciclo de scan se paso del limite     */
  CFSM_ERR_USER_BASE       = 0x8000   /* a partir de aqui, errores de tu maquina */
};

/* Traduce un codigo de error a texto legible, sin gastar RAM en AVR. */
inline const __FlashStringHelper* cfsmErrorText(uint16_t code) {
  switch (code) {
    case CFSM_ERR_NONE:           return CFSM_FSTR("Sin fallo");
    case CFSM_ERR_STEP_TIMEOUT:   return CFSM_FSTR("Timeout de paso");
    case CFSM_ERR_CYCLE_TIMEOUT:  return CFSM_FSTR("Timeout de ciclo");
    case CFSM_ERR_INTERLOCK:      return CFSM_FSTR("Enclavamiento no cumplido");
    case CFSM_ERR_ESTOP:          return CFSM_FSTR("Parada de emergencia");
    case CFSM_ERR_HANDSHAKE:      return CFSM_FSTR("Fallo de handshake");
    case CFSM_ERR_DRIVE_FAULT:    return CFSM_FSTR("Averia de accionamiento");
    case CFSM_ERR_SENSOR_INVALID: return CFSM_FSTR("Senal de sensor incoherente");
    case CFSM_ERR_RECIPE_INVALID: return CFSM_FSTR("Receta no valida");
    case CFSM_ERR_CONFIG_CRC:     return CFSM_FSTR("CRC de configuracion erroneo");
    case CFSM_ERR_NOT_HOMED:      return CFSM_FSTR("Maquina sin referenciar (home)");
    case CFSM_ERR_SCAN_OVERRUN:   return CFSM_FSTR("Scan demasiado largo");
    default:                      return CFSM_FSTR("Error de aplicacion");
  }
}

#endif /* COREFSM_CONTROL_WORDS_H */
