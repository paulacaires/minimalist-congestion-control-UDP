#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include "packet.h"

// Definição dos estados
typedef enum { SLOW_START, CONGESTION_AVOIDANCE } State;

// Função que retorna o tempo atual em milissegundos
double get_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    //tv_sec conta os segundos e tv_usec conta os microssegundos entre cada segundo
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

int main() {
    // Cria um socket UDP IPv4
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    // Estrutura que guarda o IP e a Porta
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080); // Define a porta 8080 (Big Endian)
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Endereço IP do servidor
    // 100.96.226.59
    // Configura o RTO -> estrutura timeval recebe apenas segundos e microssegundos
    struct timeval tv = {0, RTO_MS * 1000};
    // Define o RTO para o recebimento de dados no socket
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Variáveis de controle
    srand(time(NULL));
    float cwnd = MSS; // CWND inicial
    uint32_t ssthresh = INITIAL_SSTHRESH; // Limiar do SLOW START -> CONG AVOIDANCE
    uint16_t client_isn = rand() % 5000; // Seq number inicial aleatório
    // base_seq: Primeira seq enviada
    // (next_seq - (base_seq + confirmed)) -> pacotes não confirmados
    uint16_t base_seq = 0, next_seq = 0;
    State state = SLOW_START;

    // Variáveis para contagem de retransmissões e RTT
    uint32_t retransmissions = 0, rtt_count = 0;
    double total_rtt = 0;

    // Three Way Handshake
    printf("[CLIENT] Iniciando Handshake (ISN: %u)\n", client_isn);
    int connected = 0;
    while (!connected) {
        // Prepara o pacote zerado (evitar lixo de memória) - define o ISN e o SYN
        Packet syn = {0}; syn.num_seq = htons(client_isn); syn.flag_syn = 1;
        // Envia para o endereço e porta do servidor
        sendto(sockfd, &syn, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        
        // Espera o SYN-ACK do servidor e conclui o handshake
        Packet res;
        if (recv(sockfd, &res, sizeof(Packet), 0) > 0 && res.flag_syn && res.flag_ack) {
            // Se o ack_number da resposta for igual ao nosso seq_number + 1
            if (ntohs(res.num_ack) == client_isn + 1) {
                // Guarda o seq number do server
                uint16_t serv_seq = ntohs(res.num_seq);
                Packet ack = {0};
                // Define o ack_number (num_seq do servidor + 1) e ativa a flag ACK
                ack.num_ack = htons(serv_seq + 1); ack.flag_ack = 1;
                // seq_number
                ack.num_seq = res.num_ack;
                // Envia para o server, concluindo o handshake
                sendto(sockfd, &ack, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                // Alinha a base da sequência de envio com o seq_number atual (cliente)
                next_seq = ntohs(res.num_ack);
                base_seq = next_seq;
                // Sai do loop
                connected = 1;
                printf("[CLIENT] Conectado - Iniciando transferencia\n");
            }
        }
    }

    // Loop principal de transferência (enquanto os bytes confirmados < 51200)
    uint32_t total_to_send = 50 * MSS; // Envio de 50 pacotes
    uint32_t confirmed = 0;
    while (confirmed < total_to_send) {
        uint32_t in_flight = 0;
        int burst_size = 0; // Tamanho da rajada de envio (antes de ouvir os ACKs)
        // Guarda seq_number e o tempo para RTT para cada pacote
        uint16_t window_seqs[MAX_WINDOW_ARRAY];
        double window_times[MAX_WINDOW_ARRAY];

        // Envio dos pacotes (rajada) com o limite da janela de congestionamento e limite de envio
        while (in_flight + MSS <= (uint32_t)cwnd && (confirmed + in_flight) < total_to_send) {
            Packet p = {0};
            p.num_seq = htons(next_seq);
            p.bytes_enviados = htons(MSS);
            
            // Para cada posição do array (cada envio), guarda o seq_number e o tempo para o RTT
            window_seqs[burst_size] = next_seq;
            window_times[burst_size] = get_now();
            
            // Envia o pacote
            sendto(sockfd, &p, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            printf("[SEND] Seq: %u | Janela (CWND): %.0f\n", next_seq, cwnd);

            next_seq += MSS;
            in_flight += MSS;
            // Contador de pacotes enviados
            burst_size++;
        }

        // Tenta receber os ACKs para cada pacote enviado na rajada
        int timeout_occurred = 0;
        for (int i = 0; i < burst_size; i++) {
            Packet res;
            // Timeout se o servidor não responder a tempo
            if (recv(sockfd, &res, sizeof(Packet), 0) < 0) {
                timeout_occurred = 1; // recv não espera indefinidamente após a definição anterior do RTO
                break; 
            } else {
                uint16_t ack_val = ntohs(res.num_ack);
                // Se o ACK for válido (maior que o último já confirmado), atualiza os bytes confirmados e ajusta a cwnd
                if (ack_val > (base_seq + confirmed)) {
                    printf("[ACK] Recebido: %u\n", ack_val);
                    
                    double rtt = get_now() - window_times[i]; // Calcula tempo de ida e volta
                    // Para cálculo do RTT médio
                    total_rtt += rtt;
                    rtt_count++;

                    // Incrementa os bytes confirmados
                    uint32_t diff = ack_val - (base_seq + confirmed);
                    confirmed += diff;

                    // Crescimento da cwnd
                    if (state == SLOW_START) {
                        cwnd += MSS;
                        if (cwnd >= ssthresh) state = CONGESTION_AVOIDANCE;
                    } else {
                        cwnd += (float)(MSS * MSS) / cwnd;
                    }
                }
            }
        }

        // Em caso de timeout, reduz o ssthresh, reseta a cwnd e volta ao Slow Start
        if (timeout_occurred) {
            printf("[TIMEOUT] Perda detectada - Retransmitindo a partir de Seq: %u\n", base_seq + confirmed);
            retransmissions++;
            ssthresh = (uint32_t)(cwnd / 2);
            // Para que o ssthresh seja no mínimo um MSS (caso a perda ocorra quando a cwnd ainda é um MSS)
            if (ssthresh < MSS) ssthresh = MSS;
            cwnd = MSS;
            state = SLOW_START;
            next_seq = base_seq + confirmed; // Ponto de retransmissão
        }
    }

    // FIN para encerrar conexão
    Packet fin = {0}; fin.flag_fin = 1;
    sendto(sockfd, &fin, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    // Exibe o relatório final com estatísticas de performance da transmissão
    printf("\nTotal Enviado: %u bytes\n", confirmed);
    printf("Retransmissoes: %u\n", retransmissions);
    printf("RTT Medio: %.2f ms\n", (rtt_count > 0) ? total_rtt / rtt_count : 0);

    close(sockfd); // Fecha o descritor do socket
    return 0;
}