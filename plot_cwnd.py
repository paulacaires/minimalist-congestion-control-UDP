#!/usr/bin/env python3
"""
Plota CWND vs tempo a partir do arquivo cwnd_log.csv gerado pelo cliente.
Uso: python3 plot_cwnd.py
"""

import csv
import sys
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

def main():
    times, cwnds = [], []
    try:
        with open("cwnd_log.csv") as f:
            reader = csv.DictReader(f)
            for row in reader:
                times.append(float(row["tempo_ms"]))
                cwnds.append(float(row["cwnd"]) / 1024)   # em KB
    except FileNotFoundError:
        print("Arquivo cwnd_log.csv não encontrado. Execute o cliente primeiro.")
        sys.exit(1)

    fig, ax = plt.subplots(figsize=(12, 5))
    ax.step(times, cwnds, where="post", color="#2563eb", linewidth=1.8, label="CWND")
    ax.fill_between(times, cwnds, step="post", alpha=0.15, color="#2563eb")

    ax.set_xlabel("Tempo (ms)", fontsize=12)
    ax.set_ylabel("CWND (KB)", fontsize=12)
    ax.set_title("Janela de Congestionamento (CWND) × Tempo", fontsize=14, fontweight="bold")
    ax.grid(True, linestyle="--", alpha=0.4)
    ax.legend(fontsize=11)

    plt.tight_layout()
    out = "cwnd_plot.png"
    plt.savefig(out, dpi=150)
    print(f"Gráfico salvo em {out}")
    plt.show()

if __name__ == "__main__":
    main()
