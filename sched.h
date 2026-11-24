/*
 * sched.h - TCB das tarefas e contrato dos escalonadores.
 *
 * Junta dois cabeçalhos da versão organizada em pastas, nesta ordem:
 * include/task.h (o TCB) e include/sched.h (o contrato dos escalonadores).
 * O TCB vem primeiro porque prioridade_fn recebe um `const TCB *`. Ele fica
 * aqui, e não em sim.h, porque o plugin (exemplo_fifo.c) só inclui sched.h
 * e sim.h já inclui sched.h: com o TCB em sim.h, sched.h teria de incluir
 * sim.h e os dois cabeçalhos se incluiriam um ao outro (inclusão circular).
 * Cada trecho começa com um divisor e com o comentário de cabeçalho original.
 */
#ifndef SCHED_H
#define SCHED_H

#include <stdbool.h>
#include <stddef.h>

/* ===========================================================================
 * Seção task.h (originalmente include/task.h)
 * =========================================================================== */
/*
 * task.h - TCB (Task Control Block): tudo o que o simulador sabe sobre uma
 * tarefa fica numa única estrutura.
 *
 * Decisão: o TCB não tem ponteiros para memória própria (só `eventos_raw` e
 * `eventos`, que apontam para dados imutáveis da configuração). Assim um snapshot do
 * sistema é só uma cópia de um vetor de TCBs com memcpy, o que deixa o
 * avançar/retroceder simples e barato.
 */

/*toda tarefa periódica "termina" após 10 ativações. */
#define MAX_ATIVACOES 10

/* Tipo da tarefa (Projeto B, req. 1 e 2). Sai do arquivo de configuração:
 * o sinal do período separa periódica, aperiódica e esporádica, e o prefixo
 * do id separa os servidores (config_classificar, em config.c). */
typedef enum {
    TIPO_PERIODICA,   // período > 0: uma ativação por período, 10 ao todo (req. 4.4 do Projeto A)
    TIPO_APERIODICA,  // período = 0 (req. B 1.2): chega nos instantes de lista_eventos, ou só no ingresso
    TIPO_ESPORADICA,  // período < 0 (req. B 1.3): |período| = intervalo mínimo entre duas ocorrências
    TIPO_SERVIDOR     // id BSxx, PSxx ou DSxx (req. B 2): tarefa que executa as aperiódicas e esporádicas
} TipoTarefa;

/* Qual servidor (req. B 2). Os campos da linha de configuração continuam
 * com o mesmo sentido: duracao = capacidade, periodo = período do servidor,
 * prazo = prazo de cada período. */
typedef enum {
    SERV_BS,  // Background Server: executa só quando uma CPU ficaria desligada
    SERV_PS,  // Polling Server: recarrega a capacidade a cada período; se ganha a CPU sem aperiódica na fila, perde a capacidade até o próximo
    SERV_DS   // Deferrable Server: recarrega a cada período, mas guarda a capacidade até aparecer uma aperiódica
} TipoServidor;

/* Tamanho do id da tarefa contando o '\0' final: até ID_MAX-1 = 31 bytes.
 *
 * Por que texto: o enunciado (req. 3.3) só diz que o id é "o identificador
 * único da tarefa", sem fixar o tipo, e o arquivo do professor pode trazer
 * 0, 1, 01, T0, t1, A ou Tarefa_2. Nenhum desses pode ser recusado.
 *
 * Por que um vetor de tamanho fixo e não `char *`: o TCB é copiado por
 * atribuição/memcpy em cada snapshot do histórico (estado_copiar e
 * hist_gravar, em sim.c). Com o texto dentro do próprio TCB a cópia continua
 * sendo só uma cópia de bytes. Com um ponteiro, cada cópia precisaria de
 * strdup e cada snapshot descartado de um free, com risco de double free
 * (duas cópias apontando para o mesmo texto) e de vazamento. São 32 bytes
 * por tarefa e por snapshot, desprezível aqui.
 *
 * Fica aqui, junto do TCB, porque o plugin (exemplo_fifo.c) só inclui
 * sched.h e precisa enxergar o TCB inteiro; config.h inclui este arquivo
 * para usar o mesmo valor em TarefaCfg. Mudar o tamanho muda a posição dos
 * campos do TCB, então plugins .so compilados antes precisam ser
 * recompilados (make plugins). */
