/*
 * ui_gui.c - a interface do simulador numa janela gráfica.
 *
 * Faz o mesmo que a interface de terminal (ui.c), com botões, formulários
 * e o Gantt desenhado na janela: escolher o arquivo, revisar/editar a
 * configuração, rodar passo a passo (avançar, voltar, ir para, editar
 * tarefas, trocar algoritmo/quantum/CPUs) ou a execução completa, e
 * exportar o SVG ao final (req. 2.4).
 *
 * Como a tela é feita ("modo imediato"): a cada evento a tela inteira é
 * redesenhada do zero a partir do estado atual. Cada botão desenhado
 * registra o seu retângulo numa lista (`alvos`); um clique procura nessa
 * lista qual botão estava embaixo do mouse. Assim não existe "objeto
 * botão" para manter sincronizado com o estado: o desenho sempre reflete o
 * estado do momento.
 *
 * Mapa do arquivo:
 *   1. infraestrutura: cores, botões, espera de eventos, textos, listas de linhas;
 *   2. formulários (caixas de texto), escolha de algoritmo e janelas de texto;
 *   3. tela de escolha do arquivo;
 *   4. tela de configuração;
 *   5. tela de simulação (passo a passo e completa);
 *   6. entrada: ui_gui_iniciar / ui_gui_executar.
 * Como no terminal, a interface NÃO calcula a simulação: só chama sim_*,
 * edicao_* e gantt_*.
 */
/* opendir/stat (navegador de arquivos) são POSIX; existem também no MinGW. */
#define _POSIX_C_SOURCE 200809L

#include "edicao.h"
#include "gantt.h"
#include "gfx.h"
#include "tela.h"
#include "ui.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ================================================== 1. infraestrutura */

#define C_FUNDO     0xF3F4F6
#define C_PAINEL    0xFFFFFF
#define C_BORDA     0xD1D5DB
#define C_TEXTO     0x1F2937
#define C_FRACO     0x6B7280
#define C_AZUL      0x2563EB
#define C_AZUL_ESC  0x1D4ED8
#define C_HOVER     0xEEF2FF
#define C_ERRO      0xB91C1C
#define C_OK        0x15803D
#define C_AVISO     0xA16207
#define C_CABEC     0x1F2937
#define C_SEL       0xDBEAFE

#define ALT_BOTAO 28
#define LINHA 18          /* altura de uma linha de texto nos painéis */

static Tela T;                       /* a imagem da janela              */
static int mx = -1, my = -1;         /* posição do mouse                */
static bool fechar;                  /* usuário fechou a janela         */
static bool arrastando;              /* arrastando algo com o mouse     */

/* Retângulos clicáveis do quadro atual. */
typedef struct { int x, y, w, h, id; } Alvo;
static Alvo alvos[256];
static int nalvos;

/* Áreas que rolam com a roda do mouse no quadro atual. */
typedef struct { int x, y, w, h; int *rol; int max; } AreaRolagem;
static AreaRolagem rolagens[8];
static int nrolagens;

enum { B_NORMAL, B_PRIMARIO, B_PERIGO };

static bool dentro(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void novo_quadro(void) {
    nalvos = 0;
    nrolagens = 0;
    tela_sem_recorte(&T);
    tela_ret(&T, 0, 0, T.w, T.h, C_FUNDO);
}

static void registrar(int x, int y, int w, int h, int id) {
    if (nalvos < (int)(sizeof alvos / sizeof alvos[0])) alvos[nalvos++] = (Alvo){x, y, w, h, id};
}

/* O alvo desenhado por último fica por cima; por isso a busca é de trás
 * para a frente. 0 = nenhum. */
static int alvo_em(int x, int y) {
    for (int i = nalvos - 1; i >= 0; i--)
        if (dentro(x, y, alvos[i].x, alvos[i].y, alvos[i].w, alvos[i].h)) return alvos[i].id;
    return 0;
}

static int largura_botao(const char *rot) { return texto_largura(rot) + 24; }

/* Desenha um botão e o registra como clicável (se ativo). w <= 0 = largura
 * do texto. Devolve a largura. */
static int botao(int x, int y, int w, const char *rot, int id, bool ativo, int estilo) {
    if (w <= 0) w = largura_botao(rot);
    bool sobre = ativo && dentro(mx, my, x, y, w, ALT_BOTAO);
    uint32_t fundo = C_PAINEL, borda = C_BORDA, cor = C_TEXTO;
    if (!ativo) {
        fundo = 0xF3F4F6;
        cor = 0xA3A3A3;
    } else if (estilo == B_PRIMARIO) {
        fundo = sobre ? C_AZUL_ESC : C_AZUL;
        borda = C_AZUL_ESC;
        cor = 0xFFFFFF;
    } else {
        if (sobre) fundo = C_HOVER;
        if (estilo == B_PERIGO) cor = C_ERRO;
    }
    tela_ret(&T, x, y, w, ALT_BOTAO, fundo);
    tela_moldura(&T, x, y, w, ALT_BOTAO, sobre && estilo != B_PRIMARIO ? C_AZUL : borda);
    tela_texto(&T, x + (w - texto_largura(rot)) / 2, y + 6, rot, cor);
    if (ativo) registrar(x, y, w, ALT_BOTAO, id);
    return w;
}

/* Botões em sequência, quebrando linha quando não cabem (barra de
 * ferramentas que se ajusta à largura da janela). */
typedef struct { int x0, w, x, y; } Fluxo;

static void fluxo_botao(Fluxo *f, const char *rot, int id, bool ativo, int estilo) {
    int w = largura_botao(rot);
    if (f->x + w > f->x0 + f->w && f->x > f->x0) {
        f->x = f->x0;
        f->y += ALT_BOTAO + 6;
    }
    f->x += botao(f->x, f->y, w, rot, id, ativo, estilo) + 6;
}

static void fluxo_espaco(Fluxo *f) { f->x += 12; }

static void apresentar(void) { gfx_mostrar(T.pix, T.w, T.h); }

/* Mostra o quadro e espera o próximo evento que exige redesenhar.
 * Movimentos do mouse só contam se mudarem o botão sob o cursor (efeito
 * "hover") ou durante um arrasto; assim não redesenhamos à toa. */
static void esperar(GfxEvento *ev) {
    apresentar();
    int antes = alvo_em(mx, my);
    for (;;) {
        if (!gfx_esperar(ev, -1)) continue;
        switch (ev->tipo) {
        case GE_FECHAR:
            fechar = true;
            return;
        case GE_EXPOR:
            apresentar();
            continue;
        case GE_TAMANHO:
            if (ev->w == T.w && ev->h == T.h) continue;
            tela_redimensionar(&T, ev->w, ev->h);
            return;
        case GE_MOVEU:
            mx = ev->x;
            my = ev->y;
            if (arrastando || alvo_em(mx, my) != antes) return;
            continue;
        case GE_CLIQUE:
        case GE_SOLTOU:
        case GE_RODA:
            mx = ev->x;
            my = ev->y;
            return;
        default:
            return;
        }
    }
}

/* Roda do mouse sobre uma área rolável registrada: rola e devolve true. */
static bool rolar(const GfxEvento *ev, int passo) {
    for (int i = nrolagens - 1; i >= 0; i--) {
        AreaRolagem *a = &rolagens[i];
        if (!dentro(ev->x, ev->y, a->x, a->y, a->w, a->h)) continue;
        *a->rol -= ev->delta * passo;
        if (*a->rol > a->max) *a->rol = a->max;
        if (*a->rol < 0) *a->rol = 0;
        return true;
    }
    return false;
}

static void area_rolagem(int x, int y, int w, int h, int *rol, int max) {
    if (max < 0) max = 0;
    if (*rol > max) *rol = max;
    if (nrolagens < (int)(sizeof rolagens / sizeof rolagens[0])) rolagens[nrolagens++] = (AreaRolagem){x, y, w, h, rol, max};
}

/* ---- textos ---- */

static void texto_cortado(int x, int y, int w, const char *s, uint32_t cor) {
    char buf[1024];
    int n = texto_bytes_ate(s, w);
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, s, (size_t)n);
    buf[n] = '\0';
    tela_texto(&T, x, y, buf, cor);
}

/* Quebra o texto em linhas de até `w` pixels, cortando nos espaços.
 * Devolve a altura usada; com desenhar = false só mede. */
static int texto_quebrado(int x, int y, int w, const char *s, uint32_t cor, bool desenhar) {
    int linhas = 0;
    int cabe = w / FONTE_L;
    if (cabe < 8) cabe = 8;
    while (*s) {
        /* conta caracteres até o limite, lembrando o último espaço */
        const char *p = s, *corte = NULL;
        int n = 0;
        while (*p && n < cabe) {
            if (*p == ' ') corte = p;
            utf8_proximo(&p);
            n++;
        }
        if (!*p) corte = p;
        else if (!corte || corte == s) corte = p;
        if (desenhar) {
            char buf[1024];
            size_t l = (size_t)(corte - s) < sizeof buf - 1 ? (size_t)(corte - s) : sizeof buf - 1;
            memcpy(buf, s, l);
            buf[l] = '\0';
            tela_texto(&T, x, y + linhas * LINHA, buf, cor);
        }
        linhas++;
        s = corte;
        while (*s == ' ') s++;
    }
    return linhas * LINHA;
}

static void painel(int x, int y, int w, int h) {
    tela_ret(&T, x, y, w, h, C_PAINEL);
    tela_moldura(&T, x, y, w, h, C_BORDA);
}

static void titulo_painel(int x, int y, const char *s) { tela_texto_negrito(&T, x + 12, y + 10, s, C_TEXTO); }

/* Faixa escura no alto da janela com título e subtítulo. */
static int cabecalho(const char *titulo, const char *sub) {
    tela_ret(&T, 0, 0, T.w, 44, C_CABEC);
    int w = tela_texto_negrito(&T, 16, 14, titulo, 0xFFFFFF);
    if (sub && *sub) texto_cortado(16 + w + 16, 14, T.w - w - 48, sub, 0xC7CDD6);
    return 44;
}

/* Mensagem de resultado do último comando: vermelha se começa com "erro". */
static int linha_mensagem(int x, int y, int w, const char *msg) {
    if (!*msg) return 0;
    bool erro = !strncmp(msg, "erro", 4);
    int h = texto_quebrado(x + 12, y + 6, w - 24, msg, erro ? C_ERRO : C_OK, false) + 12;
    tela_ret(&T, x, y, w, h, erro ? 0xFEF2F2 : 0xF0FDF4);
    tela_moldura(&T, x, y, w, h, erro ? 0xFCA5A5 : 0x86EFAC);
    texto_quebrado(x + 12, y + 6, w - 24, msg, erro ? C_ERRO : C_OK, true);
    return h;
}

static void fmsg(char *dst, size_t tam, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, tam, fmt, ap);
    va_end(ap);
}

/* ---- listas de linhas (painéis de detalhes, estatísticas, ajuda) ---- */

typedef struct {
    char txt[256];
    uint32_t cor;
    bool negrito;
    long amostra;     /* cor de um quadradinho antes do texto; -1 = nenhum,
                         -2 = só o recuo (continuação de uma linha com amostra) */
} Linha;

typedef struct { Linha *v; int n, cap; } Linhas;

static void lin_add(Linhas *L, uint32_t cor, bool negrito, const char *fmt, ...) {
    if (L->n == L->cap) {
        L->cap = L->cap ? L->cap * 2 : 32;
        L->v = xrealloc(L->v, (size_t)L->cap * sizeof(Linha));
    }
    Linha *l = &L->v[L->n++];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l->txt, sizeof l->txt, fmt, ap);
    va_end(ap);
    l->cor = cor;
    l->negrito = negrito;
    l->amostra = -1;
}

static void lin_liberar(Linhas *L) {
    free(L->v);
    L->v = NULL;
    L->n = L->cap = 0;
}

/* Quebra as linhas que não cabem em `w` pixels (nos espaços), gerando
 * linhas de continuação com a mesma cor e recuo. */
static void quebrar_linhas(const Linhas *L, int w, Linhas *out) {
    for (int i = 0; i < L->n; i++) {
        const Linha *l = &L->v[i];
        int cabe = (w - (l->amostra != -1 ? 20 : 0) - 6) / FONTE_L;
        if (cabe < 10) cabe = 10;
        const char *s = l->txt;
        bool primeira = true;
        do {
            const char *p = s, *corte = NULL;
            int n = 0;
            while (*p && n < cabe) {
                if (*p == ' ') corte = p;
                utf8_proximo(&p);
                n++;
            }
            if (!*p || !corte || corte == s) corte = p;
            lin_add(out, l->cor, l->negrito, "%.*s", (int)(corte - s), s);
            out->v[out->n - 1].amostra = primeira ? l->amostra : (l->amostra != -1 ? -2 : -1);
            primeira = false;
            s = corte;
            while (*s == ' ') s++;
        } while (*s);
    }
}

/* Desenha as linhas num retângulo, a partir da linha `*rol`, e registra a
 * área para a roda do mouse rolar. */
static void desenhar_linhas(int x, int y, int w, int h, const Linhas *L0, int *rol) {
    Linhas Q = {0};
    quebrar_linhas(L0, w, &Q);
    const Linhas *L = &Q;
    int cabem = h / LINHA;
    area_rolagem(x, y, w, h, rol, L->n - cabem);
    tela_recorte(&T, x, y, w, h);
    for (int i = *rol, j = 0; i < L->n && j < cabem; i++, j++) {
        const Linha *l = &L->v[i];
        int xx = x;
        if (l->amostra >= 0) {
            tela_ret(&T, xx, y + j * LINHA + 3, 14, 12, (uint32_t)l->amostra);
            tela_moldura(&T, xx, y + j * LINHA + 3, 14, 12, 0x555555);
        }
        if (l->amostra != -1) xx += 20;
        if (l->negrito) tela_texto_negrito(&T, xx, y + j * LINHA, l->txt, l->cor);
        else tela_texto(&T, xx, y + j * LINHA, l->txt, l->cor);
    }
    tela_sem_recorte(&T);
    if (L->n > cabem) {   /* indicador de que há mais linhas */
        int alt = h * cabem / L->n;
        if (alt < 16) alt = 16;
        int pos = (h - alt) * *rol / (L->n - cabem);
        tela_ret(&T, x + w - 4, y + pos, 3, alt, 0xB0B6BF);
    }
    lin_liberar(&Q);
}

