# Herramientas de CoreFSM

Las herramientas traducen una descripción de hardware a
`include/HardwareConfig.h` y crean proyectos con una estructura uniforme. Solo
usan la biblioteca estándar de Python.

## Crear un proyecto

Desde la raíz del repositorio:

```bash
python lib/CoreFSM/tools/nuevo_proyecto.py 01_cinta
python lib/CoreFSM/tools/nuevo_proyecto.py 02_brazo --placa esp32
```

Placas: `nano`, `uno`, `mega` y `esp32`. La fuente predeterminada es CSV:

```bash
python lib/CoreFSM/tools/nuevo_proyecto.py 03_csv --fuente csv
python lib/CoreFSM/tools/nuevo_proyecto.py 04_json --fuente json
python lib/CoreFSM/tools/nuevo_proyecto.py 05_wokwi --fuente wokwi
python lib/CoreFSM/tools/nuevo_proyecto.py 06_manual --fuente manual
```

| Modo | Fuente canónica | Generación automática |
|---|---|---|
| `csv` | `hardware.csv` | sí |
| `json` | `hardware.json` | sí |
| `wokwi` | `diagram.json` | sí |
| `manual` | `include/HardwareConfig.h` | no |

`--sin-wokwi` sigue aceptándose como alias obsoleto de `--fuente csv`. Todos
los modos crean un proyecto que compila de entrada; el modo manual no instala
ningún hook de generación.

Opciones adicionales:

- `--dir RUTA`: elige el contenedor de proyectos. Puede ser relativa, absoluta
  o estar a cualquier profundidad; las rutas hacia CoreFSM se calculan desde el
  proyecto generado.
- `--forzar`: sobrescribe archivos de una carpeta ya existente.

## CoreFSM Studio

El entorno gráfico. Es un servidor local de la biblioteca estándar más una
aplicación web servida desde `studio/static/`; no instala nada ni sale a
internet.

```bash
python lib/CoreFSM/tools/corefsm_studio.py        # abre http://127.0.0.1:8765
python lib/CoreFSM/tools/corefsm_studio.py --port 0 --no-browser
```

En Windows, el acceso directo `CoreFSM Studio.cmd` de la raíz busca primero el
Python de PlatformIO, que es el que trae `pyserial` y habilita el monitor.

### Qué hay dentro

| Vista | Para qué |
|---|---|
| Portal | Pantalla de tareas y lista de proyectos. Es por donde se empieza. |
| Tabla de variables | Nombre, tipo, pin y comportamiento de cada señal. Escribe `hardware.csv`. |
| Dispositivos | Motores, sonares, servos y chasis, con sus pines agrupados por función. |
| Vista de dispositivo | Mapa de pines de la placa, con colisiones y avisos de PWM. |
| Bloques de datos | Los ajustes de la máquina en un sitio, con tipo y valor inicial. |
| Secuencia | Pasos, transiciones y el diagrama tipo Grafcet. |
| Programación | Editor con autocompletado de los métodos reales de cada objeto. |
| Monitor | Puerto serie: paso activo iluminado sobre el diagrama y mando remoto. |

### Importar desde otra herramienta

El botón «Importar» de la cinta —y el de la tabla de variables— abre una entrada
única para cualquier fuente:

| Fuente | Qué se le da |
|---|---|
| Wokwi | el `diagram.json` del proyecto |
| Hoja de cálculo o esquemático | un `hardware.csv` con `name,role,target,…` |
| Formato propio | un `hardware.json` |

Se puede arrastrar el archivo o pegar su contenido; el formato se detecta solo.
Antes de tocar el proyecto, Studio enseña qué señales ha encontrado, cuáles
chocan con nombres que ya existen y los avisos del generador; después se elige
entre **añadir a la tabla** (respetando lo que ya hay) o **reemplazarla**.

Detrás no hay un lector nuevo: es el mismo `corefsm_gen.py` que usa la
compilación, así que lo que importa Studio y lo que compila PlatformIO no pueden
divergir.

### El modelo del proyecto

Studio guarda su modelo en `corefsm.project.json` dentro del proyecto y, al
guardar, deriva de él:

```
corefsm.project.json
   ├──▶ hardware.csv ──▶ corefsm_gen.py ──▶ include/HardwareConfig.h
   ├──▶ corefsm.json         (nodo y placa)
   ├──▶ platformio.ini       (platform y board)
   ├──▶ include/generated/ProjectData.h · ProjectStates.h · ProjectDevices.h
   └──▶ src/generated/ProjectData.cpp · ProjectDevices.cpp
```

Un proyecto sin `corefsm.project.json` se abre igual: Studio lo importa leyendo
`hardware.csv` y `platformio.ini`, y no escribe nada hasta que se guarda.

Los archivos generados se abren en solo lectura: editarlos sería trabajo que la
siguiente generación borraría. Lo que se edita es su tabla.

### Reglas que Studio aplica por ti

- Un rol solo admite sus atributos: `DI` acepta pull-up y antirrebote, `DO`
  acepta activo bajo y estado seguro, `AI` acepta filtro. Las demás celdas
  aparecen apagadas y el tabulador las salta, porque el generador rechaza un CSV
  que las lleve.
- Dos usuarios en el mismo pin es un error, no un aviso: no se escribe nada.
- Una velocidad sobre un pin sin PWM por hardware es un aviso con nombre y pin.
- Al guardar, `platformio.ini` y `corefsm.json` se ponen de acuerdo con la placa
  elegida en lugar de quedar en contradicción.

### Monitor serie

Lee lo que ya emite la librería —las líneas `[PASO]`, `[ESTADO]` y la tabla de
observación de `printWatchTable()`— y las pinta sobre el diagrama de la
secuencia. **No hace falta tocar el firmware**: basta con que el programa use
`StepTracer` o `MaintenanceConsole`, como hacen las plantillas.

