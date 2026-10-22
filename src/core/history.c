/*
 * history.c - histórico de estados (avançar/retroceder) e rastro do Gantt.
 *
 * Decisão: guardamos um snapshot COMPLETO por tick em vez de "desfazer"
 * operações. É o jeito mais simples de garantir que voltar e avançar dá
 * exatamente o mesmo resultado, inclusive nos sorteios (o estado do gerador
 * aleatório está no snapshot). O custo é n_tarefas * sizeof(TCB) por tick,
 * desprezível nos tamanhos de simulação deste trabalho.
 *
 * Editar o estado num instante passado invalida o que veio depois, como
 * num debugger: snapshots, colunas do Gantt e eventos futuros são
 * descartados e a simulação segue a partir do estado editado.
 *
 * Este arquivo guarda TRÊS coisas diferentes, cada uma com seu papel:
 *   1. snaps[t]  - o estado completo do sistema no instante t (para voltar);
 *   2. cel/cpu   - o "rastro": o que cada tarefa fez em cada tick (para o
 *                  Gantt). Fica à parte para o gráfico não depender de reler
 *                  snapshots;
 *   3. ev        - a lista de eventos (chegadas, perdas de prazo, ...).
 */
#include "sim.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * 1. GERENCIAMENTO DE SNAPSHOTS (Fotografias da Memória por Tick)
 * ========================================================================= */


//Realiza uma cópia profunda (deep copy) de uma estrutura de Estado 'src' para 'dst'.
//Como o TCB não possui ponteiros internos dinâmicos próprios, um simples memcpy do 
//vetor de TCBs é suficiente para clonar o estado inteiro com extrema eficiência.
void estado_copiar(Estado *dst, const Estado *src) {
    TCB *buf = dst->tarefas;     /* guarda o vetor que o destino já tinha   */
    int cap_antiga = dst->n;
    *dst = *src;                  /* copia os campos simples (t, rng, ...)  */
    /* Reaproveita o vetor de destino se já tiver o tamanho certo. */
    if (cap_antiga != src->n || !buf) buf = xrealloc(buf, (size_t)(src->n ? src->n : 1) * sizeof(TCB));
    if (src->n) memcpy(buf, src->tarefas, (size_t)src->n * sizeof(TCB));    // Clona o vetor de TCBs da origem para o destino usando memcpy()
    dst->tarefas = buf;           /* o `*dst = *src` tinha sobrescrito isto */
}

//Libera a memória dinâmica alocada para o vetor de TCBs contido dentro de uma estrutura Estado
void estado_liberar(Estado *e) {
    free(e->tarefas);
    e->tarefas = NULL;
    e->n = 0;
}

//Grava o estado atual da simulação (s->atual) na pilha de snapshots no índice 't'.
//Expande a capacidade da pilha dobrando seu tamanho para manter o custo de gravação constante (O(1) amortizado).
void hist_gravar(Simulador *s) {
    if (!s->historico) return;    /* modo completo: não guardamos nada      */
    int t = s->atual.t;
    if (t >= s->capsnaps) {
        int nova = s->capsnaps ? s->capsnaps * 2 : 256;
        while (nova <= t) nova *= 2;
        s->snaps = xrealloc(s->snaps, (size_t)nova * sizeof(Estado));
        /* A parte nova precisa vir zerada: estado_copiar espera que o
         * ponteiro `tarefas` antigo seja NULL ou válido, nunca lixo. */
        memset(s->snaps + s->capsnaps, 0, (size_t)(nova - s->capsnaps) * sizeof(Estado));    // Zera a nova área alocada de memória usando memset()
        s->capsnaps = nova;
    }
    estado_copiar(&s->snaps[t], &s->atual);    // Clona o estado corrente no snapshot do tick 't' usando estado_copiar()
    if (t + 1 > s->nsnaps) s->nsnaps = t + 1;
}

