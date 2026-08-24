#ifndef COREFSM_TOWER_LIGHT_H
#define COREFSM_TOWER_LIGHT_H

#include "DigitalOutput.h"

/* ===========================================================================
 *  TowerLight.h  -  Baliza de senalizacion (columna luminosa)
 * ---------------------------------------------------------------------------
 *  EL CODIGO DE COLORES NO ES ARBITRARIO
 *  -------------------------------------
 *  Esta clase adopta una convencion industrial habitual. El significado
 *  definitivo debe contrastarse con la norma, el mercado y la evaluacion de
 *  riesgos aplicables a cada maquina:
 *
 *      ROJO fijo        Averia. La maquina esta parada y necesita intervencion.
 *      ROJO intermitente Emergencia o situacion peligrosa. Actua ya.
 *      AMARILLO fijo    En espera, en pausa, o lista pero sin orden de marcha.
 *      AMARILLO intermit. Aviso: algo va a pasar (falta material, temperatura
 *                        subiendo, mantenimiento proximo). Aun produce.
 *      VERDE fijo        Produciendo con normalidad.
 *      VERDE intermitente Arrancando o terminando ciclo.
 *
 *  La regla de oro es la PRIORIDAD: solo se enciende un color a la vez, y el
 *  rojo gana siempre. Una baliza con dos colores encendidos no comunica nada;
 *  el operario mira, no entiende, y deja de fiarse de la baliza.
 *
 *  Los metodos de esta clase aplican esa prioridad automaticamente, de modo
 *  que no puedas equivocarte: no hay forma de dejar el verde y el rojo
 *  encendidos a la vez.
 * ======================================================================== */

class TowerLight {
  public:
    /* Los pilotos se pasan por referencia: son DigitalOutput normales que
     * tambien estan registrados en el DeviceManager. La baliza solo decide
     * QUE modo tiene cada uno; quien los escribe fisicamente sigue siendo la
     * fase PAA. Asi se mantiene la separacion de fases. */
    TowerLight(DigitalOutput& red, DigitalOutput& yellow, DigitalOutput& green,
               DigitalOutput* buzzer = nullptr)
      : _red(red), _yellow(yellow), _green(green), _buzzer(buzzer) {}

    /* --- Estados normalizados ------------------------------------------- */

    void setRunning() { apply(OUT_OFF, OUT_OFF, OUT_ON, false); }
    void setStarting(){ apply(OUT_OFF, OUT_OFF, OUT_BLINK_SLOW, false); }
    void setIdle()    { apply(OUT_OFF, OUT_ON,  OUT_OFF, false); }

    /* Esperando por causa externa: la maquina esta sana, arrancara sola en
     * cuanto le llegue material. Ambar fijo, como la maquina en reposo: para
     * el operario que cruza la nave, "no produce pero no esta rota". */
    void setSuspended(){ apply(OUT_OFF, OUT_ON,  OUT_OFF, false); }

    /* Esperando por causa interna: la maquina te esta reclamando. Ambar
     * intermitente, que es lo que pide atencion sin llegar a ser alarma. */
    void setHeld()    { apply(OUT_OFF, OUT_BLINK_SLOW, OUT_OFF, false); }
    void setPaused()  { apply(OUT_OFF, OUT_BLINK_SLOW, OUT_OFF, false); }
    void setWarning() { apply(OUT_OFF, OUT_BLINK_FAST, OUT_OFF, false); }
    void setFault()   { apply(OUT_ON,  OUT_OFF, OUT_OFF, true);  }
    void setEmergency(){apply(OUT_BLINK_FAST, OUT_OFF, OUT_OFF, true); }
    void setOff()     { apply(OUT_OFF, OUT_OFF, OUT_OFF, false); }

    /* Prueba de lamparas: enciende todo un momento. Puede formar parte del
     * diagnostico exigido por la aplicacion; la libreria no verifica lamparas
     * fundidas ni el cumplimiento de ninguna norma. */
    void lampTest()   { apply(OUT_ON, OUT_ON, OUT_ON, false); }

    /* --- Puente directo desde el estado de un bloque --------------------- */

    /* Traduce el SystemState de cualquier FsmBlock al color que corresponde.
     * Con esto, la baliza de tu maquina se resuelve en una sola linea del
     * loop() y siempre dice la verdad, sin cadenas de if repartidas. */
    void reflect(uint8_t systemState, bool emergency = false, bool warning = false) {
      if (emergency)       { setEmergency(); return; }
      switch (systemState) {
        case 6 /*STATE_ERROR*/:    setFault();    break;
        case 2 /*STATE_RUNNING*/:  warning ? setWarning() : setRunning(); break;
        case 3 /*STATE_PAUSED*/:   setPaused();   break;
        case 7 /*STATE_SUSPENDED*/:setSuspended();break;
        case 8 /*STATE_HELD*/:     setHeld();     break;
        case 1 /*STATE_STARTING*/:
        case 4 /*STATE_STOPPING*/: setStarting(); break;
        default:                   setIdle();     break;
      }
    }

    /* Silencia el zumbador sin apagar la luz. Es lo que hace el boton de
     * "acuse acustico" de un cuadro: el operario se ha enterado, pero la
     * alarma sigue activa y la luz roja debe seguir encendida. */
    void muteBuzzer() { if (_buzzer) _buzzer->turnOff(); _muted = true; }
    void unmute()     { _muted = false; }

  private:
    DigitalOutput& _red;
    DigitalOutput& _yellow;
    DigitalOutput& _green;
    DigitalOutput* _buzzer;
    bool _muted = false;

    void apply(OutputMode r, OutputMode y, OutputMode g, bool horn) {
      _red.setMode(r);
      _yellow.setMode(y);
      _green.setMode(g);
      if (_buzzer) _buzzer->setMode((horn && !_muted) ? OUT_BLINK_FAST : OUT_OFF);
      /* Cuando desaparece la condicion de bocina se rearma el silencio; la
       * siguiente alarma vuelve a sonar. */
      if (!horn) _muted = false;
    }
};

#endif /* COREFSM_TOWER_LIGHT_H */
