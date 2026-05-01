
CC     = gcc
CFLAGS = -Wall -Wextra -g
 
all: servidor cliente
 
servidor: servidor.c packet.h
	$(CC) $(CFLAGS) -o servidor servidor.c
 
cliente: cliente.c packet.h
	$(CC) $(CFLAGS) -o cliente cliente.c
 
clean:
	rm -f servidor cliente
