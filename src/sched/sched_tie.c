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
 *   3. o instante de ingresso da tarefa: quem chegou antes. É o campo
 *      `ingresso` do arquivo, que o enunciado define como o instante em que
 *      a tarefa foi criada (e não a chegada da ativação atual);
 *   4. a duração: menor primeiro;
 *   5. sorteio: feito em sim.c, que guarda o estado do gerador aleatório
 *      (precisa ser reproduzível ao retroceder/avançar).
 *
 * Quantum (steering, ambiguidade 1): o quantum NÃO entra na comparação. Ao
 * fim do quantum o escalonador é chamado de novo (sim.c registra o evento
 * "fim de quantum") e, pelo critério 1, a mesma tarefa continua se ainda for
 * a de maior prioridade. Assim a ordem 1 -> 5 do enunciado vale sempre e o
 * quantum não distorce RM/EDF (uma tarefa com deadline mais próximo nunca
 * perde a CPU para uma empatada só porque esgotou a fatia de tempo).
 */
#include "sched.h"

int sched_desempate(const TCB *a, const TCB *b) {
    /* Critério 1: `cpu` >= 0 significa que executou no tick anterior. */
    bool ea = a->cpu >= 0, eb = b->cpu >= 0;
    if (ea != eb) return ea ? -1 : 1;
    /* Critério 2 */
    if (a->deadline_abs != b->deadline_abs) return a->deadline_abs < b->deadline_abs ? -1 : 1;
    /* Critério 3 */
    if (a->ingresso != b->ingresso) return a->ingresso < b->ingresso ? -1 : 1;
    /* Critério 4 */
    if (a->duracao != b->duracao) return a->duracao < b->duracao ? -1 : 1;
    /* Critério 5 fica para o chamador. */
    return 0;
}

void sched_atualizar(const Escalonador *e, TCB *k, int agora) {
    k->prio_nominal = e->prioridade(k, agora);
    /* Projeto B: aqui entra a herança (prio_ativa = min(nominal, herdada)). */
    k->prio_ativa = k->prio_nominal;
}

int sched_comparar(const TCB *a, const TCB *b) {
    if (a->prio_ativa != b->prio_ativa) return a->prio_ativa < b->prio_ativa ? -1 : 1;
    return sched_desempate(a, b);
}
