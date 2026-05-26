#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>   
#include "packet.h"

typedef enum { SLOW_START, CONGESTION_AVOIDANCE, FAST_RECOVERY } State;

typedef struct {
    Packet pkt;     
    uint16_t seq;   
    double sent_at; 
    int in_use;   
} TransmissionEntry;

double get_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

int recv_com_timeout(int sockfd, Packet *buf, double deadline) {
    while (1) {
        double now = get_now();
        if (now >= deadline) return 0; 

        double tempo_restante = deadline - now;
        struct timeval tv;
        tv.tv_sec = (long)(tempo_restante / 1000.0);
        tv.tv_usec = (long)(((long long)tempo_restante % 1000) * 1000);
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sockfd, &read_fds);
        
        int s = select(sockfd + 1, &read_fds, NULL, NULL, &tv);
        if (s < 0) return -1; 
        if (s == 0) return 0; 
        
        ssize_t n = recv(sockfd, buf, sizeof(Packet), 0);
        if (n > 0) return (int) n;
        if (n == 0) return 0;
    }
}

static FILE *log_fp = NULL;
static double log_t0 = 0;
static void log_cwnd(float cwnd) {
    if (!log_fp) return;
    fprintf(log_fp, "%.1f,%.0f\n", get_now() - log_t0, cwnd);
}

