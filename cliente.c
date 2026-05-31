#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>   // necessário para fcntl() e O_NONBLOCK
#include "packet.h"
#include <string.h>

// Definição dos estados
typedef enum { SLOW_START, CONGESTION_AVOIDANCE, FAST_RECOVERY } State;

// Registro de um pacote que foi enviado e ainda está sendo acompanhado pelo protocolo
typedef struct {
    Packet pkt;     // Cópia do pacote enviado
    uint16_t seq;   // Número de sequência
    double sent_at; // Timestamp do envio (ms)
    int in_use;     // Slot ocupado?  
} TransmissionEntry;

// Função que retorna o tempo atual em milissegundos
double get_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    //tv_sec conta os segundos e tv_usec conta os microssegundos entre cada segundo
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Função que implementa o Timeout na aplicação (não no socket)
int recv_com_timeout(int sockfd, Packet *buf, double deadline) {
    while (1) {
        double now = get_now();
        if (now >= deadline) return 0; // Passou da deadline = timeout

        double tempo_restante = deadline - now;
        struct timeval tv;
        tv.tv_sec = (long)(tempo_restante / 1000.0);
        tv.tv_usec = (long)(((long long)tempo_restante % 1000) * 1000);
    
        // Preparar um select para perguntar se o socket tem dados disponíveis para leitura
        // Conjunto de file descriptors: lista de file descriptors que quero monitorar para leitura
        fd_set read_fds;
        // Limpa (tipo um conjunto vazio)
        FD_ZERO(&read_fds);
        // Adiciona o nosso próprio socket no conjunto de sockets monitorados
        FD_SET(sockfd, &read_fds);
        // Pergunta se chegou algum dado nos fds monitorados (+1 porque começa do zero)
        int s = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
        if (s < 0) return -1; // -1 significa erro
        if (s == 0) return 0; // timeout
        
        // Efetivamente ler os dados
        ssize_t n = recv(sockfd, buf, sizeof(Packet), 0);
        if (n > 0) return (int) n;
        if (n == 0) return 0;
    }
}

// Código pronto para o log
static FILE *log_fp = NULL;
static double log_t0 = 0;
static void log_cwnd(float cwnd, uint32_t rwnd, uint32_t menor_janela) {
    if (!log_fp) return;
        fprintf(log_fp, "%.1f,%.0f,%u,%u\n",
            get_now() - log_t0, cwnd, rwnd, menor_janela);
}

