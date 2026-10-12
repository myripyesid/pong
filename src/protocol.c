#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "protocol.h"

int send_all(int sockfd, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;

    while (total_sent < len) {
        ssize_t sent = send(sockfd, ptr + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            return -1; /* Error o desconexión */
        }
        total_sent += (size_t)sent;
    }
    return 0;
}

int recv_all(int sockfd, void *buf, size_t len) {
    size_t total_rcvd = 0;
    char *ptr = (char *)buf;

    while (total_rcvd < len) {
        ssize_t rcvd = recv(sockfd, ptr + total_rcvd, len - total_rcvd, 0);
        if (rcvd <= 0) {
            return -1; /* 0 = socket cerrado por el par, <0 = error */
        }
        total_rcvd += (size_t)rcvd;
    }
    return 0;
}

int send_msg(int sockfd, uint8_t type, const void *payload, uint16_t payload_len) {
    msg_header_t header;
    header.type = type;
    header.length = htons(payload_len); /* Conversión a Network Byte Order */

    /* 1. Enviar encabezado de 3 bytes */
    if (send_all(sockfd, &header, sizeof(msg_header_t)) < 0) {
        return -1;
    }

    /* 2. Enviar payload (si existe) */
    if (payload_len > 0 && payload != NULL) {
        if (send_all(sockfd, payload, payload_len) < 0) {
            return -1;
        }
    }

    return 0;
}

int recv_msg(int sockfd, msg_header_t *out_header, void *out_payload, size_t max_payload_len) {
    /* 1. Leer encabezado fijo (3 bytes) */
    if (recv_all(sockfd, out_header, sizeof(msg_header_t)) < 0) {
        return -1;
    }

    /* Convertir longitud de red a formato del host */
    out_header->length = ntohs(out_header->length);

    /* Validar que el buffer de destino no sufra desbordamiento */
    if (out_header->length > max_payload_len) {
        fprintf(stderr, "[Protocol] Error: payload recibido (%u) supera buffer (%zu)\n",
                out_header->length, max_payload_len);
        return -2;
    }

    /* 2. Leer payload si la longitud es mayor a cero */
    if (out_header->length > 0 && out_payload != NULL) {
        if (recv_all(sockfd, out_payload, out_header->length) < 0) {
            return -1;
        }
    }

    return 0;
}
