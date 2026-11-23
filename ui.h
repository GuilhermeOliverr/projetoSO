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

#include <stddef.h>
#include <stdint.h>

// Início da definição da estrutura OpcoesUI que armazena as opções de execução da interface
typedef struct {
    const char *saida_svg;   // Caminho do arquivo de saída SVG (se NULL, o nome é derivado automaticamente do arquivo de configuração)
    bool texto;              // Flag booleana: se true, também imprime a linha do tempo e eventos em formato de texto no terminal
    uint64_t semente;        // Valor da semente (seed) inicial para o gerador de números aleatórios no sorteio determinístico
} OpcoesUI;

// Imprime na tela a tabela formatada com as configurações do sistema, tarefas e taxa de utilização calculada
void ui_imprimir_config(const Config *cfg);

// Exibe o menu interativo de configuração antes da simulação, permitindo editar parâmetros ou trocar o arquivo carregado
bool ui_menu_config(Config *cfg);

// Solicita interativamente ao usuário o caminho do arquivo de configuração até carregar um arquivo válido
bool ui_pedir_arquivo(Config *cfg);

// Pergunta ao usuário qual o modo de execução desejado: 'a' (passo a passo), 'b' (execução completa) ou 0 (sair)
char ui_pedir_modo(void);

// Executa o modo interativo passo a passo ("debugger"), permitindo avançar, retroceder e editar tarefas
void ui_passo_a_passo(const Config *cfg, const OpcoesUI *op);
// Executa a simulação inteira de forma contínua até o final e exporta o resultado (retorna 0 se OK)
int ui_execucao_completa(const Config *cfg, const OpcoesUI *op);

// Constrói o caminho de saída do arquivo SVG em 'dst', usando op->saida_svg ou o nome base + "_gantt.svg"
void ui_caminho_svg(const Config *cfg, const OpcoesUI *op, char *dst, size_t tam);

/* ---- ui_gui.c: a mesma interface, numa janela gráfica ---- */

// Tenta inicializar a janela gráfica nativa (retorna false com mensagem em 'erro' se não houver interface gráfica)
bool ui_gui_iniciar(char *erro, int tam_erro);

// Executa todo o fluxo do simulador (seleção de arquivo, menu de edição, simulação e Gantt) dentro da janela gráfica
int ui_gui_executar(const char *arquivo, const OpcoesUI *op);

#endif