/* Quantas linhas `L` ocupa depois de quebrada na largura `w`. */
static int linhas_visuais(const Linhas *L, int w) {
    Linhas Q = {0};
    quebrar_linhas(L, w, &Q);
    int n = Q.n;
    lin_liberar(&Q);
    return n;
}

/* ---- fundo das janelas de diálogo ---- */

static uint32_t *fundo_modal;
static int fundo_w, fundo_h;

/* Guarda a tela atual escurecida, para desenhar o diálogo por cima. */
static void capturar_fundo(void) {
    free(fundo_modal);
    fundo_modal = xmalloc((size_t)T.w * T.h * sizeof(uint32_t));
    tela_escurecer(&T);
    memcpy(fundo_modal, T.pix, (size_t)T.w * T.h * sizeof(uint32_t));
    fundo_w = T.w;
    fundo_h = T.h;
}

static void desenhar_fundo(void) {
    nalvos = 0;
    nrolagens = 0;
    tela_sem_recorte(&T);
    if (fundo_modal && fundo_w == T.w && fundo_h == T.h)
        memcpy(T.pix, fundo_modal, (size_t)T.w * T.h * sizeof(uint32_t));
    else
        tela_ret(&T, 0, 0, T.w, T.h, 0x50555C);   /* janela mudou de tamanho */
}

/* Caixa do diálogo centralizada; devolve x e y do conteúdo. */
static void caixa_dialogo(int w, int h, const char *titulo, int *x, int *y) {
    if (w > T.w - 24) w = T.w - 24;
    if (h > T.h - 24) h = T.h - 24;
    *x = (T.w - w) / 2;
    *y = (T.h - h) / 2;
    tela_ret(&T, *x + 4, *y + 4, w, h, 0x202020);   /* sombra */
    painel(*x, *y, w, h);
    tela_ret(&T, *x, *y, w, 34, C_CABEC);
    texto_cortado(*x + 14, *y + 9, w - 28, titulo, 0xFFFFFF);
}

/* ======================================== 2. formulários e diálogos */

typedef struct {
    const char *rotulo;
    char valor[512];
    bool bloqueado;     /* só leitura (ex.: ingresso depois da chegada) */
} Campo;

typedef struct { const char *rot; int id; int estilo; } BotaoExtra;

/* Valida os campos; devolve NULL se ok ou o motivo (e o campo com problema
 * em *campo_erro). */
typedef const char *(*ValidaForm)(Campo *c, int n, void *ctx, int *campo_erro);

enum { FORM_CANCELAR = 0, FORM_OK = 1, FORM_CAMPO = 1000 };

/* Edição de texto num campo: insere/apaga caracteres UTF-8 na posição do
 * cursor (em bytes). `tudo` = valor inteiro selecionado (digitar substitui). */
static void editar_campo(Campo *c, int *cursor, bool *tudo, const GfxEvento *ev) {
    size_t len = strlen(c->valor);
    int t = ev->tecla;
    if (*tudo && (t == GK_BACKSPACE || t == GK_DELETE || t < GK_ENTER)) {
        c->valor[0] = '\0';
        *cursor = 0;
        len = 0;
        *tudo = false;
        if (t == GK_BACKSPACE || t == GK_DELETE) return;
    }
    *tudo = false;
    if (t == GK_BACKSPACE) {
        if (*cursor == 0) return;
        int ini = *cursor - 1;
        while (ini > 0 && (c->valor[ini] & 0xC0) == 0x80) ini--;   /* início do caractere */
        memmove(c->valor + ini, c->valor + *cursor, len - (size_t)*cursor + 1);
        *cursor = ini;
    } else if (t == GK_DELETE) {
        if ((size_t)*cursor >= len) return;
        const char *p = c->valor + *cursor;
        utf8_proximo(&p);
        memmove(c->valor + *cursor, p, strlen(p) + 1);
    } else if (t == GK_ESQ) {
        while (*cursor > 0) {
            (*cursor)--;
            if ((c->valor[*cursor] & 0xC0) != 0x80) break;
        }
    } else if (t == GK_DIR) {
        if ((size_t)*cursor < len) {
            const char *p = c->valor + *cursor;
            utf8_proximo(&p);
            *cursor = (int)(p - c->valor);
        }
    } else if (t == GK_INICIO) {
        *cursor = 0;
    } else if (t == GK_FIM) {
        *cursor = (int)len;
    } else if (t < GK_ENTER) {
        char u[4];
        int n = utf8_escrever(u, (unsigned)t);
        if (len + (size_t)n >= sizeof c->valor) return;
        memmove(c->valor + *cursor + n, c->valor + *cursor, len - (size_t)*cursor + 1);
        memcpy(c->valor + *cursor, u, (size_t)n);
        *cursor += n;
    }
}

/* Desenha uma caixa de texto. `foco`: desenha o cursor. */
static void caixa_texto(int x, int y, int w, const Campo *c, bool foco, int cursor, bool tudo, bool erro) {
    tela_ret(&T, x, y, w, ALT_BOTAO, c->bloqueado ? 0xF3F4F6 : 0xFFFFFF);
    tela_moldura(&T, x, y, w, ALT_BOTAO, erro ? C_ERRO : foco ? C_AZUL : C_BORDA);
    if (foco) tela_moldura(&T, x + 1, y + 1, w - 2, ALT_BOTAO - 2, erro ? C_ERRO : C_AZUL);
    /* se o texto não cabe, mostra o final (onde normalmente está o cursor) */
    const char *v = c->valor;
    int area = w - 16;
    while (texto_largura(v) > area && *v) utf8_proximo(&v);
    int px = x + 8;
    if (foco && tudo && *c->valor) tela_ret(&T, px - 1, y + 5, texto_largura(v) + 2, 18, C_SEL);
    tela_texto(&T, px, y + 6, v, c->bloqueado ? 0x9CA3AF : C_TEXTO);
    if (foco && !tudo) {
        int desloc = (int)(v - c->valor);
        char antes[512];
        int n = cursor - desloc;
        if (n < 0) n = 0;
        memcpy(antes, v, (size_t)n);
        antes[n] = '\0';
        tela_ret(&T, px + texto_largura(antes), y + 5, 2, 18, C_TEXTO);
    }
}

/* Converte um campo em inteiro; mensagem de erro com o nome do campo. */
static const char *campo_int(const Campo *c, int *out) {
    static char buf[300];
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", c->valor);
    char *v = str_trim(tmp);
    if (!*v) {
        snprintf(buf, sizeof buf, "%s: informe um valor", c->rotulo);
        return buf;
    }
    int r = parse_int(v, out);
    if (r == -1) snprintf(buf, sizeof buf, "%s: '%s' não é um número inteiro", c->rotulo, v);
    else if (r == -2) snprintf(buf, sizeof buf, "%s: '%s' é grande demais", c->rotulo, v);
    return r ? buf : NULL;
}

/* Junta "rótulo: motivo" num buffer estático (para mensagens de validação). */
static const char *com_rotulo(const Campo *c, const char *motivo) {
    static char buf[400];
    if (!motivo) return NULL;
    snprintf(buf, sizeof buf, "%s: %s", c->rotulo, motivo);
    return buf;
}

/* Diálogo com campos de texto. Enter/OK valida; se algo estiver errado,
 * mostra o motivo em vermelho e o campo com problema fica em destaque.
 * Devolve FORM_OK, FORM_CANCELAR ou o id de um botão extra (sem validar). */
static int formulario(const char *titulo, const Linhas *intro, Campo *c, int n, ValidaForm val, void *ctx,
                      const BotaoExtra *extras, int nextras) {
    capturar_fundo();
    int foco = 0;
    while (foco < n && c[foco].bloqueado) foco++;
    int cursor = foco < n ? (int)strlen(c[foco].valor) : 0;
    bool tudo = true;
    char erro[512] = "";
    int campo_erro = -1;
    int rol = 0;
    for (;;) {
        desenhar_fundo();
        int w = 660 > T.w - 24 ? T.w - 24 : 660;
        int nintro = intro ? linhas_visuais(intro, w - 32) : 0;
        int h_intro = nintro ? (nintro < 9 ? nintro : 9) * LINHA + 10 : 0;
        int h_erro = *erro ? texto_quebrado(0, 0, w - 48, erro, 0, false) + 8 : 0;
        int h = 34 + 16 + h_intro + n * 38 + h_erro + 16 + ALT_BOTAO + 16;
        int x, y;
        caixa_dialogo(w, h, titulo, &x, &y);
        int cy = y + 34 + 14;
        if (nintro) {
            desenhar_linhas(x + 16, cy, w - 32, h_intro - 10, intro, &rol);
            cy += h_intro;
        }
        int xl = x + 16, xc = x + 230, wc = w - 230 - 16;
        for (int i = 0; i < n; i++) {
            tela_texto(&T, xl, cy + 6, c[i].rotulo, c[i].bloqueado ? 0x9CA3AF : C_TEXTO);
            caixa_texto(xc, cy, wc, &c[i], i == foco, cursor, tudo, i == campo_erro);
            if (!c[i].bloqueado) registrar(xc, cy, wc, ALT_BOTAO, FORM_CAMPO + i);
            cy += 38;
        }
        if (*erro) {
            texto_quebrado(x + 24, cy + 2, w - 48, erro, C_ERRO, true);
            cy += h_erro;
        }
        int by = y + h - ALT_BOTAO - 14;
        int bx = x + 16;
        for (int i = 0; i < nextras; i++) bx += botao(bx, by, 0, extras[i].rot, extras[i].id, true, extras[i].estilo) + 8;
        int wok = largura_botao("OK (Enter)"), wca = largura_botao("Cancelar (Esc)");
        botao(x + w - 16 - wok, by, wok, "OK (Enter)", FORM_OK + 10000, true, B_PRIMARIO);
        botao(x + w - 16 - wok - 8 - wca, by, wca, "Cancelar (Esc)", FORM_CANCELAR + 10000, true, B_NORMAL);

        GfxEvento ev;
        esperar(&ev);
        if (fechar) return FORM_CANCELAR;
        int acao = -1;
        if (ev.tipo == GE_CLIQUE) {
            int id = alvo_em(ev.x, ev.y);
            if (id >= FORM_CAMPO && id < FORM_CAMPO + n) {
                foco = id - FORM_CAMPO;
                tudo = false;
                /* cursor no caractere mais próximo do clique */
                cursor = (int)strlen(c[foco].valor);
                int rel = ev.x - (xc + 8);
                if (texto_largura(c[foco].valor) <= wc - 16 && rel >= 0)
                    cursor = texto_bytes_ate(c[foco].valor, rel + FONTE_L / 2);
            } else if (id == FORM_OK + 10000) {
                acao = FORM_OK;
            } else if (id == FORM_CANCELAR + 10000) {
                acao = FORM_CANCELAR;
            } else if (id > 0) {
                return id;   /* botão extra */
            }
        } else if (ev.tipo == GE_RODA) {
            rolar(&ev, 3);
        } else if (ev.tipo == GE_TECLA) {
            if (ev.tecla == GK_ESC) acao = FORM_CANCELAR;
            else if (ev.tecla == GK_ENTER) acao = FORM_OK;
            else if (ev.tecla == GK_TAB || ev.tecla == GK_BAIXO || ev.tecla == GK_CIMA) {
                int d = (ev.tecla == GK_CIMA || (ev.mod & GM_SHIFT)) ? -1 : 1;
                for (int k = 0; k < n; k++) {
                    foco = (foco + d + n) % n;
                    if (!c[foco].bloqueado) break;
                }
                cursor = (int)strlen(c[foco].valor);
                tudo = true;
            } else if (foco < n && !c[foco].bloqueado) {
                editar_campo(&c[foco], &cursor, &tudo, &ev);
            }
        }
        if (acao == FORM_CANCELAR) return FORM_CANCELAR;
        if (acao == FORM_OK) {
            int ce = -1;
            const char *m = val ? val(c, n, ctx, &ce) : NULL;
            if (!m) return FORM_OK;
            snprintf(erro, sizeof erro, "erro: %s", m);
            campo_erro = ce;
            if (ce >= 0 && ce < n) {
                foco = ce;
                cursor = (int)strlen(c[ce].valor);
                tudo = true;
            }
        }
    }
}

/* ---- pedir um número ---- */

typedef const char *(*RegraInt)(int v);

typedef struct { RegraInt regra; int valor; } CtxNumero;

static const char *val_numero(Campo *c, int n, void *ctx, int *ce) {
    (void)n;
    CtxNumero *k = ctx;
    *ce = 0;
    const char *m = campo_int(&c[0], &k->valor);
    if (m) return m;
    return k->regra ? com_rotulo(&c[0], k->regra(k->valor)) : NULL;
}

static bool pedir_numero(const char *titulo, const char *explicacao, const char *rotulo, int atual,
                         RegraInt regra, int *out) {
    Campo c = {rotulo, "", false};
    snprintf(c.valor, sizeof c.valor, "%d", atual);
    Linhas intro = {0};
    if (explicacao) lin_add(&intro, C_FRACO, false, "%s", explicacao);
    CtxNumero k = {regra, atual};
    int r = formulario(titulo, &intro, &c, 1, val_numero, &k, NULL, 0);
    lin_liberar(&intro);
    if (r != FORM_OK) return false;
    *out = k.valor;
    return true;
}

static const char *regra_nao_negativo(int v) { return v >= 0 ? NULL : "o instante não pode ser negativo"; }

/* ---- escolher o algoritmo ---- */

/* Lista os escalonadores registrados (inclusive plugins) como botões.
 * Devolve o índice escolhido ou -1. */
