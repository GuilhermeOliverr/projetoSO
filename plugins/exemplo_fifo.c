/*
 * exemplo_fifo.c - exemplo de escalonador carregado em tempo de execução
 * (req. 4.2: o escalonador pode estar numa biblioteca dinâmica fora do
 * executável).
 *
 * Política: FIFO preemptivo pela chegada da ativação atual (quem chegou
 * antes tem prioridade). Serve só para mostrar o mecanismo.
 *
 * Compilar:  make plugins
 * Usar:      ./projetoSO arquivo.txt --plugin plugins/exemplo_fifo.so
 *            (e no arquivo: FIFO;quantum;cpus)
 */
#include "sched.h"

static int prioridade_fifo(const TCB *a, const TCB *b, int agora) {
    (void)agora;
    if (a->chegada_atual != b->chegada_atual) return a->chegada_atual < b->chegada_atual ? -1 : 1;
    return 0;
}

/* O simulador procura exatamente este símbolo (SCHED_SIMBOLO_PLUGIN). */
const Escalonador escalonador_plugin = {
    "FIFO",
    "FIFO por chegada da ativação (plugin de exemplo)",
    prioridade_fifo,
};
