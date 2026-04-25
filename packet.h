#include <stdint.h>

#define MSS 1024        // Maximum Segment Size

#define FLAG_ACK 0x01  // 00000001
#define FLAG_SYN 0x02  // 00000010
#define FLAG_FIN 0x04  // 00000100
// Como ativar o ACK por exemplo: packet.flags |= FLAG_ACK;

#pragma pack(push, 1)
typedef struct {
    uint16_t num_seq;                   // número de sequência
    uint16_t num_ack;                   // número de ACK (reconhecimento)
    uint16_t buffer_recebimento;        // não usado (mas reservado)    
    uint16_t bytes_enviados;            // quantidade de bytes de dados  
    uint8_t flags;                      // bits: ACK=1, SYN=2, FIN=4
    
    char data[MSS];  // payload
} Packet;
#pragma pack(pop)
