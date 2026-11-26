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
#include "config.h"

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
 *   - tarefa: um programa periódico (ex.: "rode 20 ticks a cada 100").
 *   - ativação (job): cada vez que a tarefa "acorda" para trabalhar. Uma
 *     tarefa periódica tem uma ativação por período; no Projeto A são 10.
 *   - TCB: a ficha da tarefa (sched.h, seção task.h). Tudo sobre ela está lá.
 *   - tick t: o intervalo de tempo [t, t+1).
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
    if (k->estado != T_SUSPENSA) return estado_nome(k->estado);
    /* Servidor parado sem ser pelo usuário: ou acabou a capacidade (espera a
     * recarga), ou não há aperiódica para ele (ocioso). BS não tem
     * capacidade, então só fica ocioso. */
    if (tcb_eh_servidor(k) && k->motivo == SUSP_PERIODO)
        return k->srv != SRV_BS && k->capacidade == 0 ? "suspensa: sem capacidade (espera a recarga)"
                                                      : "suspensa: ocioso (nenhuma aperiódica para ele)";
    if (tcb_eh_aperiodica(k) && k->motivo == SUSP_PERIODO) return "suspensa: espera a próxima ocorrência";
    switch (k->motivo) {
    case SUSP_PERIODO: return "suspensa: próximo período";
    case SUSP_USUARIO: return "suspensa: pelo usuário";
    default: return "suspensa";
    }
}

/* Só a suspensão feita pelo usuário "trava" a tarefa de verdade. A espera
 * pelo próximo período acaba sozinha, então não conta como travada. Muita
 * lógica abaixo depende desta distinção. */
bool suspensa_pelo_usuario(const TCB *k) {
    return k->estado == T_SUSPENSA && k->motivo == SUSP_USUARIO;
}

bool tcb_eh_servidor(const TCB *k) { return k->tipo == TIPO_SERVIDOR; }

bool tcb_eh_aperiodica(const TCB *k) { return k->tipo == TIPO_APERIODICA || k->tipo == TIPO_ESPORADICA; }

/* Chegada da ativação j. A periódica anota a hora em chegadas[] (a chegada
 * pode ter sido empurrada por uma edição); a aperiódica e a esporádica têm a
 * hora fixa no arquivo, então ela é calculada e não precisa de vetor: assim
 * uma esporádica pode ter quantas ocorrências quiser (o vetor chegadas[] tem
 * só MAX_ATIVACOES posições). */
int tcb_chegada(const TCB *k, int j) {
    switch (k->tipo) {
    case TIPO_APERIODICA: return k->ingresso;
    case TIPO_ESPORADICA: return k->ingresso + k->ocorrencias[j];
    default: return k->chegadas[j];
    }
}

/* A ativação em andamento já perdeu o prazo? Periódica: o bit dela em
 * `perdas`. Aperiódica/esporádica: as perdas são marcadas em ordem (ver o
 * passo 4), então ela perdeu se o contador já passou dela. */
bool tcb_prazo_perdido(const TCB *k) {
    if (tcb_eh_aperiodica(k)) return k->prazos_conferidos > k->concluidas;
    return k->concluidas < MAX_ATIVACOES && (k->perdas & (1u << k->concluidas));
}

/* Coluna "ativ." das tabelas (7 caracteres): concluídas/total, com o total
 * de cada tipo (periódica 10, aperiódica 1, esporádica = nº de ocorrências);
 * o servidor não tem ativações, então mostra a capacidade que resta/cheia
 * ("cap 1/2"), e o BS, que não tem capacidade, só "BS". */
void tcb_texto_ativacoes(const TCB *k, char *buf, size_t tam) {
    if (k->srv == SRV_BS) snprintf(buf, tam, "%7s", "BS");
    else if (tcb_eh_servidor(k)) snprintf(buf, tam, "cap %d/%d", k->capacidade, k->duracao);
    else snprintf(buf, tam, "%4d/%-2d", k->concluidas, k->total_ativacoes);
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
    case EV_RECARGA: return "recarga da capacidade";
    case EV_ATENDIMENTO: return "entrou no servidor";
    case EV_DESCARTE: return "capacidade descartada";
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
    k->estado = T_NOVA;            /* ainda não chegou ao sistema             */
    k->proxima_chegada = t->ingresso; /* a 1ª ativação chega no ingresso      */
    k->cpu = -1;                   /* -1 = não está em nenhuma CPU            */
    k->termino = -1;               /* -1 = ainda não terminou                 */
    /* Projeto B. A esporádica chega na 1ª ocorrência (ingresso + o 1º
     * instante da lista); aperiódica tem 1 ativação; periódica, 10. */
    k->tipo = t->tipo;
    k->srv = t->srv;
    k->ocorrencias = t->ocorrencias;
    k->total_ativacoes = t->tipo == TIPO_APERIODICA ? 1 : t->tipo == TIPO_ESPORADICA ? t->nocorr : MAX_ATIVACOES;
    if (t->tipo == TIPO_ESPORADICA && t->nocorr > 0) k->proxima_chegada = t->ingresso + t->ocorrencias[0];
    k->servidor = -1;              /* -1 = nenhum servidor está com ela       */
    k->atendendo = -1;             /* -1 = o servidor não executa ninguém     */
    k->ss_inicio = -1;             /* -1 = SS fora de intervalo ativo         */
}

/* A simulação acaba quando TODAS as tarefas estão terminadas. Servidores não
 * contam: eles existem para atender as outras, então terminam junto com a
 * última delas (passo 4 do tick). Sem esta exceção um servidor (que é
 * periódico e não tem "10 ativações") deixaria a simulação sem fim. */
