<!-- GUÍA DE SUSTENTACIÓN - Pong (Internet: Arquitectura y Protocolos)
     [JUAN] Documento creado por JUAN como apoyo para la sustentación.
     Los diagramas están en texto para poder dibujarlos en el tablero. -->

# 🎓 Guía de Sustentación — Pong (1 página)

## 0. El pitch de 30 segundos

> "Implementamos Pong bajo arquitectura **cliente/servidor**. El **servidor en C**
> usa **Sockets Berkeley** sobre **TCP** y es **autoritativo**: mantiene el estado
> del juego y lo difunde. Diseñamos un **protocolo binario propio**
> (*MyAppGameProtocol*) con un **header de 3 bytes**. El servidor es **concurrente**
> con **un hilo por partida** y registra todo en un **logger**. El cliente puede
> estar en cualquier lenguaje; nosotros usamos Python."

---

## 1. Datos duros que NO puedes olvidar

| Cosa | Valor |
|---|---|
| Tipo de socket | `SOCK_STREAM` (TCP) |
| Header de cada mensaje | **3 bytes**: `uint8 type` + `uint16 length` (big-endian) |
| Opcodes | `0x01` reg req · `0x02` reg resp · `0x03` match start · `0x04` player move · `0x05` game state · `0x06` game over · `0x07` disconnect |
| Tamaños payload | 64 · 2 · 34 · 1 · 16 · 3 · 0 bytes |
| Campos de texto | `char[32]` (nickname / email / opponent) |
| Ejecución del servidor | `./server <PORT> <LogFile>` |
| Rango de puerto válido | 1024 – 65535 |
| Gana quien llegue a | **5 puntos** |
| Campo lógico | 1000 × 600 (ball/paddle en el **centro**) |
| Tick de simulación | ~60 fps (16 ms) |

---

## 2. DIAGRAMAS (los que suelen pedir)

### ⭐ 2.0 Diagrama de CLASES UML (si te lo piden)

En C no hay clases: se **mapean** los `struct`/`enum` a clases y cada `.c` a un
**módulo** con sus operaciones. El diagrama completo está en
[`docs/diagrama-clases.md`](diagrama-clases.md) (Mermaid + ASCII + PNG).

Versión mínima para dibujar:

```
 +----------------------+          +-----------------------+
 |   «module» Protocol  |          |  «struct» match_ctx   |
 +----------------------+          +-----------------------+
 | + send_all()         |          | - match_id : uint8    |
 | + recv_all()         |          | - p1 : player_info_t  |──┐
 | + send_msg()         |          | - p2 : player_info_t  |──┘ 2
 | + recv_msg()         |          +-----------------------+
 +----------+-----------+                     ^
            | empaqueta                       | usa
            v                                 |
 +----------------------+          +----------+------------+
 |  «struct» msg_*_t    |          | «module» PongServer   |
 |  (header + payloads) |<---------+-----------------------+
 +----------------------+  envia   | + init_server_socket()|
                                   | + run_server_loop()   |
                                   | + match_worker()      |
                                   | + input_reader_thr()  |
                                   | + broadcast_state()   |
                                   +----------+------------+
                                              | registra
                                              v
                                   +-----------------------+
                                   |  «module» Logger      |
                                   +-----------------------+
                                   | + logger_init()       |
                                   | + logger_log()        |
                                   | + logger_close()      |
                                   +-----------------------+
```

**Relaciones clave:** `match_context_t` **compone** 2 `player_info_t`;
`PongServer` **depende** de `Protocol`, `Logger` y `match_context_t`;
`Protocol` **depende** de todos los `msg_*_t`.

### ⭐ 2.1 Jerarquía de Protocolos (el más probable)

```
        +-------------------------------------+
        |        PongClient / PongServer      |  <- APLICACIÓN
        +-------------------------------------+
        |         MyAppGameProtocol           |  <- NUESTRO protocolo (capa aplicación)
        +-------------------------------------+
        |                 TCP                 |  <- TRANSPORTE (fiable, orientado a conexión)
        +-------------------------------------+
        |                 IP                  |  <- RED
        +-------------------------------------+
        |        Ethernet / Wi-Fi             |  <- ENLACE
        +-------------------------------------+
                    ^
                    |  API de Sockets Berkeley (interfaz hacia TCP/IP)
                    |  socket() / bind() / listen() / accept() / connect()
                    |  send() / recv() / close()
```

