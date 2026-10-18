/*
 * task.h - TCB (Task Control Block): tudo o que o simulador sabe sobre uma
 * tarefa fica numa única estrutura (requisito 1.3).
 *
 * Decisão: o TCB não tem ponteiros para memória própria (só `eventos_raw` e
 * `eventos`, que apontam para dados imutáveis da configuração). Assim um snapshot do
 * sistema é só uma cópia de um vetor de TCBs com memcpy, o que deixa o
 * avançar/retroceder simples e barato.
 */
#ifndef TASK_H
#define TASK_H

#include <stdbool.h>

/* Projeto A: toda tarefa periódica "termina" após 10 ativações (req. 4.4). */
#define MAX_ATIVACOES 10

// Início da enumeração dos estados do ciclo de vida de uma tarefa no sistema operacional
typedef enum {
    T_NOVA,        // Estado 0: Tarefa recém-criada/carregada que ainda não atingiu seu instante de primeiro ingresso
    T_PRONTA,      // Estado 1: Tarefa liberada e aguardando na fila global de prontos por uma CPU disponível
    T_EXECUTANDO,  // Estado 2: Tarefa alocada e em execução ativa em um dos processadores físicos do sistema
    T_SUSPENSA,    // Estado 3: Tarefa bloqueada/suspensa (aguardando o próximo período ou suspensa manualmente pelo usuário)
    T_TERMINADA    // Estado 4: Tarefa que concluiu definitivamente suas 10 ativações periódicas e encerrou sua execução
} EstadoTarefa;

// Início da enumeração que especifica a razão/motivo da suspensão de uma tarefa
typedef enum {
    SUSP_NENHUM,  // Motivo 0: A tarefa não está suspensa no momento
    SUSP_PERIODO,  // Motivo 1: Ativação atual concluída, aguardando o início do próximo período (no Gantt aparece em branco/vazia)
    SUSP_USUARIO   // Motivo 2: Suspensa manualmente pelo usuário no passo a passo (no Gantt aparece em preto pontilhado)
} MotivoSuspensao;

// Início da estrutura principal TCB (Task Control Block) que guarda todo o contexto da tarefa
typedef struct {

    /* ---- definidos no arquivo de configuração ---- */
    int id;                    // Identificador único da tarefa (exemplo: 1, 2, 3...)
    unsigned cor;              // Valor hexadecimal da cor RGB usada para renderizar a tarefa no Gráfico de Gantt
    int ingresso;              // Instante de relógio (tick) em que a tarefa faz seu primeiro ingresso na simulação
    int duracao;               // Quantidade de ticks de CPU necessários para completar cada ativação (C_i)
    int periodo;               // Intervalo regular de tempo em ticks entre liberações consecutivas da tarefa (P_i)
    int prazo;                 // Prazo relativo em ticks para conclusão de cada ativação (D_i)
    const char *eventos_raw;   // Ponteiro para a string bruta contendo a lista de eventos associada à tarefa
    const char *const *eventos;// Vetor dinâmico de ponteiros para as strings de eventos individuais sem interpretação
    int neventos;              // Quantidade total de eventos armazenados no vetor de eventos

    /* ---- estado dinâmico ---- */
    EstadoTarefa estado;        // Estado atual de execução da tarefa no ciclo de vida do SO (T_PRONTA, T_EXECUTANDO, etc.)
    MotivoSuspensao motivo;    // Razão da suspensão da tarefa quando estado == T_SUSPENSA (SUSP_PERIODO ou SUSP_USUARIO)
    int liberadas;             // Contador da quantidade de ativações que já foram liberadas/chegaram no sistema
    int concluidas;            // Contador da quantidade de ativações que já foram totalmente concluídas (máximo 10)
    int chegadas[MAX_ATIVACOES]; // Vetor que registra o instante exato de relógio (tick) de chegada de cada uma das 10 ativações
    int proxima_chegada;       // Instante do relógio global em que ocorrerá a liberação da próxima ativação
    int restante;              // Quantidade de ticks de CPU que ainda faltam ser executados para concluir a ativação atual
    int chegada_atual;         // Instante do relógio em que a ativação atualmente em andamento foi liberada
    int deadline_abs;          // Instante limite absoluto do relógio para o término da ativação atual (chegada_atual + prazo)
    unsigned perdas;           // Máscara/contador de bits indicando quais ativações estouraram o prazo
    int cpu;                   // Índice do processador em que a tarefa está executando no tick atual (-1 se não estiver na CPU)
    int quantum_usado;         // Quantidade de ticks contínuos que a tarefa já executou no quantum corrente

    /* ---- prioridade (menor valor = mais prioritária) ----
     * nominal: a que o algoritmo dá à tarefa (RM: período; EDF: deadline
     *          absoluto), recalculada a cada tick por sched_atualizar;
     * ativa:   a que o escalonador realmente compara (hoje é igual à
     *          nominal, mas ficam separadas para que a ativa possa mudar
     *          sem tocar na nominal). */
    int prio_nominal;        // Prioridade nominal calculada pelo algoritmo de escalonamento (menor período no RM / menor deadline no EDF)
    int prio_ativa;          // Prioridade efetiva usada na fila e no desempate do escalonador

    /* ---- estatísticas ---- */
    int ticks_exec, ticks_espera, ticks_suspensa;    // Contadores acumulados de ticks em execução, aguardando na fila e suspensa
    int deadlines_perdidos, preempcoes, sorteios;    // Estatísticas de total de perdas de prazos, preempções sofridas e vitórias em sorteios de desempate
    int resposta_max, resposta_soma;     // Métricas de tempo de resposta: maior tempo registrado e soma acumulada para média
    int termino;               // Instante de relógio em que a 10ª e última ativação foi concluída (-1 se ainda não terminou)
} TCB;

const char *estado_nome(EstadoTarefa e);    // Retorna uma string com o nome legível do estado da tarefa (exemplo: "PRONTA", "EXECUTANDO")

const char *estado_descricao(const TCB *k);    // Retorna a descrição detalhada do estado incluindo o motivo da suspensão quando aplicável

bool suspensa_pelo_usuario(const TCB *k);    // Retorna true se a tarefa foi suspensa manualmente pelo usuário (SUSP_USUARIO), impedindo que chegue a progredir sozinha

#endif
