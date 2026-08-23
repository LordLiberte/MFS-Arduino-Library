#ifndef COREFSM_DATA_BLOCK_H
#define COREFSM_DATA_BLOCK_H

#include "ConfigStore.h"

/* ===========================================================================
 *  DataBlock.h  -  Bloque de datos global (el DB de un automata)
 * ---------------------------------------------------------------------------
 *  QUE ES UN DB
 *  ------------
 *  En un automata Siemens, un DB (Datenbaustein) es una estructura de datos
 *  global a la que puede acceder cualquier bloque del programa. Sirve para dos
 *  cosas: compartir informacion entre bloques sin cablearla parametro a
 *  parametro, y guardar datos que deben sobrevivir al apagado (los DB
 *  "remanentes").
 *
 *  DataBlock<T> es eso mismo en C++: una estructura tuya, accesible desde
 *  donde quieras, que ademas sabe guardarse sola en memoria no volatil.
 *
 *  LA REMANENCIA NO ES GRATIS
 *  --------------------------
 *  Que un dato sobreviva al apagado suena bien y da la tentacion de marcarlo
 *  todo como remanente. Pero cada escritura desgasta la memoria (unos 100.000
 *  ciclos por celda), y algunos datos NO deben sobrevivir: si el paso actual
 *  de la secuencia se conservara, la maquina arrancaria a mitad de un ciclo
 *  tras un corte de luz, con la pieza a saber donde y los actuadores en
 *  posiciones desconocidas. Eso es exactamente lo que no quieres.
 *
 *  Criterio practico de que merece ser remanente:
 *
 *    SI   contadores de produccion, cuentahoras, historico de averias,
 *         calibraciones, receta seleccionada, totales de turno.
 *    NO   paso actual, estado de la maquina, temporizadores en curso,
 *         valores instantaneos de sensores, ordenes pendientes.
 *
 *  La regla es: si el dato describe DONDE ESTA la maquina ahora, no debe
 *  sobrevivir. Si describe CUANTO LLEVA HECHO o COMO ESTA AJUSTADA, si.
 *
 *  AUTOGUARDADO
 *  ------------
 *  autoSave() escribe como mucho una vez cada minSaveIntervalMs, y solo si el
 *  contenido cambio. Con el intervalo por defecto de 60 segundos, una maquina
 *  trabajando sin parar haria unas 500.000 escrituras en un ano: al limite.
 *  Por eso conviene, ademas, llamar a save() explicitamente en los momentos
 *  buenos (fin de turno, parada ordenada) y dejar el autoguardado solo como
 *  red de seguridad frente a un corte de tension.
 * ======================================================================== */

template <typename T, uint16_t VERSION = 1, uint16_t ADDRESS = 0>
class DataBlock {
  public:
    /* Acceso directo, igual que a un DB global. */
    T data;

    DataBlock() : _lastSave(0), _minInterval(60000) {}

    /* Prepara la memoria y recupera lo guardado. Si no hay nada valido, deja
     * los valores por defecto que tuviera la estructura. */
    CfsmStoreResult begin(bool loadFromNvm = true) {
      _store.begin();
      if (!loadFromNvm) return CFSM_STORE_EMPTY;
      CfsmStoreResult r = _store.load();
      if (r == CFSM_STORE_OK) data = _store.data;
      _snapshotCrc = crcOfData();
      return r;
    }

    /* Guardado inmediato e incondicional. */
    bool save() {
      _store.data = data;
      bool ok = _store.save();
      if (ok) { _snapshotCrc = crcOfData(); _lastSave = cfsm_millis(); }
      return ok;
    }

    /* Guardado perezoso: solo si cambio algo y ha pasado el intervalo minimo.
     * Se puede llamar en cada vuelta del scan sin miedo. */
    bool autoSave() {
      if (cfsm_elapsed(_lastSave) < _minInterval) return false;
      if (crcOfData() == _snapshotCrc) { _lastSave = cfsm_millis(); return false; }
      return save();
    }

    /* Hay cambios sin guardar? */
    bool isDirty() const { return crcOfData() != _snapshotCrc; }

    /* Descarta los cambios en RAM y recarga lo ultimo guardado. */
    bool revert() {
      if (_store.load() != CFSM_STORE_OK) return false;
      data = _store.data;
      _snapshotCrc = crcOfData();
      return true;
    }

    /* Borra la copia persistente: el proximo arranque usara los valores de
     * fabrica del programa. */
    bool factoryReset() { return _store.erase(); }

    void setMinSaveInterval(cfsm_time_t ms) { _minInterval = ms; }
    CfsmStoreResult lastResult() const { return _store.lastResult(); }
    const __FlashStringHelper* resultText() const { return _store.resultText(); }

    static uint16_t footprint()   { return ConfigStore<T, VERSION, ADDRESS>::footprint(); }
    static uint16_t nextAddress() { return ConfigStore<T, VERSION, ADDRESS>::nextAddress(); }

  private:
    ConfigStore<T, VERSION, ADDRESS> _store;
    uint16_t    _snapshotCrc = 0;
    cfsm_time_t _lastSave;
    cfsm_time_t _minInterval;

    uint16_t crcOfData() const { return cfsmCrc16((const uint8_t*)&data, sizeof(T)); }
};

#endif /* COREFSM_DATA_BLOCK_H */
