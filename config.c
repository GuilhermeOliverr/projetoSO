/*
 * config.c - parser do arquivo de configuração (ver config.h).
 *
 * Regras (req. 3.3.x), todas cobertas por arquivos em tests/:
 *  - maiúsculas/minúsculas não importam (o algoritmo é comparado com str_ieq);
 *  - qualquer linha pode terminar com ';' ou não;
 *  - espaços em volta dos campos e linhas em branco são ignorados;
 *  - finais de linha Windows (\r\n) e o BOM UTF-8 de editores do Windows
 *    também são aceitos, porque um arquivo válido criado no Bloco de Notas
 *    não pode falhar (req. 3.3.7);
 *  - linhas começando com '#' são comentários (extensão nossa, inofensiva:
 *    nenhuma linha válida começa com '#');
 *  - campo vazio recebe o valor padrão, com aviso (req. 3.2);
 *  - tarefas aperiódicas (periodo = 0) são ignoradas com aviso (req. 4.4).
 */
#include "config.h"
#include "sched.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- mensagens */

void msg_add(ListaMsg *l, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->itens = xrealloc(l->itens, (size_t)l->cap * sizeof *l->itens);
    }
    l->itens[l->n++] = xstrdup(buf);
}

void msg_liberar(ListaMsg *l) {
    for (int i = 0; i < l->n; i++) free(l->itens[i]);
    free(l->itens);
    l->itens = NULL;
    l->n = l->cap = 0;
}

void config_imprimir_mensagens(const Config *cfg) {
    for (int i = 0; i < cfg->avisos.n; i++) {
        term_fg(0xF1C40F);
        printf("aviso: ");
        term_reset();
        printf("%s\n", cfg->avisos.itens[i]);
    }
    for (int i = 0; i < cfg->erros.n; i++) {
        term_fg(0xE74C3C);
        printf("erro: ");
        term_reset();
        printf("%s\n", cfg->erros.itens[i]);
    }
}

/* --------------------------------------------------------------- básicos */

unsigned config_cor_padrao(int i) {
    /* Paleta com tons bem separados, para que tarefas vizinhas no Gantt não
     * se confundam. Depois de 12 tarefas as cores se repetem com variação. */
    static const unsigned paleta[] = {
        0xE74C3C, 0x3498DB, 0x2ECC71, 0xF39C12, 0x9B59B6, 0x1ABC9C,
        0xE67E22, 0x34495E, 0xF1C40F, 0xE84393, 0x16A085, 0x8E44AD,
    };
    int n = (int)(sizeof paleta / sizeof paleta[0]);
    unsigned c = paleta[i % n];
    if (i >= n) c ^= (unsigned)(0x203040 * (i / n)) & 0x7F7F7F;
    return c;
}

void config_padrao(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->algoritmo = sched_buscar(PADRAO_ALGORITMO);
    cfg->quantum = PADRAO_QUANTUM;
    cfg->ncpus = PADRAO_CPUS;
}

void config_liberar(Config *cfg) {
    for (int i = 0; i < cfg->n; i++) free(cfg->tarefas[i].eventos);
    free(cfg->tarefas);
    free(cfg->caminho);
    msg_liberar(&cfg->avisos);
    msg_liberar(&cfg->erros);
    memset(cfg, 0, sizeof *cfg);
}

void config_adicionar(Config *cfg, const TarefaCfg *t) {
    if (cfg->n == cfg->cap) {
        cfg->cap = cfg->cap ? cfg->cap * 2 : 8;
        cfg->tarefas = xrealloc(cfg->tarefas, (size_t)cfg->cap * sizeof *cfg->tarefas);
    }
    cfg->tarefas[cfg->n] = *t;
    cfg->tarefas[cfg->n].eventos = xstrdup(t->eventos ? t->eventos : "");
    cfg->n++;
}

const char *config_validar_quantum(int v) {
    return v > 0 ? NULL : "o quantum deve ser maior que zero";
}

const char *config_validar_cpus(int v) {
    static char buf[96];
    if (v >= 1 && v <= MAX_CPUS) return NULL;
    snprintf(buf, sizeof buf, "a quantidade de CPUs deve estar entre 1 e %d", MAX_CPUS);
    return buf;
}

