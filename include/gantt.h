/*
 * gantt.h - desenho do gráfico de Gantt no terminal e em SVG.
 *
 * Convenções (req. 2.1, 2.2 e 2.5, figuras da seção 6.4 do Maziero):
 *  - eixo Y: tarefa de MENOR id junto do eixo X, ids crescentes para cima;
 *  - executando: cor da tarefa, com o número da CPU dentro;
 *  - pronta (na fila, sem CPU): sem cor;
 *  - suspensa: preto pontilhado;
 *  - um bloco extra com uma linha por CPU mostra qual tarefa ocupou cada
 *    processador e quando ele ficou desligado;
 *  - marcadores no instante exato: chegada, fim de ativação, término,
 *    perda de prazo e sorteio.
 */
#ifndef GANTT_H
#define GANTT_H

#include "sim.h"

/* Máscara de eventos por célula (tarefa x tick), usada pelos dois
 * desenhos. */
enum {
    M_CHEGADA = 1, M_FIM = 2, M_TERMINO = 4, M_PRAZO = 8, M_SORTEIO = 16,
};

/* Preenche mask[(instante - ini) * n + tarefa] para instantes em
 * [ini, fim): cada evento cai na coluna do seu instante exato. */
void gantt_mascaras(const Simulador *s, int ini, int fim, unsigned char *mask);

/* Índices das tarefas ordenados por id DECRESCENTE (ordem de cima para
 * baixo no gráfico). O chamador libera. */
int *gantt_ordem(const Estado *e);

/* Terminal: desenha os ticks [ini, fim). */
void gantt_tui(const Simulador *s, int ini, int fim);

/* Terminal: desenha a simulação inteira, quebrando em faixas da largura
 * da tela. */
void gantt_tui_completo(const Simulador *s);

/* Linha do tempo em texto, um trecho por linha (útil para conferir à mão
 * e nos testes automáticos). */
void gantt_texto(const Simulador *s);

/* SVG com a simulação inteira. Devolve false e o motivo em `erro`. */
bool gantt_svg(const Simulador *s, const char *caminho, const char *titulo,
               char *erro, int tam_erro);

#endif