static int escolher_algoritmo(int atual) {
    capturar_fundo();
    for (;;) {
        desenhar_fundo();
        int nalg = sched_quantidade();
        int x, y;
        caixa_dialogo(620, 34 + 20 + nalg * 36 + 20 + ALT_BOTAO + 14, "Escolha o algoritmo de escalonamento", &x, &y);
        int w = 620 > T.w - 24 ? T.w - 24 : 620;
        int cy = y + 34 + 16;
        for (int i = 0; i < nalg; i++) {
            const Escalonador *e = sched_obter(i);
            char rot[256];
            snprintf(rot, sizeof rot, "%s - %s%s", e->nome, e->descricao, i == atual ? "  (atual)" : "");
            botao(x + 16, cy, w - 32, rot, 100 + i, true, i == atual ? B_PRIMARIO : B_NORMAL);
            cy += 36;
        }
        botao(x + w - 16 - largura_botao("Cancelar (Esc)"), y + 34 + 20 + nalg * 36 + 6, 0, "Cancelar (Esc)", 1, true, B_NORMAL);
        GfxEvento ev;
        esperar(&ev);
        if (fechar) return -1;
        if (ev.tipo == GE_TECLA && ev.tecla == GK_ESC) return -1;
        if (ev.tipo == GE_CLIQUE) {
            int id = alvo_em(ev.x, ev.y);
            if (id == 1) return -1;
            if (id >= 100 && id < 100 + nalg) return id - 100;
        }
    }
}

/* ---- janela só de leitura (estatísticas, ajuda) ---- */

static void janela_texto(const char *titulo, const Linhas *L) {
    capturar_fundo();
    int rol = 0;
    for (;;) {
        desenhar_fundo();
        int w = 900 > T.w - 24 ? T.w - 24 : 900, h = T.h - 80;
        int nl = linhas_visuais(L, w - 32);
        if (h > nl * LINHA + 34 + 24 + ALT_BOTAO + 24) h = nl * LINHA + 34 + 24 + ALT_BOTAO + 24;
        int x, y;
        caixa_dialogo(w, h, titulo, &x, &y);
        w = w > T.w - 24 ? T.w - 24 : w;
        h = h > T.h - 24 ? T.h - 24 : h;
        desenhar_linhas(x + 16, y + 34 + 12, w - 32, h - 34 - 12 - ALT_BOTAO - 24, L, &rol);
        int wf = largura_botao("Fechar (Esc)");
        botao(x + w - 16 - wf, y + h - ALT_BOTAO - 12, wf, "Fechar (Esc)", 1, true, B_PRIMARIO);
        GfxEvento ev;
        esperar(&ev);
        if (fechar) return;
        if (ev.tipo == GE_TECLA && (ev.tecla == GK_ESC || ev.tecla == GK_ENTER)) return;
        if (ev.tipo == GE_TECLA && (ev.tecla == GK_BAIXO || ev.tecla == GK_PGBAIXO)) rol += ev.tecla == GK_BAIXO ? 1 : 10;
        if (ev.tipo == GE_TECLA && (ev.tecla == GK_CIMA || ev.tecla == GK_PGCIMA)) rol -= ev.tecla == GK_CIMA ? 1 : 10;
        if (rol < 0) rol = 0;
        if (ev.tipo == GE_RODA) rolar(&ev, 3);
        if (ev.tipo == GE_CLIQUE && alvo_em(ev.x, ev.y) == 1) return;
    }
}

/* ---- formulário de tarefa (configuração e "+ tarefa" na simulação) ---- */

typedef struct {
    const int *ids;
    int nids, proprio;   /* proprio = índice da tarefa editada, -1 = nova */
    int min_ingresso;
    TarefaCfg res;       /* resultado já validado */
    int prazo_antigo, periodo_antigo;
} CtxTarefa;

enum { CT_ID, CT_COR, CT_INGRESSO, CT_DURACAO, CT_PERIODO, CT_PRAZO, CT_EVENTOS, CT_N };

/* Mesmas regras do terminal (pedir_tarefa em ui.c). */
static const char *val_tarefa(Campo *c, int n, void *ctx, int *ce) {
    (void)n;
    CtxTarefa *k = ctx;
    TarefaCfg *t = &k->res;
    const char *m;
    if ((m = campo_int(&c[CT_ID], &t->id))) { *ce = CT_ID; return m; }
    if ((m = valida_id(t->id, k->ids, k->nids, k->proprio))) { *ce = CT_ID; return com_rotulo(&c[CT_ID], m); }
    char cor[64];
    snprintf(cor, sizeof cor, "%.63s", c[CT_COR].valor);
    if (!parse_cor(str_trim(cor), &t->cor)) {
        *ce = CT_COR;
        return "cor: use 6 dígitos hexadecimais, ex.: F0E0D0";
    }
    if ((m = campo_int(&c[CT_INGRESSO], &t->ingresso))) { *ce = CT_INGRESSO; return m; }
    if ((m = valida_ingresso(t->ingresso, k->min_ingresso))) { *ce = CT_INGRESSO; return com_rotulo(&c[CT_INGRESSO], m); }
    if ((m = campo_int(&c[CT_DURACAO], &t->duracao))) { *ce = CT_DURACAO; return m; }
    if ((m = valida_positivo(t->duracao))) { *ce = CT_DURACAO; return com_rotulo(&c[CT_DURACAO], m); }
    if ((m = campo_int(&c[CT_PERIODO], &t->periodo))) { *ce = CT_PERIODO; return m; }
    if ((m = valida_periodo(t->periodo))) { *ce = CT_PERIODO; return com_rotulo(&c[CT_PERIODO], m); }
    if ((m = campo_int(&c[CT_PRAZO], &t->prazo))) { *ce = CT_PRAZO; return m; }
    /* Se o prazo acompanhava o período (prazo implícito) e o usuário só
     * mexeu no período, o prazo continua acompanhando (igual ao terminal). */
    if (t->prazo == k->prazo_antigo && k->prazo_antigo == k->periodo_antigo && t->periodo != k->periodo_antigo) {
        t->prazo = t->periodo;
        snprintf(c[CT_PRAZO].valor, sizeof c[CT_PRAZO].valor, "%d", t->prazo);
    }
    if ((m = valida_positivo(t->prazo))) { *ce = CT_PRAZO; return com_rotulo(&c[CT_PRAZO], m); }
    return NULL;
}

/* Pede os dados de uma tarefa (nova ou editada a partir de `t`). Em caso de
 * OK, preenche *t; os eventos (texto) vão para `eventos`. */
static bool form_tarefa(const char *titulo, TarefaCfg *t, const int *ids, int nids, int proprio, int min_ingresso,
                        char *eventos, size_t tam_ev) {
    Campo c[CT_N] = {
        {"id", "", false}, {"cor (RGB hex)", "", false}, {"ingresso", "", false}, {"duração (C)", "", false},
        {"período (P)", "", false}, {"prazo (D)", "", false}, {"eventos", "", false},
    };
    snprintf(c[CT_ID].valor, sizeof c[0].valor, "%d", t->id);
    snprintf(c[CT_COR].valor, sizeof c[0].valor, "%06X", t->cor);
    snprintf(c[CT_INGRESSO].valor, sizeof c[0].valor, "%d", t->ingresso);
    snprintf(c[CT_DURACAO].valor, sizeof c[0].valor, "%d", t->duracao);
    snprintf(c[CT_PERIODO].valor, sizeof c[0].valor, "%d", t->periodo);
    snprintf(c[CT_PRAZO].valor, sizeof c[0].valor, "%d", t->prazo);
    snprintf(c[CT_EVENTOS].valor, sizeof c[0].valor, "%s", t->eventos ? t->eventos : "");
    CtxTarefa k = {ids, nids, proprio, min_ingresso, *t, t->prazo, t->periodo};
    Linhas intro = {0};
    lin_add(&intro, C_FRACO, false, "Os valores sugeridos já vêm preenchidos; Tab passa para o próximo campo.");
    if (min_ingresso > 0) lin_add(&intro, C_FRACO, false, "A simulação está no instante %d: o ingresso deve ser >= %d.", min_ingresso, min_ingresso);
    int r = formulario(titulo, &intro, c, CT_N, val_tarefa, &k, NULL, 0);
    lin_liberar(&intro);
    if (r != FORM_OK) return false;
    char ev[512];
    snprintf(ev, sizeof ev, "%s", c[CT_EVENTOS].valor);
    snprintf(eventos, tam_ev, "%s", str_trim(ev));
    k.res.eventos = eventos;
    *t = k.res;
    return true;
}

static int proximo_id(const int *ids, int n) {
    int m = 0;
    for (int i = 0; i < n; i++)
        if (ids[i] >= m) m = ids[i] + 1;
    return m;
}

/* Texto da utilização e do teste de escalonabilidade (como no terminal). */
static void linhas_utilizacao(Linhas *L, int n, double u, int alg) {
    const Escalonador *e = sched_obter(alg);
    if (e && str_ieq(e->nome, "RM")) {
        double lim = sim_limite_rm(n);
        lin_add(L, C_TEXTO, false, "Utilização U = soma(C/P) = %.3f | teste suficiente do RM: U <= n(2^(1/n)-1) = %.3f -> %s",
                u, lim, u <= lim ? "escalonável (1 CPU)" : "inconclusivo (pode perder prazos)");
    } else if (e && str_ieq(e->nome, "EDF")) {
        lin_add(L, C_TEXTO, false, "Utilização U = soma(C/P) = %.3f | EDF (prazo = período): escalonável em 1 CPU sse U <= 1 -> %s",
                u, u <= 1.0 ? "sim" : "não");
    } else {
        lin_add(L, C_TEXTO, false, "Utilização U = soma(C/P) = %.3f", u);
    }
    lin_add(L, C_FRACO, false, "(os testes valem para 1 CPU; com mais CPUs são só uma referência)");
}

/* ============================================ 3. escolha do arquivo */

/* Um item da lista. `rotulo` (opcional) é o que aparece no lugar do nome,
 * ex.: "E:  (pendrive / removível)" para uma unidade do Windows. */
typedef struct { char nome[256]; char rotulo[64]; bool pasta; } Entrada;

/* O navegador trabalha com caminhos absolutos e '/' como separador (o
 * Windows também aceita '/'). O caminho vazio "" é a lista de unidades do
 * Windows (C:, D:, pendrives). */

static void barras_normais(char *p) {
    for (; *p; p++)
        if (*p == '\\') *p = '/';
}

/* dir + "/" + nome, sem duplicar a barra quando dir já termina nela ("/",
 * "E:/"); vindo da lista de unidades, "E:" vira "E:/". */
static void juntar(char *dst, size_t tam, const char *dir, const char *nome) {
    size_t l = strlen(dir);
    if (!l) snprintf(dst, tam, "%s/", nome);
    else if (dir[l - 1] == '/') snprintf(dst, tam, "%s%s", dir, nome);
    else snprintf(dst, tam, "%s/%s", dir, nome);
}

/* Pasta onde o programa foi aberto, como caminho absoluto. */
static void pasta_atual(char *dst, size_t tam) {
    if (!getcwd(dst, tam)) snprintf(dst, tam, ".");
    barras_normais(dst);
}

static void pasta_pessoal(char *dst, size_t tam) {
    const char *h = getenv("HOME");
    if (!h || !*h) h = getenv("USERPROFILE");   /* Windows */
    if (h && *h) {
        snprintf(dst, tam, "%s", h);
        barras_normais(dst);
    } else {
        pasta_atual(dst, tam);
    }
}

static bool e_pasta(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Onde os pendrives aparecem em cada sistema:
 *   Windows: como unidades (E:, F:...) -> lista de unidades ("");
 *   macOS:   /Volumes;
 *   Linux:   /media/<usuário> (Ubuntu, Debian, Mint), /run/media/<usuário>
 *            (Fedora, Arch) ou, em sistemas sem montagem automática,
 *            /media e /mnt. */
static void pasta_pendrives(char *dst, size_t tam) {
#if defined(_WIN32)
    snprintf(dst, tam, "%s", "");
#elif defined(__APPLE__)
    snprintf(dst, tam, "%s", "/Volumes");
#else
    const char *u = getenv("USER");
    if (u && *u) {
        snprintf(dst, tam, "/media/%s", u);
        if (e_pasta(dst)) return;
        snprintf(dst, tam, "/run/media/%s", u);
        if (e_pasta(dst)) return;
    }
    snprintf(dst, tam, "%s", e_pasta("/media") ? "/media" : "/mnt");
#endif
}

/* Ordem: pastas primeiro, depois arquivos, cada grupo em ordem alfabética
 * sem diferenciar maiúsculas. */
static int cmp_entrada(const void *a, const void *b) {
    const Entrada *x = a, *y = b;
    if (x->pasta != y->pasta) return x->pasta ? -1 : 1;
    for (int i = 0;; i++) {
        int cx = tolower((unsigned char)x->nome[i]), cy = tolower((unsigned char)y->nome[i]);
        if (cx != cy || !cx) return cx - cy;
    }
}

static Entrada *listar_pasta(const char *dir, int *n) {
    *n = 0;
    if (!*dir) {   /* lista de unidades (Windows) */
        GfxUnidade u[26];
        int nu = gfx_unidades(u, 26);
        Entrada *v = xcalloc((size_t)(nu ? nu : 1), sizeof(Entrada));
        for (int i = 0; i < nu; i++) {
            snprintf(v[i].nome, sizeof v[i].nome, "%.3s", u[i].raiz);
            snprintf(v[i].rotulo, sizeof v[i].rotulo, "%.3s  (%.31s)", u[i].raiz, u[i].tipo);
            v[i].pasta = true;
        }
        *n = nu;
        return v;
    }
    DIR *d = opendir(dir);
    if (!d) return NULL;
    int cap = 64;
    Entrada *v = xmalloc((size_t)cap * sizeof(Entrada));
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;   /* ocultos, "." e ".." */
        char caminho[1300];
        juntar(caminho, sizeof caminho, dir, e->d_name);
        struct stat st;
        if (stat(caminho, &st) != 0) continue;
        if (*n == cap) {
            cap *= 2;
            v = xrealloc(v, (size_t)cap * sizeof(Entrada));
        }
        snprintf(v[*n].nome, sizeof v[*n].nome, "%s", e->d_name);
        v[*n].rotulo[0] = '\0';
        v[*n].pasta = S_ISDIR(st.st_mode);
        (*n)++;
    }
    closedir(d);
    qsort(v, (size_t)*n, sizeof(Entrada), cmp_entrada);
    return v;
}

