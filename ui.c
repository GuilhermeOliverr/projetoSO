/*
 * ui.c - telas e comandos do simulador (ver ui.h).
 *
 * A interface é baseada em comandos de uma letra digitados no terminal,
 * como num debugger (gdb): é fácil de usar, não precisa de biblioteca de
 * interface e funciona igual em Linux, WSL e MSYS2. Toda entrada inválida é
 * recusada com o motivo, e Enter sem nada mantém o valor sugerido. A janela
 * gráfica (ui_gui.c) oferece o mesmo, com botões; esta interface é usada com
 * --terminal, com --modo, ou quando não há servidor gráfico.
 *
 * Mapa do arquivo (na ordem em que aparece):
 *   1. utilitários de mensagem e de leitura (pedir_int, pedir_cor, validadores);
 *   2. tela de configuração (antes da simulação): ui_menu_config;
 *   3. estatísticas e exportação do SVG;
 *   4. modo completo: ui_execucao_completa;
 *   5. modo passo a passo: detalhes_tarefa, estado_sistema, editar_tarefa,
 *      adicionar_tarefa e o laço principal ui_passo_a_passo.
 * A interface NÃO calcula a simulação: ela só chama sim_* (sim.c) e desenha
 * com gantt_* (gantt.c).
 */
#include "ui.h"
#include "sim.h"
#include "gantt.h"
#include "config.h"
#include "gfx.h"      /* utf8_colunas: alinhar ids em caracteres, não em bytes */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ mensagens */

static void ui_erro(const char *fmt, ...) {
    va_list ap;
    term_fg(0xE74C3C);
    term_negrito();
    printf("erro: ");
    term_reset();
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

static void pausar(void) {
    char buf[8];
    ler_linha("\n[Enter] para voltar ", buf, sizeof buf);
}

/* Formata uma mensagem num buffer (usado para a linha de status). */
static void fmsg(char *dst, size_t tam, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, tam, fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------- leitura de dados */

/* Um Validador é uma função que recebe o valor digitado e devolve NULL se
 * está ok, ou o texto do motivo da recusa (req. 3.1/3.4: "informar
 * claramente por que não pode"). `ctx` leva informação extra quando a regra
 * precisa (ex.: a lista de ids já usados). Passar a regra como função
 * permite reaproveitar pedir_int para todos os campos. */
typedef const char *(*Validador)(int v, const void *ctx);

/* Pede um inteiro mostrando o valor sugerido entre colchetes. Enter mantém
 * o sugerido. Repete até receber um valor válido. false = EOF.
 * Ordem das checagens: (1) é número? (2) cabe num int? (3) o validador
 * aceita? Em qualquer falha mostra o motivo e pergunta de novo. */
static bool pedir_int(const char *rotulo, int sugerido, int *out, Validador val, const void *ctx) {
    char buf[128], prompt[256];
    snprintf(prompt, sizeof prompt, "  %s [%d]: ", rotulo, sugerido);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        int v = sugerido;
        if (*buf) {
            int r = parse_int(buf, &v);
            if (r == -1) { ui_erro("'%s' não é um número inteiro", buf); continue; }
            if (r == -2) { ui_erro("'%s' é grande demais", buf); continue; }
        }
        const char *m = val ? val(v, ctx) : NULL;
        if (m) { ui_erro("%s", m); continue; }
        *out = v;
        return true;
    }
}

static bool pedir_cor(unsigned sugerida, unsigned *out) {
    char buf[64], prompt[96];
    snprintf(prompt, sizeof prompt, "  cor RGB hex [%06X]: ", sugerida);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        if (!*buf) { *out = sugerida; return true; }
        if (parse_cor(buf, out)) return true;
        ui_erro("'%s' não é uma cor válida: use 6 dígitos hexadecimais, ex.: F0E0D0", buf);
    }
}

/* As regras em si ficam na seção edicao.c de sim.c (a janela usa as mesmas); estes
 * adaptadores só encaixam cada uma no formato Validador. */
static const char *val_positivo(int v, const void *ctx) { (void)ctx; return valida_positivo(v); }
static const char *val_periodo(int v, const void *ctx) { (void)ctx; return valida_periodo(v); }
static const char *val_min(int v, const void *ctx) { return valida_ingresso(v, *(const int *)ctx); }

static const char *val_quantum(int v, const void *ctx) { (void)ctx; return config_validar_quantum(v); }
/* Rótulo do prompt de CPUs com o limite N à vista (steering, ambig. 6).
 * STR_/STR é o truque clássico do pré-processador para transformar o número
 * MAX_CPUS (64) em texto "64" em tempo de compilação; o nível duplo é
 * necessário para a macro ser expandida ANTES de virar texto. */
#define STR_(x) #x
#define STR(x) STR_(x)
#define ROTULO_CPUS "quantidade de CPUs (1 a " STR(MAX_CPUS) ")"

static const char *val_cpus(int v, const void *ctx) { (void)ctx; return config_validar_cpus(v); }

/* Contexto para validar id único: os ids em uso e a posição da tarefa
 * editada (-1 = tarefa nova), que não conta como repetida. `tipo` e `srv`
 * (Projeto B) são o tipo escolhido: o id de um servidor tem de ser da sigla
 * dele (PS1 para PS), e uma tarefa comum não pode usar um id de servidor,
 * senão o id mudaria o tipo escolhido. */
typedef struct { const char *const *ids; int n; int proprio; TipoTarefa tipo; TipoServidor srv; } CtxIds;

/* O id combina com o tipo escolhido? (ver CtxIds) */
static const char *id_combina_com_tipo(const char *v, const CtxIds *c) {
    static char buf[200];
    TipoServidor s = id_servidor(v);
    if (c->tipo == TIPO_SERVIDOR && s != c->srv) {
        snprintf(buf, sizeof buf, "o id de um servidor %s é %s seguido de um número (ex.: %s1)",
                 servidor_sigla(c->srv), servidor_sigla(c->srv), servidor_sigla(c->srv));
        return buf;
    }
    if (c->tipo != TIPO_SERVIDOR && s != SRV_NENHUM)
        return "ids BSxx, PSxx, DSxx e SSxx são de servidores; para criar um servidor, escolha o tipo servidor";
    return NULL;
}

/* Pede o id (texto) mostrando o sugerido entre colchetes; Enter mantém o
 * sugerido. Mesmo laço do pedir_int: repete até o id servir (id_validar), não
 * repetir o de outra tarefa (valida_id) e combinar com o tipo. `sugerido`
 * pode ser o próprio `out` (editar mantém o id), por isso a cópia. */
static bool pedir_id(const char *sugerido, char *out, size_t tam, const CtxIds *c) {
    char sug[ID_MAX], buf[128], prompt[ID_MAX + 32];
    snprintf(sug, sizeof sug, "%s", sugerido);
    snprintf(prompt, sizeof prompt, "  id [%s]: ", sug);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;   /* ler_linha já faz str_trim */
        const char *v = *buf ? buf : sug;
        const char *m = id_validar(v);
        if (!m) m = valida_id(v, c->ids, c->n, c->proprio);
        if (!m) m = id_combina_com_tipo(v, c);
        if (m) { ui_erro("%s", m); continue; }
        id_normalizar(out, tam, v);
        return true;
    }
}

/* Espaços que faltam para o id ocupar `largura` colunas, alinhado à direita
 * como o %6d fazia com o número. Conta caracteres (utf8_colunas), não
 * bytes: com %6s, um id com acento sairia uma coluna fora do lugar. Nunca
 * negativo: printf("%*s") com largura negativa alinharia à esquerda. */
static int folga(const char *id, int largura) {
    int c = utf8_colunas(id);
    return c < largura ? largura - c : 0;
}

/* Os ids de um vetor de tarefas, para valida_id/id_sugerir. Os ponteiros
 * apontam para dentro de `cfg` (não copiam o texto): valem enquanto o vetor
 * não for realocado, e quem chama libera só o vetor de ponteiros. */
static const char **ids_cfg(const Config *cfg) {
    const char **ids = xmalloc((size_t)(cfg->n + 1) * sizeof *ids);
    for (int i = 0; i < cfg->n; i++) ids[i] = cfg->tarefas[i].id;
    return ids;
}

