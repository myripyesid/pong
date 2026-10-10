CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude -g

#Objetos principales
all: server client

# Compilación del servidor
server: src/server/main.c
	$(CC) $(CFLAGS) src/server/main.c -o server

#Compilacion del cliente
client: src/client/main.c 
	$(CC) $(CFLAGS) src/client/main.c -o client
#Limpieza de binarios y archivos temporales
clean: 
	rm -f server client *.o server/*.o client/*.o 
	@echo "Limpieza completada." 
.PHONY: all clean
