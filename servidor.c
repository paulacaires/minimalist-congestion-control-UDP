#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include "packet.h"
#include <string.h>

typedef struct {
    Packet pkt;
    uint16_t seq;
    uint16_t bytes;
    int in_use;
} ReceiveEntry;

uint32_t libera_pacotes_em_ordem(ReceiveEntry *rbuf, int rsize, uint16_t *expected_seq, uint32_t *total_bytes) {
    uint32_t delivered = 0;
    int progress = 1;

    while (progress) {
        progress = 0;
        for (int i = 0; i < rsize; i++) {
            if (rbuf[i].in_use && rbuf[i].seq == *expected_seq) {
                printf("[REORDER-DELIVER] Seq %u entregue da fila\n", rbuf[i].seq);
                *expected_seq += rbuf[i].bytes;
                *total_bytes  += rbuf[i].bytes;
                delivered     += rbuf[i].bytes;
                rbuf[i].in_use = 0;
                progress = 1;    
            }
        }
    }
    return delivered;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(8080);
    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    srand(time(NULL));
    uint16_t expected_seq = 0;
    uint32_t total_bytes = 0;
    uint32_t packets_lost = 0;
    uint32_t out_of_order = 0;

    ReceiveEntry rbuf[RECV_BUFFER_SIZE];
    memset(rbuf, 0, sizeof(rbuf));

    printf("[SERVER] Aguardando conexao na porta 8080\n");

    Packet pkt;
    while (1) {
        if (recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len) > 0) {
            if (pkt.flag_syn) {
                uint16_t client_seq = ntohs(pkt.num_seq);
                uint16_t server_isn = rand() % 5000;

                Packet sa = {0};
                sa.num_seq = htons(server_isn);
                sa.num_ack = htons(client_seq + 1);
                sa.flag_syn = 1; sa.flag_ack = 1;
                
                // NOVO: Anuncia o buffer máximo inicial (cravado no limite do uint16_t para evitar overflow)
                sa.buffer_recebimento = htons(65535);

                sendto(sockfd, &sa, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);

                if (recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len) > 0) {
                    if (pkt.flag_ack && ntohs(pkt.num_ack) == (server_isn + 1)) {
                        expected_seq = client_seq + 1;
                        printf("[HANDSHAKE] Conexao estabelecida\n");
                        break;
                    }
                }
            }
        }
    }

    while (1) {
        if (recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len) <= 0) continue;

        if (pkt.flag_fin) {
            printf("[RECV] FIN recebido - Encerrando\n");
            break;
        }

        uint16_t cur_seq = ntohs(pkt.num_seq);
        uint16_t b_recv = ntohs(pkt.bytes_enviados);

        if (rand() % 10 < 1) {
            printf("[LOSS] Pacote Seq %u ignorado\n", cur_seq);
            packets_lost++;
            continue; 
        }

        if (cur_seq == expected_seq) {
            expected_seq += b_recv;
            total_bytes += b_recv;
            printf("[DATA] Seq %u recebido - Total: %u bytes\n", cur_seq, total_bytes);
            libera_pacotes_em_ordem(rbuf, RECV_BUFFER_SIZE, &expected_seq, &total_bytes);
        } else if ((int16_t)(cur_seq - expected_seq) > 0) {
            int achou_buffer = 0;
            for (int i = 0; i < RECV_BUFFER_SIZE; i++) {
                if (rbuf[i].in_use && rbuf[i].seq == cur_seq) {
                    achou_buffer = 1;
                    break;
                }
            }

            if (!achou_buffer) {
                int slot = -1;
                for (int i = 0; i < RECV_BUFFER_SIZE; i++) {
                    if (!rbuf[i].in_use) {
                        slot = i;
                        break;
                    }
                }

                if (slot >= 0) {
                    rbuf[slot].pkt    = pkt;
                    rbuf[slot].seq    = cur_seq;
                    rbuf[slot].bytes  = b_recv;
                    rbuf[slot].in_use = 1;
                    out_of_order++;
                    printf("[OUT-OF-ORDER] Seq %u bufferizado (esperado: %u)\n", cur_seq, expected_seq);            
                } else {
                    printf("[WARN] Buffer de reordenação cheio — Seq %u descartado\n", cur_seq);
                }
            }
        } else {
            printf("[DUP-DATA] Seq %u já confirmado — ignorado\n", cur_seq);
        }

        // NOVO: Lógica de Controle de Fluxo (Cálculo do espaço livre do buffer)
        uint16_t slots_usados = 0;
        for (int i = 0; i < RECV_BUFFER_SIZE; i++) {
            if (rbuf[i].in_use) slots_usados++;
        }
        
        // Protege contra overflow do tipo uint16_t (64 * 1024 = 65536)
        uint32_t espaco_livre_bytes = (RECV_BUFFER_SIZE - slots_usados) * MSS;
        uint16_t rwnd_anunciado = (espaco_livre_bytes > 65535) ? 65535 : (uint16_t)espaco_livre_bytes;

        // Envia o ACK
        Packet ack_p = {0};
        ack_p.num_ack = htons(expected_seq);
        ack_p.flag_ack = 1;
        ack_p.buffer_recebimento = htons(rwnd_anunciado); // NOVO: Insere a RWND no pacote

        sendto(sockfd, &ack_p, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
    }

    printf("\n=== Relatório Final ===\n");
    printf("Total Recebido    : %u bytes\n", total_bytes);
    printf("Perdas Simuladas  : %u\n", packets_lost);
    printf("Fora de Ordem     : %u\n", out_of_order);
 
    close(sockfd);
    return 0;
}