/* Aspas aparecem quando o usuário arrasta o arquivo para o terminal. */
static void tirar_aspas(char *buf) {
    size_t l = strlen(buf);
    if (l >= 2 && (buf[0] == '"' || buf[0] == '\'') && buf[l - 1] == buf[0]) {
        buf[l - 1] = '\0';
        memmove(buf, buf + 1, l - 1);
    }
}

/* Carrega um escalonador de um arquivo (.txt com a fórmula, .so ou .dylib;
 * quem decide é a extensão, em sched.c) e mostra o resultado. Em caso de
 * erro mostra TODOS os problemas do arquivo, um por linha, e a tabela de
 * algoritmos fica como estava. Devolve o índice, ou -1. */
static int carregar_escalonador(char *caminho) {
    tirar_aspas(caminho);
    char erro[4096];
    int idx = sched_carregar_arquivo(caminho, erro, sizeof erro);
    if (idx < 0) {
        ui_erro("não foi possível carregar o escalonador de '%s':", caminho);
        for (char *lin = erro, *fim; lin && *lin; lin = fim ? fim + 1 : NULL) {
            fim = strchr(lin, '\n');
            if (fim) *fim = '\0';
            printf("  %s\n", lin);
        }
        return -1;
    }
    printf("Escalonador %s carregado de %s.\n", sched_obter(idx)->nome, caminho);
    return idx;
}

/* O texto digitado é o caminho de um arquivo de escalonador? (termina em
 * .txt, .so ou .dylib, sem diferenciar maiúsculas) */
static bool parece_escalonador(const char *s) {
    const char *ponto = strrchr(s, '.');
    return ponto && (str_ieq(ponto, ".txt") || str_ieq(ponto, ".so") || str_ieq(ponto, ".dylib"));
}

/* Escolhe um algoritmo pelo nome ou número, ou carrega um novo de um
 * arquivo (o caminho de um .txt/.so; o carregado já fica escolhido).
 * -1 = cancelado. */
static int pedir_algoritmo(int atual) {
    printf("Algoritmos disponíveis:\n");
    for (int i = 0; i < sched_quantidade(); i++)
        printf("  %d) %-4s %s%s\n", i + 1, sched_obter(i)->nome, sched_obter(i)->descricao,
               i == atual ? "  (atual)" : "");
    char buf[1024];   /* cabe um caminho de arquivo */
    for (;;) {
        if (!ler_linha("  algoritmo (nome, número ou arquivo .txt/.so de um novo; Enter mantém): ", buf, sizeof buf))
            return -1;
        if (!*buf) return atual;
        int v;
        if (parse_int(buf, &v) == 0 && v >= 1 && v <= sched_quantidade()) return v - 1;
        int idx = sched_buscar(buf);
        if (idx >= 0) return idx;
        tirar_aspas(buf);
        if (parece_escalonador(buf)) {
            idx = carregar_escalonador(buf);
            if (idx >= 0) return idx;
            continue;
        }
        char nomes[256];
        sched_listar_nomes(nomes, sizeof nomes);
        ui_erro("algoritmo '%s' desconhecido (disponíveis: %s)", buf, nomes);
    }
}

/* Período de um servidor: > 0; o BS aceita 0 (não usa período). */
static const char *val_periodo_servidor(int v, const void *ctx) {
    TipoServidor s = *(const TipoServidor *)ctx;
    if (s == SRV_BS) return v >= 0 ? NULL : "o período do BS não pode ser negativo (use 0: ele não usa período)";
    return v > 0 ? NULL : "o período do servidor deve ser maior que zero (de quanto em quanto tempo a capacidade volta)";
}

/* Prazo da aperiódica: Enter vazio = sem prazo (0), como o campo vazio no
 * arquivo. Mostra o valor atual entre colchetes (ou "sem prazo"). */
static bool pedir_prazo_aperiodica(int atual, int *out) {
    char buf[64], prompt[128];
    snprintf(prompt, sizeof prompt, "  prazo (- = sem prazo) [%s]: ", atual > 0 ? "" : "sem prazo");
    if (atual > 0) snprintf(prompt, sizeof prompt, "  prazo (- = sem prazo) [%d]: ", atual);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        if (!*buf) { *out = atual > 0 ? atual : 0; return true; }
        if (!strcmp(buf, "-")) { *out = 0; return true; }
        int v;
        if (parse_int(buf, &v) == 0 && v > 0) { *out = v; return true; }
        ui_erro("'%s': o prazo deve ser um inteiro maior que zero, ou - para sem prazo", buf);
    }
}

/* Ocorrências da esporádica (Projeto B): vários instantes, relativos ao
 * ingresso, separados por vírgula ("0,15,40"). Repete até a lista servir:
 * inteiros >= 0, pelo menos um, e o intervalo mínimo entre ocorrências
 * seguidas (a mesma conferência do arquivo, req. 1.3 da parte B). */
static bool pedir_ocorrencias(const char *sugerido, int minimo, char *out, size_t tam) {
    char buf[512], prompt[700];
    snprintf(prompt, sizeof prompt, "  ocorrências (instantes relativos ao ingresso, separados por vírgula) [%s]: ",
             sugerido);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        const char *v = *buf ? buf : sugerido;
        int *oc, n;
        const char *m = config_ler_ocorrencias(v, &oc, &n);
        char motivo[300];
        if (!m && n == 0) m = "informe pelo menos uma ocorrência (ex.: 0,15,40)";
        if (!m) m = config_intervalo_minimo(oc, n, minimo, motivo, sizeof motivo);
        free(oc);
        if (m) { ui_erro("%s", m); continue; }
        snprintf(out, tam, "%s", v);
        return true;
    }
}

/* Lê os dados de uma tarefa nova (ou editada, se `t` já vier preenchida),
 * com os campos do tipo dela (t->tipo, t->srv; Projeto B):
 *   - periódica: período > 0 e lista de eventos;
 *   - aperiódica: período 0 (não pergunta) e prazo opcional;
 *   - esporádica: intervalo mínimo (o período vai negativo) e ocorrências;
 *   - servidor: a duração é a capacidade.
 * `min_ingresso` impede chegar no passado durante a simulação. Cada campo
 * usa o valor atual de `t` como sugestão (req. 3.2: valores padrão). */
static bool pedir_tarefa(TarefaCfg *t, const CtxIds *ids, int min_ingresso, char *eventos, size_t tam_ev) {
    if (!pedir_id(t->id, t->id, sizeof t->id, ids)) return false;
    if (!pedir_cor(t->cor, &t->cor)) return false;
    if (!pedir_int("ingresso", t->ingresso, &t->ingresso, val_min, &min_ingresso)) return false;
    if (!pedir_int(t->tipo == TIPO_SERVIDOR ? "capacidade (C)" : "duração", t->duracao, &t->duracao, val_positivo, NULL))
        return false;
    if (t->tipo == TIPO_APERIODICA) {
        t->periodo = 0;
        printf("  período: 0 (aperiódica: chega uma vez, no ingresso)\n");
        if (!pedir_prazo_aperiodica(t->prazo, &t->prazo)) return false;
    } else if (t->tipo == TIPO_ESPORADICA) {
        int minimo = t->periodo < 0 ? -t->periodo : PADRAO_PERIODO, antigo = minimo;
        if (!pedir_int("intervalo mínimo entre ocorrências (|período|)", minimo, &minimo, val_positivo, NULL)) return false;
        t->periodo = -minimo;
        /* prazo implícito da esporádica = intervalo mínimo; continua acompanhando */
        int sug = t->prazo <= 0 || t->prazo == antigo ? minimo : t->prazo;
        if (!pedir_int("prazo", sug, &t->prazo, val_positivo, NULL)) return false;
    } else {
        int periodo_antigo = t->periodo;
        if (t->tipo == TIPO_SERVIDOR) {
            if (!pedir_int("período do servidor", t->periodo, &t->periodo, val_periodo_servidor, &t->srv)) return false;
        } else if (!pedir_int("período", t->periodo, &t->periodo, val_periodo, NULL)) {
            return false;
        }
        /* Se o prazo acompanhava o período (prazo implícito), continua acompanhando. */
        int sug = t->prazo == periodo_antigo ? t->periodo : t->prazo;
        if (sug <= 0) sug = t->periodo > 0 ? t->periodo : 1;
        if (!pedir_int("prazo", sug, &t->prazo, val_positivo, NULL)) return false;
    }
    if (t->tipo == TIPO_ESPORADICA) {
        if (!pedir_ocorrencias(t->eventos && *t->eventos ? t->eventos : "0", -t->periodo, eventos, tam_ev)) return false;
    } else {
        char prompt[600];
        snprintf(prompt, sizeof prompt, "  lista de eventos [%s]: ", t->eventos ? t->eventos : "");
        char buf[512];
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        snprintf(eventos, tam_ev, "%s", *buf ? buf : (t->eventos ? t->eventos : ""));
    }
    t->eventos = eventos;
    return true;
}