/* Sobe uma pasta só mexendo no texto do caminho:
 *   "/home/ana" -> "/home" -> "/"      "E:/aulas" -> "E:/" -> "" (unidades) */
static void pasta_acima(char *dir, size_t tam) {
    size_t l = strlen(dir);
    if (!l || !strcmp(dir, "/")) return;                  /* já no topo */
    if (l == 3 && dir[1] == ':' && dir[2] == '/') {       /* raiz de unidade */
        dir[0] = '\0';
        return;
    }
    if (l > 1 && dir[l - 1] == '/') dir[--l] = '\0';
    char *ult = strrchr(dir, '/');
    if (!ult) {                      /* caminho relativo de um nome só */
        snprintf(dir, tam, ".");
        return;
    }
    if (ult == dir) dir[1] = '\0';                       /* "/x" -> "/"   */
    else if (ult == dir + 2 && dir[1] == ':') ult[1] = '\0';   /* "E:/x" -> "E:/" */
    else *ult = '\0';
}

/* Tela para escolher o arquivo de configuração. Clique numa pasta entra
 * nela; clique num arquivo preenche o caminho; clicar de novo no mesmo
 * arquivo (ou Enter / Carregar) carrega. Erros do arquivo aparecem todos
 * de uma vez, com o motivo (req. 3.3). Devolve true se carregou. */
static bool tela_arquivo(Config *cfg, bool pode_voltar, const Config *falhou) {
    enum { A_CARREGAR = 1, A_VOLTAR, A_ACIMA, A_CAMPO, A_PENDRIVES, A_PESSOAL, A_ATUAL, A_ITEM = 1000 };
    char dir[1024];
    char atual[1024];
    pasta_atual(atual, sizeof atual);
    if (e_pasta("exemplos")) juntar(dir, sizeof dir, atual, "exemplos");
    else snprintf(dir, sizeof dir, "%s", atual);
    int nent = 0;
    Entrada *ent = listar_pasta(dir, &nent);
    Campo campo = {"Caminho", "", false};
    int cursor = 0;
    bool tudo = false;
    Linhas msgs = {0};
    if (falhou) {
        snprintf(campo.valor, sizeof campo.valor, "%s", falhou->caminho ? falhou->caminho : "");
        lin_add(&msgs, C_ERRO, true, "O arquivo informado tem problemas. Corrija-o e carregue de novo, ou escolha outro.");
        for (int i = 0; i < falhou->avisos.n; i++) lin_add(&msgs, C_AVISO, false, "aviso: %s", falhou->avisos.itens[i]);
        for (int i = 0; i < falhou->erros.n; i++) lin_add(&msgs, C_ERRO, false, "erro: %s", falhou->erros.itens[i]);
    }
    cursor = (int)strlen(campo.valor);
    int rol_lista = 0, rol_msgs = 0;
    bool ok = false;

    while (!fechar) {
        novo_quadro();
        int y = cabecalho("Abrir arquivo de configuração", "Simulador de escalonamento de tempo real (RM / EDF) - Projeto A");
        int x = 16, w = T.w - 32;
        y += 12;

        /* caminho + carregar */
        painel(x, y, w, 92);
        titulo_painel(x, y, "Caminho do arquivo (absoluto ou relativo, qualquer pasta ou disco):");
        int wc = largura_botao("Carregar (Enter)");
        caixa_texto(x + 12, y + 34, w - 36 - wc, &campo, true, cursor, tudo, false);
        registrar(x + 12, y + 34, w - 36 - wc, ALT_BOTAO, A_CAMPO);
        botao(x + w - 12 - wc, y + 34, wc, "Carregar (Enter)", A_CARREGAR, true, B_PRIMARIO);
        tela_texto(&T, x + 12, y + 68, "Formato: algoritmo;quantum;qtde_cpus na 1ª linha e id;cor;ingresso;duracao;periodo;prazo;eventos nas seguintes.", C_FRACO);
        y += 92 + 12;

        /* mensagens (erros do último arquivo) */
        int h_msgs = 0;
        if (msgs.n) {
            int nv = linhas_visuais(&msgs, w - 24);
            h_msgs = (nv < 8 ? nv : 8) * LINHA + 20;
            painel(x, y, w, h_msgs);
            desenhar_linhas(x + 12, y + 10, w - 24, h_msgs - 20, &msgs, &rol_msgs);
            y += h_msgs + 12;
        }

        /* navegador de arquivos */
        int h_lista = T.h - y - ALT_BOTAO - 28;
        if (h_lista < 80) h_lista = 80;
        painel(x, y, w, h_lista);
        /* atalhos: chegar no pendrive com um clique */
        Fluxo f = {x + 12, w - 24, x + 12, y + 8};
        fluxo_botao(&f, "Pendrives e discos", A_PENDRIVES, true, B_NORMAL);
        fluxo_botao(&f, "Pasta pessoal", A_PESSOAL, true, B_NORMAL);
        fluxo_botao(&f, "Pasta do programa", A_ATUAL, true, B_NORMAL);
        fluxo_botao(&f, "Pasta acima", A_ACIMA, *dir && strcmp(dir, "/") != 0, B_NORMAL);
        char rot[1200];
        if (*dir) snprintf(rot, sizeof rot, "Pasta: %s", dir);
        else snprintf(rot, sizeof rot, "Unidades (discos e pendrives):");
        texto_cortado(x + 12, f.y + ALT_BOTAO + 10, w - 24, rot, C_TEXTO);
        tela_texto_negrito(&T, x + 12, f.y + ALT_BOTAO + 10, *dir ? "Pasta:" : "Unidades (discos e pendrives):", C_TEXTO);
        int ly = f.y + ALT_BOTAO + 36, lh = y + h_lista - 8 - ly;
        int cabem = lh / 22;
        area_rolagem(x, ly, w, lh, &rol_lista, nent - cabem);
        char pend[1024];
        pasta_pendrives(pend, sizeof pend);
        if (!ent) tela_texto(&T, x + 12, ly, "(não foi possível abrir esta pasta)", C_ERRO);
        else if (!nent && !strcmp(dir, pend))
            tela_texto(&T, x + 12, ly, "(nenhum pendrive encontrado: conecte-o e clique em \"Pendrives e discos\" de novo)", C_FRACO);
        else if (!nent) tela_texto(&T, x + 12, ly, "(pasta vazia)", C_FRACO);
        for (int i = rol_lista, j = 0; ent && i < nent && j < cabem; i++, j++) {
            int iy = ly + j * 22;
            char cam[1300];
            juntar(cam, sizeof cam, dir, ent[i].nome);
            bool escolhido = !ent[i].pasta && !strcmp(cam, campo.valor);
            bool sobre = dentro(mx, my, x + 6, iy, w - 12, 22);
            if (escolhido) tela_ret(&T, x + 6, iy, w - 12, 22, C_SEL);
            else if (sobre) tela_ret(&T, x + 6, iy, w - 12, 22, C_HOVER);
            if (ent[i].pasta) {
                tela_ret(&T, x + 14, iy + 6, 14, 10, 0xE5B85C);
                tela_ret(&T, x + 14, iy + 4, 6, 3, 0xE5B85C);
                if (*ent[i].rotulo) snprintf(cam, sizeof cam, "%s", ent[i].rotulo);
                else snprintf(cam, sizeof cam, "%s/", ent[i].nome);
                tela_texto_negrito(&T, x + 36, iy + 3, cam, C_TEXTO);
            } else {
                tela_moldura(&T, x + 16, iy + 3, 11, 15, C_FRACO);
                tela_texto(&T, x + 36, iy + 3, ent[i].nome, C_TEXTO);
            }
            registrar(x + 6, iy, w - 12, 22, A_ITEM + i);
        }
        if (nent > cabem) {
            char info[64];
            snprintf(info, sizeof info, "%d itens (role com a roda do mouse)", nent);
            tela_texto(&T, x + w - 12 - texto_largura(info), y + h_lista - 20, info, C_FRACO);
        }

        const char *rv = pode_voltar ? "Voltar (Esc)" : "Sair";
        botao(x, T.h - ALT_BOTAO - 12, 0, rv, A_VOLTAR, true, B_NORMAL);

        GfxEvento ev;
        esperar(&ev);
        if (fechar) break;
        int acao = 0;
        if (ev.tipo == GE_TECLA) {
            if (ev.tecla == GK_ENTER) acao = A_CARREGAR;
            else if (ev.tecla == GK_ESC && pode_voltar) acao = A_VOLTAR;
            else editar_campo(&campo, &cursor, &tudo, &ev);
        } else if (ev.tipo == GE_RODA) {
            rolar(&ev, 3);
        } else if (ev.tipo == GE_CLIQUE) {
            acao = alvo_em(ev.x, ev.y);
        }
        if (acao == A_VOLTAR) break;
        bool entra = acao >= A_ITEM && acao < A_ITEM + nent && ent[acao - A_ITEM].pasta;
        if (entra || acao == A_ACIMA || acao == A_PENDRIVES || acao == A_PESSOAL || acao == A_ATUAL) {
            if (acao == A_ACIMA) pasta_acima(dir, sizeof dir);
            else if (acao == A_PENDRIVES) pasta_pendrives(dir, sizeof dir);
            else if (acao == A_PESSOAL) pasta_pessoal(dir, sizeof dir);
            else if (acao == A_ATUAL) pasta_atual(dir, sizeof dir);
            else {
                char novo[1300];
                juntar(novo, sizeof novo, dir, ent[acao - A_ITEM].nome);
                snprintf(dir, sizeof dir, "%.1023s", novo);
            }
            free(ent);
            ent = listar_pasta(dir, &nent);
            rol_lista = 0;
            continue;
        }
        if (acao >= A_ITEM && acao < A_ITEM + nent) {
            char cam[1300];
            juntar(cam, sizeof cam, dir, ent[acao - A_ITEM].nome);
            if (strcmp(cam, campo.valor)) {   /* primeiro clique: só escolhe */
                snprintf(campo.valor, sizeof campo.valor, "%.511s", cam);
                cursor = (int)strlen(campo.valor);
                continue;
            }
            acao = A_CARREGAR;                /* segundo clique: carrega */
        }
        if (acao == A_CARREGAR) {
            char buf[512];
            snprintf(buf, sizeof buf, "%s", campo.valor);
            char *p = str_trim(buf);
            /* aspas aparecem quando se cola um caminho copiado do sistema */
            size_t l = strlen(p);
            if (l >= 2 && (p[0] == '"' || p[0] == '\'') && p[l - 1] == p[0]) {
                p[l - 1] = '\0';
                p++;
            }
            lin_liberar(&msgs);
            rol_msgs = 0;
            if (!*p) {
                lin_add(&msgs, C_ERRO, false, "erro: informe um caminho (absoluto ou relativo), ex.: exemplos/fig2_5_rm.txt");
                continue;
            }
            Config novo;
            if (config_carregar(p, &novo)) {
                config_liberar(cfg);
                *cfg = novo;
                ok = true;
                break;
            }
            lin_add(&msgs, C_ERRO, true, "Não foi possível usar '%s'. Corrija o arquivo e carregue de novo, ou escolha outro.", p);
            for (int i = 0; i < novo.avisos.n; i++) lin_add(&msgs, C_AVISO, false, "aviso: %s", novo.avisos.itens[i]);
            for (int i = 0; i < novo.erros.n; i++) lin_add(&msgs, C_ERRO, false, "erro: %s", novo.erros.itens[i]);
            config_liberar(&novo);
        }
    }
    free(ent);
    lin_liberar(&msgs);
    return ok;
}

/* ============================================== 4. configuração */

static int *ids_cfg(const Config *cfg) {
    int *ids = xmalloc((size_t)(cfg->n + 1) * sizeof(int));
    for (int i = 0; i < cfg->n; i++) ids[i] = cfg->tarefas[i].id;
    return ids;
}

static const char *regra_quantum(int v) { return config_validar_quantum(v); }
static const char *regra_cpus(int v) { return config_validar_cpus(v); }

/* Tela de configuração (req. 3.1/3.2): mostra tudo com os valores atuais e
 * permite alterar antes de simular. Devolve 'a' (passo a passo), 'b'
 * (completa) ou 0 (sair). */
