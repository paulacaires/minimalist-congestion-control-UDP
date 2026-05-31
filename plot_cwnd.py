import csv
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np

# --- Leitura do CSV ---
times, cwnds = [], []
with open("cwnd_log.csv") as f:
    reader = csv.DictReader(f)
    for row in reader:
        times.append(float(row["tempo_ms"]))
        cwnds.append(float(row["cwnd"]) / 1024)  # bytes → KB

# --- Detecta quedas (timeouts / fast retransmit) ---
drop_x, drop_y = [], []
for i in range(1, len(cwnds)):
    if cwnds[i] < cwnds[i - 1] * 0.6:  # queda de mais de 40%
        drop_x.append(times[i])
        drop_y.append(cwnds[i])

# --- Figura ---
fig, ax = plt.subplots(figsize=(13, 5))
fig.patch.set_facecolor("#0f1117")
ax.set_facecolor("#0f1117")

# Área preenchida
ax.fill_between(times, cwnds, step="post", alpha=0.18, color="#4f9cf9")

# Linha principal
ax.step(times, cwnds, where="post", color="#4f9cf9", linewidth=2, label="CWND")

# Marcadores de queda
ax.scatter(drop_x, drop_y, color="#f87171", zorder=5, s=60, label="Queda (timeout / FR)")

# --- Estilo dos eixos ---
for spine in ax.spines.values():
    spine.set_edgecolor("#2a2d3a")

ax.tick_params(colors="#9ca3af", labelsize=10)
ax.xaxis.label.set_color("#9ca3af")
ax.yaxis.label.set_color("#9ca3af")

ax.set_xlabel("Tempo (ms)", fontsize=12, labelpad=10)
ax.set_ylabel("CWND (KB)", fontsize=12, labelpad=10)
ax.set_title("Janela de Congestionamento (CWND) × Tempo", fontsize=14,
             fontweight="bold", color="#e5e7eb", pad=16)

ax.grid(True, linestyle="--", linewidth=0.5, alpha=0.25, color="#4b5563")
ax.set_xscale("symlog", linthresh=10)  # linear até 10ms, log depois

# --- Legenda ---
legend = ax.legend(fontsize=10, facecolor="#1a1d27", edgecolor="#2a2d3a",
                   labelcolor="#e5e7eb", loc="upper right")

plt.tight_layout()
plt.savefig("cwnd_plot.png", dpi=160, facecolor=fig.get_facecolor())
print("Salvo em cwnd_plot.png")
plt.show()