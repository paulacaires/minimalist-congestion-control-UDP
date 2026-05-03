import socket
import time
import random
from packet_utils import *

def get_now():
    return time.time() * 1000

def run_client():
    sockfd = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sockfd.settimeout(RTO_S)
    servaddr = ('127.0.0.1', 8080)
    
    # Métricas e Estado
    cwnd = float(MSS)
    ssthresh = INITIAL_SSTHRESH
    client_isn = random.randint(0, 4999)
    state = "SLOW_START"
    retransmissions = 0
    rtt_count = 0
    total_rtt = 0
    
    # Handshake
    print(f"[CLIENT] Iniciando Handshake...")
    connected = False
    while not connected:
        h_syn = create_header(client_isn, 0, 0, False, True, False)
        sockfd.sendto(h_syn, servaddr)
        try:
            res, _ = sockfd.recvfrom(2048)
            h, _ = separate_packet(res)
            s, a, d, f_fin, f_syn, f_ack = decode_header(h)
            if f_syn and f_ack and a == client_isn + 1:
                h_ack = create_header(0, s + 1, 0, False, False, True)
                sockfd.sendto(h_ack, servaddr)
                base_seq = client_isn + 1
                next_seq = base_seq
                connected = True
                print("[CLIENT] Conectado")
        except socket.timeout: continue

    total_to_send = 50 * MSS
    confirmed = 0

    # Loop principal de envio
    while confirmed < total_to_send:
        in_flight = 0
        burst_size = 0
        window_times = []

        # Envio da Janela
        while in_flight + MSS <= cwnd and (confirmed + in_flight) < total_to_send:
            payload = b"DADO" * 256 # 1024 bytes
            header = create_header(next_seq, 0, MSS, False, False, False)
            
            sockfd.sendto(header + payload, servaddr)
            window_times.append(get_now())
            
            print(f"[SEND] Seq: {next_seq} | CWND: {cwnd:.0f} | Estado: {state}")
            next_seq += MSS
            in_flight += MSS
            burst_size += 1

        # Recebimento de ACKs
        timeout_occurred = False
        for i in range(burst_size):
            try:
                res, _ = sockfd.recvfrom(2048)
                h, _ = separate_packet(res)
                _, ack_val, _, _, _, _ = decode_header(h)
                
                if ack_val > (base_seq + confirmed):
                    # RTT Metric
                    rtt = get_now() - window_times[i]
                    total_rtt += rtt
                    rtt_count += 1
                    
                    diff = ack_val - (base_seq + confirmed)
                    confirmed += diff
                    
                    # Logica CWND
                    if state == "SLOW_START":
                        cwnd += MSS
                        if cwnd >= ssthresh: state = "CONGESTION_AVOIDANCE"
                    else:
                        # Congestion Avoidance: CWND = CWND + (MSS^2 / CWND)
                        cwnd += (MSS * MSS) / cwnd
            except socket.timeout:
                timeout_occurred = True
                break

        if timeout_occurred:
            print(f"[TIMEOUT] Perda em Seq: {base_seq + confirmed}. Reduzindo janela.")
            retransmissions += 1
            ssthresh = max(int(cwnd / 2), MSS)
            cwnd = float(MSS)
            state = "SLOW_START"
            next_seq = base_seq + confirmed

    # Encerramento (FIN)
    h_fin = create_header(0, 0, 0, True, False, False)
    sockfd.sendto(h_fin, servaddr)

    print(f"\n--- ESTATISTICAS DO CLIENTE ---")
    print(f"Total Confirmado: {confirmed} bytes")
    print(f"Retransmissoes: {retransmissions}")
    print(f"RTT Medio: {(total_rtt/rtt_count if rtt_count > 0 else 0):.2f} ms")
    sockfd.close()

if __name__ == "__main__":
    run_client()