#ifndef COREFSM_REMOTE_IO_H
#define COREFSM_REMOTE_IO_H

#include "PacketLink.h"
#include "../io/DigitalBackend.h"

static const uint8_t CFSM_NET_SERVICE_DIGITAL_IMAGE = 0x10;

class ICfsmNetworkEndpoint {
  public:
    virtual ~ICfsmNetworkEndpoint() {}
    virtual void begin() {}
    virtual bool accepts(const CfsmNetFrame& frame) const = 0;
    virtual void onFrame(const CfsmNetFrame& frame) = 0;
    virtual void tick() {}
    virtual void publish(CfsmPacketTransport& transport) = 0;
};

/* Distribuye las tramas de un unico transporte entre varios servicios o
 * nodos remotos. No reserva memoria y limita el trabajo de cada scan. */
template <uint8_t MAX_ENDPOINTS = 4>
class CfsmNetworkManager {
  public:
    explicit CfsmNetworkManager(CfsmPacketTransport& transport)
      : _transport(transport), _count(0), _begun(false) {}

    bool attach(ICfsmNetworkEndpoint* endpoint) {
      if (_begun || endpoint == nullptr || _count >= MAX_ENDPOINTS) return false;
      for (uint8_t i = 0; i < _count; i++)
        if (_endpoints[i] == endpoint) return false;
      _endpoints[_count++] = endpoint;
      return true;
    }

    void begin() {
      if (_begun) return;
      _transport.begin();
      for (uint8_t i = 0; i < _count; i++) _endpoints[i]->begin();
      _begun = true;
    }

    /* PAE de red: recibir y actualizar snapshots antes de la logica. */
    void readInputs(uint8_t byteBudget = 32) {
      _transport.serviceRx(byteBudget);
      CfsmNetFrame frame;
      while (_transport.receive(frame)) {
        for (uint8_t i = 0; i < _count; i++) {
          if (_endpoints[i]->accepts(frame)) {
            _endpoints[i]->onFrame(frame);
            break;
          }
        }
      }
      for (uint8_t i = 0; i < _count; i++) _endpoints[i]->tick();
    }

    /* PAA de red: publicar snapshots y vaciar una cantidad acotada de TX. */
    void writeOutputs(uint8_t byteBudget = 16) {
      for (uint8_t i = 0; i < _count; i++)
        _endpoints[i]->publish(_transport);
      _transport.serviceTx(byteBudget);
    }

    uint8_t count() const { return _count; }
    CfsmPacketTransport& transport() { return _transport; }

  private:
    CfsmPacketTransport& _transport;
    ICfsmNetworkEndpoint* _endpoints[MAX_ENDPOINTS ? MAX_ENDPOINTS : 1];
    uint8_t _count;
    bool _begun;
};

/* Imagen digital bidireccional de un nodo remoto.
 *
 * Cada placa publica sus bytes TX y recibe los bytes TX de la otra como RX.
 * Al implementar IDigitalBackend, DigitalSensor y DigitalOutput pueden usar
 * sus bits exactamente igual que un GPIO local. Si vence el timeout, read()
 * devuelve el valor seguro configurado y healthy() pasa a false. */
