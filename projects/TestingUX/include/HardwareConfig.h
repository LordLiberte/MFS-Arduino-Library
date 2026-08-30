/* =============================================================================
 *  HardwareConfig.h  -  GENERADO AUTOMATICAMENTE. NO EDITAR A MANO.
 * -----------------------------------------------------------------------------
 *  Origen : hardware.csv (csv)
 *  Nodo   : main
 *  Placa  : uno
 *  Senales: 1 entradas digitales, 1 salidas digitales, 0 analogicas
 *
 *  Edita la fuente indicada arriba y vuelve a generar. La API del programa
 *  permanece estable: HW.Nombre_De_La_Senal.
 *
 *      HW.Pulsador_Marcha.hasRisen()
 * ========================================================================== */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <CoreFSM.h>

/* GPIO nativo: entradas digitales. */
#define CFSM_TABLE_DI(ROW) \
  ROW(    2, Pulsador_Marcha                 , true ,  20 )

/* GPIO nativo: salidas digitales con estado seguro false. */
#define CFSM_TABLE_DO(ROW) \
  ROW(   13, Piloto_Trabajo                  , false )

/* GPIO nativo: entradas analogicas. */
#define CFSM_TABLE_AI(ROW) \
  /* (ninguna) */

#include <io/IOTable.h>

#endif /* HARDWARE_CONFIG_H */
