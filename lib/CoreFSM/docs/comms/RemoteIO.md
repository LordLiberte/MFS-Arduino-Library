# RemoteIO.h

> Endpoints de red y una imagen digital bidireccional construida sobre
> [`CfsmPacketTransport`](PacketLink.md).

**Ruta:** `src/comms/RemoteIO.h`  
**Tipos:** `ICfsmNetworkEndpoint`, `CfsmNetworkManager`,
`RemoteDigitalBackend`.

## Gestor de red

`CfsmNetworkManager<MAX_ENDPOINTS>` conserva punteros en un array estático. Los
endpoints se adjuntan antes de `begin()`; puntero nulo, duplicado, falta de
capacidad o registro tardío devuelven `false`.

```cpp
CfsmPacketTransport transport(Serial1, 1);
CfsmNetworkManager<1> red(transport);
RemoteDigitalBackend<1> remoto(2); // peer 2, canal 0

void setup() {
  Serial1.begin(115200);
  red.attach(&remoto);
  red.begin();
}

void loop() {
  red.readInputs(32);   // antes de la lógica
  // lógica local
  red.writeOutputs(16); // después de producir la imagen TX
}
```

`readInputs()` atiende RX, entrega cada trama al primer endpoint que la acepte y
ejecuta sus `tick()`. `writeOutputs()` solicita snapshots y atiende TX con el
presupuesto indicado.

## `RemoteDigitalBackend<BYTES>`

Cada instancia publica `BYTES` propios y recibe otros `BYTES` del peer. Al
implementar [`IDigitalBackend`](../io/DigitalBackend.md), sus bits pueden
alimentar `DigitalSensor` o recoger órdenes de `DigitalOutput` sin que la FSM
conozca el protocolo.

`BYTES` debe estar entre 1 y el menor de `CFSM_NET_MAX_PAYLOAD` y 32. El límite
de 32 bytes corresponde a los 256 canales que puede direccionar la interfaz de
backend con un índice de 8 bits.

```cpp
RemoteDigitalBackend<2> ioRemota(2, 0, 500, 100);
DigitalSensor confirmacion(ioRemota, 0, false, 0);
DigitalOutput orden(ioRemota, 8, false, false);
DeviceManager<2> imagen;
```

Registra `confirmacion` y `orden` como dispositivos, pero deja que
`CfsmNetworkManager` inicialice `ioRemota` como endpoint. No registres la misma
instancia también como backend del `DeviceManager`: ambos gestores llamarían a
su `begin()`. En este backend `sampleInputs()`/`commitOutputs()` no hacen
transacciones; la recepción, timeout y publicación pertenecen al gestor de red.

Parámetros del constructor:

| Parámetro | Significado |
|---|---|
| `peerNode` | único origen aceptado |
| `channel` | imagen lógica dentro del servicio `0x10` |
| `timeoutMs` | edad máxima; cero desactiva el timeout |
| `snapshotMs` | republicación periódica aunque no cambie la imagen |

Un cambio local marca la imagen TX pendiente. Una trama válida actualiza RX,
sesión, secuencia y edad. Las pérdidas intermedias se toleran; los duplicados y
tramas antiguas se rechazan y cuentan. Un cambio de sesión se acepta como nueva
imagen y deja una indicación consumible de reinicio del peer.

## Estado y diagnóstico

| Método | Uso |
|---|---|
| `input(bit)` / `read(bit)` | bit RX o valor seguro si el enlace no es válido |
| `output(bit,value)` / `write(bit,value)` | actualiza imagen TX |
| `setSafeInput(bit,value)` | fallback local para un bit RX |
| `linkOk()` / `healthy()` | validez actual de la imagen recibida |
| `age()` | tiempo desde la última trama aceptada |
| `forceSnapshot()` | obliga a intentar publicar en la próxima PAA de red |
| `peerSession()` | sesión observada |
| `consumePeerRestarted()` | lee y borra el evento de reinicio |
| `duplicates()` / `outOfOrder()` / `timeouts()` | contadores del endpoint |

Antes de la primera recepción y después del timeout, `read()` devuelve
`safeRx`. La imagen TX no se borra automáticamente al perder al peer: cada nodo
debe aplicar localmente valores seguros a las órdenes que recibe.

El timeout invalida también la referencia de secuencia. La primera trama válida
reconstruye esa referencia aunque el peer repita la misma sesión; un cambio
real de sesión sigue generando el evento `consumePeerRestarted()`.

## Integración con el scan

Un orden coherente es:

```text
red.readInputs()        recibir snapshot remoto
HW.readInputs()         capturar hardware local
manager.updateAll()     ejecutar lógica
HW.writeOutputs()       aplicar hardware local
red.writeOutputs()      publicar snapshot propio
```

La clase transporta imágenes, no acciona mágicamente GPIO en la otra placa. El
firmware remoto debe consumir los bits recibidos y gobernar su hardware local.

## Límites

- Solo incluye una imagen digital; no hay RPC, recetas, alarmas ni enlace
  automático de `Handshake`.
- No hay ACK de aplicación ni entrega fiable de eventos únicos.
- Todos los endpoints de un gestor comparten un `Stream` y una cola RX.
- No implementa autodetección, elección de líder ni sincronización de reloj.
- Timeout, CRC y fallback mejoran el diagnóstico, pero no forman un bus de
  seguridad.

Consulta [sistemas con varios controladores](../net/multi-controller.md) y
[SAFETY.md](../../../../SAFETY.md).
