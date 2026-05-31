#!/usr/bin/env python3
"""
plotar_resultados.py
Lê os CSVs gerados por testar_perdas.sh e produz os gráficos para o relatório.

Uso:
    python3 plotar_resultados.py

Saída:
    fig1_cwnd_por_cenario.png  — CWND + rwnd por cenário (2×2)
    fig2_cwnd_comparado.png    — CWND dos 4 cenários sobrepostos
    fig3_metricas.png          — Throughput, retransmissões, tempo total
"""

import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

MSS = 1024
DIR = "resultados_perda"

# ── Configuração dos cenários ─────────────────────────────────────────────────
CENARIOS = [
    # (arquivo_csv,           label_eixo,  cor,        titulo_subplot)
    ("cwnd_sem_perda_tc.csv",   "0% tc\n(~10% real)",  "#2ecc71", "Sem perda extra (tc=0%)"),
    ("cwnd_perda_5pct_tc.csv",  "5% tc\n(~15% real)",  "#f39c12", "Perda leve (tc=5%)"),
    ("cwnd_perda_15pct_tc.csv", "15% tc\n(~25% real)", "#e67e22", "Perda moderada (tc=15%)"),
    ("cwnd_perda_30pct_tc.csv", "30% tc\n(~40% real)", "#e74c3c", "Perda severa (tc=30%)"),
]

def carregar_csv(nome):
    """Carrega cwnd_log.csv gerado pelo cliente."""
    path = os.path.join(DIR, nome)
    if not os.path.exists(path):
        print(f"[AVISO] {path} não encontrado — pulando.")
        return None
    df = pd.read_csv(path)
    # Normaliza colunas (versão antiga só tinha cwnd)
    if "rwnd" not in df.columns:
        df["rwnd"] = 64 * MSS
    if "janela_efetiva" not in df.columns:
        df["janela_efetiva"] = df[["cwnd","rwnd"]].min(axis=1)
    return df


# ── Figura 1: 2×2 por cenário ────────────────────────────────────────────────
fig, axes = plt.subplots(2, 2, figsize=(14, 9))
fig.suptitle(
    "CWND e Janela Efetiva vs Tempo — por Condição de Perda\n"
    "(CWND = controle de congestionamento | rwnd = controle de fluxo | "
    "janela efetiva = min(cwnd, rwnd))",
    fontsize=12, fontweight="bold", y=1.01
)

for ax, (csv, label_eixo, cor, titulo) in zip(axes.flat, CENARIOS):
    df = carregar_csv(csv)
    if df is None:
        ax.text(0.5, 0.5, f"Sem dados\n({csv})", ha="center", va="center",
                transform=ax.transAxes, fontsize=11, color="gray")
        ax.set_title(titulo, fontweight="bold")
        continue

    t = df["tempo_ms"]
    cwnd_mss = df["cwnd"] / MSS
    rwnd_mss = df["rwnd"] / MSS
    eff_mss  = df["janela_efetiva"] / MSS

    ax.fill_between(t, eff_mss, alpha=0.12, color=cor)
    ax.plot(t, cwnd_mss, color=cor,       lw=2.0,  label="CWND")
    ax.plot(t, rwnd_mss, color="#3498db", lw=1.3,
            linestyle="--", alpha=0.8,               label="rwnd")
    ax.plot(t, eff_mss,  color="#2c3e50", lw=1.0,
            linestyle=":",                           label="min(cwnd,rwnd)")

    # Marca quedas bruscas de cwnd (eventos de congestionamento)
    quedas = df["cwnd"].diff()
    for idx in df.index[quedas < -MSS]:
        ax.axvline(x=t[idx], color="red", alpha=0.25, lw=0.9)

    # Linha de ssthresh inicial
    ax.axhline(y=15360/MSS, color="gray", lw=0.7, linestyle="--", alpha=0.5)
    ax.text(t.iloc[-1]*0.02, 15360/MSS + 0.3, "ssthresh₀", fontsize=7, color="gray")

    ax.set_title(titulo, fontweight="bold", fontsize=11)
    ax.set_xlabel("Tempo (ms)")
    ax.set_ylabel("Janela (MSS = 1024 B)")
    ax.legend(fontsize=8, loc="upper left")
    ax.grid(True, alpha=0.25)
    ax.set_ylim(bottom=0)

