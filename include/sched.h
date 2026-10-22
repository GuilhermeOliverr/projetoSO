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
 * (a comparada de fato).
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

// Define o tipo de ponteiro de função 'prioridade_fn' que calcula e retorna a prioridade nominal da tarefa 'k' no tick 'agora'
typedef int (*prioridade_fn)(const TCB *k, int agora);

// Início da definição da estrutura que representa um algoritmo de escalonamento ("plugin")
typedef struct {
    const char *nome;         // Ponteiro para a string com o nome/sigla do algoritmo no arquivo de configuração (exemplo: "RM" ou "EDF")
    const char *descricao;     // Ponteiro para a string descritiva do algoritmo apresentada ao usuário na interface
    prioridade_fn prioridade;    // Ponteiro para a função específica do algoritmo responsável por calcular a prioridade nominal da tarefa
} Escalonador;

/* Nome do símbolo que um plugin .so deve exportar. */
#define SCHED_SIMBOLO_PLUGIN "escalonador_plugin"

/* ---- registro (sched_registro.c) ---- */
int sched_quantidade(void);    // Retorna o número total de escalonadores registrados na tabela do simulador
const Escalonador *sched_obter(int idx);    // Retorna um ponteiro para a estrutura do escalonador no índice 'idx' da tabela
int sched_buscar(const char *nome);          // Procura um algoritmo registrado pelo nome (sem diferenciar maiúsculas/minúsculas) e retorna seu índice ou -1
void sched_listar_nomes(char *buf, int tam);      // Formata o buffer 'buf' com a lista dos nomes registrados (ex: "RM, EDF") para exibição em mensagens

int sched_carregar_plugin(const char *caminho, char *erro, int tam_erro);    // Carrega dinamicamente um novo algoritmo de uma biblioteca (.so) e retorna seu índice ou -1 em caso de falha

void sched_atualizar(const Escalonador *e, TCB *k, int agora);    // Calcula a prioridade nominal da tarefa 'k' com o algoritmo 'e' e copia o resultado para a prioridade ativa

int sched_desempate(const TCB *a, const TCB *b);    // Compara as tarefas 'a' e 'b' aplicando estritamente os critérios de 1 a 4 da cadeia de desempate (retorna < 0 se 'a' vem antes, > 0 se depois)

int sched_comparar(const TCB *a, const TCB *b);    // Compara duas tarefas considerando a prioridade ativa e, em caso de empate, aplicando a cadeia de desempate

#endif
