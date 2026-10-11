/*
 * sched_edf.c - Earliest Deadline First preemptivo (capítulo 2, seção 2.4.2).
 *
 * Prioridade dinâmica: executa quem tem o deadline ABSOLUTO (chegada da
 * ativação atual + prazo) mais próximo. Como o deadline muda a cada
 * ativação, a prioridade de uma tarefa muda ao longo do tempo.
 */
#include "sched.h"

static int prioridade_edf(const TCB *k, int agora) {
    (void)agora;
    return k->deadline_abs;
}

const Escalonador escalonador_edf = {
    "EDF",
    "Earliest Deadline First: prioridade dinâmica, deadline mais próximo primeiro",
    prioridade_edf,
};