### 2.2 Arquitectura Cliente / Servidor

```
   [ Cliente 1 ] ─┐
   [ Cliente 2 ] ─┼── TCP ──▶ ┌───────────────┐
   [ Cliente N ] ─┘           │   PongServer  │
                              │  (autoritativo)│
                              └───────┬───────┘
                                      │
                     ┌────────────────┼────────────────┐
                     ▼                ▼                ▼
               Lobby/emparejar   Hilo por partida   Logger (archivo)
```

### 2.3 Secuencia de mensajes de una partida

```
   CLIENTE                                   SERVIDOR
      │                                          │
      │──── MSG_REGISTER_REQ  (0x01) ──────────▶ │
      │◀─── MSG_REGISTER_RESP (0x02) ───────────  │   status + player_id
      │                                          │   (Lobby: espera al 2º)
      │◀─── MSG_MATCH_START   (0x03) ───────────  │   rol + match_id + rival
      │                                          │
      │        ┌──── cada frame (~60 fps) ────┐  │
      │──── MSG_PLAYER_MOVE  (0x04) ─────────▶ │  │   -1 / 0 / +1
      │◀─── MSG_GAME_STATE   (0x05) ──────────  │  │   pelota + palas + marcador
      │        └──────────────────────────────┘  │
      │                                          │
      │◀─── MSG_GAME_OVER    (0x06) ───────────  │   ganador + marcador final
      │                                          │
      │──── MSG_DISCONNECT   (0x07) ──────────▶ │
```

### 2.4 Formato del header (dibújalo así)

```
   Byte:    0                1                2
         +--------+-------------------------------+
         |  Type  |        Payload Length          |
         | 1 byte |  2 bytes  (Network Byte Order) |
         +--------+-------------------------------+  ──▶ luego el payload
```

### 2.5 Máquina de estados del Hilo de Partida (servidor)

```
   [INICIO partida]
         │
         ▼
   Enviar MSG_MATCH_START a ambos
         │
         ▼
   ┌──────────── BUCLE (cada 16 ms) ────────────┐
   │  recibir MSG_PLAYER_MOVE (hilos lectores)  │
   │  mover palas · física pelota · marcador     │
   │  difundir MSG_GAME_STATE a ambos            │
   └────────────────┬───────────────────────────┘
                    │ ¿alguien llegó a 5?
                    ▼
            Enviar MSG_GAME_OVER  ──▶  cerrar sockets · liberar hilo
```

---

## 3. Preguntas frecuentes y respuesta corta

| Pregunta | Respuesta (corta) |
|---|---|
| ¿Por qué TCP y no UDP? | Protocolo binario y ordenado; datos mínimos → el overhead de TCP es despreciable. Está justificado en `docs/socket-tcp-justification.txt`. |
| ¿Por qué binario? | Más compacto y eficiente que texto; structs `packed` sin relleno. |
| ¿Por qué el header tiene longitud? | **TCP es un flujo de bytes sin límites de mensaje**; la longitud permite delimitar cada paquete. |
| ¿Cómo lees exactamente un mensaje? | Primero 3 bytes (header), luego `length` bytes (`recv_all`). |
| ¿Endianness? | Header en **network byte order** (`htons`/`ntohs`); payload entero nativo (LE). |
| ¿Cómo soporta varios clientes? | **Un hilo por partida** (`pthread_create`). |
| ¿Un `recv` bloquea el juego? | No: hay **un hilo lector por jugador** que solo guarda la dirección. |
| ¿Quién decide el estado? | El **servidor** (autoritativo); el cliente solo envía intención. |
| ¿Si un cliente se cae? | El hilo lector lo detecta y la partida termina. |
| ¿Dónde están los eventos? | `logger.c` escribe en consola y en `LogFile` (thread-safe con mutex). |

---

## 4. Comandos

```bash
make                       # compila (gcc) -> ./server  ./client
./server 8080 pong.log     # ejecuta el servidor
./client <IP> <PORT> <NICK> <EMAIL>
```

---

## 5. Repaso express (orden)

1. `docs/opcode-specification.md` → **qué** se hablan
2. `include/protocol.h` + `src/protocol.c` → **cómo** se codifica
3. `src/server/main.c` → `src/server/server.c` → **flujo** del servidor
4. `docs/socket-tcp-justification.txt` → **por qué TCP**
5. `docs/*.png` → UML ya incluido en el repo
