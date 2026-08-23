#ifndef COREFSM_IAXIS_H
#define COREFSM_IAXIS_H

#include "CoreFSM_Platform.h"

/* ===========================================================================
 *  IAxis.h  -  Interfaz minima de un eje posicionable
 * ---------------------------------------------------------------------------
 *  Existe para romper una dependencia circular incomoda: el ejecutor de
 *  recetas necesita mover ejes, pero no debe saber si detras hay un motor de
 *  continua con potenciometro, un servo, un paso a paso o un cilindro
 *  neumatico con dos finales de carrera.
 *
 *  Con esta interfaz, el ejecutor habla siempre el mismo idioma ("vete a la
 *  posicion 120 a velocidad 180 y avisame cuando llegues") y cada tipo de eje
 *  lo resuelve como sepa. El dia que cambies un motor de continua por un servo
 *  de verdad, el ejecutor de recetas y todas las recetas guardadas siguen
 *  valiendo tal cual.
 * ======================================================================== */

class IAxis {
  public:
    virtual ~IAxis() {}

    /* Ordena ir a una posicion. No bloquea: solo fija la consigna. */
    virtual void moveTo(int16_t target, uint8_t speed) = 0;

    /* Ha llegado, dentro de la tolerancia indicada? */
    virtual bool inPosition(int16_t tolerance) const = 0;

    /* Posicion actual segun la realimentacion. */
    virtual int16_t position() const = 0;

    /* Detiene el movimiento manteniendo la posicion. */
    virtual void hold() = 0;

    /* Esta el eje referenciado? Un eje sin home no sabe donde esta y ninguna
     * coordenada de una receta significa nada para el. */
    virtual bool isHomed() const { return true; }
};

#endif /* COREFSM_IAXIS_H */
