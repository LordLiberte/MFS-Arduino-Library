# PacketLink.h

> Transporte de tramas pequeñas sobre cualquier `Stream`, con COBS, CRC-16,
> buffers fijos y presupuesto de bytes por scan.

**Ruta:** `src/comms/PacketLink.h`  
**Tipos:** `CfsmNetFrame`, `CfsmNetStats`, `CfsmPacketTransport`.

## Formato

Cada trama contiene versión, origen, destino, servicio, canal, secuencia de
8 bits, sesión de 16 bits en little-endian, longitud, hasta 24 bytes de payload
y CRC-16-CCITT. El bloque se codifica con COBS y termina en `0x00`, lo que
permite resincronizar después de una trama cortada o corrupta.

Los valores por defecto pueden ajustarse antes de incluir la cabecera:

```cpp
#define CFSM_NET_MAX_PAYLOAD 24
#define CFSM_NET_RX_QUEUE 2
#define CFSM_NET_TX_BUFFER 96
#include <comms/PacketLink.h>
```

## Ciclo básico

```cpp
CfsmPacketTransport link(Serial1, 1); // Stream, id local

void setup() {
  Serial1.begin(115200);
  link.begin();
}

void loop() {
  link.serviceRx(32);

  CfsmNetFrame frame;
  while (link.receive(frame)) {
    // consumir sin bloquear
  }

  link.serviceTx(16);
}
```

`begin()` reinicia colas y secuencia, pero no inicializa el `Stream`. Si la
sesión del constructor es cero, genera una a partir de reloj e id local. Esa
mezcla no es una fuente de entropía garantizada: si distinguir reinicios es
crítico para el protocolo, aporta un identificador de arranque persistente o
aleatorio mediante el constructor o `setSession()`.

## API

| Método | Efecto |
|---|---|
| `serviceRx(byteBudget)` | consume como máximo ese número de bytes disponibles |
| `serviceTx(byteBudget)` | intenta escribir como máximo ese número de bytes encolados |
| `receive(frame)` | extrae la siguiente trama destinada al nodo o a broadcast |
| `send(dst,service,channel,data,len)` | codifica y encola; `false` si no cabe o es inválida |
| `localNode()` / `session()` | identidad local |
| `setSession(value)` | fija sesión; cero se convierte en uno |
| `stats()` | contadores acumulados de RX/TX y errores |
| `crc16(data,len)` | cálculo CRC público para pruebas o protocolos superiores |

`send()` no confirma entrega. `txFrames` cuenta tramas aceptadas en el buffer,
no bytes ya transmitidos. La cola RX descarta cuando está llena y el buffer TX
rechaza una trama completa cuando no cabe. Un rechazo TX no consume número de
secuencia; cuando vuelve a haber espacio, el receptor no ve un salto artificial.

## Límites

- No configura UART, dirección de un transceptor RS-485 ni control de acceso al
  medio.
- No hay ACK, reintento, fragmentación, cifrado, autenticación ni negociación.
- El presupuesto acota llamadas, pero una implementación concreta de
  `Stream::write()` aún podría bloquear.
- Secuencia y sesión permiten que una capa superior detecte duplicados o
  reinicios; el transporte no los rechaza por sí solo.
- El payload predeterminado es 24 bytes; si se redefine, el rango admitido por
  esta implementación es 1..242 bytes y el buffer TX debe contener una trama.
- Un CRC detecta corrupción accidental, no un emisor malicioso.

Para la imagen de proceso incluida consulta [`RemoteIO`](RemoteIO.md). Para
topología y seguridad consulta [multi-controller.md](../net/multi-controller.md)
y [SAFETY.md](../../../../SAFETY.md).
