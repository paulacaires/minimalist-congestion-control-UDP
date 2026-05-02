#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

#define MSS 1024
#define RTO_MS 500
#define INITIAL_SSTHRESH 15360

#pragma pack(push, 1)
typedef struct {
    uint16_t num_seq;            // 2 bytes
    uint16_t num_ack;            // 2 bytes
    uint16_t buffer_recebimento; // 2 bytes
    
    // 2 bytes (13 bits + 1 + 1 + 1)
    uint16_t bytes_enviados : 13; 
    uint16_t flag_fin       : 1;
    uint16_t flag_syn       : 1;
    uint16_t flag_ack       : 1;

    char data[MSS]; 
} Packet;
#pragma pack(pop)

#endif