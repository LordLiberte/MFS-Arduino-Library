#ifndef COREFSM_BLOCK_BASE_H
#define COREFSM_BLOCK_BASE_H

#include "CoreFSM_Platform.h"
#include "ControlWords.h"

/* ===========================================================================
 *  BlockBase.h  -  Interfaz comun de todo bloque funcional
 * ---------------------------------------------------------------------------
 *  QUE ES UN BLOQUE
 *  ----------------
 *  Un "bloque" es el equivalente en esta libreria al bloque de funcion (FB) de
 *  un automata: una porcion de logica con sus propias variables internas, sus
 *  entradas y sus salidas, que se ejecuta una vez por ciclo de scan y que
 *  nunca bloquea la CPU. Un bloque no llama jamas a delay(): si necesita
 *  esperar, se lo apunta y devuelve el control, y en el siguiente ciclo mira
 *  si ya ha pasado el tiempo. Esa es la diferencia fundamental entre un
 *  programa de automatizacion y un script secuencial.
 *
 *  POR QUE UNA CLASE BASE ABSTRACTA
 *  --------------------------------
 *  Porque el orquestador (BlockManager) necesita poder guardar en un mismo
 *  array cosas muy distintas -una estacion de soldadura, una camara de vision,
 *  un semaforo, un conveyor- y llamarles a todas update() sin saber lo que
 *  son. Eso es polimorfismo: el manager habla con la interfaz, no con la
 *  implementacion. Es exactamente lo que hace el OB1 de un PLC cuando llama en
 *  orden a todos los FB que le has colgado.
 *
 *  CONTRATO QUE TODO BLOQUE DEBE CUMPLIR
 *  -------------------------------------
 *   1. update() se ejecuta miles de veces por segundo. Jamas debe contener
 *      delay(), while() de espera, ni Serial.println() incondicional.
 *   2. begin() se llama una sola vez al arrancar, despues de que el hardware
 *      este configurado.
 *   3. El bloque no toca pines: lee y escribe variables. Quien las conecta al
 *      mundo fisico es la capa de E/S (IOManager) o el .ino.
 * ======================================================================== */

class BlockBase {
  public:
    BlockBase()
      : _stateStartTime(0), _name(nullptr), _id(0), _enabled(true) {}

    virtual ~BlockBase() {}

    /* -----------------------------------------------------------------------
     *  METODOS OBLIGATORIOS
     *  Todo bloque concreto tiene que implementarlos. Son "= 0" (virtuales
     *  puros), lo que significa que el compilador impide instanciar un bloque
     *  que se haya dejado alguno sin escribir. Es una red de seguridad util.
     * -------------------------------------------------------------------- */

    /* Inicializacion unica. Aqui se ponen los valores de arranque, el paso
     * inicial de la secuencia, los parametros por defecto, etc. */
    virtual void begin() = 0;

    /* Cuerpo del bloque. Se ejecuta en cada vuelta del ciclo de scan. */
    virtual void update() = 0;

    /* Estado interno del bloque, codificado como numero. Cada familia de
     * bloques decide su propio significado; FsmBlock usa SystemState. */
    virtual uint8_t getState() const = 0;

    /* true si el bloque esta en fallo y por tanto no debe considerarse
     * operativo. El manager lo consulta para propagar alarmas. */
    virtual bool isFaulted() const = 0;

    /* Intento de rearme. Debe devolver el bloque a un estado seguro conocido.
     * Es responsabilidad de cada bloque decidir si acepta el rearme (por
     * ejemplo, negarse mientras la causa del fallo siga presente). */
    virtual void reset() = 0;

