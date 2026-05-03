import struct

# Constantes equivalentes ao packet.h
MSS = 1024
RTO_S = 0.5  # 500ms
INITIAL_SSTHRESH = 15360
MAX_WINDOW_ARRAY = 1024

# Formato: ! (network byte order) 
# H (uint16_t seq), H (uint16_t ack), H (uint16_t buffer), H (flags/bytes), 1024s (data)
STRUCT_FORMAT = "!HHHH1024s"

def pack_packet(num_seq, num_ack, buffer_recebimento, bytes_enviados, flag_fin, flag_syn, flag_ack, data=b""):
    # Monta o campo de 16 bits: [bytes_enviados (13 bits)][fin (1)][syn (1)][ack (1)]
    flags_field = (bytes_enviados << 3) | (flag_fin << 2) | (flag_syn << 1) | flag_ack
    
    # Garante que os dados tenham exatamente o tamanho do MSS
    if len(data) < MSS:
        data = data.ljust(MSS, b'\0')
    elif len(data) > MSS:
        data = data[:MSS]
        
    return struct.pack(STRUCT_FORMAT, num_seq, num_ack, buffer_recebimento, flags_field, data)

def unpack_packet(raw_data):
    unpacked = struct.unpack(STRUCT_FORMAT, raw_data)
    num_seq = unpacked[0]
    num_ack = unpacked[1]
    buffer_recebimento = unpacked[2]
    flags_field = unpacked[3]
    data = unpacked[4]
    
    # Extrai bitfields
    flag_ack = flags_field & 0x1
    flag_syn = (flags_field >> 1) & 0x1
    flag_fin = (flags_field >> 2) & 0x1
    bytes_enviados = flags_field >> 3
    
    return {
        "num_seq": num_seq, "num_ack": num_ack, 
        "buffer_recebimento": buffer_recebimento,
        "bytes_enviados": bytes_enviados, "flag_fin": bool(flag_fin),
        "flag_syn": bool(flag_syn), "flag_ack": bool(flag_ack), "data": data
    }