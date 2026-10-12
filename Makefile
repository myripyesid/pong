CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g

# Objetivos principales
all: server client

# Compilación del Servidor (enlaza main.c, logger.c y protocol.c)
server: src/server/main.c src/server/logger.c src/protocol.c
	$(CC) $(CFLAGS) src/server/main.c src/server/logger.c src/protocol.c -o server

# Compilación del Cliente
client: src/client/main.c src/protocol.c
	$(CC) $(CFLAGS) src/client/main.c src/protocol.c -o client

# Limpieza de binarios y temporales
clean:
	rm -f server client *.o src/*.o src/server/*.o src/client/*.o *.log
	@echo "Limpieza completada."

.PHONY: all clean
