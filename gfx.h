/*
 * gfx.h - janela gráfica e desenho em memória.
 *
 * Junta include/gfx.h e include/tela.h da versão organizada em pastas. As
 * implementações de cada sistema (seções gfx_x11.c, gfx_win32.c e
 * gfx_cocoa.c) ficam todas em gfx_plataforma.c. Cada trecho começa com um
 * divisor e com o comentário de cabeçalho original.
 */
#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>

/* ===========================================================================
 * Seção gfx.h (originalmente include/gfx.h)
 * =========================================================================== */
/*
 * gfx.h - janela gráfica mínima, escrita à mão para cada sistema, SEM
 * biblioteca de interface (requisito G5: nada para instalar).
 *
 * A ideia: toda a interface é desenhada em memória (gfx.c, seção tela.c), num vetor de
 * pixels 0x00RRGGBB, e este módulo só sabe três coisas:
 *   1. abrir uma janela;
 *   2. copiar o vetor de pixels para dentro dela;
 *   3. entregar teclado, mouse, redimensionamento e fechamento como eventos.
 *
 * Cada sistema tem a sua implementação, e o Makefile compila todas (cada
 * arquivo só tem conteúdo no sistema dele, por #ifdef):
 *   - Linux/WSL: seção gfx_x11.c   - fala o protocolo X11 direto no socket do
 *                                    servidor gráfico (que já vem no sistema);
 *   - Windows:   seção gfx_win32.c - API Win32 (user32/gdi32, parte do Windows);
 *   - macOS:     seção gfx_cocoa.c - Cocoa via runtime do Objective-C, em C puro.
 */

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

/* ===========================================================================
 * Seção tela.h (originalmente include/tela.h)
 * =========================================================================== */
/*
 * tela.h - desenho em memória: retângulos, linhas, triângulos, círculos e
 * texto num vetor de pixels. É C puro e igual nos três sistemas; o trecho de gfx_plataforma.c
 * de cada sistema só copia o resultado para a janela.
 *
 * Coordenadas em pixels, (0,0) no canto superior esquerdo, y para baixo.
 * Cores em 0xRRGGBB, o mesmo formato das cores das tarefas.
 */

/* Fonte de largura fixa embutida (gfx.c, seção fonte.c): cada caractere tem 8 x 16. */
#define FONTE_L 8
#define FONTE_A 16

// Início da estrutura 'Tela' que encapsula o buffer de pixels da janela em memória RAM
typedef struct Tela {
    uint32_t *pix;    // Ponteiro para o vetor dinâmico contendo as cores de todos os pixels (formato 0x00RRGGBB)
    int w, h;    // Resolução atual da imagem em memória: largura 'w' e altura 'h' em pixels
    int cx0, cy0, cx1, cy1;    // Coordenadas da caixa de corte (clipping box): canto superior esquerdo (cx0, cy0) e inferior direito (cx1, cy1)
} Tela;

void tela_redimensionar(Tela *t, int w, int h);    // Reagrupa e realoca o tamanho da matriz de pixels 'pix' quando a janela gráfica muda de resolução
void tela_liberar(Tela *t);    // Libera a memória dinâmica alocada para o vetor de pixels 'pix'
void tela_recorte(Tela *t, int x, int y, int w, int h);    // Define uma região retangular de corte (clipping) para que desenhos fora dela sejam ignorados
void tela_sem_recorte(Tela *t);    // Reseta a caixa de corte para abarcar a área total da tela (0, 0 até w, h)

void tela_ret(Tela *t, int x, int y, int w, int h, uint32_t cor);          // Desenha um retângulo preenchido sólido com a 'cor' especificada na posição (x, y)
void tela_moldura(Tela *t, int x, int y, int w, int h, uint32_t cor);      // Desenha apenas o contorno/borda de um retângulo com 1 pixel de espessura
void tela_tracejado(Tela *t, int x, int y, int w, int h, uint32_t cor);    // Desenha o contorno de um retângulo com linhas tracejadas na cor especificada
void tela_pontilhado(Tela *t, int x, int y, int w, int h);                 // Desenha o preenchimento em padrão preto pontilhado para tarefas no estado suspenso (Requisito 2.1)
void tela_hachurado(Tela *t, int x, int y, int w, int h);                  // Desenha o preenchimento hachurado (listras) para indicar CPUs ociosas ou desligadas no Gantt
void tela_linha(Tela *t, int x0, int y0, int x1, int y1, int espessura, uint32_t cor);    // Desenha uma linha entre (x0, y0) e (x1, y1) com a espessura e cor informadas
void tela_triangulo(Tela *t, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t cor);    // Desenha um triângulo preenchido definido por três vértices (usado em marcadores/setas)
void tela_circulo(Tela *t, int cx, int cy, int r, uint32_t cor);    // Desenha um círculo preenchido com centro em (cx, cy) e raio 'r' na cor especificada

void tela_escurecer(Tela *t);    // Aplica um filtro de escurecimento sobre todo o buffer de tela (fundo translúcido para janelas modais)

int tela_texto(Tela *t, int x, int y, const char *s, uint32_t cor);    // Renderiza a string UTF-8 's' a partir de (x, y) e retorna a largura desenhada em pixels
int tela_texto_negrito(Tela *t, int x, int y, const char *s, uint32_t cor);    // Renderiza a string UTF-8 's' em estilo negrito e retorna a largura em pixels

int texto_largura(const char *s);    // Calcula e retorna a largura total em pixels que a string 's' ocupará ao ser desenhada

int texto_bytes_ate(const char *s, int largura);    // Retorna a quantidade de bytes de 's' que cabem dentro de um limite de 'largura' em pixels (para truncamento)

unsigned utf8_proximo(const char **s);    // Decodifica o próximo caractere/codepoint UTF-8 apontado por '*s' e avança o ponteiro de leitura

int utf8_colunas(const char *s);    // Quantos caracteres 's' ocupa na tela (byte inválido conta 1); usado para alinhar ids em caracteres, não em bytes

int utf8_escrever(char *dst, unsigned c);    // Codifica o codepoint 'c' em formato UTF-8 na string 'dst' e retorna a quantidade de bytes gravados

#endif
