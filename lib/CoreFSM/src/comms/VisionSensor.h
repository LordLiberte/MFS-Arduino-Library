#ifndef COREFSM_VISION_SENSOR_H
#define COREFSM_VISION_SENSOR_H

#include "../io/IDevice.h"

/* ===========================================================================
 *  VisionSensor.h  -  Camara de vision como un sensor mas
 * ---------------------------------------------------------------------------
 *  EL REPARTO DE TRABAJO
 *  ---------------------
 *  Un microcontrolador de control no debe recorrer matrices de pixeles dentro
 *  del scan: durante ese tiempo deja de actualizar entradas, secuencias y
 *  salidas, y el tiempo de respuesta deja de ser determinista.
 *
 *  Se hace lo mismo que en la industria: la camara es un equipo aparte
 *  (HuskyLens, OpenMV, Nicla Vision, ESP32-CAM, una Raspberry) que procesa por
 *  su cuenta y entrega el RESULTADO ya masticado por un cable serie. El
 *  autOmata recibe unos pocos numeros y decide.
 *
 *  Cognex y Keyence funcionan exactamente igual: la camara industrial hace su
 *  trabajo y le manda al PLC un "pieza OK / pieza NOK" y unas coordenadas.
 *
 *  EL PROTOCOLO
 *  ------------
 *  Trama binaria de 8 bytes, pensada para ser robusta y barata de parsear:
 *
 *      byte 0  0xAA        cabecera de sincronismo
 *      byte 1  classId     que ha reconocido (0 = nada)
 *      byte 2  offsetX     desviacion horizontal, con signo, -100..+100
 *      byte 3  offsetY     desviacion vertical, con signo
 *      byte 4  width       tamano aparente: sirve para estimar la distancia
 *      byte 5  confidence  fiabilidad 0..100
 *      byte 6  flags       bit 0 = pieza OK, bit 1 = ocupado, bit 2 = error
 *      byte 7  checksum    XOR de los bytes 1..6
 *
 *  POR QUE OFFSET Y NO COORDENADAS ABSOLUTAS
 *  -----------------------------------------
 *  Porque lo que necesita el control es el ERROR respecto al centro, no la
 *  posicion en pixeles. Con el error ya calculado, la correccion es directa:
 *
 *      offsetX = 0    objetivo centrado, seguir recto
 *      offsetX > 0    esta a la derecha, girar a la derecha
 *      offsetX < 0    esta a la izquierda, girar a la izquierda
 *
 *  Ademas el offset es independiente de la resolucion de la camara: cambias de
 *  camara y el control sigue valiendo tal cual.
 *
 *  LA VIGILANCIA DE COMUNICACION ES OBLIGATORIA
 *  --------------------------------------------
 *  Si la camara se desconecta, deja de mandar tramas. Sin vigilancia, el
 *  ultimo dato recibido se queda congelado para siempre y el robot sigue
 *  persiguiendo un objeto que ya no ve. Por eso, si no llega nada durante
 *  timeoutMs, la deteccion se invalida sola. En una maquina, un dato viejo es
 *  mas peligroso que la ausencia de dato: el dato viejo parece bueno.
 * ======================================================================== */

struct VisionResult {
  bool     detected   = false;
  uint8_t  classId    = 0;
  int8_t   offsetX    = 0;
  int8_t   offsetY    = 0;
  uint8_t  width      = 0;
  uint8_t  confidence = 0;
  bool     pieceOk    = false;
  bool     busy       = false;
  bool     error      = false;
};

class VisionSensor : public IDevice {
  public:
    VisionSensor(Stream& port, cfsm_time_t timeoutMs = 500)
      : _port(port), _timeout(timeoutMs), _lastRx(0), _idx(0),
        _minConf(60), _maxBytesPerScan(32) {}

    void begin() override {
      _idx    = 0;
      _lastRx = cfsm_millis();
      _data   = VisionResult();
      _commsOk = false;
    }

    /* Fase PAE. Consume como mucho lo que haya en el buffer de recepcion en
     * este instante; no espera a que llegue nada. Sin esa disciplina, un cable
     * suelto colgaria el scan. */
    void readInputs() override {
      uint8_t budget = _maxBytesPerScan;
      while (budget-- > 0 && _port.available()) {
        uint8_t b = (uint8_t)_port.read();

        if (_idx == 0) {
          if (b != 0xAA) continue;       /* resincronizacion */
          _buf[_idx++] = b;
        } else {
          _buf[_idx++] = b;
          if (_idx >= 8) {
            _idx = 0;
            uint8_t chk = 0;
            for (uint8_t i = 1; i <= 6; i++) chk ^= _buf[i];
            if (chk == _buf[7]) decode();
            /* Trama con checksum malo: se descarta en silencio. Un byte
             * corrupto es normal en serie; lo anormal seria hacerle caso. */
          }
        }
      }

      /* Vigilancia de comunicacion. */
      if (cfsm_elapsed(_lastRx) > _timeout) {
        _data           = VisionResult();
        _commsOk       = false;
      }
    }

    /* -----------------------------------------------------------------------
     *  CONSULTA
     * -------------------------------------------------------------------- */
    bool    hasTarget()   const { return _data.detected && !_data.error && _commsOk; }
    bool    isPieceOk()   const { return _data.pieceOk; }
    bool    isBusy()      const { return _data.busy; }
    bool    commsOk()     const { return _commsOk; }
    uint8_t classId()     const { return _data.classId; }
    int8_t  errorX()      const { return _data.offsetX; }
    int8_t  errorY()      const { return _data.offsetY; }
    uint8_t targetWidth() const { return _data.width; }
    uint8_t confidence()  const { return _data.confidence; }

