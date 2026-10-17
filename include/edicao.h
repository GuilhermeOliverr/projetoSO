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

/* FUNÇÕES DE VALIDAÇÃO (Retornam NULL se válido ou uma string de erro caso recuse) */

const char *valida_positivo(int v);  // Recebe um inteiro 'v' e verifica se é estritamente positivo (> 0). Usado para duração, período e prazo

const char *valida_periodo(int v);  // Valida se o novo período 'v' cumpre as regras do sistema (> 0)

const char *valida_ingresso(int v, int min);  // Garante que o novo instante de ingresso 'v' não seja menor que 'min' (tick atual da simulação)

const char *valida_id(int v, const int *ids, int n, int proprio);  // Verifica se o ID 'v' informado é único e não colide com outros IDs já existentes no sistema

/* FUNÇÕES DE APLICAÇÃO (Modificam o TCB e reajustam campos dependentes) */

void edicao_periodo(TCB *k, int v);  // Atualiza o período no TCB 'k' para 'v' e recalcula a data da próxima liberação da tarefa ( a próxima chegada passa a ser a última chegada + período)

void edicao_prazo(TCB *k, int v);  // Altera o prazo relativo para 'v' e ajusta imediatamente o deadline absoluto (deadline_abs) da ativação atual

void edicao_ingresso(TCB *k, int v);  // Altera o instante do primeiro ingresso da tarefa

void edicao_alternar_suspensao(TCB *k);  // Alterna a tarefa entre o estado suspenso e o normal (pausa/retoma a disputa pela CPU)

void edicao_encerrar(TCB *k, int t);  // Força o encerramento imediato da tarefa no tick 't' ajustando seu número de ativações para 10

#endif
