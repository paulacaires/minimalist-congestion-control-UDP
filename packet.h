#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <arpa/inet.h>
#include <stdio.h>

#define MSS             1024        // Maximum Segment Size
#define HEADER_SIZE     9           // Tamanho do header em bytes

#define FLAG_ACK 0x01  // 00000001
#define FLAG_SYN 0x02  // 00000010
#define FLAG_FIN 0x04  // 00000100
// Como ativar o ACK por exemplo: packet.flags |= FLAG_ACK;

#pragma pack(push, 1)
typedef struct packet {
    uint16_t num_seq;                   // número de sequência
    uint16_t num_ack;                   // número de ACK (reconhecimento)
    uint16_t buffer_recebimento;        // não usado (mas reservado)    
    uint16_t bytes_enviados;            // quantidade de bytes de dados  
    uint8_t flags;                      // bits: ACK=1, SYN=2, FIN=4
    
    char data[MSS];                     // payload
} Packet;
#pragma pack(pop)

static void header_network_friendly(Packet *p) {
    p->num_seq = htons(p->num_seq);
    p->num_ack = htons(p->num_ack);
    p->buffer_recebimento = htons(p->buffer_recebimento);
    p->bytes_enviados = htons(p->bytes_enviados);
}

static void header_human_friendly(Packet *p) {
    p->num_seq = ntohs(p->num_seq);
    p->num_ack = ntohs(p->num_ack);
    p->buffer_recebimento = ntohs(p->buffer_recebimento);
    p->bytes_enviados     = ntohs(p->bytes_enviados);
}

static void print_packet(Packet *p) {
    printf("===== PACKET =====\n");

    printf("Número sequência: %u\n", ntohs(p->num_seq));
    printf("Número do ACK: %u\n", ntohs(p->num_ack));
    printf("Buffer Recebimento: %u\n", ntohs(p->buffer_recebimento));
    printf("Bytes Enviados: %u\n", ntohs(p->bytes_enviados));

    printf("Flags: ");

    if (p->flags & FLAG_ACK) printf("ACK ");
    if (p->flags & FLAG_SYN) printf("SYN ");
    if (p->flags & FLAG_FIN) printf("FIN ");
    if (p->flags == 0) printf("NONE");

    printf("\n");

    printf("===================\n");
}

#endif /* PACKET_H */