Los botones de mando envían las mismas teclas que entiende `MaintenanceConsole`:
`s` marcha, `x` paro, `p` pausa, `r` rearme, `w` tabla, `c` estadísticas de scan.

La velocidad se propone leyendo `monitor_speed` de `platformio.ini`, que es el
origen habitual de los caracteres extraños en el monitor.

`pyserial` es opcional: sin él la aplicación funciona entera salvo el monitor,
que lo explica en su propia vista.

### Límites conocidos

- El coloreado del editor recalcula el archivo entero en cada repintado. Con los
  tamaños normales de un archivo de proyecto (menos de 300 líneas) es
  imperceptible; a partir de unas mil se nota al escribir.
- El editor no tiene buscar/reemplazar ni ir a línea desde la interfaz.
- Las recetas (`RecipeExecutor`) y las alarmas (`AlarmManager`) existen en la
  librería pero todavía no tienen vista propia.

## Generador neutral

`corefsm_gen.py` acepta CSV, JSON explícito o un esquema Wokwi y produce la
misma tabla X-Macro:

```bash
python lib/CoreFSM/tools/corefsm_gen.py --project projects/00_TestLibrary
python lib/CoreFSM/tools/corefsm_gen.py --project projects/00_TestLibrary --check
```

Descubrimiento predeterminado:

1. la fuente indicada por `corefsm.json.source`;
2. `hardware.csv`;
3. `hardware.json`;
4. `diagram.json`.

Opciones:

| Opción | Significado |
|---|---|
| `--project DIR` | raíz del proyecto; por defecto, el directorio actual |
| `-i`, `--input` | fuente explícita |
| `-o`, `--output` | cabecera de salida |
| `--format auto|csv|json|wokwi` | fuerza el adaptador de entrada |
| `--config` | manifiesto alternativo |
| `--node` | selecciona un nodo cuando hay varios |
| `--check` | valida sin escribir |
| `--quiet` | omite el resumen |

Un archivo multplaca ambiguo es un error: hay que seleccionar el nodo. El
generador no vuelve a elegir silenciosamente la primera placa.

## CSV

El encabezado tiene nueve columnas fijas:

```csv
node,name,role,target,pullup,active_low,debounce_ms,filter,safe
main,Marcha,DI,gpio.2,true,,20,,
main,Valvula,DO,EXP1.8,,false,,,false
main,Presion,AI,A0,,,,3,
```

- `role`: `DI`, `DO` o `AI`.
- `target`: pin nativo (`gpio.2`, `2`, `A0`) o `BACKEND.canal`.
- `pullup` y `debounce_ms`: solo para `DI`.
- `active_low` y `safe`: solo para `DO`.
- `filter`: solo para `AI`, de 0 a 8.

## JSON

```json
{
  "nodes": [
    {
      "id": "main",
      "board": "nano",
      "signals": [
        {"name": "Marcha", "role": "DI", "target": "gpio.2",
         "pullup": true, "debounce_ms": 20},
        {"name": "Valvula", "role": "DO", "target": "EXP1.8",
         "active_low": false, "safe": false}
      ]
    }
  ],
  "backends": [
    {"node": "main", "name": "EXP1", "driver": "MCP23017",
     "bus": "Wire", "address": "0x20"}
  ]
}
```

## Manifiesto `corefsm.json`

El manifiesto separa la fuente de los valores predeterminados y de la
infraestructura:

```json
{
  "source": {"path": "hardware.csv", "format": "csv", "node": "main"},
  "nodes": [{"id": "main", "board": "nano"}],
  "defaults": {
    "pullup": true,
    "debounce_ms": 20,
    "active_low": false,
    "safe": false
  },
  "backends": [
    {"node": "main", "name": "EXP1", "driver": "MCP23017",
     "bus": "Wire", "address": 32}
  ]
}
```

El backend generado actualmente es MCP23017: direcciones `0x20`..`0x27` y
canales 0..15. Las entradas analógicas deben ser GPIO nativos.

## Wokwi opcional

El adaptador clasifica señales por prefijo (`DI_`, `DO_`, `AI_`) y, como
respaldo, por tipo de componente. La configuración histórica `pins` e `ignore`
continúa disponible en `corefsm.json`.

`wokwi2corefsm.py` se conserva como wrapper compatible:

```bash
python lib/CoreFSM/tools/wokwi2corefsm.py \
  -i diagram.json -o include/HardwareConfig.h
```

Los proyectos antiguos pueden mantener este hook de PlatformIO; internamente
delega en el generador neutral y también descubre CSV o JSON cuando se ejecuta
como hook.

## PlatformIO y archivos versionados

En la ubicación habitual `projects/<nombre>`, el hook generado queda así:

```ini
extra_scripts = pre:../../lib/CoreFSM/tools/corefsm_gen.py
```

Si se usa `--dir`, `nuevo_proyecto.py` escribe automáticamente la ruta correcta
en `extra_scripts`, `lib_extra_dirs` y el enlace de la guía.

Conviene versionar la fuente, `corefsm.json` y `HardwareConfig.h`. Así se puede
compilar sin ejecutar Python y el CI detecta si la cabecera quedó desfasada. El
generador no reescribe el archivo cuando el contenido no cambia.

## Validaciones

Se rechazan nombres C++ inválidos o reservados, señales y destinos duplicados,
campos incompatibles con el rol, backends inexistentes, canales fuera de rango,
direcciones I2C repetidas y selección ambigua de nodo.

La referencia completa está en
[`docs/hardware/sources.md`](../docs/hardware/sources.md).
