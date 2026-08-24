# Banco de pruebas en PC

No hace falta placa. `Arduino.h` esta simulado en `stub/`, y lo importante es
que **`millis()` y `micros()` son variables normales**: el banco puede adelantar
el reloj diez minutos en un bucle y comprobar que la maquina hace lo que debe,
siempre con el mismo resultado.

```bash
./ejecutar.sh
```

Salida esperada al final de cada banco:

```text
=== 89 comprobaciones, 0 fallos ===
=== 35 comprobaciones, 0 fallos ===
```

## Que cubre

| | Escenario |
|---|---|
| P1 | Espera declarada de 10 min: sin alarma, reloj de ciclo congelado, tiempo al contador de espera |
| P2 | La plantilla de la 2.0 **sin tocar una linea**: 5 min en reposo sin caer en timeout de ciclo |
| P3 | Secuencia que rebota entre dos pasos vigilados: **sigue** dando alarma de ciclo |
| P4 | Vigilancia de paso en dos escalones: avisa a los 2 s, para a los 5, hook llamado una sola vez |
| P5 | Espera externa a mitad de ciclo: 2 min sin pieza no son una averia |
| P6 | Red de seguridad: si se deja de pedir la espera, vuelve sola a RUNNING |
| P7 | Regresion: pausa, reanudacion y parada siguen funcionando |
| P8 | Pausa **durante** una espera: los dos tiempos no se mezclan |
| P9 | Takt objetivo: avisa, no para, y el aviso se borra al cerrar el ciclo |
| P10 | ScanWatchdog: mide, detecta el exceso, no se queda pegado, y el watchdog HW nace apagado |
| P11 | Regresion: mando por palabra CFGW, incluida la parada rapida activa a bajo |
| P12 | El ejemplo 08 se comporta como prometen sus comentarios |

`banco_io.cpp` añade 35 comprobaciones de E/S y comunicaciones: antirrebote e
inversión lógica, captura y commit agrupados, estado seguro enclavado, salud de
backends, buffer de transmisión lleno, dead-time de motor, reinicio de visión y
snapshots remotos con CRC, secuencia, sesión, duplicados, pérdida y recuperación
del enlace.

## Anadir una prueba

```cpp
{ printf("P13 Lo que sea\n");
  g_ms = 0; MiBloque b; b.begin(); b.start();
  avanzar(b, 5000);                       // 5 s simulados
  CHECK(b.getStep() == PASO_X, "deberia estar en X");
}
```

`avanzar()` da un scan por milisegundo, que es pesimista a proposito: una
maquina real hace muchos mas.
