/* =============================================================================
 *  EJEMPLO 7  -  Seguimiento visual: el robot persigue un objeto
 * -----------------------------------------------------------------------------
 *  QUE HACE
 *    Una camara inteligente (HuskyLens, OpenMV, Nicla Vision, ESP32-CAM...)
 *    reconoce un objeto que le has ensenado y manda por el puerto serie el
 *    ERROR de centrado y el tamano aparente. El robot corrige direccion y
 *    distancia en lazo cerrado para mantener el objeto centrado y a una
 *    distancia fija.
 *
 *  POR QUE LA CAMARA VA APARTE
 *    Un Arduino no procesa imagenes, y no debe intentarlo: mientras recorre
 *    una matriz de pixeles no esta vigilando la seta de emergencia ni los
 *    finales de carrera. Se reparte el trabajo igual que en la industria: la
 *    camara piensa por su cuenta y entrega el resultado ya masticado; el
 *    automata solo decide y actua. Cognex y Keyence funcionan asi.
 *
 *  EL PROTOCOLO (8 bytes, definido en comms/VisionSensor.h)
 *      0xAA | clase | offsetX | offsetY | ancho | confianza | flags | checksum
 *
 *    Programa tu camara para que emita esa trama. Si prefieres otra, cambia
 *    solo decode() dentro de VisionSensor: el resto del sistema no se entera.
 *
 *  CABLEADO (Arduino Nano)
 *      Camara TX -> D8   (SoftwareSerial RX)
 *      Camara RX -> D9   (SoftwareSerial TX)
 *      Motor izquierdo   IN1=2 IN2=4 PWM=3
 *      Motor derecho     IN1=7 IN2=12 PWM=5
 *      Pulsador marcha   A0 -> GND
 *      LED de estado     A1
 *
 *    El puerto serie por hardware queda libre para el monitor: depurar un
 *    lazo de control sin poder ver los numeros es perder el tiempo.
 *
 *  AJUSTE DE LAS GANANCIAS (esto se hace mirando, no calculando)
 *    1. Pon kpAngular bajo (5). El robot seguira el objeto con pereza.
 *    2. Subelo hasta que empiece a zigzaguear al seguirlo.
 *    3. Bajalo a la mitad de ese valor. Ese es el ajuste bueno.
 *    El zigzagueo es el sistema entrando en oscilacion: es la frontera que hay
 *    que reconocer y no cruzar.
 * ========================================================================== */

#include <CoreFSM.h>
#include <SoftwareSerial.h>

/* --- Camara --------------------------------------------------------------- */
SoftwareSerial puertoCamara(8, 9);      /* RX, TX */
VisionSensor   camara(puertoCamara, 400);

/* --- Chasis --------------------------------------------------------------- */
MotorDrive          motorIzq(2, 4, 3);
MotorDrive          motorDer(7, 12, 5);
DifferentialChassis chasis(motorIzq, motorDer, 10);

/* --- Campo ---------------------------------------------------------------- */
DigitalSensor btnMarcha(A0, true, 30);
DigitalOutput ledEstado(A1);
DeviceManager<5> io;

/* --- Control de seguimiento ----------------------------------------------- */
VisualServo servo;

/* ===========================================================================
 *  El bloque de comportamiento
 * ---------------------------------------------------------------------------
 *  Tres situaciones, y las tres importan:
 *
 *    BUSCANDO   No ve nada. Gira despacio sobre si mismo hasta encontrar algo.
 *    SIGUIENDO  Lo ve. Corrige direccion y distancia de forma continua.
 *    PERDIDO    Lo veia y lo ha dejado de ver. NO se queda con la ultima
 *               consigna: para. Un dato viejo es mas peligroso que la ausencia
 *               de dato, porque parece bueno. Espera un momento por si vuelve
 *               (una oclusion breve es normal) y si no, vuelve a buscar.
 * ======================================================================== */
enum PasosSeguimiento : uint16_t {
  SEG_PARADO   = 0,
  SEG_BUSCANDO = 10,
  SEG_SIGUIENDO= 20,
  SEG_PERDIDO  = 30
};

class Seguidor : public SequenceBlock {
  public:
    Seguidor(DifferentialChassis& ch, VisionSensor& cam, VisualServo& sv)
      : _ch(ch), _cam(cam), _sv(sv) {}

    bool ordenMarcha = false;

    uint8_t  velocidadBusqueda = 120;
    uint16_t msEsperaPerdido   = 800;

    void begin() override {
      setName(F("SEGUIDOR"));
      setInitialStep(SEG_PARADO);
      setStep(SEG_PARADO);
      _ch.disable();
    }