static int tela_config(Config *cfg) {
    enum { C_ALG = 1, C_QUANTUM, C_CPUS, C_NOVA, C_ARQUIVO, C_PASSO, C_COMPLETA, C_SAIR,
           C_EDITAR = 1000, C_REMOVER = 2000 };
    char msg[600] = "";
    int rol = 0, rol_avisos = 0;
    while (!fechar) {
        novo_quadro();
        int y = cabecalho("Configuração da simulação", "Simulador de escalonamento de tempo real (RM / EDF) - Projeto A");
        int x = 16, w = T.w - 32;
        y += 12;

        /* arquivo */
        painel(x, y, w, 44);
        int wa = largura_botao("Abrir outro arquivo");
        tela_texto_negrito(&T, x + 12, y + 14, "Arquivo:", C_TEXTO);
        texto_cortado(x + 90, y + 14, w - 120 - wa, cfg->caminho ? cfg->caminho : "(nenhum)", C_TEXTO);
        botao(x + w - 8 - wa, y + 8, wa, "Abrir outro arquivo", C_ARQUIVO, true, B_NORMAL);
        y += 44 + 10;

        /* sistema */
        const Escalonador *e = sched_obter(cfg->algoritmo);
        painel(x, y, w, 132);
        titulo_painel(x, y, "Sistema");
        char lin[300];
        int wb = largura_botao("Alterar algoritmo"), xb = x + w - 12 - wb;
        snprintf(lin, sizeof lin, "Algoritmo: %s - %s", e ? e->nome : "?", e ? e->descricao : "");
        texto_cortado(x + 24, y + 40, xb - x - 36, lin, C_TEXTO);
        snprintf(lin, sizeof lin, "Quantum: %d tick(s)", cfg->quantum);
        tela_texto(&T, x + 24, y + 72, lin, C_TEXTO);
        snprintf(lin, sizeof lin, "Quantidade de CPUs: %d (de 1 a %d)", cfg->ncpus, MAX_CPUS);
        tela_texto(&T, x + 24, y + 104, lin, C_TEXTO);
        botao(xb, y + 34, wb, "Alterar algoritmo", C_ALG, true, B_NORMAL);
        botao(xb, y + 66, wb, "Alterar quantum", C_QUANTUM, true, B_NORMAL);
        botao(xb, y + 98, wb, "Alterar CPUs", C_CPUS, true, B_NORMAL);
        y += 132 + 10;

        /* tarefas */
        Linhas util = {0};
        double u = 0;
        for (int i = 0; i < cfg->n; i++) u += (double)cfg->tarefas[i].duracao / cfg->tarefas[i].periodo;
        linhas_utilizacao(&util, cfg->n, u, cfg->algoritmo);
        int h_util = linhas_visuais(&util, w - 24) * LINHA + 10;
        /* avisos do arquivo (ex.: eventos lidos e guardados), com
         * quebra de linha; até 6 linhas, o resto rola com a roda do mouse */
        Linhas avisos = {0};
        for (int i = 0; i < cfg->avisos.n; i++) lin_add(&avisos, C_AVISO, false, "aviso: %s", cfg->avisos.itens[i]);
        int nav = linhas_visuais(&avisos, w - 24);
        int h_avisos = nav ? (nav < 6 ? nav : 6) * LINHA + 8 : 0;
        int h_msg = *msg ? texto_quebrado(0, 0, w - 24, msg, 0, false) + 12 + 8 : 0;
        int h_tab = T.h - y - 10 - h_msg - (ALT_BOTAO + 24);
        if (h_tab < 150) h_tab = 150;
        painel(x, y, w, h_tab);
        snprintf(lin, sizeof lin, "Tarefas (%d periódicas)", cfg->n);
        titulo_painel(x, y, lin);
        int wn = largura_botao("+ Nova tarefa");
        botao(x + w - 12 - wn, y + 6, wn, "+ Nova tarefa", C_NOVA, true, B_NORMAL);
        /* colunas da tabela */
        static const char *const cab[] = {"id", "cor", "ingresso", "duração", "período", "prazo", "eventos"};
        static const int col[] = {24, 84, 190, 280, 370, 460, 540};
        int ty = y + 40;
        for (int c = 0; c < 7; c++) tela_texto_negrito(&T, x + col[c], ty, cab[c], C_FRACO);
        tela_ret(&T, x + 12, ty + 20, w - 24, 1, C_BORDA);
        int ly = ty + 26;
        int lh = h_tab - (ly - y) - h_util - h_avisos - 6;
        int cabem = lh / 30;
        if (cabem < 1) cabem = 1;
        area_rolagem(x, ly, w, cabem * 30, &rol, cfg->n - cabem);
        if (cfg->n == 0) tela_texto(&T, x + 24, ly + 6, "Nenhuma tarefa periódica. Use \"+ Nova tarefa\" ou abra outro arquivo.", C_FRACO);
        int we = largura_botao("Editar"), wr = largura_botao("Remover");
        for (int i = rol, j = 0; i < cfg->n && j < cabem; i++, j++) {
            const TarefaCfg *t = &cfg->tarefas[i];
            int ry = ly + j * 30;
            if (dentro(mx, my, x + 6, ry, w - 12, 30)) tela_ret(&T, x + 6, ry, w - 12, 30, 0xF9FAFB);
            char v[64];
            snprintf(v, sizeof v, "%d", t->id);
            tela_texto_negrito(&T, x + col[0], ry + 7, v, t->cor);
            tela_ret(&T, x + col[1], ry + 7, 18, 16, t->cor);
            tela_moldura(&T, x + col[1], ry + 7, 18, 16, 0x555555);
            snprintf(v, sizeof v, "%06X", t->cor);
            tela_texto(&T, x + col[1] + 24, ry + 7, v, C_TEXTO);
            int nums[4] = {t->ingresso, t->duracao, t->periodo, t->prazo};
            for (int c = 0; c < 4; c++) {
                snprintf(v, sizeof v, "%d", nums[c]);
                tela_texto(&T, x + col[2 + c], ry + 7, v, C_TEXTO);
            }
            texto_cortado(x + col[6], ry + 7, w - col[6] - we - wr - 40, t->eventos && *t->eventos ? t->eventos : "-", C_TEXTO);
            botao(x + w - 12 - wr, ry + 1, wr, "Remover", C_REMOVER + i, true, B_PERIGO);
            botao(x + w - 18 - wr - we, ry + 1, we, "Editar", C_EDITAR + i, true, B_NORMAL);
        }
        if (cfg->n > cabem) {
            snprintf(lin, sizeof lin, "mostrando %d a %d de %d (role com a roda do mouse)", rol + 1,
                     rol + cabem < cfg->n ? rol + cabem : cfg->n, cfg->n);
            tela_texto(&T, x + w - 12 - texto_largura(lin), ly + cabem * 30, lin, C_FRACO);
        }
        int uy = y + h_tab - h_util - h_avisos;
        tela_ret(&T, x + 12, uy - 4, w - 24, 1, C_BORDA);
        int zero = 0;
        desenhar_linhas(x + 12, uy + 2, w - 24, h_util - 10, &util, &zero);
        lin_liberar(&util);
        if (nav) desenhar_linhas(x + 12, uy + h_util, w - 24, h_avisos - 8, &avisos, &rol_avisos);
        lin_liberar(&avisos);
        y += h_tab + 8;
        if (*msg) y += linha_mensagem(x, y, w, msg) + 8;

        /* ações */
        int by = T.h - ALT_BOTAO - 12;
        int bx = x;
        bx += botao(bx, by, 0, "Iniciar passo a passo (Enter)", C_PASSO, true, B_PRIMARIO) + 8;
        botao(bx, by, 0, "Execução completa", C_COMPLETA, true, B_NORMAL);
        botao(x + w - largura_botao("Sair"), by, 0, "Sair", C_SAIR, true, B_NORMAL);

        GfxEvento ev;
        esperar(&ev);
        if (fechar) return 0;
        int acao = 0;
        if (ev.tipo == GE_CLIQUE) acao = alvo_em(ev.x, ev.y);
        else if (ev.tipo == GE_RODA) rolar(&ev, 1);
        else if (ev.tipo == GE_TECLA && ev.tecla == GK_ENTER) acao = C_PASSO;
        if (!acao) continue;
        msg[0] = '\0';
        switch (acao) {
        case C_PASSO:
        case C_COMPLETA:
            if (cfg->n == 0) {
                fmsg(msg, sizeof msg, "erro: não há tarefas periódicas para simular; use \"+ Nova tarefa\" ou abra outro arquivo");
                break;
            }
            return acao == C_PASSO ? 'a' : 'b';
        case C_SAIR: return 0;
        case C_ARQUIVO: tela_arquivo(cfg, true, NULL); rol = 0; break;
        case C_ALG: {
            int a = escolher_algoritmo(cfg->algoritmo);
            if (a >= 0) cfg->algoritmo = a;
            break;
        }
        case C_QUANTUM:
            pedir_numero("Quantum", "Quantos ticks seguidos uma tarefa pode usar a CPU antes de o escalonador reavaliar.",
                         "quantum", cfg->quantum, regra_quantum, &cfg->quantum);
            break;
        case C_CPUS: {
            char rot[64];
            snprintf(rot, sizeof rot, "CPUs (1 a %d)", MAX_CPUS);
            pedir_numero("Quantidade de CPUs", "Processadores do sistema simulado (fila global de prontas).", rot,
                         cfg->ncpus, regra_cpus, &cfg->ncpus);
            break;
        }
        case C_NOVA: {
            int *ids = ids_cfg(cfg);
            TarefaCfg t = {proximo_id(ids, cfg->n), config_cor_padrao(cfg->n), PADRAO_INGRESSO,
                           PADRAO_DURACAO, PADRAO_PERIODO, PADRAO_PERIODO, NULL, 0, NULL, 0};
            char ev_txt[512];
            if (form_tarefa("Nova tarefa", &t, ids, cfg->n, -1, 0, ev_txt, sizeof ev_txt)) {
                config_adicionar(cfg, &t);
                fmsg(msg, sizeof msg, "Tarefa %d adicionada.", t.id);
            }
            free(ids);
            break;
        }
        default:
            if (acao >= C_REMOVER && acao < C_REMOVER + cfg->n) {
                int i = acao - C_REMOVER, id = cfg->tarefas[i].id;
                config_liberar_tarefa(&cfg->tarefas[i]);
                memmove(&cfg->tarefas[i], &cfg->tarefas[i + 1], (size_t)(cfg->n - i - 1) * sizeof(TarefaCfg));
                cfg->n--;
                fmsg(msg, sizeof msg, "Tarefa %d removida.", id);
            } else if (acao >= C_EDITAR && acao < C_EDITAR + cfg->n) {
                int i = acao - C_EDITAR;
                int *ids = ids_cfg(cfg);
                TarefaCfg t = cfg->tarefas[i];
                char ev_txt[512], tit[64];
                snprintf(tit, sizeof tit, "Editar a tarefa %d", t.id);
                if (form_tarefa(tit, &t, ids, cfg->n, i, 0, ev_txt, sizeof ev_txt)) {
                    /* `t` divide os ponteiros de eventos com a original: copia
                     * os campos numéricos e troca a lista pelo texto novo. */
                    TarefaCfg *dst = &cfg->tarefas[i];
                    t.eventos = dst->eventos;
                    t.lista_ev = dst->lista_ev;
                    t.nev = dst->nev;
                    *dst = t;
                    config_definir_eventos(dst, ev_txt);
                    fmsg(msg, sizeof msg, "Tarefa %d alterada.", t.id);
                }
                free(ids);
            }
        }
    }
    return 0;
}

/* ============================================== 5. simulação */

typedef struct {
    const Config *cfg;
    const OpcoesUI *op;
    bool completo;          /* execução completa: sem voltar nem editar  */
    Simulador s;
    Config extras;          /* tarefas criadas durante a simulação       */
    char msg[1600];
    char caminho_svg[1024];
    bool exportado;
    VistaGantt v;
    bool seguir;            /* o Gantt acompanha o instante atual        */
    int tick_insp;          /* tick mostrado no painel de baixo; -1 = último */
    int rol_esq, rol_dir;   /* rolagem dos painéis de baixo              */
    /* geometria do último quadro (para cliques e arrastos) */
    int barra_x, barra_y, barra_w, polegar_x, polegar_w;
    int total, nvis, arrasto_dx;
} Sessao;

enum {
    S_INICIO = 1, S_VOLTAR, S_AVANCAR, S_AVANCAR10, S_FIM, S_IR, S_EDITAR, S_NOVA, S_ALG, S_QUANTUM, S_CPUS,
    S_EXPORTAR, S_ESTAT, S_AJUDA, S_SAIR, S_ZOOM_MENOS, S_ZOOM_MAIS, S_SEGUIR, S_GANTT, S_BARRA, S_POLEGAR,
    S_VER_SISTEMA
};

/* Cópia rasa do simulador com só o que já aconteceu até o instante atual:
 * ao voltar no tempo, o "futuro" gravado não deve aparecer (como no
 * terminal). Só os números ncols/nev mudam; os vetores são os mesmos. */
static Simulador visivel(const Sessao *z) {
    Simulador vis = z->s;
    if (z->completo) return vis;
    vis.ncols = z->s.atual.t < z->s.ncols ? z->s.atual.t : z->s.ncols;
    int nev = 0;
    while (nev < z->s.nev && z->s.ev[nev].tick < z->s.atual.t) nev++;
    vis.nev = nev;
    return vis;
}

static bool exportar(Sessao *z, const char *caminho) {
    Simulador vis = visivel(z);
    char erro[512];
    if (gantt_svg(&vis, caminho, z->cfg->caminho ? z->cfg->caminho : "simulação", erro, sizeof erro)) {
        fmsg(z->msg, sizeof z->msg, "Gantt da simulação exportado para '%s' (abra no navegador).", caminho);
        return true;
    }
    fmsg(z->msg, sizeof z->msg, "erro ao exportar: %s", erro);
    return false;
}

/* ---- textos dos painéis (o mesmo conteúdo dos comandos do terminal) ---- */