/* ---------------------------------------------------------------- parser */

/* Contexto de uma linha, para montar mensagens "arquivo:linha: campo ...". */
typedef struct {
    Config *cfg;
    const char *arq;
    int linha;
} Ctx;

#define MAX_CAMPOS 8

/* Divide `s` (modificado in-place) em até `max` campos separados por ';'.
 * Se `resto` != NULL, o último campo recebe todo o restante da linha (usado
 * para lista_eventos, que pode ter qualquer formato no Projeto B). Um ';'
 * final não cria campo extra (req. 3.3.3). */
static int dividir(char *s, char **campos, int max, bool resto) {
    size_t len = strlen(s);
    if (len && s[len - 1] == ';') s[--len] = '\0';
    int n = 0;
    char *p = s;
    while (n < max) {
        campos[n++] = p;
        if (resto && n == max) break;
        char *q = strchr(p, ';');
        if (!q) break;
        *q = '\0';
        p = q + 1;
    }
    for (int i = 0; i < n; i++) str_trim(campos[i]);
    return n;
}

/* Lê um campo inteiro. Campo vazio usa `padrao` (se padrao_ok) com aviso.
 * Devolve false (e registra o erro) se inválido. */
static bool campo_int(Ctx *c, const char *nome, const char *txt, int *out,
                      bool padrao_ok, int padrao) {
    if (!*txt) {
        if (padrao_ok) {
            *out = padrao;
            msg_add(&c->cfg->avisos, "%s:%d: campo '%s' vazio; usando o valor padrão %d",
                    c->arq, c->linha, nome, padrao);
            return true;
        }
        msg_add(&c->cfg->erros, "%s:%d: campo '%s' vazio: é obrigatório", c->arq, c->linha, nome);
        return false;
    }
    int r = parse_int(txt, out);
    if (r == -1) {
        msg_add(&c->cfg->erros, "%s:%d: campo '%s' = '%s': não é um número inteiro",
                c->arq, c->linha, nome, txt);
        return false;
    }
    if (r == -2) {
        msg_add(&c->cfg->erros, "%s:%d: campo '%s' = '%s': número grande demais",
                c->arq, c->linha, nome, txt);
        return false;
    }
    return true;
}

static void erro_campo(Ctx *c, const char *nome, int v, const char *motivo) {
    msg_add(&c->cfg->erros, "%s:%d: campo '%s' = %d: %s", c->arq, c->linha, nome, v, motivo);
}

static void ler_linha_sistema(Ctx *c, char *s) {
    Config *cfg = c->cfg;
    char *f[MAX_CAMPOS];
    int n = dividir(s, f, MAX_CAMPOS, false);

    /* algoritmo */
    if (!*f[0]) {
        msg_add(&cfg->avisos, "%s:%d: campo 'algoritmo_escalonamento' vazio; usando o padrão %s",
                c->arq, c->linha, PADRAO_ALGORITMO);
        cfg->algoritmo = sched_buscar(PADRAO_ALGORITMO);
    } else {
        cfg->algoritmo = sched_buscar(f[0]);
        if (cfg->algoritmo < 0) {
            char nomes[256];
            sched_listar_nomes(nomes, sizeof nomes);
            msg_add(&cfg->erros,
                    "%s:%d: campo 'algoritmo_escalonamento' = '%s': algoritmo desconhecido "
                    "(disponíveis: %s)", c->arq, c->linha, f[0], nomes);
        }
    }

    /* quantum */
    if (n < 2) {
        msg_add(&cfg->avisos, "%s:%d: campo 'quantum' ausente; usando o padrão %d",
                c->arq, c->linha, PADRAO_QUANTUM);
        cfg->quantum = PADRAO_QUANTUM;
    } else if (campo_int(c, "quantum", f[1], &cfg->quantum, true, PADRAO_QUANTUM)) {
        const char *e = config_validar_quantum(cfg->quantum);
        if (e) erro_campo(c, "quantum", cfg->quantum, e);
    }

    /* qtde_cpus */
    if (n < 3) {
        msg_add(&cfg->avisos, "%s:%d: campo 'qtde_cpus' ausente; usando o padrão %d",
                c->arq, c->linha, PADRAO_CPUS);
        cfg->ncpus = PADRAO_CPUS;
    } else if (campo_int(c, "qtde_cpus", f[2], &cfg->ncpus, true, PADRAO_CPUS)) {
        const char *e = config_validar_cpus(cfg->ncpus);
        if (e) erro_campo(c, "qtde_cpus", cfg->ncpus, e);
    }

    if (n > 3)
        msg_add(&cfg->avisos, "%s:%d: a linha do sistema tem %d campos; os campos além do 3º "
                "foram ignorados", c->arq, c->linha, n);
}

