#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* ========================================================================== */
/*                      CONSTANTES Y LÍMITES DEL PROTOCOLO                    */
/* ========================================================================== */
#define MAX_NICKNAME_LEN    32
#define MAX_EMAIL_LEN       32

/* Opcodes (Identificadores de Tipo de Mensaje) */
#define MSG_REGISTER_REQ    0x01
#define MSG_REGISTER_RESP   0x02
#define MSG_MATCH_START     0x03
#define MSG_PLAYER_MOVE     0x04
#define MSG_GAME_STATE      0x05
#define MSG_GAME_OVER       0x06
#define MSG_DISCONNECT      0x07

/* Roles de jugador */
#define ROLE_PLAYER_1       1   /* Pala izquierda */
#define ROLE_PLAYER_2       2   /* Pala derecha   */

/* Direcciones de movimiento */
#define MOVE_UP            -1
#define MOVE_STILL          0
#define MOVE_DOWN           1

/* Estados de respuesta de registro */
#define STATUS_REGISTER_OK  0
#define STATUS_LOBBY_FULL   1

/* ========================================================================== */
/*                    ENCABEZADO COMÚN DE MENSAJE (HEADER)                    */
/* ========================================================================== */
/* 
 * Encabezado fijo de 3 bytes que precede a todo paquete por TCP.
 * __attribute__((packed)) asegura que el compilador no inserte bytes de relleno.
 */
typedef struct __attribute__((packed)) {
    uint8_t  type;       /* Opcode identificador (1 byte) */
    uint16_t length;     /* Tamaño del payload posterior en bytes (2 bytes) */
} msg_header_t;

/* ========================================================================== */
/*                       ESTRUCTURAS DE CARGA ÚTIL (PAYLOADS)                 */
/* ========================================================================== */

/* 0x01: Registro de jugador (64 bytes) */
typedef struct __attribute__((packed)) {
    char nickname[MAX_NICKNAME_LEN];
    char email[MAX_EMAIL_LEN];
} msg_register_req_t;

/* 0x02: Respuesta de registro / Lobby (2 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t status;     /* 0 = OK, 1 = Error */
    uint8_t player_id;  /* Identificador temporal */
} msg_register_resp_t;

/* 0x03: Inicio de partida (34 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t player_role;                     /* 1 = P1, 2 = P2 */
    uint8_t match_id;                        /* ID de la partida */
    char    opponent_nickname[MAX_NICKNAME_LEN];
} msg_match_start_t;

/* 0x04: Entrada de movimiento del cliente (1 byte) */
typedef struct __attribute__((packed)) {
    int8_t direction;   /* -1 = Arriba, 0 = Quieto, 1 = Abajo */
} msg_player_move_t;

/* 0x05: Estado global autoritativo del juego (16 bytes) */
typedef struct __attribute__((packed)) {
    int16_t  ball_x;        /* Coordenada X de la pelota */
    int16_t  ball_y;        /* Coordenada Y de la pelota */
    int16_t  paddle1_y;     /* Altura de raqueta izquierda */
    int16_t  paddle2_y;     /* Altura de raqueta derecha   */
    uint8_t  score_p1;      /* Marcador P1 */
    uint8_t  score_p2;      /* Marcador P2 */
    uint32_t frame_id;      /* Número de secuencia del fotograma */
    uint16_t reserved;      /* Reservado para alineación */
} msg_game_state_t;

/* 0x06: Fin de la partida (3 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t winner_role;    /* 1 = Ganó P1, 2 = Ganó P2 */
    uint8_t final_score_p1;
    uint8_t final_score_p2;
} msg_game_over_t;

/* ========================================================================== */
/*                PROTOTIPOS DE FUNCIONES AUXILIARES (HELPERS)                */
/* ========================================================================== */

/* Envía exactamente 'len' bytes por el socket garantizando envío completo */
int send_all(int sockfd, const void *buf, size_t len);

/* Recibe exactamente 'len' bytes del socket garantizando recepción completa */
int recv_all(int sockfd, void *buf, size_t len);

/* Empaqueta el header y el payload, y los transmite por TCP */
int send_msg(int sockfd, uint8_t type, const void *payload, uint16_t payload_len);

/* Lee primero el header fijo de 3 bytes y luego lee exactamente su payload */
int recv_msg(int sockfd, msg_header_t *out_header, void *out_payload, size_t max_payload_len);


#endif /* PROTOCOL_H */