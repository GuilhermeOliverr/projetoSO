/*
 * config.h - leitura e validação do arquivo de configuração (req. 3.3).
 *
 * Formato:
 *   algoritmo_escalonamento;quantum;qtde_cpus
 *   id;cor;ingresso;duracao;periodo;prazo;lista_eventos
 *   ...
 *
 * O parser junta TODOS os erros do arquivo antes de desistir, para que o
 * usuário corrija tudo de uma vez, e cada mensagem diz arquivo, linha,
 * campo e motivo.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

/* Valores padrão sugeridos (req. 3.2). Valem quando um campo vem vazio no
 * arquivo e aparecem como sugestão nas telas de edição. */
#define PADRAO_ALGORITMO "RM"
#define PADRAO_QUANTUM   2
#define PADRAO_CPUS      1
#define PADRAO_INGRESSO  0
#define PADRAO_DURACAO   2
#define PADRAO_PERIODO   10
/* prazo padrão = período (prazo implícito, como no capítulo 2) */

/* Limite de CPUs. O enunciado diz "1 a N" sem fixar N; 64 cobre qualquer
 * caso didático e ainda cabe no Gantt. */
#define MAX_CPUS 64

typedef struct {
    int id;
    unsigned cor;
    int ingresso, duracao, periodo, prazo;
    char *eventos;   /* texto cru de lista_eventos ("" se não houver) */
    int linha;       /* linha do arquivo (0 = criada pelo usuário)     */
} TarefaCfg;

typedef struct {
    char **itens;
    int n, cap;
} ListaMsg;

typedef struct {
    char *caminho;
    int algoritmo;          /* índice no registro de escalonadores */
    int quantum;
    int ncpus;
    TarefaCfg *tarefas;     /* só as periódicas (aperiódicas são descartadas) */
    int n, cap;
    ListaMsg avisos;        /* problemas não fatais (ex.: tarefa aperiódica) */
    ListaMsg erros;         /* problemas fatais                              */
} Config;

/* Preenche `cfg` com os valores padrão e nenhuma tarefa. */
void config_padrao(Config *cfg);

/* Carrega o arquivo. Devolve true se não houve erros (avisos podem existir). */
bool config_carregar(const char *caminho, Config *cfg);

void config_liberar(Config *cfg);

/* Imprime avisos e erros acumulados. */
void config_imprimir_mensagens(const Config *cfg);

/* Cor padrão para a tarefa de índice i (paleta com cores bem distintas). */
unsigned config_cor_padrao(int i);

/* Acrescenta uma tarefa (cópia de `t`, eventos duplicado). */
void config_adicionar(Config *cfg, const TarefaCfg *t);

/* Validação comum ao arquivo e às telas de edição. Devolve NULL se ok, ou
 * o motivo do erro. */
const char *config_validar_quantum(int v);
const char *config_validar_cpus(int v);

void msg_add(ListaMsg *l, const char *fmt, ...);
void msg_liberar(ListaMsg *l);

#endif
