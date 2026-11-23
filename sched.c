/*
 * sched.c - os escalonadores: RM, EDF, desempate comum e registro.
 *
 * Junta, nesta ordem, src/sched/sched_rm.c, sched_edf.c, sched_tie.c e
 * sched_registro.c da versão organizada em pastas. Cada trecho começa com
 * um divisor e com o comentário de cabeçalho original. Os #include de
 * todos os trechos ficam juntos logo abaixo; o _POSIX_C_SOURCE (do trecho
 * sched_registro.c) precisa vir antes de qualquer #include.
 */
/* dlopen/dlsym são POSIX; em glibc >= 2.34 fazem parte da própria libc, então
 * o requisito "sem bibliotecas extras" continua atendido. */
#define _POSIX_C_SOURCE 200809L

#include "sched.h"
#include "config.h"

#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

/* ===========================================================================
 * Seção sched_rm.c (originalmente src/sched/sched_rm.c)
 * =========================================================================== */
/*
 * sched_rm.c - Rate Monotonic preemptivo (capítulo 2, seção 2.4.1).
 *
 * Prioridade fixa: quanto MENOR o período, MAIOR a prioridade. Não depende
 * do tempo nem da ativação atual, por isso `agora` não é usado.
 *
 * Como funciona na prática: uma tarefa que acorda a cada 100 ticks é mais
 * prioritária que uma que acorda a cada 150. Como "menor valor = mais
 * prioritária" (ver sched.h), basta devolver o próprio período.
 *
 * Preemptivo: a cada tick o escalonador reavalia. Se uma tarefa de período
 * menor chega, ela tira a CPU de uma de período maior no mesmo instante.
 */

static int prioridade_rm(const TCB *k, int agora) {
    (void)agora;   /* o cast para void diz ao compilador que ignorar o
                    * parâmetro é intencional (evita warning) */
    return k->periodo;
}

/* Registro do algoritmo: nome usado no arquivo de configuração, descrição
 * mostrada ao usuário e a função de prioridade. A seção sched_registro.c coloca
 * este objeto na tabela de algoritmos disponíveis. */
const Escalonador escalonador_rm = {
    "RM",
    "Rate Monotonic: prioridade fixa, menor período = maior prioridade",
    prioridade_rm,
};

/* ===========================================================================
 * Seção sched_edf.c (originalmente src/sched/sched_edf.c)
 * =========================================================================== */
/*
 * sched_edf.c - Earliest Deadline First preemptivo (capítulo 2, seção 2.4.2).
 *
 * Prioridade dinâmica: executa quem tem o deadline ABSOLUTO (chegada da
 * ativação atual + prazo) mais próximo. Como o deadline muda a cada
 * ativação, a prioridade de uma tarefa muda ao longo do tempo.
 *
 * Diferença para o RM, em uma frase: no RM a "importância" da tarefa é
 * sempre a mesma (o período); no EDF ela depende de quão perto do prazo a
 * ativação atual está. Por isso, em casos como a Figura 2.6 do capítulo, o
 * EDF cumpre todos os prazos e o RM perde um.
 *
 * `deadline_abs` é calculado em sim.c (iniciar_ativacao) quando a ativação
 * começa; aqui só o devolvemos como prioridade.
 */

static int prioridade_edf(const TCB *k, int agora) {
    (void)agora;
    return k->deadline_abs;
}

/* Registro do algoritmo (ver seção sched_rm.c). */
const Escalonador escalonador_edf = {
    "EDF",
    "Earliest Deadline First: prioridade dinâmica, deadline mais próximo primeiro",
    prioridade_edf,
};

/* ===========================================================================
 * Seção sched_tie.c (originalmente src/sched/sched_tie.c)
 * =========================================================================== */
/*
 * sched_tie.c - cadeia de desempate comum a todos os algoritmos (req. 4.3).
 *
 * Ordem dos critérios (o primeiro que diferenciar decide):
 *   1. a tarefa que estava executando logo antes do escalonamento (evita
 *      trocar de contexto para a própria tarefa);
 *   2. o prazo da tarefa: menor primeiro. É o campo `prazo` do arquivo (o
 *      valor relativo, contado a partir da ativação), como pede o enunciado,
 *      e não o deadline absoluto. No EDF a prioridade já é o deadline
 *      absoluto, então este critério só desempata tarefas com o mesmo
 *      deadline absoluto;
 *   3. o instante de ingresso da tarefa: quem chegou antes. É o campo
 *      `ingresso` do arquivo, que o enunciado define como o instante em que
 *      a tarefa foi criada (e não a chegada da ativação atual);
 *   4. a duração: menor primeiro;
 *   5. sorteio: feito em sim.c, que guarda o estado do gerador aleatório
 *      (precisa ser reproduzível ao retroceder/avançar).
 *
 * o quantum NÃO entra na comparação. Ao
 * fim do quantum o escalonador é chamado de novo (sim.c registra o evento
 * "fim de quantum") e, pelo critério 1, a mesma tarefa continua se ainda for
 * a de maior prioridade. Assim a ordem 1 -> 5 do enunciado vale sempre e o
 * quantum não distorce RM/EDF (uma tarefa com deadline mais próximo nunca
 * perde a CPU para uma empatada só porque esgotou a fatia de tempo).
 *
 * Convenção de TODAS as funções de comparação daqui (a mesma do qsort):
 *   resultado < 0  -> `a` vem ANTES de `b` (a é mais prioritária)
 *   resultado > 0  -> `a` vem DEPOIS de `b`
 *   resultado == 0 -> empate total (quem decide é o sorteio, em sim.c)
 */

/* Critérios 1 a 4. Só é chamada quando a prioridade ativa já empatou. */
int sched_desempate(const TCB *a, const TCB *b) {
    /* Critério 1: `cpu` >= 0 significa que executou no tick anterior.
     * `ea != eb` quer dizer que só uma das duas estava executando: ela
     * ganha (-1 = a primeira vem antes). */
    bool ea = a->cpu >= 0, eb = b->cpu >= 0;
    if (ea != eb) return ea ? -1 : 1;
    /* Critério 2: o prazo da tarefa (campo `prazo` do arquivo), menor primeiro */
    if (a->prazo != b->prazo) return a->prazo < b->prazo ? -1 : 1;
    /* Critério 3 */
    if (a->ingresso != b->ingresso) return a->ingresso < b->ingresso ? -1 : 1;
    /* Critério 4 */
    if (a->duracao != b->duracao) return a->duracao < b->duracao ? -1 : 1;
    /* Critério 5 fica para o chamador. */
    return 0;
}

