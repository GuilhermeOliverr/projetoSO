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
 *
 * Fluxo de config_carregar (lá no fim do arquivo):
 *   ler_arquivo -> para cada linha: ler_linha_sistema (1ª linha útil) ou
 *   ler_linha_tarefa (as demais) -> checagens finais.
 *
 * Duas famílias de mensagens, guardadas em listas (ListaMsg):
 *   - AVISOS: o arquivo é aceito, mas algo foi ajustado (ex.: campo vazio);
 *   - ERROS: o arquivo é rejeitado. Juntamos TODOS os erros antes de desistir,
 *     para o usuário corrigir tudo de uma vez.
 */
#include "config.h"
#include "sched.h"
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= 
    \* 1\. GERENCIAMENTO DE MENSAGENS (AVISOS E ERROS) \*
    ========================================================================= */

/* Acrescenta uma mensagem formatada (como printf) à lista. A lista cresce
 * dobrando de capacidade (8, 16, 32...). `...` + va_list é como C recebe um
 * número variável de argumentos; vsnprintf formata dentro de buf sem
 * estourar o tamanho. xstrdup guarda uma cópia, pois buf some ao sair. */
void msg_add(ListaMsg *l, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);    // Inicializa a lista de argumentos variáveis 'ap' com o formato 'fmt'
    vsnprintf(buf, sizeof buf, fmt, ap);    // Formata a string de acordo com 'fmt' e 'ap', salvando em 'buf'
    va_end(ap);           // Finaliza o processamento da lista de argumentos variáveis 'ap'
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;    // Dobra a capacidade do vetor
        l->itens = xrealloc(l->itens, (size_t)l->cap * sizeof *l->itens);    // redimensiona um bloco de memória alocado dinamicamente usando xrealloc()
    }
    l->itens[l->n++] = xstrdup(buf);    // Duplica e guarda a mensagem formatada
}

//Libera toda a memória dinâmica alocada para o vetor de mensagens e zera a estrutura ListaMsg.
void msg_liberar(ListaMsg *l) {
    for (int i = 0; i < l->n; i++) free(l->itens[i]);
    free(l->itens);
    l->itens = NULL;
    l->n = l->cap = 0;
}

//Imprime no terminal todas as mensagens acumuladas no parser: avisos em amarelo (0xF1C40F) e erros em vermelho (0xE74C3C).
void config_imprimir_mensagens(const Config *cfg) {
    for (int i = 0; i < cfg->avisos.n; i++) {
        term_fg(0xF1C40F);    // Define cor amarela para avisos
        printf("aviso: ");
        term_reset();        // Restaura a cor e os estilos padrão do terminal usando term_reset()
        printf("%s\n", cfg->avisos.itens[i]);
    }
    for (int i = 0; i < cfg->erros.n; i++) {
        term_fg(0xE74C3C);    // Define cor vermelha para erros
        printf("erro: ");
        term_reset();
        printf("%s\n", cfg->erros.itens[i]);
    }
}

/* ========================================================================= 
    \* 2\. CONFIGURAÇÃO INICIAL E GERENCIAMENTO DE TAREFAS \* 
    ========================================================================= */

//Retorna uma cor RGB no formato hexadecimal a partir de uma paleta padrão bem (separados, para que tarefas vizinhas no Gantt não se confundam) contrastada
//para a i-ésima tarefa (usada quando o arquivo não especifica uma cor).
unsigned config_cor_padrao(int i) {
    // Depois de 12 tarefas as cores se repetem com variação.
    static const unsigned paleta[] = {
        0xE74C3C, 0x3498DB, 0x2ECC71, 0xF39C12, 0x9B59B6, 0x1ABC9C,
        0xE67E22, 0x34495E, 0xF1C40F, 0xE84393, 0x16A085, 0x8E44AD,
    };
    int n = (int)(sizeof paleta / sizeof paleta[0]);
    unsigned c = paleta[i % n];
    /* A partir da 13ª tarefa mexemos nos bits da cor (XOR) para não repetir
     * exatamente; o & 0x7F7F7F limita a mudança para a cor não virar outra
     * completamente diferente. */
    if (i >= n) c ^= (unsigned)(0x203040 * (i / n)) & 0x7F7F7F;
    return c;
}