static bool todas_terminadas(const Estado *e) {
    for (int i = 0; i < e->n; i++)
        if (!tcb_eh_servidor(&e->tarefas[i]) && e->tarefas[i].estado != T_TERMINADA) return false;
    return true;
}

/* =========================================================================
 * 2. INICIALIZAÇÃO E VERIFICAÇÃO DO ESTADO DA SIMULAÇÃO
 * ========================================================================= */

//Inicializa a estrutura do simulador no tick 0, carrega o algoritmo de escalonamento, 
//aloca a matriz do Gráfico de Gantt e salva o primeiro snapshot de memória.
void sim_iniciar(Simulador *s, const Config *cfg, bool historico, uint64_t semente) {
    memset(s, 0, sizeof *s);
    s->historico = historico;
    Estado *e = &s->atual;
    e->ncpus = cfg->ncpus;
    e->quantum = cfg->quantum;
    e->alg = cfg->algoritmo;
    e->rng = semente ? semente : 0x5EED5EEDull;
    e->n = cfg->n;
    /* `cfg->n ? cfg->n : 1`: calloc/malloc de tamanho 0 pode devolver NULL;
     * pedir ao menos 1 evita tratar esse caso em todo lugar. */
    e->tarefas = xcalloc((size_t)(cfg->n ? cfg->n : 1), sizeof(TCB));
    for (int i = 0; i < cfg->n; i++) tcb_iniciar(&e->tarefas[i], &cfg->tarefas[i]);
    e->fim = todas_terminadas(e);
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
    memset(s, 0, sizeof *s);
}

/* Começa a próxima ativação pendente (a de número `concluidas`, contando de
 * 0). Se a tarefa já terminou 3 ativações, a próxima é a de índice 3, e a
 * hora em que ela chegou está guardada em chegadas[3].
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
    /* Aperiódica sem prazo (prazo 0): não há deadline; -1 só aparece na tela. */
    k->deadline_abs = k->prazo > 0 ? k->chegada_atual + k->prazo : -1;
    k->quantum_usado = 0;
    k->fila_desde = agora;
}

/* ------------------------------------------------------------ servidores */
/*
 * Projeto B: tarefas aperiódicas e esporádicas executadas por servidores
 * (seção 2.8 do capítulo "Escalonamento de Tempo Real").
 *
 * Como encaixa no tick sem mexer no que já existia:
 *   - a aperiódica/esporádica NUNCA é candidata no escalonador. Quando chega,
 *     ela entra na FILA DE APERIÓDICAS, que é global e FIFO (o capítulo serve
 *     as requisições aperiódicas em FIFO): quem chegou antes vai antes;
 *   - o servidor é uma tarefa periódica como as outras: disputa a CPU com a
 *     prioridade que o algoritmo der (RM: o período; EDF: chegada_atual +
 *     prazo) e com a mesma cadeia de desempate (req. 4.3). Só que ele só
 *     disputa quando TEM capacidade E TEM o que executar;
 *   - quando um servidor ganha a CPU sem estar com nenhuma aperiódica, ele
 *     tira a primeira da fila e fica com ela até ela terminar: ela passa a
 *     ser inacessível para os outros servidores (req. 2.2 da parte B);
 *   - executar 1 tick do servidor gasta 1 da capacidade dele e 1 do
 *     `restante` da aperiódica. No Gantt as duas linhas mostram a execução,
 *     cada uma com o seu desenho (req. 4 da parte B);
 *   - o BS fica de fora da disputa: depois que todos escolheram, ele pega uma
 *     CPU que tenha sobrado. Por isso ele nunca atrasa ninguém.
 *
 * O que muda de um servidor para outro é só COMO a capacidade volta:
 *   PS e DS: volta cheia no início de cada período (no passo 1);
 *     PS: sem aperiódica pendente, ele perde o que sobrou (o capítulo: "a
 *         tarefa PS se suspende até a sua nova chegada");
 *     DS: guarda a capacidade até o fim do período;
 *   SS: volta só o que foi consumido, no instante RT = início do intervalo
 *       ativo + período.
 */

/* Aperiódica "livre" = está na fila: tem ativação pendente, nenhum servidor
 * está com ela e o usuário não a suspendeu. (Uma que está com um servidor
 * fica PRONTA enquanto ele não executa, mas não está livre.) */
static bool aperiodica_livre(const TCB *k) {
    return tcb_eh_aperiodica(k) && k->restante > 0 && k->servidor < 0 && k->estado == T_PRONTA;
}

/* Ordem da fila de aperiódicas: FIFO pela chegada da ativação; se duas
 * chegaram juntas, a de menor id (id_comparar), para ser sempre a mesma. */
static int cmp_fifo(const TCB *a, const TCB *b) {
    if (a->chegada_atual != b->chegada_atual) return a->chegada_atual < b->chegada_atual ? -1 : 1;
    return id_comparar(a->id, b->id);
}

/* Índice da primeira da fila (-1 = fila vazia). Varredura simples: são
 * poucas tarefas, e assim não há uma fila guardada para copiar em cada
 * snapshot (a ordem sai de chegada_atual, que já está no TCB). */
static int primeira_da_fila(const Estado *e) {
    int melhor = -1;
    for (int i = 0; i < e->n; i++)
        if (aperiodica_livre(&e->tarefas[i]) && (melhor < 0 || cmp_fifo(&e->tarefas[i], &e->tarefas[melhor]) < 0))
            melhor = i;
    return melhor;
}