/* `ids`/`linhas_id` guardam todos os ids vistos (inclusive de aperiódicas),
 * para detectar repetição. */
static void ler_linha_tarefa(Ctx *c, char *s, int **ids, int **linhas_id, int *nids, int *capids) {
    Config *cfg = c->cfg;
    char *f[7];
    int n = dividir(s, f, 7, true);
    if (n < 5) {
        msg_add(&cfg->erros, "%s:%d: a linha tem %d campo(s), mas uma tarefa precisa de "
                "id;cor;ingresso;duracao;periodo;prazo[;lista_eventos]", c->arq, c->linha, n);
        return;
    }

    TarefaCfg t = {0};
    t.linha = c->linha;
    bool ok = true;

    if (campo_int(c, "id", f[0], &t.id, false, 0)) {
        for (int i = 0; i < *nids; i++) {
            if ((*ids)[i] == t.id) {
                msg_add(&cfg->erros, "%s:%d: campo 'id' = %d: id repetido (já usado na linha %d)",
                        c->arq, c->linha, t.id, (*linhas_id)[i]);
                ok = false;
                break;
            }
        }
        if (*nids == *capids) {
            *capids = *capids ? *capids * 2 : 16;
            *ids = xrealloc(*ids, (size_t)*capids * sizeof **ids);
            *linhas_id = xrealloc(*linhas_id, (size_t)*capids * sizeof **linhas_id);
        }
        (*ids)[*nids] = t.id;
        (*linhas_id)[*nids] = c->linha;
        (*nids)++;
    } else {
        ok = false;
    }

    if (!*f[1]) {
        t.cor = config_cor_padrao(cfg->n);
        msg_add(&cfg->avisos, "%s:%d: campo 'cor' vazio; usando a cor padrão %06X",
                c->arq, c->linha, t.cor);
    } else if (!parse_cor(f[1], &t.cor)) {
        msg_add(&cfg->erros, "%s:%d: campo 'cor' = '%s': esperado RGB hexadecimal com 6 dígitos "
                "(ex.: F0E0D0)", c->arq, c->linha, f[1]);
        ok = false;
    }

    if (campo_int(c, "ingresso", f[2], &t.ingresso, true, PADRAO_INGRESSO)) {
        if (t.ingresso < 0) {
            erro_campo(c, "ingresso", t.ingresso, "o instante de ingresso não pode ser negativo");
            ok = false;
        }
    } else ok = false;

    if (campo_int(c, "duracao", f[3], &t.duracao, false, 0)) {
        if (t.duracao <= 0) {
            erro_campo(c, "duracao", t.duracao, "a duração deve ser maior que zero");
            ok = false;
        }
    } else ok = false;

    if (campo_int(c, "periodo", f[4], &t.periodo, false, 0)) {
        if (t.periodo < 0) {
            erro_campo(c, "periodo", t.periodo, "valor negativo não é permitido "
                       "(use > 0 para tarefa periódica ou 0 para aperiódica)");
            ok = false;
        }
    } else ok = false;

    if (n < 6) {
        t.prazo = t.periodo;
        msg_add(&cfg->avisos, "%s:%d: campo 'prazo' ausente; usando o período (%d) como prazo",
                c->arq, c->linha, t.periodo);
    } else if (!*f[5]) {
        t.prazo = t.periodo;
        msg_add(&cfg->avisos, "%s:%d: campo 'prazo' vazio; usando o período (%d) como prazo",
                c->arq, c->linha, t.periodo);
    } else if (campo_int(c, "prazo", f[5], &t.prazo, false, 0)) {
        if (t.prazo <= 0) {
            erro_campo(c, "prazo", t.prazo, "o prazo deve ser maior que zero");
            ok = false;
        }
    } else ok = false;

    t.eventos = n >= 7 ? f[6] : "";

    if (!ok) return;

    if (t.periodo == 0) {
        msg_add(&cfg->avisos, "%s:%d: tarefa %d é aperiódica (periodo = 0) e foi ignorada: "
                "o Projeto A só simula tarefas periódicas", c->arq, c->linha, t.id);
        return;
    }
    if (t.prazo < t.duracao)
        msg_add(&cfg->avisos, "%s:%d: tarefa %d tem prazo (%d) menor que a duração (%d): "
                "vai perder o prazo em toda ativação", c->arq, c->linha, t.id, t.prazo, t.duracao);
    for (int i = 0; i < cfg->n; i++)
        if (cfg->tarefas[i].cor == t.cor)
            msg_add(&cfg->avisos, "%s:%d: tarefa %d usa a mesma cor (%06X) da tarefa %d; "
                    "no Gantt elas ficarão iguais", c->arq, c->linha, t.id, t.cor,
                    cfg->tarefas[i].id);
    config_adicionar(cfg, &t);
}