//Preenche a estrutura Config com os valores padrão sugeridos pelo sistema (RM como algoritmo, Quantum = 2, CPUs = 1) e sem nenhuma tarefa associada.
void config_padrao(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);    // Zera todos os bytes da estrutura Config usando memset() ( preencher um bloco de memória com um determinado valor constante byte a byte)
    cfg->algoritmo = sched_buscar(PADRAO_ALGORITMO);        // Busca o índice do algoritmo padrão ("RM") usando sched_buscar()
    cfg->quantum = PADRAO_QUANTUM;
    cfg->ncpus = PADRAO_CPUS;
}

//Libera toda a memória associada à estrutura Config, incluindo o vetor de tarefas, o caminho do arquivo e as listas de erros/avisos.
void config_liberar(Config *cfg) {
    for (int i = 0; i < cfg->n; i++) config_liberar_tarefa(&cfg->tarefas[i]);    // Libera a memória alocada para os eventos de cada tarefa usando config_liberar_tarefa()
    free(cfg->tarefas);
    free(cfg->caminho);
    msg_liberar(&cfg->avisos);    // Libera o vetor e textos da lista de avisos usando msg_liberar()
    msg_liberar(&cfg->erros);    // Libera o vetor e textos da lista de erros usando msg_liberar()
    memset(cfg, 0, sizeof *cfg);
}

//Adiciona uma nova tarefa ao vetor de tarefas da estrutura Config. Se o vetor estiver cheio, expande sua memória (dobrando a capacidade).
void config_adicionar(Config *cfg, const TarefaCfg *t) {
    if (cfg->n == cfg->cap) {
        cfg->cap = cfg->cap ? cfg->cap * 2 : 8;
        cfg->tarefas = xrealloc(cfg->tarefas, (size_t)cfg->cap * sizeof *cfg->tarefas);    // Dobra a capacidade do vetor dinâmico de tarefas usando xrealloc()
    }
    TarefaCfg *novo = &cfg->tarefas[cfg->n];
    *novo = *t;
    novo->eventos = NULL;
    novo->lista_ev = NULL;
    novo->nev = 0;
    config_definir_eventos(novo, t->eventos ? t->eventos : "");    // Copia e separa os eventos da tarefa de forma independente usando config_definir_eventos()
    cfg->n++;
}

//Libera a memória dinâmica alocada para uma tarefa específica (string de eventos e vetor de itens).
void config_liberar_tarefa(TarefaCfg *t) {
    for (int i = 0; i < t->nev; i++) free(t->lista_ev[i]);
    free(t->lista_ev);
    free(t->eventos);
    t->lista_ev = NULL;
    t->eventos = NULL;
    t->nev = 0;
}

//Pega a string de eventos da tarefa e a separa por vírgulas ou pontos e vírgulas, armazenando cada evento individualmente no vetor dinâmico `lista_ev`.
void config_definir_eventos(TarefaCfg *t, const char *texto) {
    char *raw = xstrdup(texto ? texto : "");    // Cria uma cópia dinâmica da string de eventos usando xstrdup()
    config_liberar_tarefa(t);    // Libera a lista de eventos anterior antes de sobrescrever usando config_liberar_tarefa()
    t->eventos = raw;
    /* Aceitamos ',' e ';' como separadores porque o formato dos eventos
     * não é definido; itens vazios (ex.: ";;") são ignorados. */
    char *copia = xstrdup(raw);    // Duplica a string de eventos usando xstrdup() para podermos modificá-la na separação
    int cap = 0;
    /* strpbrk acha o próximo separador; trocamos por '\0' para isolar o
     * item, que é limpo com str_trim e guardado se não for vazio. */
    for (char *p = copia; p;) {
        char *sep = strpbrk(p, ",;");
        if (sep) *sep = '\0';
        char *item = str_trim(p);
        if (*item) {
            if (t->nev == cap) {
                cap = cap ? cap * 2 : 4;
                t->lista_ev = xrealloc(t->lista_ev, (size_t)cap * sizeof *t->lista_ev);
            }
            t->lista_ev[t->nev++] = xstrdup(item);
        }
        p = sep ? sep + 1 : NULL;
    }
    free(copia);
}

/* ========================================================================= 
    \* 3\. VALIDADORES DE PARÂMETROS \* 
    ========================================================================= */

