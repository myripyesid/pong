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
| `0x05` | `MSG_GAME_STATE` | S -> C | 18 bytes | Coordenadas autoritativas y marcador. |
| `0x06` | `MSG_GAME_OVER` | S -> C | 3 bytes | Fin de juego y proclamación de ganador. |
| `0x07` | `MSG_DISCONNECT` | C <-> S | 0 bytes | Solicitud de desconexión. |

---

## 3. Formato Detallado de *Payloads*

* `MSG_REGISTER_REQ`: `char nickname[8]`, `char email[8]`.
* `MSG_REGISTER_RESP`: `uint8_t status`, `uint8_t player_id`.
* `MSG_MATCH_START`: `uint8_t player_role`, `uint8_t match_id`, `char opponent_nickname[8]`.
* `MSG_PLAYER_MOVE`: `int8_t direction`.
* `MSG_GAME_STATE`: `int16_t ball_x`, `int16_t ball_y`, `int16_t paddle1_y`, `int16_t paddle2_y`, `uint8_t score_p1`, `uint8_t score_p2`, `uint32_t frame_id`, `uint16_t reserved`.
* `MSG_GAME_OVER`: `uint8_t winner_role`, `uint8_t final_score_p1`, `uint8_t final_score_p2`.