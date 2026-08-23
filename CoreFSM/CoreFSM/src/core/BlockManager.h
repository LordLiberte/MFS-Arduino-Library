#ifndef COREFSM_BLOCK_MANAGER_H
#define COREFSM_BLOCK_MANAGER_H

#include "BlockBase.h"
#include "FsmBlock.h"

/* ===========================================================================
 *  BlockManager.h  -  Orquestador del ciclo de scan (el OB1 de tu maquina)
 * ---------------------------------------------------------------------------
 *  QUE HACE
 *  --------
 *  Guarda punteros a todos los bloques de logica y los ejecuta en orden en
 *  cada vuelta del loop(). Es el equivalente exacto del OB1 de un automata
 *  Siemens: el bloque de organizacion que el firmware llama ciclicamente y
 *  que a su vez llama a todos tus bloques de funcion.
 *
 *  POR QUE UN ARRAY DE PUNTEROS Y NO new/delete
 *  --------------------------------------------
 *  El array es de tamano fijo, decidido en tiempo de compilacion mediante el
 *  parametro de plantilla. Nunca se reserva ni se libera memoria en marcha.
 *
 *  Esto no es purismo: en un AVR con 2 KB de RAM, usar new/malloc en un
 *  programa que corre durante meses acaba fragmentando el monton hasta que
 *  una reserva falla y el programa se comporta de forma erratica, casi
 *  siempre de madrugada y con la maquina en produccion. En control industrial
 *  la memoria dinamica sencillamente no se usa. Todo estatico, todo acotado,
 *  todo predecible.
 *
 *  EL ORDEN DE REGISTRO IMPORTA
 *  ----------------------------
 *  Los bloques se ejecutan en el orden en que se registraron, y dentro de un
 *  mismo scan un bloque ve los valores que dejo el anterior. Si la estacion B
 *  lee una salida de la estacion A, registrar A antes que B hace que B trabaje
 *  con el dato de este ciclo; registrarla despues hace que trabaje con el del
 *  ciclo anterior. Un ciclo de retardo casi nunca importa, pero conviene
 *  saberlo cuando algo va un pelo desfasado.
 *
 *  MEDICION DEL TIEMPO DE CICLO
 *  ----------------------------
 *  El manager cronometra cada scan. Es el mismo dato que muestra un PLC en su
 *  pantalla de diagnostico y sirve para lo mismo: si el tiempo maximo de ciclo
 *  se dispara, hay algo que bloquea (un delay, un Serial saturado, un bucle
 *  de espera) y la maquina esta perdiendo reactividad.
 * ======================================================================== */

template <uint8_t MAX_BLOCKS = 8>
class BlockManager {
  public:
    BlockManager() : _count(0), _scanCount(0),
                     _lastScanUs(0), _maxScanUs(0), _minScanUs(0xFFFFFFFF),
                     _emergencyStop(false) {}

    /* -----------------------------------------------------------------------
     *  REGISTRO
     *  Devuelve false si el array esta lleno o el puntero es nulo. Conviene
     *  comprobarlo en el setup(): un bloque que no se registra simplemente no
     *  se ejecuta nunca, y es un fallo silencioso muy incomodo de localizar.
     * -------------------------------------------------------------------- */
    bool registerBlock(BlockBase* block) {
      if (_count >= MAX_BLOCKS || block == nullptr) return false;
      block->setId(_count);
      _blocks[_count++] = block;
      return true;
    }

    bool registerBlock(BlockBase* block, const __FlashStringHelper* name) {
      if (!registerBlock(block)) return false;
      block->setName(name);
      return true;
    }

    uint8_t    count() const { return _count; }
    BlockBase* at(uint8_t i) const { return (i < _count) ? _blocks[i] : nullptr; }

    /* -----------------------------------------------------------------------
     *  CICLO DE VIDA
     * -------------------------------------------------------------------- */
    void beginAll() {
      for (uint8_t i = 0; i < _count; i++) _blocks[i]->begin();
      _scanStartUs = micros();
    }

    /* El scan propiamente dicho. Se llama una vez por vuelta del loop(),
     * entre la lectura de entradas (PAE) y la escritura de salidas (PAA). */
    void updateAll() {
      uint32_t t0 = micros();

      if (_emergencyStop) {
        /* Con la seta pulsada no se ejecuta NADA de logica de proceso: se
         * avisa a todos los bloques y punto. Cualquier otra cosa seria dejar
         * correr codigo que podria mover un actuador.
         *
         * ESTO IMPORTA Y CONVIENE TENERLO PRESENTE: como update() no se
         * ejecuta, las salidas se quedan congeladas en su ultimo valor. Quien
         * las lleva a estado seguro es el hook onTransition(-> STATE_ERROR) de
         * cada bloque, que si se ejecuta (una sola vez) al declararse la
         * alarma. Si tu bloque gobierna algo peligroso, apagalo ahi. */
        for (uint8_t i = 0; i < _count; i++) _blocks[i]->onEmergencyStop();
      } else {
        for (uint8_t i = 0; i < _count; i++) {
          if (_blocks[i]->isEnabled()) _blocks[i]->update();
        }
      }

      /* Estadisticas de tiempo de ciclo. */
      _lastScanUs = micros() - t0;
      if (_lastScanUs > _maxScanUs) _maxScanUs = _lastScanUs;
      if (_lastScanUs < _minScanUs) _minScanUs = _lastScanUs;
      _scanCount++;
    }

