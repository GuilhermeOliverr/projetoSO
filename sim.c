/*
 * sim.c - a simulação, o histórico (avançar/retroceder) e as regras de
 * edição de tarefas.
 *
 * Junta, nesta ordem, src/core/sim.c, src/core/history.c e
 * src/core/edicao.c da versão organizada em pastas. Cada trecho começa com
 * um divisor e com o comentário de cabeçalho original. Os #include de
 * todos os trechos ficam juntos logo abaixo.
 */
#include "sim.h"
#include "admissao.h"
#include "config.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Seção sim.c (originalmente src/core/sim.c)
 * =========================================================================== */
/*
 * sim.c - ciclo de simulação (ver sim.h para o modelo de tempo).
 *
 * É AQUI que o "sistema operacional simulado" acontece. Tudo o que o resto do
 * programa faz (ler arquivo, desenhar Gantt, menus) existe para alimentar ou
 * mostrar o que este arquivo calcula.
 *
 * A ideia central: o tempo anda em "ticks" (batidas de relógio). A cada tick
 * executamos SEMPRE os mesmos 5 passos, nesta ordem:
 *   1. chegadas: tarefas cuja próxima ativação vence em t entram na fila;
 *   2. escalonamento: escolhe as M tarefas de maior prioridade (M = CPUs)
 *      entre as prontas e as que estavam executando; as demais ficam (ou
 *      voltam) para a fila de prontos (preempção);
 *   3. execução: cada CPU ocupada consome 1 tick da sua tarefa; CPU sem
 *      tarefa fica desligada neste tick;
 *   4. verificações no instante t+1: fim de ativação, término (10ª
 *      ativação) e perda de prazo;
 *   5. t++ e grava o snapshot.
 *
 * Vocabulário que aparece o tempo todo:
 *   - tarefa: um programa periódico (ex.: "rode 20 ticks a cada 100"), ou,
 *     no Projeto B, aperiódico/esporádico (chega nos instantes da sua
 *     lista_eventos) ou um servidor (executa as aperiódicas);
 *   - ativação (job): cada vez que a tarefa "acorda" para trabalhar. Uma
 *     tarefa periódica tem uma ativação por período; no Projeto A são 10.
 *   - TCB: a ficha da tarefa (sched.h, seção task.h). Tudo sobre ela está lá.
 *   - tick t: o intervalo de tempo [t, t+1).
 *   - fila de aperiódicas: as aperiódicas e esporádicas que chegaram e
 *     esperam um servidor. Elas nunca disputam a CPU diretamente (req. B
 *     1.1): o servidor disputa, e executa a aperiódica que tirou da fila.
 */

/* =========================================================================
 * 1. GERENCIAMENTO DE EVENTOS E SNAPSHOTS DA SIMULAÇÃO
 * ========================================================================= */

//Registra um evento ocorrido (chegada, fim de ativação, perda de prazo, sorteio ou fim de quantum) 
//no vetor dinâmico de eventos da simulação. Dobra a capacidade do vetor se ele ficar cheio.
const char *estado_nome(EstadoTarefa e) {
    switch (e) {
    case T_NOVA: return "nova (não chegou)";
    case T_PRONTA: return "pronta";
    case T_EXECUTANDO: return "executando";
    case T_SUSPENSA: return "suspensa";
    case T_TERMINADA: return "terminada";
    }
    return "?";
}

/* Igual a estado_nome, mas quando a tarefa está suspensa acrescenta o
 * motivo. É o que o comando `t id` mostra. */
const char *estado_descricao(const TCB *k) {
    if (k->estado == T_TERMINADA && k->recusada) return "recusada (teste de admissão)";
    if (k->estado != T_SUSPENSA) return estado_nome(k->estado);
    switch (k->motivo) {
    case SUSP_PERIODO: return tcb_eh_atendida(k) ? "suspensa: próxima ocorrência" : "suspensa: próximo período";
    case SUSP_USUARIO: return "suspensa: pelo usuário";
    case SUSP_SEM_TRABALHO: return "suspensa: sem aperiódica na fila";
    case SUSP_SEM_CAPACIDADE: return "suspensa: capacidade esgotada";
    default: return "suspensa";
    }
}

/* Só a suspensão feita pelo usuário "trava" a tarefa de verdade. A espera
 * pelo próximo período acaba sozinha, então não conta como travada. Muita
 * lógica abaixo depende desta distinção. */
bool suspensa_pelo_usuario(const TCB *k) {
    return k->estado == T_SUSPENSA && k->motivo == SUSP_USUARIO;
}

/* A ativação em andamento é a de número `concluidas`; ela já perdeu o prazo
 * se perdas_ate passou dela (ver perdas_ate em sched.h). */
bool ativacao_atual_perdeu_prazo(const TCB *k) {
    return k->restante > 0 && k->perdas_ate > k->concluidas;
}

bool tcb_eh_servidor(const TCB *k) {
    return k->tipo == TIPO_SERVIDOR;
}

bool tcb_eh_atendida(const TCB *k) {
    return k->tipo == TIPO_APERIODICA || k->tipo == TIPO_ESPORADICA;
}

const char *tipo_nome(const TCB *k) {
    switch (k->tipo) {
    case TIPO_APERIODICA: return "aperiódica";
    case TIPO_ESPORADICA: return "esporádica";
    case TIPO_SERVIDOR:
        switch (k->servidor) {
        case SERV_BS: return "servidor BS";
        case SERV_PS: return "servidor PS";
        case SERV_DS: return "servidor DS";
        case SERV_SS: return "servidor SS";
        }
        return "servidor";
    default: return "periódica";
    }
}

/* Quantas ativações a tarefa tem ao todo. Toda consulta ao "fim" da tarefa
 * passa por aqui, e não pela constante, porque o total depende do tipo:
 *   - periódica: MAX_ATIVACOES (req. 4.4 do Projeto A);
 *   - aperiódica e esporádica: uma por ocorrência da lista_eventos, sem
 *     limite (req. 3.3.5: "inúmeros eventos");
 *   - servidor: não termina pela contagem, e sim junto com a simulação
 *     (ver atualizar_fim); INT_MAX nunca é alcançado. */
int tcb_total_ativacoes(const TCB *k) {
    switch (k->tipo) {
    case TIPO_APERIODICA:
    case TIPO_ESPORADICA: return k->nocorrencias;
    case TIPO_SERVIDOR: return INT_MAX;
    default: return MAX_ATIVACOES;
    }
}

/* Instante em que chegou a ativação j, que já precisa ter sido liberada
 * (0 <= j < liberadas). A da aperiódica/esporádica sai da lista de
 * ocorrências, relativa ao ingresso, e por isso não tem limite de
 * ativações; a da periódica fica no vetor chegadas (são no máximo
 * MAX_ATIVACOES). O servidor só guarda o início do período atual. */
int tcb_chegada(const TCB *k, int j) {
    if (tcb_eh_atendida(k)) return k->ingresso + k->ocorrencias[j];
    if (tcb_eh_servidor(k)) return k->chegada_atual;
    return k->chegadas[j];
}

const char *admissao_nome(ResultadoAdmissao r) {
    switch (r) {
    case ADM_ACEITA: return "aceita";
    case ADM_SEM_GARANTIA: return "aceita sem garantia";
    default: return "recusada";
    }
}

/* Coluna "ativ." das tabelas: concluídas/total. O servidor não tem
 * ativações próprias para contar, e a tarefa recusada no ingresso não
 * chegou a ter nenhuma. */
void tcb_ativacoes_txt(const TCB *k, char *buf, size_t tam) {
    if (tcb_eh_servidor(k) || k->recusada) snprintf(buf, tam, "-");
    else snprintf(buf, tam, "%d/%d", k->concluidas, tcb_total_ativacoes(k));
}

/* Nome de cada tipo de evento, para as listas de eventos do passo a passo. */
const char *evento_nome(TipoEvento t) {
    switch (t) {
    case EV_CHEGADA: return "chegada";
    case EV_FIM_ATIVACAO: return "fim de ativação";
    case EV_TERMINO: return "término";
    case EV_PRAZO_PERDIDO: return "perda de prazo";
    case EV_SORTEIO: return "sorteio";
    case EV_PREEMPCAO: return "preempção";
    case EV_FIM_QUANTUM: return "fim de quantum";
    case EV_DESCARTE: return "capacidade descartada";
    case EV_RECARGA: return "recarga";
    case EV_RECUSA: return "recusada pelo teste de admissão";
    default: return "?";
    }
}

/* Monta o TCB inicial de uma tarefa a partir do que veio da configuração.
 * O memset zera TUDO primeiro (contadores, bits de perda, estatísticas), e
 * depois só preenchemos o que não é zero. */
