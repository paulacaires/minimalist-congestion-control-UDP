#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include "packet.h"

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(8080);

    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));
    srand(time(NULL));

    uint16_t server_seq = 500;

    printf("[SERVER] Online na porta 8080\n");

    while (1) {
        Packet pkt;
        recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len);

        uint16_t c_seq = ntohs(pkt.num_seq);
        
        if (rand() % 10 < 1) {
            printf("[PERDA] Ignorando Pacote Seq: %u\n", c_seq);
            continue;
        }

        Packet ack_pkt = {0};
        ack_pkt.num_seq = htons(server_seq); 
        ack_pkt.num_ack = htons(c_seq + 1);  
        ack_pkt.flag_ack = 1;

        if (pkt.flag_syn) {
            printf("[RECV] SYN (Seq: %u) | [SEND] SYN+ACK (Seq: %u, Ack: %u)\n", c_seq, server_seq, c_seq + 1);
            ack_pkt.flag_syn = 1;
        } else {
            printf("[RECV] Dados (Seq: %u) | [SEND] ACK (Seq: %u, Ack: %u)\n", c_seq, server_seq, c_seq + 1);
        }

        sendto(sockfd, &ack_pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
        
        server_seq++; 
    }
    return 0;
}