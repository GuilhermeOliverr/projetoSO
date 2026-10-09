/*
 * ui.c - telas e comandos do simulador (ver ui.h).
 *
 * A interface é baseada em comandos de uma letra digitados no terminal,
 * como num debugger (gdb): é fácil de usar, não precisa de biblioteca de
 * interface e funciona igual em Linux, WSL e MSYS2. Toda entrada inválida é
 * recusada com o motivo, e Enter sem nada mantém o valor sugerido.
 */
#include "ui.h"
#include "gantt.h"
#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ mensagens */

static void ui_erro(const char *fmt, ...) {
    va_list ap;
    term_fg(0xE74C3C);
    term_negrito();
    printf("erro: ");
    term_reset();
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void pausar(void) {
    char buf[8];
    ler_linha("\n[Enter] para voltar ", buf, sizeof buf);
}

/* Formata uma mensagem num buffer (usado para a linha de status). */
static void fmsg(char *dst, size_t tam, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, tam, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------- leitura de dados */

typedef const char *(*Validador)(int v, const void *ctx);

/* Pede um inteiro mostrando o valor sugerido entre colchetes. Enter mantém
 * o sugerido. Repete até receber um valor válido. false = EOF. */
static bool pedir_int(const char *rotulo, int sugerido, int *out, Validador val, const void *ctx) {
    char buf[128], prompt[256];
    snprintf(prompt, sizeof prompt, "  %s [%d]: ", rotulo, sugerido);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        int v = sugerido;
        if (*buf) {
            int r = parse_int(buf, &v);
            if (r == -1) { ui_erro("'%s' não é um número inteiro", buf); continue; }
            if (r == -2) { ui_erro("'%s' é grande demais", buf); continue; }
        }
        const char *m = val ? val(v, ctx) : NULL;
        if (m) { ui_erro("%s", m); continue; }
        *out = v;
        return true;
    }
}

static bool pedir_cor(unsigned sugerida, unsigned *out) {
    char buf[64], prompt[96];
    snprintf(prompt, sizeof prompt, "  cor RGB hex [%06X]: ", sugerida);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        if (!*buf) { *out = sugerida; return true; }
        if (parse_cor(buf, out)) return true;
        ui_erro("'%s' não é uma cor válida: use 6 dígitos hexadecimais, ex.: F0E0D0", buf);
    }
}

static const char *val_positivo(int v, const void *ctx) {
    (void)ctx;
    return v > 0 ? NULL : "o valor deve ser maior que zero";
}

static const char *val_periodo(int v, const void *ctx) {
    (void)ctx;
    if (v == 0) return "período 0 significa tarefa aperiódica, que o Projeto A não simula";
    return v > 0 ? NULL : "o período deve ser maior que zero";
}

static const char *val_min(int v, const void *ctx) {
    static char buf[128];
    int min = *(const int *)ctx;
    if (v >= min) return NULL;
    snprintf(buf, sizeof buf, "o valor deve ser >= %d (não dá para chegar no passado)", min);
    return buf;
}

static const char *val_quantum(int v, const void *ctx) { (void)ctx; return config_validar_quantum(v); }
/* Rótulo do prompt de CPUs com o limite N à vista (steering, ambig. 6). */
#define STR_(x) #x
#define STR(x) STR_(x)
#define ROTULO_CPUS "quantidade de CPUs (1 a " STR(MAX_CPUS) ")"

static const char *val_cpus(int v, const void *ctx) { (void)ctx; return config_validar_cpus(v); }

/* Contexto para validar id único. */
typedef struct { const int *ids; int n; int proprio; } CtxIds;

static const char *val_id(int v, const void *ctx) {
    static char buf[96];
    const CtxIds *c = ctx;
    for (int i = 0; i < c->n; i++)
        if (i != c->proprio && c->ids[i] == v) {
            snprintf(buf, sizeof buf, "já existe uma tarefa com id %d", v);
            return buf;
        }
    return NULL;
}

/* Escolhe um algoritmo pelo nome ou número. -1 = cancelado. */
static int pedir_algoritmo(int atual) {
    printf("Algoritmos disponíveis:\n");
    for (int i = 0; i < sched_quantidade(); i++)
        printf("  %d) %-4s %s%s\n", i + 1, sched_obter(i)->nome, sched_obter(i)->descricao,
               i == atual ? "  (atual)" : "");
    char buf[64];
    for (;;) {
        if (!ler_linha("  algoritmo (nome ou número, Enter mantém): ", buf, sizeof buf)) return -1;
        if (!*buf) return atual;
        int v;
        if (parse_int(buf, &v) == 0 && v >= 1 && v <= sched_quantidade()) return v - 1;
        int idx = sched_buscar(buf);
        if (idx >= 0) return idx;
        char nomes[256];
        sched_listar_nomes(nomes, sizeof nomes);
        ui_erro("algoritmo '%s' desconhecido (disponíveis: %s)", buf, nomes);
    }
}

