#!/bin/bash
# Ejecuta el banco de pruebas en el PC. No hace falta placa ni Arduino IDE:
# solo g++. El reloj de la libreria es una variable, asi que se simulan horas
# de maquina en milisegundos y siempre con el mismo resultado.
set -eu
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

for banco in banco banco_io; do
  "$CXX" -std=gnu++11 -Wall -Wextra -Werror -DCFSM_DISABLE_NVM \
      -I stub -I ../src \
      "$banco.cpp" stub/stub.cpp -o "$TMP_DIR/$banco"
  "$TMP_DIR/$banco"
done
