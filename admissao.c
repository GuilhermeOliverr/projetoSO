/*
 * admissao.c - teste de escalonabilidade na admissão (ver admissao.h).
 *
 * Quem entra em cada teste:
 *   - CARGA PERIÓDICA: as periódicas e os servidores com capacidade (PS, DS
 *     e SS), cada um com C = duração (capacidade), T = período e D = prazo.
 *     O Background Server não entra: ele só usa CPU que ficaria desligada.
 *     O Deferrable Server pode executar duas capacidades seguidas (no fim
 *     de um período e no começo do outro), e por isso entra com um jitter
 *     J = T - C: é o efeito "back-to-back" do capítulo;
 *   - ESPORÁDICA: a banda que as esporádicas pedem, C/min(D, |P|), tem de
 *     caber na banda reservada pelos servidores, Cs/Ts; e o pior tempo de
 *     resposta na fila tem de caber no prazo de cada uma. Na fila elas
 *     passam na frente das aperiódicas e são atendidas por ordem de
 *     chegada (sim.c), então o pior caso é o de uma esporádica que chega
 *     junto com uma ocorrência de cada outra, no período mais ocupado;
 *   - APERIÓDICA: precisa existir servidor. Sem prazo, é aceita; com prazo,
 *     cada ativação é testada ao entrar na fila: o trabalho das aperiódicas
 *     na frente dela, mais o dela, mais o que as esporádicas podem pedir até
 *     o prazo (elas passam na frente) tem de caber no que os servidores
 *     garantem até lá.
 * "O que os servidores garantem" num intervalo é o modelo de recurso
 * periódico: um servidor de capacidade Cs a cada Ts entrega, no pior caso,
 * nada por 2(Ts - Cs) e depois Cs a cada Ts (função garantido). Com mais de
 * um servidor as garantias são somadas, o que é uma aproximação: uma
 * ativação só executa num servidor por vez.
 *
 * Testes da carga periódica, conforme o algoritmo e o número de CPUs:
 *   1 CPU, RM:  Liu & Layland (U <= n(2^(1/n)-1), só com prazo = período e
 *               sem DS); se não bastar, a análise do tempo de resposta
 *               (exata para prioridade fixa);
 *   1 CPU, EDF: U <= 1 (exato com prazo >= período e sem DS); senão a
 *               análise de demanda do processador, h(L) <= L;
 *   M CPUs:     U <= M é a condição necessária; os suficientes são o GFB
 *               (EDF global) e o de Bertogna, Cirinei e Lipari (RM global),
 *               com a densidade C/min(D, T) no lugar de C/T.
 * Quando a condição necessária passa e nenhum teste suficiente fecha (várias
 * CPUs, DS em várias CPUs, ou um algoritmo que não é RM nem EDF), a tarefa
 * é aceita "sem garantia", com aviso: o teste não prova que vai dar certo,
 * mas também não prova que vai dar errado.
 */
#include "admissao.h"
#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define EPS 1e-9   /* folga nas comparações de utilização (1/3 + 1/3 + 1/3 não dá 1 exato em double) */

/* Limite de pontos da análise de demanda do EDF; acima disso o teste não
 * roda (fica "sem garantia") para a admissão não travar a simulação. */
#define MAX_PONTOS_DEMANDA 200000

static Admissao resultado(ResultadoAdmissao r, const char *fmt, ...) {
    Admissao a;
    a.resultado = r;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a.motivo, sizeof a.motivo, fmt, ap);
    va_end(ap);
    return a;
}

/* A tarefa i conta no sistema: já foi aceita e não terminou (ou é a que
 * pede para entrar). */
static bool no_sistema(const Estado *e, int i, int cand) {
    const TCB *k = &e->tarefas[i];
    return i == cand || (k->admitida && k->estado != T_TERMINADA);
}

static bool servidor_com_capacidade(const TCB *k) {
    return tcb_eh_servidor(k) && k->servidor != SERV_BS;
}