/* Lê os dados de uma tarefa nova (ou editada, se `t` já vier preenchida).
 * `min_ingresso` impede chegar no passado durante a simulação. */
static bool pedir_tarefa(TarefaCfg *t, const CtxIds *ids, int min_ingresso, char *eventos, size_t tam_ev) {
    if (!pedir_int("id", t->id, &t->id, val_id, ids)) return false;
    if (!pedir_cor(t->cor, &t->cor)) return false;
    if (!pedir_int("ingresso", t->ingresso, &t->ingresso, val_min, &min_ingresso)) return false;
    if (!pedir_int("duração", t->duracao, &t->duracao, val_positivo, NULL)) return false;
    int periodo_antigo = t->periodo;
    if (!pedir_int("período", t->periodo, &t->periodo, val_periodo, NULL)) return false;
    /* Se o prazo acompanhava o período (prazo implícito), continua acompanhando. */
    int sug = t->prazo == periodo_antigo ? t->periodo : t->prazo;
    if (!pedir_int("prazo", sug, &t->prazo, val_positivo, NULL)) return false;
    char prompt[600];
    snprintf(prompt, sizeof prompt, "  lista de eventos (Projeto B) [%s]: ", t->eventos ? t->eventos : "");
    char buf[512];
    if (!ler_linha(prompt, buf, sizeof buf)) return false;
    snprintf(eventos, tam_ev, "%s", *buf ? buf : (t->eventos ? t->eventos : ""));
    t->eventos = eventos;
    return true;
}

/* ------------------------------------------------------- configuração */

static void imprimir_utilizacao(int n, double u, int alg) {
    printf("Utilização U = Σ C/P = %.3f", u);
    const Escalonador *e = sched_obter(alg);
    if (e && str_ieq(e->nome, "RM")) {
        double lim = sim_limite_rm(n);
        printf("  |  teste suficiente do RM: U ≤ n(2^(1/n)−1) = %.3f → %s", lim,
               u <= lim ? "escalonável (1 CPU)" : "inconclusivo (pode perder prazos)");
    } else if (e && str_ieq(e->nome, "EDF")) {
        printf("  |  EDF (prazo = período): escalonável em 1 CPU sse U ≤ 1 → %s",
               u <= 1.0 ? "sim" : "não");
    }
    printf("\n(os testes valem para 1 CPU; com mais CPUs são só uma referência)\n");
}

void ui_imprimir_config(const Config *cfg) {
    const Escalonador *e = sched_obter(cfg->algoritmo);
    term_negrito();
    printf("Arquivo: ");
    term_reset();
    printf("%s\n", cfg->caminho ? cfg->caminho : "(nenhum)");
    term_negrito();
    printf("Sistema: ");
    term_reset();
    printf("algoritmo %s (%s) | quantum %d | %d CPU(s) (máx. %d)\n", e ? e->nome : "?",
           e ? e->descricao : "", cfg->quantum, cfg->ncpus, MAX_CPUS);
    term_negrito();
    printf("Tarefas (%d periódicas):\n", cfg->n);
    term_reset();
    printf("  %6s  %-7s %8s %8s %8s %8s  %s\n", "id", "cor", "ingresso", "duração", "período", "prazo", "eventos");
    double u = 0;
    for (int i = 0; i < cfg->n; i++) {
        const TarefaCfg *t = &cfg->tarefas[i];
        printf("  %6d  ", t->id);
        term_bg(t->cor);
        printf("  ");
        term_reset();
        printf("%06X %7d %8d %8d %8d  %s\n", t->cor, t->ingresso, t->duracao, t->periodo, t->prazo,
               t->eventos && *t->eventos ? t->eventos : "-");
        u += (double)t->duracao / t->periodo;
    }
    imprimir_utilizacao(cfg->n, u, cfg->algoritmo);
}

static int indice_cfg(const Config *cfg, int id) {
    for (int i = 0; i < cfg->n; i++)
        if (cfg->tarefas[i].id == id) return i;
    return -1;
}

static int *ids_cfg(const Config *cfg) {
    int *ids = xmalloc((size_t)(cfg->n + 1) * sizeof(int));
    for (int i = 0; i < cfg->n; i++) ids[i] = cfg->tarefas[i].id;
    return ids;
}

static int proximo_id(const int *ids, int n) {
    int m = 0;
    for (int i = 0; i < n; i++)
        if (ids[i] >= m) m = ids[i] + 1;
    return m;
}

bool ui_pedir_arquivo(Config *cfg) {
    char buf[1024];
    for (;;) {
        if (!ler_linha("Caminho do arquivo de configuração (q para sair): ", buf, sizeof buf)) return false;
        if (!*buf) { ui_erro("informe um caminho (absoluto ou relativo), ex.: exemplos/fig2_5_rm.txt"); continue; }
        if (str_ieq(buf, "q")) return false;
        /* Aspas aparecem quando o usuário arrasta o arquivo para o terminal. */
        size_t l = strlen(buf);
        if (l >= 2 && (buf[0] == '"' || buf[0] == '\'') && buf[l - 1] == buf[0]) {
            buf[l - 1] = '\0';
            memmove(buf, buf + 1, l - 1);
        }
        Config novo;
        bool ok = config_carregar(buf, &novo);
        config_imprimir_mensagens(&novo);
        if (ok) {
            config_liberar(cfg);
            *cfg = novo;
            return true;
        }
        config_liberar(&novo);
        printf("Corrija o arquivo e tente de novo, ou informe outro.\n");
    }
}

