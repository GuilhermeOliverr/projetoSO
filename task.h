/*
 * task.h - TCB (Task Control Block): tudo o que o simulador sabe sobre uma
 * tarefa fica numa única estrutura (requisito 1.3).
 *
 * Decisão: o TCB não tem ponteiros para memória própria (só `eventos_raw`,
 * que aponta para texto imutável da configuração). Assim um snapshot do
 * sistema é só uma cópia de um vetor de TCBs com memcpy, o que deixa o
 * avançar/retroceder simples e barato.
 */
#ifndef TASK_H
#define TASK_H

#include <stdbool.h>

/* Projeto A: toda tarefa periódica "termina" após 10 ativações (req. 4.4). */
#define MAX_ATIVACOES 10

typedef enum {
    T_NOVA,        /* ainda não chegou ao sistema (antes do ingresso)          */
    T_PRONTA,      /* tem ativação pendente e está na fila global de prontos    */
    T_EXECUTANDO,  /* ocupando uma CPU neste tick                               */
    T_SUSPENSA,    /* tirada da disputa (Projeto A: só por edição manual)       */
    T_ESPERANDO,   /* ativação atual concluída, aguardando o próximo período    */
    T_TERMINADA    /* concluiu as 10 ativações                                  */
} EstadoTarefa;

typedef struct {
    /* ---- definidos no arquivo de configuração ---- */
    int id;
    unsigned cor;              /* 0xRRGGBB                                       */
    int ingresso;              /* instante da 1ª ativação                        */
    int duracao;               /* C: ticks de CPU por ativação                   */
    int periodo;               /* P: intervalo entre ativações                   */
    int prazo;                 /* D: prazo relativo à chegada de cada ativação    */
    const char *eventos_raw;   /* lista de eventos, guardada crua p/ o Projeto B */

    /* ---- estado dinâmico ---- */
    EstadoTarefa estado;
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

    /* ---- estatísticas ---- */
    int ticks_exec, ticks_espera, ticks_suspensa;
    int deadlines_perdidos, preempcoes, sorteios;
    int resposta_max, resposta_soma; /* tempo de resposta (fim - chegada)       */
    int termino;               /* instante em que terminou a 10ª ativação, ou -1 */
} TCB;

const char *estado_nome(EstadoTarefa e);

#endif