void tcb_iniciar(TCB *k, const TarefaCfg *t) {
    memset(k, 0, sizeof *k); //zerar toda a memória da estrutura ou objeto apontado pelo ponteiro k
    /* O id é um vetor fixo dentro do TCB (ver ID_MAX em sched.h); snprintf
     * corta no tamanho do vetor e sempre coloca o '\0'. */
    snprintf(k->id, sizeof k->id, "%s", t->id);
    k->cor = t->cor;
    k->ingresso = t->ingresso;
    k->duracao = t->duracao;
    k->periodo = t->periodo;
    k->prazo = t->prazo;
    k->eventos_raw = t->eventos;
    k->eventos = (const char *const *)t->lista_ev;
    k->neventos = t->nev;
    k->tipo = t->tipo;
    k->servidor = t->servidor;
    k->ocorrencias = t->ocorr;     /* aponta para a configuração, como `eventos` */
    k->nocorrencias = t->nocorr;
    k->estado = T_NOVA;            /* ainda não chegou ao sistema             */
    k->proxima_chegada = t->ingresso; /* a 1ª ativação chega no ingresso      */
    /* ... ou, na aperiódica/esporádica, na 1ª ocorrência da lista */
    if (tcb_eh_atendida(k) && k->nocorrencias > 0) k->proxima_chegada = t->ingresso + k->ocorrencias[0];
    k->cpu = -1;                   /* -1 = não está em nenhuma CPU            */
    k->atendendo = -1;             /* servidor sem aperiódica                 */
    k->atendida_por = -1;          /* aperiódica na fila (quando chegar)      */
    k->ss_ativo_desde = -1;        /* Sporadic Server ocioso                  */
    k->termino = -1;               /* -1 = ainda não terminou                 */
}

/* A simulação acaba quando TODAS as tarefas estão terminadas. Os servidores
 * não contam: não têm um número de ativações e só existem para executar as
 * aperiódicas; terminam junto com o resto (atualizar_fim). */
static bool todas_terminadas(const Estado *e) {
    for (int i = 0; i < e->n; i++)
        if (!tcb_eh_servidor(&e->tarefas[i]) && e->tarefas[i].estado != T_TERMINADA) return false;
    return true;
}

/* Recalcula e->fim e, se acabou, encerra os servidores no instante atual. */
static void atualizar_fim(Estado *e) {
    e->fim = todas_terminadas(e);
    if (!e->fim) return;
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->estado == T_TERMINADA) continue;
        k->estado = T_TERMINADA;
        k->motivo = SUSP_NENHUM;
        k->cpu = -1;
        k->atendendo = -1;
        k->termino = e->t;
    }
}

/* =========================================================================
 * 2. INICIALIZAÇÃO E VERIFICAÇÃO DO ESTADO DA SIMULAÇÃO
 * ========================================================================= */

//Inicializa a estrutura do simulador no tick 0, carrega o algoritmo de escalonamento, 
//aloca a matriz do Gráfico de Gantt e salva o primeiro snapshot de memória.
void sim_iniciar(Simulador *s, const Config *cfg, bool historico, uint64_t semente, bool admissao) {
    memset(s, 0, sizeof *s);
    s->historico = historico;
    Estado *e = &s->atual;
    e->admissao = admissao;
    e->ncpus = cfg->ncpus;
    e->quantum = cfg->quantum;
    e->alg = cfg->algoritmo;
    e->rng = semente ? semente : 0x5EED5EEDull;
    e->n = cfg->n;
    /* `cfg->n ? cfg->n : 1`: calloc/malloc de tamanho 0 pode devolver NULL;
     * pedir ao menos 1 evita tratar esse caso em todo lugar. */
    e->tarefas = xcalloc((size_t)(cfg->n ? cfg->n : 1), sizeof(TCB));
    for (int i = 0; i < cfg->n; i++) tcb_iniciar(&e->tarefas[i], &cfg->tarefas[i]);
    atualizar_fim(e);
    s->largura = cfg->n;   /* colunas do rastro do Gantt = número de tarefas */
    hist_gravar(s);        /* o instante 0 também é um snapshot              */
}

/* Libera tudo que o simulador alocou. O memset final deixa a estrutura
 * zerada para que uma segunda chamada de sim_liberar não libere duas vezes. */
void sim_liberar(Simulador *s) {
    estado_liberar(&s->atual);
    for (int k = 0; k < s->nsnaps; k++) estado_liberar(&s->snaps[k]);
    free(s->snaps);
    free(s->cel);
    free(s->cpu);
    free(s->ncpus_col);
    free(s->ev);
    free(s->adm);
    memset(s, 0, sizeof *s);
}

/* Começa a próxima ativação pendente (a de número `concluidas`, contando de
 * 0). Se a tarefa já terminou 3 ativações, a próxima é a de índice 3, e a
 * hora em que ela chegou é tcb_chegada(k, 3).
 *
 * O prazo (deadline absoluto) é a chegada DESTA ativação + o prazo relativo
 * do arquivo. Ex.: chegou em 30, prazo 10 -> precisa terminar até o instante 40.
 *
 * `agora` é o instante em que a ativação passa a disputar a CPU: o da chegada
 * (passo 1 do tick) ou, se ela estava acumulada atrás da anterior, o fim do
 * tick em que a anterior terminou. É quando a ativação entra no fim da fila
 * de prontas, por isso vira o fila_desde (ver sched.h). */
static void iniciar_ativacao(TCB *k, int agora) {
    k->restante = k->duracao;
    k->chegada_atual = tcb_chegada(k, k->concluidas);
    k->deadline_abs = k->chegada_atual + k->prazo;
    k->quantum_usado = 0;
    k->fila_desde = agora;
}

/* ------------------------------------------------------------- chegadas */

/* Guarda uma decisão do teste de admissão, para as telas mostrarem o motivo. */
static void registrar_admissao(Simulador *s, int tick, int tarefa, int ativacao, const Admissao *a) {
    if (s->nadm == s->capadm) {
        s->capadm = s->capadm ? s->capadm * 2 : 32;
        s->adm = xrealloc(s->adm, (size_t)s->capadm * sizeof *s->adm);
    }
    RegistroAdmissao *r = &s->adm[s->nadm++];
    r->tick = tick;
    r->tarefa = tarefa;
    r->ativacao = ativacao;
    r->resultado = a->resultado;
    snprintf(r->motivo, sizeof r->motivo, "%s", a->motivo);
}

/* Criação das tarefas (req. B 3): cada tarefa que ingressa agora passa
 * pelo teste de admissão antes de entrar no sistema. Recusada, ela termina
 * ali mesmo, sem executar. No mesmo instante, primeiro as periódicas e os
 * servidores (a carga que os testes analisam), depois as esporádicas e por
 * fim as aperiódicas, que dependem dos servidores; dentro de cada grupo, na
 * ordem do arquivo: quem pede para entrar depois é quem fica de fora. */
static void admitir_novas(Simulador *s, int t) {
    Estado *e = &s->atual;
    for (int fase = 0; fase < 3; fase++)
        for (int i = 0; i < e->n; i++) {
            TCB *k = &e->tarefas[i];
            if (k->admitida || k->estado == T_TERMINADA || t < k->ingresso) continue;
            int f = k->tipo == TIPO_ESPORADICA ? 1 : k->tipo == TIPO_APERIODICA ? 2 : 0;
            if (f != fase) continue;
            Admissao a = admissao_tarefa(e, i);
            if (e->admissao || a.resultado == ADM_RECUSADA) registrar_admissao(s, t, i, -1, &a);
            if (a.resultado != ADM_RECUSADA) {
                k->admitida = true;
                continue;
            }
            k->estado = T_TERMINADA;
            k->recusada = true;
            k->motivo = SUSP_NENHUM;
            k->termino = t;
            evento_add(s, t, t, EV_RECUSA, i, 0, -1);
        }
}

/* Começa a próxima ativação pendente no instante `agora` (no passo 1, na
 * chegada; no passo 4, logo depois de a anterior acabar). Numa aperiódica
 * com prazo, a ativação é uma tarefa nova criada durante a execução e passa
 * pelo teste de admissão ao entrar na fila (req. B 3). Recusada, ela conta
 * como concluída sem executar, e a próxima pendente (se houver) é testada no
 * lugar. No fim, ou há uma ativação em andamento (restante > 0) e quem chama
 * a põe na fila, ou a tarefa ficou esperando a próxima ocorrência, ou
 * terminou. */
static void comecar_ativacao(Simulador *s, int i, int tick, int agora) {
    Estado *e = &s->atual;
    TCB *k = &e->tarefas[i];
    while (k->liberadas > k->concluidas) {
        iniciar_ativacao(k, agora);
        if (!e->admissao || k->tipo != TIPO_APERIODICA || k->prazo <= 0) return;
        Admissao a = admissao_ativacao(e, i, agora);
        registrar_admissao(s, tick, i, k->concluidas, &a);
        if (a.resultado != ADM_RECUSADA) return;
        evento_add(s, tick, agora, EV_RECUSA, i, k->concluidas, -1);
        k->restante = 0;
        k->concluidas++;
        k->recusas++;
    }
    if (k->concluidas >= tcb_total_ativacoes(k)) {
        k->estado = T_TERMINADA;
        k->termino = agora;
        k->cpu = -1;
        evento_add(s, tick, agora, EV_TERMINO, i, k->concluidas - 1, -1);
    } else if (!suspensa_pelo_usuario(k)) {
        k->estado = T_SUSPENSA;
        k->motivo = SUSP_PERIODO;
    }
}

