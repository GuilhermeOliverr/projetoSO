/*
 * edicao.c - regras de alteração de tarefas (ver edicao.h).
 */
#include "edicao.h"

#include <stdio.h>

/* =========================================================================
 * 1. FUNÇÕES DE VALIDAÇÃO (Regras para aceitar ou recusar edições)
 * ========================================================================= */

//Valida se um número inteiro 'v' é estritamente positivo (> 0). 
//É usada para verificar campos como duração e prazo relativo da tarefa. 
//Retorna NULL se o valor for válido, ou a mensagem de erro caso seja <= 0
const char *valida_positivo(int v) {
    return v > 0 ? NULL : "o valor deve ser maior que zero";
}

//Valida se o período 'v' é permitido no Projeto.
//Recusa o valor 0 (pois indicaria tarefa aperiódica, não simulada no Projeto A) e valores negativos. 
//Retorna NULL se for válido (> 0), ou o motivo da recusa caso contrário.
const char *valida_periodo(int v) {
    if (v == 0) return "período 0 significa tarefa aperiódica, que o Projeto A não simula";
    return v > 0 ? NULL : "o período deve ser maior que zero";
}

//Valida se o instante de primeiro ingresso da tarefa não é menor que o tick atual 'min'. 
//Impede que o usuário tente configurar o ingresso de uma tarefa para um momento no passado.
//Retorna NULL se for válido, ou formata uma mensagem de erro se v < min.
const char *valida_ingresso(int v, int min) {
    static char buf[128];
    if (v >= min) return NULL;
    snprintf(buf, sizeof buf, "o valor deve ser >= %d (não dá para chegar no passado)", min);
    return buf;
}

//Verifica se o ID 'v' informado para a tarefa é único, ou seja, se já não pertence a outra tarefa.
//Percorre o vetor 'ids' ignorando a própria tarefa em edição (índice 'proprio'). 
//Retorna NULL se for único, ou a mensagem de erro especificando que o ID já está em uso.
const char *valida_id(int v, const int *ids, int n, int proprio) {
    static char buf[96];
    for (int i = 0; i < n; i++)
        if (i != proprio && ids[i] == v) {
            snprintf(buf, sizeof buf, "já existe uma tarefa com id %d", v);    // Formata a mensagem de aviso de ID duplicado usando snprintf()
            return buf;
        }
    return NULL;
}

/* =========================================================================
 * 2. FUNÇÕES DE APLICAÇÃO DE EDIÇÃO (Alteram o TCB e recalculam dependências)
 * ========================================================================= */

//Aplica um novo valor de período 'v' na tarefa 'k' e recalcula o instante exato 
//em que a próxima ativação da tarefa deve ser liberada na simulação.
void edicao_periodo(TCB *k, int v) {
    k->periodo = v;
    if (k->liberadas > 0 && k->liberadas < MAX_ATIVACOES)
        k->proxima_chegada = k->chegadas[k->liberadas - 1] + v;
}

//Aplica um novo prazo relativo 'v' na tarefa 'k'. Se a tarefa estiver com uma ativação em andamento (restante > 0), reajusta imediatamente o seu deadline absoluto.
void edicao_prazo(TCB *k, int v) {
    k->prazo = v;
    if (k->restante > 0) k->deadline_abs = k->chegada_atual + v;
}

//Aplica um novo instante de ingresso inicial 'v' na tarefa 'k'. Esta função só deve ser chamada para tarefas que ainda não ingressaram no sistema.
void edicao_ingresso(TCB *k, int v) {
    k->ingresso = v;
    k->proxima_chegada = v;
}

//Alterna a tarefa 'k' entre o estado suspenso manualmente pelo usuário (SUSP_USUARIO) e o seu estado normal de execução/espera. 
//Se já estiver suspensa pelo usuário, restaura o estado correto (T_PRONTA, T_NOVA ou T_SUSPENSA por período).
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

//Força o encerramento antecipado da tarefa 'k' no instante 't', definindo seu estado como T_TERMINADA e registrando o instante de término.
void edicao_encerrar(TCB *k, int t) {
    k->estado = T_TERMINADA;
    k->motivo = SUSP_NENHUM;
    k->restante = 0;
    k->cpu = -1;
    k->termino = t;
}
