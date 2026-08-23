# tools/ — Generador de la tabla de hardware

## `wokwi2corefsm.py`

Convierte el `diagram.json` de Wokwi en el `HardwareConfig.h` de CoreFSM.

```bash
python3 wokwi2corefsm.py                                  # busca diagram.json aquí
python3 wokwi2corefsm.py -i ruta/diagram.json -o include/HardwareConfig.h
python3 wokwi2corefsm.py --check                          # valida sin escribir
```

Como hook de PlatformIO, en `platformio.ini`:

```ini
extra_scripts = pre:tools/wokwi2corefsm.py
```

El script detecta solo si lo ha invocado PlatformIO y, en ese caso, lee
`<proyecto>/diagram.json` y escribe `<proyecto>/include/HardwareConfig.h`.

### Clasificación de señales

**Por prefijo en el `id` del componente** (prioridad máxima):

| Prefijo | Papel |
|---|---|
| `DI_` `I_` `IN_` | entrada digital |
| `DO_` `Q_` `OUT_` | salida digital |
| `AI_` `E_` `AN_` | entrada analógica |

**Por tipo de componente** (respaldo automático): pulsadores e interruptores son
entradas, LEDs, relés y zumbadores son salidas, potenciómetros y LDR son
analógicas.

### Ajustes finos: `corefsm.json`

Colócalo junto al `diagram.json`:

```json
{
  "board": "nano",
  "defaults": { "debounce": 20 },
  "pins": {
    "3": { "name": "FC_Carro_Trabajo", "role": "DI", "pullup": true, "debounce": 5 },
    "7": { "name": "Rele_Bomba",       "role": "DO", "activeLow": true },
    "A0":{ "name": "Presion_Linea",    "role": "AI", "filter": 5 }
  },
  "ignore": ["led_decorativo", "display1"]
}
```

Las claves de `pins` son **números de pin de la placa**, que es lo único
inequívoco: un componente puede tener varios pines conectados.

### Qué comprueba

- Dos componentes en el mismo pin.
- Dos señales con el mismo nombre (no compilaría).
- Componentes cuyo tipo no sabe clasificar (los omite y avisa).
- Nombres que no son identificadores válidos de C++ (los convierte).

No sobrescribe el archivo si el contenido no ha cambiado, para no forzar
recompilaciones innecesarias.