    void update() override {
      if (!updateSequence()) { _ch.stop(); _ch.disable(); return; }

      /* Perder la comunicacion con la camara es una averia, no un detalle.
       * Sin ella el robot esta ciego y no puede seguir moviendose. */
      if (!_cam.commsOk() && _currentStep != SEG_PARADO) {
        fault(CFSM_ERR_SENSOR_INVALID);
        _ch.stop();
        return;
      }

      switch (_currentStep) {

        case SEG_PARADO:
          _ch.stop();
          if (ordenMarcha) {
            ordenMarcha = false;
            _ch.enable();
            setStep(SEG_BUSCANDO);
          }
          break;

        case SEG_BUSCANDO:
          /* Giro lento sobre si mismo: barre 360 grados hasta encontrar algo. */
          _ch.spinRight(velocidadBusqueda);
          if (_cam.hasTarget()) { _ch.stop(); setStep(SEG_SIGUIENDO); }
          break;

        case SEG_SIGUIENDO:
          if (!_cam.hasTarget()) { _ch.stop(); setStep(SEG_PERDIDO); break; }
          /* El servo calcula v y w; el chasis los reparte entre las ruedas.
           * Este bloque no sabe cuantas ruedas hay ni como se reparte. */
          _sv.update(_cam);
          _ch.drive(_sv.v, _sv.w);
          break;

        case SEG_PERDIDO:
          _ch.stop();
          if (_cam.hasTarget()) { setStep(SEG_SIGUIENDO); break; }
          if (getTimeInStep() >= msEsperaPerdido) setStep(SEG_BUSCANDO);
          break;
      }
    }

    const __FlashStringHelper* stepName(uint16_t s) const override {
      switch (s) {
        case SEG_PARADO:    return F("PARADO");
        case SEG_BUSCANDO:  return F("BUSCANDO");
        case SEG_SIGUIENDO: return F("SIGUIENDO");
        case SEG_PERDIDO:   return F("OBJETIVO_PERDIDO");
        default:            return nullptr;
      }
    }

  private:
    DifferentialChassis& _ch;
    VisionSensor&        _cam;
    VisualServo&         _sv;
};

BlockManager<2> manager;
Seguidor        seguidor(chasis, camara, servo);
StepTracer      tracer(seguidor, Serial);

/* Registro CSV para ajustar las ganancias mirando la curva, no a ojo. */
CsvLogger<4> grafica(Serial, 100);

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { }
  puertoCamara.begin(9600);

  Serial.println(F("=== CoreFSM - Seguimiento visual ==="));

  io.registerDevice(&camara,    F("CAMARA"));
  io.registerDevice(&motorIzq,  F("M_IZQ"));
  io.registerDevice(&motorDer,  F("M_DER"));
  io.registerDevice(&btnMarcha, F("MARCHA"));
  io.registerDevice(&ledEstado, F("ESTADO"));
  io.beginAll();

  motorIzq.setRamp(4);
  motorDer.setRamp(4);

  /* Ganancias de partida. Ajustalas segun el metodo del encabezado. */
  servo.kpAngular  = 12;
  servo.kpDistance = 15;
  servo.targetWidth = 45;    /* ancho aparente deseado = distancia de seguimiento */
  servo.baseSpeed   = 0;
  servo.deadBand    = 8;

  manager.registerBlock(&seguidor, F("SEGUIDOR"));
  manager.beginAll();
  seguidor.ST.cfgw.enable = true;
  seguidor.start();

  Serial.println(F("Pulsa el boton para empezar a seguir. 'g' = grafica CSV."));
}

void loop() {
  /* 1. PAE: la camara consume su buffer sin esperar a nadie */
  io.readAllInputs();

  if (btnMarcha.hasRisen()) {
    if (seguidor.getStep() == SEG_PARADO) seguidor.ordenMarcha = true;
    else { seguidor.stop(); seguidor.start(); }
  }

  /* 2. Scan */
  manager.updateAll();

  /* 3. Senalizacion y diagnostico */
  if (seguidor.isFaulted())            ledEstado.setMode(OUT_BLINK_FAST);
  else if (camara.hasTarget())         ledEstado.setMode(OUT_ON);
  else                                 ledEstado.setMode(OUT_BLINK_SLOW);

  tracer.update();

  grafica.set(0, camara.errorX());
  grafica.set(1, camara.targetWidth());
  grafica.set(2, servo.w);
  grafica.set(3, servo.v);
  grafica.tick();

  if (Serial.available() && Serial.read() == 'g') {
    grafica.enable(!grafica.isEnabled());
    if (grafica.isEnabled()) grafica.header(F("t,errorX,ancho,giro,avance"));
  }

  /* 4. PAA */
  io.writeAllOutputs();
}
