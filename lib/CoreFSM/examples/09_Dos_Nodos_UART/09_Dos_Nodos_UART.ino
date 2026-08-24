/* Dos placas ejecutan el mismo firmware con un id distinto:
 *
 *   nodo 1: -D CFSM_NODE_ID=1      nodo 2: -D CFSM_NODE_ID=2
 *
 * Cruza TX (pin 11) con RX (pin 10) y une GND. Cada pulsador enciende el LED
 * de la otra placa. El enlace publica estados, no pulsos, y cae a OFF si
 * vence el timeout. No es un bus de seguridad. */

#include <CoreFSM.h>
#include <SoftwareSerial.h>

#ifndef CFSM_NODE_ID
  #define CFSM_NODE_ID 1
#endif

static_assert(CFSM_NODE_ID == 1 || CFSM_NODE_ID == 2,
              "CFSM_NODE_ID debe ser 1 o 2");

const uint8_t NODO_REMOTO = (CFSM_NODE_ID == 1) ? 2 : 1;

SoftwareSerial enlaceSerie(10, 11);  // RX, TX
CfsmPacketTransport transporte(enlaceSerie, CFSM_NODE_ID);
CfsmNetworkManager<1> red(transporte);
RemoteDigitalBackend<1> imagenRemota(NODO_REMOTO, 0, 500, 100);

DigitalSensor botonLocal(2, true, 20);
DigitalOutput ledLocal(LED_BUILTIN);
DigitalSensor botonRemoto(imagenRemota, 0, false, 0);
DigitalOutput publicarBoton(imagenRemota, 0, false, false);
DeviceManager<4> io;

void setup() {
  enlaceSerie.begin(38400);

  red.attach(&imagenRemota);
  red.begin();

  io.registerDevice(&botonLocal, F("BOTON_LOCAL"));
  io.registerDevice(&ledLocal, F("LED_LOCAL"));
  io.registerDevice(&botonRemoto, F("BOTON_REMOTO"));
  io.registerDevice(&publicarBoton, F("PUBLICAR_BOTON"));
  io.beginAll();
}

void loop() {
  red.readInputs(32);       // PAE de red: trama validada y con timeout
  io.readAllInputs();       // PAE local y copia de la imagen remota

  publicarBoton.set(botonLocal.isTriggered());
  ledLocal.set(imagenRemota.linkOk() && botonRemoto.isTriggered());

  io.writeAllOutputs();     // PAA local y preparación del snapshot propio
  red.writeOutputs(24);     // PAA de red con trabajo acotado por scan
}