bool ui_menu_config(Config *cfg) {
    char buf[256];
    for (;;) {
        printf("\n");
        term_negrito();
        printf("=== Configuração da simulação ===\n");
        term_reset();
        ui_imprimir_config(cfg);
        printf("\n[Enter] iniciar  [a] algoritmo  [k] quantum  [c] CPUs  [e id] editar tarefa\n"
               "[n] nova tarefa  [r id] remover tarefa  [l] carregar outro arquivo  [q] sair\n");
        if (!ler_linha("> ", buf, sizeof buf)) return false;
        char cmd = buf[0];
        int arg = 0;
        bool tem_arg = parse_int(str_trim(buf + (cmd ? 1 : 0)), &arg) == 0;
        switch (cmd) {
        case '\0':
            if (cfg->n == 0) { ui_erro("não há tarefas periódicas para simular; use [n] ou [l]"); break; }
            return true;
        case 'a': {
            int a = pedir_algoritmo(cfg->algoritmo);
            if (a >= 0) cfg->algoritmo = a;
            break;
        }
        case 'k': pedir_int("quantum", cfg->quantum, &cfg->quantum, val_quantum, NULL); break;
        case 'c': pedir_int(ROTULO_CPUS, cfg->ncpus, &cfg->ncpus, val_cpus, NULL); break;
        case 'e': case 'r': {
            if (!tem_arg) { ui_erro("informe o id da tarefa, ex.: %c 2", cmd); break; }
            int i = indice_cfg(cfg, arg);
            if (i < 0) { ui_erro("não existe tarefa com id %d", arg); break; }
            if (cmd == 'r') {
                config_liberar_tarefa(&cfg->tarefas[i]);
                memmove(&cfg->tarefas[i], &cfg->tarefas[i + 1], (size_t)(cfg->n - i - 1) * sizeof(TarefaCfg));
                cfg->n--;
                printf("Tarefa %d removida.\n", arg);
                break;
            }
            int *ids = ids_cfg(cfg);
            CtxIds c = {ids, cfg->n, i};
            TarefaCfg t = cfg->tarefas[i];
            char ev[512];
            printf("Editando a tarefa %d (Enter mantém o valor):\n", arg);
            if (pedir_tarefa(&t, &c, 0, ev, sizeof ev)) {
                /* `t` divide os ponteiros de eventos com a original: copia os
                 * campos numéricos e troca a lista pelo texto novo. */
                TarefaCfg *dst = &cfg->tarefas[i];
                t.eventos = dst->eventos;
                t.lista_ev = dst->lista_ev;
                t.nev = dst->nev;
                *dst = t;
                config_definir_eventos(dst, ev);
            }
            free(ids);
            break;
        }
        case 'n': {
            int *ids = ids_cfg(cfg);
            CtxIds c = {ids, cfg->n, -1};
            TarefaCfg t = {proximo_id(ids, cfg->n), config_cor_padrao(cfg->n), PADRAO_INGRESSO,
                           PADRAO_DURACAO, PADRAO_PERIODO, PADRAO_PERIODO, NULL, 0, NULL, 0};
            char ev[512];
            printf("Nova tarefa (Enter aceita o valor sugerido):\n");
            if (pedir_tarefa(&t, &c, 0, ev, sizeof ev)) config_adicionar(cfg, &t);
            free(ids);
            break;
        }
        case 'l': ui_pedir_arquivo(cfg); break;
        case 'q': return false;
        default: ui_erro("comando '%s' desconhecido", buf);
        }
    }
}

char ui_pedir_modo(void) {
    char buf[32];
    for (;;) {
        if (!ler_linha("\nModo de execução: [a] passo a passo (padrão)  [b] completa  [q] sair: ", buf, sizeof buf))
            return 0;
        if (!*buf || str_ieq(buf, "a")) return 'a';
        if (str_ieq(buf, "b")) return 'b';
        if (str_ieq(buf, "q")) return 0;
        ui_erro("opção '%s' inválida: digite a, b ou q", buf);
    }
}

/* -------------------------------------------------------- utilidades sim */

