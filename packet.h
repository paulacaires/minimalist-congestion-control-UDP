#ifndef PACKET_H
#define PACKET_H

#include <stdint.h> // uint16_t uint32_t

#define MSS 1024
#define RTO_MS 500
#define INITIAL_SSTHRESH 15360
#define MAX_WINDOW_ARRAY 64
#define RECV_BUFFER_SIZE 64

#pragma pack(push, 1)
typedef struct {
    uint16_t num_seq;
    uint16_t num_ack;
    uint16_t buffer_recebimento;
    
    uint16_t bytes_enviados : 13; 
    uint16_t flag_fin       : 1;
    uint16_t flag_syn       : 1;
    uint16_t flag_ack       : 1;

    char data[MSS]; 
} Packet;
#pragma pack(pop)

#endif