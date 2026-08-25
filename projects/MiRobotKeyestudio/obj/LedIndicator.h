#pragma once
#include <Arduino.h>

enum class LedMode : uint8_t {
  OFF,
  ON,
  BLINK_SLOW,   // 1000 ms
  BLINK_FAST,   // 250 ms
  PULSE,        // 100 ms
  BREATHING     // Sube y baja el brillo progresivamente
};

class LedIndicator {
  private:
    uint8_t  _pin;
    LedMode  _mode;
    uint32_t _lastToggle;
    bool     _state;

    // Configuración Breathing
    uint32_t _lastFadeTime;
    int16_t  _brightness;
    int8_t   _fadeStep;
    uint8_t  _fadeIntervalMs; // Tiempo entre pasos (menor = más rápido, mayor = más lento/suave)
    uint8_t  _maxBrightness;
    uint8_t  _minBrightness;

    void setPhysical(bool state) {
      _state = state;
      digitalWrite(_pin, _state ? HIGH : LOW);
    }

  public:
    LedIndicator(uint8_t pin) 
      : _pin(pin), _mode(LedMode::OFF), _lastToggle(0), _state(false),
        _lastFadeTime(0), _brightness(0), _fadeStep(1),
        _fadeIntervalMs(15), _maxBrightness(255), _minBrightness(5) {}

    void begin() {
      pinMode(_pin, OUTPUT);
      setPhysical(false);
    }

    // Parámetros para personalizar la suavidad
    void configureBreathing(uint8_t minBr, uint8_t maxBr, uint8_t intervalMs) {
      _minBrightness = minBr;
      _maxBrightness = maxBr;
      _fadeIntervalMs = intervalMs;
    }

    void setMode(LedMode mode) {
      if (_mode == mode) return;
      _mode = mode;
      _lastToggle = millis();
      _lastFadeTime = millis();

      if (_mode == LedMode::OFF) {
        analogWrite(_pin, 0);
        setPhysical(false);
      } else if (_mode == LedMode::ON) {
        analogWrite(_pin, 255);
        setPhysical(true);
      } else if (_mode == LedMode::BREATHING) {
        _brightness = _minBrightness;
        _fadeStep = 1;
      }
    }

    void update() {
      uint32_t now = millis();

      // 1. Modos digitales
      if (_mode == LedMode::BLINK_SLOW || _mode == LedMode::BLINK_FAST || _mode == LedMode::PULSE) {
        uint16_t interval = (_mode == LedMode::BLINK_SLOW) ? 1000 : (_mode == LedMode::BLINK_FAST ? 250 : 100);
        if (now - _lastToggle >= interval) {
          _lastToggle = now;
          setPhysical(!_state);
        }
        return;
      }

      // 2. Modo respiración continuo y suave
      if (_mode == LedMode::BREATHING) {
        if (now - _lastFadeTime >= _fadeIntervalMs) {
          _lastFadeTime = now;

          _brightness += _fadeStep;

          // Inversión limpia de sentido en los límites
          if (_brightness >= _maxBrightness) {
            _brightness = _maxBrightness;
            _fadeStep = -1;
          } else if (_brightness <= _minBrightness) {
            _brightness = _minBrightness;
            _fadeStep = 1;
          }

          analogWrite(_pin, (uint8_t)_brightness);
        }
      }
    }
};