/* Servidor que pode trabalhar: já entrou no sistema, não terminou e não foi
 * suspenso pelo usuário. */
static bool servidor_ativo(const TCB *k) {
    return tcb_eh_servidor(k) && k->liberadas > 0 && k->estado != T_TERMINADA && !suspensa_pelo_usuario(k);
}

/* O servidor `sv` tira a aperiódica `a` da fila (req. 2.2 da parte B). O
 * evento marca no Gantt o instante em que ela entrou no servidor. */
static void servidor_pegar(Simulador *s, int sv, int a) {
    Estado *e = &s->atual;
    e->tarefas[sv].atendendo = a;
    e->tarefas[a].servidor = sv;
    evento_add(s, e->t, e->t, EV_ATENDIMENTO, a, e->tarefas[a].concluidas, e->tarefas[sv].cpu);
    s->ev[s->nev - 1].aux = sv;
}

/* O servidor `sv` larga a aperiódica que estava com ele (ela terminou a
 * ativação, foi suspensa/encerrada pelo usuário ou o servidor parou). */
static void servidor_soltar(Estado *e, int sv) {
    int a = e->tarefas[sv].atendendo;
    if (a >= 0) e->tarefas[a].servidor = -1;
    e->tarefas[sv].atendendo = -1;
}

/* SS: aplica as recargas que já venceram (instante <= t). Cada uma devolve a
 * capacidade consumida num intervalo ativo; o teto é a capacidade cheia. */
static void ss_aplicar_recargas(Simulador *s, int i) {
    TCB *k = &s->atual.tarefas[i];
    int t = s->atual.t;
    for (int r = 0; r < k->nrecargas;) {
        if (k->recarga_inst[r] > t) {
            r++;
            continue;
        }
        int q = k->recarga_qtd[r];
        k->capacidade += q;
        if (k->capacidade > k->duracao) k->capacidade = k->duracao;
        k->restante = k->capacidade;
        evento_add(s, t, t, EV_RECARGA, i, k->liberadas - 1, -1);
        s->ev[s->nev - 1].aux = q;
        /* tira a recarga r do vetor, puxando as seguintes uma posição */
        for (int j = r + 1; j < k->nrecargas; j++) {
            k->recarga_inst[j - 1] = k->recarga_inst[j];
            k->recarga_qtd[j - 1] = k->recarga_qtd[j];
        }
        k->nrecargas--;
    }
}

/* SS: fim do intervalo ativo (o servidor deixou de estar pronto: acabou a
 * capacidade ou não há mais aperiódica para ele). O capítulo: a recarga
 * acontece em RT = início do intervalo ativo + período e devolve o que foi
 * consumido NESTE intervalo. Ex. da figura 2.17: o intervalo começa em 4,5,
 * C consome 1 e a recarga de 1 fica para 4,5 + 10 = 14,5.
 * Se o intervalo durou um período inteiro (o SS ficou pronto, mas preso atrás
 * de tarefas mais prioritárias), RT já passou: a recarga é aplicada agora,
 * que é o primeiro instante em que se sabe quanto recarregar. */
static void ss_fechar_intervalo(Simulador *s, int i) {
    TCB *k = &s->atual.tarefas[i];
    if (k->ss_consumido > 0) {
        int rt = k->ss_inicio + k->periodo;
        if (k->nrecargas < MAX_RECARGAS) {
            k->recarga_inst[k->nrecargas] = rt;
            k->recarga_qtd[k->nrecargas] = k->ss_consumido;
            k->nrecargas++;
        } else {
            k->recarga_qtd[MAX_RECARGAS - 1] += k->ss_consumido;   /* lotou: ver MAX_RECARGAS */
        }
        if (rt <= s->atual.t) ss_aplicar_recargas(s, i);
    }
    k->ss_inicio = -1;
    k->ss_consumido = 0;
}

/* Passo 1 para um servidor: entrada no sistema e recargas. */
static void chegada_servidor(Simulador *s, int i) {
    Estado *e = &s->atual;
    TCB *k = &e->tarefas[i];
    int t = e->t;
    if (k->estado == T_TERMINADA) return;
    if (k->liberadas == 0) {
        /* Entrada do servidor (no ingresso): capacidade cheia. Fica
         * "suspenso: ocioso" até ter aperiódica para executar. O evento de
         * chegada na linha do servidor é desenhado como a entrada dele. */
        if (t < k->proxima_chegada) return;
        k->liberadas = 1;
        k->chegadas[0] = t;
        k->chegada_atual = t;
        k->deadline_abs = t + k->prazo;
        k->capacidade = k->srv == SRV_BS ? 0 : k->duracao;
        k->restante = k->capacidade;
        k->proxima_chegada = t + k->periodo;
        if (!suspensa_pelo_usuario(k)) {
            k->estado = T_SUSPENSA;
            k->motivo = SUSP_PERIODO;
        }
        evento_add(s, t, t, EV_CHEGADA, i, 0, -1);
        return;
    }
    if (k->srv == SRV_SS) {
        ss_aplicar_recargas(s, i);
    } else if ((k->srv == SRV_PS || k->srv == SRV_DS) && k->periodo > 0 && t >= k->proxima_chegada) {
        /* Novo período: capacidade cheia de novo, e o deadline (usado pelo
         * EDF) passa a ser o fim deste período (chegada + prazo). */
        int antes = k->capacidade;
        k->capacidade = k->duracao;
        k->restante = k->capacidade;
        k->chegada_atual = t;
        k->deadline_abs = t + k->prazo;
        k->liberadas++;
        k->proxima_chegada += k->periodo;
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;   /* edição pulou períodos */
        /* Só vira evento se a capacidade voltou de fato: um DS que não usou
         * nada no período já está cheio, e "recarga de 0" seria ruído. */
        if (k->duracao > antes) {
            evento_add(s, t, t, EV_RECARGA, i, k->liberadas - 1, -1);
            s->ev[s->nev - 1].aux = k->duracao - antes;
        }
    }
}

