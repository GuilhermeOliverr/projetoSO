/*
 * gfx.h - janela gráfica mínima, escrita à mão para cada sistema, SEM
 * biblioteca de interface (requisito G5: nada para instalar).
 *
 * A ideia: toda a interface é desenhada em memória (tela.c), num vetor de
 * pixels 0x00RRGGBB, e este módulo só sabe três coisas:
 *   1. abrir uma janela;
 *   2. copiar o vetor de pixels para dentro dela;
 *   3. entregar teclado, mouse, redimensionamento e fechamento como eventos.
 *
 * Cada sistema tem a sua implementação, e o Makefile compila todas (cada
 * arquivo só tem conteúdo no sistema dele, por #ifdef):
 *   - Linux/WSL: gfx_x11.c   - fala o protocolo X11 direto no socket do
 *                              servidor gráfico (que já vem no sistema);
 *   - Windows:   gfx_win32.c - API Win32 (user32/gdi32, parte do Windows);
 *   - macOS:     gfx_cocoa.c - Cocoa via runtime do Objective-C, em C puro.
 */
#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GE_NADA,
    GE_TECLA,     /* tecla: `tecla` tem o caractere (Unicode) ou um GK_*   */
    GE_CLIQUE,    /* botão esquerdo apertado em (x, y)                     */
    GE_SOLTOU,    /* botão esquerdo solto                                  */
    GE_MOVEU,     /* mouse se moveu (com ou sem botão apertado)            */
    GE_RODA,      /* roda do mouse: delta +1 = para cima, -1 = para baixo  */
    GE_TAMANHO,   /* janela redimensionada para w x h                      */
    GE_EXPOR,     /* o sistema pediu para redesenhar (janela descoberta)   */
    GE_FECHAR     /* usuário clicou no X da janela                         */
} TipoGfxEvento;

/* Teclas especiais. Começam acima do maior código Unicode (0x10FFFF) para
 * nunca se confundirem com um caractere digitado. */
enum {
    GK_ENTER = 0x110000, GK_ESC, GK_BACKSPACE, GK_DELETE, GK_TAB,
    GK_ESQ, GK_DIR, GK_CIMA, GK_BAIXO, GK_INICIO, GK_FIM, GK_PGCIMA, GK_PGBAIXO
};

/* Modificadores (campo `mod`). */
enum { GM_SHIFT = 1, GM_CTRL = 2 };

typedef struct {
    TipoGfxEvento tipo;
    int x, y;     /* posição do mouse, em pixels da imagem (0,0 = canto superior esquerdo) */
    int tecla;
    int delta;
    int mod;
    int w, h;
} GfxEvento;

/* Abre a janela. `w_real`/`h_real` recebem o tamanho efetivo da área de
 * desenho (pode diferir do pedido). Devolve false com o motivo em `erro`
 * (ex.: não há servidor gráfico); aí o programa usa o terminal. */
bool gfx_abrir(const char *titulo, int w, int h, int *w_real, int *h_real, char *erro, int tam_erro);

/* Mostra a imagem `pix` (w x h, linha a linha, 0x00RRGGBB) na janela. */
void gfx_mostrar(const uint32_t *pix, int w, int h);

/* Espera um evento por até `timeout_ms` (-1 = sem limite, 0 = só olha).
 * Devolve false se o tempo acabou sem evento. */
bool gfx_esperar(GfxEvento *ev, int timeout_ms);

void gfx_fechar(void);

/* Unidades de disco (C:, D:, pendrives...) para o navegador de arquivos.
 * Só existem no Windows; nos outros sistemas devolve 0 (lá os pendrives
 * aparecem como pastas, ex.: /media/<usuário> ou /Volumes). */
typedef struct {
    char raiz[4];     /* "E:"                       */
    char tipo[32];    /* "pendrive", "disco local"... */
} GfxUnidade;
int gfx_unidades(GfxUnidade *u, int max);

#endif
