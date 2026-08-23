# MFS-Arduino-Library

![Compilar](https://github.com/CarlosGR01/MFS-Arduino-Library/actions/workflows/compilar.yml/badge.svg)

Repositorio de automatización sobre Arduino. Contiene **CoreFSM**, una librería
que traslada el modelo de programación de un autómata industrial —ciclo de scan,
imagen de proceso, bloques funcionales, secuencias por pasos, palabras de mando
y estado, recetas y alarmas— a C++ sobre microcontrolador. Y contiene los
proyectos que la usan.

---

## Estructura

```
MFS-Arduino-Library/
│
├── lib/
│   └── CoreFSM/              LA LIBRERÍA. Una sola copia, compartida.
│       ├── src/              el código
│       ├── examples/         7 ejemplos, de lo básico al robot
│       ├── tools/            los generadores (ver más abajo)
│       └── README.md         la guía completa
│
├── projects/                 TUS MÁQUINAS. Una carpeta por proyecto.
│   └── 00_TestLibrary/       proyecto de prueba, funciona tal cual
│       ├── platformio.ini    configuración: placa, rutas a la librería
│       ├── diagram.json      el esquema de Wokwi (la fuente de verdad)
│       ├── corefsm.json      ajustes finos del generador
│       ├── wokwi.toml        para simular dentro de VS Code
│       ├── include/
│       │   └── HardwareConfig.h    GENERADO, no lo edites
│       └── src/
│           ├── main.cpp      el ciclo de scan
│           └── Proceso.h     la lógica. Aquí va tu trabajo.
│
├── .github/workflows/
│   └── compilar.yml          CI: compila todo en cada push
│
├── .vscode/extensions.json   extensiones recomendadas
├── .gitignore
└── LICENSE
```

**La regla de oro:** una copia de la librería, muchos proyectos. Arreglas un
fallo en `lib/CoreFSM/` y queda arreglado para todos a la vez. La contrapartida
es la misma moneda: si lo rompes, lo rompes para todos. Por eso está el CI.

---

## Empezar

Necesitas **VS Code** con la extensión **PlatformIO IDE**. Nada más: PlatformIO
se descarga el compilador solo la primera vez.

1. Abre en VS Code la carpeta **`projects/00_TestLibrary`** (no la raíz del
   repositorio: PlatformIO busca el `platformio.ini` en la raíz de lo que abras).
2. `Ctrl+Alt+B` para compilar. La primera vez tarda unos minutos.
3. Debe salir `SUCCESS`, con `RAM: 24.1%` y `Flash: 30.9%`.

Ese proyecto no necesita ningún componente: usa el LED que ya lleva la placa.
Si compila y parpadea, todo está en su sitio.

| Atajo | Acción |
|---|---|
| `Ctrl+Alt+B` | Compilar |
| `Ctrl+Alt+U` | Compilar y cargar a la placa |
| `Ctrl+Alt+S` | Monitor serie |

---

## Crear un proyecto nuevo

Desde la terminal, **en la raíz del repositorio**:

```bash
python lib/CoreFSM/tools/nuevo_proyecto.py 02_cinta
python lib/CoreFSM/tools/nuevo_proyecto.py 03_brazo --placa esp32
```

Placas: `nano` (por defecto), `uno`, `mega`, `esp32`.

Crea la carpeta con todo dentro y las rutas ya correctas, y deja el proyecto
compilando desde el primer momento. No copies proyectos a mano: las dos rutas
relativas del `platformio.ini` son justo lo que se olvida cambiar, y el error
que produce no se parece en nada a la causa.

---

## El esquema manda

```
   diagram.json  (lo dibujas en wokwi.com)
        │
        │  se ejecuta solo al compilar
        ▼
   include/HardwareConfig.h
        │
        ▼
   HW.Pulsador_Marcha.hasRisen()
```

Ponle nombre a cada componente en Wokwi: **el `id` del componente se convierte
en el nombre de la variable**. Mueve un cable del pin 2 al 8 y recompila: el
software lee el pin 8 sin que toques una línea.

Prefijos para forzar el tipo de señal cuando el componente no lo deja claro:
`DI_` entrada digital, `DO_` salida digital, `AI_` entrada analógica.

Lo que el esquema no puede expresar —antirrebote de un sensor concreto, un relé
activo a nivel bajo, un pin a ignorar— va en `corefsm.json`.

---

## Simular sin placa

Instala la extensión **Wokwi for VS Code**. Compila, y luego `Ctrl+Mayús+P` →
*Wokwi: Start Simulator*. Se abre el circuito del `diagram.json` con sus botones
y LEDs, y el monitor serie funciona igual que con la placa real.

---

## Integración continua

Cada push a cualquier rama dispara la compilación en GitHub. No hay que
registrar nada: el CI busca `projects/*/platformio.ini` y compila lo que
encuentre, así que los proyectos nuevos entran solos.

Tres trabajos:

| Trabajo | Qué vigila |
|---|---|
| `Buscar proyectos` | localiza los proyectos y comprueba mayúsculas de carpetas |
| `<cada proyecto>` | los compila **en paralelo**, sin pararse en el primer fallo |
| `Ejemplos de la libreria` | los 7 ejemplos: avisa si rompes algo que aún no usas |

En el resumen de cada ejecución sale el consumo de memoria de cada proyecto.

Para que `main` no pueda romperse: **Settings → Branches → Add rule** sobre
`main`, y marca *Require status checks to pass*.

---

## Flujo de trabajo con ramas

- **Carpeta** para *qué*: cada máquina es una carpeta en `projects/`.
- **Rama** para *y si…*: un cambio del que no estás seguro, o que te va a dejar
  el código sin compilar un rato.

```
lib/handshake-v2      toca la librería  -> compila varios proyectos antes de fusionar
proy/robot-vision     toca un proyecto  -> con ese basta
fix/timeout-pausa
```

Cambios pequeños y seguros van directos a `main` sin ceremonia. Montar el ritual
de un equipo de veinte personas trabajando solo es puro peaje.

---

## Aviso sobre mayúsculas

`lib` y `projects` van **en minúsculas**. En Windows da igual; en Linux —donde
corre el CI— `Lib` y `lib` son carpetas distintas, así que un fallo de
mayúsculas te funciona en local y solo se rompe en GitHub.

Git en Windows tampoco detecta un cambio de solo mayúsculas, así que si pasa hay
que renombrar en dos pasos:

```bash
git mv Lib lib_tmp
git mv lib_tmp lib
```

El CI lo comprueba y te lo dice claro si se cuela.

---

## Documentación

- **Guía completa de la librería**: [`lib/CoreFSM/README.md`](lib/CoreFSM/README.md)
- **Herramientas**: [`lib/CoreFSM/tools/README.md`](lib/CoreFSM/tools/README.md)
- **Ejemplos**: [`lib/CoreFSM/examples/`](lib/CoreFSM/examples/) — empieza por el 01

## Licencia

MIT. Ver [LICENSE](LICENSE).