template <uint8_t BYTES>
class RemoteDigitalBackend : public IDigitalBackend,
                             public ICfsmNetworkEndpoint {
  public:
    RemoteDigitalBackend(uint8_t peerNode, uint8_t channel = 0,
                         cfsm_time_t timeoutMs = 500,
                         cfsm_time_t snapshotMs = 100)
      : _peerNode(peerNode), _channel(channel), _timeoutMs(timeoutMs),
        _snapshotMs(snapshotMs), _lastRx(0), _lastTx(0),
        _peerSession(0), _lastSequence(0), _valid(false),
        _haveSequence(false), _peerRestarted(false), _dirty(true),
        _configOk(true), _duplicates(0), _outOfOrder(0), _timeouts(0) {
      static_assert(BYTES > 0, "RemoteDigitalBackend necesita al menos un byte");
      static_assert(BYTES <= 32,
                    "RemoteDigitalBackend admite como maximo 256 bits");
      static_assert(BYTES <= CFSM_NET_MAX_PAYLOAD,
                    "La imagen remota supera CFSM_NET_MAX_PAYLOAD");
      memset(_rx, 0, BYTES);
      memset(_tx, 0, BYTES);
      memset(_safeRx, 0, BYTES);
      memset(_inputMask, 0, BYTES);
      memset(_outputMask, 0, BYTES);
    }

    void configure(uint8_t bit, uint8_t mode) override {
      if (!validBit(bit)) { _configOk = false; return; }
      if (mode == OUTPUT) setArrayBit(_outputMask, bit, true);
      else                setArrayBit(_inputMask, bit, true);
    }

    bool read(uint8_t bit) const override {
      if (!validBitConst(bit)) return false;
      return getArrayBit(_valid ? _rx : _safeRx, bit);
    }

    void write(uint8_t bit, bool level) override {
      if (!validBit(bit)) { _configOk = false; return; }
      bool old = getArrayBit(_tx, bit);
      if (old != level) {
        setArrayBit(_tx, bit, level);
        _dirty = true;
      }
    }

    void begin() override {
      memset(_rx, 0, BYTES);
      _lastRx = _lastTx = cfsm_millis();
      _peerSession = 0;
      _haveSequence = false;
      _peerRestarted = false;
      _valid = false;
      _dirty = true;
    }

    void sampleInputs() override { updateTimeout(); }
    void commitOutputs() override { /* NetworkManager publica despues. */ }
    bool healthy() const override { return _configOk && _valid; }

    bool accepts(const CfsmNetFrame& frame) const override {
      return frame.source == _peerNode &&
             frame.service == CFSM_NET_SERVICE_DIGITAL_IMAGE &&
             frame.channel == _channel && frame.length == BYTES;
    }

    void onFrame(const CfsmNetFrame& frame) override {
      if (_peerSession != 0 && frame.session != _peerSession) {
        _peerRestarted = true;
      } else if (_haveSequence) {
        uint8_t distance = (uint8_t)(frame.sequence - _lastSequence);
        if (distance == 0) { _duplicates++; return; }
        if (distance > 127) { _outOfOrder++; return; }
      }

      memcpy(_rx, frame.payload, BYTES);
      _peerSession = frame.session;
      _lastSequence = frame.sequence;
      _haveSequence = true;
      _lastRx = cfsm_millis();
      _valid = true;
    }

    void tick() override { updateTimeout(); }

    void publish(CfsmPacketTransport& transport) override {
      if (!_dirty && cfsm_elapsed(_lastTx) < _snapshotMs) return;
      if (transport.send(_peerNode, CFSM_NET_SERVICE_DIGITAL_IMAGE,
                         _channel, _tx, BYTES)) {
        _dirty = false;
        _lastTx = cfsm_millis();
      }
    }

    bool input(uint8_t bit) const { return read(bit); }
    void output(uint8_t bit, bool value) { write(bit, value); }

    void setSafeInput(uint8_t bit, bool value) {
      if (validBit(bit)) setArrayBit(_safeRx, bit, value);
    }

    void forceSnapshot() { _dirty = true; }
    bool linkOk() const { return _valid; }
    cfsm_time_t age() const { return cfsm_elapsed(_lastRx); }
    uint16_t peerSession() const { return _peerSession; }

    bool consumePeerRestarted() {
      bool value = _peerRestarted;
      _peerRestarted = false;
      return value;
    }

    uint32_t duplicates() const { return _duplicates; }
    uint32_t outOfOrder() const { return _outOfOrder; }
    uint32_t timeouts() const { return _timeouts; }

  private:
    uint8_t _peerNode;
    uint8_t _channel;
    cfsm_time_t _timeoutMs;
    cfsm_time_t _snapshotMs;
    cfsm_time_t _lastRx;
    cfsm_time_t _lastTx;
    uint16_t _peerSession;
    uint8_t _lastSequence;
    bool _valid;
    bool _haveSequence;
    bool _peerRestarted;
    bool _dirty;
    bool _configOk;
    uint32_t _duplicates;
    uint32_t _outOfOrder;
    uint32_t _timeouts;
    uint8_t _rx[BYTES];
    uint8_t _tx[BYTES];
    uint8_t _safeRx[BYTES];
    uint8_t _inputMask[BYTES];
    uint8_t _outputMask[BYTES];

    bool validBit(uint8_t bit) { return bit < (uint16_t)BYTES * 8U; }
    bool validBitConst(uint8_t bit) const { return bit < (uint16_t)BYTES * 8U; }

    static bool getArrayBit(const uint8_t* data, uint8_t bit) {
      return (data[bit >> 3] & (uint8_t)(1U << (bit & 7))) != 0;
    }

    static void setArrayBit(uint8_t* data, uint8_t bit, bool value) {
      uint8_t mask = (uint8_t)(1U << (bit & 7));
      if (value) data[bit >> 3] |= mask;
      else       data[bit >> 3] &= (uint8_t)~mask;
    }

    void updateTimeout() {
      if (_valid && _timeoutMs > 0 && cfsm_elapsed(_lastRx) > _timeoutMs) {
        _valid = false;
        /* Tras una perdida de enlace no conocemos cuantos snapshots emitio el
         * peer ni si reinicio conservando la misma sesion. La primera trama
         * valida reconstruye la referencia de secuencia. */
        _haveSequence = false;
        _timeouts++;
      }
    }
};

#endif /* COREFSM_REMOTE_IO_H */
