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
    M_CHEGADA = 1,     // Bit 0 (1): Marcador de chegada/ingresso de nova ativação (↓)
    M_FIM = 2,     // Bit 1 (2): Marcador de fim de uma ativação comum (↑ ou △)
    M_TERMINO = 4,     // Bit 2 (4): Marcador de término definitivo da 10ª ativação (■)
    M_PRAZO = 8,     // Bit 3 (8): Marcador de perda de prazo / deadline miss (✗ em vermelho)
    M_SORTEIO = 16,    // Bit 4 (16): Marcador de sorteio no critério 5 de desempate (?)
};

void gantt_mascaras(const Simulador *s, int ini, int fim, unsigned char *mask);      // Preenche a matriz de máscaras combinando os eventos ocorridos em cada célula (tarefa x tick) para a faixa [ini, fim)  

int *gantt_ordem(const Estado *e);        // Aloca e retorna um vetor contendo os índices das tarefas ordenados por ID decrescente (exigência do eixo Y: maior ID no topo)

void gantt_tui(const Simulador *s, int ini, int fim);    // Desenha a faixa de tempo [ini, fim) do Gráfico de Gantt no terminal utilizando cores e caracteres ANSI 24-bit

void gantt_tui_completo(const Simulador *s);    // Renderiza a simulação completa no terminal, dividindo o gráfico em faixas ajustadas à largura da tela

void gantt_texto(const Simulador *s);    // Gera uma representação simplificada da linha do tempo em texto puro (utilizada para conferência manual e testes unitários)

bool gantt_svg(const Simulador *s, const char *caminho, const char *titulo, char *erro, int tam_erro);    // Exporta a simulação completa para um arquivo vetorial .svg em disco (Requisito 2.4)

struct Tela;    // Declaração antecipada da estrutura 'Tela' (buffer de memória de pixels), utilizada pela janela gráfica nativa

// Estrutura VistaGantt, que funciona como os parâmetros da "câmera" para desenhar o gráfico na tela
typedef struct {
    int x, y, w, h;      // Retângulo da tela reservado para o gráfico: canto superior esquerdo (x, y), largura w e altura h, em pixels
    int t_ini;           // Define o primeiro tick visível no canto esquerdo, permitindo a rolagem horizontal no tempo
    int cw;              // Define a largura em pixels de cada tick (coluna), permitindo o controle do nível de zoom
    int y_rol;           // Deslocamento vertical em pixels para rolagem da lista de tarefas quando houver muitas tarefas no eixo Y
    int sel;             // Guarda o índice da tarefa atualmente selecionada pelo usuário para destacar na tela (-1 para nenhuma)
    int t_marca;         // Guarda o instante (tick) destacado com uma linha vertical para indicar o tempo atual (-1 para desativar)
    int alt_conteudo;    // Campo de saída: armazena a altura total em pixels calculada do conteúdo para ajustar a barra de rolagem
    int lbl;             // Campo de saída: armazena a largura calculada da coluna reservada aos rótulos/nomes das tarefas
} VistaGantt;

void gantt_gfx(struct Tela *t, const Simulador *s, VistaGantt *v);    // Renderiza (desenha) as tarefas, o eixo temporal e a linha de cada CPU dentro do buffer de pixels da janela gráfica

int gantt_gfx_tarefa_em(const Simulador *s, const VistaGantt *v, int x, int y);    // Calcula e retorna o índice da tarefa que está sob a posição do ponteiro do mouse (x, y)

int gantt_gfx_legenda(struct Tela *t, int x, int y, int w, bool desenhar);    // Desenha o espaço da legenda de ícones (chegada, prazo, sorteio, CPUs) no rodapé da janela

#endif