/* Marca se o servidor disputa a CPU neste tick. Quem disputa fica PRONTO
 * (ou continua EXECUTANDO, para valer o critério 1 do desempate); quem não
 * disputa fica "suspenso" (ocioso ou sem capacidade) e larga a CPU. O
 * `restante` do servidor espelha a capacidade: é o que ele ainda pode
 * executar, e é o que um escalonador por fórmula (ex.: LLF) enxerga. */
static void servidor_marcar(TCB *k, bool disputa, int t) {
    k->restante = k->capacidade;
    if (disputa) {
        if (k->estado != T_EXECUTANDO) k->estado = T_PRONTA;
        k->motivo = SUSP_NENHUM;
        if (k->srv == SRV_SS && k->ss_inicio < 0) {
            /* Começa um intervalo ativo. No EDF o prazo do SS conta daqui
             * (deadline = RT quando prazo = período), como no SS dinâmico. */
            k->ss_inicio = t;
            k->chegada_atual = t;
            k->deadline_abs = t + k->prazo;
        }
    } else {
        k->estado = T_SUSPENSA;
        k->motivo = SUSP_PERIODO;
        k->cpu = -1;
        k->quantum_usado = 0;
    }
}

/* Passo 2a (antes do escalonador): decide quais servidores PS/DS/SS disputam
 * a CPU neste tick. Regras, nesta ordem:
 *   1. servidor parado (suspenso/encerrado pelo usuário) larga a aperiódica
 *      que estava com ele, e a aperiódica que foi suspensa/encerrada também
 *      é largada: as duas voltam a valer para a fila;
 *   2. SS que deixou de estar pronto fecha o intervalo ativo (recarga);
 *   3. sem capacidade: não disputa;
 *      com uma aperiódica: disputa;
 *      sem aperiódica e fila vazia: não disputa, e o PS perde a capacidade;
 *      sem aperiódica e fila com L tarefas livres: só os L servidores de
 *      maior prioridade disputam (cada um vai pegar uma). Assim nunca ganha
 *      a CPU um servidor que não teria o que executar.
 * Devolve false se o escalonador não conseguiu calcular uma prioridade
 * (motivo em s->erro), como em escalonar(). */
static bool preparar_servidores(Simulador *s) {
    Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    int n = e->n, t = e->t;

    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->atendendo < 0) continue;
        TCB *a = &e->tarefas[k->atendendo];
        if (servidor_ativo(k) && a->restante > 0 && a->estado != T_TERMINADA && !suspensa_pelo_usuario(a)) continue;
        if (a->estado == T_EXECUTANDO) {
            a->estado = T_PRONTA;
            a->cpu = -1;
        }
        servidor_soltar(e, i);
    }

    int livres = 0;
    for (int i = 0; i < n; i++)
        if (aperiodica_livre(&e->tarefas[i])) livres++;

    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (k->srv != SRV_SS || k->ss_inicio < 0) continue;
        if (!(servidor_ativo(k) && k->capacidade > 0 && (k->atendendo >= 0 || livres > 0))) ss_fechar_intervalo(s, i);
    }

    int *sem = xmalloc((size_t)(n ? n : 1) * sizeof(int));   /* com capacidade, mas sem aperiódica */
    int nsem = 0;
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->srv == SRV_BS || !servidor_ativo(k)) continue;
        if (k->capacidade > 0 && k->atendendo < 0 && livres > 0) {
            if (k->srv == SRV_SS && k->ss_inicio < 0) {
                /* prioridade de quem abriria o intervalo agora (EDF) */
                k->chegada_atual = t;
                k->deadline_abs = t + k->prazo;
            }
            k->restante = k->capacidade;
            sem[nsem++] = i;
            continue;
        }
        if (k->srv == SRV_PS && k->capacidade > 0 && k->atendendo < 0) {
            /* Nada pendente: o PS se suspende até o próximo período e a
             * capacidade dele fica para as periódicas (figura 2.15: em
             * t = 10 não há aperiódica e o PS não atende D, que chega em 12). */
            evento_add(s, t, t, EV_DESCARTE, i, k->liberadas - 1, -1);
            s->ev[s->nev - 1].aux = k->capacidade;
            k->capacidade = 0;
        }
        servidor_marcar(k, k->capacidade > 0 && k->atendendo >= 0, t);
    }

    /* Os sem aperiódica, do mais para o menos prioritário (mesma comparação
     * do escalonador; empate total fica na ordem do vetor). Ordenação por
     * inserção: são poucos servidores. */
    for (int j = 0; j < nsem; j++) {
        if (sched_atualizar(esc, &e->tarefas[sem[j]], t, s->erro, sizeof s->erro)) {
            free(sem);
            return false;
        }
    }
    for (int j = 1; j < nsem; j++) {
        int x = sem[j], p = j;
        while (p > 0 && sched_comparar(&e->tarefas[x], &e->tarefas[sem[p - 1]]) < 0) {
            sem[p] = sem[p - 1];
            p--;
        }
        sem[p] = x;
    }
    for (int j = 0; j < nsem; j++) servidor_marcar(&e->tarefas[sem[j]], j < livres, t);
    free(sem);
    return true;
}

