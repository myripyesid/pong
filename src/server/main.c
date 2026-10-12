#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "logger.h"

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
    logger_log(LOG_INFO, "Puerto configurado: %d", port);
    logger_log(LOG_INFO, "Archivo de registro: %s", log_filename);
    logger_log(LOG_INFO, "Módulo de argumentos y Logger cargado exitosamente.");

    logger_close();
    return EXIT_SUCCESS;
}
