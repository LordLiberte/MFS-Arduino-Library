#ifndef COREFSM_MCP23017_BACKEND_H
#define COREFSM_MCP23017_BACKEND_H

#include "DigitalBackend.h"
#include <Wire.h>

/* Backend sin dependencias externas para el expansor I2C MCP23017.
 *
 * - hasta 16 canales por chip;
 * - direcciones 0x20..0x27;
 * - una captura GPIOA/GPIOB por PAE;
 * - un volcado OLATA/OLATB solo cuando cambia alguna salida.
 *
 * No sirve para PWM, encoders rapidos, step/dir ni funciones de seguridad.
 */
class Mcp23017Backend : public IDigitalBackend {
  public:
    Mcp23017Backend(TwoWire& wire = Wire, uint8_t address = 0x20,
                    bool manageWire = true)
      : _wire(wire), _address(address), _manageWire(manageWire),
        _directions(0xFFFF), _pullups(0), _inputs(0), _outputs(0),
        _begun(false), _configDirty(true), _outputsDirty(true),
        _ioconReady(false), _latchPrimed(false),
        _configOk(address >= 0x20 && address <= 0x27),
        _readHealthy(false), _writeHealthy(false),
        _errors((address >= 0x20 && address <= 0x27) ? 0UL : 1UL) {}

    void configure(uint8_t channel, uint8_t mode) override {
      if (!validChannel(channel)) return;
      const uint16_t mask = (uint16_t)1U << channel;
      if (mode == OUTPUT) {
        _directions &= (uint16_t)~mask;
        _pullups    &= (uint16_t)~mask;
      } else {
        _directions |= mask;
        if (mode == INPUT_PULLUP) _pullups |= mask;
        else                      _pullups &= (uint16_t)~mask;
      }
      _configDirty = true;
    }

    bool read(uint8_t channel) const override {
      if (channel >= 16) return false;
      return (_inputs & ((uint16_t)1U << channel)) != 0;
    }

    void write(uint8_t channel, bool level) override {
      if (!validChannel(channel)) return;
      const uint16_t mask = (uint16_t)1U << channel;
      const uint16_t next = level ? (_outputs | mask)
                                  : (_outputs & (uint16_t)~mask);
      if (next != _outputs) {
        _outputs = next;
        _outputsDirty = true;
      }
    }

    void begin() override {
      if (_manageWire) _wire.begin();
      _begun = true;
      /* begin() tambien sirve para recuperar un expansor reiniciado sin
       * reiniciar el MCU: obliga a restaurar mapa, latch, direccion y pull-up
       * aunque las sombras de RAM ya figurasen como aplicadas. */
      _ioconReady = false;
      _configDirty = true;
      _outputsDirty = true;
      _latchPrimed = false;
      bool configOk = ensureSafeConfiguration();
      _writeHealthy = configOk;
      uint16_t first = _inputs;
      bool readOk = readPair(REG_GPIOA, first);
      _readHealthy = configOk && readOk;
      if (readOk) _inputs = first;
    }

    void sampleInputs() override {
      if (!_begun) return;
      bool ok = ensureSafeConfiguration();
      uint16_t sample = _inputs;
      ok = readPair(REG_GPIOA, sample) && ok;
      if (ok) _inputs = sample;
      _readHealthy = ok;
    }

    void commitOutputs() override {
      if (!_begun) return;
      bool ok = ensureSafeConfiguration();
      if (_outputsDirty) {
        bool wrote = writePair(REG_OLATA, _outputs);
        if (wrote) _outputsDirty = false;
        ok = wrote && ok;
      }
      _writeHealthy = ok;
    }

    bool healthy() const override {
      return _configOk && _readHealthy && _writeHealthy;
    }
    uint32_t errorCount() const { return _errors; }
    uint8_t address() const { return _address; }
    uint16_t inputImage() const { return _inputs; }
    uint16_t outputImage() const { return _outputs; }

