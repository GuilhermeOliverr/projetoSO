/*
 * sim.h - o simulador: relógio global, ativações, escalonamento por tick,
 * eventos, rastro para o Gantt e histórico para avançar/retroceder.
 *
 * Modelo de tempo: o tick t representa o intervalo [t, t+1). "Instante t"
 * é a fronteira esquerda do tick t. Chegadas acontecem no instante t (início
 * do tick); fim de ativação e perda de prazo são detectados no instante t+1
 * (fim do tick em que a tarefa executou).
 */
#ifndef SIM_H
#define SIM_H

#include "config.h"
#include "sched.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

/* Proteção contra laço infinito ao "rodar até o fim" (só aconteceria com
 * edições muito estranhas; a simulação normal sempre termina). */
#define LIMITE_TICKS 2000000

typedef enum {
    EV_CHEGADA,        /* nova ativação chegou                     */
    EV_FIM_ATIVACAO,   /* ativação concluída                       */
    EV_TERMINO,        /* 10ª ativação concluída: tarefa terminou  */
    EV_PRAZO_PERDIDO,  /* ativação passou do deadline              */
    EV_SORTEIO,        /* tarefa escolhida por sorteio (critério 5) */
    EV_PREEMPCAO,      /* tarefa perdeu a CPU                      */
    EV_QTD
} TipoEvento;

typedef struct {
    int tick;          /* tick em que foi gerado (para descartar ao editar)  */
    int instante;      /* momento exato do evento, desenhado no Gantt        */
    TipoEvento tipo;
    int tarefa;        /* índice da tarefa no vetor de TCBs                  */
    int ativacao;      /* número da ativação (0..9)                          */
    int cpu;           /* CPU envolvida, ou -1                               */
} Evento;

/* O que o Gantt desenha numa célula (tarefa x tick). */
typedef enum { G_VAZIO, G_PRONTA, G_EXEC, G_SUSPENSA } CelulaGantt;

/* Estado completo do sistema num instante. É exatamente o que vai num
 * snapshot do histórico. */
typedef struct {
    int t;              /* relógio global                                */
    int ncpus, quantum;
    int alg;            /* índice no registro de escalonadores           */
    uint64_t rng;       /* estado do sorteio: garante determinismo       */
    int n;
    TCB *tarefas;
    bool fim;           /* todas as tarefas terminaram                   */
} Estado;

typedef struct {
    Estado atual;

    /* Histórico (history.c): snaps[k] = estado no instante k. Só é mantido
     * no modo passo a passo; na execução completa não há como voltar. */
    bool historico;
    Estado *snaps;
    int nsnaps, capsnaps;

    /* Rastro do Gantt: cel/cpu[tick * largura + tarefa]. Guardamos o
     * rastro à parte dos snapshots para desenhar o gráfico sem reconstruir
     * nada, e para que a execução completa não precise de snapshots. */
    int largura;
    unsigned char *cel;
    signed char *cpu;
    unsigned char *ncpus_col;   /* CPUs existentes em cada tick (podem ser
                                   alteradas por edição durante a simulação) */
    int ncols, capcols;

    Evento *ev;
    int nev, capev;
} Simulador;

const char *evento_nome(TipoEvento t);

/* ---- sim.c ---- */
void sim_iniciar(Simulador *s, const Config *cfg, bool historico, uint64_t semente);
void sim_liberar(Simulador *s);

bool sim_avancar(Simulador *s);           /* false se a simulação já acabou   */
bool sim_voltar(Simulador *s);            /* false se já está no instante 0   */
int  sim_ir_para(Simulador *s, int t);    /* devolve o instante alcançado     */

/* Roda até o fim. Devolve NULL se terminou, ou o motivo de ter parado. */
const char *sim_rodar_ate_fim(Simulador *s);

/* Nenhuma tarefa pode progredir (todas as restantes estão suspensas). */
bool sim_bloqueado(const Estado *e);

/* Edição: chame preparar antes de alterar s->atual (descarta o futuro) e
 * concluir depois (grava o estado editado como snapshot do instante atual). */
void sim_preparar_edicao(Simulador *s);
void sim_concluir_edicao(Simulador *s);

/* Acrescenta uma tarefa durante a simulação (chega a partir do ingresso). */
void sim_adicionar_tarefa(Simulador *s, const TarefaCfg *t);

/* Reinicia o TCB a partir dos dados de configuração. */
void tcb_iniciar(TCB *k, const TarefaCfg *t);

CelulaGantt sim_celula(const Simulador *s, int tick, int tarefa);
int sim_cpu_celula(const Simulador *s, int tick, int tarefa);

/* Índice da tarefa com esse id, ou -1. */
int sim_indice(const Estado *e, int id);

/* Utilização U = soma(C/P) e limite do teste de RM n(2^(1/n) - 1). */
double sim_utilizacao(const Estado *e);
double sim_limite_rm(int n);

/* ---- history.c ---- */
void estado_copiar(Estado *dst, const Estado *src);
void estado_liberar(Estado *e);
void hist_gravar(Simulador *s);              /* grava s->atual em snaps[t]   */
void hist_truncar(Simulador *s, int t);      /* descarta tudo depois de t    */
void hist_carregar(Simulador *s, int t);     /* s->atual = snaps[t]          */
void rastro_garantir_coluna(Simulador *s, int tick);
void rastro_ajustar_largura(Simulador *s, int largura);
void evento_add(Simulador *s, int tick, int instante, TipoEvento tipo, int tarefa, int ativ, int cpu);

#endif
