/* Pruebas host de E/S extensible, corte seguro y red de snapshots. */
#include <Arduino.h>
#include <CoreFSM.h>
#include <cstdio>
#include <cstring>

static int fallos = 0, pruebas = 0;
#define CHECK(cond, msg) do { pruebas++; if (!(cond)) { fallos++; \
  printf("   FALLO: %s   (linea %d)\n", msg, __LINE__); } } while (0)

class FakeBackend : public IDigitalBackend {
 public:
  bool levels[16] = {false};
  uint8_t modes[16] = {0};
  int begins = 0, samples = 0, commits = 0;
  bool ok = true;

  void configure(uint8_t ch, uint8_t mode) override { if (ch < 16) modes[ch] = mode; }
  bool read(uint8_t ch) const override { return ch < 16 && levels[ch]; }
  void write(uint8_t ch, bool level) override { if (ch < 16) levels[ch] = level; }
  void begin() override { begins++; }
  void sampleInputs() override { samples++; }
  void commitOutputs() override { commits++; }
  bool healthy() const override { return ok; }
};

class BufferStream : public Stream {
 public:
  uint8_t rx[512], tx[512];
  size_t rxLen = 0, rxPos = 0, txLen = 0;

  size_t write(uint8_t b) override {
    if (txLen >= sizeof(tx)) return 0;
    tx[txLen++] = b;
    return 1;
  }
  int available() override { return (int)(rxLen - rxPos); }
  int read() override { return rxPos < rxLen ? rx[rxPos++] : -1; }
  int peek() override { return rxPos < rxLen ? rx[rxPos] : -1; }

  void feed(const uint8_t* data, size_t len) {
    if (rxPos == rxLen) rxPos = rxLen = 0;
    if (len > sizeof(rx) - rxLen) len = sizeof(rx) - rxLen;
    memcpy(rx + rxLen, data, len);
    rxLen += len;
  }

  void transferTo(BufferStream& other, bool corrupt = false, bool duplicate = false) {
    if (corrupt && txLen > 2) {
      size_t i = txLen - 2;  /* ultimo byte COBS antes del delimitador */
      tx[i] = (tx[i] == 0xFF) ? 0xFE : (uint8_t)(tx[i] + 1);
    }
    other.feed(tx, txLen);
    if (duplicate) other.feed(tx, txLen);
    txLen = 0;
  }
};

