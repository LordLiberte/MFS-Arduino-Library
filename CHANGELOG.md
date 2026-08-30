# Historial de cambios

Este proyecto sigue [Semantic Versioning](https://semver.org/).

## 2.3.1 · 2026-08-30

### Añadido

- **Importar la configuración de pines desde otra herramienta.** Botón
  «Importar» en la cinta y en la tabla de variables: acepta el `diagram.json`
  de Wokwi, un `hardware.csv` y un `hardware.json`, arrastrando el archivo o
  pegando su contenido. Muestra qué ha encontrado, qué nombres ya existen y los
  avisos del generador antes de tocar nada, y deja elegir entre añadir a la
  tabla o reemplazarla. Wokwi deja de ser un requisito y pasa a ser una entrada
  más: cualquier programa que exporte nombre y pin a CSV vale.

### Corregido

- **Enter ya no crea filas.** Al confirmar la última fila de una tabla se
  añadía otra vacía, que además impedía guardar por no tener nombre. Ahora
  Enter confirma y baja, y las filas solo aparecen al pulsar «Añadir».
- **Escape ya no borra lo escrito.** Descarta la fila solo si acaba de crearse
  con «Añadir» y sigue intacta; si ya hay algo escrito, cancela la celda y nada
  más.
- **Los diálogos aguantan un clic rápido.** Cerrar al pulsar fuera exige ahora
  pulsar y soltar sobre el fondo, y no admite nada durante los primeros 300 ms:
  lo cerraba al instante.
- **Un solo contenedor con scroll por vista.** Había tres encajados y la rueda
  del ratón hacía cosas distintas según dónde estuviera el cursor.
- **Nada se sale de la pantalla.** Las pistas `1fr` de la rejilla principal
  conservaban un mínimo automático y desbordaban la ventana por debajo de
  1366 px de alto, dejando el panel inferior fuera de vista. Las barras de
  herramientas se doblan, las tablas conservan anchos legibles y desplazan en
  horizontal dentro de su marco, y por debajo de 1200 px se oculta el
  inspector.
- **Fluidez.** Los repintados se agrupan por fotograma en vez de uno por
  pulsación; el coloreado del editor y la columna de números se recalculan solo
  cuando hace falta; el árbol y la consola solo se reconstruyen si su contenido
  ha cambiado; el separador del árbol dejaba un manejador por repintado; y el
  monitor deja de sondear el puerto al salir de su vista.
- El contador de filas de una tabla se quedaba con el valor del primer pintado.
- Asignar un pin desde el mapa de la placa no refrescaba el mapa.

## 2.3.0 · 2026-08-30

### Añadido

- **CoreFSM Studio**: entorno de ingeniería local con interfaz gráfica, servido
  por `corefsm_studio.py` desde `tools/studio/static/`. Sin dependencias ni
  conexión: es HTML, CSS y JavaScript escritos a mano sobre la biblioteca
  estándar de Python.
- Vista del portal orientada a tareas y asistente de proyecto nuevo con
  plantillas (máquina básica, Keyestudio 4WD KS0192, 4WD de referencia, vacío).
- Tabla de variables de E/S al estilo de una tabla de variables PLC, con edición
  por teclado, filtro y validación en vivo.
- Tablas de dispositivos, bloques de datos y tipos de usuario; los ajustes de la
  máquina dejan de estar repartidos entre el constructor, `main.cpp` y la lógica.
- Vista de dispositivo con el mapa de pines de la placa, colisiones marcadas y
  aviso cuando una velocidad cae en un pin sin PWM por hardware.
- Editor de código con coloreado de C++, símbolos del proyecto resaltados y
  autocompletado: al escribir un punto tras un objeto aparecen sus métodos
  reales con su firma y para qué sirven.
- Se pueden arrastrar señales de la tabla de E/S al editor o a una celda de
  condición para insertar su expresión.
- Diagrama de la secuencia tipo Grafcet, generado del modelo.
- Monitor serie que lee la telemetría que ya emite la librería (`[PASO]`,
  `[ESTADO]`, tabla de observación) y enciende el paso activo sobre el diagrama,
  con los mandos de `MaintenanceConsole`. No requiere cambiar el firmware.
- Lista de pasos guiados que se marca sola según el estado del proyecto.
- `studio/serialmon.py`: puente con el puerto serie. `pyserial` es opcional y su
  ausencia solo desactiva el monitor.
- Métodos del servo y del bloque de secuencia que faltaban en el catálogo.

### Corregido

- Studio ya no escribe en `hardware.csv` atributos que el rol de la señal no
  admite (`active_low` en una `DI`, por ejemplo), que abortaban la generación.
  Las plantillas que lo hacían quedan corregidas y la tabla apaga esas celdas.

## 2.2.0 · 2026-08-23

### Añadido

- Fuente de hardware neutral mediante `hardware.csv` y `hardware.json`.
- Selección explícita de nodo y validación común para CSV, JSON y Wokwi.
- Configuración manual de `HardwareConfig.h` como flujo de primera clase.
- Abstracción `IDigitalBackend` y pines digitales sobre GPIO o backend.
- Backend MCP23017 con imagen de 16 bits y operaciones I2C agrupadas por scan.
- Estado seguro configurable por salida e interbloqueo global de software.
- Transporte de paquetes COBS/CRC y snapshots digitales entre nodos con timeout.
- Ejemplos de dos placas por UART y expansión MCP23017.
- Referencia técnica organizada por módulos, guías de hardware/red y aviso de
  seguridad.
- Pruebas del generador y banco host para E/S, backends y comunicaciones.

### Cambiado

- `nuevo_proyecto.py` usa CSV por defecto y permite `csv`, `json`, `wokwi` o
  `manual`; `--sin-wokwi` se conserva como alias obsoleto de CSV.
- Wokwi pasa de requisito a adaptador opcional compatible.
- El proyecto de referencia usa CSV como fuente canónica y conserva Wokwi solo
  para simulación.
- `DeviceManager` separa captura de backends, lectura de objetos, escritura de
  objetos y commit agrupado.

### Corregido

- `--sin-wokwi` ya genera un proyecto completo y compilable.
- `--dir` calcula las rutas de librería, hook y documentación desde la ubicación
  real del proyecto, incluso fuera de `projects/`.
- Un diagrama con varias placas ya no selecciona la primera silenciosamente.
- `completeCycle()` ya puede cambiar de paso sin borrar el pulso `done`.
- Correcciones de regresión en ejes, inversión de motor, recetas, alarmas,
  entradas con rebote/forzado, ADC, visión, watchdog y datos persistentes.
- El MCP23017 normaliza `IOCON` tras reinicios parciales y precarga el latch
  antes de habilitar salidas para evitar pulsos al arrancar.
- La integración continua compila también un proyecto ESP32 recién generado.

### Seguridad

- La API histórica `setEmergencyStop()` se documenta correctamente como
  interbloqueo de software, no como parada de emergencia certificada.
- Las plantillas conectan el interbloqueo lógico con el estado seguro de las
  salidas y evitan restaurar órdenes antiguas al liberarlo.

## 2.1.0

- Estados de espera `SUSPENDED` y `HELD`, objetivo de takt, avisos de paso y
  vigilancia del tiempo de scan.

## 2.0.0

- Reorganización modular del núcleo, imagen de proceso, recetas, datos,
  diagnóstico y compatibilidad con la API anterior.
