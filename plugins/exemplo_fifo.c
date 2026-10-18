/*
 * exemplo_fifo.c - exemplo de escalonador carregado em tempo de execução
 * (req. 4.2: o escalonador pode estar numa biblioteca dinâmica fora do
 * executável).
 *
 * Política: FIFO preemptivo pela chegada da ativação atual (quem chegou
 * antes tem prioridade). Serve só para mostrar o mecanismo.
 *
 * Compilar:  make plugins
 * Usar:      ./projetoSO arquivo.txt --plugin plugins/exemplo_fifo.so
 *            (e no arquivo: FIFO;quantum;cpus)
 */
#include "sched.h"

// Função estática que calcula a prioridade nominal do FIFO: recebe a tarefa 'k' e o tick 'agora'
static int prioridade_fifo(const TCB *k, int agora) {
    (void)agora;    // Suprime o aviso do compilador de variável não utilizada (o FIFO simples não depende do tempo atual)
    return k->chegada_atual;    // Retorna o instante de chegada da ativação atual (menor valor = chegou primeiro = maior prioridade)
}

// Instancia a estrutura do plugin exportando a variável com o símbolo esperado pelo simulador
const Escalonador escalonador_plugin = {
    "FIFO",        // Nome/sigla do algoritmo lido do arquivo de configuração
    "FIFO por chegada da ativação (plugin de exemplo)",        // Descrição legível da política do algoritmo mostrada na interface do usuário
    prioridade_fifo,    // Ponteiro para a função 'prioridade_fifo' que calcula a prioridade nominal
};
