#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* Niveles de registro */
typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_DEBUG
} log_level_t;

/* Inicializa el logger abriendo el archivo en modo append y configurando el mutex */
int logger_init(const char *filename);

/* Registra un mensaje con marca de tiempo, nivel y formato variable (thread-safe) */
void logger_log(log_level_t level, const char *format, ...);

/* Cierra el archivo de log y destruye el mutex */
void logger_close(void);

#endif /* LOGGER_H */
