CC      = gcc
CFLAGS  = -Wall -Wextra -O2

all: servidor cliente

servidor: servidor.c packet.h
	$(CC) $(CFLAGS) -o servidor servidor.c

cliente: cliente.c packet.h
	$(CC) $(CFLAGS) -o cliente cliente.c

plot: cwnd_log.csv
	python3 plot_cwnd.py

clean:
	rm -f servidor cliente cwnd_log.csv

.PHONY: all plot clean