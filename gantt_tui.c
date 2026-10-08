/*
 * gantt_tui.c - Gantt no terminal, com caracteres Unicode e cores ANSI.
 *
 * Cada tick ocupa 1 coluna de texto. Cada tarefa ocupa 2 linhas: a de cima
 * tem os marcadores de evento e a de baixo a barra de execução. Isso evita
 * que um marcador esconda o número da CPU.
 *
 * Símbolos:
 *   barra:     0-9/A-Z  executando na CPU indicada (fundo = cor da tarefa)
 *              ?        (re)começou a executar por sorteio (critério 5)
 *              ─        pronta, esperando CPU (sem cor)
 *              ░        suspensa (preto pontilhado)
 *   marcador:  ↓ chegada  ↑ fim de ativação  ↕ os dois no mesmo tick
 *              ■ término (10ª ativação)  ✗ perda de prazo
 *   CPUs:      cor e id da tarefa que ocupou a CPU; · CPU desligada
 */
#include "gantt.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COR_PRONTA     0x808080
#define COR_SUSP_FUNDO 0xC8C8C8
#define COR_SUSP_TEXTO 0x000000
#define COR_DESLIGADA  0x606060
#define COR_PRAZO      0xFF4040
#define COR_EIXO       0x909090

void gantt_mascaras(const Simulador *s, int ini, int fim, unsigned char *mask) {
    int n = s->atual.n;
    memset(mask, 0, (size_t)(fim - ini) * (size_t)(n ? n : 1));
    for (int i = 0; i < s->nev; i++) {
        const Evento *v = &s->ev[i];
        /* Eventos de fim de tick (instante t+1) ficam na coluna do tick em
         * que a tarefa executou pela última vez: é ali que o olho procura. */
        int col = v->tick;
        if (col < ini || col >= fim || v->tarefa >= n) continue;
        unsigned char *m = &mask[(size_t)(col - ini) * n + v->tarefa];
        switch (v->tipo) {
        case EV_CHEGADA: *m |= M_CHEGADA; break;
        case EV_FIM_ATIVACAO: *m |= M_FIM; break;
        case EV_TERMINO: *m |= M_TERMINO; break;
        case EV_PRAZO_PERDIDO: *m |= M_PRAZO; break;
        case EV_SORTEIO: *m |= M_SORTEIO; break;
        default: break;
        }
    }
}

static const Estado *g_ordem_estado;

static int cmp_id_desc(const void *a, const void *b) {
    int ia = g_ordem_estado->tarefas[*(const int *)a].id;
    int ib = g_ordem_estado->tarefas[*(const int *)b].id;
    return (ia < ib) - (ia > ib);
}

int *gantt_ordem(const Estado *e) {
    int *o = xmalloc((size_t)(e->n ? e->n : 1) * sizeof(int));
    for (int i = 0; i < e->n; i++) o[i] = i;
    g_ordem_estado = e;
    qsort(o, (size_t)e->n, sizeof(int), cmp_id_desc);
    return o;
}

static char digito_cpu(int c) {
    if (c < 0) return ' ';
    if (c < 10) return (char)('0' + c);
    if (c < 36) return (char)('A' + c - 10);
    return '#';
}

/* Largura do rótulo à esquerda ("T12", "CPU3"), igual para todas as linhas. */
static int largura_rotulo(const Estado *e) {
    int w = 5;
    char buf[32];
    for (int i = 0; i < e->n; i++) {
        int l = snprintf(buf, sizeof buf, "T%d", e->tarefas[i].id);
        if (l + 1 > w) w = l + 1;
    }
    int l = snprintf(buf, sizeof buf, "CPU%d", e->ncpus - 1);
    if (l + 1 > w) w = l + 1;
    return w;
}

/* Quem estava em cada CPU em cada tick: ocup[(t-ini)*MAX_CPUS + c]. */
static int *ocupacao(const Simulador *s, int ini, int fim) {
    int *oc = xmalloc((size_t)(fim - ini) * MAX_CPUS * sizeof(int));
    for (int t = ini; t < fim; t++) {
        for (int c = 0; c < MAX_CPUS; c++) oc[(t - ini) * MAX_CPUS + c] = -1;
        for (int i = 0; i < s->atual.n; i++) {
            int c = sim_cpu_celula(s, t, i);
            if (c >= 0 && c < MAX_CPUS) oc[(t - ini) * MAX_CPUS + c] = i;
        }
    }
    return oc;
}