/* Libera a próxima ativação da tarefa i no instante t (passo 1 do tick). */
static void liberar_ativacao(Simulador *s, int i, int t) {
    TCB *k = &s->atual.tarefas[i];
    if (k->tipo == TIPO_PERIODICA) k->chegadas[k->liberadas] = t;   /* anota a hora da chegada */
    evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
    k->liberadas++;
    if (tcb_eh_atendida(k)) {
        /* A próxima vem da lista de ocorrências, relativa ao ingresso. */
        if (k->liberadas < k->nocorrencias) k->proxima_chegada = k->ingresso + k->ocorrencias[k->liberadas];
    } else {
        k->proxima_chegada += k->periodo;
        /* Se a edição fez a próxima chegada ficar no passado, reancora no
         * tick atual para não chegar atrasadas. */
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;
    }
    /* Se a ativação anterior ainda não terminou, esta fica acumulada e
     * começa assim que a anterior acabar */
    if (k->restante == 0) {
        comecar_ativacao(s, i, t, t);
        /* Suspensa pelo usuário continua suspensa (a ativação fica
         * esperando a retomada); a espera pelo período acabou. Uma
         * aperiódica pronta é uma aperiódica na fila dos servidores. */
        if (k->restante > 0 && !suspensa_pelo_usuario(k)) {
            k->estado = T_PRONTA;
            k->motivo = SUSP_NENHUM;
        }
    }
}

/* O servidor tem um limite de capacidade por período? Só o Background
 * Server não tem: ele usa toda CPU que sobra. */
static bool capacidade_limitada(const TCB *k) {
    return k->servidor != SERV_BS;
}

/* Sporadic Server: devolve a capacidade das recargas que venceram. */
static void aplicar_recargas(Simulador *s, int i, int t) {
    TCB *k = &s->atual.tarefas[i];
    int j = 0;
    while (j < k->nrecargas && k->recargas[j].instante <= t) {
        k->capacidade += k->recargas[j].quantidade;
        if (k->capacidade > k->duracao) k->capacidade = k->duracao;
        evento_add(s, t, t, EV_RECARGA, i, k->liberadas - 1, -1);
        j++;
    }
    if (j == 0) return;
    memmove(k->recargas, k->recargas + j, (size_t)(k->nrecargas - j) * sizeof k->recargas[0]);
    k->nrecargas -= j;
}

/* Sporadic Server: ficou ativo no instante t (passou a ter capacidade e
 * aperiódica para executar). A capacidade que ele gastar daqui em diante
 * volta um período depois de t, e no EDF o prazo dele é t + prazo. É uma
 * "ativação" do servidor, por isso vira uma chegada no Gantt. */
static void ss_ativar(Simulador *s, int i, int t) {
    TCB *k = &s->atual.tarefas[i];
    k->ss_ativo_desde = t;
    k->ss_consumo = 0;
    k->chegada_atual = t;
    k->deadline_abs = t + k->prazo;
    evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
    k->liberadas++;
}

/* Sporadic Server: ficou ocioso (sem aperiódica ou sem capacidade). Agenda
 * a volta do que gastou desde que ficou ativo para um período depois de
 * ficar ativo (ou agora, se esse instante já passou). */
static void ss_desativar(TCB *k, int t) {
    if (k->ss_ativo_desde < 0) return;
    if (k->ss_consumo > 0) {
        int quando = k->ss_ativo_desde + k->periodo;
        if (quando < t) quando = t;
        if (k->nrecargas < MAX_RECARGAS) {
            k->recargas[k->nrecargas].instante = quando;
            k->recargas[k->nrecargas].quantidade = k->ss_consumo;
            k->nrecargas++;
        } else {   /* vetor cheio: junta com a última e adia (ver MAX_RECARGAS) */
            k->recargas[MAX_RECARGAS - 1].instante = quando;
            k->recargas[MAX_RECARGAS - 1].quantidade += k->ss_consumo;
        }
    }
    k->ss_ativo_desde = -1;
    k->ss_consumo = 0;
}

/* Início de um período do servidor (é a "ativação" dele). No ingresso ele
 * passa a existir com a capacidade inteira (duracao); o Polling e o
 * Deferrable Server voltam a tê-la inteira a cada período, e o prazo deles
 * no EDF é o início do período mais o campo prazo. O Background Server não
 * tem períodos, e o Sporadic Server não recarga por período (só pelo que
 * gastou, ver ss_desativar): a única chegada deles é a do ingresso. */
static void chegada_servidor(Simulador *s, int i, int t) {
    TCB *k = &s->atual.tarefas[i];
    if (k->servidor == SERV_SS) aplicar_recargas(s, i, t);
    bool por_periodo = k->servidor == SERV_PS || k->servidor == SERV_DS;
    if (t < k->proxima_chegada || (!por_periodo && k->liberadas > 0)) return;
    evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
    k->liberadas++;
    k->chegada_atual = t;
    k->deadline_abs = t + k->prazo;
    if (capacidade_limitada(k)) k->capacidade = k->duracao;
    if (por_periodo) {
        k->proxima_chegada += k->periodo;
        /* Se a edição fez a próxima chegada ficar no passado, reancora no
         * tick atual, como na periódica. */
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;
    }
    if (k->estado == T_NOVA) {   /* acabou de ingressar; disputa a CPU quando tiver o que fazer */
        k->fila_desde = t;
        k->estado = T_SUSPENSA;
        k->motivo = SUSP_SEM_TRABALHO;
    }
}

/* ----------------------------------------------------------- servidores */

/* A fila de aperiódicas (req. B 2.2): aperiódicas e esporádicas com uma
 * ativação pendente que nenhum servidor tirou da fila. A suspensa pelo
 * usuário fica de fora até ser retomada. */
static bool na_fila(const TCB *k) {
    return tcb_eh_atendida(k) && k->restante > 0 && k->atendida_por < 0 &&
           (k->estado == T_PRONTA || k->estado == T_EXECUTANDO);
}

static int tamanho_da_fila(const Estado *e) {
    int n = 0;
    for (int i = 0; i < e->n; i++)
        if (na_fila(&e->tarefas[i])) n++;
    return n;
}

/* A primeira da fila é a que chegou antes (FIFO). Chegadas no mesmo instante
 * vão pela ordem do vetor, para o resultado não depender de sorteio. -1 se a
 * fila está vazia. */
static int primeira_da_fila(const Estado *e) {
    int melhor = -1;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (na_fila(k) && (melhor < 0 || k->chegada_atual < e->tarefas[melhor].chegada_atual)) melhor = i;
    }
    return melhor;
}

/* Desfaz os vínculos que deixaram de valer: a aperiódica foi suspensa pelo
 * usuário ou editada, ou o servidor foi suspenso ou encerrado. A aperiódica
 * volta para a fila. No andamento normal não há nada a desfazer aqui: o
 * vínculo acaba no passo 4, quando a ativação termina. */
static void conferir_vinculos(Estado *e) {
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->atendendo < 0) continue;
        TCB *a = &e->tarefas[k->atendendo];
        bool vale = k->estado != T_TERMINADA && !suspensa_pelo_usuario(k) && a->atendida_por == i &&
                    a->restante > 0 && (a->estado == T_PRONTA || a->estado == T_EXECUTANDO);
        if (!vale) {
            if (a->atendida_por == i) a->atendida_por = -1;
            k->atendendo = -1;
        }
    }
    for (int i = 0; i < e->n; i++) {
        TCB *a = &e->tarefas[i];
        if (a->atendida_por >= 0 && e->tarefas[a->atendida_por].atendendo != i) a->atendida_por = -1;
    }
}

/* O servidor disputa a CPU neste tick? Precisa já ter ingressado e não
 * estar suspenso pelo usuário. Além disso:
 *   - BS: ter o que executar, a aperiódica que já é dele ou alguma na fila
 *     (que ele pega se ganhar a CPU);
 *   - PS: ter capacidade. Disputa mesmo com a fila vazia: é quando ganha a
 *     CPU que ele consulta a fila, e se ela estiver vazia perde a
 *     capacidade até o próximo período (ver escalonar);
 *   - DS e SS: ter capacidade E o que executar. Com a fila vazia eles não
 *     disputam e guardam a capacidade ("adiam" o uso): uma aperiódica que
 *     chega no meio do período é atendida na hora. */
static bool servidor_disputa(const TCB *k, int nfila) {
    if (k->estado == T_NOVA || k->estado == T_TERMINADA || suspensa_pelo_usuario(k)) return false;
    bool tem_trabalho = k->atendendo >= 0 || nfila > 0;
    switch (k->servidor) {
    case SERV_PS: return k->capacidade > 0;
    case SERV_DS:
    case SERV_SS: return k->capacidade > 0 && tem_trabalho;
    default: return tem_trabalho;
    }
}

/* Por que um servidor que não disputa a CPU está parado. */
static MotivoSuspensao motivo_ocioso(const TCB *k) {
    return capacidade_limitada(k) && k->capacidade == 0 ? SUSP_SEM_CAPACIDADE : SUSP_SEM_TRABALHO;
}

/* O Background Server só fica com a CPU que sobraria desligada: perde para
 * qualquer outra candidata, seja qual for a prioridade dela. Entre as demais
 * valem a prioridade do algoritmo e a cadeia de desempate de sempre. */
