/*
 * sched_edf.c - Earliest Deadline First preemptivo (capítulo 2, seção 2.4.2).
 *
 * Prioridade dinâmica: executa quem tem o deadline ABSOLUTO (chegada da
 * ativação atual + prazo) mais próximo. Como o deadline muda a cada
 * ativação, a prioridade de uma tarefa muda ao longo do tempo.
 *
 * Diferença para o RM, em uma frase: no RM a "importância" da tarefa é
 * sempre a mesma (o período); no EDF ela depende de quão perto do prazo a
 * ativação atual está. Por isso, em casos como a Figura 2.6 do capítulo, o
 * EDF cumpre todos os prazos e o RM perde um.
 *
 * `deadline_abs` é calculado em sim.c (iniciar_ativacao) quando a ativação
 * começa; aqui só o devolvemos como prioridade.
 */
#include "sched.h"

static int prioridade_edf(const TCB *k, int agora) {
    (void)agora;
    return k->deadline_abs;
}

/* Registro do algoritmo (ver sched_rm.c). */
const Escalonador escalonador_edf = {
    "EDF",
    "Earliest Deadline First: prioridade dinâmica, deadline mais próximo primeiro",
    prioridade_edf,
};