/* Lê uma linha no formato do .txt pelas regras do arquivo (modo manual) e
 * mostra os avisos e erros. A tarefa lida fica em tmp->tarefas[0]. */
static bool ler_linha_conferida(const char *linha, const CtxIds *c, Config *tmp) {
    bool ok = config_ler_linha_manual(linha, c->ids, c->n, c->proprio, tmp);
    for (int i = 0; i < tmp->avisos.n; i++) printf("aviso: %s\n", tmp->avisos.itens[i]);
    for (int i = 0; i < tmp->erros.n; i++) ui_erro("%s", tmp->erros.itens[i]);
    return ok;
}

/* Modo manual (Projeto B, sugestão do professor): o que foi digitado vira a
 * mesma linha que a tarefa teria no arquivo .txt, mostrada ao usuário, e é
 * lida pelas regras do arquivo. */
static bool tarefa_pela_linha(const TarefaCfg *t, const CtxIds *c, Config *tmp) {
    char linha[1024];
    config_linha_tarefa(t, linha, sizeof linha);
    printf("Linha gerada (o mesmo texto de um arquivo .txt): %s\n", linha);
    return ler_linha_conferida(linha, c, tmp);
}

/* Seletor do tipo da tarefa nova (Projeto B). *linha = true quando o
 * usuário prefere digitar a linha inteira do .txt. false = EOF/cancelou. */
static bool pedir_tipo(TipoTarefa *tipo, TipoServidor *srv, bool *linha) {
    char buf[64];
    *srv = SRV_NENHUM;
    *linha = false;
    printf("Tipo da tarefa: [p] periódica  [a] aperiódica  [e] esporádica  [s] servidor\n"
           "                [l] digitar a linha do .txt (id;cor;ingresso;duracao;periodo;prazo;lista_eventos)\n");
    for (;;) {
        if (!ler_linha("  tipo [p]: ", buf, sizeof buf)) return false;
        if (!*buf || str_ieq(buf, "p")) { *tipo = TIPO_PERIODICA; return true; }
        if (str_ieq(buf, "a")) { *tipo = TIPO_APERIODICA; return true; }
        if (str_ieq(buf, "e")) { *tipo = TIPO_ESPORADICA; return true; }
        if (str_ieq(buf, "l")) { *tipo = TIPO_PERIODICA; *linha = true; return true; }
        if (str_ieq(buf, "s")) break;
        ui_erro("opção '%s' inválida: digite p, a, e, s ou l", buf);
    }
    *tipo = TIPO_SERVIDOR;
    for (;;) {
        if (!ler_linha("  servidor: [b] BS (Background)  [p] PS (Polling)  [d] DS (Deferrable)  [s] SS (Sporadic) [p]: ",
                       buf, sizeof buf))
            return false;
        if (!*buf || str_ieq(buf, "p")) { *srv = SRV_PS; return true; }
        if (str_ieq(buf, "b")) { *srv = SRV_BS; return true; }
        if (str_ieq(buf, "d")) { *srv = SRV_DS; return true; }
        if (str_ieq(buf, "s")) { *srv = SRV_SS; return true; }
        ui_erro("opção '%s' inválida: digite b, p, d ou s", buf);
    }
}

/* Tarefa nova pelo modo manual: seletor de tipo, campos do tipo e a linha do
 * .txt lida pelas regras do arquivo. `ids`: ids em uso; `cor` e
 * `min_ingresso`: sugestões. Se der certo, a tarefa fica em tmp->tarefas[0].
 * Quem chama sempre libera `tmp` com config_liberar. */
static bool nova_tarefa(const char *const *ids, int nids, unsigned cor, int min_ingresso, Config *tmp) {
    config_padrao(tmp);
    TipoTarefa tipo;
    TipoServidor srv;
    bool digitar;
    if (!pedir_tipo(&tipo, &srv, &digitar)) return false;
    CtxIds c = {ids, nids, -1, tipo, srv};
    if (digitar) {
        char buf[1024];
        if (!ler_linha("  linha: ", buf, sizeof buf) || !*buf) return false;
        if (!ler_linha_conferida(buf, &c, tmp)) return false;
        if (tmp->tarefas[0].ingresso < min_ingresso) {
            ui_erro("o ingresso deve ser >= %d (não dá para chegar no passado)", min_ingresso);
            return false;
        }
        return true;
    }
    /* Sugestões (req. 3.2) por tipo; a esporádica já sugere várias ocorrências. */
    TarefaCfg t = {0};
    t.tipo = tipo;
    t.srv = srv;
    t.cor = cor;
    t.ingresso = min_ingresso;
    t.duracao = PADRAO_DURACAO;
    t.periodo = tipo == TIPO_APERIODICA ? 0 : tipo == TIPO_ESPORADICA ? -PADRAO_PERIODO : PADRAO_PERIODO;
    t.prazo = tipo == TIPO_APERIODICA ? 0 : PADRAO_PERIODO;
    t.eventos = tipo == TIPO_ESPORADICA ? "0,10,20" : "";
    if (tipo == TIPO_SERVIDOR) id_sugerir_servidor(srv, ids, nids, t.id, sizeof t.id);
    else id_sugerir(ids, nids, t.id, sizeof t.id);
    char ev[512];
    printf("Nova tarefa %s (Enter aceita o valor sugerido%s):\n", tipo_nome(tipo, srv),
           min_ingresso > 0 ? "; ingresso >= instante atual" : "");
    if (!pedir_tarefa(&t, &c, min_ingresso, ev, sizeof ev)) return false;
    return tarefa_pela_linha(&t, &c, tmp);
}

/* ------------------------------------------------------- configuração */

/* Mostra o teste de escalonabilidade do conjunto (req. 3 da parte B): U, o
 * limite e de onde ele vem (ver teste_calcular em config.c). É a mesma
 * conta que decide se uma tarefa nova é aceita. */
static void imprimir_teste(const ItemTeste *v, int n, int alg, int ncpus) {
    char buf[700];
    teste_resumo(v, n, alg, ncpus, buf, sizeof buf);
    printf("%s\n", buf);
}

void ui_imprimir_config(const Config *cfg) {
    const Escalonador *e = sched_obter(cfg->algoritmo);
    term_negrito();
    printf("Arquivo: ");
    term_reset();
    printf("%s\n", cfg->caminho ? cfg->caminho : "(nenhum)");
    term_negrito();
    printf("Sistema: ");
    term_reset();
    printf("algoritmo %s (%s) | quantum %d | %d CPU(s) (máx. %d)\n", e ? e->nome : "?",
           e ? e->descricao : "", cfg->quantum, cfg->ncpus, MAX_CPUS);
    term_negrito();
    printf("Tarefas (%d):\n", cfg->n);
    term_reset();
    printf("  %6s  %-7s %8s %8s %8s %8s  %-11s %s\n", "id", "cor", "ingresso", "duração", "período", "prazo", "tipo",
           "eventos");
    for (int i = 0; i < cfg->n; i++) {
        const TarefaCfg *t = &cfg->tarefas[i];
        printf("  %*s%s  ", folga(t->id, 6), "", t->id);
        term_bg(t->cor);
        printf("  ");
        term_reset();
        /* tipo curto: "periódica", "aperiódica", "esporádica" ou a sigla do servidor */
        printf("%06X %7d %8d %8d %8d  %-11s %s\n", t->cor, t->ingresso, t->duracao, t->periodo, t->prazo,
               t->tipo == TIPO_SERVIDOR ? servidor_sigla(t->srv) : tipo_nome(t->tipo, t->srv),
               t->eventos && *t->eventos ? t->eventos : "-");
    }
    ItemTeste *v = config_itens_teste(cfg);
    imprimir_teste(v, cfg->n, cfg->algoritmo, cfg->ncpus);
    free(v);
}

