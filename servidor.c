#include "packet.h"

#include <stdio.h>
#include <stdlib.h>   // exit
#include <string.h>   // memset
#include <unistd.h>   // close

#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 8080

int main(void) {
    // 1. Criar socket UDP
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);   
    }

    struct sockaddr_in server_addr, client_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY; // aceita de qualquer IP
 
    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(1);
    }
 
    printf("[SERVER] Servidor escutando na porta %d\n", PORT);

    while (1) {
        sleep(1);
    }
}
