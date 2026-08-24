# Fuentes de asignación de hardware

> Wokwi es una fuente opcional. CoreFSM también genera la misma API
> `HW.Nombre` desde CSV, JSON neutral o una tabla C++ escrita a mano.

## Capacidades incluidas

| Fuente | Herramienta | Estado |
|---|---|---|
| C++ manual | ninguna | disponible |
| `hardware.csv` | `tools/corefsm_gen.py` | disponible |
| `hardware.json` | `tools/corefsm_gen.py` | disponible |
| Wokwi `diagram.json` | generador neutral o `wokwi2corefsm.py` compatible | disponible |
| KiCad, EasyEDA, EPLAN | — | sin adaptador incluido |

El generador usa solo la biblioteca estándar de Python. Busca, en este orden,
`hardware.csv`, `hardware.json` y `diagram.json`, salvo que `corefsm.json` o la
línea de comandos indiquen otra fuente.

## Uso

Desde la raíz de un proyecto:

```text
python ruta/a/CoreFSM/tools/corefsm_gen.py --project .
python ruta/a/CoreFSM/tools/corefsm_gen.py --project . --check
```

Por defecto escribe `include/HardwareConfig.h`. Opciones relevantes:

| Opción | Uso |
|---|---|
| `-i`, `--input` | fuente explícita |
| `-o`, `--output` | cabecera de salida |
| `--format auto|csv|json|wokwi` | formato cuando el nombre no basta |
| `--config` | manifiesto `corefsm.json` alternativo |
| `--node` | nodo/placa cuando la fuente contiene varios |
| `--check` | valida y muestra el modelo sin escribir |
| `--quiet` | suprime el resumen |

El archivo solo se reescribe si cambia el contenido. También puede usarse como
`extra_scripts` de PlatformIO; `custom_corefsm_node` selecciona un nodo.
`wokwi2corefsm.py` conserva comandos y proyectos antiguos, pero delega la lógica
en el generador neutral.

## CSV

El CSV exige exactamente estas columnas, aunque una celda pueda quedar vacía:

```csv
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
control,Marcha,DI,gpio.2,true,,20,,
control,Valvula,DO,EXP1.8,,false,,,false
control,Presion,AI,A0,,,,3,
```

Roles: `DI`, `DO` y `AI`. Destinos nativos: `gpio.2`, `2` o `A0`. Un destino
`EXP1.8` referencia el canal 8 de un backend declarado en `corefsm.json`.

## JSON neutral

```json
{
  "node": "control",
  "board": "nanoatmega328",
  "backends": [
    {"name": "EXP1", "driver": "MCP23017", "bus": "Wire", "address": "0x20"}
  ],
  "signals": [
    {"name": "Marcha", "role": "DI", "target": "gpio.2", "pullup": true, "debounce_ms": 20},
    {"name": "Valvula", "role": "DO", "target": "EXP1.8", "active_low": false, "safe": false}
  ]
}
```

`nodes` también puede agrupar varias definiciones. Si hay más de un candidato,
selecciona uno con `--node` o mediante `source.node` en `corefsm.json`.

En esta versión, el único driver de backend que genera la herramienta es
`MCP23017`, con dirección `0x20`..`0x27` y canales 0..15. Las entradas analógicas
solo admiten GPIO nativo.

## `corefsm.json`

El manifiesto puede seleccionar fuente, valores por defecto, nodos y backends:

```json
{
  "source": {"path": "hardware.csv", "format": "csv", "node": "control"},
  "defaults": {"pullup": true, "debounce_ms": 20, "active_low": false, "safe": false},
  "backends": [
    {"node": "control", "name": "EXP1", "driver": "MCP23017", "bus": "Wire", "address": 32}
  ]
}
```

Para Wokwi, además admite `ignore`, `pins` y el identificador de placa. Si un
`diagram.json` contiene varias placas, la selección es obligatoria; ya no se
toma silenciosamente la primera.

## Validaciones

El generador rechaza identificadores C++ inválidos o reservados, nombres y
destinos repetidos, opciones incompatibles con el rol, canales fuera de rango,
direcciones I2C duplicadas en el mismo bus y nodos ambiguos. Wokwi reconoce
conexiones directas placa-componente; no recorre redes eléctricas generales ni
elementos intermedios como una netlist CAD.

## Contrato de salida

La salida define las X-macros que consume [`IOTable.h`](../io/IOTable.md),
incluidas las variantes de backend y estado seguro cuando proceda. El runtime
no sabe si la tabla nació en CSV, JSON, Wokwi o a mano.

Versiona la fuente, `corefsm.json` y `HardwareConfig.h`. En CI, ejecuta primero
`--check` y compila la cabecera generada. Para no usar Python en absoluto,
consulta [asignación manual](manual-pinmap.md).
