/*
 * edicao.c - regras de alteração de tarefas (ver edicao.h).
 */
#include "edicao.h"

#include <stdio.h>

const char *valida_positivo(int v) {
    return v > 0 ? NULL : "o valor deve ser maior que zero";
}

const char *valida_periodo(int v) {
    if (v == 0) return "período 0 significa tarefa aperiódica, que o Projeto A não simula";
    return v > 0 ? NULL : "o período deve ser maior que zero";
}

const char *valida_ingresso(int v, int min) {
    static char buf[128];
    if (v >= min) return NULL;
    snprintf(buf, sizeof buf, "o valor deve ser >= %d (não dá para chegar no passado)", min);
    return buf;
}

const char *valida_id(int v, const int *ids, int n, int proprio) {
    static char buf[96];
    for (int i = 0; i < n; i++)
        if (i != proprio && ids[i] == v) {
            snprintf(buf, sizeof buf, "já existe uma tarefa com id %d", v);
            return buf;
        }
    return NULL;
}

void edicao_periodo(TCB *k, int v) {
    k->periodo = v;
    if (k->liberadas > 0 && k->liberadas < MAX_ATIVACOES)
        k->proxima_chegada = k->chegadas[k->liberadas - 1] + v;
}

void edicao_prazo(TCB *k, int v) {
    k->prazo = v;
    if (k->restante > 0) k->deadline_abs = k->chegada_atual + v;
}

void edicao_ingresso(TCB *k, int v) {
    k->ingresso = v;
    k->proxima_chegada = v;
}

void edicao_alternar_suspensao(TCB *k) {
    if (suspensa_pelo_usuario(k)) {
        /* Volta para onde estaria se não tivesse sido suspensa: com
         * ativação pendente, pronta; antes do ingresso, nova; senão,
         * esperando o próximo período. */
        if (k->restante > 0) {
            k->estado = T_PRONTA;
            k->motivo = SUSP_NENHUM;
        } else if (k->liberadas == 0) {
            k->estado = T_NOVA;
            k->motivo = SUSP_NENHUM;
        } else {
            k->estado = T_SUSPENSA;
            k->motivo = SUSP_PERIODO;
        }
    } else {
        k->estado = T_SUSPENSA;
        k->motivo = SUSP_USUARIO;
        k->cpu = -1;
    }
}

void edicao_encerrar(TCB *k, int t) {
    k->estado = T_TERMINADA;
    k->motivo = SUSP_NENHUM;
    k->restante = 0;
    k->cpu = -1;
    k->termino = t;
}
