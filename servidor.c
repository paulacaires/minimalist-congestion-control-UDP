#include "packet.h"

#include <stdio.h>
#include <stdlib.h>   // exit
#include <string.h>   // memset
#include <unistd.h>   // close
#include <time.h>     // rand, srand

#include <sys/socket.h>
#include <netinet/in.h>

#define PORT             8080
#define LOSS_PROBABILITY 10   // % de chance de simular perda de pacote

// ─────────────────────────────────────────────────────────────
// Simula perda de pacote aleatoriamente
// Retorna 1 se o pacote deve ser descartado, 0 se deve ser processado
// ─────────────────────────────────────────────────────────────
int simular_perda() {
    return (rand() % 100) < LOSS_PROBABILITY;
}

// ─────────────────────────────────────────────────────────────
// Envia um ACK para o cliente
// ack_num = próximo número de sequência esperado
// ─────────────────────────────────────────────────────────────
void enviar_ack(int socket_fd, struct sockaddr_in *client_addr, socklen_t client_len,
                uint16_t num_seq, uint16_t ack_num) {
    Packet ack;
    memset(&ack, 0, sizeof(Packet));

    ack.num_seq = num_seq;
    ack.num_ack = ack_num;
    ack.flags   = FLAG_ACK;

    // Converte para byte order da rede antes de enviar
    header_network_friendly(&ack);

    sendto(socket_fd, &ack, HEADER_SIZE, 0,
           (struct sockaddr *)client_addr, client_len);

    printf("[SERVER] ACK enviado -> ack_num=%u\n", ack_num);
}

// ─────────────────────────────────────────────────────────────
// Three-way handshake (lado servidor):
//
//   Cliente            Servidor
//     |---SYN(seq=X)-->|    recebe SYN
//     |<--SYN+ACK------|    envia SYN(seq=Y) + ACK(ack=X+1)
//     |---ACK(ack=Y+1)>|    recebe confirmação
//
// Retorna o próximo num_seq esperado nos dados, ou -1 em erro
// ─────────────────────────────────────────────────────────────
int fazer_handshake(int socket_fd, struct sockaddr_in *client_addr, socklen_t *client_len) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    // PASSO 1: Aguarda SYN do cliente
    printf("[SERVER] Aguardando SYN do cliente...\n");

    ssize_t bytes_recebidos = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                                       (struct sockaddr *)client_addr, client_len);

    if (bytes_recebidos < HEADER_SIZE) {
        printf("[SERVER] Pacote muito pequeno, ignorando\n");
        return -1;
    }

    // BUG CORRIGIDO: packet e struct (nao ponteiro), usa . e nao ->
    if (!(packet.flags & FLAG_SYN)) {
        printf("[SERVER] Esperava SYN, recebi outra coisa\n");
        return -1;
    }

    // BUG CORRIGIDO: converte os campos recebidos da rede para o formato do host
    header_human_friendly(&packet);
    uint16_t nseq_cliente = packet.num_seq;
    printf("[SERVER] SYN recebido -> num_seq=%u\n", nseq_cliente);

    // PASSO 2: Envia SYN+ACK
    uint16_t nseq_server = (uint16_t)(rand() % 1000 + 1);

    Packet server_syn_ack;
    // BUG CORRIGIDO: sempre zerar a struct antes de usar
    memset(&server_syn_ack, 0, sizeof(Packet));

    server_syn_ack.num_seq = nseq_server;
    // BUG CORRIGIDO: num_ack deve ser nseq_cliente + 1 (proximo byte esperado)
    server_syn_ack.num_ack = nseq_cliente + 1;
    server_syn_ack.flags   = FLAG_SYN | FLAG_ACK;

    header_network_friendly(&server_syn_ack);

    sendto(socket_fd, &server_syn_ack, HEADER_SIZE, 0,
           (struct sockaddr *)client_addr, *client_len);

    printf("[SERVER] SYN+ACK enviado -> num_seq=%u, num_ack=%u\n",
           nseq_server, nseq_cliente + 1);

    // PASSO 3: Aguarda ACK final do cliente
    memset(&packet, 0, sizeof(Packet));
    bytes_recebidos = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                               (struct sockaddr *)client_addr, client_len);

    if (bytes_recebidos < HEADER_SIZE) return -1;

    if (!(packet.flags & FLAG_ACK)) {
        printf("[SERVER] Esperava ACK final do handshake\n");
        return -1;
    }

    printf("[SERVER] Handshake concluido!\n\n");
    return nseq_cliente + 1;
}

