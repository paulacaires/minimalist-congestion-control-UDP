#include "packet.h"

#include <stdio.h>
#include <stdlib.h>   // exit
#include <string.h>   // memset
#include <unistd.h>   // close
#include <time.h>     // time
#include <errno.h>    // errno, EAGAIN

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h> // gettimeofday, struct timeval

#define SERVER_IP   "127.0.0.1"
#define PORT        8080

// Parâmetros do controle de congestionamento (do enunciado)
#define CWND_INICIAL    MSS         // 1024 bytes
#define SSTHRESH_INCIAL (15 * MSS)  // 15360 bytes
#define RTO_MS          500         // Retransmission Timeout: 500ms
#define TOTAL_DADOS     (50 * MSS)  // Total de bytes a enviar: 50KB

// ─────────────────────────────────────────────────────────────
// Estado do controle de congestionamento
// ─────────────────────────────────────────────────────────────
typedef enum {
    SLOW_START,
    CONGESTION_AVOIDANCE
} Estado;

typedef struct {
    double cwnd;      // janela de congestionamento (em bytes)
    double ssthresh;  // threshold: abaixo = Slow Start, acima = Cong. Avoidance
    Estado estado;
    uint32_t retransmissoes;
} ControleCongestionamento;

// ─────────────────────────────────────────────────────────────
// Configura timeout no socket (para implementar o RTO)
//
// SO_RCVTIMEO faz o recvfrom() retornar EAGAIN após o tempo configurado,
// sem precisar de threads — é o jeito mais simples de implementar RTO.
// ─────────────────────────────────────────────────────────────
void configurar_timeout(int socket_fd, int ms) {
    struct timeval tv;
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// ─────────────────────────────────────────────────────────────
// Retorna o tempo atual em milissegundos (para medir RTT)
// ─────────────────────────────────────────────────────────────
double agora_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// ─────────────────────────────────────────────────────────────
// Atualiza o CWND após receber um ACK
//
// Slow Start:          CWND += 1 MSS por ACK  → cresce exponencialmente
// Congestion Avoidance: CWND += MSS²/CWND     → cresce ~1 MSS por RTT
// ─────────────────────────────────────────────────────────────
void ao_receber_ack(ControleCongestionamento *cc, uint16_t ack_num) {
    if (cc->estado == SLOW_START) {
        cc->cwnd += MSS;

        printf("[CWND] Slow Start: cwnd=%.0f bytes (%.2f MSS) | ssthresh=%.0f\n",
               cc->cwnd, cc->cwnd / MSS, cc->ssthresh);

        // Transição para Congestion Avoidance quando cwnd atinge ssthresh
        if (cc->cwnd >= cc->ssthresh) {
            cc->estado = CONGESTION_AVOIDANCE;
            printf("[CWND] *** Transicao para Congestion Avoidance ***\n");
        }

    } else { // CONGESTION_AVOIDANCE
        // Incremento linear: MSS²/CWND por ACK = 1 MSS por RTT completo
        cc->cwnd += (double)MSS * MSS / cc->cwnd;

        printf("[CWND] Cong. Avoidance: cwnd=%.1f bytes (%.3f MSS) | ssthresh=%.0f\n",
               cc->cwnd, cc->cwnd / MSS, cc->ssthresh);
    }
}

// ─────────────────────────────────────────────────────────────
// Reage a um timeout (perda detectada via RTO expirado)
//
// RFC 5681 §3.1:
//   ssthresh = max(cwnd / 2, 2*MSS)
//   cwnd     = 1 MSS
//   volta para Slow Start
// ─────────────────────────────────────────────────────────────
void ao_timeout(ControleCongestionamento *cc) {
    printf("[CWND] TIMEOUT! cwnd=%.0f -> ssthresh=%.0f, cwnd=1MSS -> Slow Start\n",
           cc->cwnd, cc->cwnd / 2.0);

    cc->ssthresh = cc->cwnd / 2.0;
    if (cc->ssthresh < MSS) cc->ssthresh = MSS; // mínimo de 1 MSS

    cc->cwnd   = MSS;
    cc->estado = SLOW_START;
    cc->retransmissoes++;
}

// ─────────────────────────────────────────────────────────────
// Envia um pacote de dados
// ─────────────────────────────────────────────────────────────
void enviar_dados(int socket_fd, struct sockaddr_in *server_addr,
                  uint16_t num_seq, const char *dados, uint16_t tamanho) {
    Packet packet;
    memset(&packet, 0, sizeof(Packet));

    packet.num_seq       = num_seq;
    packet.bytes_enviados = tamanho;
    packet.flags         = 0; // pacote de dados puro, sem flags especiais

    memcpy(packet.data, dados, tamanho);

    // Converte header para byte order da rede
    header_network_friendly(&packet);

    sendto(socket_fd, &packet, HEADER_SIZE + tamanho, 0,
           (struct sockaddr *)server_addr, sizeof(*server_addr));
}

/*
Three-way handshake (lado cliente):

1. Envia SYN com ISN escolhido pelo cliente
2. Recebe SYN+ACK do servidor
3. Envia ACK confirmando

Retorna o num_seq inicial para os dados, ou -1 em erro
*/
int fazer_handshake(int socket_fd, struct sockaddr_in *server_addr, uint16_t *meu_isn) {
    *meu_isn = (uint16_t)(rand() % 1000 + 100);

    // PASSO 1: Envia SYN
    Packet syn;
    memset(&syn, 0, sizeof(Packet));
    syn.num_seq = *meu_isn;
    syn.flags   = FLAG_SYN;
    header_network_friendly(&syn);

    sendto(socket_fd, &syn, HEADER_SIZE, 0,
           (struct sockaddr *)server_addr, sizeof(*server_addr));

    printf("[CLIENT] SYN enviado -> meu_isn=%u\n", *meu_isn);

    // PASSO 2: Aguarda SYN+ACK do servidor
    configurar_timeout(socket_fd, 2000); // espera até 2 segundos

    Packet resposta;
    memset(&resposta, 0, sizeof(Packet));

    ssize_t bytes = recvfrom(socket_fd, &resposta, sizeof(Packet), 0, NULL, NULL);

    if (bytes < HEADER_SIZE) {
        printf("[CLIENT] Timeout no handshake\n");
        return -1;
    }

    if (!((resposta.flags & FLAG_SYN) && (resposta.flags & FLAG_ACK))) {
        printf("[CLIENT] Esperava SYN+ACK\n");
        return -1;
    }

    header_human_friendly(&resposta);
    uint16_t isn_servidor = resposta.num_seq;
    printf("[CLIENT] SYN+ACK recebido -> isn_servidor=%u\n", isn_servidor);

    // PASSO 3: Envia ACK
    Packet ack;
    memset(&ack, 0, sizeof(Packet));
    ack.num_seq = *meu_isn + 1;
    ack.num_ack = isn_servidor + 1;
    ack.flags   = FLAG_ACK;
    header_network_friendly(&ack);

    sendto(socket_fd, &ack, HEADER_SIZE, 0,
           (struct sockaddr *)server_addr, sizeof(*server_addr));

    printf("[CLIENT] ACK enviado -> handshake completo!\n\n");

    return *meu_isn + 1; // primeiro num_seq dos dados
}

int main(void) {
    srand((unsigned)time(NULL));

    // Prepara o buffer de dados a enviar (50KB de 'A','B','C'...)
    char buffer_envio[TOTAL_DADOS];
    for (int i = 0; i < TOTAL_DADOS; i++)
        buffer_envio[i] = (char)('A' + i % 26);

    // 1. Criar socket UDP
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    printf("[CLIENT] Conectando a %s:%d\n", SERVER_IP, PORT);

    // 2. Fazer o handshake
    uint16_t meu_isn;
    int seq_inicial = fazer_handshake(socket_fd, &server_addr, &meu_isn);

    if (seq_inicial < 0) {
        printf("[CLIENT] Handshake falhou\n");
        close(socket_fd);
        return 1;
    }

    // 3. Inicializa o controle de congestionamento
    ControleCongestionamento cc;
    cc.cwnd           = CWND_INICIAL;
    cc.ssthresh       = SSTHRESH_INCIAL;
    cc.estado         = SLOW_START;
    cc.retransmissoes = 0;

    printf("[CLIENT] Iniciando envio de %d bytes\n", TOTAL_DADOS);
    printf("[CLIENT] cwnd inicial: %.0f bytes | ssthresh: %.0f bytes\n\n",
           cc.cwnd, cc.ssthresh);

    // Variáveis de controle do envio
    uint32_t bytes_confirmados = 0; // bytes que o servidor já confirmou com ACK
    uint16_t proximo_seq       = (uint16_t)seq_inicial;

    // Estatísticas
    double   total_rtt    = 0.0;
    int      contagem_rtt = 0;
    uint32_t retransmissoes_total = 0;

    struct timeval inicio_tx;
    gettimeofday(&inicio_tx, NULL);

    configurar_timeout(socket_fd, RTO_MS);

    while (bytes_confirmados < TOTAL_DADOS) {
        uint32_t bytes_na_janela  = 0;
        int      pacotes_enviados = 0;

        // Guarda os pacotes para conseguir retransmitir
        uint16_t seqs[1024];
        uint16_t lens[1024];
        double   tempos_envio[1024];

        while (bytes_na_janela < (uint32_t)cc.cwnd &&
               (bytes_confirmados + bytes_na_janela) < TOTAL_DADOS) {

            uint32_t offset   = bytes_confirmados + bytes_na_janela;
            uint16_t tamanho  = (uint16_t)((TOTAL_DADOS - offset) < MSS
                                            ? (TOTAL_DADOS - offset)
                                            : MSS);

            enviar_dados(socket_fd, &server_addr,
                         proximo_seq, buffer_envio + offset, tamanho);

            seqs[pacotes_enviados]         = proximo_seq;
            lens[pacotes_enviados]         = tamanho;
            tempos_envio[pacotes_enviados] = agora_ms();

            printf("[CLIENT] -> Enviado seq=%u len=%u | cwnd=%.0f\n",
                   proximo_seq, tamanho, cc.cwnd);

            proximo_seq      += tamanho;
            bytes_na_janela  += tamanho;
            pacotes_enviados++;
        }

        // ── Fase de recepção: coleta ACKs ────────────────────
        int acks_recebidos = 0;

        while (acks_recebidos < pacotes_enviados) {
            Packet ack;
            memset(&ack, 0, sizeof(Packet));

            ssize_t bytes = recvfrom(socket_fd, &ack, sizeof(Packet), 0, NULL, NULL);

            if (bytes < 0) {
                // EAGAIN ou EWOULDBLOCK = timeout expirou (RTO)
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("[CLIENT] RTO expirado para seq=%u\n",
                           seqs[acks_recebidos]);

                    // Ajusta cwnd e ssthresh
                    ao_timeout(&cc);
                    retransmissoes_total++;

                    // Retransmite o pacote que causou timeout
                    uint32_t offset = bytes_confirmados +
                                      (seqs[acks_recebidos] - seqs[0]);

                    enviar_dados(socket_fd, &server_addr,
                                 seqs[acks_recebidos],
                                 buffer_envio + offset,
                                 lens[acks_recebidos]);

                    tempos_envio[acks_recebidos] = agora_ms();
                    printf("[CLIENT] Retransmitido seq=%u\n", seqs[acks_recebidos]);
                    continue;
                }
                perror("recvfrom");
                break;
            }

            // Não converte com header_human_friendly pois ACK tem campos mínimos
            if (!(ack.flags & FLAG_ACK)) continue;

            // Mede o RTT deste pacote
            double rtt = agora_ms() - tempos_envio[acks_recebidos];
            total_rtt    += rtt;
            contagem_rtt++;

            uint16_t ack_num = ntohs(ack.num_ack);
            printf("[CLIENT] <- ACK ack_num=%u | RTT=%.1f ms\n", ack_num, rtt);

            ao_receber_ack(&cc, ack_num);
            acks_recebidos++;
        }

        bytes_confirmados += bytes_na_janela;
        proximo_seq        = (uint16_t)(seq_inicial + bytes_confirmados);
    }

    // ── Encerramento: envia FIN ───────────────────────────────
    printf("\n[CLIENT] Envio concluido. Enviando FIN...\n");

    Packet fin;
    memset(&fin, 0, sizeof(Packet));
    fin.num_seq = proximo_seq;
    fin.flags   = FLAG_FIN;
    header_network_friendly(&fin);

    sendto(socket_fd, &fin, HEADER_SIZE, 0,
           (struct sockaddr *)&server_addr, sizeof(server_addr));

    // Aguarda FIN+ACK do servidor
    Packet fin_ack;
    configurar_timeout(socket_fd, 2000);
    recvfrom(socket_fd, &fin_ack, sizeof(Packet), 0, NULL, NULL);
    printf("[CLIENT] Conexao encerrada.\n");

    // ── Estatísticas finais ───────────────────────────────────
    struct timeval fim_tx;
    gettimeofday(&fim_tx, NULL);
    double tempo_total = (fim_tx.tv_sec  - inicio_tx.tv_sec)
                       + (fim_tx.tv_usec - inicio_tx.tv_usec) / 1e6;

    printf("\n========= ESTATISTICAS DO CLIENTE =========\n");
    printf("Dados enviados    : %u bytes\n",  bytes_confirmados);
    printf("Retransmissoes    : %u\n",         retransmissoes_total);
    printf("RTT medio         : %.2f ms\n",
           contagem_rtt > 0 ? total_rtt / contagem_rtt : 0.0);
    printf("Tempo total       : %.2f s\n",     tempo_total);
    if (tempo_total > 0)
        printf("Throughput        : %.2f KB/s\n",
               (bytes_confirmados / 1024.0) / tempo_total);

    close(socket_fd);
    return 0;
}