/* Posição da tarefa de id `id` na configuração: devolve o ÍNDICE (-1 se
 * não existe), nunca um id. Usa id_iguais, como sim_indice. */
static int indice_cfg(const Config *cfg, const char *id) {
    for (int i = 0; i < cfg->n; i++)
        if (id_iguais(cfg->tarefas[i].id, id)) return i;
    return -1;
}

bool ui_pedir_arquivo(Config *cfg) {
    char buf[1024];
    for (;;) {
        if (!ler_linha("Caminho do arquivo de configuração (q para sair): ", buf, sizeof buf)) return false;
        if (!*buf) { ui_erro("informe um caminho (absoluto ou relativo), ex.: exemplos/fig2_5_rm.txt"); continue; }
        if (str_ieq(buf, "q")) return false;
        tirar_aspas(buf);
        Config novo;
        bool ok = config_carregar(buf, &novo);
        config_imprimir_mensagens(&novo);
        if (ok) {
            config_liberar(cfg);
            *cfg = novo;
            return true;
        }
        config_liberar(&novo);
        printf("Corrija o arquivo e tente de novo, ou informe outro.\n");
    }
}

/* Menu de configuração: repete até o usuário apertar Enter (iniciar) ou
 * sair. Cada comando é UMA letra, às vezes seguida do id de uma tarefa. O
 * switch no meio trata cada letra; todas as alterações validam o valor e
 * recusam com o motivo (req. 3.1). */
bool ui_menu_config(Config *cfg) {
    char buf[256];
    for (;;) {
        printf("\n");
        term_negrito();
        printf("=== Configuração da simulação ===\n");
        term_reset();
        ui_imprimir_config(cfg);
        printf("\n[Enter] iniciar  [a] algoritmo  [k] quantum  [c] CPUs  [e id] editar tarefa\n"
               "[n] nova tarefa  [r id] remover tarefa  [l] carregar outro arquivo  [q] sair\n"
               "[p arquivo] carregar escalonador (.txt com a fórmula, .so ou .dylib) e usá-lo\n");
        if (!ler_linha("> ", buf, sizeof buf)) return false;
        /* cmd = 1ª letra; arg = o resto da linha depois dela, como texto
         * ("e T2" -> cmd 'e', arg "T2"), porque o id pode ser texto. O
         * str_trim tira o espaço entre eles; "Tarefa 2" (espaço no meio)
         * continua inteiro. */
        char cmd = buf[0];
        const char *arg = str_trim(buf + (cmd ? 1 : 0));
        bool tem_arg = *arg != '\0';
        switch (cmd) {
        case '\0':   /* Enter sem nada = iniciar (se houver tarefas) */
            if (cfg->n == 0) { ui_erro("não há tarefas para simular; use [n] ou [l]"); break; }
            return true;
        case 'a': {
            int a = pedir_algoritmo(cfg->algoritmo);
            if (a >= 0) cfg->algoritmo = a;
            break;
        }
        case 'k': pedir_int("quantum", cfg->quantum, &cfg->quantum, val_quantum, NULL); break;
        case 'c': pedir_int(ROTULO_CPUS, cfg->ncpus, &cfg->ncpus, val_cpus, NULL); break;
        case 'e': case 'r': {
            if (!tem_arg) { ui_erro("informe o id da tarefa, ex.: %c 2", cmd); break; }
            int i = indice_cfg(cfg, arg);
            if (i < 0) { ui_erro("não existe tarefa com id %s", arg); break; }
            /* As mensagens mostram o id guardado ("1"), não o digitado
             * ("01"), como quando o id era int. */
            char id[ID_MAX];
            snprintf(id, sizeof id, "%s", cfg->tarefas[i].id);
            if (cmd == 'r') {
                /* Remover do meio de um vetor: libera a memória da tarefa e
                 * desloca as seguintes uma posição para trás (memmove). */
                config_liberar_tarefa(&cfg->tarefas[i]);
                memmove(&cfg->tarefas[i], &cfg->tarefas[i + 1], (size_t)(cfg->n - i - 1) * sizeof(TarefaCfg));
                cfg->n--;
                printf("Tarefa %s removida.\n", id);
                break;
            }
            const char **ids = ids_cfg(cfg);
            /* O tipo não muda na edição (é o da tarefa); o resultado vira a
             * linha do .txt e é lido pelas regras do arquivo, como no "n". */
            CtxIds c = {ids, cfg->n, i, cfg->tarefas[i].tipo, cfg->tarefas[i].srv};
            TarefaCfg t = cfg->tarefas[i];
            char ev[512];
            Config tmp;
            config_padrao(&tmp);
            printf("Editando a tarefa %s, %s (Enter mantém o valor):\n", id, tipo_nome(t.tipo, t.srv));
            if (pedir_tarefa(&t, &c, 0, ev, sizeof ev) && tarefa_pela_linha(&t, &c, &tmp)) {
                /* troca a tarefa pela lida (que já tem eventos e ocorrências
                 * próprios) e mantém o nº da linha do arquivo original */
                int linha = cfg->tarefas[i].linha;
                config_liberar_tarefa(&cfg->tarefas[i]);
                cfg->tarefas[i] = tmp.tarefas[0];
                cfg->tarefas[i].linha = linha;
                tmp.n = 0;   /* agora é de cfg: config_liberar(&tmp) não pode liberar */
            }
            config_liberar(&tmp);
            free(ids);
            break;
        }
        case 'n': {
            /* Modo manual (Projeto B): seletor de tipo, campos do tipo e a
             * linha do .txt gerada, lida pelas regras do arquivo. */
            const char **ids = ids_cfg(cfg);
            Config tmp;
            if (nova_tarefa(ids, cfg->n, config_cor_padrao(cfg->n), 0, &tmp)) {
                /* Req. 3 da parte B: o teste de escalonabilidade vem antes de aceitar. */
                const TarefaCfg *t = &tmp.tarefas[0];
                char motivo[700];
                if (config_admitir_tarefa(cfg, t, motivo, sizeof motivo)) {
                    ui_erro("tarefa %s impedida de ser escalonada: %s", t->id, motivo);
                } else {
                    config_adicionar(cfg, t);
                    printf("Tarefa %s (%s) adicionada.\n", t->id, tipo_nome(t->tipo, t->srv));
                }
            }
            config_liberar(&tmp);
            free(ids);
            break;
        }
        case 'l': ui_pedir_arquivo(cfg); break;
        case 'p': {
            /* 'p' estava livre neste menu (a, k, c, e, r, n, l, q). O caminho
             * pode vir junto ("p /Volumes/PENDRIVE/LLF.txt") ou ser pedido. */
            char cam[1024];
            snprintf(cam, sizeof cam, "%s", arg);
            if (!*cam && (!ler_linha("Arquivo do escalonador (.txt com a fórmula, .so ou .dylib; Enter cancela): ",
                                     cam, sizeof cam) || !*cam))
                break;
            int idx = carregar_escalonador(cam);
            if (idx >= 0) {
                cfg->algoritmo = idx;
                printf("A simulação vai usar o %s ([a] troca de novo).\n", sched_obter(idx)->nome);
            }
            break;
        }
        case 'q': return false;
        default: ui_erro("comando '%s' desconhecido", buf);
        }
    }
}

char ui_pedir_modo(void) {
    char buf[32];
    for (;;) {
        if (!ler_linha("\nModo de execução: [a] passo a passo (padrão)  [b] completa  [q] sair: ", buf, sizeof buf))
            return 0;
        if (!*buf || str_ieq(buf, "a")) return 'a';
        if (str_ieq(buf, "b")) return 'b';
        if (str_ieq(buf, "q")) return 0;
        ui_erro("opção '%s' inválida: digite a, b ou q", buf);
    }
}

/* -------------------------------------------------------- utilidades sim */

/* Nome do SVG de saída: o informado em --saida, ou "<nome do arquivo de
 * configuração sem pasta e sem extensão>_gantt.svg" (ex.: fig2_5_rm.txt ->
 * fig2_5_rm_gantt.svg). Aceita '/' e '\' como separadores (Linux e Windows). */
