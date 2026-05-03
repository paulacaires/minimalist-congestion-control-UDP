import struct

# Constantes de Protocolo
MSS = 1024
RTO_S = 0.5
INITIAL_SSTHRESH = 15360
MAX_WINDOW_ARRAY = 1024

def create_header(seq, ack_num, data_len, f_fin, f_syn, f_ack):
    # 13 bits (data_len) + 1 bit (fin) + 1 bit (syn) + 1 bit (ack) = 16 bits
    flags_field = (data_len << 3) | (int(f_fin) << 2) | (int(f_syn) << 1) | int(f_ack)
    return struct.pack('!HHHH', seq, ack_num, 0, flags_field)

def decode_header(header_bytes):
    seq, ack_num, _, flags_field = struct.unpack('!HHHH', header_bytes)
    data_len = flags_field >> 3
    f_fin = bool((flags_field >> 2) & 0x1)
    f_syn = bool((flags_field >> 1) & 0x1)
    f_ack = bool(flags_field & 0x1)
    return seq, ack_num, data_len, f_fin, f_syn, f_ack

def separate_packet(raw_data):
    return raw_data[:8], raw_data[8:]