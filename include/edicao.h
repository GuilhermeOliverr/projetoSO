/*
 * edicao.h - regras para alterar tarefas, comuns às duas interfaces
 * (terminal em ui.c e janela em ui_gui.c).
 *
 * Validação: cada valida_* devolve NULL se o valor serve, ou o motivo da
 * recusa, para mostrar ao usuário (req. 3.1/3.4).
 *
 * Aplicação: cada edicao_* altera o TCB e ajusta o que depende do campo
 * (ex.: mudar o período recalcula a próxima chegada). Elas NÃO chamam
 * sim_preparar_edicao/sim_concluir_edicao; quem chama faz isso em volta,
 * depois de validar, para que uma edição recusada não descarte o futuro.
 */
#ifndef EDICAO_H
#define EDICAO_H

#include "task.h"

const char *valida_positivo(int v);
const char *valida_periodo(int v);
/* Ingresso não pode ser antes de `min` (não se chega no passado). */
const char *valida_ingresso(int v, int min);
/* `ids` tem `n` ids em uso; `proprio` é a posição da tarefa sendo editada
 * (-1 para uma tarefa nova), cujo id atual não conta como repetido. */
const char *valida_id(int v, const int *ids, int n, int proprio);

/* Período novo: a próxima chegada passa a ser a última chegada + período. */
void edicao_periodo(TCB *k, int v);
/* Prazo novo: recalcula o deadline da ativação em andamento, se houver. */
void edicao_prazo(TCB *k, int v);
/* Ingresso novo (só antes da primeira chegada). */
void edicao_ingresso(TCB *k, int v);
/* Suspende (pelo usuário) ou retoma a tarefa. */
void edicao_alternar_suspensao(TCB *k);
/* Encerra a tarefa no instante t, como se tivesse feito as 10 ativações. */
void edicao_encerrar(TCB *k, int t);

#endif
