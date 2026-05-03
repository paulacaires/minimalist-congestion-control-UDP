#include "packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/socket.h>
#include <netinet/in.h>

#define PORT             8080
#define LOSS_PROBABILITY 10   // % de chance de simular perda de pacote

int simular_perda() {
    return (rand() % 100) < LOSS_PROBABILITY;
}

void enviar_ack(int socket_fd, struct sockaddr_in *client_addr, socklen_t client_len,
                uint16_t num_seq, uint16_t ack_num) {
    Packet ack;
    memset(&ack, 0, sizeof(Packet));

    ack.num_seq = num_seq;
    ack.num_ack = ack_num;
    set_len_flags(&ack, 0, FLAG_ACK); // tamanho=0, flag=ACK

    header_network_friendly(&ack);

    sendto(socket_fd, &ack, HEADER_SIZE, 0,
           (struct sockaddr *)client_addr, client_len);

    printf("[SERVER] ACK enviado -> ack_num=%u\n", ack_num);
}

int fazer_handshake(int socket_fd, struct sockaddr_in *client_addr, socklen_t *client_len) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // PASSO 1: Aguarda SYN do cliente
    printf("[SERVER] Aguardando SYN do cliente...\n");

    ssize_t bytes_recebidos = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                                       (struct sockaddr *)client_addr, client_len);

    if (bytes_recebidos < HEADER_SIZE) {
        printf("[SERVER] Pacote muito pequeno\n");
        return -1;
    }

    // Converte antes de ler qualquer campo
    header_human_friendly(&packet);

    // Agora usamos get_flags() em vez de packet.flags
    if (!(get_flags(&packet) & FLAG_SYN)) {
        printf("[SERVER] Esperava SYN, recebi outra coisa\n");
        return -1;
    }

    uint16_t nseq_cliente = packet.num_seq;
    printf("[SERVER] SYN recebido -> num_seq=%u\n", nseq_cliente);

    // PASSO 2: Envia SYN+ACK
    uint16_t nseq_server = (uint16_t)(rand() % 1000 + 1);

    Packet syn_ack;
    memset(&syn_ack, 0, sizeof(Packet));
    syn_ack.num_seq = nseq_server;
    syn_ack.num_ack = nseq_cliente + 1;
    set_len_flags(&syn_ack, 0, FLAG_SYN | FLAG_ACK); // tamanho=0, flags=SYN+ACK

    header_network_friendly(&syn_ack);

    sendto(socket_fd, &syn_ack, HEADER_SIZE, 0,
           (struct sockaddr *)client_addr, *client_len);

    printf("[SERVER] SYN+ACK enviado -> num_seq=%u, num_ack=%u\n",
           nseq_server, nseq_cliente + 1);

    // PASSO 3: Aguarda ACK final
    memset(&packet, 0, sizeof(Packet));
    bytes_recebidos = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                               (struct sockaddr *)client_addr, client_len);

    if (bytes_recebidos < HEADER_SIZE) return -1;

    header_human_friendly(&packet);

    if (!(get_flags(&packet) & FLAG_ACK)) {
        printf("[SERVER] Esperava ACK final do handshake\n");
        return -1;
    }

    printf("[SERVER] Handshake concluido!\n\n");
    return nseq_cliente + 1;
}

int main(void) {
    srand((unsigned)time(NULL));

    // 1. Criar socket UDP
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); exit(EXIT_FAILURE);
    }

    printf("[SERVER] Escutando na porta %d\n", PORT);
    printf("[SERVER] Probabilidade de perda simulada: %d%%\n\n", LOSS_PROBABILITY);

    // 2. Fazer o handshake
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int proximo_seq_esperado = fazer_handshake(socket_fd, &client_addr, &client_len);
    if (proximo_seq_esperado < 0) {
        printf("[SERVER] Handshake falhou\n");
        close(socket_fd);
        return 1;
    }

    printf("[SERVER] Proximo seq esperado: %d\n\n", proximo_seq_esperado);

    // 3. Receber pacotes de dados
    uint32_t total_bytes   = 0;
    uint32_t total_pacotes = 0;
    uint32_t perdidos      = 0;

    Packet packet;

    while (1) {
        memset(&packet, 0, sizeof(Packet));

        ssize_t n = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                             (struct sockaddr *)&client_addr, &client_len);
        if (n < HEADER_SIZE) continue;

        // Converte antes de ler qualquer campo
        header_human_friendly(&packet);

        uint8_t  flags = get_flags(&packet);   // lê os 3 bits baixos
        uint16_t len   = get_len(&packet);     // lê os 13 bits altos

        // Verifica FIN
        if (flags & FLAG_FIN) {
            printf("\n[SERVER] FIN recebido -> encerrando\n");

            Packet fin_ack;
            memset(&fin_ack, 0, sizeof(Packet));
            fin_ack.num_seq = (uint16_t)proximo_seq_esperado;
            fin_ack.num_ack = packet.num_seq + 1;
            set_len_flags(&fin_ack, 0, FLAG_FIN | FLAG_ACK);
            header_network_friendly(&fin_ack);

            sendto(socket_fd, &fin_ack, HEADER_SIZE, 0,
                   (struct sockaddr *)&client_addr, client_len);
            break;
        }

        total_pacotes++;

        // Simula perda — não envia ACK, cliente vai retransmitir
        if (simular_perda()) {
            perdidos++;
            printf("[SERVER] Pacote seq=%u DESCARTADO (simulando perda)\n",
                   packet.num_seq);
            continue;
        }

        // Pacote na ordem correta
        if (packet.num_seq == (uint16_t)proximo_seq_esperado) {
            total_bytes           += len;
            proximo_seq_esperado  += len;
            printf("[SERVER] Dados seq=%u len=%u | total=%u bytes\n",
                   packet.num_seq, len, total_bytes);
        } else {
            printf("[SERVER] Fora de ordem: seq=%u (esperava %d)\n",
                   packet.num_seq, proximo_seq_esperado);
        }

        enviar_ack(socket_fd, &client_addr, client_len,
                   (uint16_t)proximo_seq_esperado,
                   (uint16_t)proximo_seq_esperado);
    }

    printf("\n========= ESTATISTICAS DO SERVIDOR =========\n");
    printf("Bytes recebidos   : %u\n", total_bytes);
    printf("Pacotes recebidos : %u\n", total_pacotes);
    printf("Pacotes perdidos  : %u\n", perdidos);

    close(socket_fd);
    return 0;
}