static bool em_segundo_plano(const TCB *k) {
    return tcb_eh_servidor(k) && k->servidor == SERV_BS;
}

static int comparar_candidatas(const TCB *a, const TCB *b) {
    bool pa = em_segundo_plano(a), pb = em_segundo_plano(b);
    if (pa != pb) return pa ? 1 : -1;
    return sched_comparar(a, b);
}

/* Os servidores escolhidos para uma CPU que ainda não têm aperiódica, do
 * mais prioritário para o menos (empate fica na ordem do vetor). Devolve
 * quantos são; os índices vão para `ordem`. */
static int servidores_sem_aperiodica(const Estado *e, const int *cand, int nc, const bool *escolhido, int *ordem) {
    int no = 0;
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        const TCB *k = &e->tarefas[i];
        if (!escolhido[i] || !tcb_eh_servidor(k) || k->atendendo >= 0) continue;
        int p = no++;   /* inserção ordenada, estável */
        while (p > 0 && comparar_candidatas(&e->tarefas[ordem[p - 1]], k) > 0) {
            ordem[p] = ordem[p - 1];
            p--;
        }
        ordem[p] = i;
    }
    return no;
}

/* ------------------------------------------------------------ escalonar */

/* Escolhe quem fica com as CPUs entre as candidatas que não estão `fora`.
 *
 * Em cada rodada pegamos o grupo das "melhores" tarefas restantes (as que
 * empatam em prioridade E nos critérios 1-4). Se o grupo cabe nas CPUs
 * livres, todas entram e a ordem entre elas não importa. Se não cabe,
 * sorteamos quais entram (critério 5) e marcamos quem foi sorteado. Assim
 * só há sorteio quando ele realmente decide quem executa.
 *
 * Exemplo com 2 CPUs e candidatas A (prio 5), B (prio 5), C (prio 9):
 *   rodada 1: melhor = A; grupo = {A, B} (empatam); cabe nas 2 CPUs -> entram.
 *   C fica de fora (CPUs acabaram). Não houve sorteio.
 * Com 1 CPU: rodada 1 grupo = {A, B}, mas só 1 CPU livre -> sorteia entre A e B. */
static void selecionar(Estado *e, const int *cand, int nc, const bool *fora, bool *escolhido, bool *sorteado,
                       int *grupo) {
    int livres = e->ncpus;     /* CPUs ainda sem tarefa nesta rodada          */
    int restantes = 0;         /* candidatas ainda sem decisão                */
    for (int j = 0; j < nc; j++)
        if (!fora[cand[j]]) restantes++;
    while (livres > 0 && restantes > 0) {
        /* melhor entre os não escolhidos: varredura simples; comparar < 0
         * significa "a vem antes de b" (prioridade ativa + desempate). */
        int melhor = -1;
        for (int j = 0; j < nc; j++) {
            int i = cand[j];
            if (escolhido[i] || fora[i]) continue;
            if (melhor < 0 || comparar_candidatas(&e->tarefas[i], &e->tarefas[melhor]) < 0) melhor = i;
        }
        /* grupo = todas as que empatam COM a melhor (comparar == 0). */
        int ng = 0;
        for (int j = 0; j < nc; j++) {
            int i = cand[j];
            if (!escolhido[i] && !fora[i] && comparar_candidatas(&e->tarefas[i], &e->tarefas[melhor]) == 0)
                grupo[ng++] = i;
        }
        if (ng <= livres) {
            /* Cabem todas: nenhuma decisão aleatória necessária. */
            for (int g = 0; g < ng; g++) escolhido[grupo[g]] = true;
            livres -= ng;
            restantes -= ng;
        } else {
            /* Não cabem: critério 5, sorteio. parcial: as
             * `livres` primeiras posições viram uma amostra uniforme do
             * grupo empatado (cada posição g troca com uma posição sorteada
             * entre g e o fim). */
            for (int g = 0; g < livres; g++) {
                int r = g + rng_intervalo(&e->rng, ng - g);
                int tmp = grupo[g];
                grupo[g] = grupo[r];
                grupo[r] = tmp;
                escolhido[grupo[g]] = true;
                sorteado[grupo[g]] = true;
            }
            restantes -= livres;
            livres = 0;
        }
    }
}

/* O ESCALONADOR: decide quem roda em cada CPU neste tick.
 *
 * Disputam a CPU as periódicas com ativação pendente e os servidores com
 * trabalho (servidor_disputa). Aperiódicas e esporádicas nunca disputam
 * (req. B 1.1): ficam na fila, e o servidor que ganha a CPU tira a primeira
 * da fila e a executa no lugar dele. Para isso não ser só uma promessa, a
 * seleção é conferida depois: se um servidor escolhido não tem aperiódica
 * para pegar (outros servidores mais prioritários levaram as da fila), ele
 * sai da disputa e a seleção é refeita sem ele, para a CPU ir para a
 * próxima candidata em vez de ficar ocupada à toa. O gerador aleatório volta
 * ao estado de antes a cada tentativa, então só a seleção final conta. */
