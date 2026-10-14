/*
 * util.c - implementação das funções auxiliares (ver util.h).
 *
 * Nada aqui é "sistema operacional": são ferramentas pequenas (alocar
 * memória, ler números, pintar o terminal, sortear) usadas por todo o resto.
 */
/* Precisamos de isatty/fileno (POSIX) para saber se a saída é um terminal. */
#define _POSIX_C_SOURCE 200809L
/* No macOS, TIOCGWINSZ some com _POSIX_C_SOURCE se não pedirmos as
 * extensões do Darwin. */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* Liga/desliga as cores ANSI. main.c desliga quando a saída não é um
 * terminal ou quando o usuário passa --sem-cor. */
bool g_usar_cores = true;

/* ------------------------------------------------------------ memória */
/* As quatro funções x* são o malloc/calloc/realloc/strdup que ENCERRAM o
 * programa com mensagem clara se faltar memória. O `n ? n : 1` evita pedir
 * 0 bytes, caso em que malloc pode devolver NULL mesmo sem estar sem memória
 * (comportamento definido pela implementação). */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente (%zu bytes)\n", n);
        exit(2);
    }
    return p;
}

void *xcalloc(size_t n, size_t tam) {
    void *p = calloc(n ? n : 1, tam ? tam : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente\n");
        exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente (%zu bytes)\n", n);
        exit(2);
    }
    return p;
}

/* Copia uma string para memória nova (strdup não existe em C11 puro). O +1
 * é o '\0' final. */
char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------- texto */

/* Tira espaços do começo e do fim, no próprio buffer. Passos:
 *   1. `ini` avança até o 1º caractere que não é espaço;
 *   2. `fim` recua a partir do final até o último que não é espaço;
 *   3. coloca '\0' depois dele e, se houve espaços no início, desloca o
 *      texto para o começo do buffer (memmove aceita regiões sobrepostas).
 * O cast para unsigned char é exigido por isspace: passar um char negativo
 * (acento em UTF-8) seria comportamento indefinido. */
char *str_trim(char *s) {
    char *ini = s;
    while (*ini && isspace((unsigned char)*ini)) ini++;
    char *fim = ini + strlen(ini);
    while (fim > ini && isspace((unsigned char)fim[-1])) fim--;
    *fim = '\0';
    if (ini != s) memmove(s, ini, (size_t)(fim - ini) + 1);
    return s;
}

/* Compara ignorando maiúsculas/minúsculas (req. 3.3.2: "RM", "rm" e "Rm"
 * são a mesma coisa). Percorre as duas ao mesmo tempo; se acabarem juntas
 * (*a == *b == '\0') são iguais. */
bool str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

/* Converte texto em inteiro de forma ESTRITA. Por que não usar só atoi?
 * Porque atoi("12abc") devolve 12 sem avisar, e atoi("abc") devolve 0.
 * Aqui só aceitamos: sinal opcional + dígitos, nada mais.
 * Retorno: 0 = ok, -1 = não é número, -2 = número grande demais para um int. */
int parse_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (!*p) return -1;                       /* só o sinal, sem dígitos */
    for (const char *q = p; *q; q++)
        if (!isdigit((unsigned char)*q)) return -1;
    errno = 0;
    long v = strtol(s, NULL, 10);
    if (errno == ERANGE || v > INT_MAX || v < INT_MIN) return -2;
    *out = (int)v;
    return 0;
}

/* Lê uma cor em hexadecimal: "RRGGBB", "#RRGGBB" ou "0xRRGGBB".
 * Cada par de dígitos é um canal (vermelho, verde, azul) de 0 a 255.
 * Montamos o número deslocando 4 bits por dígito: v = v*16 + dígito. */
bool parse_cor(const char *s, unsigned *out) {
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (strlen(s) != 6) return false;
    unsigned v = 0;
    for (int i = 0; i < 6; i++) {
        int c = tolower((unsigned char)s[i]);
        if (c >= '0' && c <= '9') v = v * 16 + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned)(c - 'a' + 10);
        else return false;
    }
    *out = v;
    return true;
}

