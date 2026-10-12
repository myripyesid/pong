#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "protocol.h"

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Uso: %s <SERVER_IP> <PORT> <NICKNAME> <EMAIL>\n", *argv);
        return EXIT_FAILURE;
    }

    const char *server_ip = *(argv + 1);
    int port = atoi(*(argv + 2));
    const char *nickname = *(argv + 3);
    const char *user_email = *(argv + 4);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error al crear socket cliente");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    printf("[Cliente] Conectando a %s:%d...\n", server_ip, port);
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error en connect()");
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("[Cliente] Conectado exitosamente.\n");

    /* 1. Enviar registro de protocolo */
    msg_register_req_t reg;
    memset(&reg, 0, sizeof(reg));
    strncpy(reg.nickname, nickname, MAX_NICKNAME_LEN - 1);
    strncpy((&reg)->email, user_email, MAX_EMAIL_LEN - 1);

    printf("[Cliente] Enviando registro: Nick='%s', Email='%s'...\n", reg.nickname, (&reg)->email);
    send_msg(sockfd, MSG_REGISTER_REQ, &reg, sizeof(reg));

    /* 2. Esperar respuesta de registro del Lobby */
    msg_header_t hdr;
    msg_register_resp_t resp;
    if (recv_msg(sockfd, &hdr, &resp, sizeof(resp)) < 0 || hdr.type != MSG_REGISTER_RESP) {
        fprintf(stderr, "[Cliente] Error esperando confirmación de registro.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("[Cliente] Registro confirmado. Esperando rival en el Lobby...\n");

    /* 3. Esperar paquete de inicio de partida */
    msg_match_start_t match_start;
    if (recv_msg(sockfd, &hdr, &match_start, sizeof(match_start)) < 0 || hdr.type != MSG_MATCH_START) {
        fprintf(stderr, "[Cliente] Error esperando inicio de partida.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("==========================================\n");
    printf("¡PARTIDA INICIADA! (Match ID: %d)\n", match_start.match_id);
    printf("Tu Rol: %s\n", (match_start.player_role == ROLE_PLAYER_1) ? "Pala Izquierda (P1)" : "Pala Derecha (P2)");
    printf("Tu Rival: %s\n", match_start.opponent_nickname);
    printf("==========================================\n");

    sleep(3);
    close(sockfd);
    printf("[Cliente] Sesión de prueba finalizada.\n");
    return EXIT_SUCCESS;
}