/* Painel esquerdo: o que aconteceu num tick (CPUs, fila e eventos). */
static void linhas_tick(const Sessao *z, const Simulador *vis, Linhas *L) {
    const Estado *e = &z->s.atual;
    int t = z->tick_insp >= 0 && z->tick_insp < vis->ncols ? z->tick_insp : vis->ncols - 1;
    if (t < 0) {
        lin_add(L, C_TEXTO, true, "Instante 0: nada executado ainda.");
        lin_add(L, C_FRACO, false, "Use \"Avançar\" (ou a seta para a direita) para executar o primeiro tick.");
        return;
    }
    lin_add(L, C_TEXTO, true, "Tick %d (do instante %d ao %d)%s", t, t, t + 1, t == vis->ncols - 1 ? " - o último executado" : "");
    for (int c = 0; c < vis->ncpus_col[t]; c++) {
        int quem = -1;
        for (int i = 0; i < e->n; i++)
            if (sim_cpu_celula(vis, t, i) == c) quem = i;
        if (quem >= 0) {
            lin_add(L, C_TEXTO, false, "CPU%d: tarefa T%d", c, e->tarefas[quem].id);
            L->v[L->n - 1].amostra = e->tarefas[quem].cor;
        } else {
            lin_add(L, C_FRACO, false, "CPU%d: desligada (nenhuma tarefa pronta)", c);
        }
    }
    char prontas[200] = "";
    for (int i = 0; i < e->n; i++)
        if (sim_celula(vis, t, i) == G_PRONTA) {
            char id[16];
            snprintf(id, sizeof id, " T%d", e->tarefas[i].id);
            if (strlen(prontas) + strlen(id) < sizeof prontas) strcat(prontas, id);
        }
    lin_add(L, C_TEXTO, false, "Prontas esperando CPU:%s", *prontas ? prontas : " (nenhuma)");
    bool algum = false;
    for (int j = 0; j < vis->nev; j++) {
        const Evento *ev = &vis->ev[j];
        if (ev->tick != t) continue;
        if (!algum) lin_add(L, C_TEXTO, true, "Eventos:");
        algum = true;
        lin_add(L, ev->tipo == EV_PRAZO_PERDIDO ? C_ERRO : C_TEXTO, ev->tipo == EV_PRAZO_PERDIDO,
                "  instante %d: T%d %s%s", ev->instante, e->tarefas[ev->tarefa].id, evento_nome(ev->tipo),
                ev->tipo == EV_PRAZO_PERDIDO ? "!" : "");
    }
    if (!algum) lin_add(L, C_FRACO, false, "Nenhum evento neste tick.");
    lin_add(L, C_FRACO, false, "Clique numa coluna do gráfico para ver outro tick.");
}

/* Painel direito com uma tarefa escolhida: o TCB (comando `t` do terminal). */
static void linhas_tarefa(const Sessao *z, int i, Linhas *L) {
    const TCB *k = &z->s.atual.tarefas[i];
    lin_add(L, k->cor, true, "Tarefa %d (TCB) no instante %d", k->id, z->s.atual.t);
    L->v[L->n - 1].amostra = k->cor;
    lin_add(L, C_TEXTO, false, "Parâmetros: cor %06X | ingresso %d | duração %d | período %d | prazo %d", k->cor,
            k->ingresso, k->duracao, k->periodo, k->prazo);
    lin_add(L, C_TEXTO, false, "Estado: %s", estado_descricao(k));
    if (k->restante > 0) lin_add(L, C_TEXTO, false, "Prioridade (menor = mais prioritária): nominal %d, ativa %d", k->prio_nominal, k->prio_ativa);
    lin_add(L, C_TEXTO, false, "Ativações: %d chegaram, %d concluídas (de %d)", k->liberadas, k->concluidas, MAX_ATIVACOES);
    if (k->restante > 0) {
        bool perdeu = k->perdas & (1u << k->concluidas);
        lin_add(L, perdeu ? C_ERRO : C_TEXTO, perdeu, "Ativação atual: nº %d, chegou em %d, deadline em %d, faltam %d tick(s)%s",
                k->concluidas + 1, k->chegada_atual, k->deadline_abs, k->restante, perdeu ? " - PRAZO PERDIDO" : "");
        if (k->liberadas - k->concluidas > 1) lin_add(L, C_TEXTO, false, "Ativações acumuladas esperando: %d", k->liberadas - k->concluidas - 1);
    }
    if (k->liberadas < MAX_ATIVACOES && k->estado != T_TERMINADA) lin_add(L, C_TEXTO, false, "Próxima chegada: instante %d", k->proxima_chegada);
    if (k->cpu >= 0) lin_add(L, C_TEXTO, false, "CPU no último tick: CPU%d (quantum usado %d/%d)", k->cpu, k->quantum_usado, z->s.atual.quantum);
    else lin_add(L, C_TEXTO, false, "CPU no último tick: nenhuma");
    lin_add(L, C_TEXTO, false, "Executou %d, esperou %d, suspensa %d tick(s); %d preempção(ões), %d sorteio(s), %d prazo(s) perdido(s)",
            k->ticks_exec, k->ticks_espera, k->ticks_suspensa, k->preempcoes, k->sorteios, k->deadlines_perdidos);
    if (k->neventos) {
        char evs[200] = "";
        for (int j = 0; j < k->neventos; j++) {
            if (strlen(evs) + strlen(k->eventos[j]) + 4 >= sizeof evs) break;
            if (j) strcat(evs, " | ");
            strcat(evs, k->eventos[j]);
        }
        lin_add(L, C_FRACO, false, "Eventos: %s", evs);
    }
    if (k->termino >= 0) lin_add(L, C_TEXTO, false, "Terminou no instante %d", k->termino);
}

/* Painel direito sem tarefa escolhida: o sistema todo (comando `s`). */
static void linhas_sistema(const Sessao *z, Linhas *L) {
    const Estado *e = &z->s.atual;
    const Escalonador *esc = sched_obter(e->alg);
    lin_add(L, C_TEXTO, true, "Sistema no instante %d: %s", e->t,
            e->fim ? "simulação concluída" : sim_bloqueado(e) ? "bloqueada (todas as restantes suspensas)" : "em andamento");
    lin_add(L, C_TEXTO, false, "Algoritmo %s | quantum %d | %d CPU(s)", esc->nome, e->quantum, e->ncpus);
    linhas_utilizacao(L, e->n, sim_utilizacao(e), e->alg);
    lin_add(L, C_FRACO, true, "  %6s  %-27s %6s %8s %8s %4s %6s", "id", "estado", "ativ.", "restante", "deadline", "CPU", "prazos");
    int *ordem = gantt_ordem(e);
    for (int r = e->n - 1; r >= 0; r--) {
        const TCB *k = &e->tarefas[ordem[r]];
        char rest[16] = "-", dl[16] = "-", cpu[16] = "-";
        if (k->restante > 0) {
            snprintf(rest, sizeof rest, "%d", k->restante);
            snprintf(dl, sizeof dl, "%d", k->deadline_abs);
        }
        if (k->cpu >= 0) snprintf(cpu, sizeof cpu, "%d", k->cpu);
        lin_add(L, k->deadlines_perdidos ? C_ERRO : C_TEXTO, false, "%6d  %-27s %3d/%-2d %8s %8s %4s %6d", k->id,
                estado_descricao(k), k->concluidas, MAX_ATIVACOES, rest, dl, cpu, k->deadlines_perdidos);
        L->v[L->n - 1].amostra = k->cor;
    }
    free(ordem);
    lin_add(L, C_FRACO, false, "Clique numa tarefa no gráfico para ver o TCB dela.");
}

/* Estatísticas finais (como no terminal ao sair). */
static void linhas_estatisticas(const Simulador *s, Linhas *L) {
    const Estado *e = &s->atual;
    lin_add(L, C_TEXTO, true, "Estatísticas por tarefa");
    lin_add(L, C_FRACO, true, "%6s %7s %7s %7s %7s %8s %8s %6s %7s %7s", "id", "ativ.", "prazos", "exec", "espera",
            "resp.méd", "resp.máx", "preemp", "sorteio", "término");
    int perdidos = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        perdidos += k->deadlines_perdidos;
        char term[16] = "-";
        if (k->termino >= 0) snprintf(term, sizeof term, "%d", k->termino);
        lin_add(L, C_TEXTO, false, "%6d %4d/%-2d %7d %7d %7d %8.1f %8d %6d %7d %7s", k->id, k->concluidas, MAX_ATIVACOES,
                k->deadlines_perdidos, k->ticks_exec, k->ticks_espera,
                k->concluidas ? (double)k->resposta_soma / k->concluidas : 0.0, k->resposta_max, k->preempcoes,
                k->sorteios, term);
        L->v[L->n - 1].amostra = k->cor;
    }
    lin_add(L, C_TEXTO, false, "");
    lin_add(L, C_TEXTO, true, "Uso das CPUs (%d ticks)", s->ncols);
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
        lin_add(L, C_TEXTO, false, "CPU%d: ocupada %d tick(s), desligada %d tick(s) (%.1f%% de uso)", c, ocup,
                existe - ocup, existe ? 100.0 * ocup / existe : 0.0);
    }
    lin_add(L, C_TEXTO, false, "");
    if (perdidos) lin_add(L, C_ERRO, true, "Total de prazos perdidos: %d", perdidos);
    else lin_add(L, C_OK, true, "Nenhum prazo perdido.");
    lin_add(L, C_FRACO, false, "(resp. = tempo de resposta: instante em que a ativação terminou - instante em que chegou)");
}

static void ajuda(bool completo) {
    Linhas L = {0};
    lin_add(&L, C_TEXTO, true, "Como usar");
    if (!completo) {
        lin_add(&L, C_TEXTO, false, "Avançar (seta direita, Enter ou espaço) executa 1 tick; \"Avançar 10\" executa 10.");
        lin_add(&L, C_TEXTO, false, "Voltar (seta esquerda) desfaz 1 tick; Início (Home) volta ao instante 0.");
        lin_add(&L, C_TEXTO, false, "\"Ir para...\" (tecla g) salta para um instante; \"Até o fim\" (End) roda o resto.");
        lin_add(&L, C_TEXTO, false, "Clique numa tarefa no gráfico para ver o TCB; \"Editar tarefa\" (tecla e) altera");
        lin_add(&L, C_TEXTO, false, "  cor, duração, período, prazo, ingresso, ticks restantes, suspende/retoma ou encerra.");
        lin_add(&L, C_TEXTO, false, "\"+ Tarefa\" adiciona uma tarefa; Algoritmo, Quantum e CPUs valem a partir do instante atual.");
        lin_add(&L, C_TEXTO, false, "Editar num instante passado descarta o que já tinha sido simulado depois dele.");
    } else {
        lin_add(&L, C_TEXTO, false, "Execução completa: a simulação já rodou inteira; só dá para revisar e exportar.");
    }
    lin_add(&L, C_TEXTO, false, "Roda do mouse: anda no tempo (se o gráfico não couber na altura, rola para cima/baixo;");
    lin_add(&L, C_TEXTO, false, "  aí Shift + roda anda no tempo). PgUp/PgDn também andam no tempo.");
    lin_add(&L, C_TEXTO, false, "Arraste a barra abaixo do gráfico para andar no tempo; Zoom - / + muda a escala.");
    lin_add(&L, C_TEXTO, false, "Clique num tick do gráfico para ver o que aconteceu nele (painel da esquerda).");
    lin_add(&L, C_TEXTO, false, "O SVG com a simulação inteira é gerado sozinho ao chegar ao fim (\"Exportar SVG\" gera na hora).");
    lin_add(&L, C_TEXTO, false, "");
    lin_add(&L, C_TEXTO, true, "Símbolos do gráfico (os mesmos do SVG)");
    lin_add(&L, C_TEXTO, false, "barra colorida com número = executando, na CPU indicada | tracejado = pronta (sem CPU)");
    lin_add(&L, C_TEXTO, false, "preto pontilhado = suspensa | seta para baixo = chegada | triângulo = fim de ativação");
    lin_add(&L, C_TEXTO, false, "quadrado preto = término (10ª ativação) | X vermelho = perda de prazo | círculo \"?\" = sorteio");
    lin_add(&L, C_TEXTO, false, "Linhas CPUn: quem ocupou cada processador; listrado = CPU desligada | linha azul = instante atual");
    janela_texto("Ajuda", &L);
    lin_liberar(&L);
}

/* "Editar tarefa" na simulação (req. 3.4): todos os campos de uma vez,
 * mais Suspender/Retomar e Encerrar. As regras são as do terminal
 * (editar_tarefa em ui.c), via edicao.c. */
typedef struct { const TCB *k; int t; int v[6]; unsigned cor; } CtxEdicao;

enum { E_COR, E_DURACAO, E_PERIODO, E_PRAZO, E_INGRESSO, E_RESTANTE, E_N };
enum { E_SUSPENDER = 50001, E_ENCERRAR };

static const char *val_edicao(Campo *c, int n, void *ctx, int *ce) {
    (void)n;
    CtxEdicao *k = ctx;
    char cor[64];
    snprintf(cor, sizeof cor, "%.63s", c[E_COR].valor);
    if (!parse_cor(str_trim(cor), &k->cor)) {
        *ce = E_COR;
        return "cor: use 6 dígitos hexadecimais, ex.: F0E0D0";
    }
    for (int i = E_DURACAO; i < E_N; i++) {
        if (c[i].bloqueado) continue;
        const char *m = campo_int(&c[i], &k->v[i]);
        if (!m) {
            if (i == E_PERIODO) m = valida_periodo(k->v[i]);
            else if (i == E_INGRESSO) m = valida_ingresso(k->v[i], k->t);
            else m = valida_positivo(k->v[i]);
            m = com_rotulo(&c[i], m);
        }
        if (m) {
            *ce = i;
            return m;
        }
    }
    return NULL;
}