/* Uma linha da carga periódica. */
typedef struct {
    int i;               // índice da tarefa
    long long C, T, D, J;
} Carga;

/* Junta a carga periódica do sistema com a candidata. Devolve quantas. */
static int juntar_carga(const Estado *e, int cand, Carga *v) {
    int n = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (!no_sistema(e, i, cand) || k->periodo <= 0) continue;
        if (k->tipo != TIPO_PERIODICA && !servidor_com_capacidade(k)) continue;
        Carga c = {i, k->duracao, k->periodo, k->prazo, 0};
        if (tcb_eh_servidor(k) && k->servidor == SERV_DS && c.T > c.C) c.J = c.T - c.C;
        v[n++] = c;
    }
    return n;
}

static long long min_ll(long long a, long long b) { return a < b ? a : b; }
static long long max_ll(long long a, long long b) { return a > b ? a : b; }

/* Teto de a/b para a >= 0 e b > 0. */
static long long teto(long long a, long long b) { return (a + b - 1) / b; }

/* ------------------------------------------------------------ 1 CPU: RM */

/* Análise do tempo de resposta (prioridade fixa): R = C_i + soma, sobre as
 * mais prioritárias, de teto((R + J_j) / T_j) * C_j, iterando até parar de
 * mudar. Período igual conta como mais prioritária (o desempate pode ir
 * para qualquer lado, então ficamos com o pior caso). O R precisa caber em
 * min(D, T): com R <= T cada ativação acaba antes da próxima chegar, e a
 * análise de uma ativação só vale. */
static Admissao teste_rm_um(const Estado *e, int cand, const Carga *v, int n, double U) {
    bool simples = true;   /* prazo >= período e sem DS: vale Liu & Layland */
    for (int a = 0; a < n; a++)
        if (v[a].D < v[a].T || v[a].J > 0) simples = false;
    double lim = sim_limite_rm(n);
    if (simples && U <= lim + EPS)
        return resultado(ADM_ACEITA, "RM, Liu & Layland: U = %.3f <= n(2^(1/n)-1) = %.3f (n = %d)", U, lim, n);
    int pior = -1;
    long long r_pior = 0, folga_pior = 0;
    for (int a = 0; a < n; a++) {
        long long limite = min_ll(v[a].D, v[a].T);
        long long R = v[a].C, R2 = 0;
        for (;;) {
            R2 = v[a].C;
            for (int b = 0; b < n && R2 <= limite; b++)
                if (b != a && v[b].T <= v[a].T) R2 += teto(R + v[b].J, v[b].T) * v[b].C;
            if (R2 == R || R2 > limite) break;
            R = R2;
        }
        const char *id = e->tarefas[v[a].i].id;
        if (R2 > limite)
            return resultado(ADM_RECUSADA, "RM, análise do tempo de resposta: %s%s teria tempo de resposta %lld > %s = %lld",
                             v[a].i == cand ? "ela" : "com ela, ", v[a].i == cand ? "" : id, R2,
                             v[a].D <= v[a].T ? "prazo" : "período", limite);
        if (pior < 0 || limite - R < folga_pior) {
            pior = a;
            r_pior = R;
            folga_pior = limite - R;
        }
    }
    return resultado(ADM_ACEITA, "RM, análise do tempo de resposta: todas cabem no prazo (a mais justa é %s, com R = %lld)",
                     e->tarefas[v[pior].i].id, r_pior);
}

/* ----------------------------------------------------------- 1 CPU: EDF */