    /* -----------------------------------------------------------------------
     *  COMANDOS EN DIFUSION
     *  Actuan sobre todos los bloques a la vez, por llamada virtual. Un bloque
     *  que herede directamente de BlockBase simplemente los ignora, sin que
     *  eso rompa nada.
     * -------------------------------------------------------------------- */
    void startAll() { for (uint8_t i=0;i<_count;i++) _blocks[i]->start();  }
    void stopAll()  { for (uint8_t i=0;i<_count;i++) _blocks[i]->stop();   }
    void holdAll()  { for (uint8_t i=0;i<_count;i++) _blocks[i]->hold();   }
    void resumeAll(){ for (uint8_t i=0;i<_count;i++) _blocks[i]->resume(); }
    void resetAll() { for (uint8_t i=0;i<_count;i++) _blocks[i]->reset();  }

    /* -----------------------------------------------------------------------
     *  PARADA DE EMERGENCIA
     *  Se engancha a la seta fisica. Mientras este activa, ningun bloque
     *  ejecuta logica y todos quedan en fallo. Liberarla NO rearranca nada:
     *  hay que rearmar explicitamente, que es exactamente lo que exige la
     *  normativa de seguridad de maquinas (una seta liberada jamas debe
     *  provocar un rearranque automatico).
     * -------------------------------------------------------------------- */
    void setEmergencyStop(bool active) { _emergencyStop = active; }
    bool isEmergencyStop() const       { return _emergencyStop; }

    /* -----------------------------------------------------------------------
     *  DIAGNOSTICO GLOBAL
     * -------------------------------------------------------------------- */
    bool hasAnyFault() const {
      for (uint8_t i = 0; i < _count; i++) if (_blocks[i]->isFaulted()) return true;
      return false;
    }

    /* Primer bloque en fallo, para saber quien disparo la alarma. */
    BlockBase* firstFaulted() const {
      for (uint8_t i = 0; i < _count; i++) if (_blocks[i]->isFaulted()) return _blocks[i];
      return nullptr;
    }

    bool allIdle() const {
      for (uint8_t i = 0; i < _count; i++)
        if (_blocks[i]->getState() != STATE_IDLE) return false;
      return true;
    }

    /* Estadisticas del ciclo de scan, en microsegundos. */
    uint32_t lastScanTimeUs() const { return _lastScanUs; }
    uint32_t maxScanTimeUs()  const { return _maxScanUs;  }
    uint32_t minScanTimeUs()  const { return _minScanUs == 0xFFFFFFFF ? 0 : _minScanUs; }
    uint32_t scanCount()      const { return _scanCount;  }
    void     resetScanStats() { _maxScanUs = 0; _minScanUs = 0xFFFFFFFF; }

    /* Tabla de observacion completa, al estilo de la watch table de TIA Portal.
     * Llamala desde el .ino solo de vez en cuando (por ejemplo al recibir un
     * caracter por el puerto serie), nunca en cada vuelta del scan. */
    void printWatchTable(Print& out) const {
      out.println(CFSM_FSTR("---- TABLA DE OBSERVACION ----"));
      for (uint8_t i = 0; i < _count; i++) {
        out.print(' ');
        _blocks[i]->describe(out);
        out.println();
      }
      out.print(CFSM_FSTR(" scan: ult="));   out.print(_lastScanUs);
      out.print(CFSM_FSTR("us max="));       out.print(_maxScanUs);
      out.print(CFSM_FSTR("us n="));         out.println(_scanCount);
      if (_emergencyStop) out.println(CFSM_FSTR(" *** SETA DE EMERGENCIA ACTIVA ***"));
      out.println(CFSM_FSTR("------------------------------"));
    }

  private:
    BlockBase* _blocks[MAX_BLOCKS];
    uint8_t    _count;

    uint32_t   _scanCount;
    uint32_t   _scanStartUs = 0;
    uint32_t   _lastScanUs;
    uint32_t   _maxScanUs;
    uint32_t   _minScanUs;

    bool       _emergencyStop;
};

#endif /* COREFSM_BLOCK_MANAGER_H */