static void editar_na_simulacao(Sessao *z, int i) {
    Simulador *s = &z->s;
    TCB *k = &s->atual.tarefas[i];
    int t = s->atual.t;
    bool terminou = k->estado == T_TERMINADA;
    Campo c[E_N] = {
        {"cor (RGB hex)", "", false}, {"duração", "", terminou}, {"período", "", terminou},
        {"prazo", "", terminou}, {"ingresso", "", terminou || k->liberadas > 0},
        {"ticks restantes", "", terminou || k->restante <= 0},
    };
    snprintf(c[E_COR].valor, sizeof c[0].valor, "%06X", k->cor);
    snprintf(c[E_DURACAO].valor, sizeof c[0].valor, "%d", k->duracao);
    snprintf(c[E_PERIODO].valor, sizeof c[0].valor, "%d", k->periodo);
    snprintf(c[E_PRAZO].valor, sizeof c[0].valor, "%d", k->prazo);
    snprintf(c[E_INGRESSO].valor, sizeof c[0].valor, "%d", k->ingresso < t && k->liberadas == 0 ? t : k->ingresso);
    if (k->restante > 0) snprintf(c[E_RESTANTE].valor, sizeof c[0].valor, "%d", k->restante);
    else snprintf(c[E_RESTANTE].valor, sizeof c[0].valor, "-");

    Linhas intro = {0};
    lin_add(&intro, C_TEXTO, false, "Estado: %s | ativações %d/%d", estado_descricao(k), k->concluidas, MAX_ATIVACOES);
    lin_add(&intro, C_FRACO, false, "A mudança vale a partir do instante %d. A duração nova vale a partir da próxima", t);
    lin_add(&intro, C_FRACO, false, "ativação (\"ticks restantes\" muda a atual).");
    if (terminou) lin_add(&intro, C_AVISO, false, "A tarefa já terminou as %d ativações: só a cor pode mudar.", MAX_ATIVACOES);
    else if (k->liberadas > 0) lin_add(&intro, C_FRACO, false, "O ingresso não muda mais: a tarefa já chegou no instante %d.", k->chegadas[0]);

    BotaoExtra ex[2] = {{suspensa_pelo_usuario(k) ? "Retomar" : "Suspender", E_SUSPENDER, B_NORMAL},
                        {"Encerrar agora", E_ENCERRAR, B_PERIGO}};
    CtxEdicao ctx = {k, t, {0}, k->cor};
    char tit[64];
    snprintf(tit, sizeof tit, "Editar a tarefa %d (instante %d)", k->id, t);
    int r = formulario(tit, &intro, c, E_N, val_edicao, &ctx, ex, terminou ? 0 : 2);
    lin_liberar(&intro);
    if (r == FORM_CANCELAR) return;
    if (r == E_SUSPENDER) {
        sim_preparar_edicao(s);
        edicao_alternar_suspensao(k);
        sim_concluir_edicao(s);
        if (suspensa_pelo_usuario(k))
            fmsg(z->msg, sizeof z->msg, "Tarefa %d suspensa: não disputa CPU até ser retomada (ativações continuam chegando e acumulando).", k->id);
        else
            fmsg(z->msg, sizeof z->msg, "Tarefa %d retomada (%s).", k->id, estado_descricao(k));
        z->exportado = false;
        return;
    }
    if (r == E_ENCERRAR) {
        sim_preparar_edicao(s);
        edicao_encerrar(k, t);
        sim_concluir_edicao(s);
        fmsg(z->msg, sizeof z->msg, "Tarefa %d encerrada no instante %d.", k->id, t);
        z->exportado = false;
        return;
    }
    /* OK: aplica só o que mudou, tudo numa edição só */
    bool muda[E_N] = {ctx.cor != k->cor,
                      !c[E_DURACAO].bloqueado && ctx.v[E_DURACAO] != k->duracao,
                      !c[E_PERIODO].bloqueado && ctx.v[E_PERIODO] != k->periodo,
                      !c[E_PRAZO].bloqueado && ctx.v[E_PRAZO] != k->prazo,
                      !c[E_INGRESSO].bloqueado && ctx.v[E_INGRESSO] != k->ingresso,
                      !c[E_RESTANTE].bloqueado && ctx.v[E_RESTANTE] != k->restante};
    bool alguma = false;
    for (int j = 0; j < E_N; j++) alguma |= muda[j];
    if (!alguma) {
        fmsg(z->msg, sizeof z->msg, "Nenhuma alteração na tarefa %d.", k->id);
        return;
    }
    sim_preparar_edicao(s);
    char feito[600] = "";
    size_t n = 0;
#define FEITO(...) n += (size_t)snprintf(feito + n, n < sizeof feito ? sizeof feito - n : 0, __VA_ARGS__)
    if (muda[E_COR]) { k->cor = ctx.cor; FEITO(" cor %06X;", ctx.cor); }
    if (muda[E_DURACAO]) { k->duracao = ctx.v[E_DURACAO]; FEITO(" duração %d (a partir da próxima ativação);", k->duracao); }
    if (muda[E_PERIODO]) { edicao_periodo(k, ctx.v[E_PERIODO]); FEITO(" período %d, próxima chegada no instante %d%s;", k->periodo, k->proxima_chegada, k->proxima_chegada < t ? " (já passou: chega no próximo tick)" : ""); }
    if (muda[E_PRAZO]) { edicao_prazo(k, ctx.v[E_PRAZO]); FEITO(" prazo %d%s;", k->prazo, k->restante > 0 ? " (deadline da ativação atual recalculado)" : ""); }
    if (muda[E_INGRESSO]) { edicao_ingresso(k, ctx.v[E_INGRESSO]); FEITO(" ingresso %d;", k->ingresso); }
    if (muda[E_RESTANTE]) { k->restante = ctx.v[E_RESTANTE]; FEITO(" ativação atual precisa de %d tick(s);", k->restante); }
#undef FEITO
    sim_concluir_edicao(s);
    if (n && n < sizeof feito) feito[n - 1] = '.';
    fmsg(z->msg, sizeof z->msg, "Tarefa %d alterada no instante %d:%s", k->id, t, feito);
    z->exportado = false;
}

/* Executa a ação de um botão ou tecla da tela de simulação. Devolve false
 * para sair da simulação. */
static bool acao_simulacao(Sessao *z, int a) {
    Simulador *s = &z->s;
    if (z->completo && a >= S_INICIO && a <= S_CPUS) return true;   /* desligados na completa */
    switch (a) {
    case S_AVANCAR:
    case S_AVANCAR10: {
        int k = a == S_AVANCAR ? 1 : 10, feitos = 0;
        while (feitos < k && sim_avancar(s)) feitos++;
        if (feitos < k && s->atual.fim) fmsg(z->msg, sizeof z->msg, "erro: a simulação já terminou (instante %d)", s->atual.t);
        z->seguir = true;
        z->tick_insp = -1;
        break;
    }
    case S_VOLTAR:
        if (s->atual.t == 0) fmsg(z->msg, sizeof z->msg, "erro: já está no instante 0");
        else sim_voltar(s);
        z->seguir = true;
        z->tick_insp = -1;
        break;
    case S_INICIO:
        sim_ir_para(s, 0);
        z->seguir = true;
        z->tick_insp = -1;
        break;
    case S_FIM: {
        const char *motivo = sim_rodar_ate_fim(s);
        if (motivo) fmsg(z->msg, sizeof z->msg, "erro: parou no instante %d: %s", s->atual.t, motivo);
        z->seguir = true;
        z->tick_insp = -1;
        break;
    }
    case S_IR: {
        int v;
        if (pedir_numero("Ir para o instante", "Avança ou volta a simulação até o instante informado.", "instante",
                         s->atual.t, regra_nao_negativo, &v)) {
            if (sim_ir_para(s, v) != v) fmsg(z->msg, sizeof z->msg, "A simulação termina no instante %d; parou lá.", s->atual.t);
            z->seguir = true;
            z->tick_insp = -1;
        }
        break;
    }
    case S_EDITAR: {
        int i = z->v.sel;
        if (i < 0) {
            int id;
            if (!pedir_numero("Editar tarefa", "Dica: clicar numa tarefa no gráfico já a escolhe.", "id da tarefa",
                              s->atual.n ? s->atual.tarefas[0].id : 0, NULL, &id))
                break;
            i = sim_indice(&s->atual, id);
            if (i < 0) {
                fmsg(z->msg, sizeof z->msg, "erro: não existe tarefa com id %d neste instante", id);
                break;
            }
            z->v.sel = i;
        }
        editar_na_simulacao(z, i);
        break;
    }
    case S_NOVA: {
        Estado *e = &s->atual;
        int *ids = xmalloc((size_t)(e->n + 1) * sizeof(int));
        for (int i = 0; i < e->n; i++) ids[i] = e->tarefas[i].id;
        TarefaCfg t = {proximo_id(ids, e->n), config_cor_padrao(e->n), e->t, PADRAO_DURACAO, PADRAO_PERIODO,
                       PADRAO_PERIODO, NULL, 0, NULL, 0};
        char ev[512];
        bool ok = form_tarefa("Nova tarefa durante a simulação", &t, ids, e->n, -1, e->t, ev, sizeof ev);
        free(ids);
        if (!ok) break;
        /* O TCB aponta para os eventos da configuração; a tarefa fica em
         * `extras`, que vive até o fim da simulação. */
        config_adicionar(&z->extras, &t);
        sim_preparar_edicao(s);
        sim_adicionar_tarefa(s, &z->extras.tarefas[z->extras.n - 1]);
        sim_concluir_edicao(s);
        z->exportado = false;
        fmsg(z->msg, sizeof z->msg, "Tarefa %d adicionada; chega no instante %d.", t.id, t.ingresso);
        break;
    }
    case S_ALG: {
        int a2 = escolher_algoritmo(s->atual.alg);
        if (a2 >= 0 && a2 != s->atual.alg) {
            sim_preparar_edicao(s);
            s->atual.alg = a2;
            sim_concluir_edicao(s);
            z->exportado = false;
            fmsg(z->msg, sizeof z->msg, "Algoritmo trocado para %s a partir do instante %d.", sched_obter(a2)->nome, s->atual.t);
        }
        break;
    }
    case S_QUANTUM:
    case S_CPUS: {
        int v;
        char rot[64];
        snprintf(rot, sizeof rot, a == S_QUANTUM ? "quantum" : "CPUs (1 a %d)", MAX_CPUS);
        bool ok = a == S_QUANTUM ? pedir_numero("Quantum", "Vale a partir do instante atual.", rot, s->atual.quantum, regra_quantum, &v)
                                 : pedir_numero("Quantidade de CPUs", "Vale a partir do instante atual.", rot, s->atual.ncpus, regra_cpus, &v);
        if (!ok) break;
        sim_preparar_edicao(s);
        if (a == S_QUANTUM) s->atual.quantum = v;
        else s->atual.ncpus = v;
        sim_concluir_edicao(s);
        z->exportado = false;
        fmsg(z->msg, sizeof z->msg, "%s = %d a partir do instante %d.", a == S_QUANTUM ? "Quantum" : "CPUs", v, s->atual.t);
        break;
    }
    case S_EXPORTAR: {
        Campo c = {"arquivo SVG", "", false};
        snprintf(c.valor, sizeof c.valor, "%.511s", z->caminho_svg);
        Linhas intro = {0};
        lin_add(&intro, C_FRACO, false, "Gera a imagem com o que foi simulado até o instante %d.", s->atual.t);
        int r = formulario("Exportar o Gantt em SVG", &intro, &c, 1, NULL, NULL, NULL, 0);
        lin_liberar(&intro);
        if (r == FORM_OK) exportar(z, *str_trim(c.valor) ? str_trim(c.valor) : z->caminho_svg);
        break;
    }
    case S_ESTAT: {
        Linhas L = {0};
        Simulador vis = visivel(z);
        linhas_estatisticas(&vis, &L);
        janela_texto("Estatísticas", &L);
        lin_liberar(&L);
        break;
    }
    case S_AJUDA: ajuda(z->completo); break;
    case S_SAIR: return false;
    case S_ZOOM_MENOS:
    case S_ZOOM_MAIS: {
        static const int zooms[] = {2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48};
        int nz = (int)(sizeof zooms / sizeof zooms[0]), k = 0;
        while (k < nz - 1 && zooms[k] < z->v.cw) k++;
        k += a == S_ZOOM_MAIS ? 1 : -1;
        if (k < 0) k = 0;
        if (k >= nz) k = nz - 1;
        z->v.cw = zooms[k];
        break;
    }
    case S_SEGUIR: z->seguir = true; break;
    case S_VER_SISTEMA: z->v.sel = -1; break;
    default: break;
    }
    return true;
}

