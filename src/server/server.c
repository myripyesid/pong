#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>          /* [AGREGADO POR JUAN] para nanosleep() del game loop */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "server.h"
#include "logger.h"
#include "protocol.h"

static volatile sig_atomic_t g_server_running = 1;
static uint8_t g_next_match_id = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_server_running = 0;
}

int init_server_socket(int port) {
    int server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        logger_log(LOG_ERROR, "Fallo al invocar socket(): no se pudo crear el descriptor");
        return -1;
    }
    logger_log(LOG_DEBUG, "Socket maestro creado con descriptor fd=%d", server_fd);

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_log(LOG_WARN, "No se pudo configurar SO_REUSEADDR en el socket");
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        logger_log(LOG_ERROR, "Fallo al invocar bind() en el puerto %d", port);
        close(server_fd);
        return -1;
    }
    logger_log(LOG_INFO, "Enlace exitoso (bind) en 0.0.0.0:%d", port);

    if (listen(server_fd, SERVER_BACKLOG) < 0) {
        logger_log(LOG_ERROR, "Fallo al invocar listen() con backlog=%d", SERVER_BACKLOG);
        close(server_fd);
        return -1;
    }
    logger_log(LOG_INFO, "Servidor escuchando conexiones pasivas (backlog=%d)", SERVER_BACKLOG);

    return server_fd;
}

/* ==========================================================================
 *  >>> INICIO DEL BLOQUE AGREGADO POR JUAN <<<
 *  --------------------------------------------------------------------------
 *  El servidor original SOLO emparejaba a los dos jugadores, les enviaba
 *  MSG_MATCH_START, hacía sleep(3) y cerraba los sockets. NO había lógica de
 *  juego en absoluto.
 *
 *  Todo lo que va desde aquí hasta el marcador "FIN DEL BLOQUE" es el aporte de JUAN
 *  al servidor para que la partida por red funcione de verdad, respetando el
 *  protocolo ya definido en include/protocol.h:
 *      - MSG_PLAYER_MOVE  (cliente -> servidor, opcode 0x04)
 *      - MSG_GAME_STATE   (servidor -> clientes, opcode 0x05)
 *      - MSG_GAME_OVER    (servidor -> clientes, opcode 0x06)
 *
 *  Sistema de coordenadas (coincide exacto con el cliente en Python):
 *      - Campo lógico de GAME_FIELD_W x GAME_FIELD_H.
 *      - ball_x / ball_y        = CENTRO de la pelota.
 *      - paddle1_y / paddle2_y  = CENTRO vertical de cada pala.
 * ========================================================================== */

/* [AGREGADO POR JUAN] Parámetros de la simulación */
#define GAME_FIELD_W      1000
#define GAME_FIELD_H      600
#define GAME_PADDLE_W     16
#define GAME_PADDLE_H     110
#define GAME_BALL_RADIUS  12
#define GAME_WIN_SCORE    5
#define GAME_TICK_MS      16                            /* ~62 fps */
#define GAME_TICK_SEC     (GAME_TICK_MS / 1000.0)
#define GAME_PADDLE_VEL   (420.0 * GAME_TICK_SEC)       /* unidades por tick */
#define GAME_BALL_SPEED0  380.0                         /* unidades/segundo  */
#define GAME_BALL_GAIN    1.06

/* [AGREGADO POR JUAN] Entrada de un jugador, compartida con su hilo lector */
typedef struct {
    int sockfd;
    volatile int direction;   /* -1 = arriba, 0 = quieto, 1 = abajo */
    volatile int alive;       /* pasa a 0 cuando el cliente se desconecta */
} player_input_t;

/* [AGREGADO POR JUAN] Hilo lector de movimientos.
 * Se ejecuta en paralelo al bucle de juego para que recv_msg() bloqueante no
 * detenga la simulación. Solo guarda la última dirección pedida por el cliente. */
static void *input_reader_thread(void *arg) {
    player_input_t *in = (player_input_t *)arg;
    msg_header_t hdr;
    msg_player_move_t mv;

    while (in->alive) {
        int rc = recv_msg(in->sockfd, &hdr, &mv, sizeof(mv));
        if (rc < 0) {
            break;   /* error o cliente desconectado */
        }
        if (hdr.type == MSG_PLAYER_MOVE) {
            in->direction = mv.direction;
        } else if (hdr.type == MSG_DISCONNECT) {
            break;
        }
    }
    in->alive = 0;
    return NULL;
}

