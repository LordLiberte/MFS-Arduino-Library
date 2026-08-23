#ifndef COREFSM_DEVICE_MANAGER_H
#define COREFSM_DEVICE_MANAGER_H

#include "IDevice.h"

/* ===========================================================================
 *  DeviceManager.h  -  Imagen de proceso (PAE / PAA)
 * ---------------------------------------------------------------------------
 *  Guarda todos los objetos de campo y ejecuta sus fases de lectura y
 *  escritura en bloque. Es lo que convierte un loop() lleno de digitalRead y
 *  digitalWrite en el ciclo limpio de tres lineas de un automata:
 *
 *      void loop() {
 *        HW.readInputs();      // PAE
 *        manager.updateAll();  // OB1
 *        HW.writeOutputs();    // PAA
 *      }
 *
 *  Igual que BlockManager, usa un array estatico dimensionado en tiempo de
 *  compilacion: cero memoria dinamica, consumo de RAM conocido de antemano.
 * ======================================================================== */

template <uint8_t MAX_DEVICES = 16>
class DeviceManager {
  public:
    DeviceManager() : _count(0) {}

    bool registerDevice(IDevice* dev) {
      if (_count >= MAX_DEVICES || dev == nullptr) return false;
      _devices[_count++] = dev;
      return true;
    }

    bool registerDevice(IDevice* dev, const __FlashStringHelper* name) {
      if (!registerDevice(dev)) return false;
      dev->setName(name);
      return true;
    }

    uint8_t  count() const { return _count; }
    IDevice* at(uint8_t i) const { return (i < _count) ? _devices[i] : nullptr; }

    void beginAll() {
      for (uint8_t i = 0; i < _count; i++) _devices[i]->begin();
    }

    /* PAE: foto de todas las entradas. */
    void readAllInputs() {
      for (uint8_t i = 0; i < _count; i++) _devices[i]->readInputs();
    }

    /* PAA: volcado de todas las salidas. */
    void writeAllOutputs() {
      for (uint8_t i = 0; i < _count; i++) _devices[i]->writeOutputs();
    }

    /* Quita todos los forzados de golpe. Ponlo en el arranque y asocialo a un
     * comando de mantenimiento: es la red de seguridad contra el forzado que
     * alguien se dejo puesto el viernes por la tarde. */
    void releaseAllForces() {
      for (uint8_t i = 0; i < _count; i++) _devices[i]->releaseForce();
    }

    bool hasAnyForce() const {
      for (uint8_t i = 0; i < _count; i++) if (_devices[i]->isForced()) return true;
      return false;
    }

  private:
    IDevice* _devices[MAX_DEVICES];
    uint8_t  _count;
};

#endif /* COREFSM_DEVICE_MANAGER_H */
