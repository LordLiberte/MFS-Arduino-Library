# Sistemas con varios microcontroladores

> Esta versión incluye un transporte serie de paquetes y una imagen digital
> remota. Son componentes disponibles, no una red completa que se configure
> sola ni un bus de seguridad.

## Componentes incluidos

| Cabecera | Capacidad |
|---|---|
| [`PacketLink.h`](../comms/PacketLink.md) | COBS, CRC-16, identidad, sesión, secuencia y buffers sobre `Stream` |
| [`RemoteIO.h`](../comms/RemoteIO.md) | distribución a endpoints e imagen digital con timeout |
| [`Handshake.h`](../core/Handshake.md) | handshake local en RAM; no se serializa automáticamente |

`CoreFSM.h` incluye las dos cabeceras de comunicación. La aplicación aporta el
`Stream`, topología física, velocidad, identidad única y política ante fallos.

## Ejemplo mínimo de dos nodos

En el nodo 1:

```cpp
CfsmPacketTransport transporte(Serial1, 1);
CfsmNetworkManager<1> red(transporte);
RemoteDigitalBackend<1> imagenNodo2(2, 0, 500, 100);

void setup() {
  Serial1.begin(115200);
  red.attach(&imagenNodo2);
  red.begin();
  imagenNodo2.setSafeInput(0, false);
}
```

El nodo 2 usa la configuración simétrica, intercambiando ids `1` y `2`. Los
bytes TX de cada placa se convierten en los bytes RX de la otra. El firmware de
cada nodo sigue siendo responsable de copiar órdenes recibidas a sus salidas
locales y de publicar sensores locales.

## Orden de scan

```text
1. red.readInputs(rxBudget)     recibe y valida con trabajo acotado
2. HW.readInputs()              captura E/S local
3. manager.updateAll()          lógica sobre ambas imágenes
4. HW.writeOutputs()            aplica salidas locales
5. red.writeOutputs(txBudget)   publica la imagen propia
```

No existe una foto global simultánea: el dato remoto siempre tiene una edad.
Usa `linkOk()` y `age()` como parte del estado de la máquina.

## Qué detecta la implementación

- corrupción mediante CRC y formato/longitud inválidos;
- pérdida de sincronía mediante delimitación COBS;
- buffer RX o TX lleno mediante contadores;
- duplicados y orden antiguo dentro de una misma sesión;
- cambio de sesión, interpretable como reinicio del peer;
- ausencia de snapshots mediante timeout y valores seguros de entrada.

La secuencia de 8 bits permite pérdida y vuelta modular; una distancia superior
a 127 se considera antigua. La sesión evita confundir un contador nuevo tras
reinicio con el anterior.

## Lo que no proporciona

- configuración de UART, USB, RS-485, CAN, BLE o Wi-Fi;
- control de dirección RS-485 o arbitraje multimaestro;
- ACK, reintento, entrega exacta de eventos o cola persistente;
- descubrimiento, routing, elección de líder o reloj distribuido;
- enlace automático de `Handshake`, alarmas, recetas o palabras de control;
- cifrado, autenticación o protección frente a nodos hostiles.

Para eventos que no pueden perderse, construye un protocolo de aplicación con
identificador y acuse sobre `CfsmPacketTransport`. No envíes bitfields ni
estructuras C++ en crudo: padding, tamaño de `bool` y endian pueden cambiar.

## Fallo seguro

Configura cada bit RX con `setSafeInput()` antes de operar. El timeout solo
cambia lo que devuelve la imagen remota; no garantiza que una salida física del
otro nodo se desenergice. Ese nodo debe tener vigilancia local y actuar aunque
el emisor desaparezca.

No envíes pulsos únicos esperando que siempre lleguen: publica estados
idempotentes o añade confirmación. Tras `consumePeerRestarted()`, invalida
transferencias pendientes que pertenezcan a la sesión anterior.

Una red ordinaria no sustituye un circuito de seguridad certificado. CRC,
heartbeat y fallback son diagnósticos y tolerancia a fallos de control general.
Consulta [SAFETY.md](../../../../SAFETY.md).

## Organización recomendada

Mantén un proyecto compilable por firmware y una especificación compartida del
protocolo:

```text
projects/sistema_distribuido/
├── controlador/
├── nodo_remoto/
└── protocolo.md
```

Cada nodo debe tener id, tabla de hardware y pruebas propias. El generador puede
seleccionar nodos distintos de una fuente neutral mediante `--node`; no crea por
sí solo la lógica de comunicación ni los dos proyectos.

