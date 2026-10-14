/*
 * gantt_gfx.c - Gantt dentro da janela gráfica.
 *
 * É o mesmo desenho do SVG (gantt_svg.c), só que em pixels e mostrando
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
#include "gantt.h"
#include "tela.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    char buf[32];
    for (int i = 0; i < e->n; i++) {
        snprintf(buf, sizeof buf, "T%d", e->tarefas[i].id);
        int l = texto_largura(buf) + 16;
        if (l > w) w = l;
    }
    return w;
}

/* ---- marcadores de evento (os mesmos desenhos do SVG) ---- */

static void seta_baixo(Tela *t, int x, int y) {
    tela_linha(t, x, y, x, y + 9, 2, 0x000000);
    tela_triangulo(t, x - 4, y + 7, x + 4, y + 7, x, y + 13, 0x000000);
}

static void triangulo_cima(Tela *t, int x, int y) {
    tela_triangulo(t, x - 5, y, x + 5, y, x, y - 9, 0xFFFFFF);
    tela_linha(t, x - 5, y, x + 5, y, 1, 0x000000);
    tela_linha(t, x - 5, y, x, y - 9, 1, 0x000000);
    tela_linha(t, x + 5, y, x, y - 9, 1, 0x000000);
}

static void quadrado(Tela *t, int x, int y) { tela_ret(t, x - 4, y - 9, 9, 9, 0x000000); }

static void xis(Tela *t, int x, int y) {
    tela_linha(t, x - 5, y - 5, x + 5, y + 5, 3, C_PRAZO);
    tela_linha(t, x - 5, y + 5, x + 5, y - 5, 3, C_PRAZO);
}

static void sorteio(Tela *t, int x, int y) {
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

    /* fundo da tarefa selecionada e rótulos (fora da área que rola de lado) */
    for (int r = 0; r < n; r++) {
        const TCB *k = &e->tarefas[ordem[r]];
        int yl = base + r * ALT_LINHA;
        if (ordem[r] == v->sel) tela_ret(t, v->x, yl, v->w, ALT_LINHA, C_SEL);
        char rot[32];
        snprintf(rot, sizeof rot, "T%d", k->id);
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
        case EV_CHEGADA: seta_baixo(t, x, yb - 14); break;
        case EV_FIM_ATIVACAO: triangulo_cima(t, x, yb); break;
        case EV_TERMINO: quadrado(t, x + 6, yb); break;
        case EV_PRAZO_PERDIDO: xis(t, x, yb + ALT_BARRA / 2); break;
        case EV_SORTEIO: sorteio(t, x + 7, yb - 7); break;
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
                char rot[16];
                snprintf(rot, sizeof rot, "T%d", k->id);
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
            case 4: seta_baixo(t, sx + 14, sy); break;
            case 5: triangulo_cima(t, sx + 14, sy + 12); break;
            case 6: quadrado(t, sx + 14, sy + 12); break;
            case 7: xis(t, sx + 14, sy + 7); break;
            case 8: sorteio(t, sx + 14, sy + 7); break;
            case 9: tela_ret(t, sx + 13, sy - 1, 2, 16, C_AGORA); break;
            }
            tela_texto(t, cx + 34, cy, textos[i], C_TEXTO);
        }
        cx += larg;
    }
    return cy - y + 20;
}
