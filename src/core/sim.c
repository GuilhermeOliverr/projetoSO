/*
 * sim.c - ciclo de simulação (ver sim.h para o modelo de tempo).
 *
 * Ciclo de um tick t:
 *   1. chegadas: tarefas cuja próxima ativação vence em t entram na fila;
 *   2. escalonamento: escolhe as M tarefas de maior prioridade (M = CPUs)
 *      entre as prontas e as que estavam executando; as demais ficam (ou
 *      voltam) para a fila de prontos (preempção);
 *   3. execução: cada CPU ocupada consome 1 tick da sua tarefa; CPU sem
 *      tarefa fica desligada neste tick;
 *   4. verificações no instante t+1: fim de ativação, término (10ª
 *      ativação) e perda de prazo;
 *   5. t++ e grava o snapshot.
 */
#include "sim.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

const char *estado_nome(EstadoTarefa e) {
    switch (e) {
    case T_NOVA: return "nova (não chegou)";
    case T_PRONTA: return "pronta";
    case T_EXECUTANDO: return "executando";
    case T_SUSPENSA: return "suspensa";
    case T_ESPERANDO: return "aguardando próximo período";
    case T_TERMINADA: return "terminada";
    }
    return "?";
}

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

void tcb_iniciar(TCB *k, const TarefaCfg *t) {
    memset(k, 0, sizeof *k);
    k->id = t->id;
    k->cor = t->cor;
    k->ingresso = t->ingresso;
    k->duracao = t->duracao;
    k->periodo = t->periodo;
    k->prazo = t->prazo;
    k->eventos_raw = t->eventos;
    k->eventos = (const char *const *)t->lista_ev;
    k->neventos = t->nev;
    k->estado = T_NOVA;
    k->proxima_chegada = t->ingresso;
    k->cpu = -1;
    k->termino = -1;
}

static bool todas_terminadas(const Estado *e) {
    for (int i = 0; i < e->n; i++)
        if (e->tarefas[i].estado != T_TERMINADA) return false;
    return true;
}

void sim_iniciar(Simulador *s, const Config *cfg, bool historico, uint64_t semente) {
    memset(s, 0, sizeof *s);
    s->historico = historico;
    Estado *e = &s->atual;
    e->ncpus = cfg->ncpus;
    e->quantum = cfg->quantum;
    e->alg = cfg->algoritmo;
    e->rng = semente ? semente : 0x5EED5EEDull;
    e->n = cfg->n;
    e->tarefas = xcalloc((size_t)(cfg->n ? cfg->n : 1), sizeof(TCB));
    for (int i = 0; i < cfg->n; i++) tcb_iniciar(&e->tarefas[i], &cfg->tarefas[i]);
    e->fim = todas_terminadas(e);
    s->largura = cfg->n;
    hist_gravar(s);
}

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

/* Começa a próxima ativação pendente (a de número `concluidas`). */
static void iniciar_ativacao(TCB *k) {
    k->restante = k->duracao;
    k->chegada_atual = k->chegadas[k->concluidas];
    k->deadline_abs = k->chegada_atual + k->prazo;
    k->quantum_usado = 0;
}

/* ------------------------------------------------------------ escalonar */

/* Escolhe até ncpus tarefas entre os candidatos e distribui as CPUs.
 *
 * Em cada rodada pegamos o grupo das "melhores" tarefas restantes (as que
 * empatam em prioridade E nos critérios 1-4). Se o grupo cabe nas CPUs
 * livres, todas entram e a ordem entre elas não importa. Se não cabe,
 * sorteamos quais entram (critério 5) e marcamos o evento de sorteio. Assim
 * só há sorteio quando ele realmente decide quem executa. */
