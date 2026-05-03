#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <arpa/inet.h>
#include <stdio.h>

#define MSS         1024
#define HEADER_SIZE 8

#define FLAG_ACK 0x01  // bit 0
#define FLAG_SYN 0x02  // bit 1
#define FLAG_FIN 0x04  // bit 2

#pragma pack(push, 1)
typedef struct packet {
    uint16_t num_seq;
    uint16_t num_ack;
    uint16_t buffer_recebimento;
    uint16_t len_flags;          // 13 bits: tamanho | 3 bits: flags
    char     data[MSS];
} Packet;
#pragma pack(pop)

static inline void set_len_flags(Packet *p, uint16_t len, uint8_t flags) {
    /*
        (len << 3) Abre espaço para guardar as flags nos 3 bits menos significativos
        0x07 = 0000 0111
        "|" é o OR para juntar os dois, ficando assim:
        [ len (13 bits) ][ flags (3 bits) ]

        Exemplo, se len = 0000 0000 0000 0101
                    flags = 3 0000 0011

                Então fica assim:
                    0000 0000 0010 1011

        (flags & 0x07) é uma máscara de bits para pegar somente os 3 menos significativos
    */
    p->len_flags = (len << 3) | (flags & 0x07);
}

static inline uint8_t get_flags(const Packet *p) {
    return (uint8_t)(p->len_flags & 0x07);
}

static inline uint16_t get_len(const Packet *p) {
    return (p->len_flags >> 3) & 0x1FFF;
}

static inline void header_network_friendly(Packet *p) {
    p->num_seq            = htons(p->num_seq);
    p->num_ack            = htons(p->num_ack);
    p->buffer_recebimento = htons(p->buffer_recebimento);
    p->len_flags          = htons(p->len_flags);
}

static inline void header_human_friendly(Packet *p) {
    p->num_seq            = ntohs(p->num_seq);
    p->num_ack            = ntohs(p->num_ack);
    p->buffer_recebimento = ntohs(p->buffer_recebimento);
    p->len_flags          = ntohs(p->len_flags);
}

static inline void print_packet(const char *prefixo, Packet *p) {
    printf("===== PACKET [%s] =====\n", prefixo);
    printf("Numero sequencia : %u\n", p->num_seq);
    printf("Numero do ACK    : %u\n", p->num_ack);
    printf("Tamanho dos dados: %u\n", get_len(p));
    printf("Flags            : ");
    uint8_t flags = get_flags(p);
    if (flags & FLAG_ACK) printf("ACK ");
    if (flags & FLAG_SYN) printf("SYN ");
    if (flags & FLAG_FIN) printf("FIN ");
    if (flags == 0)        printf("NONE");
    printf("\n======================\n");
}

#endif /* PACKET_H */