/*
 * sched_rm.c - Rate Monotonic preemptivo (capítulo 2, seção 2.4.1).
 *
 * Prioridade fixa: quanto MENOR o período, MAIOR a prioridade. Não depende
 * do tempo nem da ativação atual, por isso `agora` não é usado.
 */
#include "sched.h"

static int prioridade_rm(const TCB *k, int agora) {
    (void)agora;
    return k->periodo;
}

const Escalonador escalonador_rm = {
    "RM",
    "Rate Monotonic: prioridade fixa, menor período = maior prioridade",
    prioridade_rm,
};