/* Máximo divisor comum e mínimo múltiplo comum, com teto para não estourar. */
static long long mdc(long long a, long long b) {
    while (b) {
        long long r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static long long mmc_limitado(const Carga *v, int n, long long teto_valor) {
    long long m = 1;
    for (int a = 0; a < n; a++) {
        m = m / mdc(m, v[a].T) * v[a].T;
        if (m > teto_valor) return teto_valor + 1;
    }
    return m;
}

/* Demanda do processador no intervalo [0, L): o que precisa terminar até
 * L, somando as ativações de cada tarefa com chegada e prazo dentro dele.
 * O jitter adianta a chegada da tarefa (pior caso do DS). */
static long long demanda(const Carga *v, int n, long long L) {
    long long h = 0;
    for (int a = 0; a < n; a++) {
        long long x = L + v[a].J + v[a].T - v[a].D;
        if (x >= v[a].T) h += (x / v[a].T) * v[a].C;
    }
    return h;
}

static Admissao teste_edf_um(const Carga *v, int n, double U) {
    bool simples = true;   /* prazo >= período e sem DS: U <= 1 basta */
    long long dmax = 0;
    for (int a = 0; a < n; a++) {
        if (v[a].D < v[a].T || v[a].J > 0) simples = false;
        dmax = max_ll(dmax, v[a].D);
    }
    if (simples) return resultado(ADM_ACEITA, "EDF: U = %.3f <= 1", U);
    /* Até onde olhar: com U < 1, o limite de Baruah, max(Dmax, soma((T-D+J) U) / (1 - U));
     * com U = 1, o hiperperíodo mais Dmax. */
    long long L;
    if (U < 1 - EPS) {
        double soma = 0;
        for (int a = 0; a < n; a++) soma += (double)(v[a].T - v[a].D + v[a].J) * ((double)v[a].C / v[a].T);
        double b = soma / (1 - U);
        L = b > (double)dmax ? (long long)b + 1 : dmax;
    } else {
        L = mmc_limitado(v, n, (long long)LIMITE_TICKS) + dmax;
    }
    long long pontos = 0;
    for (int a = 0; a < n; a++) pontos += L / v[a].T + 1;
    if (pontos > MAX_PONTOS_DEMANDA)
        return resultado(ADM_SEM_GARANTIA, "EDF: U = %.3f <= 1, mas a análise de demanda precisaria ir até %lld ticks",
                         U, L);
    for (int a = 0; a < n; a++)
        for (long long d = v[a].D - v[a].J; d <= L; d += v[a].T) {
            if (d <= 0) continue;
            long long h = demanda(v, n, d);
            if (h > d)
                return resultado(ADM_RECUSADA, "EDF, análise de demanda: até o instante %lld as ativações pedem %lld "
                                 "tick(s) de CPU, mais do que cabe", d, h);
        }
    return resultado(ADM_ACEITA, "EDF, análise de demanda: h(L) <= L para todo L até %lld (U = %.3f)", L, U);
}

/* ------------------------------------------------------------ M CPUs */

static Admissao teste_multi(const Estado *e, const Carga *v, int n, double U, bool rm, bool edf) {
    int M = e->ncpus;
    double dens = 0, dmax = 0;
    bool tem_ds = false;
    for (int a = 0; a < n; a++) {
        double d = (double)v[a].C / min_ll(v[a].D, v[a].T);
        dens += d;
        if (d > dmax) dmax = d;
        if (v[a].J > 0) tem_ds = true;
    }
    if (!tem_ds && edf) {
        double lim = M - (M - 1) * dmax;
        if (dens <= lim + EPS)
            return resultado(ADM_ACEITA, "EDF global (GFB): soma de C/min(D,T) = %.3f <= M - (M-1)*max = %.3f", dens, lim);
    }
    if (!tem_ds && rm) {
        double lim = M / 2.0 * (1 - dmax) + dmax;
        if (dens <= lim + EPS)
            return resultado(ADM_ACEITA, "RM global (Bertogna, Cirinei e Lipari): soma de C/min(D,T) = %.3f <= "
                             "(M/2)(1-max)+max = %.3f", dens, lim);
    }
    return resultado(ADM_SEM_GARANTIA, "U = %.3f <= %d CPUs (condição necessária), mas nenhum teste suficiente %s",
                     U, M,
                     tem_ds ? "trata o Deferrable Server em várias CPUs" : (rm || edf) ? "fecha" : "existe para este algoritmo");
}

/* --------------------------------------------------------- carga periódica */

static Admissao teste_carga(const Estado *e, int cand) {
    Carga *v = xmalloc((size_t)(e->n ? e->n : 1) * sizeof *v);
    int n = juntar_carga(e, cand, v);
    double U = 0;
    Admissao r;
    for (int a = 0; a < n; a++) {
        U += (double)v[a].C / v[a].T;
        if (v[a].i == cand && v[a].C > v[a].D) {
            r = resultado(ADM_RECUSADA, "a duração (%lld) é maior que o prazo (%lld): perderia o prazo em toda ativação",
                          v[a].C, v[a].D);
            goto fim;
        }
    }
    const Escalonador *esc = sched_obter(e->alg);
    bool rm = esc && str_ieq(esc->nome, "RM"), edf = esc && str_ieq(esc->nome, "EDF");
    if (U > e->ncpus + EPS) {
        r = resultado(ADM_RECUSADA, "a utilização passaria a U = %.3f, mais do que %d CPU(s) comporta(m)", U, e->ncpus);
    } else if (e->ncpus > 1) {
        r = teste_multi(e, v, n, U, rm, edf);
    } else if (rm) {
        r = teste_rm_um(e, cand, v, n, U);
    } else if (edf) {
        r = teste_edf_um(v, n, U);
    } else {
        r = resultado(ADM_SEM_GARANTIA, "U = %.3f <= 1, mas o teste exato só existe para RM e EDF (algoritmo %s)",
                      U, esc ? esc->nome : "?");
    }
fim:
    free(v);
    return r;
}

/* ----------------------------------------------------- aperiódicas */

/* Banda reservada pelos servidores com capacidade (PS, DS e SS) e se há
 * algum Background Server. Servidor suspenso pelo usuário continua contando:
 * ele pode ser retomado. */
static double banda_servidores(const Estado *e, int cand, bool *tem_bs, bool *tem_algum) {
    double b = 0;
    *tem_bs = *tem_algum = false;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || i == cand || !no_sistema(e, i, cand)) continue;
        *tem_algum = true;
        if (k->servidor == SERV_BS) *tem_bs = true;
        else if (k->periodo > 0) b += (double)k->duracao / k->periodo;
    }
    return b;
}

