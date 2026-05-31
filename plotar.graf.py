#!/usr/bin/env python3
import os
import pandas as pd
import matplotlib.pyplot as plt

# Tamanho do pacote em bytes
MSS = 1024
ARQUIVO_LOG = "cwnd_log.csv"

def plotar_execucao_unica():
    # Verifica se o arquivo de log existe na pasta atual
    if not os.path.exists(ARQUIVO_LOG):
        print(f"[ERRO] O arquivo '{ARQUIVO_LOG}' não foi encontrado.")
        print("Certifique-se de ter executado o ./cliente primeiro!")
        return

    # Lê os dados gerados pelo cliente em C
    df = pd.read_csv(ARQUIVO_LOG)
    
    # Extrai as colunas e converte os valores de Bytes para pacotes (MSS)
    t = df["tempo_ms"]
    cwnd_mss = df["cwnd"] / MSS
    rwnd_mss = df["rwnd"] / MSS
    eff_mss  = df["janela_efetiva"] / MSS

    # Cria a figura para o gráfico
    fig, ax = plt.subplots(figsize=(12, 6))
    fig.suptitle("Evolução das Janelas de Transmissão (Execução Única)", fontsize=14, fontweight="bold")
    ax.set_title("CWND = Controle de Congestionamento | rwnd = Controle de Fluxo", fontsize=11, color="gray")

    # Desenha as linhas do gráfico
    ax.fill_between(t, eff_mss, alpha=0.15, color="#2ecc71") # Preenchimento verde clarinho
    ax.plot(t, cwnd_mss, color="#2ecc71", lw=2.5, label="CWND (Rede)")
    ax.plot(t, rwnd_mss, color="#3498db", lw=1.8, linestyle="--", alpha=0.9, label="rwnd (Buffer do Servidor)")
    ax.plot(t, eff_mss,  color="#2c3e50", lw=1.5, linestyle=":", label="Janela Efetiva (min(cwnd, rwnd))")

    # Marca as quedas da CWND com linhas verticais vermelhas (Timeouts/Fast Retransmit)
    quedas = df["cwnd"].diff()
    for idx in df.index[quedas < -MSS]:
        ax.axvline(x=t[idx], color="#e74c3c", alpha=0.3, lw=1.2)

    # Configurações de eixos, grade e legenda
    ax.set_xlabel("Tempo (ms)", fontsize=12)
    ax.set_ylabel("Tamanho da Janela (MSS = 1024 B)", fontsize=12)
    ax.legend(fontsize=11, loc="upper right")
    ax.grid(True, alpha=0.3, linestyle="--")
    ax.set_ylim(bottom=0)

    # Salva a imagem na mesma pasta
    nome_saida = "grafico_execucao_unica.png"
    plt.tight_layout()
    plt.savefig(nome_saida, dpi=150, bbox_inches="tight")
    plt.close()
    
    print(f"[SUCESSO] Gráfico gerado e salvo como: {nome_saida}")

if __name__ == "__main__":
    plotar_execucao_unica()
