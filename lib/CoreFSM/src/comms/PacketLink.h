#ifndef COREFSM_PACKET_LINK_H
#define COREFSM_PACKET_LINK_H

#include "../core/CoreFSM_Platform.h"

#ifndef CFSM_NET_MAX_PAYLOAD
  #define CFSM_NET_MAX_PAYLOAD 24
#endif
#ifndef CFSM_NET_RX_QUEUE
  #define CFSM_NET_RX_QUEUE 2
#endif
#ifndef CFSM_NET_TX_BUFFER
  #define CFSM_NET_TX_BUFFER 96
#endif

#define CFSM_NET_RAW_MAX      (CFSM_NET_MAX_PAYLOAD + 11)
#define CFSM_NET_ENCODED_MAX  (CFSM_NET_RAW_MAX + 2)

static_assert(CFSM_NET_MAX_PAYLOAD > 0 && CFSM_NET_MAX_PAYLOAD <= 242,
              "CFSM_NET_MAX_PAYLOAD debe estar entre 1 y 242");
static_assert(CFSM_NET_RX_QUEUE > 0 && CFSM_NET_RX_QUEUE <= 255,
              "CFSM_NET_RX_QUEUE debe estar entre 1 y 255");
static_assert(CFSM_NET_TX_BUFFER >= CFSM_NET_ENCODED_MAX + 1,
              "CFSM_NET_TX_BUFFER no puede contener una trama completa");

static const uint8_t CFSM_NET_VERSION = 1;
static const uint8_t CFSM_NET_BROADCAST = 0xFF;

struct CfsmNetFrame {
  uint8_t  source;
  uint8_t  destination;
  uint8_t  service;
  uint8_t  channel;
  uint8_t  sequence;
  uint16_t session;
  uint8_t  length;
  uint8_t  payload[CFSM_NET_MAX_PAYLOAD];
};

struct CfsmNetStats {
  uint32_t rxFrames;
  uint32_t txFrames;
  uint32_t crcErrors;
  uint32_t formatErrors;
  uint32_t rxOverflows;
  uint32_t rxDrops;
  uint32_t txDrops;

  CfsmNetStats()
    : rxFrames(0), txFrames(0), crcErrors(0), formatErrors(0),
      rxOverflows(0), rxDrops(0), txDrops(0) {}
};

/* Transporte de tramas sobre cualquier Stream (UART, USB serie, etc.).
 *
 * Formato interno: version, origen, destino, servicio, canal, secuencia,
 * sesion LE, longitud, payload y CRC-16-CCITT. La trama completa se codifica
 * con COBS y termina en 0x00, por lo que se resincroniza tras ruido o cortes.
 * RX y TX tienen presupuesto de bytes por scan y buffers de tamano fijo. */
class CfsmPacketTransport {
  public:
    CfsmPacketTransport(Stream& port, uint8_t localNode, uint16_t session = 0)
      : _port(port), _localNode(localNode), _session(session), _nextSequence(0),
        _rxEncodedLen(0), _discardUntilDelimiter(false),
        _rxHead(0), _rxTail(0), _rxCount(0),
        _txHead(0), _txTail(0), _txCount(0) {}

    void begin() {
      _rxEncodedLen = 0;
      _discardUntilDelimiter = false;
      _rxHead = _rxTail = _rxCount = 0;
      _txHead = _txTail = _txCount = 0;
      _nextSequence = 0;
      if (_session == 0) {
        _session = (uint16_t)((uint32_t)micros() ^
                              ((uint32_t)millis() << 5) ^
                              ((uint16_t)_localNode << 8));
        if (_session == 0) _session = 1;
      }
    }

    uint8_t localNode() const { return _localNode; }
    uint16_t session() const { return _session; }
    void setSession(uint16_t value) { _session = value ? value : 1; }
    const CfsmNetStats& stats() const { return _stats; }

    void serviceRx(uint8_t byteBudget = 32) {
      while (byteBudget-- > 0 && _port.available() > 0) {
        int value = _port.read();
        if (value < 0) break;
        uint8_t b = (uint8_t)value;

        if (_discardUntilDelimiter) {
          if (b == 0) _discardUntilDelimiter = false;
          continue;
        }

        if (b == 0) {
          if (_rxEncodedLen > 0) decodeReceivedFrame();
          _rxEncodedLen = 0;
          continue;
        }

        if (_rxEncodedLen >= CFSM_NET_ENCODED_MAX) {
          _stats.rxOverflows++;
          _rxEncodedLen = 0;
          _discardUntilDelimiter = true;
          continue;
        }
        _rxEncoded[_rxEncodedLen++] = b;
      }
    }

    void serviceTx(uint8_t byteBudget = 16) {
      while (byteBudget-- > 0 && _txCount > 0) {
        if (_port.write(_txBytes[_txTail]) == 0) break;
        _txTail = (uint16_t)((_txTail + 1U) % CFSM_NET_TX_BUFFER);
        _txCount--;
      }
    }

    bool receive(CfsmNetFrame& frame) {
      if (_rxCount == 0) return false;
      frame = _rxQueue[_rxTail];
      _rxTail = (uint8_t)((_rxTail + 1U) % CFSM_NET_RX_QUEUE);
      _rxCount--;
      return true;
    }