/* Comparação completa: primeiro a prioridade ATIVA (menor = melhor); só se
 * empatar chama o desempate. É a função que o escalonador (sim.c) usa para
 * ordenar as candidatas. */
int sched_comparar(const TCB *a, const TCB *b) {
    if (a->prio_ativa != b->prio_ativa) return a->prio_ativa < b->prio_ativa ? -1 : 1;
    return sched_desempate(a, b);
}

/* ===========================================================================
 * Seção sched_formula.c (originalmente src/sched/sched_formula.c)
 * =========================================================================== */
/*
 * sched_formula.c - escalonador definido num ARQUIVO DE TEXTO pela fórmula
 * da sua prioridade nominal (req. 4.2). Exemplo (LLF.txt):
 *
 *     # LLF.txt - Least Laxity First
 *     nome: LLF
 *     descricao: menor folga primeiro
 *     prioridade: deadline_abs - agora - restante
 *
 * Por que uma fórmula interpretada, além do nativo e do plugin .so:
 *   - não precisa de compilador nem de recompilar nada;
 *   - é texto: o mesmo arquivo funciona no macOS, no Linux e no Windows, em
 *     qualquer processador (um .so de Linux x86-64 não abre num Mac arm64);
 *   - não executa código de fora: o arquivo só combina os campos abaixo com
 *     contas, então não tem como travar o programa, ler memória alheia ou
 *     mexer em arquivos.
 * O limite: só cabem algoritmos cuja prioridade é um NÚMERO calculado a
 * partir da tarefa e do instante (o mesmo contrato do prioridade_fn). O
 * Round Robin cabe, pois o campo fila_desde guarda a ordem de entrada na
 * fila (com o quantum, ver sched.h). Filas multinível e loteria precisam de
 * um estado próprio (níveis, sorteio) e não são expressáveis assim.
 *
 * O arquivo: uma chave por linha, "chave: valor" ou "chave = valor"; chaves
 * nome (obrigatória), prioridade (obrigatória) e descricao/descrição
 * (opcional), sem diferenciar maiúsculas (req. 3.3.2). Linha vazia e linha
 * começando com '#' são ignoradas; BOM e \r\n são tratados pelo mesmo leitor
 * do arquivo de configuração (leitor_abrir/leitor_proxima, em config.c).
 *
 * A fórmula (sem diferenciar maiúsculas):
 *   - inteiros de 0 a 2147483647, + - * / %, menos unário e parênteses;
 *   - funções min(a, b, ...), max(a, b, ...) e abs(a);
 *   - campos do TCB da tarefa avaliada: periodo, prazo, duracao, ingresso,
 *     restante, chegada_atual, deadline_abs e fila_desde (instante em que a
 *     tarefa entrou no fim da fila: é o que permite um escalonador usar o
 *     quantum, ver sched.h); e agora (o tick atual).
 *   A divisão e o resto são os do C: a divisão trunca para zero (7 / 2 = 3,
 *   -7 / 2 = -3) e o resto tem o sinal do dividendo (-7 % 2 = -1).
 *
 * Como funciona:
 *   1. Na CARGA, um parser descendente recursivo (uma função por nível de
 *      precedência: soma, produto, menos unário e operando) confere a
 *      fórmula e a traduz para notação pós-fixa (RPN): "deadline_abs -
 *      agora - restante" vira [deadline_abs agora - restante -]. Todo erro
 *      (parêntese, operador sem operando, campo, função, número) aparece
 *      aqui, com a coluna, antes de o escalonador existir.
 *   2. Na AVALIAÇÃO (a cada tick, para cada candidata), a RPN roda numa
 *      pilha local: campos e números são empilhados, e cada operador
 *      desempilha seus operandos e empilha o resultado. Não há parsing de
 *      novo, e a avaliação é PURA: depende só da RPN (que não muda depois da
 *      carga), do TCB e de `agora`, sem variável static ou global. É isso
 *      que faz voltar no tempo e avançar de novo dar o mesmo resultado: o
 *      simulador restaura os TCBs de um snapshot, e não existe nenhum estado
 *      do escalonador fora deles para ficar para trás.
 *   3. As contas são feitas em int64_t, conferindo estouro em cada uma; o
 *      resultado precisa caber num int (a prioridade do TCB). Divisão por
 *      zero, estouro ou resultado fora do int NÃO viram um valor inventado
 *      (INT_MAX, 0...), porque isso mudaria o escalonamento sem aviso: a
 *      avaliação falha com o motivo e a simulação para naquele tick.
 */

#define FORMULA_PILHA_MAX 64    /* valores guardados ao mesmo tempo na avaliação */
#define ESC_LINHA_MAX 1000      /* linha mais longa aceita no arquivo .txt       */
#define ESC_NOME_MAX 31         /* caracteres do nome (como o id da tarefa)     */
#define ESC_ERROS_MAX 10        /* depois disso o arquivo não parece um escalonador */

typedef enum { F_NUM, F_CAMPO, F_SOMA, F_SUB, F_MUL, F_DIV, F_MOD, F_NEG, F_MIN, F_MAX, F_ABS } OpFormula;

/* Uma instrução da RPN. `arg` é o número (F_NUM), o campo (F_CAMPO) ou a
 * quantidade de argumentos (F_MIN, F_MAX); nas outras não é usado. */
typedef struct { OpFormula op; int arg; } InstrFormula;

/* A fórmula compilada: a RPN, montada uma vez na carga e só lida depois. */
typedef struct Formula { InstrFormula *v; int n, cap; } Formula;

/* Os campos que a fórmula enxerga, na ordem do enum. São só leitura e só da
 * própria tarefa: nada que dependa de outra tarefa ou de estado escondido. */