//Valida se o valor do quantum é maior que zero. Retorna NULL se for válido ou a mensagem de erro caso seja inválido.
const char *config_validar_quantum(int v) {
    return v > 0 ? NULL : "o quantum deve ser maior que zero";
}

//Valida se a quantidade de CPUs está entre 1 e o limite máximo (MAX\_CPUS = 64).
const char *config_validar_cpus(int v) {
    static char buf[96];   /* static: o texto precisa viver após o return */
    if (v >= 1 && v <= MAX_CPUS) return NULL;
    snprintf(buf, sizeof buf, "a quantidade de CPUs deve estar entre 1 e %d", MAX_CPUS);    // Formata a mensagem de erro no buffer estático usando snprintf() return buf;
    return buf;
}

/* =========================================================================
    \* 4\. PARSER DE LINHAS E CAMPOS DO ARQUIVO DE CONFIGURAÇÃO \*
========================================================================= */

/* Contexto de uma linha, para montar mensagens "arquivo:linha: campo ...". */
typedef struct {
    Config *cfg;
    const char *arq;
    int linha;
} Ctx;

#define MAX_CAMPOS 8

//Divide uma linha de texto em até `max` campos separados por ponto e vírgula (';'). Altera a string original inserindo '\0' nos delimitadores.
static int dividir(char *s, char **campos, int max, bool resto) {
    size_t len = strlen(s);
    if (len && s[len - 1] == ';') s[--len] = '\0';   /* ';' final opcional */
    int n = 0;
    char *p = s;
    while (n < max) {
        campos[n++] = p;
        if (resto && n == max) break;   /* o último campo leva o resto todo */
        char *q = strchr(p, ';');    // Localiza a posição do próximo ponto e vírgula na linha usando strchr()
        if (!q) break;
        *q = '\0';
        p = q + 1;
    }
    for (int i = 0; i < n; i++) str_trim(campos[i]);    // Remove espaços extras das pontas de cada campo usando str_trim()
    return n;
}