    bool send(uint8_t destination, uint8_t service, uint8_t channel,
              const uint8_t* payload, uint8_t length) {
      if (length > CFSM_NET_MAX_PAYLOAD || (length > 0 && payload == nullptr))
        return false;

      uint8_t raw[CFSM_NET_RAW_MAX];
      raw[0] = CFSM_NET_VERSION;
      raw[1] = _localNode;
      raw[2] = destination;
      raw[3] = service;
      raw[4] = channel;
      raw[5] = _nextSequence;
      raw[6] = (uint8_t)(_session & 0xFF);
      raw[7] = (uint8_t)(_session >> 8);
      raw[8] = length;
      if (length) memcpy(&raw[9], payload, length);
      uint16_t crc = crc16(raw, (uint8_t)(9 + length));
      raw[9 + length]  = (uint8_t)(crc & 0xFF);
      raw[10 + length] = (uint8_t)(crc >> 8);

      uint8_t encoded[CFSM_NET_ENCODED_MAX];
      uint8_t encodedLen = cobsEncode(raw, (uint8_t)(11 + length), encoded);
      uint16_t needed = (uint16_t)encodedLen + 1U;
      if (needed > (uint16_t)(CFSM_NET_TX_BUFFER - _txCount)) {
        _stats.txDrops++;
        return false;
      }
      for (uint8_t i = 0; i < encodedLen; i++) enqueueTxByte(encoded[i]);
      enqueueTxByte(0);
      _nextSequence++;
      _stats.txFrames++;
      return true;
    }

    static uint16_t crc16(const uint8_t* data, uint8_t length) {
      uint16_t crc = 0xFFFF;
      for (uint8_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
          crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                               : (uint16_t)(crc << 1);
      }
      return crc;
    }

  private:
    Stream& _port;
    uint8_t _localNode;
    uint16_t _session;
    uint8_t _nextSequence;
    CfsmNetStats _stats;

    uint8_t _rxEncoded[CFSM_NET_ENCODED_MAX];
    uint8_t _rxRaw[CFSM_NET_RAW_MAX];
    uint8_t _rxEncodedLen;
    bool _discardUntilDelimiter;

    CfsmNetFrame _rxQueue[CFSM_NET_RX_QUEUE ? CFSM_NET_RX_QUEUE : 1];
    uint8_t _rxHead, _rxTail, _rxCount;

    uint8_t _txBytes[CFSM_NET_TX_BUFFER ? CFSM_NET_TX_BUFFER : 1];
    uint16_t _txHead, _txTail, _txCount;

    void enqueueTxByte(uint8_t b) {
      _txBytes[_txHead] = b;
      _txHead = (uint16_t)((_txHead + 1U) % CFSM_NET_TX_BUFFER);
      _txCount++;
    }

    static uint8_t cobsEncode(const uint8_t* input, uint8_t length,
                              uint8_t* output) {
      uint8_t readIndex = 0;
      uint8_t writeIndex = 1;
      uint8_t codeIndex = 0;
      uint8_t code = 1;

      while (readIndex < length) {
        if (input[readIndex] == 0) {
          output[codeIndex] = code;
          code = 1;
          codeIndex = writeIndex++;
          readIndex++;
        } else {
          output[writeIndex++] = input[readIndex++];
          code++;
          if (code == 0xFF) {
            output[codeIndex] = code;
            code = 1;
            codeIndex = writeIndex++;
          }
        }
      }
      output[codeIndex] = code;
      return writeIndex;
    }

    static bool cobsDecode(const uint8_t* input, uint8_t length,
                           uint8_t* output, uint8_t& outputLength) {
      uint8_t readIndex = 0;
      uint8_t writeIndex = 0;
      while (readIndex < length) {
        uint8_t code = input[readIndex++];
        if (code == 0) return false;
        for (uint8_t i = 1; i < code; i++) {
          if (readIndex >= length || writeIndex >= CFSM_NET_RAW_MAX)
            return false;
          output[writeIndex++] = input[readIndex++];
        }
        if (code != 0xFF && readIndex < length) {
          if (writeIndex >= CFSM_NET_RAW_MAX) return false;
          output[writeIndex++] = 0;
        }
      }
      outputLength = writeIndex;
      return true;
    }

    void decodeReceivedFrame() {
      uint8_t rawLen = 0;
      if (!cobsDecode(_rxEncoded, _rxEncodedLen, _rxRaw, rawLen) || rawLen < 11) {
        _stats.formatErrors++;
        return;
      }
      uint8_t payloadLen = _rxRaw[8];
      if (_rxRaw[0] != CFSM_NET_VERSION || payloadLen > CFSM_NET_MAX_PAYLOAD ||
          rawLen != (uint8_t)(11 + payloadLen)) {
        _stats.formatErrors++;
        return;
      }
      uint16_t receivedCrc = (uint16_t)_rxRaw[9 + payloadLen] |
                             ((uint16_t)_rxRaw[10 + payloadLen] << 8);
      if (crc16(_rxRaw, (uint8_t)(9 + payloadLen)) != receivedCrc) {
        _stats.crcErrors++;
        return;
      }
      if (_rxRaw[2] != _localNode && _rxRaw[2] != CFSM_NET_BROADCAST) return;
      if (_rxCount >= CFSM_NET_RX_QUEUE) {
        _stats.rxDrops++;
        return;
      }

      CfsmNetFrame& frame = _rxQueue[_rxHead];
      frame.source      = _rxRaw[1];
      frame.destination = _rxRaw[2];
      frame.service     = _rxRaw[3];
      frame.channel     = _rxRaw[4];
      frame.sequence    = _rxRaw[5];
      frame.session     = (uint16_t)_rxRaw[6] | ((uint16_t)_rxRaw[7] << 8);
      frame.length      = payloadLen;
      if (payloadLen) memcpy(frame.payload, &_rxRaw[9], payloadLen);
      _rxHead = (uint8_t)((_rxHead + 1U) % CFSM_NET_RX_QUEUE);
      _rxCount++;
      _stats.rxFrames++;
    }
};

#endif /* COREFSM_PACKET_LINK_H */
