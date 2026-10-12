#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
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

/* Hilo de ejecución de una partida */
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

    sleep(3);

    close(match->p1.sockfd);
    close(match->p2.sockfd);
    logger_log(LOG_INFO, "[Partida %d] Sesión de prueba finalizada y sockets cerrados", match->match_id);

    free(match);
    pthread_exit(NULL);
}

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