int main() {
    log_fp = fopen("cwnd_log.csv", "w");
    if (log_fp) fprintf(log_fp, "tempo_ms,cwnd\n");
    log_t0 = get_now();
    
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(8080); 
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    
    srand(time(NULL));
    float cwnd = MSS; 
    uint32_t ssthresh = INITIAL_SSTHRESH; 
    uint16_t client_isn = rand() % 5000; 
    
    // NOVO: Adicionada a variável para acompanhar a Janela do Recebedor
    uint32_t rwnd = 65535; // Valor inicial seguro

    uint16_t base_seq = 0, next_seq = 0;
    State state = SLOW_START;

    uint32_t retransmissions = 0, rtt_count = 0;
    double total_rtt = 0;

    TransmissionEntry tx_buf[MAX_WINDOW_ARRAY];
    for(int i = 0; i < MAX_WINDOW_ARRAY; i++) tx_buf[i].in_use = 0;

    uint16_t ultimo_ack = 0;
    int dup_ack_count = 0;

    printf("[CLIENT] Iniciando Handshake (ISN: %u)\n", client_isn);
    int connected = 0;
    while (!connected) {
        Packet syn = {0}; syn.num_seq = htons(client_isn); syn.flag_syn = 1;
        sendto(sockfd, &syn, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
        
        Packet res;
        double deadline = get_now() + RTO_MS;
        int n = recv_com_timeout(sockfd, &res, deadline);
        if (n > 0 && res.flag_syn && res.flag_ack) {
            if (ntohs(res.num_ack) == client_isn + 1) {
                uint16_t serv_seq = ntohs(res.num_seq);
                
                // NOVO: Extrai a primeira RWND reportada pelo servidor no Handshake
                rwnd = ntohs(res.buffer_recebimento);

                Packet ack = {0};
                ack.num_ack = htons(serv_seq + 1); ack.flag_ack = 1;
                ack.num_seq = res.num_ack;
                
                sendto(sockfd, &ack, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
                
                next_seq = ntohs(res.num_ack);
                base_seq = next_seq;
                ultimo_ack = next_seq;
                connected = 1;
                printf("[CLIENT] Conectado - Iniciando transferencia (RWND Inicial: %u)\n", rwnd);
            }
        }
    }

    uint32_t total_to_send = 50 * MSS; 
    uint32_t confirmed = 0;
    while (confirmed < total_to_send) {
        
        uint32_t in_flight = 0;
        for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
            if (tx_buf[i].in_use) in_flight += MSS;
             
        // NOVO: Cálculo da Janela Efetiva (Flow Control + Congestion Control)
        // A janela de envio é o menor valor entre a rede (cwnd) e o recebedor (rwnd)
        uint32_t effective_window = ((uint32_t)cwnd < rwnd) ? (uint32_t)cwnd : rwnd;

        int sent_this_round = 0;
        
        // NOVO: Substituído `(uint32_t)cwnd` por `effective_window` no laço
        while (in_flight + MSS <= effective_window && (confirmed + in_flight) < total_to_send) {
            int slot_livre = -1;
            for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                if (!tx_buf[i].in_use) {
                    slot_livre = i; 
                    break;
                }
            }

            if (slot_livre == -1) break;

            Packet p = {0};
            p.num_seq = htons(next_seq);
            p.bytes_enviados = htons(MSS);

            tx_buf[slot_livre].pkt     = p;
            tx_buf[slot_livre].seq     = next_seq;
            tx_buf[slot_livre].sent_at = get_now();
            tx_buf[slot_livre].in_use  = 1;

            sendto(sockfd, &p, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
            
            // NOVO: Incluído o valor da RWND e Janela Efetiva nos prints
            printf("[SEND] Seq: %u | CWND: %.0f | RWND: %u | EfctWindow: %u | Estado: %s\n",
                   next_seq, cwnd, rwnd, effective_window,
                   state == SLOW_START          ? "SLOW_START" :
                   state == CONGESTION_AVOIDANCE ? "CONG_AVOID" : "FAST_RECOV");

            log_cwnd(cwnd);
            next_seq  += MSS;
            in_flight += MSS;
            sent_this_round++;
        }

        if (sent_this_round == 0 && in_flight == 0) {
            printf("Fim do envio de pacotes.\n");
            break;
        }

        double oldest_sent = get_now();
        for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
            if (tx_buf[i].in_use && tx_buf[i].sent_at < oldest_sent)
                oldest_sent = tx_buf[i].sent_at;
        
        double deadline = oldest_sent + RTO_MS;

        Packet res;
        int n = recv_com_timeout(sockfd, &res, deadline);

        if (n == 0) {
            printf("[TIMEOUT] Perda detectada - retransmitindo a partir de seq base\n");
            retransmissions++;

            ssthresh = (uint32_t)(cwnd / 2);
            if (ssthresh < MSS) ssthresh = MSS;
            cwnd = MSS;
            state = SLOW_START;
            dup_ack_count = 0;

            log_cwnd(cwnd);

            next_seq = base_seq + confirmed;

            for (int i = 0; i < MAX_WINDOW_ARRAY; i++)
                tx_buf[i].in_use = 0;
        }
        else if (n > 0) {
            uint16_t ack_val = ntohs(res.num_ack);
            
            // NOVO: Atualiza a Janela de Recepção (Flow Control) constantemente
            rwnd = ntohs(res.buffer_recebimento);

            if (ack_val == ultimo_ack) {
                dup_ack_count++;
                printf("[DUP-ACK #%d] ACK: %u (RWND: %u)\n", dup_ack_count, ack_val, rwnd);

                if (dup_ack_count == 3)  {
                    printf("[FAST-RETRANSMIT] 3 ACKs duplicados - retransmitindo seq: %u\n", ack_val);
                    retransmissions++;

                    ssthresh = (uint32_t)(cwnd / 2);
                    if (ssthresh < MSS) ssthresh = MSS;
                    cwnd  = (float)ssthresh + 3 * MSS;
                    state = FAST_RECOVERY;

                    log_cwnd(cwnd);

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
                    cwnd += MSS;
                    log_cwnd(cwnd);
                }
            }
        
            else if (ack_val > ultimo_ack) {
               printf("[ACK] Recebido: %u (RWND: %u)\n", ack_val, rwnd);
               
               for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                if (tx_buf[i].in_use && (uint16_t)(tx_buf[i].seq + MSS) == ack_val) {
                    double rtt = get_now() - tx_buf[i].sent_at;
                    total_rtt += rtt;
                    rtt_count++;
                    break;
                }
               }

                for (int i = 0; i < MAX_WINDOW_ARRAY; i++) {
                    if (tx_buf[i].in_use && (uint16_t)(tx_buf[i].seq + MSS) <= ack_val) {
                        tx_buf[i].in_use = 0;
                    }
                }

                uint32_t diff = (uint16_t)(ack_val - ultimo_ack);
                confirmed += diff;

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

                log_cwnd(cwnd);
                ultimo_ack = ack_val;
                dup_ack_count = 0;
            }
        }
    }

    Packet fin = {0}; fin.flag_fin = 1;
    sendto(sockfd, &fin, sizeof(Packet), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

    printf("\nTotal Enviado: %u bytes\n", confirmed);
    printf("Retransmissoes: %u\n", retransmissions);
    printf("RTT Medio: %.2f ms\n", (rtt_count > 0) ? total_rtt / rtt_count : 0);

    if (log_fp) fclose(log_fp);
    close(sockfd); 
    return 0;
}