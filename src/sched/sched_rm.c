/*
 * sched_rm.c - Rate Monotonic preemptivo (capítulo 2, seção 2.4.1).
 *
 * Prioridade fixa: quanto MENOR o período, MAIOR a prioridade. Não depende
 * do tempo nem da ativação atual, por isso `agora` não é usado.
 *
 * Como funciona na prática: uma tarefa que acorda a cada 100 ticks é mais
 * prioritária que uma que acorda a cada 150. Como "menor valor = mais
 * prioritária" (ver sched.h), basta devolver o próprio período.
 *
 * Preemptivo: a cada tick o escalonador reavalia. Se uma tarefa de período
 * menor chega, ela tira a CPU de uma de período maior no mesmo instante.
 */
#include "sched.h"

static int prioridade_rm(const TCB *k, int agora) {
    (void)agora;   /* o cast para void diz ao compilador que ignorar o
                    * parâmetro é intencional (evita warning) */
    return k->periodo;
}

/* Registro do algoritmo: nome usado no arquivo de configuração, descrição
 * mostrada ao usuário e a função de prioridade. sched_registro.c coloca
 * este objeto na tabela de algoritmos disponíveis. */
const Escalonador escalonador_rm = {
    "RM",
    "Rate Monotonic: prioridade fixa, menor período = maior prioridade",
    prioridade_rm,
};