static bool escalonar(Simulador *s) {
    Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    int n = e->n, t = e->t;

    /* Vetores de trabalho (liberados no fim da função):
     *   cand      - índices das tarefas candidatas a executar neste tick
     *   grupo     - empatadas da rodada atual
     *   escolhido - quem ganhou uma CPU
     *   sorteado  - quem ganhou a CPU por sorteio (para marcar no Gantt)
     *   fora      - servidor que saiu da disputa por não ter o que executar */
    int *cand = xmalloc((size_t)(n ? n : 1) * sizeof(int));

    conferir_vinculos(e);
    int nfila = tamanho_da_fila(e);

    /* Candidata = periódica pronta OU que já estava executando, e que ainda
     * tem trabalho a fazer (restante > 0); ou servidor com trabalho. Para
     * cada uma, recalculamos a prioridade com o algoritmo atual (RM, EDF,
     * plugin ou fórmula). Se o escalonador não conseguir (ex.: divisão por
     * zero numa fórmula), nada foi decidido ainda: devolvemos false com o
     * motivo em s->erro, sem inventar uma prioridade. */
    int nc = 0;
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (tcb_eh_atendida(k)) continue;   /* executa por meio de um servidor */
        if (tcb_eh_servidor(k)) {
            if (!servidor_disputa(k, nfila)) {
                ss_desativar(k, t);
                /* Sem trabalho ou sem capacidade: sai da CPU sem que isso
                 * seja uma preempção. */
                if (k->estado != T_NOVA && k->estado != T_TERMINADA && !suspensa_pelo_usuario(k)) {
                    k->estado = T_SUSPENSA;
                    k->motivo = motivo_ocioso(k);
                    k->cpu = -1;
                    k->quantum_usado = 0;
                }
                continue;
            }
            if (k->estado == T_SUSPENSA) {   /* tinha nada a fazer; agora entra no fim da fila */
                k->estado = T_PRONTA;
                k->motivo = SUSP_NENHUM;
                k->fila_desde = t;
            }
            /* Sporadic Server que fica ativo agora: o prazo no EDF já é o
             * desta ativação (ss_ativar confirma depois da seleção). */
            if (k->servidor == SERV_SS && k->ss_ativo_desde < 0) k->deadline_abs = t + k->prazo;
        } else if (!((k->estado == T_PRONTA || k->estado == T_EXECUTANDO) && k->restante > 0)) {
            continue;
        }
        /* Esgotou o quantum no tick anterior: volta para o fim da fila.
         * É a mesma condição do evento "fim de quantum" registrado mais
         * abaixo, mas precisa valer ANTES de calcular as prioridades, para
         * um escalonador que usa fila_desde (Round Robin) já trocar de
         * tarefa neste tick, e não um tick depois. RM e EDF não leem o
         * campo, e o contador quantum_usado continua sendo zerado lá
         * embaixo, como sempre foi. */
        if (k->cpu >= 0 && k->quantum_usado >= e->quantum) k->fila_desde = t;
        if (sched_atualizar(esc, k, t, s->erro, sizeof s->erro)) {
            free(cand);
            return false;
        }
        cand[nc++] = i;
    }
    int *grupo = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    bool *escolhido = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    bool *sorteado = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    bool *fora = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    int *ordem = xmalloc((size_t)(n ? n : 1) * sizeof(int));

    /* Seleção, refeita enquanto algum servidor escolhido ficar sem
     * aperiódica (ver o comentário da função). Termina porque cada volta tira
     * pelo menos um servidor da disputa. */
    uint64_t rng_antes = e->rng;
    for (;;) {
        e->rng = rng_antes;
        memset(escolhido, 0, (size_t)(n ? n : 1) * sizeof(bool));
        memset(sorteado, 0, (size_t)(n ? n : 1) * sizeof(bool));
        selecionar(e, cand, nc, fora, escolhido, sorteado, grupo);
        int no = servidores_sem_aperiodica(e, cand, nc, escolhido, ordem);
        if (no <= nfila) break;
        for (int r = nfila; r < no; r++) fora[ordem[r]] = true;
    }
    /* Cada servidor escolhido sem aperiódica tira a primeira da fila (req.
     * B 2.2), do mais prioritário para o menos. */
    int no = servidores_sem_aperiodica(e, cand, nc, escolhido, ordem);
    for (int r = 0; r < no; r++) {
        int a = primeira_da_fila(e);
        e->tarefas[a].atendida_por = ordem[r];
        e->tarefas[ordem[r]].atendendo = a;
    }

    /* Distribuição das CPUs. Quem já estava numa CPU válida fica nela
     * (evita migração desnecessária); os demais pegam a CPU livre de menor
     * número. Isso é "afinidade": sem ela, o Gantt trocaria as tarefas de
     * CPU à toa e pareceria que há preempção onde não há. */
    bool ocupada[MAX_CPUS] = {false};
    int *nova_cpu = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) nova_cpu[i] = -1;
    /* 1ª passada: mantém quem já estava numa CPU. */
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        int c = e->tarefas[i].cpu;
        if (escolhido[i] && c >= 0 && c < e->ncpus && !ocupada[c]) {
            nova_cpu[i] = c;
            ocupada[c] = true;
        }
    }
    /* 2ª passada: as escolhidas restantes pegam a menor CPU livre. O while
     * termina porque há no máximo `ncpus` escolhidas. */
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        if (!escolhido[i] || nova_cpu[i] >= 0) continue;
        int c = 0;
        while (ocupada[c]) c++;
        nova_cpu[i] = c;
        ocupada[c] = true;
    }

    /* Aplica a decisão: atualiza estado/CPU de cada candidata e registra os
     * eventos (sorteio, preempção, fim de quantum) para o Gantt. */
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        TCB *k = &e->tarefas[i];
        /* Sporadic Server que disputa e não saiu da disputa: está ativo. */
        if (!fora[i] && tcb_eh_servidor(k) && k->servidor == SERV_SS && k->ss_ativo_desde < 0) ss_ativar(s, i, t);
        if (fora[i]) {
            /* Servidor sem aperiódica para pegar: sai da disputa sem que
             * isso seja uma preempção. O Polling Server ganhou a CPU e
             * achou a fila vazia: perde a capacidade deste período, que
             * fica para as periódicas. */
            if (k->servidor == SERV_PS) {
                k->capacidade = 0;
                evento_add(s, t, t, EV_DESCARTE, i, k->liberadas - 1, -1);
            }
            ss_desativar(k, t);
            k->estado = T_SUSPENSA;
            k->motivo = motivo_ocioso(k);
            k->cpu = -1;
            k->quantum_usado = 0;
            continue;
        }
        if (escolhido[i]) {
            /* Quantum novo se acabou de ganhar a CPU, ou se esgotou o
             * anterior e o escalonador, reavaliando, manteve a tarefa (ela
             * ainda é a de maior prioridade). O evento deixa o fim do
             * quantum visível sem alterar a escolha (steering, ambig. 1). */
            if (k->cpu >= 0 && k->quantum_usado >= e->quantum)
                evento_add(s, t, t, EV_FIM_QUANTUM, i, k->concluidas, k->cpu);
            if (k->cpu < 0 || k->quantum_usado >= e->quantum) k->quantum_usado = 0;
            k->cpu = nova_cpu[i];
            k->estado = T_EXECUTANDO;
            if (sorteado[i]) {
                k->sorteios++;
                evento_add(s, t, t, EV_SORTEIO, i, k->concluidas, k->cpu);
            }
        } else {
            /* Não foi escolhida. Se estava numa CPU (k->cpu >= 0), perdeu
             * a CPU: isso é uma PREEMPÇÃO. Se estava só esperando, nada
             * muda: continua pronta. */
            if (k->cpu >= 0) {
                if (k->quantum_usado >= e->quantum)
                    evento_add(s, t, t, EV_FIM_QUANTUM, i, k->concluidas, k->cpu);
                k->preempcoes++;
                evento_add(s, t, t, EV_PREEMPCAO, i, k->concluidas, k->cpu);
            }
            k->cpu = -1; //nao tem cpu entao recebe -1
            k->quantum_usado = 0;
            k->estado = T_PRONTA;
        }
    }

    /* As aperiódicas acompanham o servidor: executam na CPU dele quando ele
     * executa; senão esperam (na fila ou com um servidor sem CPU). */
    for (int i = 0; i < n; i++) {
        TCB *a = &e->tarefas[i];
        if (!tcb_eh_atendida(a) || a->restante == 0 || !(a->estado == T_PRONTA || a->estado == T_EXECUTANDO))
            continue;
        const TCB *srv = a->atendida_por >= 0 ? &e->tarefas[a->atendida_por] : NULL;
        if (srv && srv->estado == T_EXECUTANDO) {
            a->estado = T_EXECUTANDO;
            a->cpu = srv->cpu;
        } else {
            a->estado = T_PRONTA;
            a->cpu = -1;
        }
    }
    free(cand);
    free(grupo);
    free(escolhido);
    free(sorteado);
    free(fora);
    free(ordem);
    free(nova_cpu);
    return true;
}

/* ------------------------------------------------------------------ tick */

/* Executa UM tick completo (os 5 passos descritos no topo do arquivo).
 * Devolve false se o escalonador falhou no passo 2: aí o tick é DESFEITO
 * (estado, eventos e coluna do Gantt voltam a ser os de antes dele), para que
 * o histórico até o tick anterior continue valendo e o usuário possa voltar
 * no tempo ou trocar de algoritmo e continuar. Para isso guardamos uma cópia
 * do estado antes do tick (a mesma cópia que um snapshot faz). */
