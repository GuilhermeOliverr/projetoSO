/*
 * sched_registro.c - tabela de escalonadores disponíveis.
 *
 * A simulação só conhece o índice de um escalonador nesta tabela. Incluir
 * um algoritmo novo é acrescentar uma linha em `tabela`, ou carregar um
 * plugin em tempo de execução.
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

extern const Escalonador escalonador_rm;
extern const Escalonador escalonador_edf;

#define MAX_ESCALONADORES 32

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

const Escalonador *sched_obter(int idx) {
    if (idx < 0 || idx >= quantidade) return NULL;
    return tabela[idx];
}

int sched_buscar(const char *nome) {
    for (int i = 0; i < quantidade; i++)
        if (str_ieq(nome, tabela[i]->nome)) return i;
    return -1;
}

void sched_listar_nomes(char *buf, int tam) {
    buf[0] = '\0';
    for (int i = 0; i < quantidade; i++) {
        size_t usado = strlen(buf);
        snprintf(buf + usado, (size_t)tam - usado, "%s%s", i ? ", " : "", tabela[i]->nome);
    }
}

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