/* Pior caso do que um servidor de capacidade Q a cada P garante em
 * qualquer intervalo de tamanho L (modelo de recurso periódico): no pior
 * caso ele fica 2(P - Q) sem executar e depois entrega Q a cada P. */
static long long garantido(long long P, long long Q, long long L) {
    if (L <= 0) return 0;
    if (Q >= P) return L;
    long long b = P - Q;
    if (L < b) return 0;
    long long k = (L - b) / P;
    long long extra = L - 2 * b - k * P;
    return k * Q + (extra > 0 ? extra : 0);
}

/* Soma do que os servidores com capacidade do sistema garantem em L. */
static long long oferta_servidores(const Estado *e, long long L) {
    long long o = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *x = &e->tarefas[i];
        if (servidor_com_capacidade(x) && no_sistema(e, i, -1) && x->periodo > 0) o += garantido(x->periodo, x->duracao, L);
    }
    return o;
}

/* Menor intervalo em que os servidores garantem W ticks (-1 se passa de
 * LIMITE_TICKS). A oferta só cresce com L: busca binária. */
static long long tempo_para(const Estado *e, long long W) {
    if (W <= 0) return 0;
    long long lo = W, hi = W;
    while (oferta_servidores(e, hi) < W) {
        if (hi > LIMITE_TICKS) return -1;
        hi *= 2;
    }
    while (lo < hi) {
        long long m = lo + (hi - lo) / 2;
        if (oferta_servidores(e, m) >= W) hi = m;
        else lo = m + 1;
    }
    return lo;
}