static void desenhar_simulacao(Sessao *z) {
    Simulador *s = &z->s;
    Simulador vis = visivel(z);
    const Escalonador *esc = sched_obter(s->atual.alg);
    novo_quadro();
    char sub[512];
    snprintf(sub, sizeof sub, "%s | %s | %d CPU(s) | quantum %d | instante t = %d%s",
             z->cfg->caminho ? z->cfg->caminho : "", esc->nome, s->atual.ncpus, s->atual.quantum, s->atual.t,
             s->atual.fim ? " | concluída" : sim_bloqueado(&s->atual) ? " | bloqueada (todas suspensas)" : "");
    int y = cabecalho(z->completo ? "Execução completa" : "Passo a passo", sub);
    int x = 16, w = T.w - 32;

    /* barra de ferramentas */
    Fluxo f = {x, w, x, y + 10};
    bool p = !z->completo;
    fluxo_botao(&f, "|< Início", S_INICIO, p && s->atual.t > 0, B_NORMAL);
    fluxo_botao(&f, "< Voltar", S_VOLTAR, p && s->atual.t > 0, B_NORMAL);
    fluxo_botao(&f, "Avançar >", S_AVANCAR, p && !s->atual.fim, B_PRIMARIO);
    fluxo_botao(&f, "Avançar 10 >>", S_AVANCAR10, p && !s->atual.fim, B_NORMAL);
    fluxo_botao(&f, "Até o fim >|", S_FIM, p && !s->atual.fim, B_NORMAL);
    fluxo_botao(&f, "Ir para...", S_IR, p, B_NORMAL);
    fluxo_espaco(&f);
    fluxo_botao(&f, z->v.sel >= 0 ? "Editar tarefa" : "Editar tarefa...", S_EDITAR, p, B_NORMAL);
    fluxo_botao(&f, "+ Tarefa", S_NOVA, p, B_NORMAL);
    fluxo_botao(&f, "Algoritmo", S_ALG, p, B_NORMAL);
    fluxo_botao(&f, "Quantum", S_QUANTUM, p, B_NORMAL);
    fluxo_botao(&f, "CPUs", S_CPUS, p, B_NORMAL);
    fluxo_espaco(&f);
    fluxo_botao(&f, "Exportar SVG", S_EXPORTAR, true, B_NORMAL);
    fluxo_botao(&f, "Estatísticas", S_ESTAT, true, B_NORMAL);
    fluxo_botao(&f, "Ajuda", S_AJUDA, true, B_NORMAL);
    fluxo_botao(&f, "Fechar simulação", S_SAIR, true, B_NORMAL);
    y = f.y + ALT_BOTAO + 8;

    /* mensagem do último comando, e avisos de estado */
    char estado[300] = "";
    if (!z->completo && s->atual.t < s->ncols)
        snprintf(estado, sizeof estado, "Há %d tick(s) já simulados à frente; \"Avançar\" reaplica o histórico.", s->ncols - s->atual.t);
    if (*z->msg) y += linha_mensagem(x, y, w, z->msg) + 6;
    else if (*estado) y += linha_mensagem(x, y, w, estado) + 6;

    /* tamanhos: painéis de baixo, legenda e o resto para o gráfico */
    int h_leg = gantt_gfx_legenda(&T, 0, 0, w - 24, false);
    int h_paineis = T.h / 4 < 120 ? 120 : T.h / 4 > 200 ? 200 : T.h / 4;
    int h_gantt = T.h - y - 30 - 16 - h_leg - 14 - h_paineis - 12;
    if (h_gantt < 120) {
        h_paineis -= 120 - h_gantt;
        if (h_paineis < 60) h_paineis = 60;
        h_gantt = 120;
    }

    /* faixa acima do gráfico: trecho visível, zoom, seguir */
    painel(x, y, w, 30 + h_gantt + 16 + h_leg + 12);
    int nvis = (w - 2 - z->v.lbl - 12) / z->v.cw;
    if (nvis < 1) nvis = 1;
    int agora = z->completo ? -1 : s->atual.t;
    z->total = vis.ncols + 1;
    if (z->seguir) {
        int alvo = (z->completo ? 0 : s->atual.t + 2) - nvis;
        z->v.t_ini = alvo > 0 ? alvo : 0;
    }
    int max_ini = z->total - nvis > 0 ? z->total - nvis : 0;
    if (z->v.t_ini > max_ini) z->v.t_ini = max_ini;
    if (z->v.t_ini < 0) z->v.t_ini = 0;
    char info[200];
    int ult = z->v.t_ini + nvis - 1 < vis.ncols - 1 ? z->v.t_ini + nvis - 1 : vis.ncols - 1;
    if (vis.ncols == 0) snprintf(info, sizeof info, "Gráfico de Gantt: nenhum tick executado ainda");
    else snprintf(info, sizeof info, "Gráfico de Gantt: ticks %d a %d de %d", z->v.t_ini, ult < z->v.t_ini ? z->v.t_ini : ult, vis.ncols);
    tela_texto_negrito(&T, x + 12, y + 8, info, C_TEXTO);
    int bx = x + w - 8;
    bx -= largura_botao("Zoom +");
    botao(bx, y + 2, 0, "Zoom +", S_ZOOM_MAIS, z->v.cw < 48, B_NORMAL);
    bx -= largura_botao("Zoom -") + 6;
    botao(bx, y + 2, 0, "Zoom -", S_ZOOM_MENOS, z->v.cw > 2, B_NORMAL);
    if (!z->seguir && !z->completo) {
        bx -= largura_botao("Seguir o instante atual") + 6;
        botao(bx, y + 2, 0, "Seguir o instante atual", S_SEGUIR, true, B_NORMAL);
    }

    /* o gráfico */
    z->v.x = x + 1;
    z->v.y = y + 32;
    z->v.w = w - 2;
    z->v.h = h_gantt;
    z->v.t_marca = agora;
    z->nvis = nvis;
    gantt_gfx(&T, &vis, &z->v);
    registrar(z->v.x, z->v.y, z->v.w, z->v.h, S_GANTT);
    int max_rol = z->v.alt_conteudo - z->v.h;
    if (z->v.y_rol > (max_rol > 0 ? max_rol : 0)) z->v.y_rol = max_rol > 0 ? max_rol : 0;
    if (max_rol > 0) {   /* indicador de rolagem vertical */
        int alt = z->v.h * z->v.h / z->v.alt_conteudo;
        int pos = (z->v.h - alt) * z->v.y_rol / max_rol;
        tela_ret(&T, z->v.x + z->v.w - 5, z->v.y + pos, 4, alt, 0xB0B6BF);
    }

    /* barra de rolagem horizontal (tempo) */
    z->barra_x = z->v.x + z->v.lbl;
    z->barra_y = z->v.y + z->v.h + 2;
    z->barra_w = z->v.w - z->v.lbl - 8;
    tela_ret(&T, z->barra_x, z->barra_y, z->barra_w, 12, 0xE5E7EB);
    int tot = z->total > nvis ? z->total : nvis;
    z->polegar_w = z->barra_w * nvis / tot;
    if (z->polegar_w < 30) z->polegar_w = 30;
    if (z->polegar_w > z->barra_w) z->polegar_w = z->barra_w;
    z->polegar_x = z->barra_x + (max_ini ? (z->barra_w - z->polegar_w) * z->v.t_ini / max_ini : 0);
    bool sobre = arrastando || dentro(mx, my, z->polegar_x, z->barra_y, z->polegar_w, 12);
    tela_ret(&T, z->polegar_x, z->barra_y + 1, z->polegar_w, 10, sobre ? 0x6B7280 : 0x9CA3AF);
    registrar(z->barra_x, z->barra_y, z->barra_w, 12, S_BARRA);
    registrar(z->polegar_x, z->barra_y, z->polegar_w, 12, S_POLEGAR);

    /* legenda (req. 3: sempre visível junto do gráfico) */
    gantt_gfx_legenda(&T, x + 12, z->barra_y + 18, w - 24, true);

    /* painéis de baixo: tick inspecionado | tarefa escolhida ou sistema */
    int py = T.h - h_paineis - 12;
    int we = (w - 10) * 45 / 100, wd = w - 10 - we;
    painel(x, py, we, h_paineis);
    painel(x + we + 10, py, wd, h_paineis);
    Linhas L = {0};
    linhas_tick(z, &vis, &L);
    desenhar_linhas(x + 12, py + 8, we - 24, h_paineis - 16, &L, &z->rol_esq);
    lin_liberar(&L);
    if (z->v.sel >= 0 && z->v.sel < s->atual.n) {
        linhas_tarefa(z, z->v.sel, &L);
        int wv = largura_botao("Ver o sistema");
        botao(x + we + 10 + wd - 12 - wv, py + h_paineis - ALT_BOTAO - 8, wv, "Ver o sistema", S_VER_SISTEMA, true, B_NORMAL);
    } else {
        linhas_sistema(z, &L);
    }
    desenhar_linhas(x + we + 22, py + 8, wd - 24, h_paineis - 16, &L, &z->rol_dir);
    lin_liberar(&L);
}

/* Modo (a) passo a passo ou (b) execução completa, na janela. */
static void tela_simulacao(const Config *cfg, const OpcoesUI *op, bool completo) {
    Sessao *z = xcalloc(1, sizeof *z);
    z->cfg = cfg;
    z->op = op;
    z->completo = completo;
    z->v.cw = 16;
    z->v.sel = -1;
    z->v.lbl = 80;
    z->seguir = true;
    z->tick_insp = -1;
    config_padrao(&z->extras);
    ui_caminho_svg(cfg, op, z->caminho_svg, sizeof z->caminho_svg);
    sim_iniciar(&z->s, cfg, !completo, op->semente);
    if (completo) {
        /* Simula tudo de uma vez, sem histórico, e já gera o SVG (req. 2.4). */
        const char *motivo = sim_rodar_ate_fim(&z->s);
        exportar(z, z->caminho_svg);
        if (motivo) {
            char m[1600];
            fmsg(m, sizeof m, "erro: a simulação parou antes do fim: %s. %s", motivo, z->msg);
            snprintf(z->msg, sizeof z->msg, "%s", m);
        }
        z->exportado = true;
        z->seguir = false;
        int alvo_cw = (T.w - 140) / (z->s.ncols + 1);   /* tenta caber tudo */
        z->v.cw = alvo_cw >= 16 ? 16 : alvo_cw >= 8 ? 8 : alvo_cw >= 4 ? 4 : 2;
    }

    while (!fechar) {
        /* Ao chegar ao fim, gera a imagem automaticamente (req. 2.4); depois
         * de uma edição o futuro muda, então exporta de novo no novo fim. */
        if (!completo && z->s.atual.fim && !z->exportado && z->s.atual.t == z->s.ncols) {
            char m2[sizeof z->msg];
            exportar(z, z->caminho_svg);
            snprintf(m2, sizeof m2, "%s", z->msg);
            fmsg(z->msg, sizeof z->msg, "Simulação concluída no instante %d. %s", z->s.atual.t, m2);
            z->exportado = true;
        }
        desenhar_simulacao(z);
        GfxEvento ev;
        esperar(&ev);
        if (fechar) break;
        int a = 0;
        if (ev.tipo == GE_TECLA) {
            int t = ev.tecla;
            if (t == GK_DIR || t == GK_ENTER || t == ' ' || t == 'n') a = S_AVANCAR;
            else if (t == GK_ESQ || t == 'p') a = S_VOLTAR;
            else if (t == GK_INICIO) a = S_INICIO;
            else if (t == GK_FIM || t == 'r') a = S_FIM;
            else if (t == 'g') a = S_IR;
            else if (t == 'e') a = S_EDITAR;
            else if (t == '+') a = S_NOVA;
            else if (t == 'a') a = S_ALG;
            else if (t == 'k') a = S_QUANTUM;
            else if (t == 'c') a = S_CPUS;
            else if (t == 'x') a = S_EXPORTAR;
            else if (t == 's') a = S_ESTAT;
            else if (t == 'h') a = S_AJUDA;
            else if (t == GK_ESC || t == 'q') a = S_SAIR;
            else if (t == GK_PGCIMA || t == GK_PGBAIXO) {
                z->seguir = false;
                z->v.t_ini += (t == GK_PGBAIXO ? 1 : -1) * (z->nvis * 3 / 4 > 1 ? z->nvis * 3 / 4 : 1);
                if (z->v.t_ini < 0) z->v.t_ini = 0;
            }
            if (a && !acao_simulacao(z, a)) break;
            if (a) continue;
        }
        if (ev.tipo == GE_RODA) {
            if (dentro(ev.x, ev.y, z->v.x, z->v.y, z->v.w, z->v.h) || dentro(ev.x, ev.y, z->barra_x, z->barra_y, z->barra_w, 12)) {
                /* Como num navegador: se o gráfico não cabe na altura, a roda
                 * rola para cima/baixo; senão (ou com Shift), anda no tempo. */
                bool vertical = z->v.alt_conteudo > z->v.h && !(ev.mod & GM_SHIFT);
                if (vertical) {
                    z->v.y_rol -= ev.delta * ALT_BOTAO;
                    if (z->v.y_rol < 0) z->v.y_rol = 0;
                } else {
                    z->seguir = false;
                    int passo = z->nvis / 8 > 1 ? z->nvis / 8 : 1;
                    z->v.t_ini -= ev.delta * passo;
                    if (z->v.t_ini < 0) z->v.t_ini = 0;
                }
            } else {
                rolar(&ev, 2);
            }
            continue;
        }
        if (ev.tipo == GE_SOLTOU) {
            arrastando = false;
            continue;
        }
        if (ev.tipo == GE_MOVEU && arrastando) {
            int max_ini = z->total - z->nvis > 0 ? z->total - z->nvis : 0;
            int faixa = z->barra_w - z->polegar_w;
            if (faixa > 0) z->v.t_ini = (ev.x - z->arrasto_dx - z->barra_x) * max_ini / faixa;
            if (z->v.t_ini < 0) z->v.t_ini = 0;
            z->seguir = false;
            continue;
        }
        if (ev.tipo != GE_CLIQUE) continue;
        a = alvo_em(ev.x, ev.y);
        if (a == S_POLEGAR) {
            arrastando = true;
            z->arrasto_dx = ev.x - z->polegar_x;
        } else if (a == S_BARRA) {   /* clique no trilho: anda uma página */
            z->seguir = false;
            int pag = z->nvis * 3 / 4 > 1 ? z->nvis * 3 / 4 : 1;
            z->v.t_ini += ev.x < z->polegar_x ? -pag : pag;
            if (z->v.t_ini < 0) z->v.t_ini = 0;
        } else if (a == S_GANTT) {
            /* clique no gráfico: escolhe a tarefa da linha e o tick da coluna */
            Simulador vis = visivel(z);
            int i = gantt_gfx_tarefa_em(&vis, &z->v, ev.x, ev.y);
            if (i >= 0) {
                z->v.sel = i == z->v.sel ? -1 : i;
                z->rol_dir = 0;
            }
            int x0 = z->v.x + z->v.lbl;
            if (ev.x >= x0) {
                int t = z->v.t_ini + (ev.x - x0) / z->v.cw;
                if (t < vis.ncols) {
                    z->tick_insp = t;
                    z->rol_esq = 0;
                }
            }
        } else if (a) {
            z->msg[0] = '\0';
            if (!acao_simulacao(z, a)) break;
        }
    }
    arrastando = false;
    sim_liberar(&z->s);
    config_liberar(&z->extras);
    free(z);
}

/* ================================================== 6. entrada */

bool ui_gui_iniciar(char *erro, int tam_erro) {
    int w, h;
    if (!gfx_abrir("Simulador de escalonamento RM / EDF - projetoSO", 1200, 800, &w, &h, erro, tam_erro)) return false;
    tela_redimensionar(&T, w, h);
    return true;
}

int ui_gui_executar(const char *arquivo, const OpcoesUI *op) {
    Config cfg;
    config_padrao(&cfg);
    bool tem = false;
    if (arquivo) {
        Config lido;
        if (config_carregar(arquivo, &lido)) {
            cfg = lido;
            tem = true;
        } else {
            tem = tela_arquivo(&cfg, false, &lido);
            config_liberar(&lido);
        }
    } else {
        tem = tela_arquivo(&cfg, false, NULL);
    }
    while (tem && !fechar) {
        int m = tela_config(&cfg);
        if (!m || fechar) break;
        tela_simulacao(&cfg, op, m == 'b');
    }
    config_liberar(&cfg);
    gfx_fechar();
    tela_liberar(&T);
    free(fundo_modal);
    fundo_modal = NULL;
    return 0;
}
