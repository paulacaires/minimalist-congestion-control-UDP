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
    
    # --- HANDSHAKE ---
    while True:
        data, cliaddr = sockfd.recvfrom(2048)
        h, _ = separate_packet(data)
        seq, ack, dlen, f_fin, f_syn, f_ack = decode_header(h)
        
        if f_syn:
            server_isn = random.randint(0, 4999)
            print(f"[HANDSHAKE] SYN recebido (Seq: {seq}). Enviando SYN-ACK...")
            h_res = create_header(server_isn, seq + 1, 0, False, True, True)
            sockfd.sendto(h_res, cliaddr)
            
            data, _ = sockfd.recvfrom(2048)
            h, _ = separate_packet(data)
            _, a, _, _, _, f_a = decode_header(h)
            if f_a and a == (server_isn + 1):
                expected_seq = seq + 1
                print("[HANDSHAKE] Conexao estabelecida")
                break

    # --- LOOP DE DADOS ---
    while True:
        try:
            raw_packet, cliaddr = sockfd.recvfrom(2048)
            h, payload = separate_packet(raw_packet)
            seq, ack, dlen, f_fin, f_syn, f_ack = decode_header(h)
        except: continue

        if f_fin:
            print("[RECV] FIN recebido - Encerrando servidor")
            h_fin = create_header(0, 0, 0, True, False, True)
            sockfd.sendto(h_fin, cliaddr)
            break
            
        if random.random() < 0.1: # Perda de 10%
            print(f"[LOSS] Pacote Seq {seq} ignorado")
            packets_lost += 1
            continue

        if seq == expected_seq:
            expected_seq += dlen
            total_bytes += dlen
            print(f"[DATA] Seq {seq} recebido ({dlen} bytes) - Total: {total_bytes}")
            
        h_ack = create_header(0, expected_seq, 0, False, False, True)
        sockfd.sendto(h_ack, cliaddr)

    print(f"\n--- ESTATISTICAS DO SERVIDOR ---")
    print(f"Total Recebido: {total_bytes} bytes")
    print(f"Pacotes Perdidos (Simulados): {packets_lost}")
    sockfd.close()

if __name__ == "__main__":
    run_server()