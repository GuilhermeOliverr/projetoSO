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

// Início da enumeração dos tipos de eventos de entrada e estado da janela gráfica
typedef enum {
    GE_NADA,      // 0: Nenhum evento pendente na fila no momento da verificação
    GE_TECLA,     // 1: Evento de teclado: uma tecla alfanumérica ou especial foi pressionada
    GE_CLIQUE,    // 2: Evento de mouse: botão esquerdo do mouse foi pressionado na posição (x, y)
    GE_SOLTOU,    // 3: Evento de mouse: botão esquerdo do mouse foi solto
    GE_MOVEU,     // 4: Evento de mouse: cursor foi movimentado dentro da janela
    GE_RODA,      // 5: Evento de rolagem do mouse (scroll wheel: +1 para cima, -1 para baixo)
    GE_TAMANHO,   // 6: Evento de redimensionamento da janela pelo usuário para largura 'w' e altura 'h'
    GE_EXPOR,     // 7: Evento de exposição/redesenho (janela foi descoberta ou focada pelo SO)
    GE_FECHAR     // 8: Evento de fechamento (usuário clicou no botão "X" para encerrar a janela)
} TipoGfxEvento;

// Início da enumeração dos códigos para mapeamento de teclas especiais do teclado
enum {
    GK_ENTER = 0x110000,     // Código especial para a tecla Enter (definido acima de 0x10FFFF para não conflitar com Unicode) 
    GK_ESC,     // Código especial para a tecla ESC (utilizada para cancelar ações ou fechar menus) 
    GK_BACKSPACE,     // Código especial para a tecla Backspace (apagar caractere à esquerda) 
    GK_DELETE,     // Código especial para a tecla Delete (apagar caractere à direita) 
    GK_TAB,     // Código especial para a tecla Tab (navegação entre campos) 
    GK_ESQ,     // Código especial para a seta direcional Esquerda 
    GK_DIR,     // Código especial para a seta direcional Direita 
    GK_CIMA,     // Código especial para a seta direcional Cima 
    GK_BAIXO,     // Código especial para a seta direcional Baixo 
    GK_INICIO,     // Código especial para a tecla Home / Início 
    GK_FIM,     // Código especial para a tecla End / Fim 
    GK_PGCIMA,     // Código especial para a tecla Page Up / Mover página para cima 
    GK_PGBAIXO     // Código especial para a tecla Page Down / Mover página para baixo
};

// Enumeração das máscaras de bits para identificação de teclas modificadoras (Shift ou Ctrl ativas)
enum { GM_SHIFT = 1, GM_CTRL = 2 };

// Estrutura que encapsula todos os dados relativos a um evento da janela
typedef struct {
    TipoGfxEvento tipo;    // Categoria do evento ocorrido (GE_TECLA, GE_CLIQUE, GE_FECHAR, etc.)
    int x, y;     // Posição (x, y) do cursor do mouse em pixels relativos ao canto superior esquerdo da imagem
    int tecla;    // Código do caractere digitado (Unicode) ou constante de tecla especial (GK_*)
    int delta;    // Deslocamento da roda do mouse (+1 rolagem para cima, -1 rolagem para baixo)
    int mod;    // Estado das teclas modificadoras ativas no momento do evento (GM_SHIFT, GM_CTRL ou 0)
    int w, h;    // Nova largura 'w' e nova altura 'h' informadas quando ocorre o evento GE_TAMANHO
} GfxEvento;

// Inicializa e abre a janela gráfica nativa com o título e dimensões desejadas. (Devolve false com o motivo em `erro` (ex.: não há servidor gráfico); aí o programa usa o terminal.)
bool gfx_abrir(const char *titulo, int w, int h, int *w_real, int *h_real, char *erro, int tam_erro);

// Transfere o buffer de pixels 0x00RRGGBB da memória RAM para ser renderizado dentro da janela gráfica
void gfx_mostrar(const uint32_t *pix, int w, int h);

// Processa a fila de mensagens do SO e aguarda por um evento de entrada por até 'timeout_ms' milissegundos
bool gfx_esperar(GfxEvento *ev, int timeout_ms);

// Destrói a janela gráfica nativa e libera todos os recursos gráficos alocados no SO
void gfx_fechar(void);

// Estrutura para descrever unidades de disco do sistema (utilizada no navegador de arquivos)
typedef struct {
    char raiz[4];     // String contendo a letra da unidade de disco (exemplo: "C:", "D:", "E:")
    char tipo[32];    // String descrevendo a categoria da unidade (exemplo: "disco local", "pendrive")
} GfxUnidade;

// Preenche o vetor 'u' com as unidades de disco detectadas no Windows e retorna a quantidade encontrada
int gfx_unidades(GfxUnidade *u, int max);

#endif