/* [AGREGADO POR JUAN] Envía el estado autoritativo a los dos jugadores */
static void broadcast_state(const match_context_t *match, const msg_game_state_t *st) {
    send_msg(match->p1.sockfd, MSG_GAME_STATE, st, sizeof(*st));
    send_msg(match->p2.sockfd, MSG_GAME_STATE, st, sizeof(*st));
}

/* [AGREGADO POR JUAN] Hilo de ejecución de una partida: ahora con bucle real */
static void *match_worker(void *arg) {
    match_context_t *match = (match_context_t *)arg;

    logger_log(LOG_INFO, "[Partida %d] Hilo iniciado. Emparejando: %s (P1) vs %s (P2)",
               match->match_id, match->p1.nickname, match->p2.nickname);

    /* 1. Notificar inicio a Jugador 1 */
    msg_match_start_t start_p1;
    memset(&start_p1, 0, sizeof(start_p1));
    start_p1.player_role = ROLE_PLAYER_1;
    start_p1.match_id = match->match_id;
    strncpy(start_p1.opponent_nickname, match->p2.nickname, MAX_NICKNAME_LEN - 1);
    send_msg(match->p1.sockfd, MSG_MATCH_START, &start_p1, sizeof(start_p1));

    /* 2. Notificar inicio a Jugador 2 */
    msg_match_start_t start_p2;
    memset(&start_p2, 0, sizeof(start_p2));
    start_p2.player_role = ROLE_PLAYER_2;
    start_p2.match_id = match->match_id;
    strncpy(start_p2.opponent_nickname, match->p1.nickname, MAX_NICKNAME_LEN - 1);
    send_msg(match->p2.sockfd, MSG_MATCH_START, &start_p2, sizeof(start_p2));

    logger_log(LOG_INFO, "[Partida %d] Señales MSG_MATCH_START enviadas a ambos clientes", match->match_id);

    /* ======================================================================
     * >>> BLOQUE DE JUAN: SIMULACIÓN DE LA PARTIDA (game loop) <<<
     * ==================================================================== */

    /* Un hilo lector por jugador, para no bloquear la simulación */
    player_input_t in1 = { match->p1.sockfd, MOVE_STILL, 1 };
    player_input_t in2 = { match->p2.sockfd, MOVE_STILL, 1 };
    pthread_t t1, t2;
    pthread_create(&t1, NULL, input_reader_thread, &in1);
    pthread_create(&t2, NULL, input_reader_thread, &in2);

    /* --- Estado inicial de la partida --- */
    msg_game_state_t st;
    memset(&st, 0, sizeof(st));

    double ball_x = GAME_FIELD_W / 2.0;
    double ball_y = GAME_FIELD_H / 2.0;
    double p1_y   = GAME_FIELD_H / 2.0;
    double p2_y   = GAME_FIELD_H / 2.0;
    /* La pelota sale hacia un lado distinto según el número de partida
     * (más simple que rand() y evita condiciones de carrera entre hilos).
     * OJO: el vy se calcula con % 4 - 1.5 para que NUNCA sea exactamente 0;
     * si fuera 0 la pelota iría en horizontal perfecta y rebotaría para
     * siempre entre dos palas centradas (fin de partida imposible). */
    double ball_vx = (match->match_id % 2) ? GAME_BALL_SPEED0 : -GAME_BALL_SPEED0;
    double ball_vy = (((match->match_id % 4) - 1.5) / 4.0) * GAME_BALL_SPEED0 * 0.8;
    uint8_t score1 = 0, score2 = 0;
    uint32_t frame = 0;
    int half_paddle = GAME_PADDLE_H / 2;

    logger_log(LOG_INFO, "[Partida %d] ¡Comienza la simulación a ~%d fps!",
               match->match_id, 1000 / GAME_TICK_MS);

    while (in1.alive && in2.alive) {
        /* --- 1) Mover palas según la intención recibida de cada cliente --- */
        p1_y += in1.direction * GAME_PADDLE_VEL;
        p2_y += in2.direction * GAME_PADDLE_VEL;
        if (p1_y < half_paddle) p1_y = half_paddle;
        if (p1_y > GAME_FIELD_H - half_paddle) p1_y = GAME_FIELD_H - half_paddle;
        if (p2_y < half_paddle) p2_y = half_paddle;
        if (p2_y > GAME_FIELD_H - half_paddle) p2_y = GAME_FIELD_H - half_paddle;

        /* --- 2) Física de la pelota --- */
        ball_x += ball_vx * GAME_TICK_SEC;
        ball_y += ball_vy * GAME_TICK_SEC;

        /* Rebote en las paredes superior e inferior */
        if (ball_y - GAME_BALL_RADIUS < 0) {
            ball_y = GAME_BALL_RADIUS;
            ball_vy = -ball_vy;
        } else if (ball_y + GAME_BALL_RADIUS > GAME_FIELD_H) {
            ball_y = GAME_FIELD_H - GAME_BALL_RADIUS;
            ball_vy = -ball_vy;
        }

        /* Rebote en la pala izquierda (P1) */
        if (ball_vx < 0 && ball_x - GAME_BALL_RADIUS <= GAME_PADDLE_W &&
            ball_y >= p1_y - half_paddle && ball_y <= p1_y + half_paddle) {
            ball_x = GAME_PADDLE_W + GAME_BALL_RADIUS;
            ball_vx = -ball_vx * GAME_BALL_GAIN;
            ball_vy += ((ball_y - p1_y) / half_paddle) * 220.0;
            logger_log(LOG_DEBUG, "[Partida %d] P1 golpea la pelota (frame %u)",
                       match->match_id, frame);
        }
        /* Rebote en la pala derecha (P2) */
        if (ball_vx > 0 && ball_x + GAME_BALL_RADIUS >= GAME_FIELD_W - GAME_PADDLE_W &&
            ball_y >= p2_y - half_paddle && ball_y <= p2_y + half_paddle) {
            ball_x = GAME_FIELD_W - GAME_PADDLE_W - GAME_BALL_RADIUS;
            ball_vx = -ball_vx * GAME_BALL_GAIN;
            ball_vy += ((ball_y - p2_y) / half_paddle) * 220.0;
            logger_log(LOG_DEBUG, "[Partida %d] P2 golpea la pelota (frame %u)",
                       match->match_id, frame);
        }

        /* --- 3) Punto para alguien --- */
        int point_for = 0;
        if (ball_x < -20) { score2++; point_for = 2; }
        else if (ball_x > GAME_FIELD_W + 20) { score1++; point_for = 1; }

        if (point_for) {
            logger_log(LOG_INFO, "[Partida %d] PUNTO para P%d. Marcador: %d - %d",
                       match->match_id, point_for, score1, score2);
            /* Reinicia la pelota al centro hacia quien NO anotó.
             * Igual que arriba: vy garantizado distinto de cero. */
            ball_x = GAME_FIELD_W / 2.0;
            ball_y = GAME_FIELD_H / 2.0;
            ball_vx = (point_for == 1 ? 1.0 : -1.0) * GAME_BALL_SPEED0;
            ball_vy = (((frame % 4) - 1.5) / 4.0) * GAME_BALL_SPEED0 * 0.8;

            if (score1 >= GAME_WIN_SCORE || score2 >= GAME_WIN_SCORE) {
                break;   /* fin de la partida */
            }
        }

        /* --- 4) Difundir el estado autoritativo a ambos clientes --- */
        st.ball_x    = (int16_t)ball_x;
        st.ball_y    = (int16_t)ball_y;
        st.paddle1_y = (int16_t)p1_y;
        st.paddle2_y = (int16_t)p2_y;
        st.score_p1  = score1;
        st.score_p2  = score2;
        st.frame_id  = frame++;
        st.reserved  = 0;
        broadcast_state(match, &st);

        /* --- 5) Esperar hasta el siguiente tick (~60 fps) --- */
        struct timespec ts = { 0, GAME_TICK_MS * 1000000L };
        nanosleep(&ts, NULL);
    }

    /* --- Fin de partida: proclamar ganador --- */
    msg_game_over_t over;
    over.final_score_p1 = score1;
    over.final_score_p2 = score2;
    over.winner_role    = (score1 >= score2) ? ROLE_PLAYER_1 : ROLE_PLAYER_2;
    send_msg(match->p1.sockfd, MSG_GAME_OVER, &over, sizeof(over));
    send_msg(match->p2.sockfd, MSG_GAME_OVER, &over, sizeof(over));
    logger_log(LOG_INFO, "[Partida %d] FIN DE PARTIDA. Ganador: P%d (%d - %d)",
               match->match_id, over.winner_role, score1, score2);

    /* --- Limpieza: detener hilos lectores y cerrar sockets --- */
    in1.alive = 0;
    in2.alive = 0;
    shutdown(match->p1.sockfd, SHUT_RDWR);
    shutdown(match->p2.sockfd, SHUT_RDWR);
    close(match->p1.sockfd);
    close(match->p2.sockfd);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    logger_log(LOG_INFO, "[Partida %d] Hilos y sockets liberados", match->match_id);

    free(match);
    pthread_exit(NULL);
}

