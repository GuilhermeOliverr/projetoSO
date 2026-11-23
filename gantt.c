/*
 * gantt.c - o gráfico de Gantt: em SVG, no terminal e na janela gráfica.
 *
 * Junta, nesta ordem, src/gantt/gantt_svg.c, gantt_tui.c e gantt_gfx.c da
 * versão organizada em pastas. Cada trecho começa com um divisor e com o
 * comentário de cabeçalho original. Os #include de todos os trechos ficam
 * juntos logo abaixo. As funções static de desenho dos marcadores da seção
 * gantt_gfx.c ganharam o sufixo _gfx, porque a seção gantt_svg.c tem
 * funções static com os mesmos nomes.
 */
#include "gantt.h"
#include "gfx.h"
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * Seção gantt_svg.c (originalmente src/gantt/gantt_svg.c)
 * =========================================================================== */
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
 *
 * Como ler um SVG: é um arquivo de texto com "desenhos" em XML. <rect> é
 * um retângulo, <line> uma linha, <text> um texto, <path> uma forma livre
 * (M = mover o lápis, L = traçar linha até, V = linha vertical, Z = fechar).
 * Coordenadas em pixels: x cresce para a direita e y cresce PARA BAIXO. As
 * cores e estilos ficam num bloco <style> (classes CSS) para o desenho em
 * si ficar curto: class="exec", class="susp", etc.
 */

/* Medidas do layout, em pixels. */
#define MARGEM_ESQ   90     /* espaço mínimo à esquerda para os rótulos T1, T2... */
#define TOPO         78     /* espaço no alto para título e resumo        */
#define ALT_LINHA    40     /* altura de uma linha de tarefa              */
#define ALT_BARRA    18
#define ALT_CPU      26     /* altura de uma linha de CPU                 */

/* Escreve texto escapando os caracteres especiais do XML. Sem isto, um
 * título com "<" ou "&" (ex.: nome de arquivo) ou um id como "A&B" quebraria
 * o SVG. O texto é lido caractere a caractere (utf8_proximo): o SVG declara
 * UTF-8, e um byte que não forma UTF-8 válido (id de um arquivo salvo em
 * Latin-1 pelo Bloco de Notas) deixaria o arquivo inválido e o navegador
 * não abriria a imagem. Esse byte, e os caracteres que o XML proíbe
 * (controle, surrogates), viram '?', como na janela. */