//Trunca o histórico da simulação a partir do instante 't' após uma edição dinâmica. 
//Libera os snapshots futuros (ticks > t) e remove os eventos/colunas do Gantt gerados no futuro descartado.
void hist_truncar(Simulador *s, int t) {
    if (s->historico) {
        for (int k = t + 1; k < s->nsnaps; k++) 
            estado_liberar(&s->snaps[k]);    // Libera a memória de cada snapshot futuro usando estado_liberar()
        if (s->nsnaps > t + 1) s->nsnaps = t + 1;
    }
    /* Colunas >= t e eventos gerados em ticks >= t pertencem ao futuro.
     * Para os eventos fazemos uma compactação in-place: copiamos para a
     * frente só os que ficam (índice j) e depois ajustamos o total. */
    if (s->ncols > t) s->ncols = t;
    int j = 0;
    for (int i = 0; i < s->nev; i++)
        if (s->ev[i].tick < t) s->ev[j++] = s->ev[i];
    s->nev = j;
}

//Restaura o estado da simulação (s->atual) a partir do snapshot gravado no instante 't' (operação de Undo/Ir para).
void hist_carregar(Simulador *s, int t) {
    estado_copiar(&s->atual, &s->snaps[t]);
}

/* =========================================================================
 * 2. GERENCIAMENTO DO RASTRO GRÁFICO (Matrizes do Gantt)
 * ========================================================================= */

//Garante a alocação de espaço para a coluna correspondente ao 'tick' nas matrizes 
//do Gráfico de Gantt e limpa as células (marcando como vazia e sem CPU alocada).
void rastro_garantir_coluna(Simulador *s, int tick) {
    if (tick >= s->capcols) {
        int nova = s->capcols ? s->capcols * 2 : 256;
        while (nova <= tick) nova *= 2;
        size_t w = (size_t)(s->largura ? s->largura : 1);
        s->cel = xrealloc(s->cel, (size_t)nova * w);    // Redimensiona o vetor dinâmico de células usando xrealloc()
        s->cpu = xrealloc(s->cpu, (size_t)nova * w);
        s->ncpus_col = xrealloc(s->ncpus_col, (size_t)nova);
        s->capcols = nova;
    }
    size_t w = (size_t)s->largura;
    memset(s->cel + (size_t)tick * w, G_VAZIO, w);
    memset(s->cpu + (size_t)tick * w, -1, w);
    /* Guardamos quantas CPUs existiam NESTE tick: o usuário pode mudar a
     * quantidade durante a simulação, e o Gantt antigo precisa lembrar. */
    s->ncpus_col[tick] = (unsigned char)s->atual.ncpus;
    if (tick + 1 > s->ncols) s->ncols = tick + 1;
}

//Reorganiza as matrizes 2D do rastro do Gantt quando uma nova tarefa é adicionada.
//Como o layout da matriz é 'tick * largura + tarefa', alterar a largura exige reestruturar 
//e copiar o conteúdo linha por linha em novos blocos de memória.
void rastro_ajustar_largura(Simulador *s, int largura) {
    if (largura <= s->largura) return;
    /* Refaz o layout: as colunas antigas ganham G_VAZIO para a tarefa nova
     * (ela ainda não existia naqueles ticks). */
    size_t cap = (size_t)(s->capcols ? s->capcols : 1);
    unsigned char *cel = xmalloc(cap * (size_t)largura);
    signed char *cpu = xmalloc(cap * (size_t)largura);
    memset(cel, G_VAZIO, cap * (size_t)largura);
    memset(cpu, -1, cap * (size_t)largura);
    for (int t = 0; t < s->ncols; t++) {
        memcpy(cel + (size_t)t * largura, s->cel + (size_t)t * s->largura, (size_t)s->largura);
        memcpy(cpu + (size_t)t * largura, s->cpu + (size_t)t * s->largura, (size_t)s->largura);
    }
    free(s->cel);
    free(s->cpu);
    s->cel = cel;
    s->cpu = cpu;
    s->ncpus_col = xrealloc(s->ncpus_col, cap);
    s->capcols = (int)cap;
    s->largura = largura;
}

/* =========================================================================
 * 3. REGISTRO DE EVENTOS
 * ========================================================================= */

//Adiciona um novo evento ocorrido (chegada, fim de ativação, perda de prazo, sorteio) ao vetor dinâmico de eventos da simulação.
void evento_add(Simulador *s, int tick, int instante, TipoEvento tipo, int tarefa, int ativ, int cpu) {
    if (s->nev == s->capev) {
        s->capev = s->capev ? s->capev * 2 : 256;
        s->ev = xrealloc(s->ev, (size_t)s->capev * sizeof(Evento));
    }
    s->ev[s->nev++] = (Evento){tick, instante, tipo, tarefa, ativ, cpu};
}