//Converte um texto de um campo em um número inteiro. Trata campos vazios usando valores padrão (se permitido) ou gerando erros apropriados.
static bool campo_int(Ctx *c, const char *nome, const char *txt, int *out,
                      bool padrao_ok, int padrao) {
    if (!*txt) {
        if (padrao_ok) {
            *out = padrao;
            msg_add(&c->cfg->avisos, "%s:%d: campo '%s' vazio; usando o valor padrão %d",    // Registra aviso de campo em branco usando msg_add()
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

//Registra um erro de validação para um campo cujo valor numérico está fora da faixa permitida.
static void erro_campo(Ctx *c, const char *nome, int v, const char *motivo) {
    msg_add(&c->cfg->erros, "%s:%d: campo '%s' = %d: %s", c->arq, c->linha, nome, v, motivo);
}

//Faz o parsing da 1ª linha útil do arquivo (configuração do sistema). Formato: `algoritmo_escalonamento; quantum; qtde_cpus´.
static void ler_linha_sistema(Ctx *c, char *s) {
    Config *cfg = c->cfg;
    char *f[MAX_CAMPOS];
    int n = dividir(s, f, MAX_CAMPOS, false);    // Divide a linha do sistema nos seus campos delimitados por ';' usando dividir()

    // 1\. Processa o algoritmo de escalonamento
    if (!*f[0]) {
        msg_add(&cfg->avisos, "%s:%d: campo 'algoritmo_escalonamento' vazio; usando o padrão %s",
                c->arq, c->linha, PADRAO_ALGORITMO);
        cfg->algoritmo = sched_buscar(PADRAO_ALGORITMO);    // Busca o índice do algoritmo padrão ("RM") usando sched_buscar()
    } else {
        cfg->algoritmo = sched_buscar(f[0]);
        if (cfg->algoritmo < 0) {
            char nomes[256];
            sched_listar_nomes(nomes, sizeof nomes);    // Obtém a lista textual dos algoritmos registrados usando sched_listar_nomes()
            msg_add(&cfg->erros,
                    "%s:%d: campo 'algoritmo_escalonamento' = '%s': algoritmo desconhecido "
                    "(disponíveis: %s)", c->arq, c->linha, f[0], nomes);
        }
    }

    // 2. Processa o quantum
    if (n < 2) {
        msg_add(&cfg->avisos, "%s:%d: campo 'quantum' ausente; usando o padrão %d",
                c->arq, c->linha, PADRAO_QUANTUM);
        cfg->quantum = PADRAO_QUANTUM;
    } else if (campo_int(c, "quantum", f[1], &cfg->quantum, true, PADRAO_QUANTUM)) {
        const char *e = config_validar_quantum(cfg->quantum);    // Valida se o quantum é positivo usando config_validar_quantum()
        if (e) erro_campo(c, "quantum", cfg->quantum, e);    // Registra o erro de validação do quantum usando erro_campo()
    }

    // 3. Processa a quantidade de CPUs
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

//Faz o parsing das linhas de definição de tarefas (a partir da 2ª linha útil). Formato: `id; cor; ingresso; duracao; periodo; prazo; lista_eventos`.
static void ler_linha_tarefa(Ctx *c, char *s, int **ids, int **linhas_id, int *nids, int *capids) {
    Config *cfg = c->cfg;
    char *f[7];
    int n = dividir(s, f, 7, true);    // Divide a linha da tarefa em até 7 campos usando dividir()
    if (n < 5) {
        msg_add(&cfg->erros, "%s:%d: a linha tem %d campo(s), mas uma tarefa precisa de "
                "id;cor;ingresso;duracao;periodo;prazo[;lista_eventos]", c->arq, c->linha, n);
        return;
    }

    TarefaCfg t = {0};
    t.linha = c->linha;
    bool ok = true;

    // 1. Validação do ID e checagem de repetição
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
            *ids = xrealloc(*ids, (size_t)*capids * sizeof **ids);    // Redimensiona o vetor de verificação de IDs usando xrealloc()
            *linhas_id = xrealloc(*linhas_id, (size_t)*capids * sizeof **linhas_id);    // Redimensiona o vetor de mapeamento de linhas usando xrealloc()
        }
        (*ids)[*nids] = t.id;
        (*linhas_id)[*nids] = c->linha;
        (*nids)++;
    } else {
        ok = false;
    }

    // 2. Validação da cor hexadecimal RGB
    if (!*f[1]) {
        t.cor = config_cor_padrao(cfg->n);
        msg_add(&cfg->avisos, "%s:%d: campo 'cor' vazio; usando a cor padrão %06X",
                c->arq, c->linha, t.cor);
    } else if (!parse_cor(f[1], &t.cor)) {    // Converte a string hexadecimal RGB para inteiro de 24 bits usando parse_cor()
        msg_add(&cfg->erros, "%s:%d: campo 'cor' = '%s': esperado RGB hexadecimal com 6 dígitos "
                "(ex.: F0E0D0)", c->arq, c->linha, f[1]);
        ok = false;
    }

    // 3. Validação do tempo de ingresso (>= 0)
    if (campo_int(c, "ingresso", f[2], &t.ingresso, true, PADRAO_INGRESSO)) {
        if (t.ingresso < 0) {
            erro_campo(c, "ingresso", t.ingresso, "o instante de ingresso não pode ser negativo");
            ok = false;
        }
    } else ok = false;

    // 4. Validação da duração (> 0)
    if (campo_int(c, "duracao", f[3], &t.duracao, false, 0)) {
        if (t.duracao <= 0) {
            erro_campo(c, "duracao", t.duracao, "a duração deve ser maior que zero");
            ok = false;
        }
    } else ok = false;

    // 5. Validação do período (>= 0; 0 indica aperiódica)
    if (campo_int(c, "periodo", f[4], &t.periodo, false, 0)) {
        if (t.periodo < 0) {
            erro_campo(c, "periodo", t.periodo, "valor negativo não é permitido "
                       "(use > 0 para tarefa periódica ou 0 para aperiódica)");
            ok = false;
        }
    } else ok = false;

    // 6. Validação do prazo (se omitido, assume prazo = período)
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

    if (!ok) return;   /* algum campo inválido: a tarefa não entra */

    // Descarte de tarefas aperiódicas
    if (t.periodo == 0) {
        msg_add(&cfg->avisos, "%s:%d: tarefa %d é aperiódica (periodo = 0) e foi ignorada: "
                "o Projeto A só simula tarefas periódicas", c->arq, c->linha, t.id);
        return;
    }
    /* Avisos que não impedem a simulação, mas ajudam o usuário. */
    if (t.prazo < t.duracao)
        msg_add(&cfg->avisos, "%s:%d: tarefa %d tem prazo (%d) menor que a duração (%d): "
                "vai perder o prazo em toda ativação", c->arq, c->linha, t.id, t.prazo, t.duracao);
    for (int i = 0; i < cfg->n; i++)
        if (cfg->tarefas[i].cor == t.cor)
            msg_add(&cfg->avisos, "%s:%d: tarefa %d usa a mesma cor (%06X) da tarefa %d; "
                    "no Gantt elas ficarão iguais", c->arq, c->linha, t.id, t.cor,
                    cfg->tarefas[i].id);
    config_adicionar(cfg, &t);
    
     /* a simulação; o aviso mostra que o campo foi lido (e não ignorado por
     * engano). */
    const TarefaCfg *nova = &cfg->tarefas[cfg->n - 1];
    if (nova->nev > 0)
        msg_add(&cfg->avisos, "%s:%d: tarefa %d tem %d evento(s) em lista_eventos (%.60s%s): "
                "lidos e guardados", c->arq, c->linha, t.id, nova->nev, nova->eventos,
                strlen(nova->eventos) > 60 ? "..." : "");
}

/* ========================================================================= 
    \* 5\. LEITURA DE ARQUIVO E ENTRADA PRINCIPAL DO PARSER \*
    ========================================================================= */

//Abre o arquivo em modo binário ("rb"), lê todo o seu conteúdo para um buffer em memória RAM dinamicamente redimensionado e insere o caractere nulo '\0' no final.
static char *ler_arquivo(Config *cfg, const char *caminho) {
    FILE *f = fopen(caminho, "rb");    // Abre o arquivo de configuração para leitura em modo binário usando fopen()
    if (!f) {
        msg_add(&cfg->erros, "não foi possível abrir '%s': %s", caminho, strerror(errno));
        return NULL;
    }
    /* Buffer que dobra conforme o arquivo é lido; o `- 1` reserva espaço
     * para o '\0' final. */
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);
    for (;;) {
        if (n + 1 >= cap) buf = xrealloc(buf, cap *= 2);
        size_t r = fread(buf + n, 1, cap - n - 1, f);    // Lê um bloco de bytes do arquivo diretamente para o buffer usando fread()
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
    /* Um '\0' no meio indica arquivo binário (ex.: alguém passou um .exe). */
    if (strlen(buf) != n) {
        msg_add(&cfg->erros, "'%s' contém bytes nulos: não parece um arquivo de texto", caminho);
        free(buf);
        return NULL;
    }
    return buf;
}

/*É a função principal do módulo. Carrega o arquivo de configuração, descarta 
\* a marcação BOM de arquivos UTF-8 do Windows, percorre o arquivo linha por linha, 
\* chama o parser da linha do sistema e das tarefas e verifica os erros fatais. 
\* Retorna true se o arquivo foi carregado sem erros fatais. */
bool config_carregar(const char *caminho, Config *cfg) {
    config_padrao(cfg);
    cfg->caminho = xstrdup(caminho);
    char *texto = ler_arquivo(cfg, caminho);
    if (!texto) return false;

    char *p = texto;
    /* BOM UTF-8: 3 bytes (EF BB BF) que alguns editores do Windows colocam
     * no começo do arquivo; se não os pulássemos, o 1º campo ficaria errado. */
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;  /* BOM UTF-8 */

    Ctx c = {cfg, caminho, 0};
    bool viu_sistema = false;     /* a 1ª linha útil é a do sistema         */
    int linhas_tarefa = 0;
    int *ids = NULL, *linhas_id = NULL, nids = 0, capids = 0;

    while (p) {
        char *fim = strchr(p, '\n');
        if (fim) *fim = '\0';        /* isola a linha trocando '\n' por '\0' */
        c.linha++;
        str_trim(p);  /* também remove o \r de arquivos do Windows */
        if (*p && *p != '#') {       /* ignora linha vazia e comentário      */
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

    /* Checagens do arquivo como um todo (req. 3.3.1: pelo menos 2 linhas). */
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
