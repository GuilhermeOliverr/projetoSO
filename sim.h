/*
 * sim.h - o simulador e as regras de edição de tarefas.
 *
 * Junta include/sim.h e include/edicao.h da versão organizada em pastas.
 * O TCB (antigo task.h) está em sched.h, incluído logo abaixo. Cada trecho
 * começa com um divisor e com o comentário de cabeçalho original.
 */
#ifndef SIM_H
#define SIM_H

#include "config.h"
#include "sched.h"

#include <stdbool.h>
#include <stdint.h>

/* ===========================================================================
 * Seção sim.h (originalmente include/sim.h)
 * =========================================================================== */
/*
 * sim.h - o simulador: relógio global, ativações, escalonamento por tick,
 * eventos, rastro para o Gantt e histórico para avançar/retroceder.
 *
 * Modelo de tempo: o tick t representa o intervalo [t, t+1). "Instante t"
 * é a fronteira esquerda do tick t. Chegadas acontecem no instante t (início
 * do tick); fim de ativação e perda de prazo são detectados no instante t+1
 * (fim do tick em que a tarefa executou).
 */

/* Proteção contra laço infinito ao "rodar até o fim" (só aconteceria com
 * edições muito estranhas; a simulação normal sempre termina). */
#define LIMITE_TICKS 2000000

// Início da enumeração dos tipos de eventos monitorados durante a simulação
typedef enum {
    EV_CHEGADA,        // 0: Evento de liberação/chegada de uma nova ativação da tarefa (sinalizado com ↓)
    EV_FIM_ATIVACAO,   // 1: Evento de término regular de uma ativação da tarefa (sinalizado com ↑)
    EV_TERMINO,        // 2: Evento de conclusão definitiva das 10 ativações periódicas (sinalizado com ■)
    EV_PRAZO_PERDIDO,  // 3: Evento de estouro do deadline relativo/absoluto (sinalizado com ✗ em vermelho)
    EV_SORTEIO,        // 4: Evento de aplicação do critério 5 de desempate por sorteio determinístico (sinalizado com ?)
    EV_PREEMPCAO,      // 5: Evento de preempção (tarefa é retirada da CPU para dar lugar a outra mais prioritária)
    EV_FIM_QUANTUM,    // 6: Evento de esgotamento da fatia de tempo (quantum) da tarefa em execução
    EV_DESCARTE,       // 7: Servidor de polling ganhou a CPU sem aperiódica na fila e perdeu a capacidade do período
    EV_RECARGA,        // 8: Sporadic Server recebeu de volta a capacidade gasta um período antes
    EV_QTD             // 9: Quantidade total de categorias de eventos definidas na enumeração
} TipoEvento;

// Início da estrutura que registra um evento ocorrido com seu contexto completo
typedef struct {
    int tick;         // Tick do relógio em que o evento foi gerado (usado para descartar eventos ao editar a simulação)
    int instante;      // Instante exato do evento para posicionamento correto dos marcadores gráficos no Gantt
    TipoEvento tipo;    // Categoria do evento ocorrido (EV_CHEGADA, EV_PRAZO_PERDIDO, etc.)
    int tarefa;        // Índice da tarefa associada ao evento dentro do vetor de TCBs
    int ativacao;      // Número da ativação corrente da tarefa no momento do evento (de 0 a 9)
    int cpu;          // Identificador da CPU envolvida na ocorrência do evento (-1 se ocorreu na fila de prontos)
} Evento;

// Enumeração dos estados visuais de uma célula da grade do Gantt (Vazio, Na Fila, Executando, Suspensa)
typedef enum { G_VAZIO, G_PRONTA, G_EXEC, G_SUSPENSA } CelulaGantt;

// Início da estrutura que armazena o estado instantâneo (Snapshot) do sistema a cada tick
typedef struct {
    int t;     // Valor do relógio global do sistema no instante do snapshot 
    int ncpus, quantum;     // Número de CPUs físicas ativas e valor do quantum de tempo configurados no tick 
    int alg;     // Índice do algoritmo de escalonamento ativo no registro de escalonadores (ex: RM ou EDF) 
    uint64_t rng;     // Estado interno de 64 bits do gerador de números aleatórios para manter o determinismo no sorteio 
    int n;     // Quantidade total de tarefas existentes no sistema neste instante 
    TCB *tarefas;     // Vetor contendo a cópia do estado de cada TCB no instante do snapshot 
    bool fim;     // Booleana que indica se todas as tarefas completaram suas 10 ativações
} Estado;

