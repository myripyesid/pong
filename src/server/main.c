#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "logger.h"
#include "server.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso incorrecto.\n");
        fprintf(stderr, "Sintaxis esperada: %s <PORT> <LogFile>\n", *argv);
        return EXIT_FAILURE;
    }

    int port = atoi(*(argv + 1));
    const char *log_filename = *(argv + 2);

    if (port <= 1023 || port > 65535) {
        fprintf(stderr, "[Error] Puerto inválido: %d. Debe estar en el rango 1024 - 65535.\n", port);
        return EXIT_FAILURE;
    }

    if (logger_init(log_filename) != 0) {
        fprintf(stderr, "[Error] No se pudo inicializar el archivo de log: %s\n", log_filename);
        return EXIT_FAILURE;
    }

    logger_log(LOG_INFO, "=== PongServer iniciando ===");
    logger_log(LOG_INFO, "Puerto objetivo: %d", port);
    logger_log(LOG_INFO, "Archivo de log: %s", log_filename);

    int server_fd = init_server_socket(port);
    if (server_fd < 0) {
        logger_log(LOG_ERROR, "Fallo fatal en inicialización de red. Abortando servidor.");
        logger_close();
        return EXIT_FAILURE;
    }

    /* Bucle concurrente del lobby y emparejamiento */
    run_server_loop(server_fd);

    close_server_socket(server_fd);
    logger_log(LOG_INFO, "=== PongServer detenido ordenadamente ===");
    logger_close();
    return EXIT_SUCCESS;
}