static void eixo(int lbl, int ini, int fim) {
    printf("%*s", lbl, "");
    term_fg(COR_EIXO);
    fputs("└", stdout);
    for (int t = ini; t < fim; t++) fputs(t % 5 == 0 ? "┴" : "─", stdout);
    term_reset();
    printf("\n%*s ", lbl, "");
    /* números a cada 10 ticks (ou 5 se a janela for curta), sem sobrepor */
    int passo = (fim - ini) <= 40 ? 5 : 10;
    int pos = ini;
    for (int t = ini; t < fim;) {
        if (t % passo == 0 && t >= pos) {
            char num[16];
            int l = snprintf(num, sizeof num, "%d", t);
            if (t + l > fim + 6) break;
            fputs(num, stdout);
            t += l;
            pos = t + 1;
        } else {
            putchar(' ');
            t++;
        }
    }
    putchar('\n');
}

void gantt_tui(const Simulador *s, int ini, int fim) {
    const Estado *e = &s->atual;
    int n = e->n;
    if (fim > s->ncols) fim = s->ncols;
    if (ini < 0) ini = 0;
    int lbl = largura_rotulo(e);
    if (fim <= ini) {
        printf("%*s(nenhum tick simulado ainda)\n", lbl, "");
        return;
    }
    int w = fim - ini;
    unsigned char *mask = xmalloc((size_t)w * (size_t)(n ? n : 1));
    gantt_mascaras(s, ini, fim, mask);
    int *ordem = gantt_ordem(e);

    for (int r = 0; r < n; r++) {
        int i = ordem[r];
        const TCB *k = &e->tarefas[i];

        /* linha de marcadores */
        printf("%*s ", lbl, "");
        for (int t = ini; t < fim; t++) {
            unsigned m = mask[(size_t)(t - ini) * n + i];
            if (m & M_PRAZO) {
                term_fg(COR_PRAZO);
                term_negrito();
                fputs("✗", stdout);
                term_reset();
            } else if (m & M_TERMINO) {
                fputs("■", stdout);
            } else if ((m & M_CHEGADA) && (m & M_FIM)) {
                fputs("↕", stdout);
            } else if (m & M_FIM) {
                fputs("↑", stdout);
            } else if (m & M_CHEGADA) {
                fputs("↓", stdout);
            } else {
                putchar(' ');
            }
        }
        putchar('\n');

        /* barra */
        term_fg(k->cor);
        term_negrito();
        char rot[32];
        snprintf(rot, sizeof rot, "T%d", k->id);
        printf("%-*s", lbl, rot);
        term_reset();
        term_fg(COR_EIXO);
        fputs("│", stdout);
        term_reset();
        for (int t = ini; t < fim; t++) {
            unsigned m = mask[(size_t)(t - ini) * n + i];
            switch (sim_celula(s, t, i)) {
            case G_EXEC:
                term_bg(k->cor);
                term_fg(cor_contraste(k->cor));
                putchar((m & M_SORTEIO) ? '?' : digito_cpu(sim_cpu_celula(s, t, i)));
                term_reset();
                break;
            case G_PRONTA:
                term_fg(COR_PRONTA);
                fputs("─", stdout);
                term_reset();
                break;
            case G_SUSPENSA:
                term_bg(COR_SUSP_FUNDO);
                term_fg(COR_SUSP_TEXTO);
                fputs("░", stdout);
                term_reset();
                break;
            default:
                putchar(' ');
            }
        }
        putchar('\n');
    }
    eixo(lbl, ini, fim);

    /* bloco das CPUs */
    int ncpu_max = e->ncpus;
    for (int t = ini; t < fim; t++)
        if (s->ncpus_col[t] > ncpu_max) ncpu_max = s->ncpus_col[t];
    int *oc = ocupacao(s, ini, fim);
    for (int c = 0; c < ncpu_max; c++) {
        char rot[32];
        snprintf(rot, sizeof rot, "CPU%d", c);
        term_negrito();
        printf("%-*s", lbl, rot);
        term_reset();
        term_fg(COR_EIXO);
        fputs("│", stdout);
        term_reset();
        for (int t = ini; t < fim;) {
            int i = oc[(t - ini) * MAX_CPUS + c];
            if (c >= s->ncpus_col[t]) {     /* CPU não existia neste tick */
                putchar(' ');
                t++;
                continue;
            }
            if (i < 0) {
                term_fg(COR_DESLIGADA);
                fputs("·", stdout);
                term_reset();
                t++;
                continue;
            }
            /* trecho contínuo da mesma tarefa: escreve o id dentro dele */
            int t2 = t;
            while (t2 < fim && oc[(t2 - ini) * MAX_CPUS + c] == i) t2++;
            char id[16];
            int l = snprintf(id, sizeof id, "%d", e->tarefas[i].id);
            term_bg(e->tarefas[i].cor);
            term_fg(cor_contraste(e->tarefas[i].cor));
            /* Se o id não cabe no trecho, mostra só o 1º dígito. */
            bool cabe = l <= t2 - t;
            for (int x = 0; x < t2 - t; x++)
                putchar(x == 0 ? id[0] : (cabe && x < l ? id[x] : ' '));
            term_reset();
            t = t2;
        }
        putchar('\n');
    }
    free(oc);
    free(mask);
    free(ordem);

    /* legenda (req. 3: sempre visível junto do gráfico) */
    printf("Legenda: ");
    term_bg(0x3498DB); term_fg(0xFFFFFF); fputs("0", stdout); term_reset();
    printf(" executando (nº da CPU)  ");
    term_bg(0x3498DB); term_fg(0xFFFFFF); fputs("?", stdout); term_reset();
    printf(" sorteio  ");
    term_fg(COR_PRONTA); fputs("─", stdout); term_reset();
    printf(" pronta  ");
    term_bg(COR_SUSP_FUNDO); term_fg(COR_SUSP_TEXTO); fputs("░", stdout); term_reset();
    printf(" suspensa  ");
    term_fg(COR_DESLIGADA); fputs("·", stdout); term_reset();
    printf(" CPU desligada\n         ↓ chegada  ↑ fim de ativação  ↕ chegada+fim  ■ término  ");
    term_fg(COR_PRAZO); fputs("✗", stdout); term_reset();
    printf(" perda de prazo\n");
}

