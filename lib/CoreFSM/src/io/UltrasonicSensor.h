#ifndef COREFSM_ULTRASONIC_SENSOR_H
#define COREFSM_ULTRASONIC_SENSOR_H

#include "IDevice.h"

/* ===========================================================================
 *  UltrasonicSensor.h  -  Sensor de distancia HC-SR04
 * ---------------------------------------------------------------------------
 *  COMO FUNCIONA
 *  -------------
 *  Se lanza un pulso de 10 microsegundos por TRIG. El sensor emite una rafaga
 *  ultrasonica y pone ECHO a nivel alto hasta que le vuelve el eco. Midiendo
 *  cuanto dura ese nivel alto se saca la distancia:
 *
 *      distancia_cm = tiempo_us / 58,2
 *
 *  El 58,2 sale del sonido a 343 m/s (unos 29,1 us por centimetro) multiplicado
 *  por dos, porque el sonido hace el viaje de ida y vuelta.
 *
 *  LA PARTE INCOMODA, DICHA CLARAMENTE
 *  -----------------------------------
 *  pulseIn() es BLOQUEANTE: se queda esperando el eco. Con el timeout por
 *  defecto de esta clase (20 ms, unos 3,4 metros), un obstaculo lejano o
 *  ausente puede detener el ciclo de scan 20 milisegundos enteros.
 *
 *  Se mitiga de dos formas, y conviene entender ambas:
 *
 *   1. NO SE MIDE EN CADA SCAN. Entre medidas hay un intervalo minimo (50 ms
 *      por defecto). No es solo por rendimiento: el propio sensor necesita
 *      unos 50 ms para que se apaguen los ecos de la medida anterior. Medir
 *      mas rapido da lecturas falsas por eco residual.
 *
 *   2. EL TIMEOUT ES CORTO. Solo se espera lo necesario para el alcance util.
 *      Si en tu robot nada importa mas alla de 1 metro, pon un timeout de
 *      6000 us: el peor caso baja de 20 ms a 6 ms.
 *
 *  Si esos milisegundos son inaceptables (un eje rapido, una seguridad
 *  exigente), la solucion de verdad es medir por interrupcion de cambio de
 *  pin, que no bloquea nada. Es mas codigo y consume una interrupcion; para
 *  un robot movil, el enfoque de aqui sobra.
 *
 *  FILTRO DE MEDIANA
 *  -----------------
 *  Los ultrasonidos dan lecturas disparatadas de vez en cuando: una superficie
 *  inclinada desvia el eco, una esquina lo rebota dos veces. La media
 *  aritmetica no ayuda porque un valor absurdo la arrastra. La MEDIANA de tres
 *  medidas descarta el valor raro por completo, que es justo lo que se
 *  necesita, y cuesta tres comparaciones.
 * ======================================================================== */

class UltrasonicSensor : public IDevice {
  public:
    /* trigPin, echoPin: pines del HC-SR04
     * intervalMs      : tiempo minimo entre medidas (>= 50 recomendado)
     * timeoutUs       : espera maxima del eco. 6000 us ~= 1 m, 20000 us ~= 3,4 m */
    UltrasonicSensor(uint8_t trigPin, uint8_t echoPin,
                     uint16_t intervalMs = 60, uint16_t timeoutUs = 20000)
      : _trig(trigPin), _echo(echoPin), _interval(intervalMs),
        _timeout(timeoutUs), _last(0), _distance(CFSM_ULTRASONIC_FAR) {}

    void begin() override {
      pinMode(_trig, OUTPUT);
      pinMode(_echo, INPUT);
      digitalWrite(_trig, LOW);
      _last = cfsm_millis();
      _h[0] = _h[1] = _h[2] = CFSM_ULTRASONIC_FAR;
    }

    void readInputs() override {
      if (_forced) { _distance = _simValue; return; }
      if (cfsm_elapsed(_last) < _interval) return;
      _last = cfsm_millis();

      digitalWrite(_trig, LOW);
      delayMicroseconds(3);
      digitalWrite(_trig, HIGH);
      delayMicroseconds(10);
      digitalWrite(_trig, LOW);

      uint32_t us = pulseIn(_echo, HIGH, _timeout);

      /* 0 significa "no volvio ningun eco". No es distancia cero: es
       * "no hay nada dentro del alcance", que es exactamente lo contrario.
       * Confundir ambos hace que el robot frene en campo abierto. */
      uint16_t cm = (us == 0) ? CFSM_ULTRASONIC_FAR : (uint16_t)(us / 58);

      _h[2] = _h[1]; _h[1] = _h[0]; _h[0] = cm;
      _distance = median3(_h[0], _h[1], _h[2]);
    }

    /* Distancia filtrada, en centimetros. CFSM_ULTRASONIC_FAR = nada a la vista. */
    uint16_t cm() const { return _distance; }

    bool isClear(uint16_t threshold) const { return _distance > threshold; }
    bool isObstacle(uint16_t threshold) const { return _distance <= threshold; }
    bool hasEcho() const { return _distance < CFSM_ULTRASONIC_FAR; }

    void force(uint16_t distanceCm) { _forced = true; _simValue = distanceCm; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else out.print(CFSM_FSTR("SONAR"));
      out.print(CFSM_FSTR("]="));
      if (_distance >= CFSM_ULTRASONIC_FAR) out.print(CFSM_FSTR("---"));
      else { out.print(_distance); out.print(CFSM_FSTR("cm")); }
      if (_forced) out.print(CFSM_FSTR(" *FORZADO*"));
    }

    static const uint16_t CFSM_ULTRASONIC_FAR = 999;

  private:
    uint8_t     _trig, _echo;
    uint16_t    _interval, _timeout;
    cfsm_time_t _last;
    uint16_t    _distance;
    uint16_t    _h[3];
    uint16_t    _simValue = CFSM_ULTRASONIC_FAR;

    static uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
      if (a > b) { uint16_t t = a; a = b; b = t; }
      if (b > c) { uint16_t t = b; b = c; c = t; }
      if (a > b) { uint16_t t = a; a = b; b = t; }
      return b;
    }
};

#endif /* COREFSM_ULTRASONIC_SENSOR_H */