static void esc(FILE *f, const char *s) {
    while (*s) {
        unsigned c = utf8_proximo(&s);
        switch (c) {
        case '&': fputs("&amp;", f); break;
        case '<': fputs("&lt;", f); break;
        case '>': fputs("&gt;", f); break;
        case '"': fputs("&quot;", f); break;
        case '\'': fputs("&apos;", f); break;
        default: {
            bool proibido = (c < 0x20 && c != '\t') || (c >= 0xD800 && c <= 0xDFFF) || c == 0xFFFE ||
                            c == 0xFFFF || c > 0x10FFFF;
            char u[4];
            if (proibido) fputc('?', f);
            else fwrite(u, 1, (size_t)utf8_escrever(u, c), f);
        }
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

/* Espaço à esquerda para os rótulos: os 90 px de sempre bastam para "T1" e
 * "CPU3"; um id de texto comprido (até 31 caracteres) alarga a margem em vez
 * de ser cortado pela borda esquerda da imagem (~9 px por caractere no
 * negrito de 13 px). */
static int margem_svg(const Estado *e) {
    int m = MARGEM_ESQ;
    bool todos_inteiros = sim_ids_inteiros(e);
    char rot[ID_MAX + 8];
    for (int i = 0; i < e->n; i++) {
        id_rotulo(e->tarefas[i].id, todos_inteiros, rot, sizeof rot);
        int l = 20 + 9 * utf8_colunas(rot);
        if (l > m) m = l;
    }
    return m;
}

/* Os cinco desenhos abaixo são os marcadores de evento (req. 2.2). Cada um
 * é pequeno e distinto, para a legenda explicar sem ambiguidade:
 *   seta para baixo = chegada     triângulo para cima = fim de ativação
 *   quadrado cheio  = término     X vermelho          = perda de prazo
 *   círculo roxo '?' = sorteio    (x, y = onde desenhar)                */
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

/* Gera o arquivo SVG da simulação INTEIRA (T = número de ticks simulados).
 * Devolve false e escreve o motivo em `erro` se não conseguir gravar. */
bool gantt_svg(const Simulador *s, const char *caminho, const char *titulo, char *erro, int tam_erro) {
    FILE *f = fopen(caminho, "w");
    if (!f) {
        snprintf(erro, (size_t)tam_erro, "não foi possível criar '%s': %s", caminho, strerror(errno));
        return false;
    }
    const Estado *e = &s->atual;
    int n = e->n, T = s->ncols;
    int cw = largura_tick(T);     /* largura de 1 tick em pixels (cell width) */
    int margem = margem_svg(e);   /* espaço à esquerda para os rótulos */
    /* Quantidade de CPUs a desenhar: o maior valor que existiu em algum
     * tick (o usuário pode ter mudado a quantidade durante a simulação). */
    int ncpu = e->ncpus;
    for (int t = 0; t < T; t++)
        if (s->ncpus_col[t] > ncpu) ncpu = s->ncpus_col[t];

    /* Posições verticais, de cima para baixo: tarefas -> eixo -> CPUs ->
     * legenda. A largura total acompanha o número de ticks (sem limite de
     * tempo, req. 2.4), com um mínimo de 980 para caber a legenda. */
    int y_eixo = TOPO + n * ALT_LINHA;
    int y_cpus = y_eixo + 58;
    int y_leg = y_cpus + ncpu * ALT_CPU + 28;
    int larg = margem + T * cw + 40;
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

    /* grade vertical e eixo. `passo` = de quantos em quantos ticks vai um
     * número; escolhemos o primeiro valor da lista que dê pelo menos 28px
     * entre números, para eles não se sobreporem. */
    int passo = 1;
    static const int passos[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 100000};
    for (size_t p = 0; p < sizeof passos / sizeof passos[0]; p++) {
        passo = passos[p];
        if (passo * cw >= 28) break;
    }
    for (int t = 0; t <= T; t += passo) {
        int x = margem + t * cw;
        fprintf(f, "<line class=\"grade\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", x, TOPO - 6, x, y_eixo);
        fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n", x, y_eixo, x, y_eixo + 5);
        fprintf(f, "<text class=\"num\" x=\"%d\" y=\"%d\">%d</text>\n", x, y_eixo + 17, t);
    }
    fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
            margem, y_eixo, margem + T * cw + 10, y_eixo);
    fprintf(f, "<line class=\"eixo\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
            margem, TOPO - 10, margem, y_eixo);
    fprintf(f, "<text class=\"num\" x=\"%d\" y=\"%d\">t</text>\n", margem + T * cw + 18, y_eixo + 4);

    /* tarefas: linha 0 (topo) = maior id. O rótulo (T1 ou o id cru, ver
     * id_rotulo) passa por esc(): um id como "A&B" é texto do usuário. */
    int *ordem = gantt_ordem(e);
    int *linha_de = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    bool todos_inteiros = sim_ids_inteiros(e);
    char rot[ID_MAX + 8];
    for (int r = 0; r < n; r++) linha_de[ordem[r]] = r;
    for (int r = 0; r < n; r++) {
        int i = ordem[r];
        const TCB *k = &e->tarefas[i];
        int yb = TOPO + r * ALT_LINHA + (ALT_LINHA - ALT_BARRA) - 4;   /* topo da barra */
        id_rotulo(k->id, todos_inteiros, rot, sizeof rot);
        fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\" fill=\"#%06X\">",
                margem - 10, yb + ALT_BARRA - 4, k->cor);
        esc(f, rot);
        fputs("</text>\n", f);
        fprintf(f, "<line class=\"base\" x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
                margem, yb + ALT_BARRA, margem + T * cw, yb + ALT_BARRA);
        /* trechos contínuos com o mesmo estado e a mesma CPU: em vez de um
         * retângulo por tick (milhares no arquivo), agrupamos ticks seguidos
         * iguais num retângulo só. t2 avança enquanto nada muda. */
        for (int t = 0; t < T;) {
            CelulaGantt c = sim_celula(s, t, i);
            int cpu = sim_cpu_celula(s, t, i);
            int t2 = t + 1;
            while (t2 < T && sim_celula(s, t2, i) == c && sim_cpu_celula(s, t2, i) == cpu) t2++;
            int x = margem + t * cw, w = (t2 - t) * cw;
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
        double x = margem + (double)ev->instante * cw;
        switch (ev->tipo) {
        case EV_CHEGADA: seta_baixo(f, x, yb - 14); break;
        case EV_FIM_ATIVACAO: triangulo_cima(f, x, yb); break;
        case EV_TERMINO: quadrado(f, x + 6, yb); break;
        case EV_PRAZO_PERDIDO: xis(f, x, yb + ALT_BARRA / 2.0); break;
        case EV_SORTEIO: sorteio(f, x + 6, yb - 8); break;
        default: break;
        }
    }

    /* bloco das CPUs (req. 1.2 e 2.1): uma linha por CPU mostrando qual
     * tarefa a ocupou em cada trecho; trecho sem tarefa = CPU desligada
     * (hachurado cinza). `quem` = índice da tarefa que usou a CPU c no tick
     * t (-1 = ninguém), achado procurando qual tarefa registrou aquela CPU. */
    fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\" style=\"text-anchor:start\">Processadores</text>\n",
            16, y_cpus - 10);
    for (int c = 0; c < ncpu; c++) {
        int y = y_cpus + c * ALT_CPU;
        fprintf(f, "<text class=\"rot\" x=\"%d\" y=\"%d\">CPU%d</text>\n", margem - 10, y + 14, c);
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
            int x = margem + t * cw, w = (t2 - t) * cw;
            if (!existe) {
                /* CPU não existia nestes ticks (quantidade alterada em edição) */
            } else if (quem < 0) {
                fprintf(f, "<rect class=\"deslig\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\"/>\n",
                        x, y, w, ALT_CPU - 8);
            } else {
                const TCB *k = &e->tarefas[quem];
                fprintf(f, "<rect class=\"exec\" x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%06X\"/>\n",
                        x, y, w, ALT_CPU - 8, k->cor);
                /* só escreve o rótulo se couber (~7 px por caractere,
                 * contando caracteres e não bytes) */
                id_rotulo(k->id, todos_inteiros, rot, sizeof rot);
                if (w >= utf8_colunas(rot) * 7) {
                    fprintf(f, "<text class=\"cpu-t\" x=\"%.1f\" y=\"%d\" fill=\"#%06X\">",
                            x + w / 2.0, y + 13, cor_contraste(k->cor));
                    esc(f, rot);
                    fputs("</text>\n", f);
                }
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
    /* Erros de escrita (disco cheio) só aparecem de verdade no ferror/fclose,
     * então conferimos os dois antes de dizer que deu certo. */
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) snprintf(erro, (size_t)tam_erro, "erro ao gravar '%s' (disco cheio?)", caminho);
    return ok;
}

/* As medidas do layout lá do início desta seção valem só para o SVG: a
 * seção gantt_gfx.c, mais abaixo, usa os mesmos nomes com outros valores. */
#undef MARGEM_ESQ
#undef TOPO
#undef ALT_LINHA
#undef ALT_BARRA
#undef ALT_CPU

/* ===========================================================================
 * Seção gantt_tui.c (originalmente src/gantt/gantt_tui.c)
 * =========================================================================== */
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
 *   marcador:  ↓ chegada  ↑ fim de ativação  ↕ os dois no mesmo instante
 *              (todos na coluna do instante exato: a coluna t começa em t)
 *              ■ término (10ª ativação)  ✗ perda de prazo
 *   CPUs:      cor e id da tarefa que ocupou a CPU; · CPU desligada
 *
 * De onde vêm os dados: o simulador (sim.c) já guardou, para cada tick e
 * cada tarefa, o que aconteceu (s->cel) e em qual CPU (s->cpu), além da
 * lista de eventos (s->ev). Este arquivo só LÊ esses dados e desenha; ele
 * não calcula nada da simulação.
 */

#define COR_PRONTA     0x808080
#define COR_SUSP_FUNDO 0xC8C8C8
#define COR_SUSP_TEXTO 0x000000
#define COR_DESLIGADA  0x606060
#define COR_PRAZO      0xFF4040
#define COR_EIXO       0x909090

/* Transforma a lista de eventos numa "máscara" por célula: para cada
 * (instante, tarefa) um número onde cada BIT diz que um tipo de evento
 * ocorreu ali (M_CHEGADA = bit 1, M_FIM = bit 2, ...). Usar bits permite
 * combinar eventos no mesmo instante (ex.: chegada + fim = ↕) com um OR.
 * A máscara cobre os instantes [ini, fim) e é indexada por
 * mask[(instante - ini) * n + tarefa]. */
void gantt_mascaras(const Simulador *s, int ini, int fim, unsigned char *mask) {
    int n = s->atual.n;
    memset(mask, 0, (size_t)(fim - ini) * (size_t)(n ? n : 1));
    for (int i = 0; i < s->nev; i++) {
        const Evento *v = &s->ev[i];
        /* Cada evento vai para a coluna do seu instante exato (req. 2.2):
         * a coluna t começa no instante t, então uma chegada em t fica no
         * início do tick t e um fim de ativação no instante t+1 fica logo
         * depois do último tick executado. */
        int col = v->instante;
        if (col < ini || col >= fim || v->tarefa >= n) continue;
        unsigned char *m = &mask[(size_t)(col - ini) * n + v->tarefa];
        switch (v->tipo) {
        case EV_CHEGADA: *m |= M_CHEGADA; break;
        case EV_FIM_ATIVACAO: *m |= M_FIM; break;
        case EV_TERMINO: *m |= M_TERMINO; break;
        case EV_PRAZO_PERDIDO: *m |= M_PRAZO; break;
        case EV_SORTEIO: *m |= M_SORTEIO; break;
        default: break;   /* preempção e fim de quantum não têm marcador */
        }
    }
}

/* qsort só passa dois elementos ao comparador, sem contexto extra. Como
 * precisamos consultar o vetor de tarefas para ordenar pelos IDs, o
 * apontamos por esta variável global (única exceção ao "sem globais"). */
static const Estado *g_ordem_estado;

/* Ordem DECRESCENTE de id: devolve negativo se `a` tem id MAIOR (vai antes).
 * A ordem dos ids é a de id_comparar (inteiros pelo valor, texto em ordem
 * natural: T2 antes de T10); comparar b com a, em vez de a com b, inverte
 * a ordem sem precisar negar o resultado. Nunca dá empate entre duas
 * tarefas, porque ids iguais por id_comparar são recusados como repetidos. */
static int cmp_id_desc(const void *a, const void *b) {
    const char *ia = g_ordem_estado->tarefas[*(const int *)a].id;
    const char *ib = g_ordem_estado->tarefas[*(const int *)b].id;
    return id_comparar(ib, ia);
}

/* Requisito 2.5: o menor ID fica mais perto do eixo X (embaixo). Como
 * desenhamos de cima para baixo, o maior ID sai primeiro. Devolve os
 * índices das tarefas já nessa ordem; quem chama libera o vetor. */
int *gantt_ordem(const Estado *e) {
    int *o = xmalloc((size_t)(e->n ? e->n : 1) * sizeof(int));
    for (int i = 0; i < e->n; i++) o[i] = i;
    g_ordem_estado = e;
    qsort(o, (size_t)e->n, sizeof(int), cmp_id_desc);
    return o;
}

/* Um caractere para a CPU: 0-9, depois A-Z (10 a 35), depois '#'. Cabe em
 * 1 coluna (cada tick tem 1 coluna só). */
static char digito_cpu(int c) {
    if (c < 0) return ' ';
    if (c < 10) return (char)('0' + c);
    if (c < 36) return (char)('A' + c - 10);
    return '#';
}

/* Largura do rótulo à esquerda ("T12", "CPU3"), igual para todas as linhas
 * (para o gráfico ficar alinhado). Mínimo 5; cresce se algum id for grande.
 * Conta caracteres (utf8_colunas), não bytes: "Tarefa_é" tem 9 bytes mas
 * ocupa 8 colunas no terminal. */
static int largura_rotulo(const Estado *e) {
    int w = 5;
    char buf[ID_MAX + 8];
    bool todos_inteiros = sim_ids_inteiros(e);
    for (int i = 0; i < e->n; i++) {
        id_rotulo(e->tarefas[i].id, todos_inteiros, buf, sizeof buf);
        int l = utf8_colunas(buf);
        if (l + 1 > w) w = l + 1;
    }
    int l = snprintf(buf, sizeof buf, "CPU%d", e->ncpus - 1);
    if (l + 1 > w) w = l + 1;
    return w;
}

/* Escreve o rótulo e completa com espaços até `largura` colunas. O
 * printf("%-*s") faria o mesmo contando bytes e desalinharia ids com
 * acento. */
static void rotulo_alinhado(const char *rot, int largura) {
    int folga = largura - utf8_colunas(rot);
    printf("%s%*s", rot, folga > 0 ? folga : 0, "");
}

/* Quem estava em cada CPU em cada tick: ocup[(t-ini)*MAX_CPUS + c].
 * O rastro guarda "qual CPU usou a tarefa i"; para desenhar as linhas das
 * CPUs precisamos do inverso, "qual tarefa usou a CPU c". Aqui invertemos.
 * -1 = nenhuma (CPU desligada naquele tick). */
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

/* Desenha o eixo do tempo: uma linha com ┴ a cada 5 ticks e, embaixo, os
 * números (de 10 em 10, ou de 5 em 5 se a janela for curta). Escrevemos
 * caractere por caractere porque um número de vários dígitos ocupa várias
 * colunas: avançamos `t` pelo tamanho do número impresso (`t += l`) e
 * `pos` impede que o próximo número encoste no anterior. */
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

/* Quantos caracteres do começo 'a' e 'b' têm em comum, sem diferenciar
 * maiúsculas (a mesma regra do id: T1 e t1 são o mesmo). Compara caractere
 * a caractere (utf8_proximo) para nunca cortar um caractere UTF-8 ao meio. */
static int prefixo_comum(const char *a, const char *b) {
    int n = 0;
    while (*a && *b) {
        unsigned ca = utf8_proximo(&a), cb = utf8_proximo(&b);
        if (ca < 128 && ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb < 128 && cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) break;
        n++;
    }
    return n;
}

/* Avança 'n' caracteres em 's' (sem passar do fim). */
static const char *pular_caracteres(const char *s, int n) {
    while (n-- > 0 && *s) utf8_proximo(&s);
    return s;
}

/* O que escrever dentro de cada trecho da linha das CPUs no Gantt de
 * terminal. Antes, um id que não cabia no trecho virava só o 1º caractere: com
 * ids T1, T2, T3 todo trecho de 1 tick mostrava "T" e não dava para saber
 * qual tarefa ocupou a CPU. A regra agora mostra o que DIFERENCIA as tarefas:
 *   - ids inteiros: nada muda (o id cru, sem o "T"; se não couber, o 1º
 *     algarismo), para a saída ser idêntica à de antes;
 *   - ids de texto: tira o prefixo comum a TODOS os ids (T1/T2/T3 viram
 *     1/2/3, Tarefa_1/Tarefa_2 viram 1/2), e assim o caso T1,T2,T3 fica
 *     igual ao caso 1,2,3. O prefixo nunca come um id inteiro (cada um fica
 *     com pelo menos 1 caractere);
 *   - se mesmo assim não couber (trecho de 1 tick com A1, A2, B1, que não
 *     têm prefixo comum), mostra o 1º caractere em que o id difere do id mais
 *     parecido com ele: A1 -> '1', A2 -> '2', B1 -> 'B'. Se o id é o começo
 *     de outro (T1 e T10), o 1º caractere que o difere não existe; aí vai o
 *     último dele (T1 -> '1', T10 -> '0').
 * Um caractere só não diferencia todos os casos possíveis (é uma coluna por
 * tick), mas esta regra resolve os nomes usuais (prefixo + número, letras). */
static void rotulos_cpu(const Estado *e, bool todos_inteiros, const char **curto, const char **marca) {
    int n = e->n;
    int prefixo = 0;
    if (!todos_inteiros && n > 1) {
        prefixo = INT32_MAX;
        for (int i = 0; i < n; i++) {
            int c = utf8_colunas(e->tarefas[i].id) - 1;   /* deixa ao menos 1 caractere */
            if (c < prefixo) prefixo = c;
            if (i > 0) {
                int p = prefixo_comum(e->tarefas[0].id, e->tarefas[i].id);
                if (p < prefixo) prefixo = p;
            }
        }
        if (prefixo < 0) prefixo = 0;
    }
    for (int i = 0; i < n; i++) {
        const char *id = e->tarefas[i].id;
        curto[i] = marca[i] = pular_caracteres(id, prefixo);
        if (todos_inteiros || n < 2) continue;
        int perto = 0;   /* maior prefixo em comum com algum outro id */
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            int p = prefixo_comum(id, e->tarefas[j].id);
            if (p > perto) perto = p;
        }
        int tam = utf8_colunas(id);
        marca[i] = pular_caracteres(id, perto < tam ? perto : tam - 1);
    }
}

/* Desenha o Gantt da janela de ticks [ini, fim). Estrutura:
 *   para cada tarefa (de cima para baixo): linha de marcadores + barra;
 *   eixo do tempo;
 *   uma linha por CPU;
 *   legenda. */
void gantt_tui(const Simulador *s, int ini, int fim) {
    const Estado *e = &s->atual;
    int n = e->n;
    if (fim > s->ncols) fim = s->ncols;   /* não desenha o que não foi simulado */
    if (ini < 0) ini = 0;
    int lbl = largura_rotulo(e);
    if (fim <= ini) {
        printf("%*s(nenhum tick simulado ainda)\n", lbl, "");
        return;
    }
    /* A linha de marcadores tem uma coluna a mais quando a janela chega ao
     * último tick: é onde aparecem os eventos do instante final (ex.: o
     * término da última ativação). Nas outras janelas esse instante é a
     * primeira coluna da janela seguinte, e não o repetimos. */
    int fim_m = fim == s->ncols ? fim + 1 : fim;
    unsigned char *mask = xmalloc((size_t)(fim_m - ini) * (size_t)(n ? n : 1));
    gantt_mascaras(s, ini, fim_m, mask);
    int *ordem = gantt_ordem(e);
    bool todos_inteiros = sim_ids_inteiros(e);

    for (int r = 0; r < n; r++) {
        int i = ordem[r];
        const TCB *k = &e->tarefas[i];

        /* linha de marcadores: um símbolo por coluna. A ordem do if/else
         * dá a prioridade visual: perda de prazo > término > chegada+fim
         * > fim > chegada. */
        printf("%*s ", lbl, "");
        for (int t = ini; t < fim_m; t++) {
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

        /* barra: rótulo colorido, '│' e uma célula por tick. */
        term_fg(k->cor);
        term_negrito();
        char rot[ID_MAX + 8];
        id_rotulo(k->id, todos_inteiros, rot, sizeof rot);
        rotulo_alinhado(rot, lbl);
        term_reset();
        term_fg(COR_EIXO);
        fputs("│", stdout);
        term_reset();
        for (int t = ini; t < fim; t++) {
            unsigned m = mask[(size_t)(t - ini) * n + i];
            switch (sim_celula(s, t, i)) {
            case G_EXEC:
                /* fundo = cor da tarefa; texto = número da CPU (ou '?' se
                 * a tarefa foi escolhida por sorteio neste tick). */
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

    /* bloco das CPUs: uma linha por CPU mostrando QUEM a ocupou em cada
     * tick. Trechos contínuos da mesma tarefa viram um bloco colorido com o
     * id escrito dentro (em vez de repetir o id em cada coluna). */
    int ncpu_max = e->ncpus;
    for (int t = ini; t < fim; t++)
        if (s->ncpus_col[t] > ncpu_max) ncpu_max = s->ncpus_col[t];
    int *oc = ocupacao(s, ini, fim);
    const char **curto = xmalloc((size_t)(n ? n : 1) * sizeof *curto);
    const char **marca = xmalloc((size_t)(n ? n : 1) * sizeof *marca);
    rotulos_cpu(e, todos_inteiros, curto, marca);
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
                /* Ninguém usou: CPU desligada (req. 1.2). */
                term_fg(COR_DESLIGADA);
                fputs("·", stdout);
                term_reset();
                t++;
                continue;
            }
            /* trecho contínuo da mesma tarefa: escreve o id dentro dele (cru,
             * sem o "T" dos rótulos; os de texto sem o prefixo comum) */
            int t2 = t;
            while (t2 < fim && oc[(t2 - ini) * MAX_CPUS + c] == i) t2++;
            const char *id = curto[i];
            int l = utf8_colunas(id);
            term_bg(e->tarefas[i].cor);
            term_fg(cor_contraste(e->tarefas[i].cor));
            /* Se o id (sem o prefixo comum) não cabe no trecho, mostra só o
             * caractere que diferencia a tarefa (ver rotulos_cpu). Cada
             * coluna recebe um caractere inteiro (todos os bytes dele), para
             * não cortar um caractere UTF-8 ao meio. */
            bool cabe = l <= t2 - t;
            if (!cabe) id = marca[i];
            for (int x = 0; x < t2 - t; x++) {
                if (*id && (x == 0 || (cabe && x < l))) {
                    const char *ini_c = id;
                    utf8_proximo(&id);
                    fwrite(ini_c, 1, (size_t)(id - ini_c), stdout);
                } else {
                    putchar(' ');
                }
            }
            term_reset();
            t = t2;
        }
        putchar('\n');
    }
    free(curto);
    free(marca);
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

/* Gantt de toda a simulação: quebra em "faixas" da largura do terminal
 * (uma simulação de 3000 ticks não cabe numa linha). O -3 reserva espaço
 * para o rótulo e a margem. */
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

/* Versão em texto puro (opção --texto): lista "CPU0: [0,20)T1 [20,60)T2..."
 * agrupando ticks seguidos da mesma tarefa em intervalos [início, fim).
 * Serve para conferir o resultado à mão e nos testes automáticos, que
 * comparam este texto com o esperado. Os eventos pouco interessantes
 * (preempção, quantum, chegada, fim) ficam de fora para não poluir. */
void gantt_texto(const Simulador *s) {
    const Estado *e = &s->atual;
    bool todos_inteiros = sim_ids_inteiros(e);
    char rot[ID_MAX + 8];
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
            if (i >= 0) {
                id_rotulo(e->tarefas[i].id, todos_inteiros, rot, sizeof rot);
                printf(" [%d,%d)%s", t, t2, rot);
            } else {
                printf(" [%d,%d)desligada", t, t2);
            }
            t = t2;
        }
        putchar('\n');
    }
    free(oc);
    printf("Eventos:\n");
    for (int i = 0; i < s->nev; i++) {
        const Evento *v = &s->ev[i];
        if (v->tipo == EV_PREEMPCAO || v->tipo == EV_FIM_QUANTUM || v->tipo == EV_CHEGADA || v->tipo == EV_FIM_ATIVACAO) continue;
        id_rotulo(e->tarefas[v->tarefa].id, todos_inteiros, rot, sizeof rot);
        printf("  t=%d %s %s (ativação %d)", v->instante, rot, evento_nome(v->tipo), v->ativacao + 1);
        if (v->cpu >= 0) printf(" CPU%d", v->cpu);
        putchar('\n');
    }
}

/* ===========================================================================
 * Seção gantt_gfx.c (originalmente src/gantt/gantt_gfx.c)
 * =========================================================================== */
/*
 * gantt_gfx.c - Gantt dentro da janela gráfica.
 *
 * É o mesmo desenho do SVG (seção gantt_svg.c), só que em pixels e mostrando
 * apenas o trecho visível (a interface rola e dá zoom). Mesmos símbolos,
 * para quem olhou a janela reconhecer a imagem final e vice-versa:
 *   barra colorida com o nº da CPU = executando    tracejado = pronta
 *   preto pontilhado = suspensa    seta para baixo = chegada
 *   triângulo para cima = fim de ativação    quadrado = término
 *   X vermelho = perda de prazo    círculo roxo "?" = sorteio
 *   linhas "CPUn": quem ocupou cada CPU; listrado = CPU desligada
 *
 * Como os outros desenhos, só LÊ o rastro da simulação (s->cel, s->cpu,
 * s->ev); não calcula nada.
 */

/* Medidas do layout, em pixels. */
#define TOPO       8      /* espaço acima da primeira tarefa                 */
#define ALT_LINHA  38     /* altura de uma linha de tarefa                   */
#define DESC_BARRA 16     /* da linha até a barra (espaço dos marcadores)    */
#define ALT_BARRA  18
#define ALT_CPU    22     /* altura de uma linha de CPU                      */

#define C_TEXTO    0x1E2328
#define C_EIXO     0x333333
#define C_GRADE    0xE3E5E8
#define C_SEL      0xE6EEFB
#define C_AGORA    0x2563EB
#define C_PRAZO    0xE00000
#define C_SORTEIO  0x7B2CBF

static int largura_rotulos(const Estado *e) {
    int w = texto_largura("CPU00") + 16;
    char buf[ID_MAX + 8];
    bool todos_inteiros = sim_ids_inteiros(e);
    for (int i = 0; i < e->n; i++) {
        id_rotulo(e->tarefas[i].id, todos_inteiros, buf, sizeof buf);
        int l = texto_largura(buf) + 16;
        if (l > w) w = l;
    }
    return w;
}

/* ---- marcadores de evento (os mesmos desenhos do SVG) ---- */

static void seta_baixo_gfx(Tela *t, int x, int y) {
    tela_linha(t, x, y, x, y + 9, 2, 0x000000);
    tela_triangulo(t, x - 4, y + 7, x + 4, y + 7, x, y + 13, 0x000000);
}

static void triangulo_cima_gfx(Tela *t, int x, int y) {
    tela_triangulo(t, x - 5, y, x + 5, y, x, y - 9, 0xFFFFFF);
    tela_linha(t, x - 5, y, x + 5, y, 1, 0x000000);
    tela_linha(t, x - 5, y, x, y - 9, 1, 0x000000);
    tela_linha(t, x + 5, y, x, y - 9, 1, 0x000000);
}

static void quadrado_gfx(Tela *t, int x, int y) { tela_ret(t, x - 4, y - 9, 9, 9, 0x000000); }

static void xis_gfx(Tela *t, int x, int y) {
    tela_linha(t, x - 5, y - 5, x + 5, y + 5, 3, C_PRAZO);
    tela_linha(t, x - 5, y + 5, x + 5, y - 5, 3, C_PRAZO);
}

static void sorteio_gfx(Tela *t, int x, int y) {
    tela_circulo(t, x, y, 7, 0xFFFFFF);
    tela_circulo(t, x, y, 6, C_SORTEIO);
    tela_texto(t, x - 3, y - 8, "?", 0xFFFFFF);
}

/* Texto centralizado num trecho, só se couber. */
static void texto_no_meio(Tela *t, int x, int w, int y, const char *s, uint32_t cor) {
    int l = texto_largura(s);
    if (l + 2 <= w) tela_texto(t, x + (w - l) / 2, y, s, cor);
}

void gantt_gfx(Tela *t, const Simulador *s, VistaGantt *v) {
    const Estado *e = &s->atual;
    int n = e->n, T = s->ncols;
    v->lbl = largura_rotulos(e);
    int x0 = v->x + v->lbl;                     /* x do instante t_ini       */
    int nvis = (v->w - v->lbl - 12) / v->cw;    /* ticks que cabem           */
    if (nvis < 1) nvis = 1;
    int t_fim = v->t_ini + nvis < T ? v->t_ini + nvis : T;
    int ncpu = e->ncpus;
    for (int c = 0; c < T; c++)
        if (s->ncpus_col[c] > ncpu) ncpu = s->ncpus_col[c];

    int base = v->y + TOPO - v->y_rol;          /* y do conteúdo após rolar  */
    int y_eixo = base + n * ALT_LINHA;
    int y_cpus = y_eixo + 50;
    v->alt_conteudo = TOPO + n * ALT_LINHA + 50 + ncpu * ALT_CPU + 8;

    tela_ret(t, v->x, v->y, v->w, v->h, 0xFFFFFF);
    tela_recorte(t, v->x, v->y, v->w, v->h);

    /* passo dos números do eixo: o primeiro que deixa >= 40 px entre eles */
    static const int passos[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 100000};
    int passo = 1;
    for (size_t p = 0; p < sizeof passos / sizeof passos[0]; p++) {
        passo = passos[p];
        if (passo * v->cw >= 40) break;
    }

    int *ordem = gantt_ordem(e);
    bool todos_inteiros = sim_ids_inteiros(e);

    /* fundo da tarefa selecionada e rótulos (fora da área que rola de lado) */
    for (int r = 0; r < n; r++) {
        const TCB *k = &e->tarefas[ordem[r]];
        int yl = base + r * ALT_LINHA;
        if (ordem[r] == v->sel) tela_ret(t, v->x, yl, v->w, ALT_LINHA, C_SEL);
        char rot[ID_MAX + 8];
        id_rotulo(k->id, todos_inteiros, rot, sizeof rot);
        tela_texto_negrito(t, x0 - 10 - texto_largura(rot), yl + DESC_BARRA + 1, rot, k->cor);
    }
    tela_texto_negrito(t, v->x + 8, y_eixo + 28, "Processadores", C_TEXTO);
    for (int c = 0; c < ncpu; c++) {
        char rot[16];
        snprintf(rot, sizeof rot, "CPU%d", c);
        tela_texto_negrito(t, x0 - 10 - texto_largura(rot), y_cpus + c * ALT_CPU + 2, rot, C_TEXTO);
    }

    /* daqui em diante só desenha à direita dos rótulos */
    tela_recorte(t, x0 - 6, v->y, v->w - (x0 - 6 - v->x), v->h);

    /* grade vertical e eixo do tempo */
    int t_eixo_fim = v->t_ini + nvis;
    for (int c = v->t_ini - v->t_ini % passo; c <= t_eixo_fim; c += passo) {
        if (c < v->t_ini) continue;
        int x = x0 + (c - v->t_ini) * v->cw;
        tela_ret(t, x, base - 4, 1, y_eixo - base + 4, C_GRADE);
        tela_ret(t, x, y_eixo, 1, 5, C_EIXO);
        char num[16];
        snprintf(num, sizeof num, "%d", c);
        tela_texto(t, x - texto_largura(num) / 2, y_eixo + 7, num, C_EIXO);
    }
    tela_ret(t, x0, y_eixo, (t_fim - v->t_ini) * v->cw + 8, 1, C_EIXO);
    tela_ret(t, x0, base - 6, 1, y_eixo - base + 6, C_EIXO);

    /* barras das tarefas: trechos contínuos com o mesmo estado e CPU viram
     * um retângulo só (assim o número da CPU cabe dentro) */
    for (int r = 0; r < n; r++) {
        int i = ordem[r];
        const TCB *k = &e->tarefas[i];
        int yb = base + r * ALT_LINHA + DESC_BARRA;
        tela_ret(t, x0, yb + ALT_BARRA, (t_fim - v->t_ini) * v->cw, 1, 0xBBBBBB);
        for (int c = v->t_ini; c < t_fim;) {
            CelulaGantt g = sim_celula(s, c, i);
            int cpu = sim_cpu_celula(s, c, i);
            int c2 = c + 1;
            while (c2 < t_fim && sim_celula(s, c2, i) == g && sim_cpu_celula(s, c2, i) == cpu) c2++;
            int x = x0 + (c - v->t_ini) * v->cw, w = (c2 - c) * v->cw;
            if (g == G_EXEC) {
                tela_ret(t, x, yb, w, ALT_BARRA, k->cor);
                tela_moldura(t, x, yb, w, ALT_BARRA, 0x222222);
                char rot[16];
                snprintf(rot, sizeof rot, "CPU%d", cpu);
                if (texto_largura(rot) + 2 > w) snprintf(rot, sizeof rot, "%d", cpu);
                texto_no_meio(t, x, w, yb + 1, rot, cor_contraste(k->cor));
            } else if (g == G_PRONTA) {
                tela_tracejado(t, x, yb + 5, w, ALT_BARRA - 10, 0x888888);
            } else if (g == G_SUSPENSA) {
                tela_pontilhado(t, x, yb, w, ALT_BARRA);
            }
            c = c2;
        }
    }

    /* marcadores de evento por cima das barras; o instante t_fim também
     * aparece (ex.: o término da última ativação, logo após o último tick) */
    int *linha_de = xmalloc((size_t)(n ? n : 1) * sizeof(int));
    for (int r = 0; r < n; r++) linha_de[ordem[r]] = r;
    for (int j = 0; j < s->nev; j++) {
        const Evento *ev = &s->ev[j];
        if (ev->tarefa >= n || ev->instante < v->t_ini || ev->instante > t_fim) continue;
        int yb = base + linha_de[ev->tarefa] * ALT_LINHA + DESC_BARRA;
        int x = x0 + (ev->instante - v->t_ini) * v->cw;
        switch (ev->tipo) {
        case EV_CHEGADA: seta_baixo_gfx(t, x, yb - 14); break;
        case EV_FIM_ATIVACAO: triangulo_cima_gfx(t, x, yb); break;
        case EV_TERMINO: quadrado_gfx(t, x + 6, yb); break;
        case EV_PRAZO_PERDIDO: xis_gfx(t, x, yb + ALT_BARRA / 2); break;
        case EV_SORTEIO: sorteio_gfx(t, x + 7, yb - 7); break;
        default: break;
        }
    }
    free(linha_de);

    /* linhas das CPUs: quem ocupou cada uma em cada trecho */
    for (int c = 0; c < ncpu; c++) {
        int y = y_cpus + c * ALT_CPU;
        for (int tk = v->t_ini; tk < t_fim;) {
            int quem = -1;
            for (int i = 0; i < n; i++)
                if (sim_cpu_celula(s, tk, i) == c) { quem = i; break; }
            bool existe = c < s->ncpus_col[tk];
            int t2 = tk + 1;
            while (t2 < t_fim && (c < s->ncpus_col[t2]) == existe) {
                int q2 = -1;
                for (int i = 0; i < n; i++)
                    if (sim_cpu_celula(s, t2, i) == c) { q2 = i; break; }
                if (q2 != quem) break;
                t2++;
            }
            int x = x0 + (tk - v->t_ini) * v->cw, w = (t2 - tk) * v->cw;
            if (!existe) {
                /* a CPU não existia (quantidade alterada durante a simulação) */
            } else if (quem < 0) {
                tela_hachurado(t, x, y, w, ALT_CPU - 6);
            } else {
                const TCB *k = &e->tarefas[quem];
                tela_ret(t, x, y, w, ALT_CPU - 6, k->cor);
                tela_moldura(t, x, y, w, ALT_CPU - 6, 0x222222);
                char rot[ID_MAX + 8];
                id_rotulo(k->id, todos_inteiros, rot, sizeof rot);
                texto_no_meio(t, x, w, y, rot, cor_contraste(k->cor));
            }
            tk = t2;
        }
    }

    /* instante atual: linha azul atravessando tarefas e CPUs */
    if (v->t_marca >= v->t_ini && v->t_marca <= v->t_ini + nvis) {
        int x = x0 + (v->t_marca - v->t_ini) * v->cw;
        tela_ret(t, x, base - 6, 2, y_cpus + ncpu * ALT_CPU - base + 6, C_AGORA);
    }

    free(ordem);
    tela_sem_recorte(t);
}

int gantt_gfx_tarefa_em(const Simulador *s, const VistaGantt *v, int x, int y) {
    const Estado *e = &s->atual;
    if (x < v->x || x >= v->x + v->w || y < v->y || y >= v->y + v->h) return -1;
    int dy = y - (v->y + TOPO - v->y_rol);
    if (dy < 0 || dy >= e->n * ALT_LINHA) return -1;
    int *ordem = gantt_ordem(e);
    int i = ordem[dy / ALT_LINHA];
    free(ordem);
    return i;
}

/* Legenda: cada item é um símbolo + texto; quando não cabe na linha, quebra. */
int gantt_gfx_legenda(Tela *t, int x, int y, int w, bool desenhar) {
    static const char *const textos[] = {
        "executando (nº da CPU)", "pronta", "suspensa", "CPU desligada", "chegada",
        "fim de ativação", "término (10ª ativação)", "perda de prazo", "sorteio (desempate)", "instante atual",
    };
    int n = (int)(sizeof textos / sizeof textos[0]);
    int cx = x, cy = y;
    for (int i = 0; i < n; i++) {
        int larg = 36 + texto_largura(textos[i]) + 18;
        if (cx + larg > x + w && cx > x) {
            cx = x;
            cy += 22;
        }
        if (desenhar) {
            int sx = cx, sy = cy + 2;   /* área do símbolo: 28 x 14 */
            switch (i) {
            case 0:
                tela_ret(t, sx, sy, 28, 14, 0x3498DB);
                tela_moldura(t, sx, sy, 28, 14, 0x222222);
                tela_texto(t, sx + 10, sy - 1, "0", 0xFFFFFF);
                break;
            case 1: tela_tracejado(t, sx, sy + 3, 28, 8, 0x888888); break;
            case 2: tela_pontilhado(t, sx, sy, 28, 14); break;
            case 3: tela_hachurado(t, sx, sy, 28, 14); break;
            case 4: seta_baixo_gfx(t, sx + 14, sy); break;
            case 5: triangulo_cima_gfx(t, sx + 14, sy + 12); break;
            case 6: quadrado_gfx(t, sx + 14, sy + 12); break;
            case 7: xis_gfx(t, sx + 14, sy + 7); break;
            case 8: sorteio_gfx(t, sx + 14, sy + 7); break;
            case 9: tela_ret(t, sx + 13, sy - 1, 2, 16, C_AGORA); break;
            }
            tela_texto(t, cx + 34, cy, textos[i], C_TEXTO);
        }
        cx += larg;
    }
    return cy - y + 20;
}
