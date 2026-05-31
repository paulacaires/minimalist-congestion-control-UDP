#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include "packet.h"
#include <string.h>

// Criação do slot do buffer de recebimento
// Para guardar pacotes recebidos fora de ordem
typedef struct {
    Packet pkt;
    uint16_t seq;
    uint16_t bytes;
    int in_use;
} ReceiveEntry;

// Entregar em ordem os pacotes que chegaram cedo demais e fora de ordem
uint32_t libera_pacotes_em_ordem(ReceiveEntry *rbuf, int rsize, uint16_t *expected_seq, uint32_t *total_bytes) {
    uint32_t delivered = 0;
    int progress = 1;

    /*depois que um pacote em ordem chega e expected_seq avança, ela varre o buffer procurando se o próximo pacote esperado já está guardado lá. Se estiver, entrega, avança expected_seq de novo, e repete até não encontrar mais nada em sequência.*/
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

static int slots_ocupados(ReceiveEntry *rbuf, int rsize) {
    int count = 0;
    for (int i = 0; i < rsize; i++)
        if (rbuf[i].in_use) count++;
    return count;
}

// Calcula a janela de recibimento disponível
static uint16_t calc_rwnd(ReceiveEntry *rbuf, int rsize) {
    int livres = rsize - slots_ocupados(rbuf, rsize);
    uint32_t rwnd_bytes = (uint32_t)livres * MSS;
    // Para evitar overflow
    if (rwnd_bytes > 65535) rwnd_bytes = 65535;
    return (uint16_t)rwnd_bytes;
}

int main() {
    // Cria o socket UDP IPv4
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len = sizeof(cliaddr);

    // Configura do endereço do servidor
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(8080);
    // Vincula o socket ao endereço configurado
    bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr));

    srand(time(NULL));
    uint16_t expected_seq = 0;
    uint32_t total_bytes = 0;
    uint32_t packets_lost = 0;
    uint32_t out_of_order = 0;

    // Buffer de reordenação
    ReceiveEntry rbuf[RECV_BUFFER_SIZE];
    memset(rbuf, 0, sizeof(rbuf));

    printf("[SERVER] Aguardando conexao na porta 8080\n");
    printf("[SERVER] Buffer de recebimento: %d slots / %d bytes\n", RECV_BUFFER_SIZE, RECV_WINDOW_MAX);

    Packet pkt;
    // Three-Way Handshake
    while (1) {
        // Recebe o pacote com a flag SYN
        if (recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len) > 0) {
            if (pkt.flag_syn) {
                uint16_t client_seq = ntohs(pkt.num_seq);
                uint16_t server_isn = rand() % 5000;

                // Responde com SYN-ACK
                Packet sa = {0};
                sa.num_seq = htons(server_isn);
                sa.num_ack = htons(client_seq + 1);
                sa.flag_syn = 1; sa.flag_ack = 1;
                // Falar qual é a janela que quer
                sa.buffer_recebimento = htons(calc_rwnd(rbuf, RECV_BUFFER_SIZE));

                sendto(sockfd, &sa, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);

                // Aguarda o ACK para finalizar o handshake
                if (recvfrom(sockfd, &pkt, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, &len) > 0) {
                    if (pkt.flag_ack && ntohs(pkt.num_ack) == (server_isn + 1)) {
                        expected_seq = client_seq + 1;
                        printf("[HANDSHAKE] Conexao estabelecida\n");
                        printf("[HANDSHAKE] rwnd inicial anunciada: %u bytes\n", sa.buffer_recebimento);

                        break;
                    }
                }
            }
        }
    }

    // Recebe os dados e simula de erros até receber FIN
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
            continue; // Ignora o pacote (não envia ACK)
        }

        // Verifica se o pacote recebido é o esperado para incrementar o próx seq_number e total recebido
        if (cur_seq == expected_seq) {
            expected_seq += b_recv; // Próximo seq_number esperado
            total_bytes += b_recv;
            printf("[DATA] Seq %u recebido - Total: %u bytes\n", cur_seq, total_bytes);

            // Depois que recebe tenta ver se tem um posterior que já chegou
            libera_pacotes_em_ordem(rbuf, RECV_BUFFER_SIZE, &expected_seq, &total_bytes);
        } else if ((int16_t)(cur_seq - expected_seq) > 0) {
            // Recebi o pacote, mas fora de ordem
            int achou_buffer = 0;
            for (int i = 0; i < RECV_BUFFER_SIZE; i++) {
                if (rbuf[i].in_use && rbuf[i].seq == cur_seq) {
                    achou_buffer = 1;
                    break;
                }
            }

            if (!achou_buffer) {
                // Colocar nele
                // Procurar um slot livre
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
                    printf("[OUT-OF-ORDER] Seq %u bufferizado (esperado: %u)\n",
                           cur_seq, expected_seq);           
                } else {
                    printf("[WARN] Buffer de reordenação cheio — Seq %u descartado\n",       cur_seq);

                }
            }
        } else {
            // Pacote já confirmado (retransmissão antiga)
            printf("[DUP-DATA] Seq %u já confirmado — ignorado\n", cur_seq);
        }

        // Envia o ACK
        Packet ack_p = {0};
        ack_p.num_ack = htons(expected_seq);
        ack_p.flag_ack = 1;
        // Calcula a nova janela
        uint16_t rwnd = calc_rwnd(rbuf, RECV_BUFFER_SIZE);
        ack_p.buffer_recebimento = htons(rwnd);
        sendto(sockfd, &ack_p, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);

        printf("[ACK-SENT] ACK=%u | rwnd=%u bytes (%d slots livres)\n",
        expected_seq, rwnd,
        (RECV_BUFFER_SIZE - slots_ocupados(rbuf, RECV_BUFFER_SIZE)));

    }

    printf("\n=== Relatório Final ===\n");
    printf("Total Recebido    : %u bytes\n", total_bytes);
    printf("Perdas Simuladas  : %u\n", packets_lost);
    printf("Fora de Ordem     : %u\n", out_of_order);
 
    close(sockfd);
    return 0;
}