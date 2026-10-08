/*
 * util.c - implementação das funções auxiliares (ver util.h).
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

bool g_usar_cores = true;

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

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

char *str_trim(char *s) {
    char *ini = s;
    while (*ini && isspace((unsigned char)*ini)) ini++;
    char *fim = ini + strlen(ini);
    while (fim > ini && isspace((unsigned char)fim[-1])) fim--;
    *fim = '\0';
    if (ini != s) memmove(s, ini, (size_t)(fim - ini) + 1);
    return s;
}

bool str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

int parse_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (!*p) return -1;
    for (const char *q = p; *q; q++)
        if (!isdigit((unsigned char)*q)) return -1;
    errno = 0;
    long v = strtol(s, NULL, 10);
    if (errno == ERANGE || v > INT_MAX || v < INT_MIN) return -2;
    *out = (int)v;
    return 0;
}

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

bool ler_linha(const char *prompt, char *buf, size_t tam) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
    if (!fgets(buf, (int)tam, stdin)) {
        buf[0] = '\0';
        return false;
    }
    /* Se a linha não coube no buffer, descarta o resto para não "vazar"
     * para o próximo comando. */
    if (!strchr(buf, '\n')) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    str_trim(buf);
    return true;
}

/* ---------------------------------------------------------------- terminal */

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

unsigned cor_contraste(unsigned rgb) {
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    /* Pesos aproximados da luminância (ITU-R BT.601). */
    unsigned lum = (299 * r + 587 * g + 114 * b) / 1000;
    return lum > 140 ? 0x000000 : 0xFFFFFF;
}

/* --------------------------------------------------------------------- rng */

uint32_t rng_proximo(uint64_t *estado) {
    /* xorshift64*: simples, rápido e com estado de 64 bits fácil de copiar. */
    uint64_t x = *estado ? *estado : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *estado = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

int rng_intervalo(uint64_t *estado, int n) {
    return (int)(rng_proximo(estado) % (uint32_t)n);
}
