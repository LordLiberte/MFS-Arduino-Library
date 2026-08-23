#!/bin/bash
# Ejecuta el banco de pruebas en el PC. No hace falta placa ni Arduino IDE:
# solo g++. El reloj de la libreria es una variable, asi que se simulan horas
# de maquina en milisegundos y siempre con el mismo resultado.
cd "$(dirname "$0")"
g++ -std=gnu++11 -Wall -Wextra -DCFSM_DISABLE_NVM \
    -I stub -I ../src \
    banco.cpp stub/stub.cpp -o /tmp/banco_corefsm && /tmp/banco_corefsm
