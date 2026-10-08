/*
 * sched.h - contrato dos escalonadores ("plugins").
 *
 * Requisito 4.2: deve ser fácil incluir algoritmos novos sem mexer na
 * simulação. Por isso um escalonador aqui é só uma função de COMPARAÇÃO de
 * prioridade entre duas tarefas prontas. A simulação (sim.c) é quem monta a
 * fila global, aplica a cadeia de desempate comum (sched_tie.c) e entrega as
 * M tarefas de maior prioridade para as M CPUs.
 *
 * Por que comparação e não "devolva a próxima tarefa"? Com várias CPUs
 * precisamos das M melhores, não só da melhor, e o desempate (critérios 1 a
 * 5 do enunciado) é igual para todos os algoritmos. Com a comparação, cada
 * algoritmo novo tem poucas linhas e não pode errar o desempate.
 *
 * Para adicionar um algoritmo:
 *   1. crie sched_xxx.c com uma função prioridade_fn e um `const Escalonador`;
 *   2. acrescente-o na tabela de sched_registro.c;
 *   ou compile-o como biblioteca dinâmica (ver plugins/exemplo_fifo.c) e
 *   carregue com `--plugin arquivo.so`, sem recompilar o simulador.
 */
#ifndef SCHED_H
#define SCHED_H

#include "task.h"

/* Devolve < 0 se `a` tem prioridade MAIOR que `b`, > 0 se menor, 0 se
 * empatam (aí entra a cadeia de desempate). `agora` é o tick atual, útil
 * para algoritmos dinâmicos. */
typedef int (*prioridade_fn)(const TCB *a, const TCB *b, int agora);

typedef struct {
    const char *nome;          /* string usada no arquivo (sem diferenciar caixa) */
    const char *descricao;     /* texto mostrado ao usuário                       */
    prioridade_fn prioridade;
} Escalonador;

/* Nome do símbolo que um plugin .so deve exportar. */
#define SCHED_SIMBOLO_PLUGIN "escalonador_plugin"

/* ---- registro (sched_registro.c) ---- */
int sched_quantidade(void);
const Escalonador *sched_obter(int idx);
int sched_buscar(const char *nome);          /* índice ou -1 */
void sched_listar_nomes(char *buf, int tam);  /* "RM, EDF" para mensagens */

/* Carrega um escalonador de uma biblioteca dinâmica. Devolve o índice ou -1,
 * com o motivo em `erro`. */
int sched_carregar_plugin(const char *caminho, char *erro, int tam_erro);

/* ---- desempate comum (sched_tie.c) ---- */
/* Critérios 1 a 4 do enunciado (o 5, sorteio, é feito pela simulação, que
 * tem o gerador aleatório). Mesmo sinal de prioridade_fn. */
int sched_desempate(const TCB *a, const TCB *b, int quantum);

/* Prioridade + desempate: ordem total usada para escolher as tarefas. */
int sched_comparar(const Escalonador *e, const TCB *a, const TCB *b, int agora, int quantum);

#endif