/* Lê o arquivo inteiro para a memória. Devolve NULL e registra o erro. */
static char *ler_arquivo(Config *cfg, const char *caminho) {
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        msg_add(&cfg->erros, "não foi possível abrir '%s': %s", caminho, strerror(errno));
        return NULL;
    }
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 1 >= cap) buf = xrealloc(buf, cap *= 2);
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0) break;
    }
    if (ferror(f)) {
        /* Ex.: o caminho é um diretório (fopen funciona, a leitura não). */
        msg_add(&cfg->erros, "não foi possível ler '%s': %s", caminho,
                errno ? strerror(errno) : "erro de leitura (é um diretório?)");
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    if (strlen(buf) != n) {
        msg_add(&cfg->erros, "'%s' contém bytes nulos: não parece um arquivo de texto", caminho);
        free(buf);
        return NULL;
    }
    return buf;
}

bool config_carregar(const char *caminho, Config *cfg) {
    config_padrao(cfg);
    cfg->caminho = xstrdup(caminho);
    char *texto = ler_arquivo(cfg, caminho);
    if (!texto) return false;

    char *p = texto;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;  /* BOM UTF-8 */

    Ctx c = {cfg, caminho, 0};
    bool viu_sistema = false;
    int linhas_tarefa = 0;
    int *ids = NULL, *linhas_id = NULL, nids = 0, capids = 0;

    while (p) {
        char *fim = strchr(p, '\n');
        if (fim) *fim = '\0';
        c.linha++;
        str_trim(p);  /* também remove o \r de arquivos do Windows */
        if (*p && *p != '#') {
            if (!viu_sistema) {
                ler_linha_sistema(&c, p);
                viu_sistema = true;
            } else {
                ler_linha_tarefa(&c, p, &ids, &linhas_id, &nids, &capids);
                linhas_tarefa++;
            }
        }
        p = fim ? fim + 1 : NULL;
    }
    free(ids);
    free(linhas_id);
    free(texto);

    if (!viu_sistema) {
        msg_add(&cfg->erros, "%s: arquivo vazio; esperado na 1ª linha "
                "'algoritmo_escalonamento;quantum;qtde_cpus' e uma tarefa por linha a seguir",
                caminho);
    } else if (linhas_tarefa == 0) {
        msg_add(&cfg->erros, "%s: nenhuma tarefa encontrada; o arquivo precisa de pelo menos "
                "2 linhas (sistema + 1 tarefa no formato id;cor;ingresso;duracao;periodo;prazo)",
                caminho);
    } else if (cfg->erros.n == 0 && cfg->n == 0) {
        msg_add(&cfg->avisos, "%s: nenhuma tarefa periódica restou; não há o que simular no "
                "Projeto A (adicione tarefas na tela de configuração)", caminho);
    }
    return cfg->erros.n == 0;
}