static bool executar_tick(Simulador *s) {
    Estado *e = &s->atual;
    int t = e->t;
    Estado antes = {0};
    estado_copiar(&antes, e);
    int nev_antes = s->nev, ncols_antes = s->ncols, nadm_antes = s->nadm;
    /* Garante que o rastro do Gantt tem espaço para este tick e para todas
     * as tarefas (uma tarefa pode ter sido adicionada pelo usuário). */
    rastro_ajustar_largura(s, e->n);
    rastro_garantir_coluna(s, t);

    /* 1. Criação e chegadas. Quem ingressa agora passa antes pelo teste de
     * admissão (admitir_novas). Usamos >= (e não ==) para que uma edição de ingresso ou
     * período que "pule" o instante exato não perca a ativação. A periódica
     * libera no máximo uma por tick; a aperiódica pode ter duas ocorrências
     * no mesmo instante, e libera todas as que já venceram. */
    admitir_novas(s, t);
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (k->estado == T_TERMINADA) continue;
        if (tcb_eh_servidor(k)) {
            chegada_servidor(s, i, t);
            continue;
        }
        /* Já liberou todas as ativações, ou ainda não é a hora: nada a fazer. */
        while (k->liberadas < tcb_total_ativacoes(k) && t >= k->proxima_chegada) {
            liberar_ativacao(s, i, t);
            if (!tcb_eh_atendida(k)) break;
        }
    }

    /* 2. Escalonamento: decide quem executa neste tick (função acima). */
    if (!escalonar(s)) {
        estado_copiar(e, &antes);   /* desfaz as chegadas deste tick */
        estado_liberar(&antes);
        s->nev = nev_antes;         /* e os eventos, a coluna do Gantt */
        s->ncols = ncols_antes;
        s->nadm = nadm_antes;       /* e as decisões de admissão */
        return false;
    }
    estado_liberar(&antes);

    /* 3. Execução e gravação da coluna do Gantt. Para cada tarefa, o que
     * aconteceu neste tick vira UMA célula do gráfico (cel) + a CPU usada. */
    size_t base = (size_t)t * (size_t)s->largura;   /* início da coluna t */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        switch (k->estado) {
        case T_EXECUTANDO:
            s->cel[base + i] = G_EXEC;
            s->cpu[base + i] = (signed char)k->cpu;
            /* consumiu 1 tick de CPU. O servidor não tem trabalho próprio:
             * quem consome é a aperiódica dele, que também está executando;
             * ele gasta a capacidade. */
            if (!tcb_eh_servidor(k)) k->restante--;
            else if (capacidade_limitada(k)) {
                k->capacidade--;
                if (k->servidor == SERV_SS) k->ss_consumo++;
            }
            k->ticks_exec++;
            k->quantum_usado++;
            break;
        case T_PRONTA:
            s->cel[base + i] = G_PRONTA;
            k->ticks_espera++;      /* esperando na fila de prontos */
            break;
        case T_SUSPENSA:
            /* Esperar o próximo período fica vazio no Gantt, como nas
             * figuras do capítulo; preto pontilhado é a suspensão de fato. */
            if (k->motivo == SUSP_USUARIO) {
                s->cel[base + i] = G_SUSPENSA;
                k->ticks_suspensa++;
            }
            break;
        default:
            break;
        }
    }

    /* 4. Verificações no instante t+1 (fim do tick). Ficam depois da
     * execução porque só agora sabemos se a tarefa terminou neste tick. */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (tcb_eh_servidor(k)) {
            /* O servidor não tem ativação própria nem prazo a conferir; só a
             * capacidade. Se ela acabou no meio de uma ativação, a aperiódica
             * volta para a fila (na frente, porque a fila é por ordem de
             * chegada) e o servidor espera a recarga. Se a aperiódica acabou
             * neste mesmo tick, quem a solta é o fim de ativação dela, abaixo. */
            if (k->estado == T_EXECUTANDO && capacidade_limitada(k) && k->capacidade == 0) {
                if (k->atendendo >= 0 && e->tarefas[k->atendendo].restante > 0) {
                    TCB *a = &e->tarefas[k->atendendo];
                    a->atendida_por = -1;
                    a->estado = T_PRONTA;
                    k->atendendo = -1;
                }
                k->estado = T_SUSPENSA;
                k->motivo = SUSP_SEM_CAPACIDADE;
            }
            continue;
        }
        if (k->estado == T_EXECUTANDO && k->restante == 0) {
            /* Uma aperiódica que termina a ativação solta o servidor, que
             * fica livre para pegar a próxima da fila. */
            if (k->atendida_por >= 0) {
                e->tarefas[k->atendida_por].atendendo = -1;
                k->atendida_por = -1;
            }
            /* Terminou a ativação. Tempo de resposta = fim - chegada. */
            int resp = t + 1 - k->chegada_atual;
            if (resp > k->resposta_max) k->resposta_max = resp;
            k->resposta_soma += resp;
            k->concluidas++;
            evento_add(s, t, t + 1, EV_FIM_ATIVACAO, i, k->concluidas - 1, k->cpu);
            if (k->concluidas >= tcb_total_ativacoes(k)) {
                /* Era a última (a 10ª da periódica): a tarefa "termina" (req. 4.4). */
                k->estado = T_TERMINADA;
                k->termino = t + 1;
                k->cpu = -1;
                evento_add(s, t, t + 1, EV_TERMINO, i, k->concluidas - 1, -1);
            } else if (k->liberadas > k->concluidas) {
                /* Próxima ativação já estava acumulada. Mantemos `cpu` para
                 * que ela conte como "estava executando". */
                comecar_ativacao(s, i, t, t + 1);
                if (k->restante > 0) k->estado = T_PRONTA;
            } else {
                /* Nada pendente: espera o próximo período. */
                k->estado = T_SUSPENSA;
                k->motivo = SUSP_PERIODO;
                /* Se a próxima ativação chega agora no instante t+1
                 * (ela só é liberada no passo 1 do próximo tick), a tarefa
                 * continua contando como "estava executando" para o critério
                 * 1, igual ao caso da ativação acumulada acima. */
                if (!(k->liberadas < tcb_total_ativacoes(k) && k->proxima_chegada == t + 1))
                    k->cpu = -1;
            }
        }
        /* O deadline é um instante: a ativação tem que terminar até ele.
         * Se chegamos nele (ou passamos) com trabalho restante, perdeu. A
         * tarefa continua executando até completar a duração (req. 3.3).
         * Verificamos TODAS as ativações pendentes, inclusive as acumuladas
         * que ainda esperam a anterior terminar: assim a perda aparece no
         * instante exato do deadline (req. 2.2), e não só quando a ativação
         * finalmente começa a executar. */
        if (k->estado == T_TERMINADA) continue;
        if (k->prazo <= 0) continue;   /* aperiódica sem prazo: não há prazo a perder */
        /* Começa na primeira pendente que ainda não foi marcada (as de
         * número menor que perdas_ate já foram; ver sched.h), para não
         * marcar a mesma ativação duas vezes. As chegadas são crescentes e o
         * prazo relativo é o mesmo para todas, então os deadlines também
         * crescem: na primeira que ainda não venceu, as seguintes também não
         * venceram, e o laço pode parar. */
        int j0 = k->perdas_ate > k->concluidas ? k->perdas_ate : k->concluidas;
        for (int j = j0; j < k->liberadas; j++) {
            int deadline = tcb_chegada(k, j) + k->prazo;
            if (deadline > t + 1) break;
            k->perdas_ate = j + 1;
            k->deadlines_perdidos++;
            /* deadline < t+1 só acontece se o prazo foi reduzido por edição
             * para um instante que já passou: aí a perda é detectada agora. */
            int inst = deadline > t ? deadline : t + 1;
            evento_add(s, t, inst, EV_PRAZO_PERDIDO, i, j, -1);
        }
    }

    /* 5. O relógio avança e vemos se acabou tudo. */
    e->t++;
    atualizar_fim(e);
    return true;
}

/* ------------------------------------------------------------- navegação */

/* Avança 1 tick. Se esse tick JÁ foi simulado antes (o usuário voltou com
 * `p`), não recalcula: só recarrega o snapshot guardado. Isso garante que
 * voltar e avançar dá exatamente o mesmo resultado, inclusive nos sorteios. */
bool sim_avancar(Simulador *s) {
    s->erro[0] = '\0';
    if (s->atual.fim) return false;
    int t = s->atual.t;
    if (s->historico && t + 1 < s->nsnaps) {
        hist_carregar(s, t + 1);   /* já simulado: só reaplica o snapshot */
        return true;
    }
    /* O escalonador falhou: o motivo está em s->erro e o estado é o do
     * instante t (o tick foi desfeito), então não há snapshot novo. */
    if (!executar_tick(s)) return false;
    hist_gravar(s);
    return true;
}

/* Volta 1 tick: o relógio recua porque `atual` passa a ser o snapshot do
 * instante anterior. Sem histórico (modo completo) não há como voltar. */
bool sim_voltar(Simulador *s) {
    if (!s->historico || s->atual.t == 0) return false;
    hist_carregar(s, s->atual.t - 1);
    return true;
}

/* Vai para o instante t (para frente ou para trás). Para trás é só carregar
 * o snapshot; para frente simula tick a tick até lá (ou até acabar). */
int sim_ir_para(Simulador *s, int t) {
    if (t < 0) t = 0;
    if (s->historico && t < s->nsnaps) {
        hist_carregar(s, t);
        return t;
    }
    while (s->atual.t < t && sim_avancar(s)) {}
    return s->atual.t;
}

/* "Bloqueado" = ainda há tarefas por terminar, mas TODAS estão suspensas
 * pelo usuário (ou são aperiódicas e todos os servidores estão suspensos):
 * nada vai progredir sozinho. Sem esta checagem o "rodar até o
 * fim" ficaria em laço infinito. Tarefas esperando o período não contam,
 * pois voltam sozinhas. */
bool sim_bloqueado(const Estado *e) {
    /* Uma aperiódica só progride se existe servidor que possa executá-la. */
    bool servidor = false;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (tcb_eh_servidor(k) && k->estado != T_TERMINADA && !suspensa_pelo_usuario(k)) servidor = true;
    }
    bool alguma = false;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (k->estado == T_TERMINADA || tcb_eh_servidor(k)) continue;
        /* Quem espera o período volta sozinha; só o usuário trava a tarefa
         * (ou, na aperiódica, a falta de servidor). */
        if (!suspensa_pelo_usuario(k) && (!tcb_eh_atendida(k) || servidor)) return false;
        alguma = true;
    }
    return alguma;
}

/* Roda até todas terminarem. Devolve NULL se deu certo, ou o motivo de ter
 * parado (para a interface mostrar ao usuário). */
const char *sim_rodar_ate_fim(Simulador *s) {
    /* Se já existe futuro gravado, pula direto para o fim dele. */
    if (s->historico && s->nsnaps > 0) hist_carregar(s, s->nsnaps - 1);
    while (!s->atual.fim) {
        if (sim_bloqueado(&s->atual))
            return "todas as tarefas restantes estão suspensas (ou são aperiódicas e todos os servidores estão "
                   "suspensos); retome alguma para continuar";
        if (s->atual.t >= LIMITE_TICKS) return "limite de segurança de ticks atingido";
        if (!sim_avancar(s)) return s->erro;   /* o escalonador falhou neste tick */
    }
    return NULL;
}

/* Edição de estado em qualquer passo (req. 3.4): protocolo em 3 etapas.
 *   1. sim_preparar_edicao: joga fora o "futuro" (snapshots, colunas do
 *      Gantt e eventos depois do instante atual), pois deixa de valer;
 *   2. a interface altera s->atual;
 *   3. sim_concluir_edicao: grava o estado editado como o novo snapshot.
 * É o comportamento de um debugger: editar no passado reescreve o futuro. */
void sim_preparar_edicao(Simulador *s) {
    hist_truncar(s, s->atual.t);
}

void sim_concluir_edicao(Simulador *s) {
    atualizar_fim(&s->atual);
    hist_gravar(s);
}

/* Acrescenta uma tarefa durante a simulação. realloc pode mover o vetor de
 * lugar, por isso ninguém guarda ponteiros para TCBs (só índices). */
void sim_adicionar_tarefa(Simulador *s, const TarefaCfg *t) {
    Estado *e = &s->atual;
    e->tarefas = xrealloc(e->tarefas, (size_t)(e->n + 1) * sizeof(TCB));
    tcb_iniciar(&e->tarefas[e->n], t);
    e->n++;
    rastro_ajustar_largura(s, e->n);
}

/* --------------------------------------------------------------- consultas */

