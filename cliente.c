#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include "packet.h"

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    float cwnd = MSS;
    uint32_t ssthresh = INITIAL_SSTHRESH;
    uint16_t next_seq = 100;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = RTO_MS * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    Packet syn_pkt = {0};
    syn_pkt.num_seq = htons(next_seq);
    syn_pkt.flag_syn = 1;
    
    printf("[HANDSHAKE] Enviando SYN (Seq: %d)\n", next_seq);
    sendto(sockfd, &syn_pkt, sizeof(Packet), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    Packet res;
    if (recv(sockfd, &res, sizeof(Packet), 0) < 0) {
        exit(1);
    }
    printf("[HANDSHAKE] SYN+ACK recebido! Seq_Server: %u | Ack_Recebido: %u\n", 
           ntohs(res.num_seq), ntohs(res.num_ack));
    next_seq++;

    for (int i = 0; i < 10; i++) {
        Packet data_pkt = {0};
        data_pkt.num_seq = htons(next_seq);
        strcpy(data_pkt.data, "Dados");

        printf("[SEND] Seq: %u | CWND: %.2f\n", next_seq, cwnd);
        sendto(sockfd, &data_pkt, sizeof(Packet), 0, (const struct sockaddr *)&servaddr, sizeof(servaddr));
        
        if (recv(sockfd, &res, sizeof(Packet), 0) < 0) {
            printf("[TIMEOUT] Perda do Seq: %u\n", next_seq);
            cwnd = MSS;
        } else {
            printf("[RECV] Ack: %u (Seq_Server: %u)\n", ntohs(res.num_ack), ntohs(res.num_seq));

            if (cwnd < ssthresh) cwnd += MSS;
            else cwnd += (float)(MSS * MSS) / cwnd;
            
            next_seq++;
        }
        usleep(100000);
    }

    close(sockfd);
    return 0;
}