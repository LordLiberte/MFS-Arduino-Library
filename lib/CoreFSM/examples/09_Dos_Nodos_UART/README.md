# 09 · Dos nodos por UART

Este ejemplo intercambia una imagen digital entre dos Arduino. Carga el mismo
sketch en ambos, compilando uno con `CFSM_NODE_ID=1` y el otro con
`CFSM_NODE_ID=2`.

Conexiones: pin 11 (TX) de cada placa al pin 10 (RX) de la otra, y masa común.
Cada pulsador va entre D2 y GND; el LED integrado refleja el pulsador remoto.

La comunicación usa COBS, CRC, secuencia, sesión y timeout. Sigue siendo un
enlace de control general: no sustituye un bus ni un circuito de seguridad.

