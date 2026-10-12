CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g

all: server client

server: src/server/main.c src/server/server.c src/server/logger.c src/protocol.c
	$(CC) $(CFLAGS) src/server/main.c src/server/server.c src/server/logger.c src/protocol.c -o server

client: src/client/main.c src/protocol.c
	$(CC) $(CFLAGS) src/client/main.c src/protocol.c -o client

clean:
	rm -f server client *.o src/*.o src/server/*.o src/client/*.o *.log
	@echo "Limpieza completada."

.PHONY: all clean