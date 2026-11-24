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
#include "admissao.h"
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
static const char *val_periodo_id(int v, const void *ctx) { return valida_periodo_tarefa(v, (const char *)ctx); }
static const char *val_periodo_k(int v, const void *ctx) { return valida_periodo_edicao((const TCB *)ctx, v); }
static const char *val_prazo_tipo(int v, const void *ctx) { return valida_prazo_tarefa(v, *(const TipoTarefa *)ctx); }
static const char *val_min(int v, const void *ctx) { return valida_ingresso(v, *(const int *)ctx); }
static const char *val_capacidade(int v, const void *ctx) {
    static char buf[64];
    if (v >= 0 && v <= *(const int *)ctx) return NULL;
    snprintf(buf, sizeof buf, "a capacidade deve estar entre 0 e %d (a do servidor)", *(const int *)ctx);
    return buf;
}

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
 * editada (-1 = tarefa nova), que não conta como repetida. */
typedef struct { const char *const *ids; int n; int proprio; } CtxIds;

/* Pede o id (texto) mostrando o sugerido entre colchetes; Enter mantém o
 * sugerido. Mesmo laço do pedir_int: repete até o id servir (id_validar) e
 * não repetir o de outra tarefa (valida_id). `sugerido` pode ser o próprio
 * `out` (editar mantém o id), por isso a cópia. */