    /* Objetivo centrado dentro de una banda muerta. La banda muerta evita que
     * el robot corrija sin parar por un pixel de diferencia: sin ella, oscila
     * a izquierda y derecha alrededor del centro y nunca se estabiliza. */
    bool isCentered(int8_t tolerance = 8) const {
      return hasTarget() && (abs((int)_data.offsetX) <= tolerance);
    }

    /* Confianza minima para dar por buena una deteccion. Subela si la camara
     * da falsos positivos; bajala si pierde el objetivo demasiado facil. */
    void setMinConfidence(uint8_t c) { _minConf = c; }

    /* Acota el trabajo de recepcion para que un emisor ruidoso o inundado no
     * monopolice el ciclo de scan. */
    void setMaxBytesPerScan(uint8_t bytes) { _maxBytesPerScan = bytes ? bytes : 1; }

    /* Envia una orden a la camara (disparo, cambio de programa de inspeccion).
     * El protocolo de vuelta es cosa del firmware de la camara. */
    void sendCommand(uint8_t cmd, uint8_t arg = 0) {
      uint8_t frame[4] = { 0x55, cmd, arg, (uint8_t)(cmd ^ arg) };
      _port.write(frame, 4);
    }

    const VisionResult& raw() const { return _data; }

    void describe(Print& out) const {
      out.print('[');
      if (_name) out.print(_name); else out.print(CFSM_FSTR("VISION"));
      out.print(CFSM_FSTR("] "));
      if (!_commsOk)          out.print(CFSM_FSTR("SIN COMUNICACION"));
      else if (!_data.detected) out.print(CFSM_FSTR("sin objetivo"));
      else {
        out.print(CFSM_FSTR("id=")); out.print(_data.classId);
        out.print(CFSM_FSTR(" x="));  out.print(_data.offsetX);
        out.print(CFSM_FSTR(" y="));  out.print(_data.offsetY);
        out.print(CFSM_FSTR(" cf=")); out.print(_data.confidence);
      }
    }

  private:
    Stream&     _port;
    cfsm_time_t _timeout;
    cfsm_time_t _lastRx;
    uint8_t     _buf[8];
    uint8_t     _idx;
    uint8_t     _minConf;
    uint8_t     _maxBytesPerScan;
    bool        _commsOk = false;
    VisionResult _data;

    void decode() {
      _data.classId    = _buf[1];
      _data.offsetX    = (int8_t)_buf[2];
      _data.offsetY    = (int8_t)_buf[3];
      _data.width      = _buf[4];
      _data.confidence = _buf[5];
      _data.pieceOk    = (_buf[6] & 0x01) != 0;
      _data.busy       = (_buf[6] & 0x02) != 0;
      _data.error      = (_buf[6] & 0x04) != 0;
      _data.detected   = !_data.error && (_data.classId != 0) &&
                         (_data.confidence >= _minConf);
      _lastRx  = cfsm_millis();
      _commsOk = true;
    }
};

/* ===========================================================================
 *  VisualServo  -  Control proporcional de seguimiento visual
 * ---------------------------------------------------------------------------
 *  Convierte el error que ve la camara en consignas de avance y giro para un
 *  chasis. Es un lazo cerrado: la camara mide, el control corrige, el robot se
 *  mueve, la camara vuelve a medir.
 *
 *      w = Kp_ang  * offsetX                (correccion de direccion)
 *      v = Kp_dist * (anchoDeseado - ancho)  (correccion de distancia)
 *
 *  El truco de la distancia: no hace falta un sensor de distancia. Cuanto mas
 *  cerca esta un objeto, mas grande se ve. Si el objeto se ve mas estrecho de
 *  lo deseado, esta lejos y hay que avanzar; si se ve mas ancho, esta cerca y
 *  hay que retroceder. Es una medida grosera pero suficiente para mantener una
 *  distancia de seguimiento, y sale gratis.
 *
 *  Ajuste practico de las ganancias: sube Kp hasta que el robot empiece a
 *  zigzaguear al seguir el objetivo, y luego bajala a la mitad. Ese
 *  zigzagueo es el sistema entrando en oscilacion, y es exactamente la
 *  frontera que hay que evitar.
 * ======================================================================== */

struct VisualServo {
  uint8_t kpAngular  = 15;   /* decimas: 15 = ganancia 1,5 */
  uint8_t kpDistance = 20;
  uint8_t targetWidth = 40;  /* ancho aparente deseado      */
  uint8_t baseSpeed   = 0;   /* avance minimo constante     */
  int8_t  deadBand    = 8;

  /* Salidas, listas para pasar a chassis.drive(v, w). */
  int16_t v = 0;
  int16_t w = 0;

  /* Devuelve false si no hay objetivo: en ese caso v y w salen a cero y el
   * chasis debe parar. Perder de vista el objetivo NUNCA debe traducirse en
   * seguir con la ultima consigna. */
  bool update(const VisionSensor& cam) {
    if (!cam.hasTarget()) { v = 0; w = 0; return false; }

    int16_t ex = cam.errorX();
    w = (abs((int)ex) <= deadBand) ? 0 : (int16_t)(((int32_t)ex * kpAngular) / 10);

    int16_t ed = (int16_t)targetWidth - (int16_t)cam.targetWidth();
    v = baseSpeed + (int16_t)(((int32_t)ed * kpDistance) / 10);

    if (v >  255) v =  255;
    if (v < -255) v = -255;
    if (w >  255) w =  255;
    if (w < -255) w = -255;
    return true;
  }
};

#endif /* COREFSM_VISION_SENSOR_H */
