<!-- DIAGRAMA DE CLASES UML - Proyecto Pong
     [JUAN] Documento creado por JUAN a partir del codigo real:
     include/protocol.h, include/server.h, include/logger.h y src/**.c
     Se incluye en version Mermaid (GitHub lo renderiza) y en ASCII. -->

# Diagrama de Clases UML — Pong

Este documento modela **los tipos (estructuras), las funciones y sus relaciones**
tal como están implementados en C. En UML, los `struct`/`enum` de C se representan
como **clases** (con sus atributos) y las funciones como **operaciones** que
dependen del módulo donde viven.

---

## 1. Diagrama de clases (Mermaid)

```mermaid
classDiagram
    direction LR

    %% ============ CAPA DE PROTOCOLO (protocol.h / protocol.c) ============
    class msg_header_t {
        <<struct packed>>
        +uint8_t type
        +uint16_t length
    }
    class msg_register_req_t {
        <<struct packed>>
        +char nickname[32]
        +char email[32]
    }
    class msg_register_resp_t {
        <<struct packed>>
        +uint8_t status
        +uint8_t player_id
    }
    class msg_match_start_t {
        <<struct packed>>
        +uint8_t player_role
        +uint8_t match_id
        +char opponent_nickname[32]
    }
    class msg_player_move_t {
        <<struct packed>>
        +int8_t direction
    }
    class msg_game_state_t {
        <<struct packed>>
        +int16_t ball_x
        +int16_t ball_y
        +int16_t paddle1_y
        +int16_t paddle2_y
        +uint8_t score_p1
        +uint8_t score_p2
        +uint32_t frame_id
        +uint16_t reserved
    }
    class msg_game_over_t {
        <<struct packed>>
        +uint8_t winner_role
        +uint8_t final_score_p1
        +uint8_t final_score_p2
    }

    class Protocol {
        <<module protocol.c>>
        +send_all(sockfd, buf, len) int
        +recv_all(sockfd, buf, len) int
        +send_msg(sockfd, type, payload, len) int
        +recv_msg(sockfd, header, payload, max) int
    }

    class msg_header_t
    class msg_register_req_t
    class msg_register_resp_t
    class msg_match_start_t
    class msg_player_move_t
    class msg_game_state_t
    class msg_game_over_t

    %% ============ CAPA DE SERVIDOR (server.h / server.c) ============
    class player_info_t {
        <<struct>>
        +int sockfd
        +char nickname[32]
        +char email[32]
        +struct sockaddr_in address
    }
    class match_context_t {
        <<struct>>
        +uint8_t match_id
        +player_info_t p1
        +player_info_t p2
    }
    class player_input_t {
        <<struct>>
        +int sockfd
        +volatile int direction
        +volatile int alive
    }
    class PongServer {
        <<module server.c>>
        +init_server_socket(port) int
        +run_server_loop(server_fd) void
        +close_server_socket(server_fd) void
        +match_worker(arg) void*
        +input_reader_thread(arg) void*
        +broadcast_state(match, st) void
    }

    %% ============ CAPA DE LOGGER (logger.h / logger.c) ============
    class log_level_t {
        <<enum>>
        LOG_INFO
        LOG_WARN
        LOG_ERROR
        LOG_DEBUG
    }
    class Logger {
        <<module logger.c>>
        +logger_init(filename) int
        +logger_log(level, format, ...) void
        +logger_close() void
    }

    %% ============ RELACIONES ============
    match_context_t *-- player_info_t : p1, p2
    PongServer ..> match_context_t : usa
    PongServer ..> player_input_t : crea (hilo lector)
    PongServer ..> Protocol : envia/recibe
    PongServer ..> Logger : registra eventos
    Protocol ..> msg_header_t : empaqueta
    Protocol ..> msg_register_req_t
    Protocol ..> msg_register_resp_t
    Protocol ..> msg_match_start_t
    Protocol ..> msg_player_move_t
    Protocol ..> msg_game_state_t
    Protocol ..> msg_game_over_t
    Logger ..> log_level_t : usa
```

---

## 2. Versión ASCII (para el tablero o el informe)

```
 ┌─────────────────────────────────────────────────────────────────────┐
 │                        CAPA DE PROTOCOLO                            │
 ├─────────────────────────────────────────────────────────────────────┤
 │  «struct packed» msg_header_t          «module» Protocol            │
 │  ─────────────────────────            ─────────────────────────     │
 │  - type   : uint8                     + send_all(sockfd,buf,len)    │
 │  - length : uint16 (Network Byte Order) + recv_all(...)              │
 │                                        + send_msg(sockfd,type,pl,len)│
 │  «struct packed» msg_game_state_t      + recv_msg(sockfd,hdr,pl,max) │
 │  - ball_x, ball_y       : int16              │                      │
 │  - paddle1_y, paddle2_y : int16              │ usa                  │
 │  - score_p1, score_p2   : uint8              ▼                      │
 │  - frame_id             : uint32      ┌──────────────┐              │
 │  - reserved             : uint16      │ msg_header_t │              │
 │                                       └──────────────┘              │
 └─────────────────────────────────────────────────────────────────────┘
                                    ▲
                                    │ envia/recibe
 ┌──────────────────────────────────┴──────────────────────────────────┐
 │                         CAPA DE SERVIDOR                            │
 ├─────────────────────────────────────────────────────────────────────┤
 │  «struct» player_info_t        «struct» match_context_t             │
 │  ───────────────────────       ────────────────────────             │
 │  - sockfd   : int              - match_id : uint8                   │
 │  - nickname : char[32]         - p1 : player_info_t   ←──┐          │
 │  - email    : char[32]         - p2 : player_info_t   ───┘ 2        │
 │  - address  : sockaddr_in                                           │
 │                                                                     │
 │  «module» PongServer                                                │
 │  ──────────────────────────────────────────────────────────         │
 │  + init_server_socket(port) : int                                   │
 │  + run_server_loop(server_fd) : void     (lobby + emparejamiento)   │
 │  + close_server_socket(server_fd) : void                            │
 │  + match_worker(arg) : void*             (bucle de juego)           │
 │  + input_reader_thread(arg) : void*      (recibe MSG_PLAYER_MOVE)   │
 │  + broadcast_state(match, st) : void                                │
 └──────────────────────────────────┬──────────────────────────────────┘
                                    │ registra eventos
 ┌──────────────────────────────────┴──────────────────────────────────┐
 │                          CAPA DE LOGGER                             │
 ├─────────────────────────────────────────────────────────────────────┤
 │  «enum» log_level_t      «module» Logger                            │
 │  ────────────────        ────────────────────────────               │
 │  LOG_INFO                + logger_init(filename) : int              │
 │  LOG_WARN                + logger_log(level, fmt, ...) : void       │
 │  LOG_ERROR               + logger_close() : void                    │
 │  LOG_DEBUG                                                          │
 └─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Relaciones (lectura rápida)

| Relación | Tipo UML | Significado |
| --- | --- | --- |
| `match_context_t` → `player_info_t` | **Composición** (`2`) | Una partida contiene exactamente 2 jugadores |
| `PongServer` → `match_context_t` | **Dependencia** | El servidor usa el contexto de partida |
| `PongServer` → `player_input_t` | **Dependencia** | Cada partida crea un hilo lector por jugador |
| `Protocol` → `msg_*_t` | **Dependencia** | Empaqueta/desempaqueta los mensajes del protocolo |
| `PongServer` → `Protocol` | **Dependencia** | Envía y recibe por los helpers del protocolo |
| `PongServer` → `Logger` | **Dependencia** | Registra todos los eventos |
| `Logger` → `log_level_t` | **Dependencia** | Usa los niveles de log |

---

## 4. Nota sobre C vs. UML

En C no existen clases; el proyecto es **procedural**. Por eso el diagrama aplica la
convención de mapear:

* `struct` / `enum` → **clase** (atributos = campos)
* Cada archivo `.c` → **módulo** con sus **operaciones** (las funciones)
* `static` → miembro **privado**; funciones públicas del `.h` → **públicas**

Esto es el estándar aceptado para documentar software en C con notación UML.