typedef enum { CF_PERIODO, CF_PRAZO, CF_DURACAO, CF_INGRESSO, CF_RESTANTE, CF_CHEGADA_ATUAL, CF_DEADLINE_ABS,
               CF_FILA_DESDE, CF_AGORA, CF_N } CampoFormula;
static const char *const CAMPOS_FORMULA[CF_N] = {"periodo", "prazo", "duracao", "ingresso", "restante",
                                                  "chegada_atual", "deadline_abs", "fila_desde", "agora"};
#define LISTA_CAMPOS "periodo, prazo, duracao, ingresso, restante, chegada_atual, deadline_abs, fila_desde e agora"

static int64_t valor_campo(CampoFormula c, const TCB *k, int agora) {
    switch (c) {
    case CF_PERIODO: return k->periodo;
    case CF_PRAZO: return k->prazo;
    case CF_DURACAO: return k->duracao;
    case CF_INGRESSO: return k->ingresso;
    case CF_RESTANTE: return k->restante;
    case CF_CHEGADA_ATUAL: return k->chegada_atual;
    case CF_DEADLINE_ABS: return k->deadline_abs;
    case CF_FILA_DESDE: return k->fila_desde;
    default: return agora;
    }
}

enum { FN_MIN, FN_MAX, FN_ABS, FN_N };
static const char *const FUNCOES_FORMULA[FN_N] = {"min", "max", "abs"};

/* ---- compilação (parser descendente recursivo) ---- */

typedef struct {
    const char *ini, *p;   /* a fórmula inteira e a posição atual              */
    Formula *f;            /* onde a RPN é montada                             */
    char *erro;            /* motivo do primeiro erro ("" = nenhum)            */
    size_t tam;
    int prof, prof_max;    /* altura da pilha da avaliação, simulada aqui      */
    char ultimo;           /* último operador lido e sua coluna, para dizer    */
    int col_ultimo;        /*   "falta um operando depois de '+'"              */
} Parser;

static bool eh_letra(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool eh_dig(char c) { return c >= '0' && c <= '9'; }
static bool p_falhou(const Parser *ps) { return ps->erro[0] != '\0'; }
static void p_espacos(Parser *ps) { while (*ps->p == ' ' || *ps->p == '\t') ps->p++; }

/* Coluna (a partir de 1) de `pos`, em caracteres UTF-8, não em bytes. */
static int p_coluna(const Parser *ps, const char *pos) {
    int c = 1;
    for (const char *q = ps->ini; q < pos; q++)
        if (((unsigned char)*q & 0xC0) != 0x80) c++;
    return c;
}

/* Guarda só o PRIMEIRO erro: os seguintes seriam consequência dele. */
static void p_erro(Parser *ps, const char *fmt, ...) {
    if (p_falhou(ps)) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ps->erro, ps->tam, fmt, ap);
    va_end(ap);
}

/* Acrescenta uma instrução à RPN. `efeito` é quanto ela muda a altura da
 * pilha (+1 empilha, -1 junta dois em um...), para saber na carga o tamanho
 * de pilha que a avaliação vai precisar. */
static void p_emitir(Parser *ps, OpFormula op, int arg, int efeito) {
    if (p_falhou(ps)) return;
    Formula *f = ps->f;
    if (f->n == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 16;
        f->v = xrealloc(f->v, (size_t)f->cap * sizeof *f->v);
    }
    f->v[f->n++] = (InstrFormula){op, arg};
    ps->prof += efeito;
    if (ps->prof > ps->prof_max) ps->prof_max = ps->prof;
}

/* Índice do nome (ini, len) em `nomes`, sem diferenciar maiúsculas; -1 se não está. */
static int achar_nome(const char *ini, int len, const char *const *nomes, int n) {
    for (int i = 0; i < n; i++) {
        int j = 0;
        while (j < len && nomes[i][j] && tolower((unsigned char)ini[j]) == nomes[i][j]) j++;
        if (j == len && !nomes[i][j]) return i;
    }
    return -1;
}

static void p_caractere_invalido(Parser *ps) {
    unsigned char c = (unsigned char)*ps->p;
    int col = p_coluna(ps, ps->p);
    if (c < 0x20 || c >= 0x7F)
        p_erro(ps, "caractere inválido na coluna %d (byte 0x%02X); use só números, campos, "
               "+ - * / %%, parênteses e vírgulas", col, c);
    else
        p_erro(ps, "caractere inválido '%c' na coluna %d; use só números, campos, + - * / %%, "
               "parênteses e vírgulas", c, col);
}

/* O caractere em ps->p não cabe ali (depois de uma expressão completa):
 * explica por quê. `col_abre` é a coluna do '(' ainda aberto (0 = nenhum). */
static void p_inesperado(Parser *ps, int col_abre) {
    char c = *ps->p;
    int col = p_coluna(ps, ps->p);
    if (!c) p_erro(ps, "o parêntese aberto na coluna %d não foi fechado", col_abre);
    else if (c == ')') p_erro(ps, "')' sobrando na coluna %d: não há '(' aberto para ele", col);
    else if (eh_dig(c) || eh_letra(c) || c == '(')
        p_erro(ps, "dois operandos seguidos: falta um operador (+ - * / %%) antes da coluna %d", col);
    else if (c == ',') p_erro(ps, "',' na coluna %d fora dos argumentos de min(...) ou max(...)", col);
    else p_caractere_invalido(ps);
}

static void p_expr(Parser *ps);

