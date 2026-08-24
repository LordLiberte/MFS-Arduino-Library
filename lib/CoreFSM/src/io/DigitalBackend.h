#ifndef COREFSM_DIGITAL_BACKEND_H
#define COREFSM_DIGITAL_BACKEND_H

#include "../core/CoreFSM_Platform.h"

/* Backend para GPIO digital agrupado (expansores, registros o E/S remota).
 * read() y write() trabajan sobre una imagen en memoria; sampleInputs() y
 * commitOutputs() realizan como maximo una transaccion de bus por scan. */
class IDigitalBackend {
  public:
    virtual ~IDigitalBackend() {}

    virtual void configure(uint8_t channel, uint8_t mode) = 0;
    virtual bool read(uint8_t channel) const = 0;
    virtual void write(uint8_t channel, bool level) = 0;

    virtual void begin() {}
    virtual void sampleInputs() {}
    virtual void commitOutputs() {}
    virtual bool healthy() const { return true; }
};

/* Referencia a un pin nativo o a un canal de un backend. El puntero nulo es
 * deliberado: conserva la ruta directa y barata de Arduino para GPIO local. */
struct DigitalPin {
  IDigitalBackend* backend;
  uint8_t channel;

  DigitalPin(uint8_t nativePin) : backend(nullptr), channel(nativePin) {}
  DigitalPin(IDigitalBackend& provider, uint8_t providerChannel)
    : backend(&provider), channel(providerChannel) {}

  bool isNative() const { return backend == nullptr; }

  void configure(uint8_t mode) const {
    if (backend) backend->configure(channel, mode);
    else         pinMode(channel, mode);
  }

  bool read() const {
    return backend ? backend->read(channel) : (digitalRead(channel) == HIGH);
  }

  void write(bool level) const {
    if (backend) backend->write(channel, level);
    else         digitalWrite(channel, level ? HIGH : LOW);
  }
};

#endif /* COREFSM_DIGITAL_BACKEND_H */
