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

typedef struct Tela {
    uint32_t *pix;
    int w, h;
    /* Recorte: nada é desenhado fora de [cx0, cx1) x [cy0, cy1). Serve para
     * o Gantt não invadir os rótulos quando é rolado para os lados. */
    int cx0, cy0, cx1, cy1;
} Tela;

void tela_redimensionar(Tela *t, int w, int h);
void tela_liberar(Tela *t);
void tela_recorte(Tela *t, int x, int y, int w, int h);
void tela_sem_recorte(Tela *t);

void tela_ret(Tela *t, int x, int y, int w, int h, uint32_t cor);          /* cheio      */
void tela_moldura(Tela *t, int x, int y, int w, int h, uint32_t cor);      /* só a borda */
void tela_tracejado(Tela *t, int x, int y, int w, int h, uint32_t cor);    /* borda tracejada */
void tela_pontilhado(Tela *t, int x, int y, int w, int h);                 /* preto pontilhado (suspensa) */
void tela_hachurado(Tela *t, int x, int y, int w, int h);                  /* listras (CPU desligada)   */
void tela_linha(Tela *t, int x0, int y0, int x1, int y1, int espessura, uint32_t cor);
void tela_triangulo(Tela *t, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t cor);
void tela_circulo(Tela *t, int cx, int cy, int r, uint32_t cor);
/* Escurece a imagem toda (fundo atrás de uma janela de diálogo). */
void tela_escurecer(Tela *t);

/* Texto UTF-8 com o topo em y. Devolve a largura desenhada em pixels. */
int tela_texto(Tela *t, int x, int y, const char *s, uint32_t cor);
int tela_texto_negrito(Tela *t, int x, int y, const char *s, uint32_t cor);
/* Largura do texto em pixels (caracteres x FONTE_L). */
int texto_largura(const char *s);
/* Quantos bytes de `s` cabem em `largura` pixels (para cortar texto longo). */
int texto_bytes_ate(const char *s, int largura);

/* UTF-8: lê o próximo código de *s e avança o ponteiro. */
unsigned utf8_proximo(const char **s);
/* Escreve o código `c` em UTF-8 em `dst` (até 4 bytes). Devolve os bytes. */
int utf8_escrever(char *dst, unsigned c);

#endif