/* Trabalho das esporádicas do sistema (com a candidata) que chega num
 * intervalo de tamanho x contando a que chega no fim dele: floor(x/|P|)+1
 * ocorrências de cada (a primeira no início). */
static long long demanda_esporadicas(const Estado *e, int cand, long long x) {
    long long w = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (k->tipo == TIPO_ESPORADICA && no_sistema(e, i, cand)) w += (x / -k->periodo + 1) * k->duracao;
    }
    return w;
}

/* Pior tempo de resposta de uma esporádica na fila (FIFO entre elas). No
 * período ocupado que começa com uma ocorrência de cada esporádica, uma que
 * chega no instante x espera tudo o que chegou até x, inclusive ela: termina
 * quando os servidores garantiram esse trabalho. O pior caso está num x em
 * que alguma chegada acontece (múltiplo de algum |P|). -1 = não deu para
 * calcular (passa do limite). */
static long long resposta_esporadicas(const Estado *e, int cand) {
    long long B = tempo_para(e, demanda_esporadicas(e, cand, 0)), B2;
    while (B >= 0 && (B2 = tempo_para(e, demanda_esporadicas(e, cand, B - 1))) > B) B = B2;
    if (B < 0) return -1;
    long long pior = 0, pontos = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (k->tipo != TIPO_ESPORADICA || !no_sistema(e, i, cand)) continue;
        for (long long x = 0; x < B; x += -k->periodo) {
            if (++pontos > MAX_PONTOS_DEMANDA) return -1;
            long long f = tempo_para(e, demanda_esporadicas(e, cand, x));
            if (f < 0) return -1;
            if (f - x > pior) pior = f - x;
        }
    }
    return pior;
}

static Admissao teste_esporadica(const Estado *e, int cand) {
    const TCB *k = &e->tarefas[cand];
    bool tem_bs, tem_algum;
    double banda = banda_servidores(e, cand, &tem_bs, &tem_algum);
    if (!tem_algum) return resultado(ADM_RECUSADA, "não há servidor para executá-la (req. B 1.1)");
    if (k->duracao > k->prazo)
        return resultado(ADM_RECUSADA, "a duração (%d) é maior que o prazo (%d): perderia o prazo em toda ocorrência",
                         k->duracao, k->prazo);
    double pedida = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *x = &e->tarefas[i];
        if (x->tipo != TIPO_ESPORADICA || !no_sistema(e, i, cand)) continue;
        int janela = x->prazo < -x->periodo ? x->prazo : -x->periodo;
        if (janela > 0) pedida += (double)x->duracao / janela;
    }
    if (banda <= 0)
        return resultado(ADM_SEM_GARANTIA, "só há Background Server, que não reserva banda (executa quando sobra CPU)");
    if (pedida > banda + EPS)
        return resultado(ADM_RECUSADA, "banda das esporádicas, soma de C/min(D,|P|) = %.3f > banda dos servidores, "
                         "soma de Cs/Ts = %.3f", pedida, banda);
    long long R = resposta_esporadicas(e, cand);
    if (R < 0)
        return resultado(ADM_SEM_GARANTIA, "banda %.3f <= %.3f, mas o pior tempo de resposta na fila não pôde ser "
                         "calculado (período ocupado longo demais)", pedida, banda);
    for (int i = 0; i < e->n; i++) {
        const TCB *x = &e->tarefas[i];
        if (x->tipo == TIPO_ESPORADICA && no_sistema(e, i, cand) && R > x->prazo)
            return resultado(ADM_RECUSADA, "banda %.3f <= %.3f, mas na fila dos servidores %s%s poderia esperar até %lld "
                             "tick(s), mais que o prazo (%d)", pedida, banda, i == cand ? "ela" : "com ela, ",
                             i == cand ? "" : x->id, R, x->prazo);
    }
    return resultado(ADM_ACEITA, "banda das esporádicas %.3f <= banda dos servidores %.3f, e o pior tempo de resposta "
                     "na fila (%lld) cabe no prazo de todas", pedida, banda, R);
}

