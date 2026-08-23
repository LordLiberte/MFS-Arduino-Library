/* =============================================================================
 *  HardwareConfig.h  -  GENERADO AUTOMATICAMENTE. NO EDITAR A MANO.
 * -----------------------------------------------------------------------------
 *  Origen : diagram.json
 *  Placa  : nano
 *  Senales: 4 entradas digitales, 4 salidas digitales, 1 analogicas
 *
 *  Este archivo lo reescribe wokwi2corefsm.py cada vez que compilas. Cualquier
 *  cambio hecho aqui se perdera. Para modificar la asignacion de hardware:
 *
 *    - mueve el cable o renombra el componente en Wokwi (diagram.json), o
 *    - ajusta las opciones finas en corefsm.json
 *
 *  Cada fila de las tablas de abajo se convierte en un objeto completo, con su
 *  antirrebote, sus flancos y su capacidad de forzado:
 *
 *      HW.Pulsador_Marcha.hasRisen()
 * ========================================================================== */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

/* -----------------------------------------------------------------------------
 *  ENTRADAS DIGITALES (%I)
 *        PIN | NOMBRE SIMBOLICO                | PULL-UP | ANTIRREBOTE (ms)
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_DI(ROW) \
  ROW(    2, Pulsador_Marcha                 , true ,  20 ) \
  ROW(    3, Pulsador_Pausa                  , true ,  20 ) \
  ROW(    4, FC_Fin_Cinta                    , true ,   5 ) \
  ROW(    5, Pulsador_Rearme                 , true ,  20 )

/* -----------------------------------------------------------------------------
 *  SALIDAS DIGITALES (%Q)
 *        PIN | NOMBRE SIMBOLICO                | ACTIVA A NIVEL BAJO
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_DO(ROW) \
  ROW(   10, Motor_Cinta                     , false ) \
  ROW(   11, Luz_Verde                       , false ) \
  ROW(   12, Luz_Amarilla                    , false ) \
  ROW(   13, Luz_Roja                        , false )

/* -----------------------------------------------------------------------------
 *  ENTRADAS ANALOGICAS (%IW)
 *        PIN | NOMBRE SIMBOLICO                | FILTRO (0 = crudo .. 8 = muy suave)
 * -------------------------------------------------------------------------- */
#define CFSM_TABLE_AI(ROW) \
  ROW(   A0, Consigna_Velocidad              , 3 )

/* Expande las tablas: declara los objetos, los registra y genera la imagen de
 * proceso. Tiene que ir DESPUES de las tres tablas. */
#include <io/IOTable.h>

#endif /* HARDWARE_CONFIG_H */