/* Passo 2c (depois do escalonador): cada servidor PS/DS/SS que ganhou a CPU
 * sem estar com uma aperiódica pega a primeira da fila, do mais para o menos
 * prioritário; depois o BS pega uma CPU que sobrou; por fim, cada
 * aperiódica fica no estado do seu servidor (executando na CPU dele, ou
 * pronta esperando ele voltar). */
static void distribuir_aperiodicas(Simulador *s) {
    Estado *e = &s->atual;
    int n = e->n;

    int *vez = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int nv = 0;
    for (int i = 0; i < n; i++) {
        const TCB *k = &e->tarefas[i];
        if (tcb_eh_servidor(k) && k->srv != SRV_BS && k->estado == T_EXECUTANDO && k->atendendo < 0) vez[nv++] = i;
    }
    for (int j = 1; j < nv; j++) {
        int x = vez[j], p = j;
        while (p > 0 && sched_comparar(&e->tarefas[x], &e->tarefas[vez[p - 1]]) < 0) {
            vez[p] = vez[p - 1];
            p--;
        }
        vez[p] = x;
    }
    for (int j = 0; j < nv; j++) {
        int a = primeira_da_fila(e);
        if (a >= 0) servidor_pegar(s, vez[j], a);
    }
    free(vez);

    /* BS: usa uma CPU que ficou livre depois do escalonador (prioridade
     * abaixo de todas). Volta para a mesma CPU se ela estiver livre. Vários
     * BS: na ordem do arquivo. */
    bool ocupada[MAX_CPUS] = {false};
    for (int i = 0; i < n; i++) {
        const TCB *k = &e->tarefas[i];
        if (k->estado == T_EXECUTANDO && !tcb_eh_aperiodica(k) && k->srv != SRV_BS && k->cpu >= 0 && k->cpu < MAX_CPUS)
            ocupada[k->cpu] = true;
    }
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (k->srv != SRV_BS || !servidor_ativo(k)) continue;
        bool tem = k->atendendo >= 0 || primeira_da_fila(e) >= 0;
        int c = -1;
        if (tem) {
            if (k->cpu >= 0 && k->cpu < e->ncpus && !ocupada[k->cpu]) c = k->cpu;
            for (int x = 0; x < e->ncpus && c < 0; x++)
                if (!ocupada[x]) c = x;
        }
        if (c >= 0) {
            ocupada[c] = true;
            k->cpu = c;
            k->estado = T_EXECUTANDO;
            k->motivo = SUSP_NENHUM;
            if (k->atendendo < 0) servidor_pegar(s, i, primeira_da_fila(e));
        } else {
            if (k->cpu >= 0 && tem) {
                /* estava executando e uma tarefa mais prioritária pegou a CPU */
                k->preempcoes++;
                evento_add(s, e->t, e->t, EV_PREEMPCAO, i, k->liberadas - 1, k->cpu);
            }
            k->cpu = -1;
            k->estado = tem ? T_PRONTA : T_SUSPENSA;
            k->motivo = tem ? SUSP_NENHUM : SUSP_PERIODO;
        }
    }

    for (int i = 0; i < n; i++) {
        TCB *a = &e->tarefas[i];
        if (!tcb_eh_aperiodica(a) || a->estado == T_TERMINADA || suspensa_pelo_usuario(a)) continue;
        const TCB *sv = a->servidor >= 0 ? &e->tarefas[a->servidor] : NULL;
        if (sv && sv->estado == T_EXECUTANDO) {
            a->estado = T_EXECUTANDO;
            a->cpu = sv->cpu;
        } else {
            if (a->estado == T_EXECUTANDO) a->estado = T_PRONTA;
            a->cpu = -1;
        }
    }
}

/* ------------------------------------------------------------ escalonar */

/* O ESCALONADOR: decide quem roda em cada CPU neste tick.
 *
 * Em cada rodada pegamos o grupo das "melhores" tarefas restantes (as que
 * empatam em prioridade E nos critérios 1-4). Se o grupo cabe nas CPUs
 * livres, todas entram e a ordem entre elas não importa. Se não cabe,
 * sorteamos quais entram (critério 5) e marcamos o evento de sorteio. Assim
 * só há sorteio quando ele realmente decide quem executa.
 *
 * Exemplo com 2 CPUs e candidatas A (prio 5), B (prio 5), C (prio 9):
 *   rodada 1: melhor = A; grupo = {A, B} (empatam); cabe nas 2 CPUs -> entram.
 *   C fica de fora (CPUs acabaram). Não houve sorteio.
 * Com 1 CPU: rodada 1 grupo = {A, B}, mas só 1 CPU livre -> sorteia entre A e B. */