static void escalonar(Simulador *s) {
    Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    int n = e->n, t = e->t;

    int *cand = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    int *grupo = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    bool *escolhido = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    bool *sorteado = xcalloc((size_t)(n ? n : 1), sizeof(bool));
    int nc = 0;
    for (int i = 0; i < n; i++) {
        TCB *k = &e->tarefas[i];
        if ((k->estado == T_PRONTA || k->estado == T_EXECUTANDO) && k->restante > 0) {
            sched_atualizar(esc, k, t);
            cand[nc++] = i;
        }
    }

    int livres = e->ncpus;
    int restantes = nc;
    while (livres > 0 && restantes > 0) {
        /* melhor entre os não escolhidos */
        int melhor = -1;
        for (int j = 0; j < nc; j++) {
            if (escolhido[cand[j]]) continue;
            if (melhor < 0 || sched_comparar(&e->tarefas[cand[j]], &e->tarefas[melhor]) < 0)
                melhor = cand[j];
        }
        int ng = 0;
        for (int j = 0; j < nc; j++) {
            int i = cand[j];
            if (!escolhido[i] && sched_comparar(&e->tarefas[i], &e->tarefas[melhor]) == 0)
                grupo[ng++] = i;
        }
        if (ng <= livres) {
            for (int g = 0; g < ng; g++) escolhido[grupo[g]] = true;
            livres -= ng;
            restantes -= ng;
        } else {
            /* Fisher-Yates parcial: as `livres` primeiras posições viram
             * uma amostra uniforme do grupo empatado. */
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
     * número. */
    bool ocupada[MAX_CPUS] = {false};
    int *nova_cpu = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    for (int i = 0; i < n; i++) nova_cpu[i] = -1;
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        int c = e->tarefas[i].cpu;
        if (escolhido[i] && c >= 0 && c < e->ncpus && !ocupada[c]) {
            nova_cpu[i] = c;
            ocupada[c] = true;
        }
    }
    for (int j = 0; j < nc; j++) {
        int i = cand[j];
        if (!escolhido[i] || nova_cpu[i] >= 0) continue;
        int c = 0;
        while (ocupada[c]) c++;
        nova_cpu[i] = c;
        ocupada[c] = true;
    }

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

static void executar_tick(Simulador *s) {
    Estado *e = &s->atual;
    int t = e->t;
    rastro_ajustar_largura(s, e->n);
    rastro_garantir_coluna(s, t);

    /* 1. Chegadas. Usamos >= (e não ==) para que uma edição de ingresso ou
     * período que "pule" o instante exato não perca a ativação. */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (k->estado == T_TERMINADA || k->liberadas >= MAX_ATIVACOES) continue;
        if (t < k->proxima_chegada) continue;
        k->chegadas[k->liberadas] = t;
        evento_add(s, t, t, EV_CHEGADA, i, k->liberadas, -1);
        k->liberadas++;
        k->proxima_chegada += k->periodo;
        if (k->proxima_chegada <= t) k->proxima_chegada = t + k->periodo;
        /* Se a ativação anterior ainda não terminou, esta fica acumulada e
         * começa assim que a anterior acabar (ver README, decisões). */
        if (k->restante == 0) {
            iniciar_ativacao(k);
            if (k->estado != T_SUSPENSA) k->estado = T_PRONTA;
        }
    }

    /* 2. Escalonamento. */
    escalonar(s);

    /* 3. Execução e gravação da coluna do Gantt. */
    size_t base = (size_t)t * (size_t)s->largura;
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        switch (k->estado) {
        case T_EXECUTANDO:
            s->cel[base + i] = G_EXEC;
            s->cpu[base + i] = (signed char)k->cpu;
            k->restante--;
            k->ticks_exec++;
            k->quantum_usado++;
            break;
        case T_PRONTA:
            s->cel[base + i] = G_PRONTA;
            k->ticks_espera++;
            break;
        case T_SUSPENSA:
            s->cel[base + i] = G_SUSPENSA;
            k->ticks_suspensa++;
            break;
        default:
            break;
        }
    }

    /* 4. Verificações no instante t+1. */
    for (int i = 0; i < e->n; i++) {
        TCB *k = &e->tarefas[i];
        if (k->estado == T_EXECUTANDO && k->restante == 0) {
            int resp = t + 1 - k->chegada_atual;
            if (resp > k->resposta_max) k->resposta_max = resp;
            k->resposta_soma += resp;
            k->concluidas++;
            evento_add(s, t, t + 1, EV_FIM_ATIVACAO, i, k->concluidas - 1, k->cpu);
            if (k->concluidas >= MAX_ATIVACOES) {
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
                k->estado = T_ESPERANDO;
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

    e->t++;
    e->fim = todas_terminadas(e);
}

/* ------------------------------------------------------------- navegação */

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

bool sim_voltar(Simulador *s) {
    if (!s->historico || s->atual.t == 0) return false;
    hist_carregar(s, s->atual.t - 1);
    return true;
}

int sim_ir_para(Simulador *s, int t) {
    if (t < 0) t = 0;
    if (s->historico && t < s->nsnaps) {
        hist_carregar(s, t);
        return t;
    }
    while (s->atual.t < t && sim_avancar(s)) {}
    return s->atual.t;
}

bool sim_bloqueado(const Estado *e) {
    bool alguma = false;
    for (int i = 0; i < e->n; i++) {
        EstadoTarefa st = e->tarefas[i].estado;
        if (st == T_TERMINADA) continue;
        if (st != T_SUSPENSA) return false;
        alguma = true;
    }
    return alguma;
}

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

void sim_preparar_edicao(Simulador *s) {
    hist_truncar(s, s->atual.t);
}

void sim_concluir_edicao(Simulador *s) {
    s->atual.fim = todas_terminadas(&s->atual);
    hist_gravar(s);
}

void sim_adicionar_tarefa(Simulador *s, const TarefaCfg *t) {
    Estado *e = &s->atual;
    e->tarefas = xrealloc(e->tarefas, (size_t)(e->n + 1) * sizeof(TCB));
    tcb_iniciar(&e->tarefas[e->n], t);
    e->n++;
    rastro_ajustar_largura(s, e->n);
}

/* --------------------------------------------------------------- consultas */

CelulaGantt sim_celula(const Simulador *s, int tick, int tarefa) {
    if (tick < 0 || tick >= s->ncols || tarefa >= s->largura) return G_VAZIO;
    return (CelulaGantt)s->cel[(size_t)tick * s->largura + tarefa];
}

int sim_cpu_celula(const Simulador *s, int tick, int tarefa) {
    if (tick < 0 || tick >= s->ncols || tarefa >= s->largura) return -1;
    return s->cpu[(size_t)tick * s->largura + tarefa];
}

int sim_indice(const Estado *e, int id) {
    for (int i = 0; i < e->n; i++)
        if (e->tarefas[i].id == id) return i;
    return -1;
}

double sim_utilizacao(const Estado *e) {
    double u = 0;
    for (int i = 0; i < e->n; i++) u += (double)e->tarefas[i].duracao / e->tarefas[i].periodo;
    return u;
}

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
