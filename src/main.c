/*
 * main.c - ponto de entrada do simulador.
 *
 * Uso:
 *   projetoSO [arquivo] [opções]
 *
 * Sem argumentos, o programa pergunta o caminho do arquivo (qualquer nome,
 * qualquer diretório ou disco - req. 3.3.4), mostra a configuração com os
 * valores padrão para edição e depois pergunta o modo de execução.
 * Com --modo, roda sem perguntas (útil para scripts e para a defesa).
 */
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "sched.h"
#include "ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static void uso(const char *prog) {
    printf("Simulador de escalonamento de tempo real (RM / EDF) - Projeto A\n\n"
           "Uso: %s [arquivo_config] [opções]\n\n"
           "Opções:\n"
           "  --modo passo|completo  modo de execução, sem perguntar (a = passo, b = completo)\n"
           "  --saida ARQ.svg        onde salvar o Gantt (padrão: <nome do config>_gantt.svg)\n"
           "  --texto                no modo completo, imprime também a linha do tempo em texto\n"
           "  --validar              só carrega e valida o arquivo (código de saída 0 = válido)\n"
           "  --semente N            semente do sorteio de desempate (padrão: 1; 0 = relógio)\n"
           "  --plugin LIB.so        carrega um escalonador de uma biblioteca dinâmica\n"
           "  --sem-cor              desliga as cores ANSI\n"
           "  -h, --help             mostra esta ajuda\n\n"
           "Formato do arquivo:\n"
           "  algoritmo_escalonamento;quantum;qtde_cpus\n"
           "  id;cor;ingresso;duracao;periodo;prazo;lista_eventos\n"
           "  ...\n", prog);
}

int main(int argc, char **argv) {
    const char *arquivo = NULL;
    const char *modo = NULL;
    bool validar = false;
    OpcoesUI op = {NULL, false, 1};

#ifndef _WIN32
    /* Cores só quando a saída é um terminal de verdade. */
    if (!isatty(STDOUT_FILENO)) g_usar_cores = false;
#endif
    if (getenv("NO_COLOR")) g_usar_cores = false;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool tem_valor = i + 1 < argc;
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            uso(argv[0]);
            return 0;
        } else if (!strcmp(a, "--sem-cor")) {
            g_usar_cores = false;
        } else if (!strcmp(a, "--texto")) {
            op.texto = true;
        } else if (!strcmp(a, "--validar")) {
            validar = true;
        } else if (!strcmp(a, "--modo") && tem_valor) {
            modo = argv[++i];
            if (!str_ieq(modo, "passo") && !str_ieq(modo, "a") && !str_ieq(modo, "completo") && !str_ieq(modo, "b")) {
                fprintf(stderr, "erro: --modo '%s' inválido; use 'passo' ou 'completo'\n", modo);
                return 2;
            }
        } else if (!strcmp(a, "--saida") && tem_valor) {
            op.saida_svg = argv[++i];
        } else if (!strcmp(a, "--semente") && tem_valor) {
            int v;
            if (parse_int(argv[++i], &v) != 0 || v < 0) {
                fprintf(stderr, "erro: --semente '%s' inválida; use um inteiro >= 0\n", argv[i]);
                return 2;
            }
            op.semente = v ? (uint64_t)v : (uint64_t)time(NULL);
        } else if (!strcmp(a, "--plugin") && tem_valor) {
            char erro[1024];
            int idx = sched_carregar_plugin(argv[++i], erro, sizeof erro);
            if (idx < 0) {
                fprintf(stderr, "erro: plugin: %s\n", erro);
                return 2;
            }
            printf("Escalonador '%s' carregado de %s\n", sched_obter(idx)->nome, argv[i]);
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "erro: opção '%s' desconhecida ou sem valor (use --help)\n", a);
            return 2;
        } else if (!arquivo) {
            arquivo = a;
        } else {
            fprintf(stderr, "erro: mais de um arquivo informado ('%s' e '%s')\n", arquivo, a);
            return 2;
        }
    }

    Config cfg;
    config_padrao(&cfg);
    if (arquivo) {
        Config lido;
        bool ok = config_carregar(arquivo, &lido);
        config_imprimir_mensagens(&lido);
        if (!ok) {
            config_liberar(&lido);
            if (validar || modo) return 1;
            printf("O arquivo informado tem problemas. Informe outro (ou corrija e informe de novo).\n");
            if (!ui_pedir_arquivo(&cfg)) return 1;
        } else {
            cfg = lido;
        }
    } else {
        if (validar) {
            fprintf(stderr, "erro: --validar precisa do caminho do arquivo\n");
            return 2;
        }
        printf("Simulador de escalonamento de tempo real (RM / EDF) - Projeto A\n");
        if (!ui_pedir_arquivo(&cfg)) return 0;
    }

    if (validar) {
        ui_imprimir_config(&cfg);
        printf("Arquivo válido.\n");
        config_liberar(&cfg);
        return 0;
    }

    int ret = 0;
    char m;
    if (modo) {
        m = (str_ieq(modo, "completo") || str_ieq(modo, "b")) ? 'b' : 'a';
        if (m == 'a') ui_imprimir_config(&cfg);
    } else {
        if (!ui_menu_config(&cfg)) {
            config_liberar(&cfg);
            return 0;
        }
        m = ui_pedir_modo();
    }
    if (m == 'a') {
        if (cfg.n == 0) {
            fprintf(stderr, "erro: não há tarefas periódicas para simular\n");
            ret = 1;
        } else {
            ui_passo_a_passo(&cfg, &op);
        }
    } else if (m == 'b') {
        ret = ui_execucao_completa(&cfg, &op);
    }
    config_liberar(&cfg);
    return ret;
}