void ui_caminho_svg(const Config *cfg, const OpcoesUI *op, char *dst, size_t tam) {
    if (op->saida_svg) { snprintf(dst, tam, "%s", op->saida_svg); return; }
    /* nome do arquivo de configuração, sem diretório nem extensão */
    const char *c = cfg->caminho ? cfg->caminho : "simulacao";
    const char *b = c;
    for (const char *p = c; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    char base[256];
    snprintf(base, sizeof base, "%s", *b ? b : "simulacao");
    char *ponto = strrchr(base, '.');
    if (ponto && ponto != base) *ponto = '\0';
    snprintf(dst, tam, "%s_gantt.svg", base);
}

static void titulo_svg(const Config *cfg, char *dst, size_t tam) {
    snprintf(dst, tam, "%s", cfg->caminho ? cfg->caminho : "simulação");
}

/* Tabela final: por tarefa (ativações, prazos perdidos, tempos, preempções)
 * e por CPU (quanto ficou ocupada x desligada, req. 1.2). "Tempo de resposta"
 * = instante em que a ativação terminou - instante em que chegou. */
static void estatisticas(const Simulador *s) {
    const Estado *e = &s->atual;
    term_negrito();
    printf("\nEstatísticas por tarefa:\n");
    term_reset();
    printf("  %6s %7s %7s %7s %7s %8s %8s %6s %7s %7s\n", "id", "ativ.", "prazos", "exec", "espera",
           "resp.méd", "resp.máx", "preemp", "sorteio", "término");
    int perdidos = 0;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        perdidos += k->deadlines_perdidos;
        char ativ[24];
        tcb_texto_ativacoes(k, ativ, sizeof ativ);
        printf("  %*s%s %s %7d %7d %7d %8.1f %8d %6d %7d ", folga(k->id, 6), "", k->id, ativ,
               k->deadlines_perdidos, k->ticks_exec, k->ticks_espera,
               k->concluidas ? (double)k->resposta_soma / k->concluidas : 0.0, k->resposta_max,
               k->preempcoes, k->sorteios);
        if (k->termino >= 0) printf("%7d\n", k->termino);
        else printf("%7s\n", "-");
    }
    term_negrito();
    printf("Uso das CPUs (%d ticks):\n", s->ncols);
    term_reset();
    int ncpu = e->ncpus;
    for (int t = 0; t < s->ncols; t++)
        if (s->ncpus_col[t] > ncpu) ncpu = s->ncpus_col[t];
    for (int c = 0; c < ncpu; c++) {
        int ocup = 0, existe = 0;
        for (int t = 0; t < s->ncols; t++) {
            if (c >= s->ncpus_col[t]) continue;
            existe++;
            for (int i = 0; i < e->n; i++)
                if (sim_cpu_celula(s, t, i) == c) { ocup++; break; }
        }
        printf("  CPU%d: ocupada %d tick(s), desligada %d tick(s) (%.1f%% de uso)\n", c, ocup,
               existe - ocup, existe ? 100.0 * ocup / existe : 0.0);
    }
    if (perdidos) {
        term_fg(0xE74C3C);
        printf("Total de prazos perdidos: %d\n", perdidos);
        term_reset();
    } else {
        printf("Nenhum prazo perdido.\n");
    }
}

static bool exportar(const Simulador *s, const Config *cfg, const char *caminho, char *msg, size_t tam) {
    char titulo[512], erro[512];
    titulo_svg(cfg, titulo, sizeof titulo);
    if (gantt_svg(s, caminho, titulo, erro, sizeof erro)) {
        fmsg(msg, tam, "Gantt da simulação exportado para '%s' (abra no navegador).", caminho);
        return true;
    }
    fmsg(msg, tam, "erro ao exportar: %s", erro);
    return false;
}

/* ------------------------------------------------------- modo completo */

/* Modo (b): simula tudo de uma vez, SEM histórico (historico = false, não
 * dá para voltar) e mostra só o resultado final: Gantt, estatísticas e o
 * SVG. Devolve o código de saída do programa. */
int ui_execucao_completa(const Config *cfg, const OpcoesUI *op) {
    if (cfg->n == 0) {
        ui_erro("não há tarefas para simular");
        return 1;
    }
    Simulador s;
    sim_iniciar(&s, cfg, false, op->semente);
    const char *motivo = sim_rodar_ate_fim(&s);
    term_negrito();
    printf("\n=== Resultado da execução completa ===\n");
    term_reset();
    const Escalonador *esc = sched_obter(s.atual.alg);
    printf("Algoritmo %s | %d CPU(s) | quantum %d | simulação terminou no instante %d\n",
           esc->nome, s.atual.ncpus, s.atual.quantum, s.atual.t);
    if (motivo) ui_erro("a simulação parou antes do fim: %s", motivo);
    gantt_tui_completo(&s);
    if (op->texto) gantt_texto(&s);
    estatisticas(&s);
    char caminho[1024], msg[1400];
    ui_caminho_svg(cfg, op, caminho, sizeof caminho);
    bool ok = exportar(&s, cfg, caminho, msg, sizeof msg);
    if (ok) printf("%s\n", msg);
    else ui_erro("%s", msg + strlen("erro ao exportar: "));
    sim_liberar(&s);
    return ok && !motivo ? 0 : 1;
}

/* ---------------------------------------------------- modo passo a passo */

/* Comando `t id`: despeja o TCB de uma tarefa (req. 1.5.1: examinar o
 * estado de cada tarefa individualmente, como num debugger). */
static void detalhes_tarefa(const Simulador *s, int i) {
    const TCB *k = &s->atual.tarefas[i];
    term_negrito();
    term_fg(k->cor);
    printf("Tarefa %s (TCB) no instante %d\n", k->id, s->atual.t);
    term_reset();
    printf("  parâmetros: cor %06X | ingresso %d | duração %d | período %d | prazo %d\n",
           k->cor, k->ingresso, k->duracao, k->periodo, k->prazo);
    printf("  tipo: %s\n", tipo_nome(k->tipo, k->srv));
    if (k->tipo == TIPO_ESPORADICA) {
        printf("  ocorrências (intervalo mínimo %d):", -k->periodo);
        for (int j = 0; j < k->total_ativacoes; j++) printf(" %d", tcb_chegada(k, j));
        putchar('\n');
    } else {
        printf("  eventos: %d", k->neventos);
        for (int j = 0; j < k->neventos; j++) printf("%s%s", j ? " | " : ": ", k->eventos[j]);
        putchar('\n');
    }
    printf("  estado: %s\n", estado_descricao(k));
    if (tcb_eh_servidor(k)) {
        /* O servidor não tem ativações: o que interessa é a capacidade e
         * qual aperiódica ele está executando. */
        if (k->srv != SRV_BS) printf("  capacidade: %d de %d tick(s)\n", k->capacidade, k->duracao);
        if (k->atendendo >= 0) printf("  executando a aperiódica: %s\n", s->atual.tarefas[k->atendendo].id);
        if (k->estado != T_TERMINADA && k->liberadas > 0 && (k->srv == SRV_PS || k->srv == SRV_DS))
            printf("  próxima recarga (início do período): instante %d\n", k->proxima_chegada);
        if (k->srv == SRV_SS) {
            if (k->ss_inicio >= 0)
                printf("  intervalo ativo desde %d (consumiu %d; RT = %d)\n", k->ss_inicio, k->ss_consumido,
                       k->ss_inicio + k->periodo);
            for (int r = 0; r < k->nrecargas; r++)
                printf("  recarga agendada: +%d no instante %d\n", k->recarga_qtd[r], k->recarga_inst[r]);
        }
        if (k->estado == T_PRONTA || k->estado == T_EXECUTANDO)
            printf("  prioridade (menor = mais prioritária): nominal %d, ativa %d\n", k->prio_nominal, k->prio_ativa);
    } else {
        if (k->restante > 0 && !tcb_eh_aperiodica(k))
            printf("  prioridade (menor = mais prioritária): nominal %d, ativa %d\n", k->prio_nominal, k->prio_ativa);
        printf("  ativações: %d chegaram, %d concluídas (de %d)\n", k->liberadas, k->concluidas, k->total_ativacoes);
        if (k->restante > 0) {
            char dl[24] = "sem prazo";
            if (k->deadline_abs >= 0) snprintf(dl, sizeof dl, "deadline em %d", k->deadline_abs);
            printf("  ativação atual: nº %d, chegou em %d, %s, faltam %d tick(s)%s\n",
                   k->concluidas + 1, k->chegada_atual, dl, k->restante,
                   tcb_prazo_perdido(k) ? " — PRAZO PERDIDO" : "");
            if (k->liberadas - k->concluidas > 1)
                printf("  ativações acumuladas esperando: %d\n", k->liberadas - k->concluidas - 1);
            if (tcb_eh_aperiodica(k))
                printf("  servidor: %s\n", k->servidor >= 0 ? s->atual.tarefas[k->servidor].id
                                                            : "nenhum (esperando na fila de aperiódicas)");
        }
        if (k->liberadas < k->total_ativacoes && k->estado != T_TERMINADA)
            printf("  próxima chegada: instante %d\n", k->proxima_chegada);
    }
    printf("  CPU no último tick: %s", k->cpu >= 0 ? "" : "nenhuma\n");
    if (k->cpu >= 0) printf("CPU%d (quantum usado %d/%d)\n", k->cpu, k->quantum_usado, s->atual.quantum);
    printf("  estatísticas: executou %d, esperou %d, suspensa %d tick(s); %d preempção(ões), "
           "%d sorteio(s), %d prazo(s) perdido(s)\n", k->ticks_exec, k->ticks_espera, k->ticks_suspensa,
           k->preempcoes, k->sorteios, k->deadlines_perdidos);
    if (k->termino >= 0) printf("  terminou no instante %d\n", k->termino);
}