/* O rastro do Gantt é uma tabela guardada num vetor 1D: a célula da tarefa
 * `tarefa` no tick `tick` fica em [tick * largura + tarefa] (largura =
 * número de tarefas). Fora dos limites devolvemos "vazio"/-1. */
CelulaGantt sim_celula(const Simulador *s, int tick, int tarefa) {
    if (tick < 0 || tick >= s->ncols || tarefa >= s->largura) return G_VAZIO;
    return (CelulaGantt)s->cel[(size_t)tick * s->largura + tarefa];
}

int sim_cpu_celula(const Simulador *s, int tick, int tarefa) {
    if (tick < 0 || tick >= s->ncols || tarefa >= s->largura) return -1;
    return s->cpu[(size_t)tick * s->largura + tarefa];
}

/* O usuário digita o ID da tarefa; internamente usamos o índice no vetor.
 * Esta função traduz um no outro e devolve o ÍNDICE, -1 se não existe:
 * nunca um id, porque nenhum valor de id serve de "nenhuma tarefa". A busca
 * usa id_iguais, então "t0" acha a tarefa "T0" e "01" acha a "1". */
int sim_indice(const Estado *e, const char *id) {
    for (int i = 0; i < e->n; i++)
        if (id_iguais(e->tarefas[i].id, id)) return i;
    return -1;
}

/* true se todos os ids do estado atual são inteiros: decide se os rótulos
 * saem "T<id>" ou crus (ver id_rotulo). É calculado sempre a partir do
 * estado atual porque uma tarefa de id texto pode ser adicionada durante a
 * simulação (e some de novo se o usuário voltar para antes dela). */
bool sim_ids_inteiros(const Estado *e) {
    for (int i = 0; i < e->n; i++)
        if (!id_eh_inteiro(e->tarefas[i].id, NULL)) return false;
    return true;
}

/* Parcela de uma tarefa na utilização: C/P da periódica e capacidade/período
 * do servidor. Aperiódicas e esporádicas não entram (executam dentro dos
 * servidores), nem o Background Server (só usa CPU que ficaria desligada).
 * -1 = não entra. */
double utilizacao_parcela(TipoTarefa tipo, TipoServidor servidor, int duracao, int periodo) {
    if (periodo <= 0) return -1;
    if (tipo == TIPO_PERIODICA || (tipo == TIPO_SERVIDOR && servidor != SERV_BS)) return (double)duracao / periodo;
    return -1;
}

/* Utilização do processador: U = soma de C/P (duração / período). Se U > 1
 * numa CPU só, é impossível cumprir todos os prazos. Em `n` (pode ser NULL)
 * vai quantas tarefas entraram na soma, o n do limite do RM. */
double sim_utilizacao(const Estado *e, int *n) {
    double u = 0;
    int cont = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        double p = utilizacao_parcela(k->tipo, k->servidor, k->duracao, k->periodo);
        if (p < 0) continue;
        u += p;
        cont++;
    }
    if (n) *n = cont;
    return u;
}

/* Limite do teste suficiente do RM (Liu & Layland): U <= n(2^(1/n) - 1).
 * Se U está abaixo do limite, o RM garante escalonar; acima, não garante
 * (mas ainda pode funcionar). */
double sim_limite_rm(int n) {
    if (n <= 0) return 1.0;
    /* 2^(1/n) por bissecção: x^n = 2 com x em [1, 2]. Evita depender da
     * libm só por causa de uma conta. */
    double lo = 1.0, hi = 2.0;
    for (int it = 0; it < 60; it++) {
        double m = (lo + hi) / 2, p = 1.0;
        for (int k = 0; k < n; k++) p *= m;
        if (p < 2.0) lo = m;
        else hi = m;
    }
    return n * (lo - 1.0);
}

/* ===========================================================================
 * Seção history.c (originalmente src/core/history.c)
 * =========================================================================== */
/*
 * history.c - histórico de estados (avançar/retroceder) e rastro do Gantt.
 *
 * Decisão: guardamos um snapshot COMPLETO por tick em vez de "desfazer"
 * operações. É o jeito mais simples de garantir que voltar e avançar dá
 * exatamente o mesmo resultado, inclusive nos sorteios (o estado do gerador
 * aleatório está no snapshot). O custo é n_tarefas * sizeof(TCB) por tick,
 * desprezível nos tamanhos de simulação deste trabalho.
 *
 * Editar o estado num instante passado invalida o que veio depois, como
 * num debugger: snapshots, colunas do Gantt e eventos futuros são
 * descartados e a simulação segue a partir do estado editado.
 *
 * Este arquivo guarda TRÊS coisas diferentes, cada uma com seu papel:
 *   1. snaps[t]  - o estado completo do sistema no instante t (para voltar);
 *   2. cel/cpu   - o "rastro": o que cada tarefa fez em cada tick (para o
 *                  Gantt). Fica à parte para o gráfico não depender de reler
 *                  snapshots;
 *   3. ev        - a lista de eventos (chegadas, perdas de prazo, ...).
 */

/* =========================================================================
 * 1. GERENCIAMENTO DE SNAPSHOTS (Fotografias da Memória por Tick)
 * ========================================================================= */

//Realiza uma cópia profunda (deep copy) de uma estrutura de Estado 'src' para 'dst'.
//Como o TCB não possui ponteiros internos dinâmicos próprios, um simples memcpy do
//vetor de TCBs é suficiente para clonar o estado inteiro com extrema eficiência.
//O id (texto) é um vetor fixo dentro do TCB, então vai junto no memcpy, sem strdup/free.
void estado_copiar(Estado *dst, const Estado *src) {
    TCB *buf = dst->tarefas;     /* guarda o vetor que o destino já tinha   */
    int cap_antiga = dst->n;
    *dst = *src;                  /* copia os campos simples (t, rng, ...)  */
    /* Reaproveita o vetor de destino se já tiver o tamanho certo. */
    if (cap_antiga != src->n || !buf) buf = xrealloc(buf, (size_t)(src->n ? src->n : 1) * sizeof(TCB));
    if (src->n) memcpy(buf, src->tarefas, (size_t)src->n * sizeof(TCB));    // Clona o vetor de TCBs da origem para o destino usando memcpy()
    dst->tarefas = buf;           /* o `*dst = *src` tinha sobrescrito isto */
}

//Libera a memória dinâmica alocada para o vetor de TCBs contido dentro de uma estrutura Estado
void estado_liberar(Estado *e) {
    free(e->tarefas);
    e->tarefas = NULL;
    e->n = 0;
}

//Grava o estado atual da simulação (s->atual) na pilha de snapshots no índice 't'.
//Expande a capacidade da pilha dobrando seu tamanho para manter o custo de gravação constante (O(1) amortizado).
void hist_gravar(Simulador *s) {
    if (!s->historico) return;    /* modo completo: não guardamos nada      */
    int t = s->atual.t;
    if (t >= s->capsnaps) {
        int nova = s->capsnaps ? s->capsnaps * 2 : 256;
        while (nova <= t) nova *= 2;
        s->snaps = xrealloc(s->snaps, (size_t)nova * sizeof(Estado));
        /* A parte nova precisa vir zerada: estado_copiar espera que o
         * ponteiro `tarefas` antigo seja NULL ou válido, nunca lixo. */
        memset(s->snaps + s->capsnaps, 0, (size_t)(nova - s->capsnaps) * sizeof(Estado));    // Zera a nova área alocada de memória usando memset()
        s->capsnaps = nova;
    }
    estado_copiar(&s->snaps[t], &s->atual);    // Clona o estado corrente no snapshot do tick 't' usando estado_copiar()
    if (t + 1 > s->nsnaps) s->nsnaps = t + 1;
}

//Trunca o histórico da simulação a partir do instante 't' após uma edição dinâmica. 
//Libera os snapshots futuros (ticks > t) e remove os eventos/colunas do Gantt gerados no futuro descartado.
void hist_truncar(Simulador *s, int t) {
    if (s->historico) {
        for (int k = t + 1; k < s->nsnaps; k++) 
            estado_liberar(&s->snaps[k]);    // Libera a memória de cada snapshot futuro usando estado_liberar()
        if (s->nsnaps > t + 1) s->nsnaps = t + 1;
    }
    /* Colunas >= t e eventos gerados em ticks >= t pertencem ao futuro.
     * Para os eventos fazemos uma compactação in-place: copiamos para a
     * frente só os que ficam (índice j) e depois ajustamos o total. */
    if (s->ncols > t) s->ncols = t;
    int j = 0;
    for (int i = 0; i < s->nev; i++)
        if (s->ev[i].tick < t) s->ev[j++] = s->ev[i];
    s->nev = j;
    /* As decisões de admissão seguem a mesma regra dos eventos. */
    j = 0;
    for (int i = 0; i < s->nadm; i++)
        if (s->adm[i].tick < t) s->adm[j++] = s->adm[i];
    s->nadm = j;
}

//Restaura o estado da simulação (s->atual) a partir do snapshot gravado no instante 't' (operação de Undo/Ir para).
void hist_carregar(Simulador *s, int t) {
    estado_copiar(&s->atual, &s->snaps[t]);
}

/* =========================================================================
 * 2. GERENCIAMENTO DO RASTRO GRÁFICO (Matrizes do Gantt)
 * ========================================================================= */

