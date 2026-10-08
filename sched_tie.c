/*
 * sched_tie.c - cadeia de desempate comum a todos os algoritmos (req. 4.3).
 *
 * Ordem dos critérios (o primeiro que diferenciar decide):
 *   1. a tarefa que estava executando logo antes do escalonamento (evita
 *      trocar de contexto para a própria tarefa);
 *   2. o prazo: deadline absoluto mais próximo. Usamos o absoluto (e não o
 *      prazo relativo do arquivo) porque é ele que diz quem está mais
 *      "apertado" agora; na 1ª ativação de tarefas com o mesmo ingresso os
 *      dois dão o mesmo resultado;
 *   3. o instante de ingresso: quem chegou antes;
 *   4. a duração: menor primeiro;
 *   5. sorteio: feito em sim.c, que guarda o estado do gerador aleatório
 *      (precisa ser reproduzível ao retroceder/avançar).
 *
 * Quantum: RM e EDF não usam fatia de tempo para decidir prioridade, então
 * o quantum só serve para revezar tarefas EMPATADAS (como um round-robin
 * dentro do mesmo nível de prioridade). O critério 1 vale enquanto a tarefa
 * não esgotou o quantum; esgotado, ela vai para TRÁS das empatadas, que
 * assim ganham a vez. Se ninguém empata com ela, continua executando.
 */
#include "sched.h"

/* 0 = estava executando com quantum sobrando (melhor), 1 = não estava
 * executando, 2 = estava executando mas esgotou o quantum (cede a vez). */
static int posicao_criterio1(const TCB *k, int quantum) {
    if (k->cpu < 0) return 1;
    return k->quantum_usado < quantum ? 0 : 2;
}

int sched_desempate(const TCB *a, const TCB *b, int quantum) {
    /* Critério 1 */
    int ea = posicao_criterio1(a, quantum);
    int eb = posicao_criterio1(b, quantum);
    if (ea != eb) return ea < eb ? -1 : 1;
    /* Critério 2 */
    if (a->deadline_abs != b->deadline_abs) return a->deadline_abs < b->deadline_abs ? -1 : 1;
    /* Critério 3 */
    if (a->ingresso != b->ingresso) return a->ingresso < b->ingresso ? -1 : 1;
    /* Critério 4 */
    if (a->duracao != b->duracao) return a->duracao < b->duracao ? -1 : 1;
    /* Critério 5 fica para o chamador. */
    return 0;
}

int sched_comparar(const Escalonador *e, const TCB *a, const TCB *b, int agora, int quantum) {
    int c = e->prioridade(a, b, agora);
    if (c != 0) return c;
    return sched_desempate(a, b, quantum);
}
