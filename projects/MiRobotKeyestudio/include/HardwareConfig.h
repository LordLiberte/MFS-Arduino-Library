/* =============================================================================
 *  HardwareConfig.h  -  GENERADO AUTOMATICAMENTE. NO EDITAR A MANO.
 * -----------------------------------------------------------------------------
 *  Origen : hardware.csv (csv)
 *  Nodo   : main
 *  Placa  : nano
 *  Senales: 0 entradas digitales, 2 salidas digitales, 0 analogicas
 *
 *  Edita la fuente indicada arriba y vuelve a generar. La API del programa
 *  permanece estable: HW.Nombre_De_La_Senal.
 *
 *      HW.LedMiniPCB.turnOn()
 * ========================================================================== */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

/* GPIO nativo: entradas digitales. */
#define CFSM_TABLE_DI(ROW) \
  /* (ninguna) */

/* GPIO nativo: salidas digitales con estado seguro false. */
#define CFSM_TABLE_DO(ROW) \
  ROW(    9, LedMiniPCB                      , false ) \
  ROW(   10, Servo                           , false )

/* GPIO nativo: entradas analogicas. */
#define CFSM_TABLE_AI(ROW) \
  /* (ninguna) */

#include <io/IOTable.h>

#endif /* HARDWARE_CONFIG_H */
