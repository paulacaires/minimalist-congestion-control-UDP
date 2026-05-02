#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include "packet.h"

typedef enum { SLOW_START, CONGESTION_AVOIDANCE } State;

double get_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080);
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    struct timeval tv = {0, RTO_MS * 1000};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    srand(time(NULL));
    float cwnd = MSS;
    uint32_t ssthresh = INITIAL_SSTHRESH;
    uint16_t client_isn = rand() % 5000;
    uint16_t base_seq = 0, next_seq = 0;
    State state = SLOW_START;

    uint32_t retransmissions = 0, rtt_count = 0;
    double total_rtt = 0, start_time_total = get_now();

    printf("[CLIENT] Iniciando Handshake (ISN: %u)\n", client_isn);
    int connected = 0;
    while (!connected) {
        Packet syn = {0}; syn.num_seq = htons(client_isn); syn.flag_syn = 1;
        sendto(sockfd, &syn, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        
        Packet res;
        if (recv(sockfd, &res, sizeof(Packet), 0) > 0 && res.flag_syn && res.flag_ack) {
            if (ntohs(res.num_ack) == client_isn + 1) {
                uint16_t serv_seq = ntohs(res.num_seq);
                Packet ack = {0};
                ack.num_ack = htons(serv_seq + 1); ack.flag_ack = 1;
                sendto(sockfd, &ack, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                
                base_seq = client_isn + 1;
                next_seq = base_seq;
                connected = 1;
                printf("[CLIENT] Conectado - Iniciando transferencia\n");
            }
        }
    }

    uint32_t total_to_send = 50 * MSS; 
    uint32_t confirmed = 0;

    while (confirmed < total_to_send) {
        uint32_t in_flight = 0;
        int burst_size = 0;
        uint16_t window_seqs[MAX_WINDOW_ARRAY];
        double window_times[MAX_WINDOW_ARRAY];

        while (in_flight + MSS <= (uint32_t)cwnd && (confirmed + in_flight) < total_to_send && burst_size < MAX_WINDOW_ARRAY) {
            Packet p = {0};
            p.num_seq = htons(next_seq);
            p.bytes_enviados = htons(MSS);
            
            window_seqs[burst_size] = next_seq;
            window_times[burst_size] = get_now();
            
            sendto(sockfd, &p, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            
            printf("[SEND] Seq: %u | Janela (CWND): %.0f\n", next_seq, cwnd);

            next_seq += MSS;
            in_flight += MSS;
            burst_size++;
        }

        int timeout_occurred = 0;
        for (int i = 0; i < burst_size; i++) {
            Packet res;
            if (recv(sockfd, &res, sizeof(Packet), 0) < 0) {
                timeout_occurred = 1;
                break; 
            } else {
                uint16_t ack_val = ntohs(res.num_ack);
                if (ack_val > (base_seq + confirmed)) {
                    printf("[ACK] Recebido: %u\n", ack_val);
                    
                    double rtt = get_now() - window_times[i];
                    total_rtt += rtt;
                    rtt_count++;

                    uint32_t diff = ack_val - (base_seq + confirmed);
                    confirmed += diff;

                    if (state == SLOW_START) {
                        cwnd += MSS;
                        if (cwnd >= ssthresh) state = CONGESTION_AVOIDANCE;
                    } else {
                        cwnd += (float)(MSS * MSS) / cwnd;
                    }
                }
            }
        }

        if (timeout_occurred) {
            printf("[TIMEOUT] Perda detectada - Retransmitindo a partir de Seq: %u\n", base_seq + confirmed);
            retransmissions++;
            ssthresh = (uint32_t)(cwnd / 2);
            if (ssthresh < MSS) ssthresh = MSS;
            cwnd = MSS;
            state = SLOW_START;
            next_seq = base_seq + confirmed; 
        }
    }

    Packet fin = {0}; fin.flag_fin = 1;
    sendto(sockfd, &fin, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    printf("\nTotal Enviado: %u bytes\n", confirmed);
    printf("Retransmissoes: %u\n", retransmissions);
    printf("RTT Medio: %.2f ms\n", (rtt_count > 0) ? total_rtt / rtt_count : 0);

    close(sockfd);
    return 0;
}