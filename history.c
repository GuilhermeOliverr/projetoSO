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
 */
#include "sim.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void estado_copiar(Estado *dst, const Estado *src) {
    TCB *buf = dst->tarefas;
    int cap_antiga = dst->n;
    *dst = *src;
    /* Reaproveita o vetor de destino se já tiver o tamanho certo. */
    if (cap_antiga != src->n || !buf) buf = xrealloc(buf, (size_t)(src->n ? src->n : 1) * sizeof(TCB));
    if (src->n) memcpy(buf, src->tarefas, (size_t)src->n * sizeof(TCB));
    dst->tarefas = buf;
}

void estado_liberar(Estado *e) {
    free(e->tarefas);
    e->tarefas = NULL;
    e->n = 0;
}

void hist_gravar(Simulador *s) {
    if (!s->historico) return;
    int t = s->atual.t;
    if (t >= s->capsnaps) {
        int nova = s->capsnaps ? s->capsnaps * 2 : 256;
        while (nova <= t) nova *= 2;
        s->snaps = xrealloc(s->snaps, (size_t)nova * sizeof(Estado));
        memset(s->snaps + s->capsnaps, 0, (size_t)(nova - s->capsnaps) * sizeof(Estado));
        s->capsnaps = nova;
    }
    estado_copiar(&s->snaps[t], &s->atual);
    if (t + 1 > s->nsnaps) s->nsnaps = t + 1;
}

void hist_truncar(Simulador *s, int t) {
    if (s->historico) {
        for (int k = t + 1; k < s->nsnaps; k++) estado_liberar(&s->snaps[k]);
        if (s->nsnaps > t + 1) s->nsnaps = t + 1;
    }
    /* Colunas >= t e eventos gerados em ticks >= t pertencem ao futuro. */
    if (s->ncols > t) s->ncols = t;
    int j = 0;
    for (int i = 0; i < s->nev; i++)
        if (s->ev[i].tick < t) s->ev[j++] = s->ev[i];
    s->nev = j;
}

void hist_carregar(Simulador *s, int t) {
    estado_copiar(&s->atual, &s->snaps[t]);
}

void rastro_garantir_coluna(Simulador *s, int tick) {
    if (tick >= s->capcols) {
        int nova = s->capcols ? s->capcols * 2 : 256;
        while (nova <= tick) nova *= 2;
        size_t w = (size_t)(s->largura ? s->largura : 1);
        s->cel = xrealloc(s->cel, (size_t)nova * w);
        s->cpu = xrealloc(s->cpu, (size_t)nova * w);
        s->ncpus_col = xrealloc(s->ncpus_col, (size_t)nova);
        s->capcols = nova;
    }
    size_t w = (size_t)s->largura;
    memset(s->cel + (size_t)tick * w, G_VAZIO, w);
    memset(s->cpu + (size_t)tick * w, -1, w);
    s->ncpus_col[tick] = (unsigned char)s->atual.ncpus;
    if (tick + 1 > s->ncols) s->ncols = tick + 1;
}

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

void evento_add(Simulador *s, int tick, int instante, TipoEvento tipo, int tarefa, int ativ, int cpu) {
    if (s->nev == s->capev) {
        s->capev = s->capev ? s->capev * 2 : 256;
        s->ev = xrealloc(s->ev, (size_t)s->capev * sizeof(Evento));
    }
    s->ev[s->nev++] = (Evento){tick, instante, tipo, tarefa, ativ, cpu};
}