plt.tight_layout()
plt.savefig("fig1_cwnd_por_cenario.png", dpi=150, bbox_inches="tight")
plt.close()
print("[OK] fig1_cwnd_por_cenario.png")


# ── Figura 2: CWND dos 4 cenários sobrepostos ────────────────────────────────
fig, ax = plt.subplots(figsize=(13, 6))
ax.set_title(
    "Comparação de CWND nos Quatro Cenários de Perda",
    fontsize=13, fontweight="bold"
)

for csv, label_eixo, cor, titulo in CENARIOS:
    df = carregar_csv(csv)
    if df is None:
        continue
    ax.plot(df["tempo_ms"], df["cwnd"]/MSS, color=cor, lw=2.0, label=titulo)

ax.set_xlabel("Tempo (ms)", fontsize=11)
ax.set_ylabel("CWND (em MSS)", fontsize=11)
ax.legend(fontsize=10)
ax.grid(True, alpha=0.25)
ax.set_ylim(bottom=0)
plt.tight_layout()
plt.savefig("fig2_cwnd_comparado.png", dpi=150, bbox_inches="tight")
plt.close()
print("[OK] fig2_cwnd_comparado.png")


# ── Figura 3: Métricas do resumo.csv ─────────────────────────────────────────
resumo_path = os.path.join(DIR, "resumo.csv")
if not os.path.exists(resumo_path):
    print(f"[AVISO] {resumo_path} não encontrado — pulando Fig 3.")
    sys.exit(0)

res = pd.read_csv(resumo_path)
# Calcula throughput em KB/s
res["throughput_kbs"] = (res["bytes_enviados"] / (res["tempo_ms"] / 1000)) / 1024

labels = [f"{r}% tc" for r in res["taxa_perda_pct"]]
cores  = ["#2ecc71", "#f39c12", "#e67e22", "#e74c3c"][:len(res)]

fig, axes = plt.subplots(1, 3, figsize=(15, 5))
fig.suptitle("Métricas de Desempenho por Condição de Perda", fontsize=13, fontweight="bold")

# — Throughput —
ax = axes[0]
bars = ax.bar(labels, res["throughput_kbs"], color=cores, edgecolor="white", lw=1.2)
ax.set_title("Throughput médio", fontweight="bold")
ax.set_ylabel("KB/s")
ax.set_xlabel("Taxa de perda (tc)")
for bar, val in zip(bars, res["throughput_kbs"]):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
            f"{val:.0f}", ha="center", fontsize=10, fontweight="bold")
ax.grid(axis="y", alpha=0.3)

# — Retransmissões por tipo —
ax = axes[1]
x  = np.arange(len(labels))
w  = 0.35
ax.bar(x - w/2, res["timeouts"],          w, label="Timeouts",       color="#e74c3c", alpha=0.85)
ax.bar(x + w/2, res["retransmissoes"] - res["timeouts"], w,
       label="Fast Retransmit", color="#f39c12", alpha=0.85)
ax.set_title("Tipo de retransmissão", fontweight="bold")
ax.set_ylabel("Quantidade")
ax.set_xlabel("Taxa de perda (tc)")
ax.set_xticks(x)
ax.set_xticklabels(labels)
ax.legend(fontsize=9)
ax.grid(axis="y", alpha=0.3)

# — Tempo total —
ax = axes[2]
bars = ax.bar(labels, res["tempo_ms"]/1000, color=cores, edgecolor="white", lw=1.2)
ax.set_title("Tempo total de transferência", fontweight="bold")
ax.set_ylabel("Segundos")
ax.set_xlabel("Taxa de perda (tc)")
for bar, val in zip(bars, res["tempo_ms"]/1000):
    ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.02,
            f"{val:.1f}s", ha="center", fontsize=10, fontweight="bold")
ax.grid(axis="y", alpha=0.3)

plt.tight_layout()
plt.savefig("fig3_metricas.png", dpi=150, bbox_inches="tight")
plt.close()
print("[OK] fig3_metricas.png")

print("\nPronto! Importe os .png no seu relatório.")