static bool pedir_id(const char *sugerido, char *out, size_t tam, const CtxIds *c) {
    char sug[ID_MAX], buf[128], prompt[ID_MAX + 32];
    snprintf(sug, sizeof sug, "%s", sugerido);
    snprintf(prompt, sizeof prompt, "  id [%s]: ", sug);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;   /* ler_linha já faz str_trim */
        const char *v = *buf ? buf : sug;
        const char *m = id_validar(v);
        if (!m) m = valida_id(v, c->ids, c->n, c->proprio);
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

/* Lê os dados de uma tarefa nova (ou editada, se `t` já vier preenchida).
 * `min_ingresso` impede chegar no passado durante a simulação. Cada campo
 * usa o valor atual de `t` como sugestão (req. 3.2: valores padrão). */
/* O tipo da tarefa sai do id (servidor) e do sinal do período (Projeto B):
 * depois do período, os campos seguintes já são pedidos com as regras do
 * tipo (prazo 0 só na aperiódica; instantes de chegada na lista de eventos
 * da aperiódica e da esporádica). */
static bool pedir_tarefa(TarefaCfg *t, const CtxIds *ids, int min_ingresso, char *eventos, size_t tam_ev) {
    if (!pedir_id(t->id, t->id, sizeof t->id, ids)) return false;
    if (!pedir_cor(t->cor, &t->cor)) return false;
    if (!pedir_int("ingresso", t->ingresso, &t->ingresso, val_min, &min_ingresso)) return false;
    bool servidor = config_id_servidor(t->id, NULL);
    if (!pedir_int(servidor ? "capacidade (duração)" : "duração", t->duracao, &t->duracao, val_positivo, NULL)) return false;
    TipoTarefa tipo_antigo = t->tipo;
    int periodo_antigo = t->periodo;
    if (!pedir_int(servidor ? "período do servidor" : "período (> 0 periódica, 0 aperiódica, < 0 esporádica)", t->periodo,
                   &t->periodo, val_periodo_id, t->id))
        return false;
    config_classificar(t);
    printf("  tipo: %s\n", tipo_nome_de(t->tipo, t->servidor));
    /* Se o prazo acompanhava o período (prazo implícito), continua acompanhando. */
    int sug = t->prazo == prazo_padrao_tipo(tipo_antigo, periodo_antigo) ? prazo_padrao_tipo(t->tipo, t->periodo) : t->prazo;
    if (valida_prazo_tarefa(sug, t->tipo)) sug = prazo_padrao_tipo(t->tipo, t->periodo);
    if (!pedir_int(t->tipo == TIPO_APERIODICA ? "prazo (0 = sem prazo)" : "prazo", sug, &t->prazo, val_prazo_tipo, &t->tipo))
        return false;
    bool chegadas = t->tipo == TIPO_APERIODICA || t->tipo == TIPO_ESPORADICA;
    const char *atual = t->eventos ? t->eventos : "";
    char prompt[700], buf[512];
    snprintf(prompt, sizeof prompt, "  %s [%s]: ",
             chegadas ? "instantes de chegada, relativos ao ingresso (ex.: 0,20,45; vazio = só no ingresso)"
                      : "lista de eventos", atual);
    for (;;) {
        if (!ler_linha(prompt, buf, sizeof buf)) return false;
        snprintf(eventos, tam_ev, "%s", *buf ? buf : atual);
        const char *m = valida_eventos_tarefa(eventos, t->tipo, t->periodo, t->ingresso);
        if (!m) break;
        ui_erro("%s", m);
    }
    t->eventos = eventos;
    return true;
}

/* ------------------------------------------------------- configuração */

/* Mostra U = soma(C/P) e o que ela significa para o algoritmo escolhido:
 *   RM:  compara com o limite n(2^(1/n)-1) (teste suficiente: abaixo dele
 *        garante escalonar, acima fica "inconclusivo");
 *   EDF: com prazo = período, escalona em 1 CPU se e somente se U <= 1.
 * É só uma informação para o usuário; não afeta a simulação. */
static void imprimir_utilizacao(int n, double u, int alg) {
    printf("Utilização U = Σ C/P (periódicas e servidores) = %.3f", u);
    const Escalonador *e = sched_obter(alg);
    if (e && str_ieq(e->nome, "RM")) {
        double lim = sim_limite_rm(n);
        printf("  |  teste suficiente do RM: U ≤ n(2^(1/n)−1) = %.3f → %s", lim,
               u <= lim ? "escalonável (1 CPU)" : "inconclusivo (pode perder prazos)");
    } else if (e && str_ieq(e->nome, "EDF")) {
        printf("  |  EDF (prazo = período): escalonável em 1 CPU sse U ≤ 1 → %s",
               u <= 1.0 ? "sim" : "não");
    }
    printf("\n(os testes valem para 1 CPU; com mais CPUs são só uma referência)\n");
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
    printf("  %6s  %-11s %-7s %8s %8s %8s %8s  %s\n", "id", "tipo", "cor", "ingresso", "duração", "período", "prazo", "eventos");
    double u = 0;
    int nu = 0;   /* quantas entram na utilização (o n do limite do RM) */
    for (int i = 0; i < cfg->n; i++) {
        const TarefaCfg *t = &cfg->tarefas[i];
        const char *tipo = tipo_nome_de(t->tipo, t->servidor);
        printf("  %*s%s  %s%*s ", folga(t->id, 6), "", t->id, tipo, folga(tipo, 11), "");
        term_bg(t->cor);
        printf("  ");
        term_reset();
        printf("%06X %7d %8d %8d %8d  %s\n", t->cor, t->ingresso, t->duracao, t->periodo, t->prazo,
               t->eventos && *t->eventos ? t->eventos : "-");
        double p = utilizacao_parcela(t->tipo, t->servidor, t->duracao, t->periodo);
        if (p >= 0) {
            u += p;
            nu++;
        }
    }
    imprimir_utilizacao(nu, u, cfg->algoritmo);
    printf("(servidor: duração = capacidade; aperiódica: período 0; esporádica: |período| = intervalo mínimo e "
           "eventos = instantes de chegada)\n");
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
            CtxIds c = {ids, cfg->n, i};
            TarefaCfg t = cfg->tarefas[i];
            char ev[512];
            printf("Editando a tarefa %s (Enter mantém o valor):\n", id);
            if (pedir_tarefa(&t, &c, 0, ev, sizeof ev)) {
                /* `t` divide os ponteiros de eventos com a original: copia os
                 * campos numéricos e troca a lista pelo texto novo. */
                TarefaCfg *dst = &cfg->tarefas[i];
                t.eventos = dst->eventos;
                t.lista_ev = dst->lista_ev;
                t.nev = dst->nev;
                *dst = t;
                config_classificar(dst);   /* o período (ou o id) pode ter mudado o tipo */
                config_definir_eventos(dst, ev);
            }
            free(ids);
            break;
        }
        case 'n': {
            const char **ids = ids_cfg(cfg);
            CtxIds c = {ids, cfg->n, -1};
            TarefaCfg t = {"", config_cor_padrao(cfg->n), PADRAO_INGRESSO,
                           PADRAO_DURACAO, PADRAO_PERIODO, PADRAO_PERIODO, NULL, 0, NULL, 0,
                           TIPO_PERIODICA, SERV_BS, NULL, 0};
            id_sugerir(ids, cfg->n, t.id, sizeof t.id);
            char ev[512];
            printf("Nova tarefa (Enter aceita o valor sugerido):\n");
            if (pedir_tarefa(&t, &c, 0, ev, sizeof ev)) config_adicionar(cfg, &t);
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
        char ativ[32];
        tcb_ativacoes_txt(k, ativ, sizeof ativ);
        printf("  %*s%s %7s %7d %7d %7d %8.1f %8d %6d %7d ", folga(k->id, 6), "", k->id, ativ,
               k->deadlines_perdidos, k->ticks_exec, k->ticks_espera,
               k->concluidas > k->recusas ? (double)k->resposta_soma / (k->concluidas - k->recusas) : 0.0, k->resposta_max,
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

/* Decisões do teste de admissão (req. B 3), com o motivo de cada uma:
 * recusa em vermelho, aceite sem garantia em amarelo. */
static void admissoes(const Simulador *s) {
    if (s->nadm == 0) return;
    term_negrito();
    printf("Teste de admissão:\n");
    term_reset();
    for (int i = 0; i < s->nadm; i++) {
        const RegistroAdmissao *r = &s->adm[i];
        const TCB *k = &s->atual.tarefas[r->tarefa];
        printf("  t=%d %s", r->tick, k->id);
        if (r->ativacao >= 0) printf(" (ativação %d)", r->ativacao + 1);
        printf(": ");
        if (r->resultado != ADM_ACEITA) term_fg(r->resultado == ADM_RECUSADA ? 0xE74C3C : 0xF1C40F);
        printf("%s", admissao_nome(r->resultado));
        term_reset();
        printf(" (%s)\n", r->motivo);
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
    sim_iniciar(&s, cfg, false, op->semente, op->admissao);
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
    admissoes(&s);
    char caminho[1024], msg[1400];
    ui_caminho_svg(cfg, op, caminho, sizeof caminho);
    bool ok = exportar(&s, cfg, caminho, msg, sizeof msg);
    if (ok) printf("%s\n", msg);
    else ui_erro("%s", msg + strlen("erro ao exportar: "));
    sim_liberar(&s);
    return ok && !motivo ? 0 : 1;
}

/* ---------------------------------------------------- modo passo a passo */

/* Parte do `t id` que depende do tipo (Projeto B): o que o servidor tem de
 * capacidade e quem ele executa; quando a aperiódica chega e onde ela está
 * (na fila ou com qual servidor); e o que o teste de admissão decidiu. */
static void detalhes_tipo(const Simulador *s, int i) {
    const Estado *e = &s->atual;
    const TCB *k = &e->tarefas[i];
    printf("  tipo: %s", tipo_nome(k));
    if (k->tipo == TIPO_ESPORADICA) printf(" (intervalo mínimo entre ocorrências: %d)", -k->periodo);
    if (k->tipo == TIPO_APERIODICA && k->prazo <= 0) printf(" (sem prazo)");
    if (tcb_eh_servidor(k) && k->servidor != SERV_BS) printf(" (capacidade %d a cada %d tick(s))", k->duracao, k->periodo);
    putchar('\n');
    if (tcb_eh_servidor(k)) {
        if (k->servidor != SERV_BS) printf("  capacidade restante: %d de %d\n", k->capacidade, k->duracao);
        printf("  executando a aperiódica: %s\n", k->atendendo >= 0 ? e->tarefas[k->atendendo].id : "nenhuma");
        if (k->servidor == SERV_SS) {
            if (k->ss_ativo_desde >= 0)
                printf("  ativo desde o instante %d (gastou %d; volta no instante %d)\n", k->ss_ativo_desde,
                       k->ss_consumo, k->ss_ativo_desde + k->periodo);
            printf("  recargas agendadas: %s", k->nrecargas ? "" : "nenhuma");
            for (int j = 0; j < k->nrecargas; j++)
                printf("%s+%d no instante %d", j ? ", " : "", k->recargas[j].quantidade, k->recargas[j].instante);
            putchar('\n');
        }
    }
    if (tcb_eh_atendida(k)) {
        printf("  ocorrências (relativas ao ingresso %d):", k->ingresso);
        for (int j = 0; j < k->nocorrencias && j < 20; j++) printf(" %d", k->ocorrencias[j]);
        if (k->nocorrencias > 20) printf(" ... (%d ao todo)", k->nocorrencias);
        putchar('\n');
        if (k->restante > 0 && k->atendida_por >= 0) {
            printf("  com o servidor %s\n", e->tarefas[k->atendida_por].id);
        } else if (k->restante > 0 && (k->estado == T_PRONTA || k->estado == T_EXECUTANDO)) {
            int *fila = xmalloc((size_t)(e->n ? e->n : 1) * sizeof *fila);
            int nf = sim_fila_aperiodicas(e, fila), pos = 0;
            for (int j = 0; j < nf; j++)
                if (fila[j] == i) pos = j + 1;
            free(fila);
            printf("  na fila de aperiódicas: posição %d de %d\n", pos, nf);
        }
        if (k->recusas) printf("  ativações recusadas pelo teste de admissão: %d\n", k->recusas);
    }
    const RegistroAdmissao *r = sim_admissao_tarefa(s, i);
    if (r) printf("  teste de admissão (instante %d): %s (%s)\n", r->tick, admissao_nome(r->resultado), r->motivo);
    else if (!k->admitida && k->estado != T_TERMINADA) printf("  teste de admissão: no ingresso (instante %d)\n", k->ingresso);
}

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
    detalhes_tipo(s, i);
    if (!tcb_eh_atendida(k)) {
        printf("  eventos: %d", k->neventos);
        for (int j = 0; j < k->neventos; j++) printf("%s%s", j ? " | " : ": ", k->eventos[j]);
        putchar('\n');
    }
    printf("  estado: %s\n", estado_descricao(k));
    if ((k->restante > 0 && !tcb_eh_atendida(k)) ||
        (tcb_eh_servidor(k) && (k->estado == T_PRONTA || k->estado == T_EXECUTANDO)))
        printf("  prioridade (menor = mais prioritária): nominal %d, ativa %d\n", k->prio_nominal, k->prio_ativa);
    if (!tcb_eh_servidor(k))
        printf("  ativações: %d chegaram, %d concluídas (de %d)\n", k->liberadas, k->concluidas, tcb_total_ativacoes(k));
    if (k->restante > 0) {
        char dl[32] = "sem prazo";
        if (k->prazo > 0) snprintf(dl, sizeof dl, "deadline em %d", k->deadline_abs);
        printf("  ativação atual: nº %d, chegou em %d, %s, faltam %d tick(s)%s\n",
               k->concluidas + 1, k->chegada_atual, dl, k->restante,
               ativacao_atual_perdeu_prazo(k) ? " — PRAZO PERDIDO" : "");
        if (k->liberadas - k->concluidas > 1)
            printf("  ativações acumuladas esperando: %d\n", k->liberadas - k->concluidas - 1);
    }
    if (!tcb_eh_servidor(k) && k->liberadas < tcb_total_ativacoes(k) && k->estado != T_TERMINADA)
        printf("  próxima chegada: instante %d\n", k->proxima_chegada);
    printf("  CPU no último tick: %s", k->cpu >= 0 ? "" : "nenhuma\n");
    if (k->cpu >= 0) printf("CPU%d (quantum usado %d/%d)\n", k->cpu, k->quantum_usado, s->atual.quantum);
    printf("  estatísticas: executou %d, esperou %d, suspensa %d tick(s); %d preempção(ões), "
           "%d sorteio(s), %d prazo(s) perdido(s)\n", k->ticks_exec, k->ticks_espera, k->ticks_suspensa,
           k->preempcoes, k->sorteios, k->deadlines_perdidos);
    if (k->termino >= 0) printf("  terminou no instante %d\n", k->termino);
}

/* A fila de aperiódicas, na ordem de atendimento (esporádicas primeiro), e
 * quem está com cada servidor. Só aparece se há aperiódica ou servidor. */
static void imprimir_fila(const Estado *e) {
    bool tem = false;
    for (int i = 0; i < e->n; i++)
        if (tcb_eh_servidor(&e->tarefas[i]) || tcb_eh_atendida(&e->tarefas[i])) tem = true;
    if (!tem) return;
    int *fila = xmalloc((size_t)(e->n ? e->n : 1) * sizeof *fila);
    int nf = sim_fila_aperiodicas(e, fila);
    printf("  fila de aperiódicas:");
    for (int j = 0; j < nf; j++)
        printf(" %s (%s, chegou em %d)", e->tarefas[fila[j]].id, tipo_curto(&e->tarefas[fila[j]]),
               e->tarefas[fila[j]].chegada_atual);
    printf("%s\n", nf ? "" : " vazia");
    free(fila);
    bool algum = false;
    for (int i = 0; i < e->n; i++) {
        const TCB *k = &e->tarefas[i];
        if (!tcb_eh_servidor(k) || k->atendendo < 0) continue;
        printf("%s %s -> %s", algum ? "," : "  com servidor:", k->id, e->tarefas[k->atendendo].id);
        algum = true;
    }
    if (algum) putchar('\n');
}

/* Comando `s`: visão geral do sistema (todas as tarefas numa tabela). */
static void estado_sistema(const Simulador *s) {
    const Estado *e = &s->atual;
    const Escalonador *esc = sched_obter(e->alg);
    term_negrito();
    printf("Sistema no instante %d\n", e->t);
    term_reset();
    printf("  algoritmo %s | quantum %d | %d CPU(s) | teste de admissão %s | %s\n", esc->nome, e->quantum,
           e->ncpus, e->admissao ? "ligado" : "desligado",
           e->fim ? "simulação concluída" : sim_bloqueado(e) ? "bloqueada (todas suspensas)" : "em andamento");
    int nu;
    double u = sim_utilizacao(e, &nu);
    imprimir_utilizacao(nu, u, e->alg);
    printf("\n  %6s %-5s %-34s %7s %8s %8s %5s %7s\n", "id", "tipo", "estado", "ativ.", "restante", "deadline", "CPU",
           "prazos");
    int *ordem = gantt_ordem(e);
    for (int r = e->n - 1; r >= 0; r--) {
        const TCB *k = &e->tarefas[ordem[r]];
        char ativ[32], rest[16] = "-", dl[16] = "-";
        tcb_ativacoes_txt(k, ativ, sizeof ativ);
        /* "restante" do servidor é a capacidade que ainda tem */
        if (tcb_eh_servidor(k) && k->servidor != SERV_BS) snprintf(rest, sizeof rest, "cap %d", k->capacidade);
        if (k->restante > 0) snprintf(rest, sizeof rest, "%d", k->restante);
        if (k->restante > 0 && k->prazo > 0) snprintf(dl, sizeof dl, "%d", k->deadline_abs);
        const char *est = estado_descricao(k);
        printf("  %*s%s %-5s %s%*s %7s %8s %8s ", folga(k->id, 6), "", k->id, tipo_curto(k), est, folga(est, 34), "", ativ,
               rest, dl);
        if (k->cpu >= 0 && k->estado == T_EXECUTANDO) printf("%5d ", k->cpu);
        else printf("%5s ", "-");
        printf("%7d\n", k->deadlines_perdidos);
    }
    free(ordem);
    imprimir_fila(e);
    int cont[3] = {0, 0, 0};
    for (int i = 0; i < s->nadm; i++) cont[s->adm[i].resultado]++;
    if (s->nadm)
        printf("  teste de admissão até agora: %d aceita(s), %d sem garantia, %d recusada(s) "
               "(d mostra os motivos)\n", cont[ADM_ACEITA], cont[ADM_SEM_GARANTIA], cont[ADM_RECUSADA]);
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
        int aper, quem = gantt_quem_na_cpu(s, t, c, &aper);   /* servidor: "PS1:A1" */
        if (quem >= 0) {
            id_rotulo(e->tarefas[quem].id, todos_inteiros, rot, sizeof rot);
            printf("  CPU%d=%s", c, rot);
            if (aper >= 0) {
                id_rotulo(e->tarefas[aper].id, todos_inteiros, rot, sizeof rot);
                printf(":%s", rot);
            }
        } else {
            printf("  CPU%d=desligada", c);
        }
    }
    printf("  | prontas:");
    int np = 0, na = 0;
    for (int i = 0; i < e->n; i++)
        if (sim_celula(s, t, i) == G_PRONTA) {
            if (tcb_eh_atendida(&e->tarefas[i])) {   /* aperiódica esperando: vai na lista abaixo */
                na++;
                continue;
            }
            id_rotulo(e->tarefas[i].id, todos_inteiros, rot, sizeof rot);
            printf(" %s", rot);
            np++;
        }
    if (!np) printf(" (nenhuma)");
    if (na) {
        printf("  | aperiódicas esperando:");
        for (int i = 0; i < e->n; i++)
            if (sim_celula(s, t, i) == G_PRONTA && tcb_eh_atendida(&e->tarefas[i])) {
                id_rotulo(e->tarefas[i].id, todos_inteiros, rot, sizeof rot);
                printf(" %s", rot);
            }
    }
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
           "  d     decisões do teste de admissão (aceita, sem garantia ou recusada, com o motivo)\n"
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
           "  1) cor   2) %s   3) período   4) prazo   5) ingresso\n"
           "  6) %s\n"
           "  7) %s   8) encerrar a tarefa agora   0) cancelar\n",
           t, tcb_eh_servidor(k) ? "capacidade" : "duração",
           tcb_eh_servidor(k) ? "capacidade que o servidor tem agora" : "ticks restantes da ativação atual",
           suspensa_pelo_usuario(k) ? "retomar (tirar da suspensão)" : "suspender");
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
        if (k->recusada)
            fmsg(msg, tam, "erro: a tarefa %s foi recusada pelo teste de admissão; só a cor pode mudar", k->id);
        else if (tcb_eh_servidor(k))
            fmsg(msg, tam, "erro: o servidor %s já terminou; só a cor pode mudar", k->id);
        else
            fmsg(msg, tam, "erro: a tarefa %s já terminou as %d ativações; só a cor pode mudar",
                 k->id, tcb_total_ativacoes(k));
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
        if (!pedir_int(tcb_eh_servidor(k) ? "capacidade (duração)" : "duração", k->duracao, &v, val_positivo, NULL)) return;
        sim_preparar_edicao(s);
        k->duracao = v;
        if (tcb_eh_servidor(k))
            fmsg(msg, tam, "Capacidade do servidor %s = %d (vale a partir da próxima recarga; use a opção 6 "
                 "para mudar a que ele tem agora).", k->id, v);
        else
            fmsg(msg, tam, "Duração da tarefa %s = %d (vale a partir da próxima ativação; use a opção 6 "
                 "para mudar a atual).", k->id, v);
        break;
    case 3:
        if (!pedir_int("período", k->periodo, &v, val_periodo_k, k)) return;
        sim_preparar_edicao(s);
        edicao_periodo(k, v);
        if (k->tipo == TIPO_PERIODICA || (tcb_eh_servidor(k) && (k->servidor == SERV_PS || k->servidor == SERV_DS)))
            fmsg(msg, tam, "Período da tarefa %s = %d; próxima chegada no instante %d%s.", k->id, v,
                 k->proxima_chegada, k->proxima_chegada < t ? " (já passou: chega no próximo tick)" : "");
        else
            fmsg(msg, tam, "Período da tarefa %s = %d.", k->id, v);
        break;
    case 4:
        if (!pedir_int(k->tipo == TIPO_APERIODICA ? "prazo (0 = sem prazo)" : "prazo", k->prazo, &v, val_prazo_tipo,
                       &k->tipo))
            return;
        sim_preparar_edicao(s);
        edicao_prazo(k, v);
        fmsg(msg, tam, "Prazo da tarefa %s = %d%s.", k->id, v,
             k->restante > 0 ? "; deadline da ativação atual recalculado" : "");
        break;
    case 5:
        if (k->liberadas > 0) {
            fmsg(msg, tam, "erro: a tarefa %s já ingressou no instante %d; o ingresso só pode mudar "
                 "antes da primeira chegada", k->id, tcb_chegada(k, 0));
            return;
        }
        if (!pedir_int("ingresso", k->ingresso < t ? t : k->ingresso, &v, val_min, &t)) return;
        sim_preparar_edicao(s);
        edicao_ingresso(k, v);
        fmsg(msg, tam, "Ingresso da tarefa %s = %d.", k->id, v);
        break;
    case 6:
        if (tcb_eh_servidor(k)) {   /* o servidor não tem ativação própria: a opção mexe na capacidade */
            if (k->servidor == SERV_BS) {
                fmsg(msg, tam, "erro: o Background Server %s não tem capacidade (usa toda CPU que sobra)", k->id);
                return;
            }
            int lim = k->duracao;
            if (!pedir_int("capacidade restante", k->capacidade, &v, val_capacidade, &lim)) return;
            sim_preparar_edicao(s);
            k->capacidade = v;
            fmsg(msg, tam, "Servidor %s agora tem %d tick(s) de capacidade até a próxima recarga.", k->id, v);
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
    /* Mudou um parâmetro de uma tarefa que já está no sistema: o teste de
     * admissão não bloqueia a edição (é o usuário mexendo no sistema como
     * num debugger), mas avisa o que ele diria agora. */
    if ((op == 2 || op == 3 || op == 4) && k->admitida && k->estado != T_TERMINADA && e->admissao) {
        Admissao a = admissao_tarefa(e, i);
        if (a.resultado != ADM_ACEITA) {
            size_t l = strlen(msg);
            snprintf(msg + l, tam - l, " Aviso: com o valor novo, o teste de admissão daria \"%s\" (%s).",
                     admissao_nome(a.resultado), a.motivo);
        }
    }
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
    CtxIds c = {ids, e->n, -1};
    TarefaCfg t = {"", config_cor_padrao(e->n), e->t, PADRAO_DURACAO,
                   PADRAO_PERIODO, PADRAO_PERIODO, NULL, 0, NULL, 0,
                   TIPO_PERIODICA, SERV_BS, NULL, 0};
    id_sugerir(ids, e->n, t.id, sizeof t.id);
    char ev[512];
    int min = e->t;
    printf("Nova tarefa (Enter aceita o valor sugerido; ingresso >= %d):\n", min);
    if (!pedir_tarefa(&t, &c, min, ev, sizeof ev)) {
        free(ids);
        return;
    }
    free(ids);
    /* O TCB aponta para os eventos da configuração; guardamos a tarefa em
     * `extras`, que vive até o fim da simulação. */
    config_adicionar(extras, &t);
    sim_preparar_edicao(s);
    sim_adicionar_tarefa(s, &extras->tarefas[extras->n - 1]);
    sim_concluir_edicao(s);
    const TCB *k = &s->atual.tarefas[s->atual.n - 1];
    fmsg(msg, tam, "Tarefa %s adicionada (%s); é criada no instante %d e passa pelo teste de admissão lá.", t.id,
         tipo_nome(k), t.ingresso);
    if (s->atual.admissao) {
        Admissao a = admissao_tarefa(&s->atual, s->atual.n - 1);
        size_t l = strlen(msg);
        snprintf(msg + l, tam - l, " Se entrasse agora: %s (%s).", admissao_nome(a.resultado), a.motivo);
    }
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
    sim_iniciar(&s, cfg, true, op->semente, op->admissao);
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
        if (cmd && !strchr("npgrsteda+kcvfxhq", cmd)) {
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
        case 'd':
            term_limpar();
            if (s.nadm) admissoes(&s);
            else printf("Nenhuma tarefa passou pelo teste de admissão até o instante %d%s.\n", s.atual.t,
                        s.atual.admissao ? "" : " (o teste está desligado: --sem-admissao)");
            pausar();
            break;
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
