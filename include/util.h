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

// Aloca 'n' bytes de memória com verificação automática (encerra o programa com erro claro se faltar memória)
void *xmalloc(size_t n);  

// Aloca 'n' elementos de tamanho 'tam' zerando a memória alocada, com verificação de falha
void *xcalloc(size_t n, size_t tam);

// Redimensiona o bloco de memória apontado por 'p' para 'n' bytes com verificação automática de falha
void *xrealloc(void *p, size_t n);

// Aloca memória e duplica a string 's' de forma segura (substitui a função não-padrão strdup)
char *xstrdup(const char *s);

// Remove espaços, tabulações e quebras de linha ('\\r', '\\n') do início e do fim da string 's'
char *str_trim(char *s);

// Compara se duas strings 'a' e 'b' são iguais ignorando maiúsculas e minúsculas (case-insensitive)
bool str_ieq(const char *a, const char *b);

// Converte a string 's' em inteiro em '*out' (retorna 0 se ok, -1 se inválido, -2 se estourar int)
int parse_int(const char *s, int *out);

// Converte string hexadecimal ("RRGGBB", "#RRGGBB" ou "0xRRGGBB") para um valor RGB em '*out'
bool parse_cor(const char *s, unsigned *out);

// Exibe 'prompt' e lê uma linha do teclado em 'buf' (retorna false em EOF ou interrupção)
bool ler_linha(const char *prompt, char *buf, size_t tam);

/* --- Terminal ------------------------------------------------------------
 * Usamos sequências ANSI (cores 24-bit) em vez de ncurses para não exigir
 * bibliotecas extras. Quando a saída não é um terminal (ex.: redirecionada
 * para arquivo nos testes) ou o usuário pede --sem-cor, as cores são
 * desligadas e a saída fica em texto puro. */

extern bool g_usar_cores;  // Variável global booleana que habilita ou desabilita a emissão de códigos de cores ANSI no terminal

void term_fg(unsigned rgb);         // Emite a sequência de escape ANSI para alterar a cor do texto/fonte no terminal usando cor RGB 24-bit
void term_bg(unsigned rgb);         // Emite a sequência de escape ANSI para alterar a cor de fundo das células no terminal usando cor RGB 24-bit
void term_reset(void);              // Emite a sequência de escape ANSI para restaurar as cores e estilos padrão do terminal
void term_negrito(void);            // Emite a sequência de escape ANSI para ativar o estilo de texto em negrito no terminal
void term_fraco(void);              // Emite a sequência de escape ANSI para ativar o estilo de texto esmaecido/suave no terminal
void term_limpar(void);             // Emite a sequência de escape ANSI para limpar a tela do terminal (ativo apenas se g_usar_cores for true)
int  term_largura(void);            // Retorna a quantidade de colunas da janela do terminal (padrão de 100 colunas)

// Calcula a luminância da cor 'rgb' e retorna a cor de texto ideal (preto ou branco) para legibilidade no Gantt
unsigned cor_contraste(unsigned rgb);

// Avança o estado de 64 bits do gerador aleatório pseudo-determinístico e retorna um número de 32 bits
uint32_t rng_proximo(uint64_t *estado);

// Retorna um inteiro aleatório no intervalo [0, n-1] utilizando o estado explícito mantido no snapshot
int rng_intervalo(uint64_t *estado, int n);  

#endif