/* nome(arg, arg, ...): ps->p está no '('. */
static void p_chamada(Parser *ps, int fn, int col_nome) {
    int col_abre = p_coluna(ps, ps->p), nargs = 0;
    ps->ultimo = '(';
    ps->col_ultimo = col_abre;
    ps->p++;
    p_espacos(ps);
    if (*ps->p == ')') {
        ps->p++;
    } else {
        for (;;) {
            p_expr(ps);
            if (p_falhou(ps)) return;
            nargs++;
            p_espacos(ps);
            if (*ps->p == ',') {
                ps->ultimo = ',';
                ps->col_ultimo = p_coluna(ps, ps->p);
                ps->p++;
                continue;
            }
            if (*ps->p == ')') {
                ps->p++;
                break;
            }
            p_inesperado(ps, col_abre);
            return;
        }
    }
    if (fn == FN_ABS && nargs != 1) {
        p_erro(ps, "abs (coluna %d) recebe exatamente 1 argumento, mas recebeu %d", col_nome, nargs);
    } else if (fn != FN_ABS && nargs < 1) {
        p_erro(ps, "%s (coluna %d) precisa de pelo menos 1 argumento: %s(a, b, ...)", FUNCOES_FORMULA[fn], col_nome,
               FUNCOES_FORMULA[fn]);
    } else if (fn == FN_ABS) {
        p_emitir(ps, F_ABS, 0, 0);
    } else {
        /* junta os nargs valores do topo em um */
        p_emitir(ps, fn == FN_MIN ? F_MIN : F_MAX, nargs, -(nargs - 1));
    }
}

/* Operando: número, campo, função(...) ou (expressão). */
static void p_operando(Parser *ps) {
    p_espacos(ps);
    const char *ini = ps->p;
    int col = p_coluna(ps, ini);
    char c = *ini;
    if (eh_dig(c)) {
        int64_t v = 0;
        bool grande = false;
        for (; eh_dig(*ps->p); ps->p++)
            if (!grande && (v = v * 10 + (*ps->p - '0')) > INT_MAX) grande = true;
        if (grande)
            p_erro(ps, "número grande demais '%.*s' na coluna %d: o máximo é %d", (int)(ps->p - ini), ini, col,
                   INT_MAX);
        else
            p_emitir(ps, F_NUM, (int)v, +1);
        return;
    }
    if (eh_letra(c)) {
        while (eh_letra(*ps->p) || eh_dig(*ps->p)) ps->p++;
        int len = (int)(ps->p - ini);
        int campo = achar_nome(ini, len, CAMPOS_FORMULA, CF_N), fn = achar_nome(ini, len, FUNCOES_FORMULA, FN_N);
        p_espacos(ps);
        if (*ps->p == '(') {
            if (fn >= 0) p_chamada(ps, fn, col);
            else if (campo >= 0) p_erro(ps, "'%.*s' (coluna %d) é um campo, não uma função: tire o '(' depois dele", len, ini, col);
            else p_erro(ps, "função desconhecida '%.*s' na coluna %d; as funções são min(a, b, ...), max(a, b, ...) "
                        "e abs(a)", len, ini, col);
        } else if (campo >= 0) {
            p_emitir(ps, F_CAMPO, campo, +1);
        } else if (fn >= 0) {
            p_erro(ps, "'%.*s' (coluna %d) é uma função: escreva %.*s(...)", len, ini, col, len, ini);
        } else {
            p_erro(ps, "campo desconhecido '%.*s' na coluna %d; os campos são: " LISTA_CAMPOS, len, ini, col);
        }
        return;
    }
    if (c == '(') {
        ps->ultimo = '(';
        ps->col_ultimo = col;
        ps->p++;
        p_expr(ps);
        if (p_falhou(ps)) return;
        p_espacos(ps);
        if (*ps->p == ')') ps->p++;
        else p_inesperado(ps, col);
        return;
    }
    /* aqui deveria começar um operando, e não começa */
    if (!c && ps->ultimo) p_erro(ps, "falta um operando depois de '%c' (coluna %d)", ps->ultimo, ps->col_ultimo);
    else if (!c) p_erro(ps, "a fórmula está vazia");
    else if (strchr("+*/%),", c)) p_erro(ps, "falta um operando antes de '%c' (coluna %d)", c, col);
    else p_caractere_invalido(ps);
}

/* Menos unário (pode repetir: --x = x). Tem precedência maior que * / %. */
static void p_unario(Parser *ps) {
    p_espacos(ps);
    if (*ps->p != '-') {
        p_operando(ps);
        return;
    }
    ps->ultimo = '-';
    ps->col_ultimo = p_coluna(ps, ps->p);
    ps->p++;
    p_unario(ps);
    p_emitir(ps, F_NEG, 0, 0);
}

/* Produto: unario (('*' | '/' | '%') unario)*, da esquerda para a direita. */
static void p_termo(Parser *ps) {
    p_unario(ps);
    for (;;) {
        if (p_falhou(ps)) return;
        p_espacos(ps);
        char c = *ps->p;
        if (c != '*' && c != '/' && c != '%') return;
        ps->ultimo = c;
        ps->col_ultimo = p_coluna(ps, ps->p);
        ps->p++;
        p_unario(ps);
        p_emitir(ps, c == '*' ? F_MUL : c == '/' ? F_DIV : F_MOD, 0, -1);
    }
}

/* Soma: termo (('+' | '-') termo)*, da esquerda para a direita, então
 * "a - b - c" é (a - b) - c. */
static void p_expr(Parser *ps) {
    p_termo(ps);
    for (;;) {
        if (p_falhou(ps)) return;
        p_espacos(ps);
        char c = *ps->p;
        if (c != '+' && c != '-') return;
        ps->ultimo = c;
        ps->col_ultimo = p_coluna(ps, ps->p);
        ps->p++;
        p_termo(ps);
        p_emitir(ps, c == '+' ? F_SOMA : F_SUB, 0, -1);
    }
}

static void formula_liberar(Formula *f) {
    if (!f) return;
    free(f->v);
    free(f);
}

/* Compila `texto` para RPN. Devolve a fórmula (alocada), ou NULL com o
 * motivo (e a coluna) em `erro`. */
static Formula *formula_compilar(const char *texto, char *erro, size_t tam) {
    Formula *f = xcalloc(1, sizeof *f);
    Parser ps = {texto, texto, f, erro, tam, 0, 0, '\0', 0};
    erro[0] = '\0';
    p_expr(&ps);
    if (!p_falhou(&ps)) {
        p_espacos(&ps);
        if (*ps.p) p_inesperado(&ps, 0);   /* sobrou algo depois da expressão completa */
    }
    if (!p_falhou(&ps) && ps.prof_max > FORMULA_PILHA_MAX)
        p_erro(&ps, "fórmula complicada demais: a conta precisa guardar %d valores ao mesmo tempo, e o máximo é %d",
               ps.prof_max, FORMULA_PILHA_MAX);
    if (p_falhou(&ps)) {
        formula_liberar(f);
        return NULL;
    }
    return f;
}

