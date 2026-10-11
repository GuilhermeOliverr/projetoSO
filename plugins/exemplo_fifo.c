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

/* Prioridade nominal = instante de chegada da ativação (menor = antes). */
static int prioridade_fifo(const TCB *k, int agora) {
    (void)agora;
    return k->chegada_atual;
}

/* O simulador procura exatamente este símbolo (SCHED_SIMBOLO_PLUGIN). */
const Escalonador escalonador_plugin = {
    "FIFO",
    "FIFO por chegada da ativação (plugin de exemplo)",
    prioridade_fifo,
};
