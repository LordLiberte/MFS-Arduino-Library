#ifndef COREFSM_HANDSHAKE_H
#define COREFSM_HANDSHAKE_H

#include "CoreFSM_Platform.h"

/* ===========================================================================
 *  Handshake.h  -  Traspaso formal de testigo entre secuencias
 * ---------------------------------------------------------------------------
 *  EL PROBLEMA QUE RESUELVE
 *  ------------------------
 *  Dos estaciones que se pasan una pieza parecen faciles de coordinar:
 *
 *      // Estacion A                    // Estacion B
 *      piezaLista = true;               if (piezaLista) recoger();
 *
 *  Y funciona... hasta que un dia no. Porque no hay nada que garantice que B
 *  llego a ver el flag antes de que A lo bajara, ni que A sepa si B se ha
 *  enterado. Si B estaba en fallo, A cree que entrego la pieza y sigue
 *  produciendo. Si A baja el flag demasiado pronto, B se queda esperando una
 *  pieza que ya paso. Son carreras criticas (race conditions), y en una
 *  maquina real se manifiestan como "una vez cada doscientos ciclos se atasca
 *  y no sabemos por que".
 *
 *  LA SOLUCION: HANDSHAKE DE CUATRO FASES
 *  --------------------------------------
 *  El protocolo estandar en automatizacion. Nadie avanza hasta que el otro ha
 *  confirmado. Las cuatro senales son:
 *
 *      cmdStart    Maestro -> Esclavo   "empieza"
 *      statusBusy  Esclavo -> Maestro   "estoy trabajando"
 *      statusDone  Esclavo -> Maestro   "he terminado, la pieza es tuya"
 *      cmdAck      Maestro -> Esclavo   "recibido, puedes soltar"
 *
 *  Y la danza completa:
 *
 *      A (entrega)                        B (recoge)
 *      ───────────                        ──────────
 *      statusDone = true      ────────>   ve statusDone
 *                                         cmdAck = true      (acepta el relevo)
 *      ve cmdAck              <────────
 *      statusDone = false
 *      vuelve a reposo        ────────>   ve statusDone bajado
 *                                         cmdAck = false     (cierra el ciclo)
 *
 *  Fijate en el detalle: A no vuelve a reposo hasta ver el acuse, y B no baja
 *  el acuse hasta ver que A bajo el aviso. Con eso, ninguna de las dos puede
 *  adelantarse. El precio es cuatro booleanos y dos ciclos de scan extra.
 *
 *  VENTAJA DE DIAGNOSTICO
 *  ----------------------
 *  Si la linea se para, mirando estos cuatro bits sabes exactamente quien
 *  espera a quien:
 *
 *      statusDone=1, cmdAck=0  -> A entrego y B no lo acepta. Mira a B.
 *      statusBusy=1 mucho rato -> B se quedo colgada en su ciclo. Mira a B.
 *      todo a cero, A parada   -> nadie pidio nada. Mira al maestro.
 * ======================================================================== */

struct Handshake {
  /* Comandos: los escribe el maestro, los lee el esclavo. */
  bool cmdStart    = false;
  bool cmdAck      = false;

  /* Estados: los escribe el esclavo, los lee el maestro. */
  bool statusBusy  = false;
  bool statusDone  = false;
  bool statusError = false;

  /* Dato opcional que viaja con el testigo: identificador de pieza, resultado
   * de una inspeccion, numero de receta... Evita tener que anadir un canal de
   * comunicacion aparte para un solo valor. */
  uint16_t payload = 0;

  /* ---------------- Utilidades para el lado ESCLAVO ---------------- */

  /* Hay una peticion de arranque pendiente? */
  bool hasStartRequest() const { return cmdStart && !statusBusy && !statusDone; }

  /* Acepta la peticion y consume el comando. El consumo inmediato es lo que
   * convierte una senal mantenida en un pulso, y evita que la estacion
   * rearranque sola en cuanto termine. */
  void acceptStart() {
    cmdStart   = false;
    statusBusy = true;
    statusDone = false;
  }

