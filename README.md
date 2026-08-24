# MFS Arduino Library

[![Compilar y probar](https://github.com/LordLiberte/MFS-Arduino-Library/actions/workflows/compilar.yml/badge.svg)](https://github.com/LordLiberte/MFS-Arduino-Library/actions/workflows/compilar.yml)
[![Licencia MIT](https://img.shields.io/badge/licencia-MIT-blue.svg)](LICENSE)

**CoreFSM 2.2** es un framework de automatización para Arduino y otros
microcontroladores. Organiza el firmware como un autómata industrial: captura
de entradas, lógica determinista y aplicación de salidas en cada scan, con
máquinas de estados, secuencias, recetas, alarmas y diagnóstico.

La lógica no depende del esquema ni de números de pin. El hardware puede
definirse con **CSV**, **JSON**, tablas C++ manuales o, de forma opcional, un
`diagram.json` de Wokwi. Todos los caminos producen la misma API simbólica:

```cpp
HW.Pulsador_Marcha.hasRisen();
HW.Valvula.set(true);
```

> CoreFSM es software de control general, no software de seguridad certificado.
> Lee [SAFETY.md](SAFETY.md) antes de conectar actuadores.

## Qué aporta

- Ciclo PAE → lógica → PAA sin `delay()` ni memoria dinámica.
- Bloques y secuencias con estados, pasos, pausas, timeouts y contadores.
- Entradas con antirrebote y flancos; salidas con inversión, modos y watchdog.
- Recetas, configuración persistente, alarmas y telemetría.
- Tabla de hardware generada desde una fuente neutral o escrita a mano.
- GPIO local y expansores MCP23017 con una captura/escritura agrupada por scan.
- Imágenes digitales entre placas sobre cualquier `Stream`, con COBS, CRC,
  secuencia, sesión y timeout.
- Pruebas host, pruebas del generador y compilación automática en GitHub.

## Inicio rápido

Necesitas VS Code con PlatformIO o Arduino IDE. El proyecto de referencia está
en [`projects/00_TestLibrary`](projects/00_TestLibrary):

1. Abre esa carpeta en VS Code.
2. Conecta un pulsador entre D2 y GND; el ejemplo usa el LED integrado D13.
3. Compila con `Ctrl+Alt+B` y carga con `Ctrl+Alt+U`.

Para crear una máquina nueva desde la raíz del repositorio:

```bash
python lib/CoreFSM/tools/nuevo_proyecto.py 01_cinta
python lib/CoreFSM/tools/nuevo_proyecto.py 02_brazo --placa esp32
```

El modo predeterminado crea `hardware.csv`. También están disponibles:

```bash
python lib/CoreFSM/tools/nuevo_proyecto.py 03_json --fuente json
python lib/CoreFSM/tools/nuevo_proyecto.py 04_wokwi --fuente wokwi
python lib/CoreFSM/tools/nuevo_proyecto.py 05_manual --fuente manual
```

## Una fuente de hardware, varios adaptadores

```text
 hardware.csv ─┐
 hardware.json ├─> corefsm_gen.py ─> HardwareConfig.h ─> HW.Nombre
 diagram.json ─┘        opcional

 HardwareConfig.h manual ───────────────────────────────> HW.Nombre
```

El generador busca `hardware.csv`, `hardware.json` y `diagram.json`, en ese
orden, salvo que `corefsm.json` seleccione una fuente. Wokwi ya no es un
requisito: queda como adaptador y simulador gráfico.

Ejemplo CSV:

```csv
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
main,Marcha,DI,gpio.2,true,,20,,
main,Valvula,DO,EXP1.8,,false,,,false
main,Presion,AI,A0,,,,3,
```

`EXP1.8` puede apuntar a un MCP23017 declarado en `corefsm.json`. Consulta
[fuentes de hardware](lib/CoreFSM/docs/hardware/sources.md),
[asignación manual](lib/CoreFSM/docs/hardware/manual-pinmap.md) y
[expansores](lib/CoreFSM/docs/hardware/io-expanders.md).

## Ciclo de scan

```cpp
void loop() {
  HW.readInputs();
  leerEntradas();
  manager.updateAll();
  escribirSalidas();
  HW.setSafetyInterlock(manager.isEmergencyStop());
  HW.writeOutputs();
}
```

`setEmergencyStop()` conserva su nombre por compatibilidad, pero implementa un
**interbloqueo de software**. El enlace con `HW.setSafetyInterlock()` fuerza el
valor seguro configurado de cada salida; no reemplaza relés, PLC ni circuitos
de seguridad apropiados.

## Más E/S y varias placas

Para ampliar E/S local, `Mcp23017Backend` mantiene una imagen de 16 bits y
agrupa las transacciones I2C. Para distribuir lógica, `PacketLink` y `RemoteIO`
intercambian snapshots direccionados sin acoplar la red a una máquina de
estados concreta.

El orden recomendado con red es:

```cpp
red.readInputs();
HW.readInputs();
manager.updateAll();
HW.writeOutputs();
red.writeOutputs();
```

No existe una fotografía global instantánea: todo dato remoto tiene edad y
validez. Consulta la [guía multinodo](lib/CoreFSM/docs/net/multi-controller.md)
y el ejemplo [`09_Dos_Nodos_UART`](lib/CoreFSM/examples/09_Dos_Nodos_UART).

## Ejemplos

| Nº | Tema |
|---:|---|
| 01 | primer bloque y ciclo de scan |
| 02 | proceso secuencial de soldadura |
| 03 | cinta, baliza y tabla simbólica |
| 04 | dos estaciones con handshake local |
| 05 | recetas, EEPROM y teach-in |
| 06 | robot de cuatro ruedas |
| 07 | seguimiento visual no bloqueante |
| 08 | esperas, takt y watchdog de scan |
| 09 | dos nodos por UART con timeout |
| 10 | expansor MCP23017 agrupado |

Están en [`lib/CoreFSM/examples`](lib/CoreFSM/examples).

## Organización del repositorio

```text
MFS-Arduino-Library/
├── lib/CoreFSM/
│   ├── src/              biblioteca pública
│   ├── docs/             referencia por módulos
│   ├── examples/         sketches compilables
│   ├── tests/            regresión en PC
│   └── tools/            generador y plantillas
├── projects/             un firmware compilable por máquina o nodo
├── .github/workflows/    compilación y pruebas automáticas
├── SAFETY.md
├── CHANGELOG.md
└── CONTRIBUTING.md
```

Una sola copia de `lib/CoreFSM` alimenta todos los proyectos. El CI descubre
automáticamente cada `projects/*/platformio.ini`, compila los ejemplos y ejecuta
las pruebas C++ y Python; además crea desde cero y compila un proyecto ESP32.

## Documentación

- [Guía de uso de CoreFSM](lib/CoreFSM/README.md)
- [Referencia técnica por módulos](lib/CoreFSM/docs/README.md)
- [Herramientas y formatos](lib/CoreFSM/tools/README.md)
- [Pruebas](lib/CoreFSM/tests/README.md)
- [Historial de cambios](CHANGELOG.md)
- [Cómo contribuir](CONTRIBUTING.md)

## Licencia

MIT. Consulta [LICENSE](LICENSE).
