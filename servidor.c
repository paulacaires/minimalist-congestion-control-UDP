#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include "packet.h"

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

    printf("[SERVER] Aguardando conexao na porta 8080\n");

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

                sendto(sockfd, &sa, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);

                // Aguarda o ACK para finalizar o handshake
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
        } else {
            printf("[OUT-OF-ORDER] Esperado: %u | Recebido: %u\n", expected_seq, cur_seq);
        }

        // Envia o ACK
        Packet ack_p = {0};
        ack_p.num_ack = htons(expected_seq);
        ack_p.flag_ack = 1;
        sendto(sockfd, &ack_p, sizeof(Packet), 0, (struct sockaddr *)&cliaddr, len);
    }

    printf("\nTotal Recebido: %u bytes\n", total_bytes);
    printf("Perdas Simuladas: %u\n", packets_lost);

    close(sockfd);
    return 0;
}