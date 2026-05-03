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
    
    cwnd = float(MSS)
    ssthresh = INITIAL_SSTHRESH
    client_isn = random.randint(0, 4999)
    state = "SLOW_START"
    
    retransmissions = 0
    rtt_count = 0
    total_rtt = 0
    
    # Handshake
    print(f"[CLIENT] Iniciando Handshake (ISN: {client_isn})")
    connected = False
    while not connected:
        syn = pack_packet(client_isn, 0, 0, 0, 0, 1, 0)
        sockfd.sendto(syn, servaddr)
        
        try:
            res_data, _ = sockfd.recvfrom(2048)
            res = unpack_packet(res_data)
            if res['flag_syn'] and res['flag_ack'] and res['num_ack'] == client_isn + 1:
                serv_seq = res['num_seq']
                ack = pack_packet(0, serv_seq + 1, 0, 0, 0, 0, 1)
                sockfd.sendto(ack, servaddr)
                
                base_seq = client_isn + 1
                next_seq = base_seq
                connected = True
                print("[CLIENT] Conectado - Iniciando transferencia")
        except socket.timeout:
            continue

    total_to_send = 50 * MSS
    confirmed = 0

    while confirmed < total_to_send:
        in_flight = 0
        burst_size = 0
        window_seqs = []
        window_times = []

        # Envio da Janela
        while in_flight + MSS <= cwnd and (confirmed + in_flight) < total_to_send and burst_size < MAX_WINDOW_ARRAY:
            p = pack_packet(next_seq, 0, 0, MSS, 0, 0, 0)
            window_seqs.append(next_seq)
            window_times.append(get_now())
            
            sockfd.sendto(p, servaddr)
            print(f"[SEND] Seq: {next_seq} | Janela (CWND): {cwnd:.0f}")
            
            next_seq += MSS
            in_flight += MSS
            burst_size += 1

        # Recebimento de ACKs
        timeout_occurred = False
        for i in range(burst_size):
            try:
                res_data, _ = sockfd.recvfrom(2048)
                res = unpack_packet(res_data)
                ack_val = res['num_ack']
                
                if ack_val > (base_seq + confirmed):
                    print(f"[ACK] Recebido: {ack_val}")
                    rtt = get_now() - window_times[i]
                    total_rtt += rtt
                    rtt_count += 1
                    
                    diff = ack_val - (base_seq + confirmed)
                    confirmed += diff
                    
                    if state == "SLOW_START":
                        cwnd += MSS
                        if cwnd >= ssthresh: state = "CONGESTION_AVOIDANCE"
                    else:
                        cwnd += (MSS * MSS) / cwnd
            except socket.timeout:
                timeout_occurred = True
                break

        if timeout_occurred:
            print(f"[TIMEOUT] Perda detectada - Retransmitindo em Seq: {base_seq + confirmed}")
            retransmissions += 1
            ssthresh = max(int(cwnd / 2), MSS)
            cwnd = float(MSS)
            state = "SLOW_START"
            next_seq = base_seq + confirmed

    # FIN
    fin = pack_packet(0, 0, 0, 0, 1, 0, 0)
    sockfd.sendto(fin, servaddr)

    print(f"\nTotal Enviado: {confirmed} bytes")
    print(f"Retransmissoes: {retransmissions}")
    print(f"RTT Medio: {(total_rtt/rtt_count if rtt_count > 0 else 0):.2f} ms")
    sockfd.close()

if __name__ == "__main__":
    run_client()