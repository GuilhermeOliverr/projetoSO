/*
 * sched_tie.c - cadeia de desempate comum a todos os algoritmos (req. 4.3).
 *
 * Ordem dos critérios (o primeiro que diferenciar decide):
 *   1. a tarefa que estava executando logo antes do escalonamento (evita
 *      trocar de contexto para a própria tarefa);
 *   2. o prazo: deadline absoluto mais próximo. Usamos o absoluto (e não o
 *      prazo relativo do arquivo) porque desempata pela urgência real da
 *      ativação atual. No EDF ele repete a prioridade (que já é o deadline
 *      absoluto): a lista de critérios é genérica, vale para qualquer
 *      algoritmo, e é no RM que este critério atua;
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
 *
 * Convenção de TODAS as funções de comparação daqui (a mesma do qsort):
 *   resultado < 0  -> `a` vem ANTES de `b` (a é mais prioritária)
 *   resultado > 0  -> `a` vem DEPOIS de `b`
 *   resultado == 0 -> empate total (quem decide é o sorteio, em sim.c)
 */
#include "sched.h"

/* Critérios 1 a 4. Só é chamada quando a prioridade ativa já empatou. */
int sched_desempate(const TCB *a, const TCB *b) {
    /* Critério 1: `cpu` >= 0 significa que executou no tick anterior.
     * `ea != eb` quer dizer que só uma das duas estava executando: ela
     * ganha (-1 = a primeira vem antes). */
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

/* Pergunta ao algoritmo qual a prioridade da tarefa neste tick e guarda no
 * TCB. É chamada para cada candidata, a cada tick (EDF muda o valor a cada
 * ativação, por isso não basta calcular uma vez). */
void sched_atualizar(const Escalonador *e, TCB *k, int agora) {
    k->prio_nominal = e->prioridade(k, agora);
    /* Projeto B: aqui entra a herança (prio_ativa = min(nominal, herdada)). */
    k->prio_ativa = k->prio_nominal;
}

/* Comparação completa: primeiro a prioridade ATIVA (menor = melhor); só se
 * empatar chama o desempate. É a função que o escalonador (sim.c) usa para
 * ordenar as candidatas. */
int sched_comparar(const TCB *a, const TCB *b) {
    if (a->prio_ativa != b->prio_ativa) return a->prio_ativa < b->prio_ativa ? -1 : 1;
    return sched_desempate(a, b);
}