  private:
    enum Register : uint8_t {
      REG_IODIRA = 0x00,
      REG_GPINTENA = 0x04,
      REG_IOCON_BANK1_A = 0x05,
      REG_IOCON_BANK0_A = 0x0A,
      REG_GPPUA  = 0x0C,
      REG_GPIOA  = 0x12,
      REG_OLATA  = 0x14
    };

    TwoWire& _wire;
    uint8_t  _address;
    bool     _manageWire;
    uint16_t _directions;
    uint16_t _pullups;
    uint16_t _inputs;
    uint16_t _outputs;
    bool     _begun;
    bool     _configDirty;
    bool     _outputsDirty;
    bool     _ioconReady;
    bool     _latchPrimed;
    bool     _configOk;
    bool     _readHealthy;
    bool     _writeHealthy;
    uint32_t _errors;

    bool validChannel(uint8_t channel) {
      if (channel < 16) return true;
      _configOk = false;
      _readHealthy = _writeHealthy = false;
      _errors++;
      return false;
    }

    bool applyConfiguration() {
      if (!_configDirty) return true;
      bool ok = writePair(REG_IODIRA, _directions);
      ok = writePair(REG_GPPUA, _pullups) && ok;
      if (ok) _configDirty = false;
      return ok;
    }

    bool ensureSafeConfiguration() {
      if (!_configOk) return false;
      if (!_ioconReady && !normalizeRegisterMap()) return false;
      if (_configDirty && (!_latchPrimed || _outputsDirty)) {
        /* OLAT debe contener el valor seguro ANTES de habilitar una salida.
         * Si la escritura falla, IODIR no se toca y el siguiente scan vuelve
         * a intentar exactamente en este orden. */
        if (!writePair(REG_OLATA, _outputs)) return false;
        _latchPrimed = true;
        _outputsDirty = false;
      }
      return applyConfiguration();
    }

    bool normalizeRegisterMap() {
      /* IOCON puede sobrevivir a un reset del MCU si el expansor conserva
       * alimentacion. Con BANK=1 vive en 0x05; con BANK=0, 0x05 es GPINTENB.
       * Escribir cero ahi es seguro en ambos casos y garantiza BANK=0. Ya con
       * el mapa conocido, 0x0A fija tambien SEQOP=0 para las parejas A/B y
       * GPINTENA/B se borran porque este backend trabaja por sondeo. */
      if (!writeRegister(REG_IOCON_BANK1_A, 0x00)) return false;
      if (!writeRegister(REG_IOCON_BANK0_A, 0x00)) return false;
      if (!writePair(REG_GPINTENA, 0x0000)) return false;
      _ioconReady = true;
      return true;
    }

    bool writeRegister(uint8_t reg, uint8_t value) {
      _wire.beginTransmission(_address);
      _wire.write(reg);
      _wire.write(value);
      if (_wire.endTransmission() == 0) return true;
      _errors++;
      return false;
    }

    bool writePair(uint8_t reg, uint16_t value) {
      _wire.beginTransmission(_address);
      _wire.write(reg);
      _wire.write((uint8_t)(value & 0xFF));
      _wire.write((uint8_t)(value >> 8));
      if (_wire.endTransmission() == 0) return true;
      _errors++;
      return false;
    }

    bool readPair(uint8_t reg, uint16_t& value) {
      _wire.beginTransmission(_address);
      _wire.write(reg);
      if (_wire.endTransmission(false) != 0) {
        _errors++;
        return false;
      }
      uint8_t received = _wire.requestFrom(_address, (uint8_t)2);
      if (received != 2 || _wire.available() < 2) {
        while (_wire.available()) _wire.read();
        _errors++;
        return false;
      }
      uint8_t lo = (uint8_t)_wire.read();
      uint8_t hi = (uint8_t)_wire.read();
      value = (uint16_t)lo | ((uint16_t)hi << 8);
      return true;
    }
};

#endif /* COREFSM_MCP23017_BACKEND_H */
