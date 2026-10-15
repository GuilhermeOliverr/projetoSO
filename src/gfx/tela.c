/*
 * tela.c - desenho em memória (ver tela.h).
 *
 * Tudo aqui escreve direto no vetor de pixels, sempre passando por
 * `ponto`/`faixa`, que respeitam o recorte. Assim nenhuma função precisa se
 * preocupar em sair da imagem.
 */
#include "tela.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

extern const unsigned char g_fonte8x16[224][16];

void tela_redimensionar(Tela *t, int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w != t->w || h != t->h || !t->pix) {
        free(t->pix);
        t->pix = xmalloc((size_t)w * (size_t)h * sizeof(uint32_t));
        t->w = w;
        t->h = h;
    }
    tela_sem_recorte(t);
}

void tela_liberar(Tela *t) {
    free(t->pix);
    t->pix = NULL;
    t->w = t->h = 0;
}

void tela_recorte(Tela *t, int x, int y, int w, int h) {
    t->cx0 = x < 0 ? 0 : x;
    t->cy0 = y < 0 ? 0 : y;
    t->cx1 = x + w > t->w ? t->w : x + w;
    t->cy1 = y + h > t->h ? t->h : y + h;
}

void tela_sem_recorte(Tela *t) {
    t->cx0 = t->cy0 = 0;
    t->cx1 = t->w;
    t->cy1 = t->h;
}

static void ponto(Tela *t, int x, int y, uint32_t cor) {
    if (x >= t->cx0 && x < t->cx1 && y >= t->cy0 && y < t->cy1) t->pix[(size_t)y * t->w + x] = cor;
}

/* Linha horizontal de x0 até x1-1 (já recortada). */
static void faixa(Tela *t, int x0, int x1, int y, uint32_t cor) {
    if (y < t->cy0 || y >= t->cy1) return;
    if (x0 < t->cx0) x0 = t->cx0;
    if (x1 > t->cx1) x1 = t->cx1;
    uint32_t *p = &t->pix[(size_t)y * t->w];
    for (int x = x0; x < x1; x++) p[x] = cor;
}

void tela_ret(Tela *t, int x, int y, int w, int h, uint32_t cor) {
    for (int j = y; j < y + h; j++) faixa(t, x, x + w, j, cor);
}

void tela_moldura(Tela *t, int x, int y, int w, int h, uint32_t cor) {
    if (w <= 0 || h <= 0) return;
    faixa(t, x, x + w, y, cor);
    faixa(t, x, x + w, y + h - 1, cor);
    tela_ret(t, x, y, 1, h, cor);
    tela_ret(t, x + w - 1, y, 1, h, cor);
}

/* Borda com traços de 3 pixels e espaços de 2, como o "pronta" do SVG. */
void tela_tracejado(Tela *t, int x, int y, int w, int h, uint32_t cor) {
    for (int i = 0; i < w; i++)
        if (i % 5 < 3) {
            ponto(t, x + i, y, cor);
            ponto(t, x + i, y + h - 1, cor);
        }
    for (int j = 0; j < h; j++)
        if (j % 5 < 3) {
            ponto(t, x, y + j, cor);
            ponto(t, x + w - 1, y + j, cor);
        }
}

/* Suspensa (req. 2.1): fundo branco com pontos pretos a cada 3 pixels e
 * borda preta, como no SVG. */
void tela_pontilhado(Tela *t, int x, int y, int w, int h) {
    tela_ret(t, x, y, w, h, 0xFFFFFF);
    for (int j = 1; j < h; j += 3)
        for (int i = 1 + (j / 3) % 2; i < w; i += 3) ponto(t, x + i, y + j, 0x000000);
    tela_moldura(t, x, y, w, h, 0x000000);
}

/* CPU desligada: listras diagonais cinza sobre fundo claro. */
void tela_hachurado(Tela *t, int x, int y, int w, int h) {
    tela_ret(t, x, y, w, h, 0xEEEEEE);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if ((x + i + y + j) % 6 < 2) ponto(t, x + i, y + j, 0xAAAAAA);
    tela_moldura(t, x, y, w, h, 0x999999);
}

/* Bresenham: anda pelo eixo mais longo e decide pelo erro acumulado quando
 * dar um passo no outro. A espessura é um quadrado pintado em cada ponto. */