/* ==========================================================================
 *  >>> FIN DEL BLOQUE AGREGADO POR JUAN <<<
 * ========================================================================== */

void run_server_loop(int server_fd) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    (&sa)->sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);

    player_info_t waiting_player;
    int has_waiting_player = 0;

    logger_log(LOG_INFO, "Lobby inicializado. A la espera de conexiones...");

    while (g_server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (!g_server_running) break;
            logger_log(LOG_WARN, "Fallo al ejecutar accept() en socket maestro");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        logger_log(LOG_INFO, "Nueva conexión aceptada desde %s:%d (fd=%d)", client_ip, client_port, client_fd);

        /* Handshake de registro de protocolo */
        msg_header_t header;
        msg_register_req_t reg_req;
        memset(&reg_req, 0, sizeof(reg_req));

        if (recv_msg(client_fd, &header, &reg_req, sizeof(reg_req)) < 0 || header.type != MSG_REGISTER_REQ) {
            logger_log(LOG_ERROR, "Cliente fd=%d envió paquete inválido durante registro. Desconectando.", client_fd);
            close(client_fd);
            continue;
        }

        logger_log(LOG_INFO, "Jugador registrado: Nickname='%s', Email='%s' (fd=%d)",
                   reg_req.nickname, (&reg_req)->email, client_fd);

        /* Responder con confirmación de registro */
        msg_register_resp_t reg_resp;
        reg_resp.status = STATUS_REGISTER_OK;
        reg_resp.player_id = has_waiting_player ? 2 : 1;
        send_msg(client_fd, MSG_REGISTER_RESP, &reg_resp, sizeof(reg_resp));

        /* Lógica del Lobby */
        if (!has_waiting_player) {
            waiting_player.sockfd = client_fd;
            strncpy(waiting_player.nickname, reg_req.nickname, MAX_NICKNAME_LEN);
            strncpy((&waiting_player)->email, (&reg_req)->email, MAX_EMAIL_LEN);
            waiting_player.address = client_addr;
            has_waiting_player = 1;
            logger_log(LOG_INFO, "Jugador '%s' colocado en espera en el Lobby.", waiting_player.nickname);
        } else {
            match_context_t *match = (match_context_t *)malloc(sizeof(match_context_t));
            if (!match) {
                logger_log(LOG_ERROR, "No se pudo asignar memoria para la partida");
                close(client_fd);
                continue;
            }

            match->match_id = g_next_match_id++;
            match->p1 = waiting_player;
            match->p2.sockfd = client_fd;
            strncpy(match->p2.nickname, reg_req.nickname, MAX_NICKNAME_LEN);
            strncpy((&(match->p2))->email, (&reg_req)->email, MAX_EMAIL_LEN);
            match->p2.address = client_addr;

            has_waiting_player = 0;

            pthread_t tid;
            if (pthread_create(&tid, NULL, match_worker, match) != 0) {
                logger_log(LOG_ERROR, "Fallo al crear hilo con pthread_create");
                free(match);
                continue;
            }
            pthread_detach(tid);
            logger_log(LOG_INFO, "Partida %d emparejada. Hilo independiente creado exitosamente.", match->match_id);
        }
    }

    if (has_waiting_player) {
        close(waiting_player.sockfd);
    }
}

void close_server_socket(int server_fd) {
    if (server_fd >= 0) {
        close(server_fd);
        logger_log(LOG_INFO, "Socket maestro cerrado (fd=%d)", server_fd);
    }
}
