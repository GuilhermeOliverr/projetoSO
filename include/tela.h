/*
 * tela.h - desenho em memória: retângulos, linhas, triângulos, círculos e
 * texto num vetor de pixels. É C puro e igual nos três sistemas; o gfx_*.c
 * de cada sistema só copia o resultado para a janela.
 *
 * Coordenadas em pixels, (0,0) no canto superior esquerdo, y para baixo.
 * Cores em 0xRRGGBB, o mesmo formato das cores das tarefas.
 */
#ifndef TELA_H
#define TELA_H

#include <stdint.h>

/* Fonte de largura fixa embutida (fonte.c): cada caractere tem 8 x 16. */
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

int utf8_escrever(char *dst, unsigned c);    // Codifica o codepoint 'c' em formato UTF-8 na string 'dst' e retorna a quantidade de bytes gravados

#endif
