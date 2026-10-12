#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include <pthread.h>
#include "protocol.h"

#define SERVER_BACKLOG 10

/* Información de un jugador conectado en el servidor */
typedef struct {
    int sockfd;
    char nickname[MAX_NICKNAME_LEN];
    char email[MAX_EMAIL_LEN];
    struct sockaddr_in address;
} player_info_t;

/* Datos delegados al hilo de partida */
typedef struct {
    uint8_t match_id;
    player_info_t p1;
    player_info_t p2;
} match_context_t;

/* Inicializa el socket TCP maestro */
int init_server_socket(int port);

/* Bucle concurrente de aceptación de clientes y emparejamiento */
void run_server_loop(int server_fd);

/* Cierra el socket maestro */
void close_server_socket(int server_fd);

#endif /* SERVER_H */
