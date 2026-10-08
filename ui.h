/*
 * ui.h - interação com o usuário no terminal.
 *
 *  - tela de configuração (antes da simulação): mostra os parâmetros com
 *    os valores atuais/padrão e permite editá-los (req. 3.1 e 3.2);
 *  - modo (a) passo a passo: um "debugger" do sistema (req. 1.5.1/1.5.2);
 *  - modo (b) execução completa: só o resultado final (req. 1.5.3).
 */
#ifndef UI_H
#define UI_H

#include "config.h"
#include "sim.h"

#include <stdint.h>

typedef struct {
    const char *saida_svg;   /* NULL = nome derivado do arquivo de config */
    bool texto;              /* imprime também a linha do tempo em texto   */
    uint64_t semente;        /* semente do sorteio                         */
} OpcoesUI;

/* Tabela com sistema, tarefas e utilização. */
void ui_imprimir_config(const Config *cfg);

/* Tela de configuração. Devolve false se o usuário quis sair. Pode trocar o
 * arquivo carregado (opção [l]). */
bool ui_menu_config(Config *cfg);

/* Pergunta o caminho do arquivo até carregar um válido. false = desistiu. */
bool ui_pedir_arquivo(Config *cfg);

/* Pergunta o modo: 'a' (passo a passo), 'b' (completo) ou 0 (sair). */
char ui_pedir_modo(void);

void ui_passo_a_passo(const Config *cfg, const OpcoesUI *op);
/* Devolve o código de saída do programa (0 = ok). */
int ui_execucao_completa(const Config *cfg, const OpcoesUI *op);

#endif