    /* -----------------------------------------------------------------------
     *  COMANDOS EN DIFUSION
     *  Son virtuales y no hacen nada por defecto. Existen aqui, en la clase
     *  base, para que el orquestador pueda mandar a TODOS los bloques a la vez
     *  sin necesidad de convertir punteros a la fuerza.
     *
     *  La alternativa -hacer static_cast<FsmBlock*> sobre un BlockBase*- es
     *  comportamiento indefinido en cuanto alguien registra un bloque que
     *  hereda directamente de BlockBase: la llamada se sale de la tabla de
     *  funciones virtuales y salta a una direccion arbitraria de la flash.
     *  Y el sitio donde eso ocurriria es justo el peor: un interbloqueo de
     *  parada. De ahi que se resuelva con polimorfismo de verdad.
     * -------------------------------------------------------------------- */
    virtual void start()  {}
    virtual void stop()   {}
    virtual void hold()   {}
    virtual void resume() {}
    virtual void abort(uint16_t code) { CFSM_UNUSED(code); }

    /* Se llama a TODOS los bloques cuando se activa el interbloqueo de
     * software. FsmBlock lo traduce a una alarma; no sustituye una funcion de
     * seguridad cableada. */
    virtual void onEmergencyStop() {}

    /* -----------------------------------------------------------------------
     *  IDENTIFICACION
     *  Sirve para que los mensajes de diagnostico digan "ESTACION_SOLDADURA"
     *  en lugar de "bloque 2". En AVR el nombre se guarda en memoria de
     *  programa mediante F(), de modo que cuesta 2 bytes de RAM en lugar de
     *  toda la longitud de la cadena.
     * -------------------------------------------------------------------- */
    void setName(const __FlashStringHelper* n) { _name = n; }
    const __FlashStringHelper* getName() const { return _name; }

    void setId(uint8_t id) { _id = id; }
    uint8_t getId() const  { return _id; }

    /* -----------------------------------------------------------------------
     *  HABILITACION
     *  Un bloque deshabilitado sigue registrado en el manager pero se salta en
     *  el scan. Equivale a desactivar la llamada a un FB en el OB1: util para
     *  poner fuera de servicio una estacion averiada sin recompilar, o para
     *  aislar un bloque durante la puesta en marcha.
     *
     *  Ojo: deshabilitar un bloque congela sus salidas en el ultimo valor que
     *  tuvieran. Si eso es peligroso, llama antes a stop() o a un metodo que
     *  ponga las salidas en estado seguro.
     * -------------------------------------------------------------------- */
    void enable()            { _enabled = true; }
    void disable()           { _enabled = false; }
    void setEnabled(bool e)  { _enabled = e; }
    bool isEnabled() const   { return _enabled; }

    /* -----------------------------------------------------------------------
     *  TIEMPO
     *  Cuanto lleva el bloque en su estado actual, en milisegundos. La resta
     *  se hace en aritmetica sin signo, por lo que es inmune al
     *  desbordamiento de millis() a los 49,7 dias.
     * -------------------------------------------------------------------- */
    cfsm_time_t getTimeInState() const { return cfsm_elapsed(_stateStartTime); }

    /* -----------------------------------------------------------------------
     *  DIAGNOSTICO
     *  Volcado legible del bloque. La implementacion por defecto imprime lo
     *  que sabe la clase base; los bloques hijos pueden ampliarla llamando
     *  primero a BlockBase::describe(out) y anadiendo sus propios datos.
     * -------------------------------------------------------------------- */
    virtual void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else out.print(_id);
      out.print(CFSM_FSTR("] estado="));
      out.print(getState());
      out.print(CFSM_FSTR(" t="));
      out.print(getTimeInState());
      out.print(CFSM_FSTR("ms"));
      if (!_enabled)  out.print(CFSM_FSTR(" (DESHABILITADO)"));
      if (isFaulted()) out.print(CFSM_FSTR(" (FALLO)"));
    }

  protected:
    /* Marca de tiempo de la ultima entrada al estado actual. La actualizan las
     * clases derivadas cada vez que cambian de estado. */
    cfsm_time_t _stateStartTime;

  private:
    const __FlashStringHelper* _name;
    uint8_t _id;
    bool    _enabled;
};

#endif /* COREFSM_BLOCK_BASE_H */
