/*
 * config.h - arquivo de configuração e funções auxiliares.
 *
 * Junta include/config.h e include/util.h da versão organizada em pastas.
 * Cada trecho começa com um divisor e com o comentário de cabeçalho original.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ID_MAX (tamanho do id da tarefa) fica em sched.h, junto do TCB, porque o
 * plugin só inclui sched.h; TarefaCfg usa o mesmo valor. */
#include "sched.h"

/* ===========================================================================
 * Seção config.h (originalmente include/config.h)
 * =========================================================================== */
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
    char id[ID_MAX]; // Identificador único, em texto (ver ID_MAX em sched.h); id inteiro fica na forma do %d ("01" vira "1")
    unsigned cor;
    int ingresso,    // Instante (tick) da primeira chegada da tarefa
        duracao,     // C: ticks de CPU que a tarefa precisa em cada ativação
        periodo,     // P: intervalo em ticks entre duas ativações consecutivas
        prazo;       // D: prazo relativo à chegada de cada ativação (deadline = chegada + prazo)
    char *eventos;   // Ponteiro para a string original da lista de eventos
    int linha;      // Número da linha do arquivo .txt onde a tarefa foi lida (0 se criada na interface)
    char **lista_ev;     // Vetor dinâmico de strings contendo os eventos já separados por ',' ou ';'
    int nev;     // Quantidade total de eventos presentes em lista_ev
    TipoTarefa tipo;         // Periódica, aperiódica, esporádica ou servidor (config_classificar)
    TipoServidor servidor;   // Qual servidor, quando tipo == TIPO_SERVIDOR
    int *ocorr;              // Aperiódica/esporádica: instantes de chegada relativos ao ingresso, lidos de lista_eventos
    int nocorr;              // Quantidade de instantes em ocorr
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
    TarefaCfg *tarefas;    // Vetor dinâmico com a configuração de todas as tarefas (de todos os tipos)
    int n, cap;        // n = número total de tarefas , cap = capacidade alocada do vetor
    ListaMsg avisos;        // Lista contendo problemas não fatais (ex: tarefa aperiódica) 
    ListaMsg erros;         // Lista contendo erros fatais que impedem a execução
} Config;

/* Leitura de um arquivo de texto linha a linha, com as regras do arquivo de
 * configuração: recusa diretório e arquivo binário, pula o BOM UTF-8 e tira
 * espaços, tabs e o \r (Windows) das pontas de cada linha. É usada aqui e
 * pelo arquivo de escalonador (sched.c, seção sched_formula.c), para os dois
 * aceitarem exatamente os mesmos arquivos sem duplicar código. */
typedef struct {
    char *texto;   // o arquivo inteiro na memória (as linhas devolvidas apontam para dentro dele)
    char *p;       // início da próxima linha (NULL = acabou)
    int linha;     // número da última linha devolvida por leitor_proxima (a 1ª é 1)
} LeitorLinhas;

bool leitor_abrir(LeitorLinhas *l, const char *caminho, ListaMsg *erros);    // Lê o arquivo inteiro; false com o motivo em 'erros' (não abre, é diretório, é binário)
char *leitor_proxima(LeitorLinhas *l);    // Próxima linha, já sem '\n', '\r' e espaços nas pontas; NULL no fim do arquivo
void leitor_fechar(LeitorLinhas *l);      // Libera o texto (as linhas devolvidas deixam de valer)

/* Protótipos das Funções do Módulo de Configuração */

void config_padrao(Config *cfg);    // Inicializa a estrutura Config com os valores padrão sugeridos

bool config_carregar(const char *caminho, Config *cfg);    // Realiza a leitura (parser) e validação do arquivo de entrada

void config_liberar(Config *cfg);    // Libera toda a memória dinâmica alocada pela estrutura Config

void config_imprimir_mensagens(const Config *cfg);    // Imprime na interface todos os avisos e erros acumulados

unsigned config_cor_padrao(int i);    // Retorna uma cor padrão da paleta para a i-ésima tarefa

void config_adicionar(Config *cfg, const TarefaCfg *t);    // Adiciona uma nova tarefa ao vetor dinâmico de tarefas

void config_definir_eventos(TarefaCfg *t, const char *texto);    // Divide a string de eventos por delimitadores e armazena em lista_ev

void config_liberar_tarefa(TarefaCfg *t);    // Libera a memória associada à string de eventos de uma tarefa

bool config_id_servidor(const char *id, TipoServidor *tipo);    // true se o id indica um servidor (BS seguido de um número, sem diferenciar maiúsculas); o tipo vai para '*tipo' (pode ser NULL)

void config_classificar(TarefaCfg *t);    // Define t->tipo e t->servidor pelo id e pelo sinal do período (req. B 1.2, 1.3 e 2)

// Lê os instantes de 'eventos' de uma aperiódica ou esporádica: inteiros >= 0, em ordem crescente e, na esporádica, separados por
// pelo menos |periodo| (req. B 1.3). Sem nenhum instante, a tarefa tem uma ocorrência só, no ingresso. Devolve NULL se deu certo
// (com o vetor novo em '*out' e o tamanho em '*n', se não forem NULL) ou o motivo da recusa.
const char *config_ocorrencias(const char *eventos, TipoTarefa tipo, int periodo, int ingresso, int **out, int *n);

const char *config_validar_quantum(int v);    // Valida se o quantum informado é maior que 0
const char *config_validar_cpus(int v);    // Valida se o número de CPUs está dentro do intervalo permitido (1 a MAX_CPUS)

void msg_add(ListaMsg *l, const char *fmt, ...);    // Formata e insere uma nova mensagem na estrutura ListaMsg
void msg_liberar(ListaMsg *l);    // Libera a memória alocada para a estrutura ListaMsg

