#ifndef COREFSM_IO_MANAGER_H
#define COREFSM_IO_MANAGER_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  IOManager.h  -  Imagen de proceso por punteros (via ligera)
 * ---------------------------------------------------------------------------
 *  CUANDO USAR ESTO Y CUANDO USAR IOTable
 *  --------------------------------------
 *  Son dos formas de resolver lo mismo con distinto equilibrio:
 *
 *    IOTable + CfsmHardware  (recomendado)
 *      Cada senal es un objeto completo con antirrebote, flancos y forzado.
 *      Cuesta unos 12-16 bytes de RAM por senal. Es lo que quieres en el 90%
 *      de los casos, y lo que genera el script de Wokwi.
 *
 *    IOManager  (esta clase)
 *      Vincula un pin directamente a la direccion de memoria de un bool que ya
 *      existe dentro de tus bloques. Cuesta 4 bytes por senal y no aporta
 *      antirrebote ni flancos. Util cuando vas justo de RAM en un ATmega328,
 *      cuando las senales vienen de sensores electronicos que no rebotan, o
 *      cuando quieres mapear a mano variables que ya tienes declaradas.
 *
 *  COMO FUNCIONA
 *  -------------
 *      io.mapInput(2, &estacion.pulsadorMarcha);
 *
 *  El operador & obtiene la direccion en RAM de esa variable concreta dentro
 *  de ese objeto concreto. IOManager guarda la pareja [pin, direccion]. En
 *  cada readInputs() recorre la tabla, lee el pin y escribe el resultado
 *  directamente en esa direccion. Tu bloque encuentra la variable ya
 *  actualizada sin que nadie se la haya pasado explicitamente: es exactamente
 *  lo que hace la imagen de proceso de un automata.
 *
 *  AVISO SOBRE PUNTEROS COLGANTES
 *  ------------------------------
 *  Los objetos apuntados deben vivir tanto como el IOManager. Mapea siempre
 *  variables de objetos globales (declarados fuera de cualquier funcion). Si
 *  mapeas una variable local de setup(), al salir de setup() esa memoria se
 *  reutiliza y el IOManager estara escribiendo encima de la pila. El sintoma
 *  es un programa que se comporta de forma aleatoria e imposible de depurar.
 * ======================================================================== */

struct InputMapping {
  uint8_t pin;
  bool*   target;
  bool    activeLow;
};

struct OutputMapping {
  uint8_t     pin;
  const bool* source;
  bool        activeLow;
};

template <uint8_t MAX_INPUTS = 8, uint8_t MAX_OUTPUTS = 8>
class IOManager {
  public:
    IOManager() : _inCount(0), _outCount(0) {}

    /* activeLow = true para el cableado estandar de conmutacion a masa. */
    bool mapInput(uint8_t pin, bool* target, bool activeLow = true) {
      if (_inCount >= MAX_INPUTS || target == nullptr) return false;
      _inputs[_inCount++] = { pin, target, activeLow };
      return true;
    }

    /* activeLow = true para modulos de rele que se activan con nivel bajo. */
    bool mapOutput(uint8_t pin, const bool* source, bool activeLow = false) {
      if (_outCount >= MAX_OUTPUTS || source == nullptr) return false;
      _outputs[_outCount++] = { pin, source, activeLow };
      return true;
    }

    void begin() {
      for (uint8_t i = 0; i < _inCount; i++)
        pinMode(_inputs[i].pin, _inputs[i].activeLow ? INPUT_PULLUP : INPUT);
      for (uint8_t i = 0; i < _outCount; i++) {
        pinMode(_outputs[i].pin, OUTPUT);
        digitalWrite(_outputs[i].pin, _outputs[i].activeLow ? HIGH : LOW);
      }
    }

    /* PAE */
    void readInputs() {
      for (uint8_t i = 0; i < _inCount; i++) {
        bool level = (digitalRead(_inputs[i].pin) == LOW);
        *(_inputs[i].target) = _inputs[i].activeLow ? level : !level;
      }
    }

    /* PAA */
    void writeOutputs() {
      for (uint8_t i = 0; i < _outCount; i++) {
        bool v = *(_outputs[i].source);
        digitalWrite(_outputs[i].pin, (v != _outputs[i].activeLow) ? HIGH : LOW);
      }
    }

    uint8_t inputCount()  const { return _inCount;  }
    uint8_t outputCount() const { return _outCount; }

  private:
    InputMapping  _inputs[MAX_INPUTS];
    OutputMapping _outputs[MAX_OUTPUTS];
    uint8_t       _inCount;
    uint8_t       _outCount;
};

#endif /* COREFSM_IO_MANAGER_H */