void tela_linha(Tela *t, int x0, int y0, int x1, int y1, int esp, uint32_t cor) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int a = esp / 2;
    for (;;) {
        if (esp <= 1) ponto(t, x0, y0, cor);
        else tela_ret(t, x0 - a, y0 - a, esp, esp, cor);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Triângulo cheio: para cada linha y, calcula onde as três arestas cruzam
 * essa linha e pinta entre o menor e o maior cruzamento. */
void tela_triangulo(Tela *t, int x0, int y0, int x1, int y1, int x2, int y2, uint32_t cor) {
    int xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    int ymin = y0, ymax = y0;
    for (int i = 1; i < 3; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    for (int y = ymin; y <= ymax; y++) {
        double esq = 1e9, dir = -1e9;
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            int ya = ys[i], yb = ys[j];
            if (ya == yb) {
                if (y == ya) {
                    double a = xs[i] < xs[j] ? xs[i] : xs[j], b = xs[i] < xs[j] ? xs[j] : xs[i];
                    if (a < esq) esq = a;
                    if (b > dir) dir = b;
                }
                continue;
            }
            if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
            double x = xs[i] + (double)(y - ya) * (xs[j] - xs[i]) / (yb - ya);
            if (x < esq) esq = x;
            if (x > dir) dir = x;
        }
        if (esq <= dir) faixa(t, (int)(esq + 0.5), (int)(dir + 0.5) + 1, y, cor);
    }
}

void tela_circulo(Tela *t, int cx, int cy, int r, uint32_t cor) {
    for (int j = -r; j <= r; j++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + j * j <= r * r) dx++;
        faixa(t, cx - dx, cx + dx + 1, cy + j, cor);
    }
}

/* Mistura cada pixel com cinza escuro (fica ~45% do brilho original). */
void tela_escurecer(Tela *t) {
    size_t n = (size_t)t->w * t->h;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = t->pix[i];
        uint32_t r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
        t->pix[i] = ((r * 45 / 100 + 10) << 16) | ((g * 45 / 100 + 12) << 8) | (b * 45 / 100 + 16);
    }
}

/* --------------------------------------------------------------- UTF-8 */

/* Decodifica um caractere UTF-8. Bytes inválidos viram '?' e avançam 1,
 * para nunca travar num texto estragado. */
unsigned utf8_proximo(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = p[0];
    int n = 0;
    if (c < 0x80) n = 0;
    else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
    else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
    else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
    else { *s += 1; return '?'; }
    for (int i = 1; i <= n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += 1; return '?'; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *s += n + 1;
    return c;
}

int utf8_escrever(char *d, unsigned c) {
    if (c < 0x80) { d[0] = (char)c; return 1; }
    if (c < 0x800) { d[0] = (char)(0xC0 | (c >> 6)); d[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000) {
        d[0] = (char)(0xE0 | (c >> 12));
        d[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        d[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    d[0] = (char)(0xF0 | (c >> 18));
    d[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    d[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    d[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* --------------------------------------------------------------- texto */

/* A fonte só tem U+0020..U+00FF. Os poucos símbolos fora disso que o
 * programa usa nas mensagens viram um equivalente próximo. */
static unsigned glifo(unsigned c) {
    if (c >= 0x20 && c <= 0xFF) return c;
    switch (c) {
    case 0x2014: case 0x2013: case 0x2212: return '-';   /* travessões, menos */
    case 0x2264: return '<';                              /* menor ou igual    */
    case 0x2265: return '>';
    case 0x2192: return '>';                              /* seta              */
    case 0x03A3: return 'S';                              /* sigma             */
    case 0x2026: return '.';
    default: return '?';
    }
}

static void caractere(Tela *t, int x, int y, unsigned c, uint32_t cor) {
    const unsigned char *g = g_fonte8x16[glifo(c) - 0x20];
    /* Pula rápido se o caractere está todo fora do recorte. */
    if (x + FONTE_L <= t->cx0 || x >= t->cx1 || y + FONTE_A <= t->cy0 || y >= t->cy1) return;
    for (int j = 0; j < FONTE_A; j++)
        for (int i = 0; i < FONTE_L; i++)
            if (g[j] & (0x80 >> i)) ponto(t, x + i, y + j, cor);
}

int tela_texto(Tela *t, int x, int y, const char *s, uint32_t cor) {
    int x0 = x;
    while (*s) {
        caractere(t, x, y, utf8_proximo(&s), cor);
        x += FONTE_L;
    }
    return x - x0;
}

/* Negrito "de máquina de escrever": o mesmo texto 1 pixel à direita. */
int tela_texto_negrito(Tela *t, int x, int y, const char *s, uint32_t cor) {
    tela_texto(t, x + 1, y, s, cor);
    return tela_texto(t, x, y, s, cor) + 1;
}

int texto_largura(const char *s) {
    int n = 0;
    while (*s) {
        utf8_proximo(&s);
        n++;
    }
    return n * FONTE_L;
}

int texto_bytes_ate(const char *s, int largura) {
    const char *p = s;
    int w = 0;
    while (*p && w + FONTE_L <= largura) {
        utf8_proximo(&p);
        w += FONTE_L;
    }
    return (int)(p - s);
}
