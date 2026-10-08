/*
 * util.h - funções auxiliares usadas por todos os módulos.
 *
 * Por que existe: o C11 puro não tem strdup, strcasecmp nem uma forma segura
 * de converter texto em inteiro com detecção de erro. Juntamos aqui essas
 * pequenas rotinas para não depender de extensões POSIX (requisito G5:
 * compilar em qualquer lugar só com a libc).
 */
#ifndef UTIL_H
#define UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Alocação que aborta com mensagem clara em vez de devolver NULL: o simulador
 * não tem como continuar sem memória, e checar NULL em cada chamada só
 * espalharia código de erro pelo projeto. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t tam);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Remove espaços/tabs/\r/\n do início e do fim, in-place. Devolve s. */
char *str_trim(char *s);

/* Comparação sem diferenciar maiúsculas/minúsculas (requisito 3.3.2). */
bool str_ieq(const char *a, const char *b);

/* Converte texto em int de forma estrita: aceita sinal opcional e dígitos,
 * nada mais. Devolve 0 se ok, -1 se não é número, -2 se estoura o int. */
int parse_int(const char *s, int *out);

/* Converte "RRGGBB", "#RRGGBB" ou "0xRRGGBB" em 0xRRGGBB. Devolve false se
 * não forem exatamente 6 dígitos hexadecimais. */
bool parse_cor(const char *s, unsigned *out);

/* Lê uma linha do teclado mostrando um prompt. Devolve false em EOF
 * (ex.: Ctrl+D ou entrada redirecionada que acabou), para que o programa
 * possa encerrar sem travar. */
bool ler_linha(const char *prompt, char *buf, size_t tam);

/* --- Terminal ------------------------------------------------------------
 * Usamos sequências ANSI (cores 24-bit) em vez de ncurses para não exigir
 * bibliotecas extras. Quando a saída não é um terminal (ex.: redirecionada
 * para arquivo nos testes) ou o usuário pede --sem-cor, as cores são
 * desligadas e a saída fica em texto puro. */
extern bool g_usar_cores;

void term_fg(unsigned rgb);          /* cor do texto */
void term_bg(unsigned rgb);          /* cor de fundo */
void term_reset(void);
void term_negrito(void);
void term_fraco(void);               /* texto esmaecido */
void term_limpar(void);              /* limpa a tela (só com cores ligadas) */
int  term_largura(void);             /* colunas do terminal (padrão 100) */

/* Escolhe preto ou branco para o texto sobre um fundo rgb, pelo brilho
 * percebido, para que o número da CPU fique legível em qualquer cor. */
unsigned cor_contraste(unsigned rgb);

/* --- Gerador pseudoaleatório ---------------------------------------------
 * Não usamos rand() porque o estado dele é global e escondido: não daria
 * para salvá-lo no snapshot. Com o estado explícito, voltar e avançar a
 * simulação sorteia sempre os mesmos valores (determinismo). */
uint32_t rng_proximo(uint64_t *estado);
int rng_intervalo(uint64_t *estado, int n);   /* inteiro em [0, n) */

#endif