void gantt_tui_completo(const Simulador *s) {
    int lbl = largura_rotulo(&s->atual);
    int w = term_largura() - lbl - 3;
    if (w < 20) w = 20;
    if (s->ncols == 0) {
        gantt_tui(s, 0, 0);
        return;
    }
    for (int ini = 0; ini < s->ncols; ini += w) {
        int fim = ini + w < s->ncols ? ini + w : s->ncols;
        printf("\n--- ticks %d a %d ---\n", ini, fim - 1);
        gantt_tui(s, ini, fim);
    }
}

void gantt_texto(const Simulador *s) {
    const Estado *e = &s->atual;
    printf("Linha do tempo por CPU ([início, fim) e tarefa):\n");
    int ncpu_max = e->ncpus;
    for (int t = 0; t < s->ncols; t++)
        if (s->ncpus_col[t] > ncpu_max) ncpu_max = s->ncpus_col[t];
    int *oc = ocupacao(s, 0, s->ncols);
    for (int c = 0; c < ncpu_max; c++) {
        printf("  CPU%d:", c);
        for (int t = 0; t < s->ncols;) {
            int i = oc[t * MAX_CPUS + c];
            int t2 = t;
            while (t2 < s->ncols && oc[t2 * MAX_CPUS + c] == i) t2++;
            if (i >= 0) printf(" [%d,%d)T%d", t, t2, e->tarefas[i].id);
            else printf(" [%d,%d)desligada", t, t2);
            t = t2;
        }
        putchar('\n');
    }
    free(oc);
    printf("Eventos:\n");
    for (int i = 0; i < s->nev; i++) {
        const Evento *v = &s->ev[i];
        if (v->tipo == EV_PREEMPCAO || v->tipo == EV_CHEGADA || v->tipo == EV_FIM_ATIVACAO) continue;
        printf("  t=%d T%d %s (ativação %d)", v->instante, e->tarefas[v->tarefa].id,
               evento_nome(v->tipo), v->ativacao + 1);
        if (v->cpu >= 0) printf(" CPU%d", v->cpu);
        putchar('\n');
    }
}
