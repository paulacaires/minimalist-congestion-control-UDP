#!/bin/bash
# =============================================================================
# testar_perdas.sh
# Simula diferentes condições de perda usando 'tc' (traffic control) e coleta
# métricas do cliente/servidor para o relatório.
#
# Uso: sudo ./testar_perdas.sh
# Requer: gcc, iproute2 (tc), python3 com matplotlib e pandas
# =============================================================================

set -e

# Compilar
echo "[*] Compilando..."
gcc -Wall -O2 -o servidor servidor.c
gcc -Wall -O2 -o cliente  cliente.c
echo "[*] Compilação OK"

INTERFACE="lo"          # loopback (cliente e servidor no mesmo host)
PORTA=8080
RESULTADOS_DIR="resultados_perda"
mkdir -p "$RESULTADOS_DIR"

# Função: aplica perda na interface loopback com tc netem
aplicar_perda() {
    local pct=$1
    # Remove regra anterior se existir
    tc qdisc del dev $INTERFACE root 2>/dev/null || true

    if [ "$pct" = "0" ]; then
        echo "[TC] Sem perda artificial (apenas a simulada no código)"
    else
        # netem: simula perda aleatória uniforme
        tc qdisc add dev $INTERFACE root netem loss ${pct}%
        echo "[TC] Perda de ${pct}% aplicada em $INTERFACE"
    fi
}

remover_perda() {
    tc qdisc del dev $INTERFACE root 2>/dev/null || true
    echo "[TC] Regra de perda removida"
}

# Função: roda um experimento completo
rodar_experimento() {
    local taxa=$1      # ex: "0", "5", "10", "20"
    local label=$2     # ex: "sem_perda", "perda_5pct"

    echo ""
    echo "============================================================"
    echo " EXPERIMENTO: perda ${taxa}% — ${label}"
    echo "============================================================"

    aplicar_perda "$taxa"

    # Modifica temporariamente o servidor.c para desativar a perda interna
    # quando tc já está injetando perda (opcional — comente se quiser acumular)
    # Aqui mantemos a perda interna (rand % 10 < 1) E a do tc somadas.
    # Para isolar, edite servidor.c: mude "rand() % 10 < 1" para "0 < 0"

    # Inicia servidor em background
    ./servidor > "$RESULTADOS_DIR/servidor_${label}.txt" 2>&1 &
    SERVER_PID=$!
    sleep 0.3   # Aguarda o servidor estar pronto

    # Roda cliente e captura saída
    INICIO=$(date +%s%N)   # nanosegundos
    ./cliente > "$RESULTADOS_DIR/cliente_${label}.txt" 2>&1
    FIM=$(date +%s%N)

    TEMPO_MS=$(( (FIM - INICIO) / 1000000 ))

    # Mata servidor (ele fica esperando FIN)
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true

    # Salva o CSV de cwnd gerado pelo cliente
    if [ -f cwnd_log.csv ]; then
        cp cwnd_log.csv "$RESULTADOS_DIR/cwnd_${label}.csv"
    fi

    # Extrai métricas do log do cliente
    RETX=$(grep -c "RETX\|TIMEOUT" "$RESULTADOS_DIR/cliente_${label}.txt" || echo "0")
    TIMEOUTS=$(grep -c "TIMEOUT" "$RESULTADOS_DIR/cliente_${label}.txt" || echo "0")
    RTT=$(grep "RTT Medio" "$RESULTADOS_DIR/cliente_${label}.txt" | awk '{print $(NF-1)}')
    TOTAL=$(grep "Total Enviado" "$RESULTADOS_DIR/cliente_${label}.txt" | awk '{print $(NF-1)}')

    echo "  Tempo total  : ${TEMPO_MS} ms"
    echo "  Total enviado: ${TOTAL}"
    echo "  Retransmissões: ${RETX}"
    echo "  Timeouts     : ${TIMEOUTS}"
    echo "  RTT médio    : ${RTT}"

    # Salva linha de sumário
    echo "${taxa},${TEMPO_MS},${TOTAL},${RETX},${TIMEOUTS},${RTT}" \
        >> "$RESULTADOS_DIR/resumo.csv"

    remover_perda
    sleep 0.5
}

# Cabeçalho do resumo
echo "taxa_perda_pct,tempo_ms,bytes_enviados,retransmissoes,timeouts,rtt_medio_ms" \
    > "$RESULTADOS_DIR/resumo.csv"

# ── Rodar os 4 cenários ───────────────────────────────────────────────────────
# ATENÇÃO: o servidor já simula ~10% de perda internamente (rand() % 10 < 1)
# As perdas do tc são ADICIONAIS. Para isolar apenas o tc, comente a linha
# "if (rand() % 10 < 1)" no servidor.c e recompile antes de rodar.

rodar_experimento "0"  "sem_perda_tc"       # ~10% (só código)
rodar_experimento "5"  "perda_5pct_tc"      # ~10% código + 5% tc ≈ 15% real
rodar_experimento "15" "perda_15pct_tc"     # ~10% código + 15% tc ≈ 25% real
rodar_experimento "30" "perda_30pct_tc"     # ~10% código + 30% tc ≈ 40% real

echo ""
echo "============================================================"
echo " TODOS OS EXPERIMENTOS CONCLUÍDOS"
echo " Resultados em: $RESULTADOS_DIR/"
echo " Rode agora:    python3 plotar_resultados.py"
echo "============================================================"