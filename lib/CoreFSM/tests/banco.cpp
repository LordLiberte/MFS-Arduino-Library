/* Banco de pruebas de CoreFSM 2.2 sobre PC. El reloj es una variable, asi que
 * se simulan horas de maquina en milisegundos y de forma reproducible. */
#include <Arduino.h>
#include <CoreFSM.h>
#include <cstdio>
#include "../examples/08_Esperas_y_Ritmo/Dosificadora.h"

static int fallos = 0, pruebas = 0;
#define CHECK(cond, msg) do { pruebas++; if (!(cond)) { fallos++; \
  printf("   FALLO: %s   (linea %d)\n", msg, __LINE__); } } while (0)

static void avanzar(SequenceBlock& b, unsigned long ms) {
  for (unsigned long i = 0; i < ms; i++) { g_ms++; b.update(); }
}

/* =====================================================================
 *  1. La secuencia de Carlos: verde -> rojo, con espera declarada
 * ================================================================== */
enum { Inizalizacion = 0, Waiting_CMD = 10, ON_GreenLed = 20, ON_RedLed = 30 };

class Proceso : public SequenceBlock {
 public:
  bool ordenMarcha = false, GreenLed = false, RedLed = false;
  uint16_t tiempoTrabajoMs = 1500;
  int avisos = 0;
  void begin() override {
    setName(F("PROCESO")); setInitialStep(Inizalizacion);
    setStep(Inizalizacion); setCycleTimeout(30000);
  }
  void update() override {
    if (!updateSequence()) { GreenLed = false; RedLed = false; return; }
    switch (_currentStep) {
      case Inizalizacion: GreenLed = RedLed = false; setStep(Waiting_CMD); break;
      case Waiting_CMD:
        GreenLed = RedLed = false;
        if (holdWhile(!ordenMarcha)) break;
        setStep(ON_GreenLed, 2000, 5000);
        break;
      case ON_GreenLed:
        GreenLed = true; RedLed = false;
        if (getTimeInStep() >= tiempoTrabajoMs) setStep(ON_RedLed, 2000, 5000);
        break;
      case ON_RedLed:
        GreenLed = false; RedLed = true;
        if (getTimeInStep() >= tiempoTrabajoMs) { RedLed = false; completeCycle(); }
        break;
    }
  }
 protected:
  void onStepWarning(uint16_t) override { avisos++; }
};

/* =====================================================================
 *  2. La plantilla ORIGINAL, sin tocar una linea. Es la prueba de que el
 *     codigo que ya existe mejora sin reescribirlo.
 * ================================================================== */
enum { V_REPOSO = 0, V_TRABAJO = 10 };
class ProcesoViejo : public SequenceBlock {
 public:
  bool ordenMarcha = false, salida = false;
  uint16_t tiempoTrabajoMs = 1500;
  void begin() override {
    setName(F("VIEJO")); setInitialStep(V_REPOSO);
    setStep(V_REPOSO); setCycleTimeout(30000);
  }
  void update() override {
    if (!updateSequence()) { salida = false; return; }
    switch (_currentStep) {
      case V_REPOSO:
        salida = false;
        if (ordenMarcha) setStep(V_TRABAJO, 5000);
        break;
      case V_TRABAJO:
        salida = true;
        if (getTimeInStep() >= tiempoTrabajoMs) { salida = false; completeCycle(V_REPOSO); }
        break;
    }
  }
};

/* =====================================================================
 *  3. Ping-pong: dos pasos vigilados que rebotan sin terminar nunca
 * ================================================================== */
class PingPong : public SequenceBlock {
 public:
  void begin() override { setInitialStep(0); setStep(0, 5000); setCycleTimeout(30000); }
  void update() override {
    if (!updateSequence()) return;
    switch (_currentStep) {
      case 0:  if (getTimeInStep() >= 4000) setStep(10, 5000); break;
      case 10: if (getTimeInStep() >= 4000) setStep(20, 5000); break;
      case 20: if (getTimeInStep() >= 4000) setStep(10, 5000); break;
    }
  }
};

/* =====================================================================
 *  4. Espera EXTERNA a mitad de ciclo (falta pieza)
 * ================================================================== */
class ConPieza : public SequenceBlock {
 public:
  bool piezaPresente = false;
  void begin() override { setInitialStep(0); setStep(0); setCycleTimeout(10000); }
  void update() override {
    if (!updateSequence()) return;
    switch (_currentStep) {
      case 0:  setStep(10); break;                       /* arranca el ciclo */
      case 10:                                            /* trabaja 2 s     */
        if (getTimeInStep() >= 2000) setStep(20);
        break;
      case 20:                                            /* espera pieza    */
        if (suspendWhile(!piezaPresente)) break;
        setStep(30);
        break;
      case 30:
        if (getTimeInStep() >= 1000) completeCycle();
        break;
    }
  }
};