/* ===========================================================================
 * Seção util.h (originalmente include/util.h)
 * =========================================================================== */
/*
 * util.h - funções auxiliares usadas por todos os módulos.
 *
 * Por que existe: o C11 puro não tem strdup, strcasecmp nem uma forma segura
 * de converter texto em inteiro com detecção de erro. Juntamos aqui essas
 * pequenas rotinas para não depender de extensões POSIX (requisito G5:
 * compilar em qualquer lugar só com a libc).
 */

// Aloca 'n' bytes de memória com verificação automática (encerra o programa com erro claro se faltar memória)
void *xmalloc(size_t n);  

// Aloca 'n' elementos de tamanho 'tam' zerando a memória alocada, com verificação de falha
void *xcalloc(size_t n, size_t tam);

// Redimensiona o bloco de memória apontado por 'p' para 'n' bytes com verificação automática de falha
void *xrealloc(void *p, size_t n);

// Aloca memória e duplica a string 's' de forma segura (substitui a função não-padrão strdup)
char *xstrdup(const char *s);

// Remove espaços, tabulações e quebras de linha ('\\r', '\\n') do início e do fim da string 's'
char *str_trim(char *s);

// Compara se duas strings 'a' e 'b' são iguais ignorando maiúsculas e minúsculas (case-insensitive)
bool str_ieq(const char *a, const char *b);

// Converte a string 's' em inteiro em '*out' (retorna 0 se ok, -1 se inválido, -2 se estourar int)
int parse_int(const char *s, int *out);

// Converte string hexadecimal ("RRGGBB", "#RRGGBB" ou "0xRRGGBB") para um valor RGB em '*out'
bool parse_cor(const char *s, unsigned *out);

// Exibe 'prompt' e lê uma linha do teclado em 'buf' (retorna false em EOF ou interrupção)
bool ler_linha(const char *prompt, char *buf, size_t tam);

// Pasta de um caminho de arquivo ("/Volumes/PENDRIVE/x.txt" -> "/Volumes/PENDRIVE"; "x.txt" -> "."), com '/' ou '\\'
void caminho_pasta(const char *caminho, char *dst, size_t tam);

/* --- Id das tarefas ------------------------------------------------------
 * O id é texto (ver ID_MAX em sched.h). Estas funções são as únicas que
 * sabem as regras dele; o resto do programa só copia, compara e mostra.
 * Nenhum valor de id é usado como sentinela ("0 = nenhuma", "-1 = não
 * achou"): quem procura uma tarefa devolve o ÍNDICE no vetor (-1 se não
 * achar), nunca um id. */

// Devolve NULL se 's' (já sem espaços nas pontas) serve como id, ou o motivo da recusa
const char *id_validar(const char *s);

// true se o id é um número inteiro (mesma regra do parse_int); o valor vai para '*v' (pode ser NULL)
bool id_eh_inteiro(const char *s, int *v);

// Copia o id já validado para 'dst'; um id inteiro fica na forma do %d ("01" e "+1" viram "1")
void id_normalizar(char *dst, size_t tam, const char *s);

// Ordem dos ids (< 0, 0 ou > 0, como strcmp): inteiros pelo valor e antes dos textos; textos em ordem natural, sem diferenciar maiúsculas
int id_comparar(const char *a, const char *b);

// true se 'a' e 'b' são o mesmo id para o programa (id_comparar == 0): "T1" = "t1", "01" = "1"
bool id_iguais(const char *a, const char *b);

// Rótulo do id no Gantt e nas listas: "T<id>" se todos os ids forem inteiros (T0, T1, T10), senão o id cru
void id_rotulo(const char *id, bool todos_inteiros, char *buf, size_t tam);

// Sugere em 'dst' um id para uma tarefa nova, que não repete nenhum dos 'n' ids de 'ids'
void id_sugerir(const char *const *ids, int n, char *dst, size_t tam);

/* --- Terminal ------------------------------------------------------------
 * Usamos sequências ANSI (cores 24-bit) em vez de ncurses para não exigir
 * bibliotecas extras. Quando a saída não é um terminal (ex.: redirecionada
 * para arquivo nos testes) ou o usuário pede --sem-cor, as cores são
 * desligadas e a saída fica em texto puro. */

extern bool g_usar_cores;  // Variável global booleana que habilita ou desabilita a emissão de códigos de cores ANSI no terminal

void term_fg(unsigned rgb);         // Emite a sequência de escape ANSI para alterar a cor do texto/fonte no terminal usando cor RGB 24-bit
void term_bg(unsigned rgb);         // Emite a sequência de escape ANSI para alterar a cor de fundo das células no terminal usando cor RGB 24-bit
void term_reset(void);              // Emite a sequência de escape ANSI para restaurar as cores e estilos padrão do terminal
void term_negrito(void);            // Emite a sequência de escape ANSI para ativar o estilo de texto em negrito no terminal
void term_fraco(void);              // Emite a sequência de escape ANSI para ativar o estilo de texto esmaecido/suave no terminal
void term_limpar(void);             // Emite a sequência de escape ANSI para limpar a tela do terminal (ativo apenas se g_usar_cores for true)
int  term_largura(void);            // Retorna a quantidade de colunas da janela do terminal (padrão de 100 colunas)

// Calcula a luminância da cor 'rgb' e retorna a cor de texto ideal (preto ou branco) para legibilidade no Gantt
unsigned cor_contraste(unsigned rgb);

// Avança o estado de 64 bits do gerador aleatório pseudo-determinístico e retorna um número de 32 bits
uint32_t rng_proximo(uint64_t *estado);

// Retorna um inteiro aleatório no intervalo [0, n-1] utilizando o estado explícito mantido no snapshot
int rng_intervalo(uint64_t *estado, int n);  

#endif
