<!-- [JUAN] Documento revisado y actualizado para reflejar la implementación real
     del protocolo en include/protocol.h y el bucle de juego del servidor. -->

# Especificación Formal de Sintaxis: `MyAppGameProtocol`

---

## 1. Encabezado Común (*Fixed Header*)

Todos los mensajes transmitidos a través de la capa de transporte TCP inician con un encabezado fijo de **3 bytes**:

* `Type` (1 byte, `uint8_t`): *Opcode* identificador del mensaje.
* `Payload Length` (2 bytes, `uint16_t` en *Network Byte Order*): Longitud en bytes de la carga útil posterior.

---

## 2. Tabla de Mensajes y *Opcodes*

| Opcode | Constante | Dirección | Tamaño Payload | Descripción |
| --- | --- | --- | --- | --- |
| `0x01` | `MSG_REGISTER_REQ` | C -> S | 64 bytes | Registro con nickname y email. |
| `0x02` | `MSG_REGISTER_RESP` | S -> C | 2 bytes | Confirmación de registro y espera. |
| `0x03` | `MSG_MATCH_START` | S -> C | 34 bytes | Notificación de partida y rol asignado. |
| `0x04` | `MSG_PLAYER_MOVE` | C -> S | 1 byte | Intención de desplazamiento (-1, 0, 1). |
| `0x05` | `MSG_GAME_STATE` | S -> C | 16 bytes | Coordenadas autoritativas y marcador. |
| `0x06` | `MSG_GAME_OVER` | S -> C | 3 bytes | Fin de juego y proclamación de ganador. |
| `0x07` | `MSG_DISCONNECT` | C <-> S | 0 bytes | Solicitud de desconexión. |

---

## 3. Formato Detallado de *Payloads*

<!-- [JUAN] Corregido: los campos de texto son char[32] (no char[8]) para coincidir
     con MAX_NICKNAME_LEN y MAX_EMAIL_LEN definidos en include/protocol.h. -->

Todos los *payloads* se empaquetan sin relleno (*packed*). Los enteros multi-byte
viajan en *Little-Endian* (formato nativo del compilador C en x86/ARM); únicamente
el campo `length` del encabezado usa *Network Byte Order*.

* `MSG_REGISTER_REQ` (64 B): `char nickname[32]`, `char email[32]`.
* `MSG_REGISTER_RESP` (2 B): `uint8_t status`, `uint8_t player_id`.
* `MSG_MATCH_START` (34 B): `uint8_t player_role`, `uint8_t match_id`, `char opponent_nickname[32]`.
* `MSG_PLAYER_MOVE` (1 B): `int8_t direction` (`-1` = arriba, `0` = quieto, `1` = abajo).
* `MSG_GAME_STATE` (16 B): `int16_t ball_x`, `int16_t ball_y`, `int16_t paddle1_y`, `int16_t paddle2_y`, `uint8_t score_p1`, `uint8_t score_p2`, `uint32_t frame_id`, `uint16_t reserved`.
* `MSG_GAME_OVER` (3 B): `uint8_t winner_role`, `uint8_t final_score_p1`, `uint8_t final_score_p2`.

---

## 4. Semántica del Juego y Bucle de Simulación

<!-- [JUAN] Sección agregada por mí para documentar el comportamiento del game loop
     implementado en src/server/server.c (match_worker). -->

### 4.1 Sistema de coordenadas

El servidor y el cliente comparten un **campo lógico** de `1000 x 600` unidades:

* `ball_x`, `ball_y`: **centro** de la pelota.
* `paddle1_y`, `paddle2_y`: **centro vertical** de cada pala (pala izquierda = P1,
  pala derecha = P2).
* El cliente escala estas coordenadas al tamaño de su ventana.

### 4.2 Bucle autoritativo

1. El servidor simula a **~60 fps** (tick de 16 ms).
2. En cada tick:
   * aplica la última dirección recibida por cada jugador (`MSG_PLAYER_MOVE`),
   * actualiza la física de la pelota (rebotes y colisiones),
   * difunde el `MSG_GAME_STATE` a ambos clientes.
3. Un hilo lector por jugador consume los movimientos de forma asíncrona para no
   bloquear la simulación.

### 4.3 Reglas

* La partida termina cuando un jugador alcanza **5 puntos** (`GAME_WIN_SCORE`).
* Al finalizar, el servidor envía `MSG_GAME_OVER` con el ganador y el marcador final,
  y cierra los sockets asociados a la partida.
* Si un jugador se desconecta durante la partida, el hilo lector lo detecta y la
  simulación finaliza.