/* Lê uma linha do teclado. Devolve false só no EOF (Ctrl+D ou entrada
 * redirecionada que acabou), para os menus poderem encerrar em vez de ficar
 * em laço infinito. */
bool ler_linha(const char *prompt, char *buf, size_t tam) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);   /* sem o flush o prompt pode aparecer depois */
    }
    if (!fgets(buf, (int)tam, stdin)) {
        buf[0] = '\0';
        return false;
    }
    /* Se a linha não coube no buffer, descarta o resto para não "vazar"
     * para o próximo comando. (Se coube, o fgets guardou o '\n'.) */
    if (!strchr(buf, '\n')) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    str_trim(buf);
    return true;
}

/* ---------------------------------------------------------------- terminal */
/* Cores ANSI: "\x1b[" é o caractere ESC seguido de um código que o terminal
 * entende. 38;2;R;G;B = cor de texto RGB de 24 bits; 48;2;R;G;B = cor de
 * fundo; 0 = volta ao normal; 1 = negrito; 2 = esmaecido. Tudo vira
 * no-op se as cores estiverem desligadas. */

void term_fg(unsigned rgb) {
    if (g_usar_cores)
        printf("\x1b[38;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

void term_bg(unsigned rgb) {
    if (g_usar_cores)
        printf("\x1b[48;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

void term_reset(void) {
    if (g_usar_cores) fputs("\x1b[0m", stdout);
}

void term_negrito(void) {
    if (g_usar_cores) fputs("\x1b[1m", stdout);
}

void term_fraco(void) {
    if (g_usar_cores) fputs("\x1b[2m", stdout);
}

void term_limpar(void) {
    /* Sem cores assumimos saída não interativa (arquivo, pipe): não faz
     * sentido limpar, e as sequências sujariam o texto. */
    if (g_usar_cores) fputs("\x1b[H\x1b[2J\x1b[3J", stdout);
}

/* Largura do terminal em colunas, para o Gantt saber quantos ticks cabem
 * numa linha. Tenta (1) perguntar ao terminal via ioctl, (2) a variável
 * COLUMNS, (3) assume 100. Valores <= 20 são descartados por serem absurdos. */
int term_largura(void) {
#ifndef _WIN32
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) return ws.ws_col;
#endif
    const char *c = getenv("COLUMNS");
    int v;
    if (c && parse_int(c, &v) == 0 && v > 20) return v;
    return 100;
}

/* Escolhe preto ou branco para escrever sobre uma cor de fundo, calculando o
 * brilho percebido: o olho humano acha o verde mais claro que o azul, por
 * isso os pesos 299/587/114 (padrão ITU-R BT.601). Fundo claro -> texto
 * preto; fundo escuro -> texto branco. */
unsigned cor_contraste(unsigned rgb) {
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    /* Pesos aproximados da luminância (ITU-R BT.601). */
    unsigned lum = (299 * r + 587 * g + 114 * b) / 1000;
    return lum > 140 ? 0x000000 : 0xFFFFFF;
}

/* --------------------------------------------------------------------- rng */

/* Gerador pseudoaleatório xorshift64*. Cada chamada embaralha os bits do
 * estado com deslocamentos e XOR (>>12, <<25, >>27) e multiplica por uma
 * constante para espalhar melhor. O estado é um número de 64 bits que
 * fica DENTRO do snapshot da simulação, por isso voltar e avançar repete os
 * mesmos sorteios. Estado 0 travaria o gerador (0 xor 0 = 0), então
 * trocamos por uma constante. */
uint32_t rng_proximo(uint64_t *estado) {
    /* xorshift64*: simples, rápido e com estado de 64 bits fácil de copiar. */
    uint64_t x = *estado ? *estado : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *estado = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

/* Inteiro sorteado em [0, n). O `%` introduz um viés minúsculo quando n não
 * divide 2^32, irrelevante para sortear entre poucas tarefas. */
int rng_intervalo(uint64_t *estado, int n) {
    return (int)(rng_proximo(estado) % (uint32_t)n);
}
