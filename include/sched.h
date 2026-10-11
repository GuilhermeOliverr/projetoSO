/*
 * sched.h - contrato dos escalonadores ("plugins").
 *
 * Requisito 4.2: deve ser fácil incluir algoritmos novos sem mexer na
 * simulação. Por isso um escalonador aqui é só uma função que diz a
 * PRIORIDADE NOMINAL de uma tarefa (um número; menor = mais prioritária).
 * A simulação (sim.c) é quem monta a fila global, aplica a cadeia de
 * desempate comum (sched_tie.c) e entrega as M tarefas de maior prioridade
 * para as M CPUs.
 *
 * Por que um número e não "devolva a próxima tarefa"? Com várias CPUs
 * precisamos das M melhores, não só da melhor, e o desempate (critérios 1 a
 * 5 do enunciado) é igual para todos os algoritmos. E, sendo um número, a
 * simulação consegue separar a prioridade nominal (do algoritmo) da ativa
 * (a comparada de fato), o que a herança de prioridade do Projeto B exige.
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

/* Prioridade nominal da tarefa `k` no tick `agora` (útil para algoritmos
 * dinâmicos). MENOR valor = MAIOR prioridade; valores iguais empatam e aí
 * entra a cadeia de desempate. */
typedef int (*prioridade_fn)(const TCB *k, int agora);

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

/* ---- prioridade e desempate comum (sched_tie.c) ---- */
/* Recalcula prio_nominal com o algoritmo `e` e copia para prio_ativa (no
 * Projeto A não há herança de prioridade). */
void sched_atualizar(const Escalonador *e, TCB *k, int agora);

/* Critérios 1 a 4 do enunciado (o 5, sorteio, é feito pela simulação, que
 * tem o gerador aleatório). < 0 se `a` vem antes de `b`, > 0 se depois. */
int sched_desempate(const TCB *a, const TCB *b);

/* Prioridade ativa + desempate: ordem usada para escolher as tarefas. */
int sched_comparar(const TCB *a, const TCB *b);

#endif
