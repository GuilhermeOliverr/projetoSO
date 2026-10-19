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
 *   - TCB: a ficha da tarefa (task.h). Tudo sobre ela está lá.
 *   - tick t: o intervalo de tempo [t, t+1).
 */
#include "sim.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
    \* 1\. GERENCIAMENTO DE EVENTOS E SNAPSHOTS DA SIMULAÇÃO \*
========================================================================= \*/

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
    default: return "?";
    }
}

/* Monta o TCB inicial de uma tarefa a partir do que veio da configuração.
 * O memset zera TUDO primeiro (contadores, bits de perda, estatísticas), e
 * depois só preenchemos o que não é zero. */
void tcb_iniciar(TCB *k, const TarefaCfg *t) {
    memset(k, 0, sizeof *k); //zerar toda a memória da estrutura ou objeto apontado pelo ponteiro k
    k->id = t->id;
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
}

/* A simulação acaba quando TODAS as tarefas estão terminadas. */
static bool todas_terminadas(const Estado *e) {
    for (int i = 0; i < e->n; i++)
        if (e->tarefas[i].estado != T_TERMINADA) return false;
    return true;
}

/* ========================================================================= 
    \* 2\. INICIALIZAÇÃO E VERIFICAÇÃO DO ESTADO DA SIMULAÇÃO \*
========================================================================= \*/

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
 * do arquivo. Ex.: chegou em 30, prazo 10 -> precisa terminar até o instante 40. */
static void iniciar_ativacao(TCB *k) {
    k->restante = k->duracao;
    k->chegada_atual = k->chegadas[k->concluidas];
    k->deadline_abs = k->chegada_atual + k->prazo;
    k->quantum_usado = 0;
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
static void escalonar(Simulador *s) {
    Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    int n = e->n, t = e->t;

    /* Vetores de trabalho (liberados no fim da função):
     *   cand      - índices das tarefas candidatas a executar neste tick
     *   grupo     - empatadas da rodada atual
     *   escolhido - quem ganhou uma CPU
     *   sorteado  - quem ganhou a CPU por sorteio (para marcar no Gantt) */
    int *cand = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int *grupo = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    bool *escolhido = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    bool *sorteado = xcalloc((size_t)(n ? n : 1), sizeof(bool));

    /* Candidata = está pronta OU já estava executando, e ainda tem trabalho
     * a fazer (restante > 0). Para cada uma, recalculamos a prioridade com o
     * algoritmo atual (RM, EDF ou plugin). */
    int nc = 0;
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if ((k->estado == T_PRONTA || k->estado == T_EXECUTANDO) && k->restante > 0) {
            sched_atualizar(esc, k, t);
            cand[nc++] = i;
        }
    }

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
            /* Não cabem: critério 5, sorteio. Fisher-Yates parcial: as
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
            k->cpu = -1;
            k->quantum_usado = 0;
            k->estado = T_PRONTA;
        }
    }
    free(cand);
    free(grupo);
    free(escolhido);
    free(sorteado);
    free(nova_cpu);
}

/* ------------------------------------------------------------------ tick */

/* Executa UM tick completo (os 5 passos descritos no topo do arquivo). */
static void executar_tick(Simulador *s) {
    Estado *e = &s->atual;
    int t = e->t;
    /* Garante que o rastro do Gantt tem espaço para este tick e para todas
     * as tarefas (uma tarefa pode ter sido adicionada pelo usuário). */
    rastro_ajustar_largura(s, e->n);
    rastro_garantir_coluna(s, t);

    /* 1. Chegadas. Usamos >= (e não ==) para que uma edição de ingresso ou
     * período que "pule" o instante exato não perca a ativação. */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        /* Terminadas, ou que já liberaram as 10 ativações: nada a fazer. */
        if (k->estado == T_TERMINADA || k->liberadas >= MAX_ATIVACOES) continue;
        if (t < k->proxima_chegada) continue;   /* ainda não é a hora */
        k->chegadas[k->liberadas] = t;          /* anota a hora da chegada */
        evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
        k->liberadas++;
        k->proxima_chegada += k->periodo;
        /* Se a edição fez a próxima chegada ficar no passado, reancora no
         * tick atual para não gerar uma rajada de chegadas atrasadas. */
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;
        /* Se a ativação anterior ainda não terminou, esta fica acumulada e
         * começa assim que a anterior acabar (ver README, decisões). */
        if (k->restante == 0) {
            iniciar_ativacao(k);
            /* Suspensa pelo usuário continua suspensa (a ativação fica
             * esperando a retomada); a espera pelo período acabou. */
            if (!suspensa_pelo_usuario(k)) {
                k->estado = T_PRONTA;
                k->motivo = SUSP_NENHUM;
            }
        }
    }

    /* 2. Escalonamento: decide quem executa neste tick (função acima). */
    escalonar(s);

    /* 3. Execução e gravação da coluna do Gantt. Para cada tarefa, o que
     * aconteceu neste tick vira UMA célula do gráfico (cel) + a CPU usada. */
    size_t base = (size_t)t * (size_t)s->largura;   /* início da coluna t */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        switch (k->estado) {
        case T_EXECUTANDO:
            s->cel[base + i] = G_EXEC;
            s->cpu[base + i] = (signed char)k->cpu;
            k->restante--;          /* consumiu 1 tick de CPU */
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
                 * que ela conte como "estava executando" (critério 1). */
                iniciar_ativacao(k);
                k->estado = T_PRONTA;
            } else {
                /* Nada pendente: espera o próximo período. */
                k->estado = T_SUSPENSA;
                k->motivo = SUSP_PERIODO;
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

    /* 5. O relógio avança e vemos se acabou tudo. */
    e->t++;
    e->fim = todas_terminadas(e);
}

/* ------------------------------------------------------------- navegação */

/* Avança 1 tick. Se esse tick JÁ foi simulado antes (o usuário voltou com
 * `p`), não recalcula: só recarrega o snapshot guardado. Isso garante que
 * voltar e avançar dá exatamente o mesmo resultado, inclusive nos sorteios. */
bool sim_avancar(Simulador *s) {
    if (s->atual.fim) return false;
    int t = s->atual.t;
    if (s->historico && t + 1 < s->nsnaps) {
        hist_carregar(s, t + 1);   /* já simulado: só reaplica o snapshot */
        return true;
    }
    executar_tick(s);
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
        if (st == T_TERMINADA) continue;
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
        sim_avancar(s);
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

/* O usuário digita o ID da tarefa; internamente usamos o índice no vetor.
 * Esta função traduz um no outro (-1 se não existe). */
int sim_indice(const Estado *e, int id) {
    for (int i = 0; i < e->n; i++)
        if (e->tarefas[i].id == id) return i;
    return -1;
}

/* Utilização do processador: U = soma de C/P (duração / período). Se U > 1
 * numa CPU só, é impossível cumprir todos os prazos. */
double sim_utilizacao(const Estado *e) {
    double u = 0;
    for (int i = 0; i < e->n; i++) u += (double)e->tarefas[i].duracao / e->tarefas[i].periodo;
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
