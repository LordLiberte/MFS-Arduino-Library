# ConfigStore.h

> Persistencia binaria de una estructura POD, con magic, versión, tamaño y
> CRC-16/CCITT.

**Ruta:** `src/data/ConfigStore.h`  
**Tipos:** `CfsmStoreHeader`, `CfsmStoreResult`, `ConfigStore<T,VERSION,ADDRESS>`.

## 1. Formato almacenado

```text
ADDRESS
  ├─ magic    uint16_t = 0xC0FB
  ├─ version  uint16_t
  ├─ size     uint16_t
  ├─ crc      uint16_t, calculado sobre T
  └─ payload  sizeof(T) bytes
```

`load()` solo copia el payload a `data` después de validar los cuatro campos.
Si falla, conserva los valores que el programa hubiera puesto en `data`.

## 2. Tipo admisible

`T` debe ser una estructura de representación estable: enteros, booleanos,
arrays fijos y otras estructuras simples. No guardes punteros, referencias,
`String`, contenedores dinámicos ni objetos cuyo significado dependa de un
constructor.

```cpp
struct Ajustes {
  uint16_t tiempoMs;
  uint8_t velocidad;
};

ConfigStore<Ajustes, 2, 0> config;

void setup() {
  config.data = {1500, 180}; // valores de fábrica
  config.begin();
  CfsmStoreResult r = config.load();
  (void)r;
}
```

Sube `VERSION` al cambiar la interpretación de `T`, incluso si `sizeof(T)` no
cambia.

## 3. Plataformas

La disponibilidad se decide en
[`CoreFSM_Platform.h`](../core/CoreFSM_Platform.md):

- AVR usa EEPROM real;
- ESP32, ESP8266 y RP2040 se tratan como EEPROM emulada con `begin/commit`;
- plataformas sin `CFSM_HAS_NVM` devuelven `CFSM_STORE_NO_NVM` y no guardan.

`begin(nvmSize)` debe llamarse antes de cargar o guardar en plataformas con
EEPROM emulada.

## 4. API

| Miembro | Resultado |
|---|---|
| `data` | Estructura activa en RAM |
| `begin(nvmSize)` | Inicializa la emulación cuando aplica |
| `load()` | Valida y carga; devuelve `CfsmStoreResult` |
| `save()` | Guarda cabecera y payload; evita bytes idénticos |
| `erase()` | Invalida el magic |
| `lastResult()` / `isValid()` | Estado de la última carga/guardado |
| `resultText()` | Diagnóstico en flash |
| `footprint()` | `sizeof(header)+sizeof(T)` |
| `nextAddress()` | Primera dirección posterior al bloque |

Resultados posibles: `OK`, `EMPTY`, `BAD_VERSION`, `BAD_SIZE`, `BAD_CRC` y
`NO_NVM`.

## 5. Distribución de direcciones

```cpp
using StoreA = ConfigStore<Ajustes, 2, 0>;
using StoreB = ConfigStore<Calibracion, 1, StoreA::nextAddress()>;
```

Comprueba también que el final del último bloque cabe en `EEPROM.length()`. En
las versiones que no validen límites dentro de `ConfigStore`, una dirección
incorrecta puede sobrescribir otro bloque o envolver el espacio de EEPROM.

## 6. Escrituras y desgaste

`save()` compara byte a byte y solo llama `EEPROM.write()` cuando hay cambios.
Eso reduce desgaste, pero no convierte el guardado por scan en una práctica
válida. Guarda ante una acción deliberada o usa [`DataBlock`](DataBlock.md) con
un intervalo suficientemente largo.

## 7. Límites actuales

- El formato copia padding y endian de la arquitectura; no es un formato de
  intercambio portable entre micros distintos.
- CRC detecta corrupción, no autentica datos.
- Revisa si tu versión comprueba el retorno de `EEPROM.begin/commit`; las
  implementaciones históricas no propagaban esos fallos.
- `erase()` no borra físicamente todo el payload, solo impide aceptarlo.
- Cambiar compilador, packing o tipos puede exigir subir `VERSION`.

## 8. Relación

`DataBlock` lo envuelve para detectar cambios. `RecipeBank` reutiliza la misma
cabecera y CRC para ranuras de receta, pero implementa su propio acceso.