// Início da estrutura principal que encapsula o simulador completo
typedef struct {
    Estado atual;     // Estado instantâneo corrente da simulação no tick ativo (s->atual) 
    bool historico;     // Booleana que habilita ou desabilita a gravação do histórico de snapshots para o debugger/undo 
    Estado *snaps;     // Vetor dinâmico contendo a pilha de snapshots de todos os ticks passados (snaps[t]) 
    int nsnaps, capsnaps;     // Quantidade atual de snapshots gravados e capacidade alocada de memória da pilha 
    int largura;     // Quantidade de colunas (ticks) alocadas no buffer do rastro gráfico do Gantt 
    unsigned char *cel;     // Matriz achatada 2D guardando o estado de cada célula (tarefa x tick) para o Gantt 
    signed char *cpu;     // Matriz achatada 2D guardando qual CPU executou cada tarefa em cada tick (-1 se nenhuma) 
    unsigned char *ncpus_col;     // Vetor guardando a quantidade de CPUs existentes em cada tick (suporta edições dinâmicas) 
    int ncols, capcols;     // Quantidade de colunas (ticks) utilizadas e capacidade alocada das matrizes do rastro 
    Evento *ev;     // Vetor dinâmico com o histórico de todos os eventos registrados durante a simulação 
    int nev, capev;     // Quantidade atual de eventos armazenados e capacidade alocada do vetor de eventos 
    char erro[400];     // Por que o último sim_avancar parou ("" = não parou por erro): o escalonador não conseguiu calcular uma prioridade

} Simulador;

const char *evento_nome(TipoEvento t);    // Retorna o nome em texto descritivo do tipo de evento 't' (usado em logs e legendas)

void sim_iniciar(Simulador *s, const Config *cfg, bool historico, uint64_t semente);    // Inicializa o simulador 's' a partir das configurações 'cfg', aloca estruturas e grava o tick 0
void sim_liberar(Simulador *s);    // Libera toda a memória dinâmica alocada pelo simulador (snapshots, eventos, rastro do Gantt)

bool sim_avancar(Simulador *s);           // Avança 1 tick e salva o snapshot; false se já acabou ou se o escalonador falhou (motivo em s->erro, estado de antes do tick)
bool sim_voltar(Simulador *s);            // Retrocede a simulação em 1 tick restaurando o snapshot do instante anterior (função Undo)
int  sim_ir_para(Simulador *s, int t);    // Desloca o relógio da simulação diretamente para o tick 't' (avançando ou retrocedendo no histórico)

const char *sim_rodar_ate_fim(Simulador *s);    // Executa a simulação continuamente em laço até que todas as tarefas terminem; devolve o motivo se parar antes (limite de ticks, todas suspensas, falha do escalonador)

bool sim_bloqueado(const Estado *e);    // true se nenhuma tarefa restante progride sozinha: todas suspensas pelo usuário, ou aperiódicas sem servidor disponível

void sim_preparar_edicao(Simulador *s);    // Prepara o simulador para edições dinâmicas, preparando o histórico para possíveis truncamentos
void sim_concluir_edicao(Simulador *s);    // Finaliza o processo de edição dinâmica e trunca o histórico futuro a partir do tick editado

void sim_adicionar_tarefa(Simulador *s, const TarefaCfg *t);    // Adiciona uma nova tarefa dinamicamente durante a execução da simulação

void tcb_iniciar(TCB *k, const TarefaCfg *t);    // Inicializa a estrutura TCB 'k' com os parâmetros da configuração 't' para o tick 0

CelulaGantt sim_celula(const Simulador *s, int tick, int tarefa);    // Consulta e retorna o estado visual da célula (Vazio, Pronta, Executando, Suspensa) no Gantt
int sim_cpu_celula(const Simulador *s, int tick, int tarefa);    // Consulta e retorna o índice da CPU em que a 'tarefa' executou no 'tick' (-1 se nenhuma)

int sim_indice(const Estado *e, const char *id);    // Busca o índice da tarefa no vetor de TCBs a partir do seu 'id' (id_iguais); retorna o ÍNDICE, ou -1 se não encontrar