/* Comando `s`: visão geral do sistema (todas as tarefas numa tabela). */
static void estado_sistema(const Simulador *s) {
    const Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    term_negrito();
    printf("Sistema no instante %d\n", e->t);
    term_reset();
    printf("  algoritmo %s | quantum %d | %d CPU(s) | %s\n", esc->nome, e->quantum, e->ncpus,
           e->fim ? "simulação concluída" : sim_bloqueado(e) ? "bloqueada (todas suspensas)" : "em andamento");
    int nv;
    ItemTeste *v = sim_itens_teste(e, &nv);
    imprimir_teste(v, nv, e->alg, e->ncpus);
    free(v);
    printf("\n  %6s %-27s %7s %8s %8s %5s %7s\n", "id", "estado", "ativ.", "restante", "deadline", "CPU", "prazos");
    int *ordem = gantt_ordem(e);
    for (int r = e->n - 1; r >= 0; r--) {
        const TCB *k = &e->tarefas[ordem[r]];
        char ativ[24];
        tcb_texto_ativacoes(k, ativ, sizeof ativ);
        printf("  %*s%s %-27s %s ", folga(k->id, 6), "", k->id, estado_descricao(k), ativ);
        if (k->restante > 0 && !tcb_eh_servidor(k)) printf("%8d %8d ", k->restante, k->deadline_abs);
        else printf("%8s %8s ", "-", "-");
        if (k->cpu >= 0) printf("%5d ", k->cpu);
        else printf("%5s ", "-");
        printf("%7d\n", k->deadlines_perdidos);
    }
    free(ordem);
}

/* Linha com a situação das CPUs e da fila no último tick executado. */
static void resumo_tick(const Simulador *s) {
    const Estado *e = &s->atual;
    if (e->t == 0) {
        printf("Instante 0: nada executado ainda. [n] avança um tick.\n");
        return;
    }
    int t = e->t - 1;
    /* rótulos como no Gantt: T1 se todos os ids são inteiros, senão o id cru */
    bool todos_inteiros = sim_ids_inteiros(e);
    char rot[ID_MAX + 8];
    printf("Tick %d:", t);
    for (int c = 0; c < s->ncpus_col[t]; c++) {
        int quem = sim_ocupante_cpu(s, t, c);
        if (quem >= 0) {
            id_rotulo(e->tarefas[quem].id, todos_inteiros, rot, sizeof rot);
            printf("  CPU%d=%s", c, rot);
            int sv = sim_servidor_cpu(s, t, c);   /* aperiódica executada por um servidor */
            if (sv >= 0 && sv != quem) {
                id_rotulo(e->tarefas[sv].id, todos_inteiros, rot, sizeof rot);
                printf("(%s)", rot);
            }
        } else {
            printf("  CPU%d=desligada", c);
        }
    }
    printf("  | prontas:");
    int np = 0;
    for (int i = 0; i < e->n; i++)
        if (sim_celula(s, t, i) == G_PRONTA) {
            id_rotulo(e->tarefas[i].id, todos_inteiros, rot, sizeof rot);
            printf(" %s", rot);
            np++;
        }
    if (!np) printf(" (nenhuma)");
    putchar('\n');
    bool algum = false;
    for (int v = 0; v < s->nev; v++) {
        const Evento *ev = &s->ev[v];
        if (ev->tick != t) continue;
        if (!algum) printf("Eventos:");
        algum = true;
        id_rotulo(e->tarefas[ev->tarefa].id, todos_inteiros, rot, sizeof rot);
        printf("  [t=%d] %s %s", ev->instante, rot, evento_nome(ev->tipo));
        if (ev->tipo == EV_PRAZO_PERDIDO) printf("!");
        /* eventos dos servidores (Projeto B): quanto recarregou/perdeu, em qual servidor entrou */
        if (ev->tipo == EV_RECARGA || ev->tipo == EV_DESCARTE) printf(" (%d)", ev->aux);
        if (ev->tipo == EV_ATENDIMENTO && ev->aux >= 0) {
            id_rotulo(e->tarefas[ev->aux].id, todos_inteiros, rot, sizeof rot);
            printf(" %s", rot);
        }
    }
    if (algum) putchar('\n');
}

static void ajuda_passo(void) {
    printf("Comandos:\n"
           "  [Enter] ou n [k]  avança 1 (ou k) tick(s)       p [k]  volta 1 (ou k) tick(s)\n"
           "  g t   vai para o instante t                     r      roda até o fim\n"
           "  s     estado do sistema                         t id   detalhes (TCB) da tarefa\n"
           "  e id  edita estado/parâmetros da tarefa         +      adiciona tarefa\n"
           "  a     troca o algoritmo   k  quantum   c  quantidade de CPUs\n"
           "  v t   fixa a janela do Gantt a partir de t      v      volta a seguir o relógio\n"
           "  f     Gantt completo      x [arquivo]  exporta SVG      h  ajuda      q  sair\n"
           "Editar num instante passado descarta o que já tinha sido simulado depois dele.\n");
}

/* Edita uma tarefa durante a simulação (req. 3.4). Toda mudança passa por
 * sim_preparar_edicao/sim_concluir_edicao, que descartam o futuro e gravam o
 * novo estado no histórico.
 *
 * Padrão de CADA opção do switch (importante para entender o código):
 *   1. pede o valor e VALIDA (se inválido, sai com `return` ANTES de
 *      sim_preparar_edicao: assim uma edição recusada não destrói o futuro);
 *   2. sim_preparar_edicao(s): descarta o futuro;
 *   3. altera o TCB;
 *   4. escreve a mensagem de confirmação em `msg`.
 * No fim do switch, sim_concluir_edicao grava o novo estado. */