#define ID_MAX 32

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
    SUSP_USUARIO,  // Motivo 2: Suspensa manualmente pelo usuário no passo a passo (no Gantt aparece em preto pontilhado)
    SUSP_SEM_TRABALHO,   // Motivo 3: Servidor sem aperiódica para executar (a fila está vazia; no Gantt aparece em branco)
    SUSP_SEM_CAPACIDADE  // Motivo 4: Servidor com a capacidade esgotada, esperando a recarga (no Gantt aparece em branco)
} MotivoSuspensao;

// Início da estrutura principal TCB (Task Control Block) que guarda todo o contexto da tarefa
typedef struct {

    /* ---- definidos no arquivo de configuração ---- */
    char id[ID_MAX];           // Identificador único da tarefa, em texto (ex.: "1", "T0", "A"); ver ID_MAX acima
    unsigned cor;              // Valor hexadecimal da cor RGB usada para renderizar a tarefa no Gráfico de Gantt
    int ingresso;              // Instante de relógio (tick) em que a tarefa faz seu primeiro ingresso na simulação
    int duracao;               // Quantidade de ticks de CPU necessários para completar cada ativação (C_i)
    int periodo;               // Intervalo regular de tempo em ticks entre liberações consecutivas da tarefa (P_i)
    int prazo;                 // Prazo relativo em ticks para conclusão de cada ativação (D_i)
    const char *eventos_raw;   // Ponteiro para a string bruta contendo a lista de eventos associada à tarefa
    const char *const *eventos;// Vetor dinâmico de ponteiros para as strings de eventos individuais sem interpretação
    int neventos;              // Quantidade total de eventos armazenados no vetor de eventos
    TipoTarefa tipo;           // Periódica, aperiódica, esporádica ou servidor (ver TipoTarefa)
    TipoServidor servidor;     // Qual servidor, quando tipo == TIPO_SERVIDOR
    const int *ocorrencias;    // Aperiódica/esporádica: instantes de chegada, relativos ao ingresso (dados imutáveis da configuração, como `eventos`)
    int nocorrencias;          // Quantidade de ocorrências (= total de ativações da aperiódica/esporádica)

    /* ---- estado dinâmico ---- */
    EstadoTarefa estado;        // Estado atual de execução da tarefa no ciclo de vida do SO (T_PRONTA, T_EXECUTANDO, etc.)
    MotivoSuspensao motivo;    // Razão da suspensão da tarefa quando estado == T_SUSPENSA (SUSP_PERIODO ou SUSP_USUARIO)
    int liberadas;             // Contador da quantidade de ativações que já foram liberadas/chegaram no sistema
    int concluidas;            // Contador da quantidade de ativações que já foram totalmente concluídas (máximo 10)
    int chegadas[MAX_ATIVACOES]; // Instante de chegada de cada ativação; não leia direto, use tcb_chegada()
    int proxima_chegada;       // Instante do relógio global em que ocorrerá a liberação da próxima ativação //do periodo da tarefa (chegada do próximo job)
    int restante;              // Quantidade de ticks de CPU que ainda faltam ser executados para concluir a ativação atual
    int chegada_atual;         // Instante do relógio em que a ativação atualmente em andamento foi liberada
    int deadline_abs;          // Instante limite absoluto do relógio para o término da ativação atual (chegada_atual + prazo)
    /* perdas_ate: as ativações pendentes de número MENOR que este já foram
     * marcadas como "prazo perdido". Basta um número (e não um bit por
     * ativação) porque as chegadas são crescentes e o prazo relativo é o
     * mesmo para todas: entre as ativações pendentes, as que já venceram
     * formam sempre um começo da fila. Antes era uma máscara de 32 bits, que
     * limitava a 32 ativações; uma tarefa esporádica (Projeto B) pode ter
     * qualquer número de ocorrências. */
    int perdas_ate;
    int cpu;                   // Índice do processador em que a tarefa está executando no tick atual (-1 se não estiver na CPU)
    int quantum_usado;         // Quantidade de ticks contínuos que a tarefa já executou no quantum corrente
    /* fila_desde: instante em que a tarefa entrou no FIM da fila de prontas.
     * Recebe o instante atual quando uma ativação começa a disputar a CPU e
     * quando a tarefa esgota o quantum; NÃO muda quando ela é preemptada por
     * outra mais prioritária (mantém o lugar na fila, como no Round Robin com
     * prioridades). Existe para o quantum ser "específico de cada
     * escalonador" (resposta do professor): quem quiser usá-lo põe
     * fila_desde na prioridade (Round Robin = "prioridade: fila_desde"). Vai
     * pela PRIORIDADE, e não pelo desempate, de propósito: a cadeia do 4.3
     * (critério 1 = quem já estava na CPU) continua igual para todos os
     * algoritmos. RM e EDF não leem este campo, então nada muda neles. */
    int fila_desde;

    /* Vínculo entre servidor e aperiódica (req. B 2.2): o servidor que tira
     * uma aperiódica (ou esporádica) da fila fica com ela até a ativação
     * acabar ou a capacidade dele esgotar, e nenhum outro servidor a vê.
     * São índices no vetor de tarefas, e não ponteiros, para o TCB continuar
     * sendo copiado com memcpy nos snapshots. */
    int atendendo;             // Servidor: índice da aperiódica/esporádica que ele está executando (-1 = nenhuma)
    int atendida_por;          // Aperiódica/esporádica: índice do servidor que a tirou da fila (-1 = está na fila)
    int capacidade;            // Servidor: ticks de CPU que ainda pode usar até a próxima recarga (o BS não tem limite e não usa)

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

bool ativacao_atual_perdeu_prazo(const TCB *k);    // true se a ativação em andamento (a de número `concluidas`) já foi marcada como prazo perdido

int tcb_total_ativacoes(const TCB *k);    // Quantas ativações a tarefa tem ao todo; quando chega a esse número de concluídas, ela termina
int tcb_chegada(const TCB *k, int j);     // Instante em que chegou a ativação j (de 0 a liberadas-1)

bool tcb_eh_servidor(const TCB *k);       // true se a tarefa é um servidor (BSxx, PSxx ou DSxx)
bool tcb_eh_atendida(const TCB *k);       // true se é aperiódica ou esporádica: só executa por meio de um servidor (req. B 1.1)
const char *tipo_nome(const TCB *k);      // Nome do tipo para as telas: "periódica", "aperiódica", "esporádica" ou "servidor BS"
void tcb_ativacoes_txt(const TCB *k, char *buf, size_t tam);    // "concluídas/total" para as tabelas; "-" num servidor, que não tem ativações contadas

/* ===========================================================================
 * Seção sched.h (originalmente include/sched.h)
 * =========================================================================== */
/*
 * sched.h - contrato dos escalonadores ("plugins").
 *
 * Requisito 4.2: deve ser fácil incluir algoritmos novos sem mexer na
 * simulação. Por isso um escalonador aqui é só uma função que diz a
 * PRIORIDADE NOMINAL de uma tarefa (um número; menor = mais prioritária).
 * A simulação (sim.c) é quem monta a fila global, aplica a cadeia de
 * desempate comum (sched.c, seção sched_tie.c) e entrega as M tarefas de maior prioridade
 * para as M CPUs.
 *
 * Por que um número e não "devolva a próxima tarefa"? Com várias CPUs
 * precisamos das M melhores, não só da melhor, e o desempate (critérios 1 a
 * 5 do enunciado) é igual para todos os algoritmos. E, sendo um número, a
 * simulação consegue separar a prioridade nominal (do algoritmo) da ativa
 * (a comparada de fato).
 *
 * Há três formas de adicionar um algoritmo, todas com o mesmo contrato
 * (devolver a prioridade nominal; o desempate é sempre o comum):
 *   1. NATIVO: escreva uma função prioridade_fn e um `const Escalonador` em
 *      sched.c (como RM e EDF) e acrescente-o à lista `nativos` da seção
 *      sched_registro.c. Exige recompilar; serve para o que faz parte do
 *      programa;
 *   2. PLUGIN .so/.dylib: compile a mesma coisa como biblioteca dinâmica
 *      (ver exemplo_fifo.c) e carregue com `--plugin arquivo.so`. Não
 *      recompila o simulador, mas precisa de compilador e só abre no mesmo
 *      sistema e processador em que foi compilado (um .so de Linux x86-64
 *      não abre num Mac arm64). Serve para algoritmos que uma fórmula não
 *      expressa;
 *   3. ARQUIVO .txt com a fórmula da prioridade (seção sched_formula.c):
 *          nome: LLF
 *          prioridade: deadline_abs - agora - restante
 *      É interpretado pelo programa: sem compilador, igual em qualquer
 *      sistema e processador, e sem executar código de fora. Carrega com
 *      `--plugin LLF.txt`, pelo botão "Carregar escalonador..." da janela,
 *      pelo comando p do terminal ou sozinho, quando o arquivo de
 *      configuração pede LLF e há um LLF.txt na mesma pasta (pendrive).
 *      É a forma mais fácil e portátil, para qualquer algoritmo cuja
 *      prioridade seja uma conta sobre os campos da tarefa.
 * Os carregados (.so e .txt) entram sempre no fim da tabela e nunca saem:
 * RM continua 0, EDF continua 1, e os índices guardados na configuração e
 * no histórico continuam valendo.
 */

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

/* ---- registro (sched.c, seção sched_registro.c) ---- */
int sched_quantidade(void);    // Retorna o número total de escalonadores registrados na tabela do simulador
const Escalonador *sched_obter(int idx);    // Retorna um ponteiro para a estrutura do escalonador no índice 'idx' da tabela
int sched_buscar(const char *nome);          // Procura um algoritmo registrado pelo nome (sem diferenciar maiúsculas/minúsculas) e retorna seu índice ou -1
void sched_listar_nomes(char *buf, int tam);      // Formata o buffer 'buf' com a lista dos nomes registrados (ex: "RM, EDF") para exibição em mensagens

// Carrega um escalonador de um arquivo, pela extensão (sem diferenciar maiúsculas): .txt = fórmula de prioridade; .so/.dylib = plugin compilado.
// Devolve o índice na tabela, ou -1 com TODOS os problemas em 'erro' (um por linha, "arquivo:linha: motivo"). Se falhar, a tabela não muda.
int sched_carregar_arquivo(const char *caminho, char *erro, int tam_erro);

// Igual a sched_carregar_arquivo, mas o escalonador do arquivo precisa se chamar 'nome' (carga automática pelo arquivo de configuração)
int sched_carregar_arquivo_esperado(const char *caminho, const char *nome, char *erro, int tam_erro);

// Calcula a prioridade nominal da tarefa 'k' com o algoritmo 'e' e copia para a prioridade ativa. Devolve NULL se deu certo, ou o motivo
// (escrito em 'erro') se o escalonador não conseguiu calcular (ex.: divisão por zero numa fórmula); aí a prioridade da tarefa não muda.
const char *sched_atualizar(const Escalonador *e, TCB *k, int agora, char *erro, size_t tam);

int sched_desempate(const TCB *a, const TCB *b);    // Compara as tarefas 'a' e 'b' aplicando estritamente os critérios de 1 a 4 da cadeia de desempate (retorna < 0 se 'a' vem antes, > 0 se depois)

int sched_comparar(const TCB *a, const TCB *b);    // Compara duas tarefas considerando a prioridade ativa e, em caso de empate, aplicando a cadeia de desempate

#endif