bool sim_ids_inteiros(const Estado *e);    // true se todos os ids do estado atual são inteiros (rótulos "T<id>"; ver id_rotulo)

double utilizacao_parcela(TipoTarefa tipo, TipoServidor servidor, int duracao, int periodo);    // C/P da tarefa na utilização, ou -1 se ela não entra na soma (aperiódica, esporádica, Background Server)
double sim_utilizacao(const Estado *e, int *n);    // Utilização total U = soma das parcelas (utilizacao_parcela); em 'n' (pode ser NULL) vai quantas tarefas entraram na soma
double sim_limite_rm(int n);    // Calcula o limite teórico de escalonabilidade do teste de Liu & Layland para RM: n*(2^(1/n) - 1)

void estado_copiar(Estado *dst, const Estado *src);    // Copia profundamente o estado 'src' para o estado 'dst' (alocando novos TCBs)
void estado_liberar(Estado *e);    // Libera a memória alocada para os TCBs internos da estrutura Estado
void hist_gravar(Simulador *s);              // Grava uma cópia do estado atual (s->atual) na pilha de histórico de snapshots (snaps[t])
void hist_truncar(Simulador *s, int t);      // Descarta todos os snapshots do histórico posteriores ao tick 't' após uma edição
void hist_carregar(Simulador *s, int t);     // Restaura o estado atual da simulação (s->atual) a partir do snapshot salvo em snaps[t]
void rastro_garantir_coluna(Simulador *s, int tick);    // Garante que as matrizes do rastro do Gantt tenham memória alocada suficiente para o 'tick'
void rastro_ajustar_largura(Simulador *s, int largura);    // Redimensiona a capacidade de colunas das matrizes do rastro do Gantt
void evento_add(Simulador *s, int tick, int instante, TipoEvento tipo, int tarefa, int ativ, int cpu);    // Registra e adiciona um novo evento ao histórico da simulação

/* ===========================================================================
 * Seção edicao.h (originalmente include/edicao.h)
 * =========================================================================== */
/*
 * edicao.h - regras para alterar tarefas, comuns às duas interfaces
 * (terminal em ui.c e janela em ui_gui.c).
 *
 * Validação: cada valida_* devolve NULL se o valor serve, ou o motivo da
 * recusa, para mostrar ao usuário (req. 3.1/3.4).
 *
 * Aplicação: cada edicao_* altera o TCB e ajusta o que depende do campo
 * (ex.: mudar o período recalcula a próxima chegada). Elas NÃO chamam
 * sim_preparar_edicao/sim_concluir_edicao; quem chama faz isso em volta,
 * depois de validar, para que uma edição recusada não descarte o futuro.
 */

/* FUNÇÕES DE VALIDAÇÃO (Retornam NULL se válido ou uma string de erro caso recuse) */

const char *valida_positivo(int v);  // Recebe um inteiro 'v' e verifica se é estritamente positivo (> 0). Usado para duração, período e prazo

const char *valida_periodo(int v);  // Valida se o novo período 'v' cumpre as regras do sistema (> 0)

const char *valida_ingresso(int v, int min);  // Garante que o novo instante de ingresso 'v' não seja menor que 'min' (tick atual da simulação)

const char *valida_id(const char *v, const char *const *ids, int n, int proprio);  // Verifica se o ID 'v' informado é único (id_iguais) entre os 'n' ids, ignorando a posição 'proprio' (-1 = tarefa nova)

/* FUNÇÕES DE APLICAÇÃO (Modificam o TCB e reajustam campos dependentes) */

void edicao_periodo(TCB *k, int v);  // Atualiza o período no TCB 'k' para 'v' e recalcula a data da próxima liberação da tarefa ( a próxima chegada passa a ser a última chegada + período)

void edicao_prazo(TCB *k, int v);  // Altera o prazo relativo para 'v' e ajusta imediatamente o deadline absoluto (deadline_abs) da ativação atual

void edicao_ingresso(TCB *k, int v);  // Altera o instante do primeiro ingresso da tarefa

void edicao_alternar_suspensao(TCB *k);  // Alterna a tarefa entre o estado suspenso e o normal (pausa/retoma a disputa pela CPU)

void edicao_encerrar(TCB *k, int t);  // Força o encerramento imediato da tarefa no tick 't' ajustando seu número de ativações para 10

#endif
