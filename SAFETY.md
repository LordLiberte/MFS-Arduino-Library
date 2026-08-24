# Seguridad funcional y uso responsable

CoreFSM es software educativo y de control general. **No es una biblioteca de
seguridad certificada** y no debe ser el único medio para proteger personas,
maquinaria o instalaciones.

## Parada de emergencia

La entrada denominada `EmergencyStop`, `E-Stop` o «seta» dentro del software es
un interbloqueo lógico. Puede detener secuencias y ordenar estados seguros, pero
depende de que el microcontrolador, firmware, alimentación, entradas, salidas y
comunicaciones continúen funcionando correctamente.

Una parada de emergencia real debe implementarse con circuitos, relés o PLC de
seguridad apropiados para el análisis de riesgos de la máquina. Debe retirar la
energía peligrosa sin depender de este firmware.

## Estados seguros

- Define el valor seguro de cada salida y comprueba el nivel eléctrico real,
  incluido `activeLow`.
- No asumas que dejar de ejecutar `update()` apaga un actuador: una orden puede
  quedar retenida hasta la siguiente escritura física.
- Diseña contactores, válvulas y drivers para caer a una condición segura ante
  reset, brown-out, watchdog, cable roto y pérdida de comunicación.
- Verifica el arranque: antes de que `setup()` termine, los pines pueden estar
  flotantes o adoptar valores impuestos por el bootloader.
- Tras un fallo o recuperación de bus, no reapliques automáticamente una orden
  antigua.

## Forzado y mantenimiento

El forzado de señales falsea la imagen de proceso. Úsalo solo durante una
intervención controlada, señaliza que está activo y libera todos los forzados
antes de devolver la máquina a operación. Nunca fuerces una función de
seguridad.

## Comunicaciones y sensores

Todo dato remoto debe llevar una noción de validez o edad. Al vencer un timeout,
la aplicación debe elegir un estado seguro; conservar indefinidamente el último
valor recibido suele ser peligroso. CRC y heartbeat detectan algunos fallos,
pero no convierten un transporte ordinario en un bus de seguridad.

## Validación antes de mover hardware

1. Prueba primero con actuadores desconectados o la máquina elevada y contenida.
2. Comprueba individualmente sentido, inversión y estado seguro de cada salida.
3. Simula sensores atascados, cable roto, rebotes, timeout y valores fuera de
   rango.
4. Corta alimentación y comunicaciones durante cada fase de la secuencia.
5. Mide el peor tiempo de scan y deja margen suficiente.
6. Verifica que liberar una parada no provoca rearranque automático.
7. Documenta las pruebas y repítelas tras cambios de firmware o cableado.

La licencia MIT excluye garantías, pero esa exclusión no sustituye el análisis
de riesgos, las normas aplicables ni la validación de la máquina completa.