/* ---- avaliação ---- */

/* Contas em int64_t que conferem o estouro ANTES de fazer a operação (em C,
 * estourar um inteiro com sinal é comportamento indefinido). São as checagens
 * clássicas (CERT INT32-C), só com +, -, * e / dos próprios int64_t. */
static bool somar64(int64_t a, int64_t b, int64_t *r) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *r = a + b;
    return true;
}

static bool subtrair64(int64_t a, int64_t b, int64_t *r) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
    *r = a - b;
    return true;
}

static bool multiplicar64(int64_t a, int64_t b, int64_t *r) {
    if (a > 0) {
        if (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a) return false;
    } else if (a < 0) {
        if (b > 0 ? a < INT64_MIN / b : b < INT64_MAX / a) return false;
    }
    *r = a * b;
    return true;
}

/* Avalia a fórmula para a tarefa `k` no tick `agora`. PURA: lê só a RPN, o
 * TCB e `agora`; a pilha é local. Devolve true com a prioridade em *res, ou
 * false com o motivo em `erro`. A compilação garante que toda operação tem
 * seus operandos na pilha e que ela não passa de FORMULA_PILHA_MAX. */
static bool formula_avaliar(const Formula *f, const TCB *k, int agora, int *res, char *erro, size_t tam) {
    int64_t pilha[FORMULA_PILHA_MAX];
    int topo = 0;
    for (int i = 0; i < f->n; i++) {
        const InstrFormula *in = &f->v[i];
        int64_t a, b, r = 0;
        bool ok = true;
        switch (in->op) {
        case F_NUM: pilha[topo++] = in->arg; continue;
        case F_CAMPO: pilha[topo++] = valor_campo((CampoFormula)in->arg, k, agora); continue;
        case F_NEG:
        case F_ABS:
            a = pilha[topo - 1];
            if (a == INT64_MIN) {   /* -INT64_MIN não existe em 64 bits */
                snprintf(erro, tam, "estouro numérico ao trocar o sinal de %" PRId64, a);
                return false;
            }
            pilha[topo - 1] = in->op == F_NEG || a < 0 ? -a : a;
            continue;
        case F_MIN:
        case F_MAX:
            topo -= in->arg;
            r = pilha[topo];
            for (int j = 1; j < in->arg; j++)
                if (in->op == F_MIN ? pilha[topo + j] < r : pilha[topo + j] > r) r = pilha[topo + j];
            pilha[topo++] = r;
            continue;
        default:
            break;
        }
        /* operadores binários: desempilha b (o da direita) e depois a */
        b = pilha[--topo];
        a = pilha[topo - 1];
        switch (in->op) {
        case F_SOMA: ok = somar64(a, b, &r); break;
        case F_SUB: ok = subtrair64(a, b, &r); break;
        case F_MUL: ok = multiplicar64(a, b, &r); break;
        case F_DIV:
        case F_MOD:
            if (b == 0) {
                snprintf(erro, tam, in->op == F_DIV ? "divisão por zero (%" PRId64 " / 0)"
                                                    : "resto da divisão por zero (%" PRId64 " %% 0)", a);
                return false;
            }
            /* INT64_MIN / -1 estoura; INT64_MIN % -1 vale 0 (em C as duas são indefinidas) */
            if (a == INT64_MIN && b == -1) ok = in->op == F_MOD;
            else r = in->op == F_DIV ? a / b : a % b;   /* a do C: trunca para zero */
            break;
        default: break;
        }
        if (!ok) {
            const char simbolo[] = {[F_SOMA] = '+', [F_SUB] = '-', [F_MUL] = '*', [F_DIV] = '/'};
            snprintf(erro, tam, "estouro numérico em %" PRId64 " %c %" PRId64 " (não cabe em 64 bits)", a,
                     simbolo[in->op], b);
            return false;
        }
        pilha[topo - 1] = r;
    }
    if (pilha[0] < INT_MIN || pilha[0] > INT_MAX) {
        snprintf(erro, tam, "o resultado %" PRId64 " não cabe num int (de %d a %d)", pilha[0], INT_MIN, INT_MAX);
        return false;
    }
    *res = (int)pilha[0];
    return true;
}

/* ---- leitura do arquivo .txt ---- */

/* O que um arquivo .txt define, já conferido. */
typedef struct {
    char *nome, *descricao;   /* alocados                                        */
    Formula *formula;         /* compilada                                       */
    int linha_nome;           /* para "arquivo:linha:" nas mensagens do registro */
} DefFormula;

enum { CH_NOME, CH_DESCRICAO, CH_PRIORIDADE, CH_N };
static const char *const CHAVES_FORMULA[CH_N] = {"nome", "descricao", "prioridade"};

/* Qual chave é (sem diferenciar maiúsculas), -1 se nenhuma. "descrição" é
 * aceita com acento em UTF-8 e em Latin-1 (arquivo salvo como ANSI). */
static int chave_formula(const char *s) {
    static const char *const descricao[] = {"descricao", "descrição", "DESCRIÇÃO", "descri\xe7\xe3o", "DESCRI\xc7\xc3O"};
    for (int i = 0; i < CH_N; i++)
        if (str_ieq(s, CHAVES_FORMULA[i])) return i;
    for (size_t i = 0; i < sizeof descricao / sizeof descricao[0]; i++)
        if (str_ieq(s, descricao[i])) return CH_DESCRICAO;
    return -1;
}

/* NULL se o nome serve, ou o motivo. O nome vai na 1ª linha do arquivo de
 * configuração ("LLF;2;1"), por isso não pode ter ';' nem espaço. */
