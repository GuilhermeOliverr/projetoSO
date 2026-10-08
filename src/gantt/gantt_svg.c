/*
 * gantt_svg.c - exporta o Gantt da simulação inteira como imagem SVG
 * (req. 2.4).
 *
 * Por que SVG: é texto puro (escrevemos com fprintf, sem biblioteca), não
 * tem limite de tamanho, é vetorial (dá zoom sem serrilhar) e abre em
 * qualquer navegador. Não é print de tela: o desenho é gerado a partir do
 * rastro completo da simulação.
 *
 * Layout (de cima para baixo): título e resumo; bloco das tarefas (maior
 * id em cima, menor id junto do eixo X); eixo do tempo; bloco das CPUs;
 * legenda.
 */
#include "gantt.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARGEM_ESQ   90
#define TOPO         78
#define ALT_LINHA    40     /* altura de uma linha de tarefa              */
#define ALT_BARRA    18
#define ALT_CPU      26     /* altura de uma linha de CPU                 */

/* Escreve texto escapando os caracteres especiais do XML. */
static void esc(FILE *f, const char *s) {
    for (; *s; s++) {
        switch (*s) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        default: fputc(*s, f);
        }
    }
}

/* Largura de um tick em pixels: diminui conforme a simulação cresce, para
 * que a imagem não fique gigantesca, mas nunca some (mínimo 3px). */
static int largura_tick(int T) {
    if (T <= 50) return 22;
    if (T <= 120) return 14;
    if (T <= 300) return 9;
    if (T <= 800) return 6;
    if (T <= 3000) return 4;
    return 3;
}

static void seta_baixo(FILE *f, double x, double y) {
    fprintf(f, "<path class=\"chegada\" d=\"M%.1f %.1f V%.1f M%.1f %.1f L%.1f %.1f L%.1f %.1f Z\"/>\n",
            x, y, y + 11, x - 4, y + 7, x, y + 13, x + 4, y + 7);
}

static void triangulo_cima(FILE *f, double x, double y) {
    fprintf(f, "<path class=\"fim\" d=\"M%.1f %.1f L%.1f %.1f L%.1f %.1f Z\"/>\n",
            x - 4.5, y, x, y - 8, x + 4.5, y);
}

static void quadrado(FILE *f, double x, double y) {
    fprintf(f, "<rect class=\"termino\" x=\"%.1f\" y=\"%.1f\" width=\"9\" height=\"9\"/>\n", x - 4.5, y - 9);
}

static void xis(FILE *f, double x, double y) {
    fprintf(f, "<path class=\"prazo\" d=\"M%.1f %.1f L%.1f %.1f M%.1f %.1f L%.1f %.1f\"/>\n",
            x - 5, y - 5, x + 5, y + 5, x - 5, y + 5, x + 5, y - 5);
}

static void sorteio(FILE *f, double x, double y) {
    fprintf(f, "<circle class=\"sorteio\" cx=\"%.1f\" cy=\"%.1f\" r=\"6\"/>"
            "<text class=\"sorteio-t\" x=\"%.1f\" y=\"%.1f\">?</text>\n", x, y, x, y + 3.5);
}