static void editar_tarefa(Simulador *s, int i, char *msg, size_t tam) {
    Estado *e = &s->atual;
    TCB *k = &e->tarefas[i];
    int t = e->t;
    detalhes_tarefa(s, i);
    printf("\nO que alterar? (no instante %d)\n"
           "  1) cor   2) duração   3) período   4) prazo   5) ingresso\n"
           "  6) ticks restantes da ativação atual\n"
           "  7) %s   8) encerrar a tarefa agora   0) cancelar\n",
           t, suspensa_pelo_usuario(k) ? "retomar (tirar da suspensão)" : "suspender");
    char buf[32];
    if (!ler_linha("> ", buf, sizeof buf) || !*buf || buf[0] == '0') {
        fmsg(msg, tam, "Edição cancelada.");
        return;
    }
    int op;
    if (parse_int(buf, &op) != 0 || op < 1 || op > 8) {
        fmsg(msg, tam, "erro: opção '%s' inválida (use 0 a 8)", buf);
        return;
    }
    if (k->estado == T_TERMINADA && op != 1) {
        fmsg(msg, tam, "erro: a tarefa %s já terminou; só a cor pode mudar", k->id);
        return;
    }
    int v = 0;
    unsigned cor;
    switch (op) {
    case 1:
        if (!pedir_cor(k->cor, &cor)) return;
        sim_preparar_edicao(s);
        k->cor = cor;
        fmsg(msg, tam, "Cor da tarefa %s alterada para %06X.", k->id, cor);
        break;
    case 2:
        if (!pedir_int(tcb_eh_servidor(k) ? "capacidade cheia (C)" : "duração", k->duracao, &v, val_positivo, NULL))
            return;
        sim_preparar_edicao(s);
        edicao_duracao(k, v);
        if (tcb_eh_servidor(k))
            fmsg(msg, tam, "Capacidade do servidor %s = %d (a que resta agora: %d).", k->id, v, k->capacidade);
        else
            fmsg(msg, tam, "Duração da tarefa %s = %d (vale a partir da próxima ativação; use a opção 6 "
                 "para mudar a atual).", k->id, v);
        break;
    case 3: {
        const char *m = valida_periodo_editavel(k);
        if (m) {
            fmsg(msg, tam, "erro: o período de %s não pode mudar: %s", k->id, m);
            return;
        }
        if (!pedir_int("período", k->periodo, &v, val_periodo, NULL)) return;
        sim_preparar_edicao(s);
        edicao_periodo(k, v);
        fmsg(msg, tam, "Período da tarefa %s = %d; próxima chegada no instante %d%s.", k->id, v,
             k->proxima_chegada, k->proxima_chegada < t ? " (já passou: chega no próximo tick)" : "");
        break;
    }
    case 4:
        if (!pedir_int("prazo", k->prazo, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        edicao_prazo(k, v);
        fmsg(msg, tam, "Prazo da tarefa %s = %d%s.", k->id, v,
             k->restante > 0 ? "; deadline da ativação atual recalculado" : "");
        break;
    case 5:
        if (k->liberadas > 0) {
            fmsg(msg, tam, "erro: a tarefa %s já ingressou no instante %d; o ingresso só pode mudar "
                 "antes da primeira chegada", k->id, k->chegadas[0]);
            return;
        }
        if (!pedir_int("ingresso", k->ingresso < t ? t : k->ingresso, &v, val_min, &t)) return;
        sim_preparar_edicao(s);
        edicao_ingresso(k, v);
        fmsg(msg, tam, "Ingresso da tarefa %s = %d.", k->id, v);
        break;
    case 6:
        if (tcb_eh_servidor(k)) {
            /* No servidor, o que "resta" é a capacidade (de 0 até a cheia). */
            if (k->srv == SRV_BS || k->liberadas == 0) {
                fmsg(msg, tam, "erro: o servidor %s %s", k->id,
                     k->srv == SRV_BS ? "é BS: não tem capacidade" : "ainda não entrou no sistema");
                return;
            }
            if (!pedir_int("capacidade que resta", k->capacidade, &v, NULL, NULL)) return;
            if (v < 0 || v > k->duracao) {
                fmsg(msg, tam, "erro: a capacidade que resta precisa estar entre 0 e %d (a capacidade cheia)", k->duracao);
                return;
            }
            sim_preparar_edicao(s);
            k->capacidade = k->restante = v;
            fmsg(msg, tam, "Servidor %s com %d de capacidade.", k->id, v);
            break;
        }
        if (k->restante <= 0) {
            fmsg(msg, tam, "erro: a tarefa %s não tem ativação em andamento (estado: %s)", k->id,
                 estado_descricao(k));
            return;
        }
        if (!pedir_int("ticks restantes", k->restante, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        k->restante = v;
        fmsg(msg, tam, "Ativação atual da tarefa %s agora precisa de %d tick(s).", k->id, v);
        break;
    case 7:
        sim_preparar_edicao(s);
        edicao_alternar_suspensao(k);
        if (!suspensa_pelo_usuario(k))
            fmsg(msg, tam, "Tarefa %s retomada (%s).", k->id, estado_descricao(k));
        else
            fmsg(msg, tam, "Tarefa %s suspensa: não disputa CPU até ser retomada (ativações "
                 "continuam chegando e acumulando).", k->id);
        break;
    case 8:
        sim_preparar_edicao(s);
        edicao_encerrar(k, t);
        fmsg(msg, tam, "Tarefa %s encerrada no instante %d.", k->id, t);
        break;
    }
    sim_concluir_edicao(s);
}

/* Comando `+`: acrescenta uma tarefa com a simulação em andamento. O TCB
 * aponta para a lista de eventos da TarefaCfg, que precisa continuar
 * existindo; por isso a guardamos em `extras` (vive até o fim). O ingresso
 * mínimo é o instante atual: não se cria tarefa no passado. */
static void adicionar_tarefa(Simulador *s, Config *extras, char *msg, size_t tam) {
    Estado *e = &s->atual;
    /* ids das tarefas que existem NESTE instante (ponteiros para dentro dos
     * TCBs; o vetor de TCBs só é realocado depois do free abaixo) */
    const char **ids = xmalloc((size_t)(e->n + 1) * sizeof *ids);
    for (int i = 0; i < e->n; i++) ids[i] = e->tarefas[i].id;
    Config tmp;
    bool ok = nova_tarefa(ids, e->n, config_cor_padrao(e->n), e->t, &tmp);   /* modo manual (Projeto B) */
    free(ids);
    if (!ok) {
        config_liberar(&tmp);
        fmsg(msg, tam, "Nenhuma tarefa adicionada.");
        return;
    }
    const TarefaCfg *t = &tmp.tarefas[0];
    /* Req. 3 da parte B: o teste de escalonabilidade vem antes de aceitar,
     * contra as tarefas que ainda não terminaram. */
    char motivo[700];
    if (sim_admitir_tarefa(&s->atual, t, motivo, sizeof motivo)) {
        fmsg(msg, tam, "erro: tarefa %s impedida de ser escalonada: %s", t->id, motivo);
        config_liberar(&tmp);
        return;
    }
    /* O TCB aponta para os eventos (e as ocorrências) da configuração;
     * guardamos a tarefa em `extras`, que vive até o fim da simulação. */
    config_adicionar(extras, t);
    sim_preparar_edicao(s);
    sim_adicionar_tarefa(s, &extras->tarefas[extras->n - 1]);
    sim_concluir_edicao(s);
    fmsg(msg, tam, "Tarefa %s (%s) adicionada; chega no instante %d.", t->id, tipo_nome(t->tipo, t->srv),
         t->ingresso);
    config_liberar(&tmp);
}

/* Modo (a): o "debugger" do sistema. Laço principal, a cada volta:
 *   1. se chegou ao fim, exporta o SVG automaticamente (req. 2.4);
 *   2. redesenha a tela: cabeçalho, Gantt atualizado (req. 2.3), resumo do
 *      último tick e a mensagem do último comando;
 *   3. lê um comando e executa (switch no final).
 * `msg` leva o resultado do comando anterior para a próxima tela; se
 * começa com "erro" aparece em vermelho, senão em verde. */
void ui_passo_a_passo(const Config *cfg, const OpcoesUI *op) {
    Simulador s;
    sim_iniciar(&s, cfg, true, op->semente);
    Config extras;              /* tarefas criadas durante a simulação */
    config_padrao(&extras);
    char msg[1600] = "";
    char caminho_svg[1024];
    ui_caminho_svg(cfg, op, caminho_svg, sizeof caminho_svg);
    bool exportado = false;
    int janela_ini = -1;  /* -1 = a janela acompanha o relógio */
    char buf[256];

    for (;;) {
        /* Ao chegar no fim, gera a imagem automaticamente (req. 2.4). Depois
         * de uma edição o futuro muda, então exporta de novo no novo fim. */
        if (s.atual.fim && !exportado && s.atual.t == s.ncols) {
            char m2[1400];
            exportar(&s, cfg, caminho_svg, m2, sizeof m2);
            fmsg(msg, sizeof msg, "Simulação concluída no instante %d. %s", s.atual.t, m2);
            exportado = true;
        }

        term_limpar();
        const Escalonador *esc = sched_obter(s.atual.alg);
        term_negrito();
        printf("=== Passo a passo | %s | %s | %d CPU(s) | quantum %d | instante t = %d ===\n",
               cfg->caminho ? cfg->caminho : "", esc->nome, s.atual.ncpus, s.atual.quantum, s.atual.t);
        term_reset();

        int lbl = 8;
        int largura = term_largura() - lbl - 4;
        if (largura < 20) largura = 20;
        int fim = s.atual.t, ini;
        if (janela_ini >= 0) {
            ini = janela_ini;
            fim = ini + largura;
            if (fim > s.atual.t) fim = s.atual.t;
        } else {
            ini = fim - largura > 0 ? fim - largura : 0;
        }
        printf("Gantt: ticks %d a %d%s\n", ini, fim - 1, janela_ini >= 0 ? " (janela fixa: 'v' volta a seguir)" : "");
        /* gantt_tui só desenha colunas já simuladas e anteriores ao instante
         * atual: ao retroceder, o "futuro" gravado não aparece. `vis` é uma
         * CÓPIA rasa do simulador (só os números mudam: ncols e nev), usada
         * apenas para desenhar, então não alteramos o rastro real. */
        Simulador vis = s;
        vis.ncols = s.atual.t < s.ncols ? s.atual.t : s.ncols;
        int nev = 0;
        while (nev < s.nev && s.ev[nev].tick < s.atual.t) nev++;
        vis.nev = nev;
        gantt_tui(&vis, ini, fim);
        resumo_tick(&vis);
        if (s.atual.fim) printf("Simulação concluída. Use p/g para revisar ou e para editar e seguir de outro ponto.\n");
        else if (sim_bloqueado(&s.atual)) printf("Atenção: todas as tarefas restantes estão suspensas.\n");
        if (s.atual.t < s.ncols)
            printf("(há %d tick(s) já simulados à frente; n reaplica o histórico)\n", s.ncols - s.atual.t);
        if (*msg) {
            if (!strncmp(msg, "erro", 4)) term_fg(0xE74C3C);
            else term_fg(0x2ECC71);
            printf("%s\n", msg);
            term_reset();
            msg[0] = '\0';
        }
        if (!ler_linha("[Enter] avança, h ajuda > ", buf, sizeof buf)) break;

        /* Interpretação do comando: cmd = 1ª letra, resto = o que vem
         * depois, arg = resto como número (se for). strchr checa se a letra
         * é um comando conhecido. Os comandos `x` (nome do arquivo), `t` e
         * `e` (id da tarefa, que pode ser texto) recebem o resto como
         * texto, por isso não exigem número. */
        char cmd = buf[0];
        char *resto = str_trim(buf + (cmd ? 1 : 0));
        int arg = 0;
        bool tem_arg = parse_int(resto, &arg) == 0;
        if (cmd && !strchr("npgrstea+kcvfxhq", cmd)) {
            fmsg(msg, sizeof msg, "erro: comando '%s' desconhecido (h mostra a ajuda)", buf);
            continue;
        }
        if (*resto && !tem_arg && cmd != 'x' && cmd != 't' && cmd != 'e') {
            fmsg(msg, sizeof msg, "erro: argumento '%s' não é um número inteiro", resto);
            continue;
        }

        switch (cmd) {
        case '\0':
        case 'n': {
            int k = tem_arg ? arg : 1;
            if (k < 1) { fmsg(msg, sizeof msg, "erro: a quantidade de ticks deve ser >= 1"); break; }
            int feitos = 0;
            while (feitos < k && sim_avancar(&s)) feitos++;
            if (feitos < k) {
                if (s.atual.fim) fmsg(msg, sizeof msg, "erro: a simulação já terminou (instante %d)", s.atual.t);
                else if (*s.erro)   /* o escalonador falhou: o tick foi desfeito */
                    fmsg(msg, sizeof msg, "erro: %s; volte no tempo (p) ou troque o algoritmo (a) para continuar",
                         s.erro);
            }
            break;
        }
        case 'p': {
            int k = tem_arg ? arg : 1;
            if (k < 1) { fmsg(msg, sizeof msg, "erro: a quantidade de ticks deve ser >= 1"); break; }
            if (s.atual.t == 0) { fmsg(msg, sizeof msg, "erro: já está no instante 0"); break; }
            /* Voltar não recalcula nada: sim_voltar só recarrega o snapshot
             * do tick anterior (o relógio recua junto, req. 1.5.2). */
            for (int j = 0; j < k && sim_voltar(&s); j++) {}
            break;
        }
        case 'g':
            if (!tem_arg) { fmsg(msg, sizeof msg, "erro: informe o instante, ex.: g 50"); break; }
            if (arg < 0) { fmsg(msg, sizeof msg, "erro: o instante não pode ser negativo"); break; }
            if (sim_ir_para(&s, arg) != arg) {
                if (*s.erro) fmsg(msg, sizeof msg, "erro: parou no instante %d: %s", s.atual.t, s.erro);
                else fmsg(msg, sizeof msg, "A simulação termina no instante %d; parou lá.", s.atual.t);
            }
            break;
        case 'r': {
            const char *motivo = sim_rodar_ate_fim(&s);
            if (motivo) fmsg(msg, sizeof msg, "erro: parou no instante %d: %s", s.atual.t, motivo);
            break;
        }
        case 's': term_limpar(); estado_sistema(&s); pausar(); break;
        case 't': case 'e': {
            if (!*resto) { fmsg(msg, sizeof msg, "erro: informe o id da tarefa, ex.: %c 1", cmd); break; }
            int i = sim_indice(&s.atual, resto);   /* índice, -1 = não existe */
            if (i < 0) { fmsg(msg, sizeof msg, "erro: não existe tarefa com id %s neste instante", resto); break; }
            term_limpar();
            if (cmd == 't') {
                detalhes_tarefa(&s, i);
                pausar();
            } else {
                editar_tarefa(&s, i, msg, sizeof msg);
                exportado = false;
            }
            break;
        }
        case '+': adicionar_tarefa(&s, &extras, msg, sizeof msg); exportado = false; break;
        case 'a': {
            int a = pedir_algoritmo(s.atual.alg);
            if (a >= 0 && a != s.atual.alg) {
                sim_preparar_edicao(&s);
                s.atual.alg = a;
                sim_concluir_edicao(&s);
                exportado = false;
                fmsg(msg, sizeof msg, "Algoritmo trocado para %s a partir do instante %d.",
                     sched_obter(a)->nome, s.atual.t);
            }
            break;
        }
        case 'k': case 'c': {
            int v;
            bool ok = cmd == 'k' ? pedir_int("quantum", s.atual.quantum, &v, val_quantum, NULL)
                                 : pedir_int(ROTULO_CPUS, s.atual.ncpus, &v, val_cpus, NULL);
            if (!ok) break;
            sim_preparar_edicao(&s);
            if (cmd == 'k') s.atual.quantum = v;
            else s.atual.ncpus = v;
            sim_concluir_edicao(&s);
            exportado = false;
            fmsg(msg, sizeof msg, "%s = %d a partir do instante %d.", cmd == 'k' ? "Quantum" : "CPUs", v, s.atual.t);
            break;
        }
        case 'v':
            if (tem_arg && arg < 0) { fmsg(msg, sizeof msg, "erro: o instante não pode ser negativo"); break; }
            janela_ini = tem_arg ? arg : -1;
            break;
        case 'f': {
            term_limpar();
            Simulador v2 = s;
            v2.ncols = vis.ncols;
            v2.nev = vis.nev;
            gantt_tui_completo(&v2);
            pausar();
            break;
        }
        case 'x': {
            const char *destino = *resto ? resto : caminho_svg;
            Simulador v2 = s;
            v2.ncols = vis.ncols;
            v2.nev = vis.nev;
            exportar(&v2, cfg, destino, msg, sizeof msg);
            break;
        }
        case 'h': term_limpar(); ajuda_passo(); pausar(); break;
        case 'q': goto sair;   /* sai dos DOIS laços de uma vez (for + switch) */
        default: fmsg(msg, sizeof msg, "erro: comando '%c' desconhecido (h mostra a ajuda)", cmd);
        }
    }
sair:
    estatisticas(&s);
    sim_liberar(&s);
    config_liberar(&extras);
}
