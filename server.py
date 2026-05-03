import socket
import random
from packet_utils import *

def run_server():
    sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sockfd.bind(('0.0.0.0', 8080))
    
    expected_seq = 0
    total_bytes = 0
    packets_lost = 0
    
    print("[SERVER] Aguardando conexao na porta 8080")
    
    # Handshake
    while True:
        data, cliaddr = sockfd.recvfrom(2048)
        pkt = unpack_packet(data)
        
        if pkt['flag_syn']:
            client_seq = pkt['num_seq']
            server_isn = random.randint(0, 4999)
            
            # Envia SYN-ACK
            sa = pack_packet(server_isn, client_seq + 1, 0, 0, 0, 1, 1)
            sockfd.sendto(sa, cliaddr)
            
            # Aguarda ACK final do handshake
            data, _ = sockfd.recvfrom(2048)
            pkt = unpack_packet(data)
            if pkt['flag_ack'] and pkt['num_ack'] == (server_isn + 1):
                expected_seq = client_seq + 1
                print("[HANDSHAKE] Conexao estabelecida")
                break

    # Loop de Dados
    while True:
        try:
            data, cliaddr = sockfd.recvfrom(2048)
            pkt = unpack_packet(data)
        except: continue

        if pkt['flag_fin']:
            print("[RECV] FIN recebido - Encerrando")
            fa = pack_packet(0, 0, 0, 0, 1, 0, 1)
            sockfd.sendto(fa, cliaddr)
            break
            
        cur_seq = pkt['num_seq']
        b_recv = pkt['bytes_enviados']

        # Simulação de perda (10%)
        if random.random() < 0.1:
            print(f"[LOSS] Pacote Seq {cur_seq} ignorado")
            packets_lost += 1
            continue

        if cur_seq == expected_seq:
            expected_seq += b_recv
            total_bytes += b_recv
            print(f"[DATA] Seq {cur_seq} recebido - Total: {total_bytes} bytes")
            
        # Envia ACK cumulativo
        ack_p = pack_packet(0, expected_seq, 0, 0, 0, 0, 1)
        sockfd.sendto(ack_p, cliaddr)

    print(f"\nTotal Recebido: {total_bytes} bytes")
    print(f"Perdas Simuladas: {packets_lost}")
    sockfd.close()

if __name__ == "__main__":
    run_server()