/* =====================================================================
 *  5. Bloque que deja de pedir la espera (red de seguridad)
 * ================================================================== */
class Olvidadizo : public SequenceBlock {
 public:
  bool pedir = true;
  void begin() override { setInitialStep(0); setStep(0); }
  void update() override {
    if (!updateSequence()) return;
    if (pedir) suspendWhile(true);
  }
};

int main() {
  printf("=== BANCO DE PRUEBAS CoreFSM 2.2 ===\n\n");

  /* ---- P1: espera declarada de 10 minutos, sin falsa alarma ---- */
  { printf("P1  Espera declarada larga (10 min) sin falsa alarma\n");
    g_ms = 0; Proceso p; p.begin(); p.start();
    avanzar(p, 600000UL);
    CHECK(!p.isFaulted(), "no debe caer en alarma esperando");
    CHECK(p.getState() == STATE_HELD, "debe quedar en HELD");
    CHECK(p.ST.stw.held, "stw.held debe estar a 1");
    CHECK(!p.ST.stw.running, "stw.running debe estar a 0 mientras espera");
    CHECK(p.getCycleTime() < 100, "el reloj de ciclo debe estar congelado");
    CHECK(p.getBlockedTime() > 599000UL, "el tiempo debe ir al contador de espera");
    /* y ahora que produzca */
    p.ordenMarcha = true; avanzar(p, 5);
    CHECK(p.getState() == STATE_RUNNING, "al llegar la orden vuelve a RUNNING");
    avanzar(p, 3200);
    CHECK(p.getCycleCount() == 1, "debe haber completado un ciclo");
    CHECK(p.getLastCycleTime() >= 2900 && p.getLastCycleTime() <= 3200,
          "el ciclo productivo debe medir ~3 s, no 10 min");
    CHECK(p.getLastBlockedTime() > 599000UL, "la espera se contabiliza aparte");
    CHECK(p.avisos == 0, "sin avisos: los pasos van dentro de tiempo");
  }

  /* ---- P2: plantilla original sin tocar, reposo de 5 min ---- */
  { printf("P2  Plantilla ORIGINAL sin modificar, 5 min en reposo\n");
    g_ms = 0; ProcesoViejo v; v.begin(); v.start();
    avanzar(v, 300000UL);
    CHECK(!v.isFaulted(), "el reposo en el paso inicial ya no dispara el ciclo");
    v.ordenMarcha = true; avanzar(v, 2);
    v.ordenMarcha = false; avanzar(v, 2000);
    CHECK(v.getCycleCount() == 1, "y sigue produciendo igual que antes");
    CHECK(v.ST.stw.done, "done debe sobrevivir al cierre de ciclo");
    CHECK(!v.isFaulted(), "sin alarmas durante el ciclo");
  }

  /* ---- P3: el ping-pong SIGUE disparando ---- */
  { printf("P3  Ping-pong entre pasos vigilados: debe seguir cazandolo\n");
    g_ms = 0; PingPong pp; pp.begin(); pp.start();
    avanzar(pp, 45000UL);
    CHECK(pp.isFaulted(), "una secuencia que no termina nunca debe dar alarma");
    CHECK(pp.getErrorCode() == CFSM_ERR_CYCLE_TIMEOUT, "y debe ser timeout de ciclo");
  }

  /* ---- P4: aviso de paso antes del fallo ---- */
  { printf("P4  Vigilancia de paso en dos escalones\n");
    g_ms = 0; Proceso p; p.begin(); p.start();
    p.ordenMarcha = true; avanzar(p, 5);
    p.tiempoTrabajoMs = 60000;               /* el paso verde no acabara nunca */
    avanzar(p, 1900);
    CHECK(!p.ST.stw.stepWarn, "antes del tiempo de aviso, sin aviso");
    CHECK(p.avisos == 0, "el hook aun no se ha llamado");
    avanzar(p, 200);
    CHECK(p.ST.stw.stepWarn, "pasados 2 s debe haber aviso");
    CHECK(p.ST.stw.warning, "y el aviso general debe reflejarlo");
    CHECK(p.avisos == 1, "el hook se llama UNA vez, no en cada scan");
    CHECK(!p.isFaulted(), "pero el aviso NO para la maquina");
    avanzar(p, 1800);
    CHECK(p.avisos == 1, "sigue siendo una sola llamada");
    CHECK(!p.isFaulted(), "a los 4 s aun no ha vencido el limite duro");
    avanzar(p, 1200);
    CHECK(p.isFaulted(), "a los 5 s si debe parar");
    CHECK(p.getErrorCode() == CFSM_ERR_STEP_TIMEOUT, "y debe ser timeout de paso");
    CHECK(p.ST.stw.stepTimeout, "con el bit de causa puesto");
  }

  /* ---- P5: espera externa a mitad de ciclo ---- */
  { printf("P5  Espera EXTERNA a mitad de ciclo (falta pieza)\n");
    g_ms = 0; ConPieza c; c.begin(); c.start();
    avanzar(c, 2100);
    CHECK(c.getStep() == 20, "debe estar esperando la pieza");
    CHECK(c.getState() == STATE_SUSPENDED, "y en SUSPENDED, no en RUNNING");
    CHECK(c.ST.stw.suspended, "stw.suspended a 1");
    cfsm_time_t antes = c.getCycleTime();
    avanzar(c, 120000UL);                    /* dos minutos sin pieza */
    CHECK(!c.isFaulted(), "dos minutos sin pieza no son una averia");
    CHECK(c.getCycleTime() <= antes + 5, "el reloj de ciclo no avanza esperando");
    CHECK(c.getBlockedTime() > 119000UL, "pero el de espera si");
    c.piezaPresente = true; avanzar(c, 1100);
    CHECK(c.getCycleCount() == 1, "al llegar la pieza termina el ciclo");
    CHECK(c.getLastCycleTime() < 4000, "ciclo productivo ~3 s");
    CHECK(c.getLastBlockedTime() > 119000UL, "espera ~2 min, contada aparte");
  }

  /* ---- P6: red de seguridad, dejar de pedir la espera ---- */
  { printf("P6  Si se deja de pedir la espera, vuelve solo a RUNNING\n");
    g_ms = 0; Olvidadizo o; o.begin(); o.start();
    avanzar(o, 10);
    CHECK(o.getState() == STATE_SUSPENDED, "primero espera");
    o.pedir = false; avanzar(o, 3);
    CHECK(o.getState() == STATE_RUNNING, "y se libera sola al dejar de pedirlo");
  }

  /* ---- P7: la pausa sigue funcionando (regresion) ---- */
  { printf("P7  Regresion: pausa, reanudacion y parada\n");
    g_ms = 0; Proceso p; p.begin(); p.start();
    p.ordenMarcha = true; avanzar(p, 5); p.ordenMarcha = false;
    avanzar(p, 500);
    CHECK(p.getStep() == ON_GreenLed, "trabajando");
    p.hold(); avanzar(p, 10000);
    CHECK(p.getState() == STATE_PAUSED, "la pausa pausa");
    CHECK(!p.GreenLed, "y apaga las salidas");
    CHECK(!p.isFaulted(), "una pausa larga no es una alarma");
    cfsm_time_t tPaso = p.getTimeInStep();
    p.resume(); avanzar(p, 5);
    CHECK(p.getState() == STATE_RUNNING, "y se reanuda");
    CHECK(p.getTimeInStep() <= tPaso + 50, "sin haber consumido el tiempo de paso");
    p.stop(); avanzar(p, 5);
    CHECK(p.getState() == STATE_IDLE || p.getState() == STATE_STOPPED, "y para");
  }

  /* ---- P8: pausa mientras espera: los tiempos no se mezclan ---- */
  { printf("P8  Pausa DURANTE una espera: cada tiempo a su contador\n");
    g_ms = 0; ConPieza c; c.begin(); c.start();
    avanzar(c, 2100);
    CHECK(c.getState() == STATE_SUSPENDED, "esperando pieza");
    avanzar(c, 5000);
    c.hold(); avanzar(c, 8000);
    CHECK(c.getState() == STATE_PAUSED, "la pausa gana sobre la espera");
    cfsm_time_t esperaTrasPausa = c.getBlockedTime();
    CHECK(esperaTrasPausa >= 4900 && esperaTrasPausa <= 5200,
          "solo los 5 s de espera cuentan como espera, no los 8 de pausa");
    c.resume(); avanzar(c, 10);
    CHECK(!c.isFaulted(), "sin alarma tras la maniobra");
  }

  /* ---- P9: takt objetivo avisa pero no para ---- */
  { printf("P9  Takt objetivo: avisa, no para\n");
    g_ms = 0; Proceso p; p.begin(); p.start();
    p.setCycleTarget(2000);                  /* ciclo real ~3 s */
    p.ordenMarcha = true; avanzar(p, 5); p.ordenMarcha = false;
    avanzar(p, 2500);
    CHECK(p.isOverTakt(), "debe avisar de que se pasa del takt");
    CHECK(p.ST.stw.warning, "con el bit de aviso");
    CHECK(!p.isFaulted(), "pero sin parar la maquina");
    avanzar(p, 1000);
    CHECK(p.getCycleCount() == 1, "el ciclo termina igual");
    CHECK(!p.isOverTakt(), "y el aviso se borra al cerrar el ciclo");
  }

  /* ---- P10: watchdog de scan ---- */
  { printf("P10 Watchdog de scan\n");
    ScanWatchdog w(20);
    g_us = 0;
    for (int i = 0; i < 100; i++) { w.begin(); g_us += 1500; w.end(); }
    CHECK(w.maxUs() == 1500, "mide el scan");
    CHECK(w.overruns() == 0, "1,5 ms no se pasa de 20 ms");
    CHECK(!w.isOverrun(), "y no marca exceso");
    CHECK(w.headroomPct() > 90, "margen amplio");
    w.begin(); g_us += 25000; w.end();       /* alguien metio un delay */
    CHECK(w.isOverrun(), "25 ms si se pasa");
    CHECK(w.overruns() == 1, "y lo cuenta");
    CHECK(w.worstOverrunUs() == 25000, "guardando el peor caso");
    CHECK(w.headroomPct() == 0, "sin margen");
    w.begin(); g_us += 1000; w.end();
    CHECK(!w.isOverrun(), "el aviso es por scan, no se queda pegado");
    CHECK(w.overruns() == 1, "pero el contador acumulado se mantiene");
    CHECK(!w.hardwareWatchdogArmed(), "el watchdog hardware nace APAGADO");
  }

  /* ---- P11: la palabra de mando sigue gobernando ---- */
  { printf("P11 Regresion: mando por CFGW\n");
    g_ms = 0; ProcesoViejo v; v.begin();
    v.ST.cfgw.start = true; avanzar(v, 3);
    CHECK(v.getState() == STATE_RUNNING, "arranca por palabra de mando");
    CHECK(!v.ST.cfgw.start, "y el bit se consume");
    v.ST.cfgw.holdRequest = true; avanzar(v, 3);
    CHECK(v.getState() == STATE_PAUSED, "pausa por palabra de mando");
    v.ST.cfgw.holdRequest = false; avanzar(v, 3);
    CHECK(v.getState() == STATE_RUNNING, "y reanuda en el flanco de bajada");
    v.ST.cfgw.quickStop = false; avanzar(v, 3);
    CHECK(v.isFaulted() && v.getErrorCode() == CFSM_ERR_ESTOP,
          "la parada rapida activa a bajo sigue funcionando");
  }

  /* ---- P12: el ejemplo 08 hace lo que dicen sus comentarios ---- */
  { printf("P12 El ejemplo 08 se comporta como promete\n");
    g_ms = 0; Dosificadora d; d.begin(); d.start();
    avanzar(d, 10);
    CHECK(d.getStep() == PASO_ESPERA_BOTE, "espera bote");
    CHECK(d.getState() == STATE_SUSPENDED, "y lo hace en SUSPENDED");
    avanzar(d, 600000UL);                       /* diez minutos sin bote */
    CHECK(!d.isFaulted(), "diez minutos sin bote NO son una averia");
    CHECK(d.getCycleTime() < 100, "el ciclo productivo no ha empezado");

    d.botePresente = true; avanzar(d, 2100);    /* llena 2 s */
    CHECK(d.getCycleCount() == 0, "aun no ha cerrado");
    CHECK(!d.isFaulted(), "el llenado va dentro de tiempo");
    CHECK(d.avisosLlenado == 0, "y sin aviso: 2 s < 2,5 s de aviso");
    avanzar(d, 600);
    CHECK(d.getCycleCount() == 1, "cierra el ciclo tras expulsar");
    CHECK(d.getLastCycleTime() < 4000, "productivo ~2,5 s");
    CHECK(d.getLastBlockedTime() > 599000UL, "espera ~10 min, aparte");

    /* Ahora con el deposito bajo: espera INTERNA */
    d.depositoBajo = true; d.botePresente = true;
    avanzar(d, 2600);
    CHECK(d.getStep() == PASO_RECARGA, "pide recarga");
    CHECK(d.getState() == STATE_HELD, "y eso es HELD, no SUSPENDED");
    avanzar(d, 120000UL);
    CHECK(!d.isFaulted(), "dos minutos recargando tampoco son averia");
    d.acuseRecarga = true; avanzar(d, 3); d.acuseRecarga = false;
    avanzar(d, 600);
    CHECK(d.getCycleCount() == 2, "tras el acuse, el ciclo se cierra");

    /* Y una valvula atascada: el llenado no termina */
    d.depositoBajo = false;
    d.tiempoLlenadoMs = 60000;                  /* no llenara nunca */
    avanzar(d, 2600);
    CHECK(d.avisosLlenado == 1, "primero avisa a los 2,5 s");
    CHECK(!d.isFaulted(), "y NO para la maquina");
    avanzar(d, 1600);
    CHECK(d.isFaulted(), "a los 4 s si para");
    CHECK(d.getErrorCode() == ALM_VALVULA_ATASCADA,
          "con el codigo propio de la maquina, no el generico");
  }

  printf("\n=== %d comprobaciones, %d fallos ===\n", pruebas, fallos);
  return fallos ? 1 : 0;
}