int main() {
  printf("=== BANCO E/S Y RED CoreFSM ===\n\n");

  { printf("IO1 Tiempo estable y forzado logico\n");
    resetArduinoStub();
    g_digitalLevels[3] = HIGH;
    DigitalSensor sensor(3, true, 20);
    sensor.begin();
    g_ms = 100; g_digitalLevels[3] = LOW; sensor.readInputs();
    g_ms = 105; g_digitalLevels[3] = HIGH; sensor.readInputs();
    CHECK(sensor.timeInState() >= 100,
          "un rebote rechazado no reinicia el tiempo del estado filtrado");

    sensor.setInverted(true);
    sensor.setDebounce(0);
    sensor.force(true);
    sensor.readInputs();
    CHECK(sensor.isTriggered(), "force(true) es el valor logico final aunque haya inversion");
  }

  { printf("IO2 Backend agrupado y corte seguro\n");
    resetArduinoStub();
    FakeBackend backend;
    backend.levels[0] = true;
    DigitalSensor entrada(backend, 0, false, 0);
    DigitalOutput salida(backend, 1, false);
    DigitalOutput seguraOn(backend, 2, false, true);
    seguraOn.setMaxOnTime(50);
    DeviceManager<3, 1> io;
    CHECK(io.registerBackend(&backend), "registra backend");
    CHECK(io.registerDevice(&entrada), "registra entrada");
    CHECK(io.registerDevice(&salida), "registra salida");
    CHECK(io.registerDevice(&seguraOn), "registra salida con estado seguro ON");
    io.beginAll();
    CHECK(backend.begins == 1 && backend.samples == 1 && backend.commits == 1,
          "begin captura y vuelca exactamente una vez");
    CHECK(entrada.isTriggered() && !entrada.hasRisen(),
          "primera captura sincroniza sin flanco fantasma");

    salida.turnOn();
    io.writeAllOutputs();
    CHECK(backend.levels[1], "la salida queda en la sombra del backend");
    CHECK(backend.commits == 2, "un solo commit por PAA");
    io.setSafetyInterlock(true);
    CHECK(!backend.levels[1] && salida.mode() == OUT_OFF,
          "el interbloqueo corta inmediatamente y borra la orden");
    io.setSafetyInterlock(false);
    g_ms = 1000;
    io.writeAllOutputs();
    CHECK(!backend.levels[1], "liberar el interbloqueo no rearranca la salida");
    CHECK(backend.levels[2],
          "el valor seguro ON persiste sin heredar el watchdog anterior");
    seguraOn.turnOn();
    io.writeAllOutputs();
    CHECK(backend.levels[2], "un mando ON nuevo inicia su propio cronometro");
    g_ms = 1051;
    io.writeAllOutputs();
    CHECK(!backend.levels[2] && seguraOn.hasTimedOut(),
          "el watchdog vuelve a proteger tras abandonar el estado seguro");
    seguraOn.turnOff();
    io.writeAllOutputs();
    CHECK(!backend.levels[2] && !seguraOn.hasTimedOut(),
          "apagar rearma el enclavamiento del watchdog");
    CHECK(io.allBackendsHealthy(), "propaga la salud del backend");
  }

  { printf("NET2 TX lleno no consume secuencias\n");
    resetArduinoStub();
    BufferStream portA, portB;
    CfsmPacketTransport transportA(portA, 1, 0x1111);
    CfsmPacketTransport transportB(portB, 2, 0x2222);
    RemoteDigitalBackend<1> imageA(2, 7, 500, 100);
    RemoteDigitalBackend<1> imageB(1, 7, 500, 100);
    CfsmNetworkManager<1> netA(transportA), netB(transportB);
    netA.attach(&imageA); netB.attach(&imageB);
    netA.begin(); netB.begin();

    imageA.output(0, true);
    netA.writeOutputs(255);
    portA.transferTo(portB);
    netB.readInputs(255);
    CHECK(imageB.input(0), "establece la referencia inicial de secuencia");

    uint8_t junk = 0x5A;
    while (transportA.send(2, 0x55, 0, &junk, 1)) { }
    bool rejected = true;
    for (uint16_t i = 0; i < 130; i++)
      rejected = !transportA.send(2, 0x55, 0, &junk, 1) && rejected;
    CHECK(rejected, "el buffer lleno rechaza nuevas tramas");
    transportA.serviceTx(255);
    portA.transferTo(portB);
    netB.readInputs(255);

    imageA.output(0, false);
    imageA.forceSnapshot();
    netA.writeOutputs(255);
    portA.transferTo(portB);
    netB.readInputs(255);
    CHECK(!imageB.input(0) && imageB.outOfOrder() == 0,
          "los intentos TX fallidos no crean un salto de secuencia");
  }

  { printf("DRV1 Estado durante inversion y reinicio de vision\n");
    resetArduinoStub();
    MotorDrive drive(4, 5, 6);
    drive.begin();
    drive.enable();
    drive.runForward(100);
    drive.writeOutputs();
    CHECK(drive.ST.stw.running && drive.ST.stw.fwdActive && drive.ST.stw.atSetpoint,
          "el variador informa marcha adelante");
    drive.runReverse(100);
    drive.writeOutputs();
    CHECK(!drive.ST.stw.running && !drive.ST.stw.fwdActive &&
          !drive.ST.stw.revActive && !drive.ST.stw.atSetpoint,
          "el dead-time publica estado detenido coherente");
    g_ms = 10;
    drive.writeOutputs();
    CHECK(!drive.ST.stw.running && !drive.ST.stw.fwdActive &&
          !drive.ST.stw.revActive && !drive.ST.stw.atSetpoint,
          "el estado sigue detenido durante toda la ventana muerta");

    BufferStream cameraPort;
    VisionSensor camera(cameraPort, 500);
    camera.begin();
    uint8_t frame[8] = {0xAA, 1, 0, 0, 20, 80, 0, 0};
    for (uint8_t i = 1; i <= 6; i++) frame[7] ^= frame[i];
    cameraPort.feed(frame, sizeof(frame));
    camera.readInputs();
    CHECK(camera.commsOk(), "una trama valida establece comunicacion");
    camera.begin();
    CHECK(!camera.commsOk(), "reinicializar vision invalida la comunicacion previa");
  }

  { printf("NET1 Snapshot, CRC, timeout y reinicio de peer\n");
    resetArduinoStub();
    BufferStream portA, portB;
    CfsmPacketTransport transportA(portA, 1, 0x1111);
    CfsmPacketTransport transportB(portB, 2, 0x2222);
    RemoteDigitalBackend<1> imageA(2, 7, 500, 100);
    RemoteDigitalBackend<1> imageB(1, 7, 500, 100);
    CfsmNetworkManager<1> netA(transportA), netB(transportB);
    CHECK(netA.attach(&imageA) && netB.attach(&imageB), "adjunta endpoints");
    netA.begin(); netB.begin();

    imageA.output(3, true);
    netA.writeOutputs(255);
    portA.transferTo(portB);
    netB.readInputs(255);
    CHECK(imageB.linkOk() && imageB.input(3), "recibe snapshot direccionado");

    imageA.output(3, false);
    netA.writeOutputs(255);
    portA.transferTo(portB, true);
    netB.readInputs(255);
    CHECK(imageB.input(3), "una trama corrupta no sustituye el ultimo dato valido");
    CHECK(transportB.stats().crcErrors == 1, "cuenta el error CRC");

    g_ms = 600;
    netB.readInputs(0);
    CHECK(!imageB.linkOk() && !imageB.input(3), "timeout aplica el valor seguro");

    imageA.forceSnapshot();
    netA.writeOutputs(255);
    portA.transferTo(portB);
    netB.readInputs(255);
    CHECK(imageB.linkOk() && !imageB.input(3),
          "recupera tras timeout aunque el peer conserve la misma sesion");

    g_ms = 1200;
    netB.readInputs(0);
    CHECK(!imageB.linkOk(), "puede volver a detectar una segunda perdida");

    transportA.setSession(0x3333);
    imageA.output(3, false);
    netA.writeOutputs(255);
    portA.transferTo(portB, false, true);
    netB.readInputs(255);
    CHECK(imageB.linkOk() && !imageB.input(3), "un snapshot valido recupera el enlace");
    CHECK(imageB.consumePeerRestarted(), "detecta el cambio de sesion del peer");
    CHECK(imageB.duplicates() == 1, "descarta snapshots duplicados");
  }

  printf("\n=== %d comprobaciones, %d fallos ===\n", pruebas, fallos);
  return fallos ? 1 : 0;
}
