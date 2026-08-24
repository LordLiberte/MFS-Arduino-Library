#ifndef EJEMPLO_MCP23017_HARDWARE_CONFIG_H
#define EJEMPLO_MCP23017_HARDWARE_CONFIG_H

#include <CoreFSM.h>
#include <io/Mcp23017Backend.h>

#define CFSM_TABLE_BACKEND(ROW) \
  ROW(Mcp23017Backend, Expansor1, Wire, 0x20)

#define CFSM_TABLE_DI_BACKEND(ROW) \
  ROW(Expansor1, 0, Pulsador_Expansor, true, 20)

#define CFSM_TABLE_DO_BACKEND(ROW) \
  ROW(Expansor1, 8, Rele_Expansor, false, false)

#include <io/IOTable.h>

#endif
