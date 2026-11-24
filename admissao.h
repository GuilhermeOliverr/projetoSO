/*
 * admissao.h - teste de escalonabilidade na admissão (Projeto B, req. 3).
 *
 * O enunciado pede o teste "antes de qualquer tarefa ser aceita e incluída
 * para execução no sistema", para todos os tipos de tarefa. A simulação
 * (sim.c) chama estas funções em dois momentos:
 *   - no ingresso de cada tarefa (é quando ela é criada), com
 *     admissao_tarefa: a tarefa nova contra as que já estão no sistema;
 *   - quando uma ativação de aperiódica com prazo entra na fila, com
 *     admissao_ativacao: cada ocorrência dela é uma tarefa nova criada
 *     durante a execução.
 *
 * As funções só leem o estado; quem aplica o resultado é sim.c.
 */
#ifndef ADMISSAO_H
#define ADMISSAO_H

#include "sim.h"

typedef struct {
    ResultadoAdmissao resultado;
    char motivo[240];   // o teste usado e os números dele, para mostrar ao usuário
} Admissao;

// A tarefa 'cand' (índice em e->tarefas, ainda não admitida) pode entrar no sistema, junto com as que já foram aceitas e não terminaram?
Admissao admissao_tarefa(const Estado *e, int cand);

// A ativação atual da aperiódica 'cand', que entra na fila no instante 'agora', cabe até o prazo dela no que os servidores garantem?
Admissao admissao_ativacao(const Estado *e, int cand, int agora);

#endif