int main() {
    // [Log CWND + rwnd + janela efetiva, de fato]
    log_fp = fopen("cwnd_log.csv", "w");
    if (log_fp) fprintf(log_fp, "tempo_ms,cwnd,rwnd,janela_efetiva\n");
    log_t0 = get_now();
    

    // Cria um socket UDP IPv4
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // Colocar o socket no modo não-bloqueante
    // F_GETFL -> Devolver as flags atuais desse socket
    int flags = fcntl(sockfd, F_GETFL, 0);
    // F_SETFL -> Configurar novas flags
    // "|" OR bit a bit para adicionar a flag O_NONBLOCK
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // Estrutura que guarda o IP e a Porta
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080); // Define a porta 8080 (Big Endian)
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Endereço IP do servidor
    // 100.96.226.59
    
    // Configura o RTO -> estrutura timeval recebe apenas segundos e microssegundos
    // struct timeval tv = {0, RTO_MS * 1000};
    // Define o RTO para o recebimento de dados no socket
    // setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Variáveis de controle
    srand(time(NULL));
    float cwnd = MSS; // CWND inicial
    uint32_t ssthresh = INITIAL_SSTHRESH; // Limiar do SLOW START -> CONG AVOIDANCE
    uint16_t client_isn = rand() % 5000; // Seq number inicial aleatório
    // base_seq: Primeira seq enviada
    // (next_seq - (base_seq + confirmed)) -> pacotes não confirmados
    uint16_t base_seq = 0, next_seq = 0;
    State state = SLOW_START;

    uint32_t rwnd = RECV_WINDOW_MAX;

    // Variáveis para contagem de retransmissões e RTT
    uint32_t retransmissions = 0, rtt_count = 0;
    double total_rtt = 0;

    // Buffer de retransmissão: guarda todos os pacotes em voo
    TransmissionEntry tx_buf[MAX_WINDOW_ARRAY];
    // memset(tx_buf, 0, sizeof(tx_buf));

    // Controle de ACKs duplicados
    uint16_t ultimo_ack = 0;
    int dup_ack_count = 0;

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
        double deadline = get_now() + RTO_MS;
        int n = recv_com_timeout(sockfd, &res, deadline);
        if (n > 0 && res.flag_syn && res.flag_ack) {
            if (ntohs(res.num_ack) == client_isn + 1) {
                // Guarda o seq number do server
                uint16_t serv_seq = ntohs(res.num_seq);

                // Descobre qual é a rwnd que o servidor quer
                rwnd = ntohs(res.buffer_recebimento);

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
                ultimo_ack = next_seq;
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
        // FASE DE ENVIO: Envia rajada até o limite da cwnd
        uint32_t in_flight = 0;
        // Conta bytes em voo (slots ocupados no tx_buf)
        for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
            if (tx_buf[i].in_use) in_flight += MSS;

        /*
        Tem que caber em ambas as janelas, então a janela_efetiva é a menor das duas.
        */
        uint32_t menor_janela = ((uint32_t)cwnd < rwnd) ? (uint32_t)cwnd : rwnd;
        
        // A rede permite mais tráfego, mas o servidor não.
        if (rwnd < (uint32_t)cwnd && rwnd > 0)
            printf("[FLOW-CTRL] rwnd (%u) limita cwnd (%.0f) — menor_janela=%u\n", rwnd, cwnd, menor_janela);

        if (rwnd == 0) {
            printf("[ZERO-WND] Servidor sem espaço — aguardando...\n");
            usleep(RTO_MS * 1000);
            continue;
        }
             
        int sent_this_round = 0;
        while (in_flight + MSS <= menor_janela && (confirmed + in_flight) < total_to_send) {
            // Procurar um slot livre no buffer de retransmissão
            int slot_livre = -1;
            for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                // encontrou alguma posição livre
                if (!tx_buf[i].in_use) {
                    slot_livre = i; 
                    break;
                }
            }

            if (slot_livre == -1) break;

            Packet p = {0};
            p.num_seq = htons(next_seq);
            p.bytes_enviados = htons(MSS);

            // Guarda no buffer de retransmissão
            tx_buf[slot_livre].pkt     = p;
            tx_buf[slot_livre].seq     = next_seq;
            tx_buf[slot_livre].sent_at = get_now();
            tx_buf[slot_livre].in_use  = 1;

            // Envia o pacote
            sendto(sockfd, &p, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            
            printf("[SEND] Seq: %u | CWND: %.0f | Estado: %s\n",
                   next_seq, cwnd,
                   state == SLOW_START          ? "SLOW_START" :
                   state == CONGESTION_AVOIDANCE ? "CONG_AVOID" : "FAST_RECOV");

            log_cwnd(cwnd, rwnd, menor_janela);
            next_seq  += MSS;
            in_flight += MSS;
            sent_this_round++;
        }

        if (sent_this_round == 0 && in_flight == 0) {
            // Não tem mais nada para enviar
            break;
        }

        // Aguarda ACKs por pacote, incluindo o timeout
        double oldest_sent = get_now();
        // Procura o pacote mais antigo em voo
        for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
            if (tx_buf[i].in_use && tx_buf[i].sent_at < oldest_sent)
                oldest_sent = tx_buf[i].sent_at;
        // O pacote mais antigo expira primeiro
        double deadline = oldest_sent + RTO_MS;

        Packet res;
        int n = recv_com_timeout(sockfd, &res, deadline);

        // Timeout  
        if (n == 0) {
            printf("[TIMEOUT] Perda detectada - retransmitindo a partir de seq base\n");
            retransmissions++;

            ssthresh = (uint32_t)(cwnd / 2);
            if (ssthresh < MSS) ssthresh = MSS;
            cwnd = MSS;
            state = SLOW_START;
            dup_ack_count = 0;

            log_cwnd(cwnd, rwnd, (uint32_t)cwnd < rwnd ? (uint32_t)cwnd : rwnd);

            // Retransmite todos os pacotes em voo
            // Volta para o ponto que não foi confirmado
            next_seq = base_seq + confirmed;

            for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
                tx_buf[i].in_use = 0;
        }

        // ACK recebido
        else if (n > 0) {
            uint16_t ack_val = ntohs(res.num_ack);

            // Atualiza a rwnd com base nos pacotes retornados pelo servidor
            uint16_t new_rwnd = ntohs(res.buffer_recebimento);
            if (new_rwnd != rwnd) {
                printf("[FLOW-CTRL] O servidor quer outra rwnd: %u → %u bytes\n", rwnd, new_rwnd);
                rwnd = new_rwnd;
            }

            // Verificar se é um ACK duplicado
            if (ack_val == ultimo_ack) {
                dup_ack_count++;
                printf("[DUP-ACK #%d] ACK: %u\n", dup_ack_count, ack_val);

                if (dup_ack_count == 3)  {
                    printf("[FAST-RETRANSMIT] 3 ACKs duplicados - retransmitindo seq: %u\n", ack_val);
                    retransmissions++;

                    ssthresh = (uint32_t)(cwnd / 2);
                    if (ssthresh < MSS) ssthresh = MSS;
                    // +3 pacotes a mais do que a ssthresh pela regra
                    cwnd  = (float)ssthresh + 3 * MSS;
                    state = FAST_RECOVERY;

                    uint32_t log_janela = (uint32_t)cwnd < rwnd ? (uint32_t)cwnd : rwnd;
                    log_cwnd(cwnd, rwnd, log_janela);

                    // Retransmite o pacote que gerou o ACK duplicado  
                    for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                        if (tx_buf[i].in_use && tx_buf[i].seq == ack_val) {
                            sendto(sockfd, &tx_buf[i].pkt, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                            tx_buf[i].sent_at = get_now();
                            printf("[RETX] Seq: %u\n", ack_val);
                            break;
                        }
                    }
                }
            
                else if (dup_ack_count > 3 && state == FAST_RECOVERY) {
                    /* 
                        Se continua chegando ACK duplicado, significa que mais pacotes
                        estão saindo da rede, então pode enviar mais pacotes.
                    */
                    cwnd += MSS;
                    uint32_t log_janela = (uint32_t)cwnd < rwnd ? (uint32_t)cwnd : rwnd;
                    log_cwnd(cwnd, rwnd, log_janela);
                }
            }
        
            // Se não é um ACK duplicado (normal)
            else if (ack_val > ultimo_ack) {
               printf("[ACK] Recebido: %u\n", ack_val);
               
               // Calcula o RTT. Qual pacote o ACK confirmou?
               for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                // "+ MSS" porque confirma o ACK que confirma o pacote X é o do número de seq + MSS
                if (tx_buf[i].in_use &&
                (uint16_t)(tx_buf[i].seq + MSS) == ack_val) {
                    double rtt = get_now() - tx_buf[i].sent_at;
                    total_rtt += rtt;
                    rtt_count++;
                    break;
                }
               }

               // Confirma para "trás" o ACK cumulativo
                for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                    if (tx_buf[i].in_use &&
                        (uint16_t)(tx_buf[i].seq + MSS) <= ack_val) {
                        tx_buf[i].in_use = 0;
                    }
                }

                // Atualiza bytes confirmados
                uint32_t diff = (uint16_t)(ack_val - ultimo_ack);
                confirmed += diff;

                // Depois de um ACK, ajustar CWND
                if (state == SLOW_START) {
                    cwnd += MSS;
                    if (cwnd >= ssthresh) state = CONGESTION_AVOIDANCE;
                } else if (state == CONGESTION_AVOIDANCE) {
                    cwnd += (float)(MSS * MSS) / cwnd;
                } else if (state == FAST_RECOVERY) {
                    cwnd  = (float)ssthresh;
                    state = CONGESTION_AVOIDANCE;
                    printf("[FAST-RECOVERY] Saindo — CWND deflacionada para %.0f\n", cwnd);
                }

                uint32_t log_janela = (uint32_t)cwnd < rwnd ? (uint32_t)cwnd : rwnd;
                log_cwnd(cwnd, rwnd, log_janela);
                ultimo_ack = ack_val;
                dup_ack_count = 0;
            }
        }
    }

    // FIN para encerrar conexão
    Packet fin = {0}; fin.flag_fin = 1;
    sendto(sockfd, &fin, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    // Exibe o relatório final com estatísticas de performance da transmissão
    printf("\nTotal Enviado: %u bytes\n", confirmed);
    printf("Retransmissoes: %u\n", retransmissions);
    printf("RTT Medio: %.2f ms\n", (rtt_count > 0) ? total_rtt / rtt_count : 0);

    if (log_fp) fclose(log_fp);
    close(sockfd); // Fecha o descritor do socket
    return 0;
}