static void caminho_svg_padrao(const Config *cfg, const OpcoesUI *op, char *dst, size_t tam) {
    if (op->saida_svg) { snprintf(dst, tam, "%s", op->saida_svg); return; }
    /* nome do arquivo de configuração, sem diretório nem extensão */
    const char *c = cfg->caminho ? cfg->caminho : "simulacao";
    const char *b = c;
    for (const char *p = c; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    char base[256];
    snprintf(base, sizeof base, "%s", *b ? b : "simulacao");
    char *ponto = strrchr(base, '.');
    if (ponto && ponto != base) *ponto = '\0';
    snprintf(dst, tam, "%s_gantt.svg", base);
}

static void titulo_svg(const Config *cfg, char *dst, size_t tam) {
    snprintf(dst, tam, "%s", cfg->caminho ? cfg->caminho : "simulação");
}

static void estatisticas(const Simulador *s) {
    const Estado *e = &s->atual;
    term_negrito();
    printf("\nEstatísticas por tarefa:\n");
    term_reset();
    printf("  %6s %7s %7s %7s %7s %8s %8s %6s %7s %7s\n", "id", "ativ.", "prazos", "exec", "espera",
           "resp.méd", "resp.máx", "preemp", "sorteio", "término");
    int perdidos = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        perdidos += k->deadlines_perdidos;
        printf("  %6d %4d/%-2d %7d %7d %7d %8.1f %8d %6d %7d ", k->id, k->concluidas, MAX_ATIVACOES,
               k->deadlines_perdidos, k->ticks_exec, k->ticks_espera,
               k->concluidas ? (double)k->resposta_soma / k->concluidas : 0.0, k->resposta_max,
               k->preempcoes, k->sorteios);
        if (k->termino >= 0) printf("%7d\n", k->termino);
        else printf("%7s\n", "-");
    }
    term_negrito();
    printf("Uso das CPUs (%d ticks):\n", s->ncols);
    term_reset();
    int ncpu = e->ncpus;
    for (int t = 0; t < s->ncols; t++)
        if (s->ncpus_col[t] > ncpu) ncpu = s->ncpus_col[t];
    for (int c = 0; c < ncpu; c++) {
        int ocup = 0, existe = 0;
        for (int t = 0; t < s->ncols; t++) {
            if (c >= s->ncpus_col[t]) continue;
            existe++;
            for (int i = 0; i < e->n; i++)
                if (sim_cpu_celula(s, t, i) == c) { ocup++; break; }
        }
        printf("  CPU%d: ocupada %d tick(s), desligada %d tick(s) (%.1f%% de uso)\n", c, ocup,
               existe - ocup, existe ? 100.0 * ocup / existe : 0.0);
    }
    if (perdidos) {
        term_fg(0xE74C3C);
        printf("Total de prazos perdidos: %d\n", perdidos);
        term_reset();
    } else {
        printf("Nenhum prazo perdido.\n");
    }
}

static bool exportar(const Simulador *s, const Config *cfg, const char *caminho, char *msg, size_t tam) {
    char titulo[512], erro[512];
    titulo_svg(cfg, titulo, sizeof titulo);
    if (gantt_svg(s, caminho, titulo, erro, sizeof erro)) {
        fmsg(msg, tam, "Gantt da simulação exportado para '%s' (abra no navegador).", caminho);
        return true;
    }
    fmsg(msg, tam, "erro ao exportar: %s", erro);
    return false;
}

/* ------------------------------------------------------- modo completo */

int ui_execucao_completa(const Config *cfg, const OpcoesUI *op) {
    if (cfg->n == 0) {
        ui_erro("não há tarefas periódicas para simular");
        return 1;
    }
    Simulador s;
    sim_iniciar(&s, cfg, false, op->semente);
    const char *motivo = sim_rodar_ate_fim(&s);
    term_negrito();
    printf("\n=== Resultado da execução completa ===\n");
    term_reset();
    const Escalonador *esc = sched_obter(s.atual.alg);
    printf("Algoritmo %s | %d CPU(s) | quantum %d | simulação terminou no instante %d\n",
           esc->nome, s.atual.ncpus, s.atual.quantum, s.atual.t);
    if (motivo) ui_erro("a simulação parou antes do fim: %s", motivo);
    gantt_tui_completo(&s);
    if (op->texto) gantt_texto(&s);
    estatisticas(&s);
    char caminho[1024], msg[1400];
    caminho_svg_padrao(cfg, op, caminho, sizeof caminho);
    bool ok = exportar(&s, cfg, caminho, msg, sizeof msg);
    if (ok) printf("%s\n", msg);
    else ui_erro("%s", msg + strlen("erro ao exportar: "));
    sim_liberar(&s);
    return ok && !motivo ? 0 : 1;
}

/* ---------------------------------------------------- modo passo a passo */