bool gantt_svg(const Simulador *s, const char *caminho, const char *titulo, char *erro, int tam_erro) {
    FILE *f = fopen(caminho, "w");
    if (!f) {
        snprintf(erro, (size_t)tam_erro, "não foi possível criar '%s': %s", caminho, strerror(errno));
        return false;
    }
    const Estado *e = &s->atual;
    int n = e->n, T = s->ncols;
    int cw = largura_tick(T);
    int ncpu = e->ncpus;
    for (int t = 0; t < T; t++)
        if (s->ncpus_col[t] > ncpu) ncpu = s->ncpus_col[t];

    int y_eixo = TOPO + n * ALT_LINHA;
    int y_cpus = y_eixo + 58;
    int y_leg = y_cpus + ncpu * ALT_CPU + 28;
    int larg = MARGEM_ESQ + T * cw + 40;
    if (larg < 980) larg = 980;
    int alt = y_leg + 80;

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
               "viewBox=\"0 0 %d %d\" font-family=\"DejaVu Sans, Arial, sans-serif\">\n",
            larg, alt, larg, alt);
    fputs("<defs>\n"
          /* suspensa: preto pontilhado (req. 2.1) */
          "<pattern id=\"susp\" width=\"4\" height=\"4\" patternUnits=\"userSpaceOnUse\">"
          "<rect width=\"4\" height=\"4\" fill=\"#fff\"/><circle cx=\"2\" cy=\"2\" r=\"1.1\" fill=\"#000\"/></pattern>\n"
          /* CPU desligada: hachurado cinza */
          "<pattern id=\"deslig\" width=\"6\" height=\"6\" patternUnits=\"userSpaceOnUse\" "
          "patternTransform=\"rotate(45)\"><rect width=\"6\" height=\"6\" fill=\"#eee\"/>"
          "<line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"6\" stroke=\"#aaa\" stroke-width=\"2\"/></pattern>\n"
          "</defs>\n<style>\n"
          ".titulo{font-size:17px;font-weight:bold}.sub{font-size:12px;fill:#444}\n"
          ".rot{font-size:13px;font-weight:bold;text-anchor:end}\n"
          ".eixo{stroke:#333;stroke-width:1.2}.grade{stroke:#ddd;stroke-width:1}\n"
          ".num{font-size:10px;text-anchor:middle;fill:#333}\n"
          ".base{stroke:#bbb;stroke-width:1}\n"
          ".exec{stroke:#222;stroke-width:0.6}.cpu-t{font-size:10px;text-anchor:middle}\n"
          ".pronta{fill:none;stroke:#888;stroke-width:1;stroke-dasharray:3 2}\n"
          ".susp{fill:url(#susp);stroke:#000;stroke-width:0.8}\n"
          ".deslig{fill:url(#deslig);stroke:#999;stroke-width:0.5}\n"
          ".chegada{stroke:#000;stroke-width:1.4;fill:#000}\n"
          ".fim{fill:#fff;stroke:#000;stroke-width:1.2}\n"
          ".termino{fill:#000}\n"
          ".prazo{stroke:#E00000;stroke-width:2.6;fill:none}\n"
          ".sorteio{fill:#7B2CBF;stroke:#fff;stroke-width:1}\n"
          ".sorteio-t{font-size:10px;font-weight:bold;fill:#fff;text-anchor:middle}\n"
          ".leg{font-size:12px}\n"
          "</style>\n<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n", f);

    /* título e resumo */
    int perdidos = 0;
    for (int i = 0; i < n; i++) perdidos += e->tarefas[i].deadlines_perdidos;
    const Escalonador *esc_alg = sched_obter(e->alg);
    fputs("<text class=\"titulo\" x=\"16\" y=\"26\">Gráfico de Gantt — ", f);
    esc(f, titulo);
    fputs("</text>\n", f);
    fprintf(f, "<text class=\"sub\" x=\"16\" y=\"46\">Algoritmo %s · %d CPU(s) · quantum %d · "
               "U = %.3f · %d tick(s) simulados · prazos perdidos: %d%s</text>\n",
            esc_alg ? esc_alg->nome : "?", e->ncpus, e->quantum, sim_utilizacao(e), T, perdidos,
            e->fim ? "" : " · (simulação incompleta)");

    /* grade vertical e eixo */
    int passo = 1;
    static const int passos[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 100000};
    for (size_t p = 0; p < sizeof passos / sizeof passos[0]; p++) {
        passo = passos[p];
        if (passo * cw >= 28) break;
    }
    for (int t = 0; t <= T; t += passo) {
        int x = MARGEM_ESQ + t * cw;
        fprintf(f, "<line class=\"grade\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", x, TOPO - 6, x, y_eixo);
        fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", x, y_eixo, x, y_eixo + 5);
        fprintf(f, "<text class=\"num\" x=\"%d\" y=\"%d\">%d</text>\n", x, y_eixo + 17, t);
    }
    fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
            MARGEM_ESQ, y_eixo, MARGEM_ESQ + T * cw + 10, y_eixo);
    fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
            MARGEM_ESQ, TOPO - 10, MARGEM_ESQ, y_eixo);
    fprintf(f, "<text class=\"num\" x=\"%d\" y=\"%d\">t</text>\n", MARGEM_ESQ + T * cw + 18, y_eixo + 4);

    /* tarefas: linha 0 (topo) = maior id */
    int *ordem = gantt_ordem(e);
    int *linha_de = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    for (int r = 0; r < n; r++) linha_de[ordem[r]] = r;
    for (int r = 0; r < n; r++) {
        int i = ordem[r];
        const TCB *k = &e->tarefas[i];
        int yb = TOPO + r * ALT_LINHA + (ALT_LINHA - ALT_BARRA) - 4;   /* topo da barra */
        fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\" fill=\"#%06X\">T%d</text>\n",
                MARGEM_ESQ - 10, yb + ALT_BARRA - 4, k->cor, k->id);
        fprintf(f, "<line class=\"base\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
                MARGEM_ESQ, yb + ALT_BARRA, MARGEM_ESQ + T * cw, yb + ALT_BARRA);
        /* trechos contínuos com o mesmo estado e a mesma CPU */
        for (int t = 0; t < T;) {
            CelulaGantt c = sim_celula(s, t, i);
            int cpu = sim_cpu_celula(s, t, i);
            int t2 = t + 1;
            while (t2 < T && sim_celula(s, t2, i) == c && sim_cpu_celula(s, t2, i) == cpu) t2++;
            int x = MARGEM_ESQ + t * cw, w = (t2 - t) * cw;
            if (c == G_EXEC) {
                fprintf(f, "<rect class=\"exec\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%06X\"/>\n",
                        x, yb, w, ALT_BARRA, k->cor);
                if (w >= 10) {
                    fprintf(f, "<text class=\"cpu-t\" x=\"%.1f\" y=\"%d\" fill=\"#%06X\">%s%d</text>\n",
                            x + w / 2.0, yb + 13, cor_contraste(k->cor), w >= 40 ? "CPU" : "", cpu);
                }
            } else if (c == G_PRONTA) {
                fprintf(f, "<rect class=\"pronta\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
                        x, yb + 5, w, ALT_BARRA - 10);
            } else if (c == G_SUSPENSA) {
                fprintf(f, "<rect class=\"susp\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
                        x, yb, w, ALT_BARRA);
            }
            t = t2;
        }
    }

    /* marcadores de evento, desenhados por cima das barras */
    for (int v = 0; v < s->nev; v++) {
        const Evento *ev = &s->ev[v];
        if (ev->tarefa >= n) continue;
        int yb = TOPO + linha_de[ev->tarefa] * ALT_LINHA + (ALT_LINHA - ALT_BARRA) - 4;
        double x = MARGEM_ESQ + (double)ev->instante * cw;
        switch (ev->tipo) {
        case EV_CHEGADA: seta_baixo(f, x, yb - 14); break;
        case EV_FIM_ATIVACAO: triangulo_cima(f, x, yb); break;
        case EV_TERMINO: quadrado(f, x + 6, yb); break;
        case EV_PRAZO_PERDIDO: xis(f, x, yb + ALT_BARRA / 2.0); break;
        case EV_SORTEIO: sorteio(f, x + 6, yb - 8); break;
        default: break;
        }
    }

    /* bloco das CPUs */
    fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\" style=\"text-anchor:start\">Processadores</text>\n",
            16, y_cpus - 10);
    for (int c = 0; c < ncpu; c++) {
        int y = y_cpus + c * ALT_CPU;
        fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\">CPU%d</text>\n", MARGEM_ESQ - 10, y + 14, c);
        for (int t = 0; t < T;) {
            int quem = -1;
            for (int i = 0; i < n; i++)
                if (sim_cpu_celula(s, t, i) == c) { quem = i; break; }
            bool existe = c < s->ncpus_col[t];
            int t2 = t + 1;
            while (t2 < T && (c < s->ncpus_col[t2]) == existe) {
                int q2 = -1;
                for (int i = 0; i < n; i++)
                    if (sim_cpu_celula(s, t2, i) == c) { q2 = i; break; }
                if (q2 != quem) break;
                t2++;
            }
            int x = MARGEM_ESQ + t * cw, w = (t2 - t) * cw;
            if (!existe) {
                /* CPU não existia nestes ticks (quantidade alterada em edição) */
            } else if (quem < 0) {
                fprintf(f, "<rect class=\"deslig\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
                        x, y, w, ALT_CPU - 8);
            } else {
                const TCB *k = &e->tarefas[quem];
                fprintf(f, "<rect class=\"exec\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%06X\"/>\n",
                        x, y, w, ALT_CPU - 8, k->cor);
                char id[16];
                int l = snprintf(id, sizeof id, "T%d", k->id);
                if (w >= l * 7)
                    fprintf(f, "<text class=\"cpu-t\" x=\"%.1f\" y=\"%d\" fill=\"#%06X\">%s</text>\n",
                            x + w / 2.0, y + 13, cor_contraste(k->cor), id);
            }
            t = t2;
        }
    }

    /* legenda */
    int lx = 16, ly = y_leg;
    fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\" style=\"text-anchor:start\">Legenda</text>\n", lx, ly);
    ly += 22;
    fprintf(f, "<rect class=\"exec\" x=\"%d\" y=\"%d\" width=\"28\" height=\"14\" fill=\"#3498DB\"/>"
               "<text class=\"cpu-t\" x=\"%d\" y=\"%d\" fill=\"#fff\">0</text>"
               "<text class=\"leg\" x=\"%d\" y=\"%d\">executando (nº da CPU)</text>\n",
            lx, ly - 11, lx + 14, ly, lx + 34, ly);
    lx += 190;
    fprintf(f, "<rect class=\"pronta\" x=\"%d\" y=\"%d\" width=\"28\" height=\"8\"/>"
               "<text class=\"leg\" x=\"%d\" y=\"%d\">pronta (sem cor)</text>\n", lx, ly - 8, lx + 34, ly);
    lx += 140;
    fprintf(f, "<rect class=\"susp\" x=\"%d\" y=\"%d\" width=\"28\" height=\"14\"/>"
               "<text class=\"leg\" x=\"%d\" y=\"%d\">suspensa</text>\n", lx, ly - 11, lx + 34, ly);
    lx += 110;
    fprintf(f, "<rect class=\"deslig\" x=\"%d\" y=\"%d\" width=\"28\" height=\"14\"/>"
               "<text class=\"leg\" x=\"%d\" y=\"%d\">CPU desligada</text>\n", lx, ly - 11, lx + 34, ly);
    lx = 16;
    ly += 26;
    seta_baixo(f, lx + 6, ly - 14);
    fprintf(f, "<text class=\"leg\" x=\"%d\" y=\"%d\">chegada</text>\n", lx + 16, ly);
    lx += 90;
    triangulo_cima(f, lx + 6, ly);
    fprintf(f, "<text class=\"leg\" x=\"%d\" y=\"%d\">fim de ativação</text>\n", lx + 16, ly);
    lx += 130;
    quadrado(f, lx + 6, ly);
    fprintf(f, "<text class=\"leg\" x=\"%d\" y=\"%d\">término (10ª ativação)</text>\n", lx + 16, ly);
    lx += 175;
    xis(f, lx + 6, ly - 5);
    fprintf(f, "<text class=\"leg\" x=\"%d\" y=\"%d\">perda de prazo</text>\n", lx + 16, ly);
    lx += 130;
    sorteio(f, lx + 6, ly - 5);
    fprintf(f, "<text class=\"leg\" x=\"%d\" y=\"%d\">sorteio (desempate)</text>\n", lx + 16, ly);

    fputs("</svg>\n", f);
    free(ordem);
    free(linha_de);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) snprintf(erro, (size_t)tam_erro, "erro ao gravar '%s' (disco cheio?)", caminho);
    return ok;
}
