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

/* Os 5 estados do ciclo de vida de uma tarefa (Maziero, cap. 4, seção 4.4). */
typedef enum {
    T_NOVA,        /* ainda não chegou ao sistema (antes do ingresso)          */
    T_PRONTA,      /* tem ativação pendente e está na fila global de prontos    */
    T_EXECUTANDO,  /* ocupando uma CPU neste tick                               */
    T_SUSPENSA,    /* não pode executar: o motivo fica em TCB.motivo            */
    T_TERMINADA    /* concluiu as 10 ativações                                  */
} EstadoTarefa;

/* Por que uma tarefa está SUSPENSA. O Maziero inclui em "suspensa" tanto a
 * espera por um recurso quanto a espera pelo tempo passar (sleep); aqui o
 * motivo decide como a tarefa sai da suspensão e como aparece no Gantt. */
typedef enum {
    SUSP_NENHUM,   /* a tarefa não está suspensa                                */
    SUSP_PERIODO,  /* ativação concluída, esperando o próximo período: volta
                      sozinha para PRONTA quando a próxima ativação chega. No
                      Gantt fica vazia, como nas figuras 2.5 e 2.6 do capítulo. */
    SUSP_USUARIO   /* suspensa pelo usuário no passo a passo: só volta quando
                      for retomada; chegadas não a tiram da suspensão. No Gantt
                      aparece em preto pontilhado.                             */
} MotivoSuspensao;

typedef struct {
    /* ---- definidos no arquivo de configuração ---- */
    int id;
    unsigned cor;              /* 0xRRGGBB                                       */
    int ingresso;              /* instante da 1ª ativação                        */
    int duracao;               /* C: ticks de CPU por ativação                   */
    int periodo;               /* P: intervalo entre ativações                   */
    int prazo;                 /* D: prazo relativo à chegada de cada ativação    */
    const char *eventos_raw;   /* lista de eventos como veio do arquivo          */
    const char *const *eventos;/* a mesma lista já separada, um item por evento.
                                  Vetor dinâmico (sem limite de eventos por
                                  tarefa), guardado sem interpretação.          */
    int neventos;

    /* ---- estado dinâmico ---- */
    EstadoTarefa estado;
    MotivoSuspensao motivo;    /* só vale quando estado == T_SUSPENSA         */
    int liberadas;             /* ativações que já chegaram (0..10)              */
    int concluidas;            /* ativações já terminadas (0..10)                */
    int chegadas[MAX_ATIVACOES]; /* instante de chegada de cada ativação         */
    int proxima_chegada;       /* quando chega a próxima ativação                */
    int restante;              /* ticks que faltam na ativação atual (0 = nenhuma) */
    int chegada_atual;         /* chegada da ativação em andamento               */
    int deadline_abs;          /* chegada_atual + prazo                          */
    unsigned perdas;           /* bit k ligado = ativação k já perdeu o prazo.
                                  Um bit por ativação (e não um bool só da
                                  atual) porque ativações acumuladas, que ainda
                                  nem começaram, também podem vencer o prazo. */
    int cpu;                   /* CPU usada no último tick (-1 = não executou).
                                  Serve para o critério 1 de desempate e para
                                  manter a tarefa na mesma CPU (afinidade).     */
    int quantum_usado;         /* ticks seguidos desde que ganhou a CPU          */

    /* ---- prioridade (menor valor = mais prioritária) ----
     * nominal: a que o algoritmo dá à tarefa (RM: período; EDF: deadline
     *          absoluto), recalculada a cada tick por sched_atualizar;
     * ativa:   a que o escalonador realmente compara (hoje é igual à
     *          nominal, mas ficam separadas para que a ativa possa mudar
     *          sem tocar na nominal). */
    int prio_nominal;
    int prio_ativa;

    /* ---- estatísticas ---- */
    int ticks_exec, ticks_espera, ticks_suspensa;
    int deadlines_perdidos, preempcoes, sorteios;
    int resposta_max, resposta_soma; /* tempo de resposta (fim - chegada)       */
    int termino;               /* instante em que terminou a 10ª ativação, ou -1 */
} TCB;

const char *estado_nome(EstadoTarefa e);
/* Estado com o motivo, quando suspensa ("suspensa: próximo período"). */
const char *estado_descricao(const TCB *k);
/* Suspensa pelo usuário: a única suspensão que impede a tarefa de progredir
 * sozinha. */
bool suspensa_pelo_usuario(const TCB *k);

#endif