static void detalhes_tarefa(const Simulador *s, int i) {
    const TCB *k = &s->atual.tarefas[i];
    term_negrito();
    term_fg(k->cor);
    printf("Tarefa %d (TCB) no instante %d\n", k->id, s->atual.t);
    term_reset();
    printf("  parâmetros: cor %06X | ingresso %d | duração %d | período %d | prazo %d\n",
           k->cor, k->ingresso, k->duracao, k->periodo, k->prazo);
    printf("  eventos (Projeto B): %d", k->neventos);
    for (int j = 0; j < k->neventos; j++) printf("%s%s", j ? " | " : ": ", k->eventos[j]);
    putchar('\n');
    printf("  estado: %s\n", estado_nome(k->estado));
    if (k->restante > 0)
        printf("  prioridade (menor = mais prioritária): nominal %d, ativa %d\n", k->prio_nominal, k->prio_ativa);
    printf("  ativações: %d chegaram, %d concluídas (de %d)\n", k->liberadas, k->concluidas, MAX_ATIVACOES);
    if (k->restante > 0) {
        printf("  ativação atual: nº %d, chegou em %d, deadline em %d, faltam %d tick(s)%s\n",
               k->concluidas + 1, k->chegada_atual, k->deadline_abs, k->restante,
               (k->perdas & (1u << k->concluidas)) ? " — PRAZO PERDIDO" : "");
        if (k->liberadas - k->concluidas > 1)
            printf("  ativações acumuladas esperando: %d\n", k->liberadas - k->concluidas - 1);
    }
    if (k->liberadas < MAX_ATIVACOES && k->estado != T_TERMINADA)
        printf("  próxima chegada: instante %d\n", k->proxima_chegada);
    printf("  CPU no último tick: %s", k->cpu >= 0 ? "" : "nenhuma\n");
    if (k->cpu >= 0) printf("CPU%d (quantum usado %d/%d)\n", k->cpu, k->quantum_usado, s->atual.quantum);
    printf("  estatísticas: executou %d, esperou %d, suspensa %d tick(s); %d preempção(ões), "
           "%d sorteio(s), %d prazo(s) perdido(s)\n", k->ticks_exec, k->ticks_espera, k->ticks_suspensa,
           k->preempcoes, k->sorteios, k->deadlines_perdidos);
    if (k->termino >= 0) printf("  terminou no instante %d\n", k->termino);
}

static void estado_sistema(const Simulador *s) {
    const Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    term_negrito();
    printf("Sistema no instante %d\n", e->t);
    term_reset();
    printf("  algoritmo %s | quantum %d | %d CPU(s) | %s\n", esc->nome, e->quantum, e->ncpus,
           e->fim ? "simulação concluída" : sim_bloqueado(e) ? "bloqueada (todas suspensas)" : "em andamento");
    imprimir_utilizacao(e->n, sim_utilizacao(e), e->alg);
    printf("\n  %6s %-27s %7s %8s %8s %5s %7s\n", "id", "estado", "ativ.", "restante", "deadline", "CPU", "prazos");
    int *ordem = gantt_ordem(e);
    for (int r = e->n - 1; r >= 0; r--) {
        const TCB *k = &e->tarefas[ordem[r]];
        printf("  %6d %-27s %4d/%-2d ", k->id, estado_nome(k->estado), k->concluidas, MAX_ATIVACOES);
        if (k->restante > 0) printf("%8d %8d ", k->restante, k->deadline_abs);
        else printf("%8s %8s ", "-", "-");
        if (k->cpu >= 0) printf("%5d ", k->cpu);
        else printf("%5s ", "-");
        printf("%7d\n", k->deadlines_perdidos);
    }
    free(ordem);
}

/* Linha com a situação das CPUs e da fila no último tick executado. */
static void resumo_tick(const Simulador *s) {
    const Estado *e = &s->atual;
    if (e->t == 0) {
        printf("Instante 0: nada executado ainda. [n] avança um tick.\n");
        return;
    }
    int t = e->t - 1;
    printf("Tick %d:", t);
    for (int c = 0; c < s->ncpus_col[t]; c++) {
        int quem = -1;
        for (int i = 0; i < e->n; i++)
            if (sim_cpu_celula(s, t, i) == c) quem = i;
        if (quem >= 0) printf("  CPU%d=T%d", c, e->tarefas[quem].id);
        else printf("  CPU%d=desligada", c);
    }
    printf("  | prontas:");
    int np = 0;
    for (int i = 0; i < e->n; i++)
        if (sim_celula(s, t, i) == G_PRONTA) { printf(" T%d", e->tarefas[i].id); np++; }
    if (!np) printf(" (nenhuma)");
    putchar('\n');
    bool algum = false;
    for (int v = 0; v < s->nev; v++) {
        const Evento *ev = &s->ev[v];
        if (ev->tick != t) continue;
        if (!algum) printf("Eventos:");
        algum = true;
        printf("  [t=%d] T%d %s", ev->instante, e->tarefas[ev->tarefa].id, evento_nome(ev->tipo));
        if (ev->tipo == EV_PRAZO_PERDIDO) printf("!");
    }
    if (algum) putchar('\n');
}

