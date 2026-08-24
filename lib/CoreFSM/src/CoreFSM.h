#ifndef COREFSM_H
#define COREFSM_H

/* ===========================================================================
 *  CoreFSM  -  Framework de automatizacion para Arduino
 *  Version 2.2.0
 * ---------------------------------------------------------------------------
 *  Traslada el modelo de programacion de un automata industrial a C++ sobre
 *  microcontroladores: ciclo de scan determinista, imagen de proceso de
 *  entradas y salidas, bloques funcionales no bloqueantes, secuencias por
 *  pasos con vigilancia de tiempos, palabras de mando y estado, recetas y
 *  gestion de alarmas.
 *
 *  INCLUSION
 *  ---------
 *  Basta con incluir este archivo:
 *
 *      #include <CoreFSM.h>
 *
 *  Si vas justo de flash, puedes incluir solo lo que uses:
 *
 *      #include <core/SequenceBlock.h>
 *      #include <core/BlockManager.h>
 *
 *  MAPA DE LA LIBRERIA
 *  -------------------
 *    core/    El motor. Bloques, estados, pasos, palabras de mando, scan.
 *    io/      El mundo fisico. Sensores, salidas, imagen de proceso, tablas.
 *    logic/   Temporizadores, flancos y contadores IEC 61131-3.
 *    drive/   Motores, cinematica de chasis y ejes posicionados.
 *    data/    Bloques de datos, configuracion persistente, recetas, alarmas.
 *    diag/    Trazas, telemetria y consola de mantenimiento.
 *    comms/   Sensores inteligentes por bus serie (vision).
 *    tools/   Generador neutral CSV/JSON con adaptador opcional para Wokwi.
 *
 *  EL CICLO DE SCAN, QUE ES LO UNICO QUE HAY QUE ENTENDER PARA EMPEZAR
 *  ------------------------------------------------------------------
 *      void loop() {
 *        HW.readInputs();        // PAE  - foto de todas las entradas
 *        manager.updateAll();    // OB1  - la logica calcula con esa foto
 *        HW.writeOutputs();      // PAA  - volcado de todas las salidas
 *      }
 *
 *  Tres lineas, siempre las mismas, en ese orden. Todo lo demas son bloques
 *  que se registran en el manager y objetos que se declaran en la tabla de
 *  hardware.
 * ======================================================================== */

/* --- Nucleo --- */
#include "core/CoreFSM_Platform.h"
#include "core/ControlWords.h"
#include "core/BlockBase.h"
#include "core/FsmBlock.h"
#include "core/SequenceBlock.h"
#include "core/BlockManager.h"
#include "core/Handshake.h"
#include "core/IAxis.h"

/* --- Bloques funcionales IEC 61131-3 --- */
#include "logic/Timers.h"
#include "logic/Edges.h"
#include "logic/Counters.h"

/* --- Entradas y salidas --- */
#include "io/IDevice.h"
#include "io/DigitalBackend.h"
#include "io/DeviceManager.h"
#include "io/DigitalSensor.h"
#include "io/DigitalOutput.h"
#include "io/AnalogSensor.h"
#include "io/TowerLight.h"
#include "io/UltrasonicSensor.h"
#include "io/IOManager.h"

/* --- Accionamientos --- */
#include "drive/MotorDrive.h"
#include "drive/Chassis.h"

/* --- Datos --- */
#include "data/ConfigStore.h"
#include "data/DataBlock.h"
#include "data/AlarmManager.h"
#include "data/RecipeTypes.h"
#include "data/RecipeExecutor.h"

/* --- Diagnostico --- */
#include "diag/Logger.h"
#include "diag/Telemetry.h"
#include "diag/ScanWatchdog.h"

/* --- Comunicaciones --- */
#include "comms/PacketLink.h"
#include "comms/RemoteIO.h"
#include "comms/VisionSensor.h"

/* NOTA: io/IOTable.h NO se incluye aqui a proposito.
 * Ese archivo genera codigo a partir de las macros CFSM_TABLE_DI / DO / AI, y
 * tiene que incluirse DESPUES de que las hayas definido. Su sitio es el final
 * de tu HardwareConfig.h. */

#endif /* COREFSM_H */
