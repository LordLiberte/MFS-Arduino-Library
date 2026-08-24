#ifndef COREFSM_DEVICE_MANAGER_H
#define COREFSM_DEVICE_MANAGER_H

#include "IDevice.h"
#include "DigitalBackend.h"

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

template <uint8_t MAX_DEVICES = 16, uint8_t MAX_BACKENDS = 0>
class DeviceManager {
  public:
    DeviceManager()
      : _count(0), _backendCount(0), _begun(false),
        _safetyInterlock(false) {}

    bool registerDevice(IDevice* dev) {
      if (_begun || _count >= MAX_DEVICES || dev == nullptr) return false;
      for (uint8_t i = 0; i < _count; i++)
        if (_devices[i] == dev) return false;
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

    bool registerBackend(IDigitalBackend* backend) {
      if (_begun || backend == nullptr || _backendCount >= MAX_BACKENDS)
        return false;
      for (uint8_t i = 0; i < _backendCount; i++)
        if (_backends[i] == backend) return false;
      _backends[_backendCount++] = backend;
      return true;
    }

    uint8_t backendCount() const { return _backendCount; }

    bool allBackendsHealthy() const {
      for (uint8_t i = 0; i < _backendCount; i++)
        if (!_backends[i]->healthy()) return false;
      return true;
    }

    void beginAll() {
      if (_begun) return;

      /* Los dispositivos declaran primero los canales que usan. Asi un
       * backend puede configurar todo el puerto de una vez al arrancar. */
      for (uint8_t i = 0; i < _count; i++) _devices[i]->begin();
      for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->begin();

      /* Primera imagen coherente y primer volcado seguro antes de salir del
       * setup(). DigitalSensor usa esta muestra sin generar flancos falsos. */
      for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->sampleInputs();
      for (uint8_t i = 0; i < _count; i++) _devices[i]->readInputs();
      for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->commitOutputs();
      _begun = true;
    }

    /* PAE: foto de todas las entradas. */
    void readAllInputs() {
      for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->sampleInputs();
      for (uint8_t i = 0; i < _count; i++) _devices[i]->readInputs();
    }

    /* PAA: volcado de todas las salidas. */
    void writeAllOutputs() {
      if (_safetyInterlock) {
        for (uint8_t i = 0; i < _count; i++) _devices[i]->enterSafeState();
      } else {
        for (uint8_t i = 0; i < _count; i++) _devices[i]->writeOutputs();
      }
      for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->commitOutputs();
    }

    /* Corte global de software. El flanco de activacion se vuelca en el acto,
     * sin esperar a la siguiente vuelta del loop. Liberarlo no recupera las
     * ordenes anteriores: cada salida conserva su valor seguro configurado
     * hasta recibir una orden nueva. */
    void setSafetyInterlock(bool active) {
      if (active && !_safetyInterlock) {
        for (uint8_t i = 0; i < _count; i++) _devices[i]->enterSafeState();
        for (uint8_t i = 0; i < _backendCount; i++) _backends[i]->commitOutputs();
      }
      _safetyInterlock = active;
    }

    bool isSafetyInterlocked() const { return _safetyInterlock; }

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
    IDevice* _devices[MAX_DEVICES ? MAX_DEVICES : 1];
    IDigitalBackend* _backends[MAX_BACKENDS ? MAX_BACKENDS : 1];
    uint8_t  _count;
    uint8_t  _backendCount;
    bool     _begun;
    bool     _safetyInterlock;
};

#endif /* COREFSM_DEVICE_MANAGER_H */