Admissao admissao_ativacao(const Estado *e, int cand, int agora) {
    const TCB *k = &e->tarefas[cand];
    long long L = (long long)k->deadline_abs - agora;
    bool tem_bs = false, tem_cap = false;
    for (int i = 0; i < e->n; i++) {
        const TCB *x = &e->tarefas[i];
        if (!tcb_eh_servidor(x) || !no_sistema(e, i, -1)) continue;
        if (x->servidor == SERV_BS) tem_bs = true;
        else if (x->periodo > 0) tem_cap = true;
    }
    long long oferta = oferta_servidores(e, L);
    if (!tem_cap && !tem_bs) return resultado(ADM_RECUSADA, "não há servidor para executá-la");
    if (!tem_cap)
        return resultado(ADM_SEM_GARANTIA, "só há Background Server, que não garante tempo até o prazo");
    /* Na frente dela: as aperiódicas que chegaram antes e ainda não
     * acabaram (estejam na fila ou com um servidor) e as esporádicas, que
     * passam na frente de qualquer aperiódica. Das esporádicas conta o que
     * pode estar pendente ou chegar até o prazo: as que chegaram nos
     * últimos D (garantidas em D) e as que chegam nos próximos L. */
    long long frente = 0, esporadicas = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *x = &e->tarefas[i];
        if (x->tipo == TIPO_ESPORADICA && no_sistema(e, i, -1)) {
            esporadicas += ((L + x->prazo) / -x->periodo + 1) * x->duracao;
            continue;
        }
        if (i == cand || x->tipo != TIPO_APERIODICA || x->restante <= 0) continue;
        if (x->estado != T_PRONTA && x->estado != T_EXECUTANDO) continue;
        if (x->chegada_atual < k->chegada_atual || (x->chegada_atual == k->chegada_atual && i < cand)) frente += x->restante;
    }
    long long total = frente + esporadicas + k->restante;
    if (total <= oferta)
        return resultado(ADM_ACEITA, "aperiódicas na frente (%lld) + esporádicas até o prazo (%lld) + a dela (%d) cabe "
                         "no que os servidores garantem até o prazo (%lld em %lld tick(s))", frente, esporadicas,
                         k->restante, oferta, L);
    return resultado(ADM_RECUSADA, "aperiódicas na frente (%lld) + esporádicas até o prazo (%lld) + a dela (%d) passa "
                     "do que os servidores garantem até o prazo (%lld em %lld tick(s))", frente, esporadicas,
                     k->restante, oferta, L);
}

/* ------------------------------------------------------------ entrada */

Admissao admissao_tarefa(const Estado *e, int cand) {
    const TCB *k = &e->tarefas[cand];
    if (tcb_eh_atendida(k)) {
        bool tem_bs, tem_algum;
        banda_servidores(e, cand, &tem_bs, &tem_algum);
        /* Sem servidor ela nunca executaria (req. B 1.1): isso vale mesmo
         * com o teste desligado. */
        if (!tem_algum) return resultado(ADM_RECUSADA, "não há servidor para executá-la (req. B 1.1)");
        if (!e->admissao) return resultado(ADM_ACEITA, "teste de admissão desligado (--sem-admissao)");
        if (k->tipo == TIPO_ESPORADICA) return teste_esporadica(e, cand);
        if (k->prazo > 0) return resultado(ADM_ACEITA, "há servidor; cada ativação passa pelo teste ao entrar na fila");
        return resultado(ADM_ACEITA, "há servidor; sem prazo, é executada quando um servidor tiver capacidade");
    }
    if (!e->admissao) return resultado(ADM_ACEITA, "teste de admissão desligado (--sem-admissao)");
    if (tcb_eh_servidor(k) && k->servidor == SERV_BS)
        return resultado(ADM_ACEITA, "Background Server: só usa CPU que ficaria desligada, não entra na carga");
    return teste_carga(e, cand);
}
