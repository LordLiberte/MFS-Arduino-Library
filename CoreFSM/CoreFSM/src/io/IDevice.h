#ifndef COREFSM_IDEVICE_H
#define COREFSM_IDEVICE_H

#include "../core/CoreFSM_Platform.h"

/* ===========================================================================
 *  IDevice.h  -  Interfaz de todo objeto de campo
 * ---------------------------------------------------------------------------
 *  DOS FAMILIAS DE OBJETOS, NO UNA
 *  -------------------------------
 *  Esta libreria distingue con claridad dos tipos de cosas, siguiendo el
 *  reparto de la norma ISA-88:
 *
 *    IDevice   = Modulo de Control (CM). Es un trozo de hardware: un sensor,
 *                un piloto, una baliza, un motor. Sabe de pines, de tensiones
 *                y de rebotes. NO sabe nada del proceso.
 *
 *    BlockBase = Modulo de Equipo (EM). Es logica: una secuencia, una
 *                estacion, un coordinador. Sabe del proceso. NO sabe nada de
 *                pines.
 *
 *  Que sean interfaces distintas no es un capricho de organizacion: es lo que
 *  permite que el ciclo de scan tenga las tres fases separadas de un automata.
 *
 *  LAS TRES FASES Y POR QUE IMPORTAN
 *  ---------------------------------
 *      readInputs()   PAE - todos los sensores se leen a la vez, al principio
 *      update()       OB1 - la logica calcula con esa foto congelada
 *      writeOutputs() PAA - todas las salidas se escriben a la vez, al final
 *
 *  La consecuencia importante es la COHERENCIA. Como todas las entradas se
 *  leen en el mismo instante, la logica trabaja con una foto congelada del
 *  estado de la planta. Si en cambio hicieras digitalRead() en medio del
 *  codigo de proceso, un sensor podria cambiar a mitad del razonamiento y
 *  llegarias a conclusiones imposibles: por ejemplo, ver el cilindro en reposo
 *  al principio de la funcion y en trabajo al final, y activar dos salidas
 *  incompatibles. Los PLC hacen exactamente esto, y por el mismo motivo.
 *
 *  Ademas se gana rendimiento: digitalRead/digitalWrite de Arduino son
 *  sorprendentemente lentos (hacen busquedas en tablas y deshabilitan
 *  interrupciones). Agruparlos en dos rafagas ordenadas es mas rapido y
 *  predecible que salpicarlos por todo el programa.
 * ======================================================================== */

class IDevice {
  public:
    IDevice() : _name(nullptr), _forced(false) {}
    virtual ~IDevice() {}

    /* Configuracion del hardware: pinMode, buses, valores iniciales. */
    virtual void begin() = 0;

    /* Fase PAE. Los dispositivos de solo salida la dejan vacia. */
    virtual void readInputs() {}

    /* Fase PAA. Los dispositivos de solo entrada la dejan vacia. */
    virtual void writeOutputs() {}

    /* Nombre para el diagnostico, en memoria de programa. */
    void setName(const __FlashStringHelper* n) { _name = n; }
    const __FlashStringHelper* getName() const { return _name; }

    /* -----------------------------------------------------------------------
     *  FORZADO (FORCE)
     *  Reproduce la funcion de forzado de un PLC: desconectar una senal del
     *  mundo fisico y darle un valor a mano. Es la herramienta que mas se usa
     *  en una puesta en marcha, para poder probar la secuencia entera antes de
     *  que el armario este cableado, o para seguir produciendo mientras se
     *  cambia un sensor averiado.
     *
     *  Igual que en un PLC, es peligrosa: una senal forzada miente. Por eso
     *  hay isForced() y por eso la telemetria de esta libreria marca en la
     *  tabla de observacion todo lo que este forzado. Nunca dejes un forzado
     *  puesto al terminar.
     * -------------------------------------------------------------------- */
    bool isForced() const { return _forced; }
    void releaseForce()   { _forced = false; }

  protected:
    const __FlashStringHelper* _name;
    bool _forced;
};

#endif /* COREFSM_IDEVICE_H */
