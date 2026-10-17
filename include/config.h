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

// Estrutura que armazena os parâmetros de configuração de uma única tarefa
typedef struct {
    int id;
    unsigned cor;
    int ingresso,    // Instante (tick) da primeira chegada da tarefa
        duracao,     // C: ticks de CPU que a tarefa precisa em cada ativação
        periodo,     // P: intervalo em ticks entre duas ativações consecutivas
        prazo;       // D: prazo relativo à chegada de cada ativação (deadline = chegada + prazo)
    char *eventos;   // Ponteiro para a string original da lista de eventos
    int linha;      // Número da linha do arquivo .txt onde a tarefa foi lida (0 se criada na interface)
    char **lista_ev;     // Vetor dinâmico de strings contendo os eventos já separados por ',' ou ';'
    int nev;     // Quantidade total de eventos presentes em lista_ev
} TarefaCfg;

// Estrutura para acúmulo de mensagens de avisos e erros durante a leitura do arquivo
typedef struct {
    char **itens;    // Vetor dinâmico com o texto de cada mensagem de erro/aviso acumulada
    int n, cap;    // n = quantidade atual de mensagens , cap = capacidade alocada do vetor
} ListaMsg;

// Estrutura principal que armazena toda a configuração global do sistema simulado
typedef struct {
    char *caminho;    // Nome ou caminho completo do arquivo de configuração carregado
    int algoritmo;         // Índice/Identificador do algoritmo de escalonamento ativo
    int quantum;    // Valor do quantum de tempo em ticks
    int ncpus;       // número de cpus
    TarefaCfg *tarefas;    // Vetor dinâmico com a configuração de todas as tarefas periódicas
    int n, cap;        // n = número total de tarefas , cap = capacidade alocada do vetor
    ListaMsg avisos;        // Lista contendo problemas não fatais (ex: tarefa aperiódica) 
    ListaMsg erros;         // Lista contendo erros fatais que impedem a execução
} Config;

/* Protótipos das Funções do Módulo de Configuração */

void config_padrao(Config *cfg);    // Inicializa a estrutura Config com os valores padrão sugeridos

bool config_carregar(const char *caminho, Config *cfg);    // Realiza a leitura (parser) e validação do arquivo de entrada

void config_liberar(Config *cfg);    // Libera toda a memória dinâmica alocada pela estrutura Config

void config_imprimir_mensagens(const Config *cfg);    // Imprime na interface todos os avisos e erros acumulados

unsigned config_cor_padrao(int i);    // Retorna uma cor padrão da paleta para a i-ésima tarefa

void config_adicionar(Config *cfg, const TarefaCfg *t);    // Adiciona uma nova tarefa ao vetor dinâmico de tarefas

void config_definir_eventos(TarefaCfg *t, const char *texto);    // Divide a string de eventos por delimitadores e armazena em lista_ev

void config_liberar_tarefa(TarefaCfg *t);    // Libera a memória associada à string de eventos de uma tarefa

const char *config_validar_quantum(int v);    // Valida se o quantum informado é maior que 0
const char *config_validar_cpus(int v);    // Valida se o número de CPUs está dentro do intervalo permitido (1 a MAX_CPUS)

void msg_add(ListaMsg *l, const char *fmt, ...);    // Formata e insere uma nova mensagem na estrutura ListaMsg
void msg_liberar(ListaMsg *l);    // Libera a memória alocada para a estrutura ListaMsg

#endif