  /* Anuncia que la tarea ha terminado. */
  void announceDone(uint16_t data = 0) {
    statusBusy = false;
    statusDone = true;
    payload    = data;
  }

  /* El maestro ya ha acusado recibo? */
  bool isAcknowledged() const { return cmdAck; }

  /* Cierra el traspaso por el lado esclavo. */
  void clearDone() { statusDone = false; }

  /* ---------------- Utilidades para el lado MAESTRO ---------------- */

  bool isReady() const  { return !statusBusy && !statusDone && !statusError; }
  bool isBusy()  const  { return statusBusy; }
  bool isDone()  const  { return statusDone; }
  bool hasError() const { return statusError; }

  void requestStart(uint16_t data = 0) {
    cmdStart = true;
    payload  = data;
  }

  void acknowledge()      { cmdAck = true; }
  void clearAcknowledge() { cmdAck = false; }

  /* ---------------- Comun ---------------- */

  /* Deja el handshake en su estado de reposo. Se llama en begin() y en el
   * rearme tras una alarma: si una averia interrumpe el traspaso a mitad, los
   * flags quedan descolocados y hay que limpiarlos antes de volver a producir. */
  void reset() {
    cmdStart = cmdAck = false;
    statusBusy = statusDone = statusError = false;
    payload = 0;
  }

  /* Volcado de diagnostico en una linea. */
  void describe(Print& out) const {
    out.print(CFSM_FSTR("HS[start="));  out.print(cmdStart    ? '1' : '0');
    out.print(CFSM_FSTR(" busy="));     out.print(statusBusy  ? '1' : '0');
    out.print(CFSM_FSTR(" done="));     out.print(statusDone  ? '1' : '0');
    out.print(CFSM_FSTR(" ack="));      out.print(cmdAck      ? '1' : '0');
    out.print(CFSM_FSTR(" err="));      out.print(statusError ? '1' : '0');
    out.print(CFSM_FSTR(" data="));     out.print(payload);
    out.print(']');
  }
};

/* ---------------------------------------------------------------------------
 *  Ayuda de alto nivel para el lado maestro
 * ---------------------------------------------------------------------------
 *  Encapsula la secuencia completa "esperar a que el vecino termine, acusar
 *  recibo y cerrar", que si se escribe a mano en cada estacion acaba
 *  copiandose mal en alguna.
 *
 *  Uso tipico dentro de una secuencia:
 *
 *      case PASO_ESPERAR_PIEZA:
 *        if (HandshakeMaster::collect(estacionAnterior.handshake)) {
 *          setStep(PASO_PROCESAR, 5000);
 *        }
 *        break;
 * ------------------------------------------------------------------------ */
namespace HandshakeMaster {

  /* Recoge el testigo del vecino. Devuelve true UNA sola vez, en el ciclo en
   * que el traspaso queda completado. */
  inline bool collect(Handshake& upstream) {
    if (upstream.statusDone && !upstream.cmdAck) {
      upstream.acknowledge();      /* "acepto el relevo" */
      return false;                /* aun no: falta que el vecino lo vea */
    }
    if (upstream.cmdAck && !upstream.statusDone) {
      upstream.clearAcknowledge(); /* el vecino ya bajo el aviso: cerramos */
      return true;                 /* traspaso completo */
    }
    return false;
  }

  /* Entrega el testigo al vecino de aguas abajo. Devuelve true cuando este ha
   * acusado recibo y por tanto ya se puede volver a reposo. */
  inline bool deliver(Handshake& own, uint16_t data = 0) {
    if (!own.statusDone) {
      own.announceDone(data);
      return false;
    }
    if (own.isAcknowledged()) {
      own.clearDone();
      return true;
    }
    return false;
  }
}

#endif /* COREFSM_HANDSHAKE_H */