static void ajuda_passo(void) {
    printf("Comandos:\n"
           "  [Enter] ou n [k]  avança 1 (ou k) tick(s)       p [k]  volta 1 (ou k) tick(s)\n"
           "  g t   vai para o instante t                     r      roda até o fim\n"
           "  s     estado do sistema                         t id   detalhes (TCB) da tarefa\n"
           "  e id  edita estado/parâmetros da tarefa         +      adiciona tarefa\n"
           "  a     troca o algoritmo   k  quantum   c  quantidade de CPUs\n"
           "  v t   fixa a janela do Gantt a partir de t      v      volta a seguir o relógio\n"
           "  f     Gantt completo      x [arquivo]  exporta SVG      h  ajuda      q  sair\n"
           "Editar num instante passado descarta o que já tinha sido simulado depois dele.\n");
}

/* Edita uma tarefa durante a simulação. Toda mudança passa por
 * sim_preparar_edicao/sim_concluir_edicao, que descartam o futuro e gravam o
 * novo estado no histórico. */
static void editar_tarefa(Simulador *s, int i, char *msg, size_t tam) {
    Estado *e = &s->atual;
    TCB *k = &e->tarefas[i];
    int t = e->t;
    detalhes_tarefa(s, i);
    printf("\nO que alterar? (no instante %d)\n"
           "  1) cor   2) duração   3) período   4) prazo   5) ingresso\n"
           "  6) ticks restantes da ativação atual\n"
           "  7) %s   8) encerrar a tarefa agora   0) cancelar\n",
           t, k->estado == T_SUSPENSA ? "retomar (tirar da suspensão)" : "suspender");
    char buf[32];
    if (!ler_linha("> ", buf, sizeof buf) || !*buf || buf[0] == '0') {
        fmsg(msg, tam, "Edição cancelada.");
        return;
    }
    int op;
    if (parse_int(buf, &op) != 0 || op < 1 || op > 8) {
        fmsg(msg, tam, "erro: opção '%s' inválida (use 0 a 8)", buf);
        return;
    }
    if (k->estado == T_TERMINADA && op != 1) {
        fmsg(msg, tam, "erro: a tarefa %d já terminou as %d ativações; só a cor pode mudar",
             k->id, MAX_ATIVACOES);
        return;
    }
    int v = 0;
    unsigned cor;
    switch (op) {
    case 1:
        if (!pedir_cor(k->cor, &cor)) return;
        sim_preparar_edicao(s);
        k->cor = cor;
        fmsg(msg, tam, "Cor da tarefa %d alterada para %06X.", k->id, cor);
        break;
    case 2:
        if (!pedir_int("duração", k->duracao, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        k->duracao = v;
        fmsg(msg, tam, "Duração da tarefa %d = %d (vale a partir da próxima ativação; use a opção 6 "
             "para mudar a atual).", k->id, v);
        break;
    case 3:
        if (!pedir_int("período", k->periodo, &v, val_periodo, NULL)) return;
        sim_preparar_edicao(s);
        k->periodo = v;
        if (k->liberadas > 0 && k->liberadas < MAX_ATIVACOES)
            k->proxima_chegada = k->chegadas[k->liberadas - 1] + v;
        fmsg(msg, tam, "Período da tarefa %d = %d; próxima chegada no instante %d%s.", k->id, v,
             k->proxima_chegada, k->proxima_chegada < t ? " (já passou: chega no próximo tick)" : "");
        break;
    case 4:
        if (!pedir_int("prazo", k->prazo, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        k->prazo = v;
        if (k->restante > 0) k->deadline_abs = k->chegada_atual + v;
        fmsg(msg, tam, "Prazo da tarefa %d = %d%s.", k->id, v,
             k->restante > 0 ? "; deadline da ativação atual recalculado" : "");
        break;
    case 5:
        if (k->liberadas > 0) {
            fmsg(msg, tam, "erro: a tarefa %d já ingressou no instante %d; o ingresso só pode mudar "
                 "antes da primeira chegada", k->id, k->chegadas[0]);
            return;
        }
        if (!pedir_int("ingresso", k->ingresso < t ? t : k->ingresso, &v, val_min, &t)) return;
        sim_preparar_edicao(s);
        k->ingresso = v;
        k->proxima_chegada = v;
        fmsg(msg, tam, "Ingresso da tarefa %d = %d.", k->id, v);
        break;
    case 6:
        if (k->restante <= 0) {
            fmsg(msg, tam, "erro: a tarefa %d não tem ativação em andamento (estado: %s)", k->id,
                 estado_nome(k->estado));
            return;
        }
        if (!pedir_int("ticks restantes", k->restante, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        k->restante = v;
        fmsg(msg, tam, "Ativação atual da tarefa %d agora precisa de %d tick(s).", k->id, v);
        break;
    case 7:
        sim_preparar_edicao(s);
        if (k->estado == T_SUSPENSA) {
            k->estado = k->restante > 0 ? T_PRONTA : (k->liberadas == 0 ? T_NOVA : T_ESPERANDO);
            fmsg(msg, tam, "Tarefa %d retomada (%s).", k->id, estado_nome(k->estado));
        } else {
            k->estado = T_SUSPENSA;
            k->cpu = -1;
            fmsg(msg, tam, "Tarefa %d suspensa: não disputa CPU até ser retomada (ativações "
                 "continuam chegando e acumulando).", k->id);
        }
        break;
    case 8:
        sim_preparar_edicao(s);
        k->estado = T_TERMINADA;
        k->restante = 0;
        k->cpu = -1;
        k->termino = t;
        fmsg(msg, tam, "Tarefa %d encerrada no instante %d.", k->id, t);
        break;
    }
    sim_concluir_edicao(s);
}

static void adicionar_tarefa(Simulador *s, Config *extras, char *msg, size_t tam) {
    Estado *e = &s->atual;
    int *ids = xmalloc((size_t)(e->n + 1) * sizeof(int));
    for (int i = 0; i < e->n; i++) ids[i] = e->tarefas[i].id;
    CtxIds c = {ids, e->n, -1};
    TarefaCfg t = {proximo_id(ids, e->n), config_cor_padrao(e->n), e->t, PADRAO_DURACAO,
                   PADRAO_PERIODO, PADRAO_PERIODO, NULL, 0, NULL, 0};
    char ev[512];
    int min = e->t;
    printf("Nova tarefa (Enter aceita o valor sugerido; ingresso >= %d):\n", min);
    if (!pedir_tarefa(&t, &c, min, ev, sizeof ev)) {
        free(ids);
        return;
    }
    free(ids);
    /* O TCB aponta para os eventos da configuração; guardamos a tarefa em
     * `extras`, que vive até o fim da simulação. */
    config_adicionar(extras, &t);
    sim_preparar_edicao(s);
    sim_adicionar_tarefa(s, &extras->tarefas[extras->n - 1]);
    sim_concluir_edicao(s);
    fmsg(msg, tam, "Tarefa %d adicionada; chega no instante %d.", t.id, t.ingresso);
}

void ui_passo_a_passo(const Config *cfg, const OpcoesUI *op) {
    Simulador s;
    sim_iniciar(&s, cfg, true, op->semente);
    Config extras;              /* tarefas criadas durante a simulação */
    config_padrao(&extras);
    char msg[1600] = "";
    char caminho_svg[1024];
    caminho_svg_padrao(cfg, op, caminho_svg, sizeof caminho_svg);
    bool exportado = false;
    int janela_ini = -1;  /* -1 = a janela acompanha o relógio */
    char buf[256];

    for (;;) {
        /* Ao chegar no fim, gera a imagem automaticamente (req. 2.4). Depois
         * de uma edição o futuro muda, então exporta de novo no novo fim. */
        if (s.atual.fim && !exportado && s.atual.t == s.ncols) {
            char m2[1400];
            exportar(&s, cfg, caminho_svg, m2, sizeof m2);
            fmsg(msg, sizeof msg, "Simulação concluída no instante %d. %s", s.atual.t, m2);
            exportado = true;
        }

        term_limpar();
        const Escalonador *esc = sched_obter(s.atual.alg);
        term_negrito();
        printf("=== Passo a passo | %s | %s | %d CPU(s) | quantum %d | instante t = %d ===\n",
               cfg->caminho ? cfg->caminho : "", esc->nome, s.atual.ncpus, s.atual.quantum, s.atual.t);
        term_reset();

        int lbl = 8;
        int largura = term_largura() - lbl - 4;
        if (largura < 20) largura = 20;
        int fim = s.atual.t, ini;
        if (janela_ini >= 0) {
            ini = janela_ini;
            fim = ini + largura;
            if (fim > s.atual.t) fim = s.atual.t;
        } else {
            ini = fim - largura > 0 ? fim - largura : 0;
        }
        printf("Gantt: ticks %d a %d%s\n", ini, fim - 1, janela_ini >= 0 ? " (janela fixa: 'v' volta a seguir)" : "");
        /* gantt_tui só desenha colunas já simuladas e anteriores ao instante
         * atual: ao retroceder, o "futuro" gravado não aparece. */
        Simulador vis = s;
        vis.ncols = s.atual.t < s.ncols ? s.atual.t : s.ncols;
        int nev = 0;
        while (nev < s.nev && s.ev[nev].tick < s.atual.t) nev++;
        vis.nev = nev;
        gantt_tui(&vis, ini, fim);
        resumo_tick(&vis);
        if (s.atual.fim) printf("Simulação concluída. Use p/g para revisar ou e para editar e seguir de outro ponto.\n");
        else if (sim_bloqueado(&s.atual)) printf("Atenção: todas as tarefas restantes estão suspensas.\n");
        if (s.atual.t < s.ncols)
            printf("(há %d tick(s) já simulados à frente; n reaplica o histórico)\n", s.ncols - s.atual.t);
        if (*msg) {
            if (!strncmp(msg, "erro", 4)) term_fg(0xE74C3C);
            else term_fg(0x2ECC71);
            printf("%s\n", msg);
            term_reset();
            msg[0] = '\0';
        }
        if (!ler_linha("[Enter] avança, h ajuda > ", buf, sizeof buf)) break;

        char cmd = buf[0];
        char *resto = str_trim(buf + (cmd ? 1 : 0));
        int arg = 0;
        bool tem_arg = parse_int(resto, &arg) == 0;
        if (cmd && !strchr("npgrstea+kcvfxhq", cmd)) {
            fmsg(msg, sizeof msg, "erro: comando '%s' desconhecido (h mostra a ajuda)", buf);
            continue;
        }
        if (*resto && !tem_arg && cmd != 'x') {
            fmsg(msg, sizeof msg, "erro: argumento '%s' não é um número inteiro", resto);
            continue;
        }

        switch (cmd) {
        case '\0':
        case 'n': {
            int k = tem_arg ? arg : 1;
            if (k < 1) { fmsg(msg, sizeof msg, "erro: a quantidade de ticks deve ser >= 1"); break; }
            int feitos = 0;
            while (feitos < k && sim_avancar(&s)) feitos++;
            if (feitos < k) {
                if (s.atual.fim) fmsg(msg, sizeof msg, "erro: a simulação já terminou (instante %d)", s.atual.t);
            }
            break;
        }
        case 'p': {
            int k = tem_arg ? arg : 1;
            if (k < 1) { fmsg(msg, sizeof msg, "erro: a quantidade de ticks deve ser >= 1"); break; }
            if (s.atual.t == 0) { fmsg(msg, sizeof msg, "erro: já está no instante 0"); break; }
            for (int j = 0; j < k && sim_voltar(&s); j++) {}
            break;
        }
        case 'g':
            if (!tem_arg) { fmsg(msg, sizeof msg, "erro: informe o instante, ex.: g 50"); break; }
            if (arg < 0) { fmsg(msg, sizeof msg, "erro: o instante não pode ser negativo"); break; }
            if (sim_ir_para(&s, arg) != arg)
                fmsg(msg, sizeof msg, "A simulação termina no instante %d; parou lá.", s.atual.t);
            break;
        case 'r': {
            const char *motivo = sim_rodar_ate_fim(&s);
            if (motivo) fmsg(msg, sizeof msg, "erro: parou no instante %d: %s", s.atual.t, motivo);
            break;
        }
        case 's': term_limpar(); estado_sistema(&s); pausar(); break;
        case 't': case 'e': {
            if (!tem_arg) { fmsg(msg, sizeof msg, "erro: informe o id da tarefa, ex.: %c 1", cmd); break; }
            int i = sim_indice(&s.atual, arg);
            if (i < 0) { fmsg(msg, sizeof msg, "erro: não existe tarefa com id %d neste instante", arg); break; }
            term_limpar();
            if (cmd == 't') {
                detalhes_tarefa(&s, i);
                pausar();
            } else {
                editar_tarefa(&s, i, msg, sizeof msg);
                exportado = false;
            }
            break;
        }
        case '+': adicionar_tarefa(&s, &extras, msg, sizeof msg); exportado = false; break;
        case 'a': {
            int a = pedir_algoritmo(s.atual.alg);
            if (a >= 0 && a != s.atual.alg) {
                sim_preparar_edicao(&s);
                s.atual.alg = a;
                sim_concluir_edicao(&s);
                exportado = false;
                fmsg(msg, sizeof msg, "Algoritmo trocado para %s a partir do instante %d.",
                     sched_obter(a)->nome, s.atual.t);
            }
            break;
        }
        case 'k': case 'c': {
            int v;
            bool ok = cmd == 'k' ? pedir_int("quantum", s.atual.quantum, &v, val_quantum, NULL)
                                 : pedir_int(ROTULO_CPUS, s.atual.ncpus, &v, val_cpus, NULL);
            if (!ok) break;
            sim_preparar_edicao(&s);
            if (cmd == 'k') s.atual.quantum = v;
            else s.atual.ncpus = v;
            sim_concluir_edicao(&s);
            exportado = false;
            fmsg(msg, sizeof msg, "%s = %d a partir do instante %d.", cmd == 'k' ? "Quantum" : "CPUs", v, s.atual.t);
            break;
        }
        case 'v':
            if (tem_arg && arg < 0) { fmsg(msg, sizeof msg, "erro: o instante não pode ser negativo"); break; }
            janela_ini = tem_arg ? arg : -1;
            break;
        case 'f': {
            term_limpar();
            Simulador v2 = s;
            v2.ncols = vis.ncols;
            v2.nev = vis.nev;
            gantt_tui_completo(&v2);
            pausar();
            break;
        }
        case 'x': {
            const char *destino = *resto ? resto : caminho_svg;
            Simulador v2 = s;
            v2.ncols = vis.ncols;
            v2.nev = vis.nev;
            exportar(&v2, cfg, destino, msg, sizeof msg);
            break;
        }
        case 'h': term_limpar(); ajuda_passo(); pausar(); break;
        case 'q': goto sair;
        default: fmsg(msg, sizeof msg, "erro: comando '%c' desconhecido (h mostra a ajuda)", cmd);
        }
    }
sair:
    estatisticas(&s);
    sim_liberar(&s);
    config_liberar(&extras);
}
