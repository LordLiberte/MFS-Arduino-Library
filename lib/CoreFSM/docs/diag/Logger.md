# Logger.h

> Trazas con niveles eliminables en compilación y salida configurable mediante la interfaz Arduino `Print`.

**Ruta:** [`src/diag/Logger.h`](../../src/diag/Logger.h)  
**Incluye:** [`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md)  
**API pública:** `CfsmLogger`, `CfsmLogLevel` y macros `CFSM_ERROR`,
`CFSM_WARN`, `CFSM_INFO`, `CFSM_DEBUG` con sus variantes `_V`.

---

## 1. Propósito

`Logger.h` centraliza la salida de diagnóstico y permite borrar en compilación
los niveles que no se desean en producción. No crea ni configura un puerto
serie: trabaja con cualquier objeto que implemente `Print`, como `Serial`, otro
UART o un destino definido por la aplicación.

La motivación es controlar dos costes distintos:

- flash y literales de texto, eliminando macros por nivel;
- tiempo de scan, emitiendo solo eventos puntuales en vez de imprimir por nivel
  en cada vuelta.

El logger no contiene una cola asíncrona. Las llamadas terminan invocando
directamente `Print::print()`/`println()` y pueden bloquear si el destino no
acepta datos con suficiente rapidez.

## 2. Modelo y semántica

### 2.1 Nivel de compilación

`CFSM_LOG_LEVEL` debe definirse **antes de la primera inclusión** de `Logger.h` o
de `CoreFSM.h`:

| Valor | Símbolo | Macros conservadas |
|---:|---|---|
| `0` | `CFSM_LOG_NONE` | ninguna |
| `1` | `CFSM_LOG_ERROR` | errores |
| `2` | `CFSM_LOG_WARN` | errores y avisos |
| `3` | `CFSM_LOG_INFO` | errores, avisos e información |
| `4` | `CFSM_LOG_DEBUG` | todos los niveles |

Si no se define, vale `3`. Las macros que quedan por encima del nivel se
expanden a `do {} while (0)`: no imprimen y tampoco evalúan sus argumentos. Así
se pueden eliminar tanto el trabajo como los literales que solo aparecen dentro
de esas llamadas.

El nivel es único para toda la unidad de compilación. Cambiar la macro después
de que el include guard haya procesado el archivo no tiene efecto.

### 2.2 Destino global

`CfsmLogger` mantiene un único puntero estático a `Print` y una opción global de
timestamp. Antes de `begin()`, o después de `end()`, las macros habilitadas no
hacen nada. `begin()` sustituye cualquier destino configurado anteriormente.

El objeto entregado debe seguir vivo mientras esté registrado. La clase no toma
propiedad de él, no llama a `Serial.begin()`, no vacía buffers en `end()` y no
sincroniza accesos entre tareas o interrupciones.

### 2.3 Formato

Con timestamps activados, una línea tiene este aspecto:

```text
[12.034] I: paso=20
```

El tiempo es `cfsm_millis()` expresado como segundos y tres dígitos de
milisegundos. Sin timestamp, comienza directamente por `I: `. Al desbordar el
reloj de 32 bits, la marca vuelve a cero.

Las variantes simples imprimen el mensaje y un salto de línea. Las variantes
`_V` imprimen primero el mensaje, después el valor y finalmente el salto; **no
añaden `=`, espacio ni separador**, de modo que ese carácter debe formar parte
del mensaje.

## 3. API

### 3.1 `CfsmLogger`

| Método estático | Efecto |
|---|---|
| `begin(Print& out, bool timestamps = true)` | Registra el destino y el formato de prefijo |
| `end()` | Desregistra el destino; no hace `flush()` |
| `ready()` | Devuelve si existe un destino registrado |
| `out()` | Devuelve el `Print*` actual o `nullptr` |
| `prefix(char level)` | Escribe timestamp opcional y `<level>: `; no escribe salto |

`prefix()` y `out()` permiten construir una línea especial, pero no aplican por
sí mismos el filtro de nivel. La aplicación debe comprobar `ready()` y evitar
intercalar varias escrituras concurrentes.

### 3.2 Macros de usuario

```cpp
CFSM_ERROR(message)
CFSM_ERROR_V(message, value)
CFSM_WARN(message)
CFSM_WARN_V(message, value)
CFSM_INFO(message)
CFSM_INFO_V(message, value)
CFSM_DEBUG(message)
CFSM_DEBUG_V(message, value)
```

`message` y `value` deben ser imprimibles por la sobrecarga correspondiente de
Arduino `Print`. Para ahorrar RAM en AVR, usa `F("texto")` o
`CFSM_FSTR("texto")`.

También existen `CFSM_LOG_LINE(lvl, ch, msg)` y
`CFSM_LOG_KV(lvl, ch, msg, val)` como primitivas de las macros anteriores. Al
usarlas directamente, su código y mensaje siguen compilados aunque `lvl` se
decida dinámicamente; para la eliminación completa conviene usar las macros por
nivel.

## 4. Ejemplo mínimo

```cpp
#define CFSM_LOG_LEVEL 3       // antes de CoreFSM.h
#include <CoreFSM.h>

void setup() {
  Serial.begin(115200);
  CfsmLogger::begin(Serial, true);

  CFSM_INFO(CFSM_FSTR("Control iniciado"));
}

void onStepEntered(uint16_t step) {
  CFSM_INFO_V(CFSM_FSTR("paso="), step);
}

void onFault(uint16_t code) {
  CFSM_ERROR_V(CFSM_FSTR("fallo="), code);
}
```

La variante `_V` usa el formato normal de `Print::println(value)` y, por tanto,
imprime el código del ejemplo en decimal. Para hexadecimal hay que construir la
línea con `prefix()` y `Print::print(code, HEX)`.

## 5. Coste

- Estado global: un puntero a `Print` y un `bool`, más el relleno que decida el
  ABI. No hay instancias ni memoria dinámica.
- Una macro deshabilitada no ejecuta nada. Una habilitada realiza varias
  llamadas virtuales a `Print` y, con timestamp, una lectura del reloj y varias
  conversiones numéricas.
- El tiempo dominante no es el prefijo, sino el transporte. A 115200 bit/s un
  carácter serie requiere aproximadamente 87 microsegundos en el enlace; si el
  buffer de transmisión se llena, `print()` puede esperar.
- Las definiciones estáticas usan `__attribute__((weak))` para que la cabecera se
  pueda incluir desde varias unidades de compilación. Es una extensión de
  toolchains compatibles con GNU, no C++ estándar puro.

## 6. Errores frecuentes y limitaciones

- **Definir el nivel demasiado tarde.** Debe preceder a cualquier inclusión que
  llegue a `Logger.h`.
- **Olvidar `CfsmLogger::begin()`.** El silencio antes de configurarlo es el
  comportamiento esperado.
- **Confundir logger con salida no bloqueante.** No hay búfer propio ni tarea de
  vaciado. Evita imprimir en cada scan o dentro de bucles rápidos.
- **Imprimir desde una ISR.** `Print`, el estado estático y el transporte no se
  protegen para interrupciones; además, una espera dentro de una ISR puede
  bloquear el sistema.
- **Usar un destino temporal.** El logger conserva una referencia indirecta; un
  `Print` destruido deja un puntero inválido.
- **Esperar formato hexadecimal en `_V`.** La macro solo llama a `println(val)`.
- **Esperar exclusión mutua.** Dos tareas o módulos que impriman al mismo tiempo
  pueden intercalar fragmentos de líneas.
- **Usarlo como registro durable.** La salida puede perderse al reiniciar y no
  sustituye un registro persistente de alarmas.

## 7. Relación con otros módulos

- [`Telemetry`](Telemetry.md) ofrece trazado de pasos, CSV y consola. Esas clases
  escriben directamente en su `Print`/`Stream`; no respetan `CFSM_LOG_LEVEL` ni
  necesitan que `CfsmLogger` esté iniciado.
- [`ScanWatchdog`](ScanWatchdog.md) permite detectar que la instrumentación está
  alargando el scan. No conviene emitir trazas largas desde su callback.
- [`SequenceBlock`](../core/SequenceBlock.md) proporciona hooks como
  `onStepEntered()` y `onStepWarning()`, lugares adecuados para registrar un
  evento una sola vez.
- [`CoreFSM_Platform`](../core/CoreFSM_Platform.md) aporta el reloj y
  `CFSM_FSTR`, que conserva literales en memoria de programa en AVR.