static bool escalonar(Simulador *s) {
    Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    int n = e->n, t = e->t;

    /* Vetores de trabalho (liberados no fim da função):
     *   cand      - índices das tarefas candidatas a executar neste tick
     *   grupo     - empatadas da rodada atual
     *   escolhido - quem ganhou uma CPU
     *   sorteado  - quem ganhou a CPU por sorteio (para marcar no Gantt) */
    /* Projeto B: antes, decide quais servidores disputam a CPU (ver
     * preparar_servidores). Eles entram como candidatas comuns abaixo. */
    if (!preparar_servidores(s)) return false;

    int *cand = xmalloc((size_t)(n ? n : 1) * sizeof(int));

    /* Candidata = está pronta OU já estava executando, e ainda tem trabalho
     * a fazer (restante > 0). Para cada uma, recalculamos a prioridade com o
     * algoritmo atual (RM, EDF, plugin ou fórmula). Se o escalonador não
     * conseguir (ex.: divisão por zero numa fórmula), nada foi decidido ainda:
     * devolvemos false com o motivo em s->erro, sem inventar uma prioridade.
     * Aperiódicas e esporádicas não são candidatas (só executam por um
     * servidor) e o BS também não (ele só pega CPU que sobrar, no fim). */
    int nc = 0;
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if (tcb_eh_aperiodica(k) || k->srv == SRV_BS) continue;
        if ((k->estado == T_PRONTA || k->estado == T_EXECUTANDO) && k->restante > 0) {
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
    }
    int *grupo = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    bool *escolhido = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    bool *sorteado = xcalloc((size_t)(n ? n : 1), sizeof(bool));

    int livres = e->ncpus;     /* CPUs ainda sem tarefa nesta rodada          */
    int restantes = nc;        /* candidatas ainda sem decisão                */
    while (livres > 0 && restantes > 0) {
        /* melhor entre os não escolhidos: varredura simples; sched_comparar
         * < 0 significa "a vem antes de b" (prioridade ativa + desempate). */
        int melhor = -1;
        for (int j = 0; j < nc; j++) {
            if (escolhido[cand[j]]) continue;
            if (melhor < 0 || sched_comparar(&e->tarefas[cand[j]], &e->tarefas[melhor]) < 0)
                melhor = cand[j];
        }
        /* grupo = todas as que empatam COM a melhor (comparar == 0). */
        int ng = 0;
        for (int j = 0; j < nc; j++) {
            int i = cand[j];
            if (!escolhido[i] && sched_comparar(&e->tarefas[i], &e->tarefas[melhor]) == 0)
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
    /* Projeto B: servidores escolhidos pegam aperiódicas, o BS pega o que
     * sobrou e as aperiódicas acompanham o seu servidor. */
    distribuir_aperiodicas(s);
    free(cand);
    free(grupo);
    free(escolhido);
    free(sorteado);
    free(nova_cpu);
    return true;
}

/* Passo 4 para uma aperiódica/esporádica, no instante t+1: fim da ativação
 * (ela sai do servidor) e perda de prazo. Igual ao da periódica, com duas
 * diferenças: o total de ativações é o da tarefa (1, ou o nº de ocorrências)
 * e as perdas são marcadas em ordem com um contador, e não com um bit por
 * ativação, porque uma esporádica pode ter mais de 32 ocorrências. A ordem
 * vale porque as chegadas crescem e o prazo é o mesmo para todas: se a
 * ativação j ainda não venceu, a j+1 também não. */
static void verificar_aperiodica(Simulador *s, int i) {
    Estado *e = &s->atual;
    TCB *k = &e->tarefas[i];
    int t = e->t;
    if (k->estado == T_EXECUTANDO && k->restante == 0) {
        int resp = t + 1 - k->chegada_atual;
        if (resp > k->resposta_max) k->resposta_max = resp;
        k->resposta_soma += resp;
        k->concluidas++;
        evento_add(s, t, t + 1, EV_FIM_ATIVACAO, i, k->concluidas - 1, k->cpu);
        if (k->servidor >= 0) servidor_soltar(e, k->servidor);
        k->cpu = -1;
        if (k->concluidas >= k->total_ativacoes) {
            k->estado = T_TERMINADA;
            k->termino = t + 1;
            evento_add(s, t, t + 1, EV_TERMINO, i, k->concluidas - 1, -1);
        } else if (k->liberadas > k->concluidas) {
            iniciar_ativacao(k, t + 1);   /* a próxima ocorrência já chegou: volta para a fila */
            k->estado = T_PRONTA;
        } else {
            k->estado = T_SUSPENSA;       /* espera a próxima ocorrência */
            k->motivo = SUSP_PERIODO;
        }
    }
    if (k->estado == T_TERMINADA || k->prazo <= 0) return;   /* prazo 0 = aperiódica sem prazo */
    int j = k->prazos_conferidos > k->concluidas ? k->prazos_conferidos : k->concluidas;
    for (; j < k->liberadas; j++) {
        int deadline = tcb_chegada(k, j) + k->prazo;
        if (deadline > t + 1) break;
        k->deadlines_perdidos++;
        int inst = deadline > t ? deadline : t + 1;
        evento_add(s, t, inst, EV_PRAZO_PERDIDO, i, j, -1);
    }
    k->prazos_conferidos = j;
}

/* Quando a última tarefa que não é servidor termina, os servidores terminam
 * junto (no mesmo instante): não há mais nada para eles executarem. */
static void encerrar_servidores(Simulador *s, int instante) {
    Estado *e = &s->atual;
    if (!todas_terminadas(e)) return;
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->estado == T_TERMINADA) continue;
        servidor_soltar(e, i);
        k->estado = T_TERMINADA;
        k->motivo = SUSP_NENHUM;
        k->cpu = -1;
        k->termino = instante;
        evento_add(s, e->t, instante, EV_TERMINO, i, k->liberadas > 0 ? k->liberadas - 1 : 0, -1);
    }
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
    int nev_antes = s->nev, ncols_antes = s->ncols;
    /* Garante que o rastro do Gantt tem espaço para este tick e para todas
     * as tarefas (uma tarefa pode ter sido adicionada pelo usuário). */
    rastro_ajustar_largura(s, e->n);
    rastro_garantir_coluna(s, t);

    /* 1. Chegadas. Usamos >= (e não ==) para que uma edição de ingresso ou
     * período que "pule" o instante exato não perca a ativação. */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        /* Servidor: entrada no sistema e recarga da capacidade. */
        if (tcb_eh_servidor(k)) {
            chegada_servidor(s, i);
            continue;
        }
        /* Aperiódica/esporádica: a ativação entra na fila de aperiódicas
         * (fica PRONTA, mas só um servidor a executa). A próxima ocorrência
         * da esporádica vem da lista do arquivo, não do período. */
        if (tcb_eh_aperiodica(k)) {
            if (k->estado == T_TERMINADA || k->liberadas >= k->total_ativacoes || t < k->proxima_chegada) continue;
            evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
            k->liberadas++;
            if (k->liberadas < k->total_ativacoes) k->proxima_chegada = tcb_chegada(k, k->liberadas);
            if (k->restante == 0) {
                iniciar_ativacao(k, t);
                if (!suspensa_pelo_usuario(k)) {
                    k->estado = T_PRONTA;
                    k->motivo = SUSP_NENHUM;
                }
            }
            continue;
        }
        /* Terminadas, ou que já liberaram as 10 ativações: nada a fazer. */
        if (k->estado == T_TERMINADA || k->liberadas >= MAX_ATIVACOES) continue;
        if (t < k->proxima_chegada) continue;   /* ainda não é a hora */
        k->chegadas[k->liberadas] = t;          /* anota a hora da chegada */
        evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
        k->liberadas++;
        k->proxima_chegada += k->periodo;
        /* Se a edição fez a próxima chegada ficar no passado, reancora no
         * tick atual para não chegar atrasadas. */
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;
        /* Se a ativação anterior ainda não terminou, esta fica acumulada e
         * começa assim que a anterior acabar */
        if (k->restante == 0) {
            iniciar_ativacao(k, t);
            /* Suspensa pelo usuário continua suspensa (a ativação fica
             * esperando a retomada); a espera pelo período acabou. */
            if (!suspensa_pelo_usuario(k)) {
                k->estado = T_PRONTA;
                k->motivo = SUSP_NENHUM;
            }
        }
    }

    /* 2. Escalonamento: decide quem executa neste tick (função acima). */
    if (!escalonar(s)) {
        estado_copiar(e, &antes);   /* desfaz as chegadas deste tick */
        estado_liberar(&antes);
        s->nev = nev_antes;         /* e os eventos e a coluna do Gantt */
        s->ncols = ncols_antes;
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
            s->cpu[base + i] = (signed char)k->cpu;
            k->ticks_exec++;
            k->quantum_usado++;
            if (tcb_eh_servidor(k)) {
                /* O servidor gasta 1 da capacidade (o BS não tem); o SS
                 * anota o consumo do intervalo ativo, que é o que volta na
                 * recarga. O `restante` da aperiódica cai na linha dela. */
                s->cel[base + i] = G_SERVIDOR;
                if (k->srv != SRV_BS) {
                    k->capacidade--;
                    k->restante = k->capacidade;
                    if (k->srv == SRV_SS) k->ss_consumido++;
                }
            } else {
                s->cel[base + i] = tcb_eh_aperiodica(k) ? G_APERIODICA : G_EXEC;
                k->restante--;          /* consumiu 1 tick de CPU */
            }
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
        if (tcb_eh_servidor(k)) continue;   /* servidor não tem ativação nem prazo próprios */
        if (tcb_eh_aperiodica(k)) {
            verificar_aperiodica(s, i);
            continue;
        }
        if (k->estado == T_EXECUTANDO && k->restante == 0) {
            /* Terminou a ativação. Tempo de resposta = fim - chegada. */
            int resp = t + 1 - k->chegada_atual;
            if (resp > k->resposta_max) k->resposta_max = resp;
            k->resposta_soma += resp;
            k->concluidas++;
            evento_add(s, t, t + 1, EV_FIM_ATIVACAO, i, k->concluidas - 1, k->cpu);
            if (k->concluidas >= MAX_ATIVACOES) {
                /* Era a 10ª: a tarefa periódica "termina" (req. 4.4). */
                k->estado = T_TERMINADA;
                k->termino = t + 1;
                k->cpu = -1;
                evento_add(s, t, t + 1, EV_TERMINO, i, k->concluidas - 1, -1);
            } else if (k->liberadas > k->concluidas) {
                /* Próxima ativação já estava acumulada. Mantemos `cpu` para
                 * que ela conte como "estava executando". */
                iniciar_ativacao(k, t + 1);
                k->estado = T_PRONTA;
            } else {
                /* Nada pendente: espera o próximo período. */
                k->estado = T_SUSPENSA;
                k->motivo = SUSP_PERIODO;
                /* Se a próxima ativação chega agora no instante t+1
                 * (ela só é liberada no passo 1 do próximo tick), a tarefa
                 * continua contando como "estava executando" para o critério
                 * 1, igual ao caso da ativação acumulada acima. */
                if (!(k->liberadas < MAX_ATIVACOES && k->proxima_chegada == t + 1))
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
        for (int j = k->concluidas; j < k->liberadas; j++) {
            /* `perdas` é um número usado como vetor de bits: o bit j fica
             * ligado quando a ativação j já foi marcada como perdida, para
             * não marcar duas vezes. (1u << j) cria um número com só o bit j. */
            unsigned bit = 1u << j;
            int deadline = k->chegadas[j] + k->prazo;
            if ((k->perdas & bit) || deadline > t + 1) continue;
            k->perdas |= bit;
            k->deadlines_perdidos++;
            /* deadline < t+1 só acontece se o prazo foi reduzido por edição
             * para um instante que já passou: aí a perda é detectada agora. */
            int inst = deadline > t ? deadline : t + 1;
            evento_add(s, t, inst, EV_PRAZO_PERDIDO, i, j, -1);
        }
    }
    encerrar_servidores(s, t + 1);

    /* 5. O relógio avança e vemos se acabou tudo. */
    e->t++;
    e->fim = todas_terminadas(e);
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
 * pelo usuário: nada vai progredir sozinho. Sem esta checagem o "rodar até o
 * fim" ficaria em laço infinito. Tarefas esperando o período não contam,
 * pois voltam sozinhas. */
bool sim_bloqueado(const Estado *e) {
    bool alguma = false;
    for (int i = 0; i < e->n; i++) {
        EstadoTarefa st = e->tarefas[i].estado;
        /* Servidor não conta: ele só termina junto com as outras (ver
         * todas_terminadas), então nunca é ele que faz a simulação andar. */
        if (st == T_TERMINADA || tcb_eh_servidor(&e->tarefas[i])) continue;
        /* Quem espera o período volta sozinha; só o usuário trava a tarefa. */
        if (!suspensa_pelo_usuario(&e->tarefas[i])) return false;
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
            return "todas as tarefas restantes estão suspensas; retome alguma para continuar";
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
    encerrar_servidores(s, s->atual.t);   /* a edição pode ter encerrado a última tarefa */
    s->atual.fim = todas_terminadas(&s->atual);
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

/* Linha das CPUs no Gantt: o rastro guarda "qual CPU a tarefa i usou"; aqui
 * é o inverso, "quem usou a CPU c". Com servidores, o servidor e a aperiódica
 * que ele executa marcam a MESMA CPU no mesmo tick: a CPU mostra a
 * aperiódica (é o código dela que roda), e o servidor sai à parte por
 * sim_servidor_cpu. */
int sim_ocupante_cpu(const Simulador *s, int tick, int c) {
    int quem = -1;
    for (int i = 0; i < s->atual.n; i++) {
        if (sim_cpu_celula(s, tick, i) != c) continue;
        if (sim_celula(s, tick, i) != G_SERVIDOR) return i;
        quem = i;
    }
    return quem;
}

int sim_servidor_cpu(const Simulador *s, int tick, int c) {
    for (int i = 0; i < s->atual.n; i++)
        if (sim_cpu_celula(s, tick, i) == c && sim_celula(s, tick, i) == G_SERVIDOR) return i;
    return -1;
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

/* Utilização do processador: U = soma de C/P (duração / período). Se U > 1
 * numa CPU só, é impossível cumprir todos os prazos. */
double sim_utilizacao(const Estado *e) {
    double u = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        u += tarefa_utilizacao(k->tipo, k->srv, k->duracao, k->periodo);   /* aperiódica (período 0) não divide por zero */
    }
    return u;
}

/* As tarefas do estado atual no formato do teste de escalonabilidade (ver
 * teste_calcular em config.c). As que já terminaram ficam de fora: não usam
 * mais CPU, então não pesam para uma tarefa nova que entra agora. O limite do
 * RM (que ficava aqui como sim_limite_rm) passou para limite_rm, em config.c,
 * porque o teste também roda na carga do arquivo. */
ItemTeste *sim_itens_teste(const Estado *e, int *n) {
    ItemTeste *v = xmalloc((size_t)(e->n ? e->n : 1) * sizeof *v);
    int m = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (k->estado != T_TERMINADA) v[m++] = (ItemTeste){k->tipo, k->srv, k->duracao, k->periodo, k->prazo, k->id};
    }
    *n = m;
    return v;
}

/* Teste para a tarefa `t` entrar durante a simulação ("+ tarefa"), contra
 * as que ainda não terminaram, com o algoritmo e as CPUs do instante atual. */
const char *sim_admitir_tarefa(const Estado *e, const TarefaCfg *t, char *buf, size_t tam) {
    int n;
    ItemTeste *v = sim_itens_teste(e, &n);
    const char *m = teste_admitir(v, n, (ItemTeste){t->tipo, t->srv, t->duracao, t->periodo, t->prazo, t->id}, e->alg, e->ncpus, buf, tam);
    free(v);
    return m;
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
    s->ev[s->nev++] = (Evento){tick, instante, tipo, tarefa, ativ, cpu, -1};   /* aux: quem precisa preenche depois */
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

//Valida o novo período de uma tarefa PERIÓDICA ou de um servidor (edição).
//Recusa 0 e negativos: eles mudariam o tipo da tarefa (aperiódica/esporádica), e o tipo não muda depois de criada.
//Retorna NULL se for válido (> 0), ou o motivo da recusa caso contrário.
const char *valida_periodo(int v) {
    if (v == 0) return "período 0 tornaria a tarefa aperiódica; o tipo de uma tarefa não muda depois de criada "
                       "(para uma aperiódica, adicione uma tarefa nova)";
    if (v < 0) return "período negativo tornaria a tarefa esporádica; o tipo de uma tarefa não muda depois de "
                      "criada (para uma esporádica, adicione uma tarefa nova)";
    return NULL;
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
    if (k->liberadas > 0 && k->liberadas < MAX_ATIVACOES)
        k->proxima_chegada = k->chegadas[k->liberadas - 1] + v;
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
            k->motivo = SUSP_PERIODO;
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