// ─────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────
int main(void) {
    srand((unsigned)time(NULL));

    // 1. Criar socket UDP
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_port        = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("[SERVER] Escutando na porta %d\n", PORT);
    printf("[SERVER] Probabilidade de perda simulada: %d%%\n\n", LOSS_PROBABILITY);

    // 2. Fazer o handshake
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // BUG CORRIGIDO: passa &client_addr e &client_len (ponteiros)
    int proximo_seq_esperado = fazer_handshake(socket_fd, &client_addr, &client_len);

    if (proximo_seq_esperado < 0) {
        printf("[SERVER] Handshake falhou, encerrando\n");
        close(socket_fd);
        return 1;
    }

    printf("[SERVER] Proximo seq esperado: %d\n\n", proximo_seq_esperado);

    // 3. Receber pacotes de dados
    uint32_t total_bytes_recebidos = 0;
    uint32_t total_pacotes         = 0;
    uint32_t pacotes_perdidos      = 0;

    Packet packet;

    while (1) {
        memset(&packet, 0, sizeof(Packet));

        ssize_t bytes_recebidos = recvfrom(socket_fd, &packet, sizeof(Packet), 0,
                                           (struct sockaddr *)&client_addr, &client_len);

        if (bytes_recebidos < HEADER_SIZE) continue;

        header_human_friendly(&packet);

        // Verifica FIN: cliente quer encerrar
        if (packet.flags & FLAG_FIN) {
            printf("\n[SERVER] FIN recebido -> encerrando conexao\n");

            Packet fin_ack;
            memset(&fin_ack, 0, sizeof(Packet));
            fin_ack.num_seq = (uint16_t)proximo_seq_esperado;
            fin_ack.num_ack = packet.num_seq + 1;
            fin_ack.flags   = FLAG_FIN | FLAG_ACK;
            header_network_friendly(&fin_ack);

            sendto(socket_fd, &fin_ack, HEADER_SIZE, 0,
                   (struct sockaddr *)&client_addr, client_len);
            break;
        }

        total_pacotes++;

        // 4. Simula perda de pacote
        // Se "perder", nao envia ACK -> cliente vai aguardar RTO e retransmitir
        if (simular_perda()) {
            pacotes_perdidos++;
            printf("[SERVER] Pacote seq=%u DESCARTADO (simulando perda)\n",
                   packet.num_seq);
            continue;
        }

        // Pacote chegou na ordem correta
        if (packet.num_seq == (uint16_t)proximo_seq_esperado) {
            total_bytes_recebidos += packet.bytes_enviados;
            proximo_seq_esperado  += packet.bytes_enviados;

            printf("[SERVER] Dados seq=%u len=%u | total=%u bytes\n",
                   packet.num_seq, packet.bytes_enviados, total_bytes_recebidos);
        } else {
            printf("[SERVER] Fora de ordem: seq=%u (esperava %d)\n",
                   packet.num_seq, proximo_seq_esperado);
        }

        // 5. Envia ACK
        enviar_ack(socket_fd, &client_addr, client_len,
                   (uint16_t)proximo_seq_esperado,
                   (uint16_t)proximo_seq_esperado);
    }

    printf("\n========= ESTATISTICAS DO SERVIDOR =========\n");
    printf("Bytes recebidos   : %u\n", total_bytes_recebidos);
    printf("Pacotes recebidos : %u\n", total_pacotes);
    printf("Pacotes perdidos  : %u\n", pacotes_perdidos);

    close(socket_fd);
    return 0;
}