static const char *nome_invalido(const char *s) {
    int chars = 0;
    for (const unsigned char *q = (const unsigned char *)s; *q; q++) {
        if (*q < 0x20 || *q == 0x7F) return "tem um caractere de controle";
        if (*q == ' ') return "não pode ter espaços (ele vai na 1ª linha do arquivo de configuração, ex.: LLF;2;1)";
        if (*q == ';') return "não pode ter ';', que separa os campos do arquivo de configuração";
        if ((*q & 0xC0) != 0x80) chars++;
    }
    return chars > ESC_NOME_MAX ? "tem mais de 31 caracteres" : NULL;
}

/* Nome do arquivo sem a pasta, para a descrição padrão. */
static const char *sem_pasta(const char *caminho) {
    const char *b = caminho;
    for (const char *q = caminho; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

static void formula_def_liberar(DefFormula *d) {
    free(d->nome);
    free(d->descricao);
    formula_liberar(d->formula);
    memset(d, 0, sizeof *d);
}

/* Lê e confere um arquivo .txt de escalonador. Junta TODOS os problemas em
 * `erros` (como o config.c faz), no formato "arquivo:linha: motivo". Devolve
 * true com `def` preenchido só se não houve nenhum. Não mexe na tabela de
 * escalonadores: quem registra é sched_carregar_arquivo, e só se der certo. */
static bool formula_ler_arquivo(const char *caminho, DefFormula *def, ListaMsg *erros) {
    memset(def, 0, sizeof *def);
    int n0 = erros->n;
    LeitorLinhas l;
    if (!leitor_abrir(&l, caminho, erros)) return false;
    int linha_de[CH_N] = {0};       /* linha onde cada chave apareceu (0 = ainda não) */
    const char *valor[CH_N] = {NULL};
    int uteis = 0;
    for (char *s; (s = leitor_proxima(&l)) && erros->n - n0 < ESC_ERROS_MAX;) {
        if (!*s || *s == '#') continue;
        uteis++;
        size_t len = strlen(s);
        char *sep = strpbrk(s, ":=");
        if (len > ESC_LINHA_MAX) {
            msg_add(erros, "%s:%d: linha muito longa (%zu caracteres; o máximo é %d)", caminho, l.linha, len, ESC_LINHA_MAX);
            /* a chave dela conta como vista: senão viria um "falta a chave"
             * que é só consequência deste erro */
            if (sep) {
                *sep = '\0';
                int ch = chave_formula(str_trim(s));
                if (ch >= 0 && !linha_de[ch]) linha_de[ch] = l.linha;
            }
            continue;
        }
        if (!sep) {
            msg_add(erros, "%s:%d: linha sem ':' nem '=': cada linha deve ser 'chave: valor' (é mesmo um "
                    "arquivo de escalonador?)", caminho, l.linha);
            continue;
        }
        *sep = '\0';
        char *chave = str_trim(s), *v = str_trim(sep + 1);
        int ch = chave_formula(chave);
        if (ch < 0) {
            msg_add(erros, "%s:%d: chave desconhecida '%.40s'; as chaves válidas são nome, descricao (ou "
                    "descrição) e prioridade", caminho, l.linha, chave);
            continue;
        }
        if (linha_de[ch]) {
            msg_add(erros, "%s:%d: chave '%s' repetida (já definida na linha %d)", caminho, l.linha,
                    CHAVES_FORMULA[ch], linha_de[ch]);
            continue;
        }
        linha_de[ch] = l.linha;
        if (!*v) {
            msg_add(erros, "%s:%d: a chave '%s' está sem valor", caminho, l.linha, CHAVES_FORMULA[ch]);
            continue;
        }
        valor[ch] = v;
        if (ch == CH_NOME) {
            const char *m = nome_invalido(v);
            if (m) msg_add(erros, "%s:%d: nome '%.40s' inválido: %s", caminho, l.linha, v, m);
        } else if (ch == CH_PRIORIDADE) {
            char motivo[400];
            def->formula = formula_compilar(v, motivo, sizeof motivo);
            if (!def->formula) msg_add(erros, "%s:%d: fórmula da prioridade: %s", caminho, l.linha, motivo);
        }
    }
    if (erros->n - n0 >= ESC_ERROS_MAX) {
        msg_add(erros, "%s: erros demais; o arquivo não parece um escalonador (veja o formato no README)", caminho);
    } else if (!uteis) {
        msg_add(erros, "%s: o arquivo está vazio ou só tem comentários; esperado 'nome: ...' e 'prioridade: ...'",
                caminho);
    } else {
        if (!linha_de[CH_NOME]) msg_add(erros, "%s: falta a chave 'nome' (ex.: nome: LLF)", caminho);
        if (!linha_de[CH_PRIORIDADE])
            msg_add(erros, "%s: falta a chave 'prioridade' (ex.: prioridade: deadline_abs - agora - restante)", caminho);
    }
    bool ok = erros->n == n0;
    if (ok) {
        char desc[600];
        snprintf(desc, sizeof desc, "carregado de %s", sem_pasta(caminho));
        def->nome = xstrdup(valor[CH_NOME]);
        def->descricao = xstrdup(valor[CH_DESCRICAO] ? valor[CH_DESCRICAO] : desc);
        def->linha_nome = linha_de[CH_NOME];
    } else {
        formula_def_liberar(def);   /* tudo ou nada: nada do que foi lido sobra */
    }
    leitor_fechar(&l);      /* `valor` apontava para dentro do texto: já foi copiado */
    return ok;
}

/* ===========================================================================
 * Seção sched_registro.c (originalmente src/sched/sched_registro.c)
 * =========================================================================== */
/*
 * sched_registro.c - tabela de escalonadores disponíveis.
 *
 * A simulação só conhece o índice de um escalonador nesta tabela. Incluir
 * um algoritmo novo é acrescentar uma linha em `nativos`, ou carregar um
 * plugin .so ou um arquivo .txt com a fórmula em tempo de execução.
 *
 * Isso é o que atende o requisito 4.2 ("novos algoritmos sem modificar o
 * código da simulação"): sim.c nunca menciona RM, EDF ou fórmula, só pede
 * sched_obter(indice) e sched_atualizar.
 */

/* `extern` = "este objeto existe em outro arquivo .c" (seções sched_rm.c e
 * sched_edf.c). O linker junta tudo na hora de gerar o executável. */
extern const Escalonador escalonador_rm;
extern const Escalonador escalonador_edf;

#define MAX_ESCALONADORES 32

/* Os algoritmos compilados no programa, nesta ordem: RM é o 0 e EDF é o 1.
 * Incluir um nativo novo é só acrescentá-lo aqui: a quantidade sai do
 * sizeof, então não existe um contador para lembrar de atualizar (antes era
 * `quantidade = 2`, fixo). */
static const Escalonador *const nativos[] = {
    &escalonador_rm,
    &escalonador_edf,
};
#define N_NATIVOS ((int)(sizeof nativos / sizeof nativos[0]))

/* Os carregados durante a execução (plugin .so/.dylib ou fórmula .txt).
 * Entram SEMPRE depois dos nativos e nunca saem: a configuração e cada
 * snapshot do histórico guardam o ÍNDICE do algoritmo, que precisa continuar
 * apontando para o mesmo escalonador até o fim do programa.
 *
 * `formula` é o "vetor de fórmulas em paralelo à tabela": prioridade_fn não
 * recebe contexto, então uma função genérica não saberia QUAL fórmula
 * avaliar. Em vez de mudar a assinatura (e quebrar RM, EDF e os .so, que são
 * lidos pelo layout atual da struct Escalonador), o registro guarda a fórmula
 * ao lado do escalonador, e sched_atualizar decide: com fórmula, avalia a
 * fórmula; sem fórmula (nativo ou .so), chama e->prioridade como sempre. */
typedef struct {
    const Escalonador *esc;   /* o que o resto do programa enxerga                  */
    Formula *formula;          /* fórmula compilada (.txt); NULL para plugin .so     */
    void *handle;              /* biblioteca aberta (.so); NULL para fórmula         */
    char *origem;              /* de onde veio, para as mensagens                    */
} Carregado;

static Carregado carregados[MAX_ESCALONADORES - N_NATIVOS];
static int ncarregados;

/* Libera os carregados na saída do programa (atexit): fecha os .so (dlclose)
 * e devolve a memória das fórmulas, para leaks/valgrind saírem limpos. Só
 * roda no fim, porque um escalonador nunca é descarregado com o programa
 * rodando. */
static void liberar_carregados(void) {
    while (ncarregados > 0) {
        Carregado *c = &carregados[--ncarregados];
        if (c->formula) {   /* escalonador de fórmula: a struct e os textos são nossos */
            formula_liberar(c->formula);
            free((void *)c->esc->nome);
            free((void *)c->esc->descricao);
            free((void *)c->esc);
        }
#ifndef _WIN32
        if (c->handle) dlclose(c->handle);
#endif
        free(c->origem);
    }
}

int sched_quantidade(void) { return N_NATIVOS + ncarregados; }

/* Devolve o escalonador do índice, ou NULL se o índice não existe. */
const Escalonador *sched_obter(int idx) {
    if (idx < 0 || idx >= sched_quantidade()) return NULL;
    return idx < N_NATIVOS ? nativos[idx] : carregados[idx - N_NATIVOS].esc;
}

/* Procura pelo nome do arquivo ("rm", "EDF", ...) sem diferenciar caixa
 *. Devolve o índice ou -1. */
int sched_buscar(const char *nome) {
    for (int i = 0; i < sched_quantidade(); i++)
        if (str_ieq(nome, sched_obter(i)->nome)) return i;
    return -1;
}

/* Escreve "RM, EDF" (e plugins) no buffer, para mensagens de erro do tipo
 * "algoritmo desconhecido; use RM, EDF". snprintf com o espaço restante
 * (tam - usado) evita estourar o buffer. */
void sched_listar_nomes(char *buf, int tam) {
    buf[0] = '\0';
    for (int i = 0; i < sched_quantidade(); i++) {
        size_t usado = strlen(buf);
        snprintf(buf + usado, (size_t)tam - usado, "%s%s", i ? ", " : "", sched_obter(i)->nome);
    }
}

/* A fórmula do escalonador, ou NULL se ele é nativo ou plugin .so. */
static const Formula *formula_de(const Escalonador *e) {
    for (int i = 0; i < ncarregados; i++)
        if (carregados[i].esc == e) return carregados[i].formula;
    return NULL;
}

/* Pergunta ao algoritmo qual a prioridade da tarefa neste tick e guarda no
 * TCB. É chamada para cada candidata, a cada tick (EDF muda o valor a cada
 * ativação, por isso não basta calcular uma vez). É o ÚNICO lugar que chama
 * e->prioridade, por isso é aqui que se decide entre fórmula e função.
 * Se a fórmula não puder ser calculada (divisão por zero, estouro), devolve
 * o motivo em `erro` e não mexe na prioridade: sim.c só fica sabendo que "o
 * escalonador falhou: <motivo>" e para o tick, sem saber que existe fórmula. */
const char *sched_atualizar(const Escalonador *e, TCB *k, int agora, char *erro, size_t tam) {
    const Formula *f = formula_de(e);
    int p;
    if (f) {
        char motivo[200];
        if (!formula_avaliar(f, k, agora, &p, motivo, sizeof motivo)) {
            snprintf(erro, tam, "escalonador %s: %s ao calcular a prioridade da tarefa %s no instante %d", e->nome,
                     motivo, k->id, agora);
            return erro;
        }
    } else {
        p = e->prioridade(k, agora);
    }
    k->prio_nominal = p;
    k->prio_ativa = k->prio_nominal;
    return NULL;
}

/* Confere se um escalonador chamado `nome` pode entrar na tabela. Mesma
 * regra para .so e .txt: nome repetido é RECUSADO, então um arquivo de fora
 * nunca substitui RM/EDF nem outro já carregado. `esperado` != NULL exige
 * esse nome (carga automática pelo arquivo de configuração). NULL se pode,
 * senão o motivo em `buf`. */
static const char *conferir_entrada(const char *nome, const char *esperado, char *buf, size_t tam) {
    int i = sched_buscar(nome);
    if (i >= 0 && i < N_NATIVOS)
        snprintf(buf, tam, "já existe um escalonador chamado '%s' (é um algoritmo do próprio programa, que um "
                 "arquivo não pode substituir)", sched_obter(i)->nome);
    else if (i >= 0)
        snprintf(buf, tam, "já existe um escalonador chamado '%s' (carregado de %s)", sched_obter(i)->nome,
                 carregados[i - N_NATIVOS].origem);
    else if (esperado && !str_ieq(nome, esperado))
        snprintf(buf, tam, "o arquivo define o escalonador '%s', mas o arquivo de configuração pede '%s' (o nome "
                 "do arquivo e o 'nome:' de dentro dele precisam ser iguais)", nome, esperado);
    else if (sched_quantidade() >= MAX_ESCALONADORES)
        snprintf(buf, tam, "limite de %d escalonadores atingido", MAX_ESCALONADORES);
    else
        return NULL;
    return buf;
}

/* Põe na tabela (sempre no fim). Só é chamada depois de TUDO conferido:
 * uma carga que falha não deixa nada para trás. */
static int registrar(const Escalonador *e, Formula *f, void *handle, const char *origem) {
    if (ncarregados == 0) atexit(liberar_carregados);
    carregados[ncarregados] = (Carregado){e, f, handle, xstrdup(origem)};
    return N_NATIVOS + ncarregados++;
}

/* Junta as mensagens numa string só, uma por linha. */
static void juntar_msgs(const ListaMsg *l, char *erro, int tam) {
    erro[0] = '\0';
    for (int i = 0; i < l->n; i++) {
        size_t usado = strlen(erro);
        snprintf(erro + usado, (size_t)tam - usado, "%s%s", i ? "\n" : "", l->itens[i]);
    }
}

/* Escalonador de fórmula (.txt): lê, confere e compila (seção
 * sched_formula.c); só então confere o nome e registra. */
static int carregar_txt(const char *caminho, const char *esperado, char *erro, int tam_erro) {
    ListaMsg erros = {0};
    DefFormula def;
    int idx = -1;
    if (formula_ler_arquivo(caminho, &def, &erros)) {
        char motivo[400];
        if (conferir_entrada(def.nome, esperado, motivo, sizeof motivo)) {
            msg_add(&erros, "%s:%d: %s", caminho, def.linha_nome, motivo);
        } else {
            Escalonador *e = xmalloc(sizeof *e);
            *e = (Escalonador){def.nome, def.descricao, NULL};   /* prioridade = NULL: quem calcula é a fórmula */
            idx = registrar(e, def.formula, NULL, caminho);
            def.nome = def.descricao = NULL;   /* agora pertencem à tabela */
            def.formula = NULL;
        }
        formula_def_liberar(&def);
    }
    juntar_msgs(&erros, erro, tam_erro);
    msg_liberar(&erros);
    return idx;
}

/* Plugin compilado (.so/.dylib):
 *   1. dlopen abre o arquivo;
 *   2. dlsym procura nele o símbolo `escalonador_plugin`;
 *   3. conferimos que é válido e que o nome pode entrar;
 *   4. acrescentamos à tabela.
 * Em qualquer falha escrevemos o motivo em `erro` e devolvemos -1. */
static int carregar_so(const char *caminho, const char *esperado, char *erro, int tam_erro) {
#ifdef _WIN32
    (void)esperado;
    snprintf(erro, (size_t)tam_erro, "%s: plugins dinâmicos (.so/.dylib) não são suportados nesta plataforma; "
             "use um arquivo .txt com a fórmula da prioridade", caminho);
    return -1;
#else
    if (sched_quantidade() >= MAX_ESCALONADORES) {
        snprintf(erro, (size_t)tam_erro, "limite de %d escalonadores atingido", MAX_ESCALONADORES);
        return -1;
    }
    /* dlopen só procura no diretório atual se o caminho tiver '/'. */
    char caminho_real[1024];
    if (strchr(caminho, '/')) snprintf(caminho_real, sizeof caminho_real, "%s", caminho);
    else snprintf(caminho_real, sizeof caminho_real, "./%s", caminho);

    /* RTLD_NOW = resolve todos os símbolos já, para falhar cedo. */
    void *h = dlopen(caminho_real, RTLD_NOW);
    if (!h) {
        snprintf(erro, (size_t)tam_erro, "não foi possível abrir '%s': %s (um .so/.dylib só abre no mesmo sistema "
                 "e processador em que foi compilado; um .txt com a fórmula funciona em qualquer um)", caminho,
                 dlerror());
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
    char motivo[400];
    if (conferir_entrada(e->nome, esperado, motivo, sizeof motivo)) {
        snprintf(erro, (size_t)tam_erro, "%s: %s", caminho, motivo);
        dlclose(h);
        return -1;
    }
    /* O handle fica aberto até o fim do programa (a tabela aponta para
     * dentro dele) e é fechado por liberar_carregados, registrada com atexit. */
    return registrar(e, NULL, h, caminho);
#endif
}

/* Extensão do arquivo (".txt"), ou "" se não tiver: o último '.' depois da
 * última barra ('/' ou '\\'). */
static const char *extensao(const char *caminho) {
    const char *ponto = NULL;
    for (const char *q = caminho; *q; q++) {
        if (*q == '.') ponto = q;
        else if (*q == '/' || *q == '\\') ponto = NULL;
    }
    return ponto ? ponto : "";
}

/* A função única de carga: decide pela extensão, sem diferenciar
 * maiúsculas ("LLF.TXT" é texto). */
int sched_carregar_arquivo_esperado(const char *caminho, const char *nome, char *erro, int tam_erro) {
    const char *ext = extensao(caminho);
    if (str_ieq(ext, ".txt")) return carregar_txt(caminho, nome, erro, tam_erro);
    if (str_ieq(ext, ".so") || str_ieq(ext, ".dylib")) return carregar_so(caminho, nome, erro, tam_erro);
    snprintf(erro, (size_t)tam_erro, "'%s': extensão não reconhecida; use .txt (fórmula da prioridade), .so ou "
             ".dylib (plugin compilado)", caminho);
    return -1;
}

int sched_carregar_arquivo(const char *caminho, char *erro, int tam_erro) {
    return sched_carregar_arquivo_esperado(caminho, NULL, erro, tam_erro);
}