//Garante a alocação de espaço para a coluna correspondente ao 'tick' nas matrizes 
//do Gráfico de Gantt e limpa as células (marcando como vazia e sem CPU alocada).
void rastro_garantir_coluna(Simulador *s, int tick) {
    if (tick >= s->capcols) {
        int nova = s->capcols ? s->capcols * 2 : 256;
        while (nova <= tick) nova *= 2;
        size_t w = (size_t)(s->largura ? s->largura : 1);
        s->cel = xrealloc(s->cel, (size_t)nova * w);    // Redimensiona o vetor dinâmico de células usando xrealloc()
        s->cpu = xrealloc(s->cpu, (size_t)nova * w);
        s->ncpus_col = xrealloc(s->ncpus_col, (size_t)nova);
        s->capcols = nova;
    }
    size_t w = (size_t)s->largura;
    memset(s->cel + (size_t)tick * w, G_VAZIO, w);
    memset(s->cpu + (size_t)tick * w, -1, w);
    /* Guardamos quantas CPUs existiam NESTE tick: o usuário pode mudar a
     * quantidade durante a simulação, e o Gantt antigo precisa lembrar. */
    s->ncpus_col[tick] = (unsigned char)s->atual.ncpus;
    if (tick + 1 > s->ncols) s->ncols = tick + 1;
}

//Reorganiza as matrizes 2D do rastro do Gantt quando uma nova tarefa é adicionada.
//Como o layout da matriz é 'tick * largura + tarefa', alterar a largura exige reestruturar 
//e copiar o conteúdo linha por linha em novos blocos de memória.
void rastro_ajustar_largura(Simulador *s, int largura) {
    if (largura <= s->largura) return;
    /* Refaz o layout: as colunas antigas ganham G_VAZIO para a tarefa nova
     * (ela ainda não existia naqueles ticks). */
    size_t cap = (size_t)(s->capcols ? s->capcols : 1);
    unsigned char *cel = xmalloc(cap * (size_t)largura);
    signed char *cpu = xmalloc(cap * (size_t)largura);
    memset(cel, G_VAZIO, cap * (size_t)largura);
    memset(cpu, -1, cap * (size_t)largura);
    for (int t = 0; t < s->ncols; t++) {
        memcpy(cel + (size_t)t * largura, s->cel + (size_t)t * s->largura, (size_t)s->largura);
        memcpy(cpu + (size_t)t * largura, s->cpu + (size_t)t * s->largura, (size_t)s->largura);
    }
    free(s->cel);
    free(s->cpu);
    s->cel = cel;
    s->cpu = cpu;
    s->ncpus_col = xrealloc(s->ncpus_col, cap);
    s->capcols = (int)cap;
    s->largura = largura;
}

/* =========================================================================
 * 3. REGISTRO DE EVENTOS
 * ========================================================================= */

//Adiciona um novo evento ocorrido (chegada, fim de ativação, perda de prazo, sorteio) ao vetor dinâmico de eventos da simulação.
void evento_add(Simulador *s, int tick, int instante, TipoEvento tipo, int tarefa, int ativ, int cpu) {
    if (s->nev == s->capev) {
        s->capev = s->capev ? s->capev * 2 : 256;
        s->ev = xrealloc(s->ev, (size_t)s->capev * sizeof(Evento));
    }
    s->ev[s->nev++] = (Evento){tick, instante, tipo, tarefa, ativ, cpu};
}

/* ===========================================================================
 * Seção edicao.c (originalmente src/core/edicao.c)
 * =========================================================================== */
/*
 * edicao.c - regras de alteração de tarefas (ver sim.h, seção edicao.h).
 */

/* =========================================================================
 * 1. FUNÇÕES DE VALIDAÇÃO (Regras para aceitar ou recusar edições)
 * ========================================================================= */

//Valida se um número inteiro 'v' é estritamente positivo (> 0). 
//É usada para verificar campos como duração e prazo relativo da tarefa. 
//Retorna NULL se o valor for válido, ou a mensagem de erro caso seja <= 0
const char *valida_positivo(int v) {
    return v > 0 ? NULL : "o valor deve ser maior que zero";
}

//Valida se o período 'v' é permitido no Projeto.
//Recusa o valor 0 (pois indicaria tarefa aperiódica, não simulada no Projeto A) e valores negativos. 
//Retorna NULL se for válido (> 0), ou o motivo da recusa caso contrário.
const char *valida_periodo(int v) {
    if (v == 0) return "período 0 significa tarefa aperiódica, que o Projeto A não simula";
    return v > 0 ? NULL : "o período deve ser maior que zero";
}

//Valida se o instante de primeiro ingresso da tarefa não é menor que o tick atual 'min'. 
//Impede que o usuário tente configurar o ingresso de uma tarefa para um momento no passado.
//Retorna NULL se for válido, ou formata uma mensagem de erro se v < min.
const char *valida_ingresso(int v, int min) {
    static char buf[128];
    if (v >= min) return NULL;
    snprintf(buf, sizeof buf, "o valor deve ser >= %d (não dá para chegar no passado)", min);
    return buf;
}

//Verifica se o ID 'v' informado para a tarefa é único, ou seja, se já não pertence a outra tarefa.
//Percorre o vetor 'ids' ignorando a própria tarefa em edição (índice 'proprio', -1 = tarefa nova).
//"Já pertence" é id_iguais, a mesma regra do arquivo: "t1" repete "T1" e "01" repete "1".
//Retorna NULL se for único, ou a mensagem de erro especificando que o ID já está em uso.
const char *valida_id(const char *v, const char *const *ids, int n, int proprio) {
    static char buf[2 * ID_MAX + 160];
    for (int i = 0; i < n; i++)
        if (i != proprio && id_iguais(ids[i], v)) {
            if (!strcmp(ids[i], v))
                snprintf(buf, sizeof buf, "já existe uma tarefa com id %s", v);    // Formata a mensagem de aviso de ID duplicado usando snprintf()
            else
                snprintf(buf, sizeof buf, "o id %s é o mesmo que o id %s, que já existe (ids não diferenciam "
                         "maiúsculas de minúsculas, e números valem pelo valor: 01 = 1)", v, ids[i]);
            return buf;
        }
    return NULL;
}

/* =========================================================================
 * 2. FUNÇÕES DE APLICAÇÃO DE EDIÇÃO (Alteram o TCB e recalculam dependências)
 * ========================================================================= */

//Aplica um novo valor de período 'v' na tarefa 'k' e recalcula o instante exato 
//em que a próxima ativação da tarefa deve ser liberada na simulação.
void edicao_periodo(TCB *k, int v) {
    k->periodo = v;
    if (k->liberadas > 0 && k->liberadas < tcb_total_ativacoes(k))
        k->proxima_chegada = tcb_chegada(k, k->liberadas - 1) + v;
}

//Aplica um novo prazo relativo 'v' na tarefa 'k'. Se a tarefa estiver com uma ativação em andamento (restante > 0), reajusta imediatamente o seu deadline absoluto.
void edicao_prazo(TCB *k, int v) {
    k->prazo = v;
    if (k->restante > 0) k->deadline_abs = k->chegada_atual + v;
}

//Aplica um novo instante de ingresso inicial 'v' na tarefa 'k'. Esta função só deve ser chamada para tarefas que ainda não ingressaram no sistema.
void edicao_ingresso(TCB *k, int v) {
    k->ingresso = v;
    k->proxima_chegada = v;
    if (tcb_eh_atendida(k) && k->nocorrencias > 0) k->proxima_chegada = v + k->ocorrencias[0];
    k->admitida = false;   /* a tarefa é criada no ingresso novo e passa pelo teste lá */
}

//Alterna a tarefa 'k' entre o estado suspenso manualmente pelo usuário (SUSP_USUARIO) e o seu estado normal de execução/espera. 
//Se já estiver suspensa pelo usuário, restaura o estado correto (T_PRONTA, T_NOVA ou T_SUSPENSA por período).
void edicao_alternar_suspensao(TCB *k) {
    if (suspensa_pelo_usuario(k)) {
        /* Volta para onde estaria se não tivesse sido suspensa: com
         * ativação pendente, pronta; antes do ingresso, nova; senão,
         * esperando o próximo período. */
        if (k->restante > 0) {
            k->estado = T_PRONTA;
            k->motivo = SUSP_NENHUM;
        } else if (k->liberadas == 0) {
            k->estado = T_NOVA;
            k->motivo = SUSP_NENHUM;
        } else {
            k->estado = T_SUSPENSA;
            k->motivo = tcb_eh_servidor(k) ? motivo_ocioso(k) : SUSP_PERIODO;
        }
    } else {
        k->estado = T_SUSPENSA;
        k->motivo = SUSP_USUARIO;
        k->cpu = -1;
    }
}

//Força o encerramento antecipado da tarefa 'k' no instante 't', definindo seu estado como T_TERMINADA e registrando o instante de término.
void edicao_encerrar(TCB *k, int t) {
    k->estado = T_TERMINADA;
    k->motivo = SUSP_NENHUM;
    k->restante = 0;
    k->cpu = -1;
    k->termino = t;
}
