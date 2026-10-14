/*
 * sched_registro.c - tabela de escalonadores disponíveis.
 *
 * A simulação só conhece o índice de um escalonador nesta tabela. Incluir
 * um algoritmo novo é acrescentar uma linha em `tabela`, ou carregar um
 * plugin em tempo de execução.
 *
 * Isso é o que atende o requisito 4.2 ("novos algoritmos sem modificar o
 * código da simulação"): sim.c nunca menciona RM ou EDF, só pede
 * sched_obter(indice) e chama a função de prioridade que vier.
 */
/* dlopen/dlsym são POSIX; em glibc >= 2.34 fazem parte da própria libc, então
 * o requisito "sem bibliotecas extras" continua atendido. */
#define _POSIX_C_SOURCE 200809L

#include "sched.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

/* `extern` = "este objeto existe em outro arquivo .c" (sched_rm.c e
 * sched_edf.c). O linker junta tudo na hora de gerar o executável. */
extern const Escalonador escalonador_rm;
extern const Escalonador escalonador_edf;

#define MAX_ESCALONADORES 32

/* A tabela já nasce com RM (índice 0) e EDF (índice 1); plugins entram
 * depois. `quantidade` conta quantas posições estão em uso. */
static const Escalonador *tabela[MAX_ESCALONADORES] = {
    &escalonador_rm,
    &escalonador_edf,
};
static int quantidade = 2;

#ifndef _WIN32
/* Handles dos plugins abertos, fechados na saída do programa (atexit) para
 * não deixar memória do carregador dinâmico pendurada (valgrind limpo). */
static void *handles[MAX_ESCALONADORES];
static int nhandles;

static void fechar_plugins(void) {
    while (nhandles > 0) dlclose(handles[--nhandles]);
}
#endif

int sched_quantidade(void) { return quantidade; }

/* Devolve o escalonador do índice, ou NULL se o índice não existe. */
const Escalonador *sched_obter(int idx) {
    if (idx < 0 || idx >= quantidade) return NULL;
    return tabela[idx];
}

/* Procura pelo nome do arquivo ("rm", "EDF", ...) sem diferenciar caixa
 * (req. 3.3.2). Devolve o índice ou -1. */
int sched_buscar(const char *nome) {
    for (int i = 0; i < quantidade; i++)
        if (str_ieq(nome, tabela[i]->nome)) return i;
    return -1;
}

/* Escreve "RM, EDF" (e plugins) no buffer, para mensagens de erro do tipo
 * "algoritmo desconhecido; use RM, EDF". snprintf com o espaço restante
 * (tam - usado) evita estourar o buffer. */
void sched_listar_nomes(char *buf, int tam) {
    buf[0] = '\0';
    for (int i = 0; i < quantidade; i++) {
        size_t usado = strlen(buf);
        snprintf(buf + usado, (size_t)tam - usado, "%s%s", i ? ", " : "", tabela[i]->nome);
    }
}

/* Carrega um escalonador de uma biblioteca dinâmica (.so):
 *   1. dlopen abre o arquivo;
 *   2. dlsym procura nele o símbolo `escalonador_plugin`;
 *   3. conferimos que é válido e que o nome não repete;
 *   4. acrescentamos à tabela.
 * Em qualquer falha escrevemos o motivo em `erro` e devolvemos -1. */
int sched_carregar_plugin(const char *caminho, char *erro, int tam_erro) {
#ifdef _WIN32
    (void)caminho;
    snprintf(erro, (size_t)tam_erro, "plugins dinâmicos não são suportados nesta plataforma");
    return -1;
#else
    if (quantidade >= MAX_ESCALONADORES) {
        snprintf(erro, (size_t)tam_erro, "limite de %d escalonadores atingido", MAX_ESCALONADORES);
        return -1;
    }
    /* dlopen só procura no diretório atual se o caminho tiver '/'. */
    char caminho_real[1024];
    if (strchr(caminho, '/')) snprintf(caminho_real, sizeof caminho_real, "%s", caminho);
    else snprintf(caminho_real, sizeof caminho_real, "./%s", caminho);

    /* RTLD_NOW = resolve todos os símbolos já, para falhar cedo. */
    void *h = dlopen(caminho_real, RTLD_NOW);
    if (!h) {
        snprintf(erro, (size_t)tam_erro, "não foi possível abrir '%s': %s", caminho, dlerror());
        return -1;
    }
    const Escalonador *e = (const Escalonador *)dlsym(h, SCHED_SIMBOLO_PLUGIN);
    if (!e || !e->nome || !e->prioridade) {
        snprintf(erro, (size_t)tam_erro,
                 "'%s' não exporta um Escalonador válido chamado '%s'",
                 caminho, SCHED_SIMBOLO_PLUGIN);
        dlclose(h);
        return -1;
    }
    if (sched_buscar(e->nome) >= 0) {
        snprintf(erro, (size_t)tam_erro, "já existe um escalonador chamado '%s'", e->nome);
        dlclose(h);
        return -1;
    }
    /* O handle fica aberto até o fim do programa (a tabela aponta para
     * dentro dele) e é fechado por fechar_plugins, registrada com atexit. */
    if (nhandles == 0) atexit(fechar_plugins);
    handles[nhandles++] = h;
    tabela[quantidade] = e;
    return quantidade++;
#endif
}
