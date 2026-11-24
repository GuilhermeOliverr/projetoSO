/*
 * config.c - leitura do arquivo de configuração e funções auxiliares.
 *
 * Junta, nesta ordem, src/core/config.c e src/core/util.c da versão
 * organizada em pastas. Cada trecho começa com um divisor e com o
 * comentário de cabeçalho original. Os #include dos dois trechos ficam
 * juntos logo abaixo; _POSIX_C_SOURCE e _DARWIN_C_SOURCE (do trecho
 * util.c) precisam vir antes de qualquer #include.
 */
/* Precisamos de isatty/fileno (POSIX) para saber se a saída é um terminal. */
#define _POSIX_C_SOURCE 200809L
/* No macOS, TIOCGWINSZ some com _POSIX_C_SOURCE se não pedirmos as
 * extensões do Darwin. */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "config.h"
#include "sched.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* ===========================================================================
 * Seção config.c (originalmente src/core/config.c)
 * =========================================================================== */
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
 *  - linha que começa com '#' e NÃO tem nenhum ';' é comentário (extensão
 *    nossa; o enunciado não tem comentários). Como o id é texto, "#1" é um
 *    id válido: "#1;FF0000;0;2;10;10" é uma tarefa e não pode sumir sem
 *    aviso. Nenhuma linha útil se perde com a regra: uma tarefa tem pelo
 *    menos 5 campos (sempre tem ';') e um sistema "#RM" seria um algoritmo
 *    desconhecido de qualquer jeito. Vale também para a 1ª linha;
 *  - o id é texto (até 31 bytes, ver id_validar); ids que são inteiros
 *    continuam valendo pelo número ("01" é o mesmo id que "1");
 *  - campo vazio recebe o valor padrão, com aviso (req. 3.2);
 *  - o período diz o tipo da tarefa: > 0 periódica, 0 aperiódica e < 0
 *    esporádica (req. B 1.2 e 1.3); o id BSxx, PSxx, DSxx ou SSxx indica um
 *    servidor (req. B 2). Se cada tarefa entra no sistema quem decide é o
 *    teste de admissão, durante a simulação (admissao.c).
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

/* =========================================================================
 * 1. GERENCIAMENTO DE MENSAGENS (AVISOS E ERROS)
 * ========================================================================= */

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
 * 2. CONFIGURAÇÃO INICIAL E GERENCIAMENTO DE TAREFAS
 * ========================================================================= */

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
    novo->ocorr = NULL;
    novo->nocorr = 0;
    config_classificar(novo);   /* o tipo sempre sai do id e do período, venha a tarefa do arquivo ou da interface */
    config_definir_eventos(novo, t->eventos ? t->eventos : "");    // Copia e separa os eventos (e as ocorrências) da tarefa de forma independente usando config_definir_eventos()
    cfg->n++;
}

//Libera a memória dinâmica alocada para uma tarefa específica (string de eventos, vetor de itens e ocorrências).
void config_liberar_tarefa(TarefaCfg *t) {
    for (int i = 0; i < t->nev; i++) free(t->lista_ev[i]);
    free(t->lista_ev);
    free(t->eventos);
    free(t->ocorr);
    t->lista_ev = NULL;
    t->eventos = NULL;
    t->nev = 0;
    t->ocorr = NULL;
    t->nocorr = 0;
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
    /* Numa aperiódica ou esporádica a lista é o que diz quando ela chega
     * (req. B 1.3). Quem chama já validou o texto (config_ocorrencias); se
     * mesmo assim não servir, fica a ocorrência única no ingresso, para o
     * TCB nunca apontar para uma lista vazia. */
    if (t->tipo == TIPO_APERIODICA || t->tipo == TIPO_ESPORADICA) {
        if (config_ocorrencias(raw, t->tipo, t->periodo, t->ingresso, &t->ocorr, &t->nocorr)) {
            t->ocorr = xcalloc(1, sizeof *t->ocorr);
            t->nocorr = 1;
        }
    }
}

/* Servidor = BS, PS, DS ou SS seguido de um número (req. B 2: "BSxx", onde
 * xx diferencia os servidores do mesmo tipo). Sem diferenciar maiúsculas, como o resto do
 * arquivo: "bs1" também é servidor. "BS" sozinho ou "BSA" não são: o id
 * fica sendo o de uma tarefa comum. */
bool config_id_servidor(const char *id, TipoServidor *tipo) {
    static const struct { const char *prefixo; TipoServidor tipo; } servidores[] = {
        {"BS", SERV_BS},
        {"PS", SERV_PS},
        {"DS", SERV_DS},
        {"SS", SERV_SS},
    };
    for (size_t i = 0; i < sizeof servidores / sizeof servidores[0]; i++) {
        const char *p = servidores[i].prefixo;
        if (toupper((unsigned char)id[0]) != p[0] || toupper((unsigned char)id[1]) != p[1]) continue;
        const char *num = id + 2;
        if (!*num) return false;
        for (const char *q = num; *q; q++)
            if (!isdigit((unsigned char)*q)) return false;
        if (tipo) *tipo = servidores[i].tipo;
        return true;
    }
    return false;
}

/* O id decide se é servidor; o sinal do período decide o resto: 0 é
 * aperiódica (req. B 1.2) e negativo é esporádica (req. B 1.3). */
void config_classificar(TarefaCfg *t) {
    t->servidor = SERV_BS;
    if (config_id_servidor(t->id, &t->servidor)) t->tipo = TIPO_SERVIDOR;
    else if (t->periodo == 0) t->tipo = TIPO_APERIODICA;
    else if (t->periodo < 0) t->tipo = TIPO_ESPORADICA;
    else t->tipo = TIPO_PERIODICA;
}

/* Os instantes vêm separados por ',' (ou ';', como os eventos em geral), e
 * são relativos ao ingresso: numa tarefa criada no tempo 10, o 0 chega em 10
 * e o 5 chega em 15 (req. B 1.3). A esporádica tem de respeitar o intervalo
 * mínimo |periodo| entre duas ocorrências seguidas; na aperiódica só a ordem
 * é exigida (duas podem chegar juntas). O motivo fica num buffer estático,
 * como nos outros validadores. */
const char *config_ocorrencias(const char *eventos, TipoTarefa tipo, int periodo, int ingresso, int **out, int *n) {
    static char buf[200];
    int *v = NULL, nv = 0, cap = 0;
    const char *motivo = NULL;
    char *copia = xstrdup(eventos ? eventos : "");
    for (char *p = copia; p && !motivo;) {
        char *sep = strpbrk(p, ",;");
        if (sep) *sep = '\0';
        char *item = str_trim(p);
        p = sep ? sep + 1 : NULL;
        if (!*item) continue;
        int x;
        if (parse_int(item, &x) != 0 || x < 0) {
            snprintf(buf, sizeof buf, "o instante '%.40s' não é um número inteiro >= 0", item);
            motivo = buf;
        } else if (x > INT_MAX - (ingresso > 0 ? ingresso : 0)) {
            snprintf(buf, sizeof buf, "o instante %d somado ao ingresso (%d) é grande demais", x, ingresso);
            motivo = buf;
        } else if (nv > 0 && x < v[nv - 1]) {
            snprintf(buf, sizeof buf, "os instantes precisam estar em ordem crescente (%d vem depois de %d)", x, v[nv - 1]);
            motivo = buf;
        } else if (nv > 0 && tipo == TIPO_ESPORADICA && x - v[nv - 1] < -periodo) {
            snprintf(buf, sizeof buf, "as ocorrências %d e %d estão a %d tick(s) uma da outra, menos que o "
                     "intervalo mínimo da esporádica (|periodo| = %d)", v[nv - 1], x, x - v[nv - 1], -periodo);
            motivo = buf;
        } else {
            if (nv == cap) {
                cap = cap ? cap * 2 : 8;
                v = xrealloc(v, (size_t)cap * sizeof *v);
            }
            v[nv++] = x;
        }
    }
    free(copia);
    if (!motivo && nv == 0) {   /* sem lista: uma ocorrência, no próprio ingresso */
        v = xrealloc(v, sizeof *v);
        v[nv++] = 0;
    }
    if (motivo || !out) {
        free(v);
        v = NULL;
        nv = 0;
    }
    if (out) *out = v;
    if (n) *n = nv;
    return motivo;
}

/* =========================================================================
 * 3. VALIDADORES DE PARÂMETROS
 * ========================================================================= */

//Valida se o valor do quantum é maior que zero. Retorna NULL se for válido ou a mensagem de erro caso seja inválido.
const char *config_validar_quantum(int v) {
    return v > 0 ? NULL : "o quantum deve ser maior que zero";
}

//Valida se a quantidade de CPUs está entre 1 e o limite máximo (MAX_CPUS = 64).
const char *config_validar_cpus(int v) {
    static char buf[96];   /* static: o texto precisa viver após o return */
    if (v >= 1 && v <= MAX_CPUS) return NULL;
    snprintf(buf, sizeof buf, "a quantidade de CPUs deve estar entre 1 e %d", MAX_CPUS);    // Formata a mensagem de erro no buffer estático usando snprintf() return buf;
    return buf;
}

/* =========================================================================
 * 4. PARSER DE LINHAS E CAMPOS DO ARQUIVO DE CONFIGURAÇÃO
 * ========================================================================= */

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

/* Pasta de um caminho ("/Volumes/PENDRIVE/x.txt" -> "/Volumes/PENDRIVE",
 * "x.txt" -> "."). Aceita '/' e '\\' (Linux, macOS e Windows). Também é
 * usada pela janela, para abrir o navegador na pasta da configuração. */
void caminho_pasta(const char *caminho, char *dst, size_t tam) {
    const char *sep = NULL;
    for (const char *q = caminho; *q; q++)
        if (*q == '/' || *q == '\\') sep = q;
    if (!sep) snprintf(dst, tam, ".");
    else if (sep == caminho) snprintf(dst, tam, "%c", *sep);                        /* "/x.txt" -> "/"   */
    else if (sep[-1] == ':') snprintf(dst, tam, "%.*s", (int)(sep - caminho) + 1, caminho);  /* "E:\\x" -> "E:\\" */
    else snprintf(dst, tam, "%.*s", (int)(sep - caminho), caminho);
}

/* Carga automática do escalonador (req. 4.2, cenário do pendrive): o
 * algoritmo pedido na 1ª linha não está registrado, então procuramos NA
 * PASTA DO ARQUIVO DE CONFIGURAÇÃO um arquivo com o mesmo nome e extensão
 * .txt, .so ou .dylib, nessa ordem de preferência (o .txt funciona em
 * qualquer sistema). O nome é comparado com str_ieq, listando a pasta, e não
 * com fopen("LLF.txt"): no Linux e no macOS o sistema de arquivos pode
 * diferenciar maiúsculas, e "llf;2;1" precisa achar "Llf.TXT" (req. 3.3.2).
 * Arquivos ocultos (começando com '.') ficam de fora, inclusive os "._*" que
 * o macOS cria em pendrive FAT, e o próprio arquivo de configuração também.
 * Devolve o índice do escalonador; -1 se a pasta não tem nenhum arquivo com
 * esse nome; -2 se tem, mas ele não carregou (os erros já foram registrados). */
static int escalonador_da_pasta(Ctx *c, const char *nome) {
    static const char *const ext[] = {".txt", ".so", ".dylib"};   /* ordem de preferência */
    char pasta[1024], achado[1400] = "";
    int melhor = 3;   /* posição em `ext` do melhor achado (3 = nenhum) */
    caminho_pasta(c->arq, pasta, sizeof pasta);
    DIR *d = opendir(pasta);
    if (!d) return -1;
    struct stat st_cfg, st;
    bool tem_cfg = stat(c->arq, &st_cfg) == 0;
    for (struct dirent *e; (e = readdir(d));) {
        const char *n = e->d_name, *ponto = strrchr(n, '.');
        if (n[0] == '.' || !ponto || (size_t)(ponto - n) != strlen(nome)) continue;
        char base[256];
        snprintf(base, sizeof base, "%.*s", (int)(ponto - n), n);
        if (!str_ieq(base, nome)) continue;
        for (int r = 0; r < melhor; r++) {
            if (!str_ieq(ponto, ext[r])) continue;
            char cam[1400];
            snprintf(cam, sizeof cam, "%s%s%s", pasta, strchr("/\\", pasta[strlen(pasta) - 1]) ? "" : "/", n);
            if (stat(cam, &st) != 0 || !S_ISREG(st.st_mode)) break;
            if (tem_cfg && st.st_dev == st_cfg.st_dev && st.st_ino == st_cfg.st_ino) break;   /* é o próprio arquivo de configuração */
            snprintf(achado, sizeof achado, "%s", cam);
            melhor = r;
            break;
        }
    }
    closedir(d);
    if (!*achado) return -1;

    char erro[4096];
    int idx = sched_carregar_arquivo_esperado(achado, nome, erro, sizeof erro);
    if (idx >= 0) {
        msg_add(&c->cfg->avisos, "%s:%d: escalonador %s carregado automaticamente de %s", c->arq, c->linha,
                sched_obter(idx)->nome, achado);
        return idx;
    }
    msg_add(&c->cfg->erros, "%s:%d: campo 'algoritmo_escalonamento' = '%s': o escalonador foi achado em %s, "
            "mas não pôde ser carregado:", c->arq, c->linha, nome, achado);
    /* sched_carregar_arquivo junta os problemas um por linha; cada um vira um erro da lista */
    for (char *lin = erro, *fim; lin && *lin; lin = fim ? fim + 1 : NULL) {
        fim = strchr(lin, '\n');
        if (fim) *fim = '\0';
        msg_add(&c->cfg->erros, "%s", lin);
    }
    return -2;
}

//Faz o parsing da 1ª linha útil do arquivo (configuração do sistema). Formato: `algoritmo_escalonamento; quantum; qtde_cpus´.
static void ler_linha_sistema(Ctx *c, char *s) {
    Config *cfg = c->cfg;
    char *f[MAX_CAMPOS];
    int n = dividir(s, f, MAX_CAMPOS, false);    // Divide a linha do sistema nos seus campos delimitados por ';' usando dividir()

    // 1. Processa o algoritmo de escalonamento
    if (!*f[0]) {
        msg_add(&cfg->avisos, "%s:%d: campo 'algoritmo_escalonamento' vazio; usando o padrão %s",
                c->arq, c->linha, PADRAO_ALGORITMO);
        cfg->algoritmo = sched_buscar(PADRAO_ALGORITMO);    // Busca o índice do algoritmo padrão ("RM") usando sched_buscar()
    } else {
        cfg->algoritmo = sched_buscar(f[0]);
        /* Não registrado: tenta o arquivo de mesmo nome na pasta (pendrive). */
        if (cfg->algoritmo < 0) cfg->algoritmo = escalonador_da_pasta(c, f[0]);
        if (cfg->algoritmo == -1) {   /* -2: achou o arquivo, mas ele tinha erro (já registrado) */
            char nomes[256];
            sched_listar_nomes(nomes, sizeof nomes);    // Obtém a lista textual dos algoritmos registrados usando sched_listar_nomes()
            msg_add(&cfg->erros,
                    "%s:%d: campo 'algoritmo_escalonamento' = '%s': algoritmo desconhecido "
                    "(disponíveis: %s); coloque %s.txt na mesma pasta do arquivo de configuração ou "
                    "carregue-o pelo botão \"Carregar escalonador...\" (janela), pelo comando p (terminal) "
                    "ou com --plugin", c->arq, c->linha, f[0], nomes, f[0]);
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

/* Ids já vistos no arquivo, para achar repetição (inclusive de tarefas
 * aperiódicas, que não entram na configuração). Os ponteiros apontam para o
 * campo dentro do texto do arquivo, que só é liberado no fim de
 * config_carregar: não precisa copiar nem liberar cada id. */
typedef struct {
    const char **id;    // texto de cada id, como está no arquivo
    int *linha;         // linha em que cada id apareceu
    int n, cap;
} IdsVistos;

/* Cópia do id para mostrar numa mensagem de erro: caractere de controle
 * vira '?' (não bagunça o terminal) e um id comprido é cortado com "...". */
static void id_para_msg(const char *s, char *dst, size_t tam) {
    size_t j = 0;
    for (size_t i = 0; s[i] && j + 4 < tam; i++) {
        unsigned char ch = (unsigned char)s[i];
        dst[j++] = (ch < 0x20 || ch == 0x7F) ? '?' : (char)ch;
    }
    if (strlen(s) > j) {
        while (j > 0 && ((unsigned char)dst[j - 1] & 0xC0) == 0x80) j--;   /* não corta um caractere UTF-8 ao meio */
        if (j > 0 && (unsigned char)dst[j - 1] >= 0xC0) j--;
        memcpy(dst + j, "...", 3);
        j += 3;
    }
    dst[j] = '\0';
}

//Faz o parsing das linhas de definição de tarefas (a partir da 2ª linha útil). Formato: `id; cor; ingresso; duracao; periodo; prazo; lista_eventos`.
static void ler_linha_tarefa(Ctx *c, char *s, IdsVistos *vistos) {
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

    // 1. Validação do ID (texto) e checagem de repetição
    const char *motivo = id_validar(f[0]);
    if (!*f[0]) {
        /* mesma mensagem dos outros campos obrigatórios vazios */
        msg_add(&cfg->erros, "%s:%d: campo 'id' vazio: é obrigatório", c->arq, c->linha);
        ok = false;
    } else if (motivo) {
        char mostra[48];
        id_para_msg(f[0], mostra, sizeof mostra);
        msg_add(&cfg->erros, "%s:%d: campo 'id' = '%s': %s", c->arq, c->linha, mostra, motivo);
        ok = false;
    } else {
        /* Repetido = id_iguais, e não strcmp: "t1" repete "T1" (req. 3.3.2,
         * sem diferenciar maiúsculas) e "01" repete "1" (como quando o id
         * era int). A mensagem diz com qual id da outra linha ele bate. */
        for (int i = 0; i < vistos->n; i++) {
            if (!id_iguais(vistos->id[i], f[0])) continue;
            if (!strcmp(vistos->id[i], f[0]))
                msg_add(&cfg->erros, "%s:%d: campo 'id' = '%s': id repetido (já usado na linha %d)",
                        c->arq, c->linha, f[0], vistos->linha[i]);
            else
                msg_add(&cfg->erros, "%s:%d: campo 'id' = '%s': id repetido: é o mesmo id que '%s', "
                        "já usado na linha %d (ids não diferenciam maiúsculas de minúsculas, e "
                        "números valem pelo valor: 01 = 1)", c->arq, c->linha, f[0], vistos->id[i],
                        vistos->linha[i]);
            ok = false;
            break;
        }
        if (vistos->n == vistos->cap) {
            vistos->cap = vistos->cap ? vistos->cap * 2 : 16;
            vistos->id = xrealloc(vistos->id, (size_t)vistos->cap * sizeof *vistos->id);    // Redimensiona o vetor de verificação de IDs usando xrealloc()
            vistos->linha = xrealloc(vistos->linha, (size_t)vistos->cap * sizeof *vistos->linha);    // Redimensiona o vetor de mapeamento de linhas usando xrealloc()
        }
        vistos->id[vistos->n] = f[0];
        vistos->linha[vistos->n] = c->linha;
        vistos->n++;
        id_normalizar(t.id, sizeof t.id, f[0]);
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

    // 5. Validação do período: > 0 periódica, 0 aperiódica (req. B 1.2), < 0 esporádica (req. B 1.3).
    //    O tipo sai daqui e do id (config_classificar); um servidor é periódico, então precisa de período > 0.
    bool periodo_ok = campo_int(c, "periodo", f[4], &t.periodo, false, 0);
    if (!periodo_ok) ok = false;
    config_classificar(&t);
    if (periodo_ok && t.periodo == INT_MIN) {   /* |periodo| não caberia num int */
        msg_add(&cfg->erros, "%s:%d: campo 'periodo' = '%s': número grande demais", c->arq, c->linha, f[4]);
        ok = false;
    } else if (periodo_ok && t.tipo == TIPO_SERVIDOR && t.periodo <= 0) {
        erro_campo(c, "periodo", t.periodo, "um servidor é uma tarefa periódica: o período deve ser maior que zero");
        ok = false;
    }

    // 6. Validação do prazo. Se omitido: o período (periódica e servidor), o intervalo mínimo (esporádica) ou
    //    nenhum (aperiódica: prazo 0 = sem prazo, ela nunca perde prazo).
    int prazo_padrao = t.tipo == TIPO_ESPORADICA ? -t.periodo : t.tipo == TIPO_APERIODICA ? 0 : t.periodo;
    const char *nome_padrao = t.tipo == TIPO_ESPORADICA ? "o intervalo mínimo |periodo|" : "o período";
    if (n < 6 || !*f[5]) {
        t.prazo = prazo_padrao;
        if (t.tipo == TIPO_APERIODICA)
            msg_add(&cfg->avisos, "%s:%d: campo 'prazo' %s; a aperiódica %s fica sem prazo",
                    c->arq, c->linha, n < 6 ? "ausente" : "vazio", t.id);
        else
            msg_add(&cfg->avisos, "%s:%d: campo 'prazo' %s; usando %s (%d) como prazo",
                    c->arq, c->linha, n < 6 ? "ausente" : "vazio", nome_padrao, prazo_padrao);
    } else if (campo_int(c, "prazo", f[5], &t.prazo, false, 0)) {
        if (t.tipo == TIPO_APERIODICA && t.prazo < 0) {
            erro_campo(c, "prazo", t.prazo, "o prazo não pode ser negativo (0 = aperiódica sem prazo)");
            ok = false;
        } else if (t.tipo != TIPO_APERIODICA && t.prazo <= 0) {
            erro_campo(c, "prazo", t.prazo, "o prazo deve ser maior que zero");
            ok = false;
        }
    } else ok = false;

    t.eventos = n >= 7 ? f[6] : "";

    if (!ok) return;   /* algum campo inválido: a tarefa não entra */

    // 7. Ocorrências da aperiódica/esporádica (lista_eventos), com o intervalo mínimo da esporádica (req. B 1.3)
    if (t.tipo == TIPO_APERIODICA || t.tipo == TIPO_ESPORADICA) {
        const char *m = config_ocorrencias(t.eventos, t.tipo, t.periodo, t.ingresso, NULL, NULL);
        if (m) {
            msg_add(&cfg->erros, "%s:%d: campo 'lista_eventos' da %s %s: %s", c->arq, c->linha,
                    t.tipo == TIPO_ESPORADICA ? "esporádica" : "aperiódica", t.id, m);
            return;
        }
    }
    if (t.tipo == TIPO_SERVIDOR && t.servidor == SERV_BS)
        msg_add(&cfg->avisos, "%s:%d: o servidor %s é um Background Server: executa as aperiódicas só quando "
                "uma CPU ficaria desligada, sem limite de capacidade (a duração e o período não o limitam)",
                c->arq, c->linha, t.id);
    /* Avisos que não impedem a simulação, mas ajudam o usuário. */
    if (t.prazo > 0 && t.prazo < t.duracao)
        msg_add(&cfg->avisos, "%s:%d: tarefa %s tem prazo (%d) menor que a duração (%d): "
                "vai perder o prazo em toda ativação", c->arq, c->linha, t.id, t.prazo, t.duracao);
    for (int i = 0; i < cfg->n; i++)
        if (cfg->tarefas[i].cor == t.cor)
            msg_add(&cfg->avisos, "%s:%d: tarefa %s usa a mesma cor (%06X) da tarefa %s; "
                    "no Gantt elas ficarão iguais", c->arq, c->linha, t.id, t.cor,
                    cfg->tarefas[i].id);
    config_adicionar(cfg, &t);

    /* Numa periódica (ou num servidor) a lista_eventos não muda a simulação;
     * o aviso mostra que o campo foi lido (e não ignorado por engano). */
    const TarefaCfg *nova = &cfg->tarefas[cfg->n - 1];
    if (nova->nev > 0 && (nova->tipo == TIPO_PERIODICA || nova->tipo == TIPO_SERVIDOR))
        msg_add(&cfg->avisos, "%s:%d: tarefa %s tem %d evento(s) em lista_eventos (%.60s%s): "
                "lidos e guardados", c->arq, c->linha, t.id, nova->nev, nova->eventos,
                strlen(nova->eventos) > 60 ? "..." : "");
}

/* =========================================================================
 * 5. LEITURA DE ARQUIVO E ENTRADA PRINCIPAL DO PARSER
 * ========================================================================= */

//Abre o arquivo em modo binário ("rb"), lê todo o seu conteúdo para um buffer em memória RAM dinamicamente redimensionado e insere o caractere nulo '\0' no final.
//Os problemas vão para `erros` (a lista de erros do arquivo de configuração ou do arquivo de escalonador).
static char *ler_arquivo(ListaMsg *erros, const char *caminho) {
    FILE *f = fopen(caminho, "rb");    // Abre o arquivo de configuração para leitura em modo binário usando fopen()
    if (!f) {
        msg_add(erros, "não foi possível abrir '%s': %s", caminho, strerror(errno));
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
        msg_add(erros, "não foi possível ler '%s': %s", caminho,
                errno ? strerror(errno) : "erro de leitura (é um diretório?)");
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    /* Um '\0' no meio indica arquivo binário (ex.: alguém passou um .exe). */
    if (strlen(buf) != n) {
        msg_add(erros, "'%s' contém bytes nulos: não parece um arquivo de texto", caminho);
        free(buf);
        return NULL;
    }
    return buf;
}

//Lê o arquivo inteiro e prepara a leitura linha a linha. O BOM UTF-8 (3 bytes, EF BB BF, que alguns
//editores do Windows colocam no começo do arquivo) é pulado aqui: sem isso, o 1º campo ficaria errado.
bool leitor_abrir(LeitorLinhas *l, const char *caminho, ListaMsg *erros) {
    l->texto = ler_arquivo(erros, caminho);
    l->p = l->texto;
    l->linha = 0;
    if (!l->texto) return false;
    if ((unsigned char)l->p[0] == 0xEF && (unsigned char)l->p[1] == 0xBB && (unsigned char)l->p[2] == 0xBF)
        l->p += 3;  /* BOM UTF-8 */
    return true;
}

//Devolve a próxima linha (isolada trocando o '\n' por '\0' no próprio texto) já sem espaços nas pontas,
//o que também tira o \r de arquivos do Windows. NULL quando o arquivo acabou.
char *leitor_proxima(LeitorLinhas *l) {
    char *s = l->p;
    if (!s) return NULL;
    char *fim = strchr(s, '\n');
    if (fim) *fim = '\0';        /* isola a linha trocando '\n' por '\0' */
    l->linha++;
    l->p = fim ? fim + 1 : NULL;
    return str_trim(s);
}

void leitor_fechar(LeitorLinhas *l) {
    free(l->texto);
    l->texto = l->p = NULL;
}

/*É a função principal do módulo. Carrega o arquivo de configuração, descarta 
 * a marcação BOM de arquivos UTF-8 do Windows, percorre o arquivo linha por linha, 
 * chama o parser da linha do sistema e das tarefas e verifica os erros fatais. 
 * Retorna true se o arquivo foi carregado sem erros fatais. */
bool config_carregar(const char *caminho, Config *cfg) {
    config_padrao(cfg);
    cfg->caminho = xstrdup(caminho);
    LeitorLinhas l;
    if (!leitor_abrir(&l, caminho, &cfg->erros)) return false;   /* também pula o BOM UTF-8 */

    Ctx c = {cfg, caminho, 0};
    bool viu_sistema = false;     /* a 1ª linha útil é a do sistema         */
    int linhas_tarefa = 0;
    IdsVistos vistos = {NULL, NULL, 0, 0};

    for (char *p; (p = leitor_proxima(&l));) {   /* sem \n, \r e espaços nas pontas */
        c.linha = l.linha;
        /* Comentário = começa com '#' e não tem nenhum ';' (ver o topo do
         * arquivo): "#1;FF0000;..." é a tarefa de id "#1". */
        bool comentario = *p == '#' && !strchr(p, ';');
        if (*p && !comentario) {     /* ignora linha vazia e comentário      */
            if (!viu_sistema) {
                ler_linha_sistema(&c, p);
                viu_sistema = true;
            } else {
                ler_linha_tarefa(&c, p, &vistos);
                linhas_tarefa++;
            }
        }
    }
    /* `vistos.id` aponta para dentro do texto do leitor: libera o vetor antes. */
    free(vistos.id);
    free(vistos.linha);
    leitor_fechar(&l);

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
        msg_add(&cfg->avisos, "%s: nenhuma tarefa restou; não há o que simular "
                "(adicione tarefas na tela de configuração)", caminho);
    }
    return cfg->erros.n == 0;
}

/* ===========================================================================
 * Seção util.c (originalmente src/core/util.c)
 * =========================================================================== */
/*
 * util.c - implementação das funções auxiliares (ver config.h, seção util.h).
 *
 * Nada aqui é "sistema operacional": são ferramentas pequenas (alocar
 * memória, ler números, pintar o terminal, sortear) usadas por todo o resto.
 */

/* Liga/desliga as cores ANSI. main.c desliga quando a saída não é um
 * terminal ou quando o usuário passa --sem-cor. */
bool g_usar_cores = true;

/* ------------------------------------------------------------ memória */
/* As quatro funções x* são o malloc/calloc/realloc/strdup que ENCERRAM o
 * programa com mensagem clara se faltar memória. O `n ? n : 1` evita pedir
 * 0 bytes, caso em que malloc pode devolver NULL mesmo sem estar sem memória
 * (comportamento definido pela implementação). */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente (%zu bytes)\n", n);
        exit(2);
    }
    return p;
}

void *xcalloc(size_t n, size_t tam) {
    void *p = calloc(n ? n : 1, tam ? tam : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente\n");
        exit(2);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) {
        fprintf(stderr, "erro fatal: memória insuficiente (%zu bytes)\n", n);
        exit(2);
    }
    return p;
}

/* Copia uma string para memória nova (strdup não existe em C11 puro). O +1
 * é o '\0' final. */
char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

/* ------------------------------------------------------------- texto */

/* Tira espaços do começo e do fim, no próprio buffer. Passos:
 *   1. `ini` avança até o 1º caractere que não é espaço;
 *   2. `fim` recua a partir do final até o último que não é espaço;
 *   3. coloca '\0' depois dele e, se houve espaços no início, desloca o
 *      texto para o começo do buffer (memmove aceita regiões sobrepostas).
 * O cast para unsigned char é exigido por isspace: passar um char negativo
 * (acento em UTF-8) seria comportamento indefinido. */
char *str_trim(char *s) {
    char *ini = s;
    while (*ini && isspace((unsigned char)*ini)) ini++;
    char *fim = ini + strlen(ini);
    while (fim > ini && isspace((unsigned char)fim[-1])) fim--;
    *fim = '\0';
    if (ini != s) memmove(s, ini, (size_t)(fim - ini) + 1);
    return s;
}

/* Compara ignorando maiúsculas/minúsculas (req. 3.3.2: "RM", "rm" e "Rm"
 * são a mesma coisa). Percorre as duas ao mesmo tempo; se acabarem juntas
 * (*a == *b == '\0') são iguais. */
bool str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

/* Converte texto em inteiro de forma ESTRITA. Por que não usar só atoi?
 * Porque atoi("12abc") devolve 12 sem avisar, e atoi("abc") devolve 0.
 * Aqui só aceitamos: sinal opcional + dígitos, nada mais.
 * Retorno: 0 = ok, -1 = não é número, -2 = número grande demais para um int. */
int parse_int(const char *s, int *out) {
    if (!s || !*s) return -1;
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (!*p) return -1;                       /* só o sinal, sem dígitos */
    for (const char *q = p; *q; q++)
        if (!isdigit((unsigned char)*q)) return -1;
    errno = 0;
    long v = strtol(s, NULL, 10);
    if (errno == ERANGE || v > INT_MAX || v < INT_MIN) return -2;
    *out = (int)v;
    return 0;
}

/* Lê uma cor em hexadecimal: "RRGGBB", "#RRGGBB" ou "0xRRGGBB".
 * Cada par de dígitos é um canal (vermelho, verde, azul) de 0 a 255.
 * Montamos o número deslocando 4 bits por dígito: v = v*16 + dígito. */
bool parse_cor(const char *s, unsigned *out) {
    if (s[0] == '#') s++;
    else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (strlen(s) != 6) return false;
    unsigned v = 0;
    for (int i = 0; i < 6; i++) {
        int c = tolower((unsigned char)s[i]);
        if (c >= '0' && c <= '9') v = v * 16 + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned)(c - 'a' + 10);
        else return false;
    }
    *out = v;
    return true;
}

/* Lê uma linha do teclado. Devolve false só no EOF (Ctrl+D ou entrada
 * redirecionada que acabou), para os menus poderem encerrar em vez de ficar
 * em laço infinito. */
bool ler_linha(const char *prompt, char *buf, size_t tam) {
    if (prompt) {
        fputs(prompt, stdout);
        fflush(stdout);   /* sem o flush o prompt pode aparecer depois */
    }
    if (!fgets(buf, (int)tam, stdin)) {
        buf[0] = '\0';
        return false;
    }
    /* Se a linha não coube no buffer, descarta o resto para não "vazar"
     * para o próximo comando. (Se coube, o fgets guardou o '\n'.) */
    if (!strchr(buf, '\n')) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    }
    str_trim(buf);
    return true;
}

/* -------------------------------------------------------------------- ids */
/* O id da tarefa é texto (ver ID_MAX em sched.h). Todas as regras sobre ele
 * estão aqui: quem valida, compara, ordena, mostra ou sugere um id chama
 * estas funções, então as regras são as mesmas no arquivo, no terminal, na
 * janela, no Gantt e no SVG. */

/* Aceita qualquer texto que dê para mostrar e guardar. Recusa só:
 *   - vazio: a tarefa ficaria sem nome (quem chama já passou str_trim);
 *   - mais de ID_MAX-1 bytes: não cabe no vetor fixo do TCB, e cortar o id
 *     em silêncio poderia juntar dois ids diferentes num só;
 *   - caracteres de controle (< 0x20 e 0x7F, ex.: tab): não aparecem na
 *     tela, e dois ids que parecem iguais seriam diferentes.
 * Todo o resto é aceito: letras, dígitos, sinal de menos, acentos, '_',
 * '-', '.', '#', espaço no meio. Bytes >= 0x80 que não formam UTF-8 válido
 * também (arquivo salvo em ANSI/Latin-1 pelo Bloco de Notas): o arquivo não
 * pode ser recusado por isso (req. 3.3.7); a janela mostra '?' para eles e
 * o SVG troca por '?'. O ';' nunca chega aqui vindo do arquivo, porque é o
 * separador dos campos. */
const char *id_validar(const char *s) {
    static char buf[160];   /* static: o texto precisa viver após o return */
    size_t n = strlen(s);
    if (n == 0 || strspn(s, " ") == n) return "o id não pode ficar vazio";
    if (n > ID_MAX - 1) {
        snprintf(buf, sizeof buf, "id longo demais: tem %zu bytes e o máximo é %d "
                 "(cada letra acentuada ocupa 2 bytes)", n, ID_MAX - 1);
        return buf;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch < 0x20 || ch == 0x7F) {
            snprintf(buf, sizeof buf, "o id tem um caractere de controle (byte 0x%02X na posição %zu), "
                     "que não aparece na tela", ch, i + 1);
            return buf;
        }
    }
    return NULL;
}

/* "Inteiro" é exatamente o que o parse_int aceita (sinal opcional + dígitos,
 * cabendo num int), a mesma regra de quando o campo era int. Um número que
 * não cabe num int ("99999999999") não é recusado: vira um id de texto. */
bool id_eh_inteiro(const char *s, int *v) {
    int tmp;
    if (parse_int(s, &tmp) != 0) return false;
    if (v) *v = tmp;
    return true;
}

/* Um id inteiro é guardado como o %d do número ("01", "+1" e "1" viram
 * "1"). É o que acontecia quando o campo era int, e é o que garante que,
 * com todos os ids inteiros, tabelas, mensagens e rótulos saiam iguais aos
 * de antes. Um id de texto é guardado como veio. Sempre com snprintf, que
 * garante o '\0'; o `tmp` deixa usar dst == s. */
void id_normalizar(char *dst, size_t tam, const char *s) {
    char tmp[ID_MAX];
    int v;
    if (id_eh_inteiro(s, &v)) snprintf(tmp, sizeof tmp, "%d", v);
    else snprintf(tmp, sizeof tmp, "%s", s);
    snprintf(dst, tam, "%s", tmp);
}

static bool eh_digito(char c) { return c >= '0' && c <= '9'; }

/* Ordem dos ids, nesta prioridade:
 *   a) os dois são inteiros: pelo valor (0 < 2 < 10; "01" = "1"; -1 < 0).
 *      É a ordem de quando o id era int, então nada muda nesse caso;
 *   b) só um é inteiro: o inteiro vem antes;
 *   c) nenhum é inteiro: ordem "natural" sem diferenciar maiúsculas. Cada
 *      trecho de dígitos vale pelo número, então "T0" < "T1" < "T2" < "T10"
 *      (com strcmp, "T10" viria antes de "T2" e ficaria abaixo dele no
 *      Gantt, contra o req. 2.5), "t1" = "T1" (req. 3.3.2: strings do
 *      arquivo sem diferenciar maiúsculas) e "T01" = "T1".
 * Os dígitos são comparados pelo tamanho (sem zeros à esquerda) e depois
 * um a um, e não convertidos para int: um trecho de 30 dígitos não estoura.
 * O tolower só conhece ASCII (locale "C"), como no str_ieq: "É" e "é" são
 * ids diferentes. */
int id_comparar(const char *a, const char *b) {
    int va, vb;
    bool ia = id_eh_inteiro(a, &va), ib = id_eh_inteiro(b, &vb);
    if (ia && ib) return (va > vb) - (va < vb);   /* -1, 0 ou 1 sem subtrair (não estoura) */
    if (ia != ib) return ia ? -1 : 1;
    while (*a && *b) {
        if (eh_digito(*a) && eh_digito(*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            size_t la = 0, lb = 0;
            while (eh_digito(a[la])) la++;
            while (eh_digito(b[lb])) lb++;
            if (la != lb) return la < lb ? -1 : 1;   /* mais dígitos = número maior */
            int c = memcmp(a, b, la);
            if (c) return c < 0 ? -1 : 1;
            a += la;
            b += lb;
            continue;
        }
        int ca = tolower((unsigned char)*a), cb = tolower((unsigned char)*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        a++;
        b++;
    }
    return (*a != '\0') - (*b != '\0');   /* o que acabou antes vem antes */
}

/* É isto que define id repetido: "T1" e "t1" são o mesmo id, "01" e "1"
 * também (como quando o id era int). "0" e "T0" NÃO: um é inteiro e o outro
 * é texto. Usar a própria id_comparar garante que a ordem do Gantt nunca
 * tenha empate entre duas tarefas aceitas. */
bool id_iguais(const char *a, const char *b) {
    return id_comparar(a, b) == 0;
}

/* Rótulo do id (Gantt, terminal, SVG, janela). Se TODOS os ids do estado
 * atual são inteiros, sai "T<id>", exatamente o "T%d" de antes (T0, T1,
 * T10). Se pelo menos um não é inteiro, TODOS saem crus, sem prefixo: senão
 * o id "T1" viraria "TT1", e os ids "0" e "T0" apareceriam os dois como
 * "T0". Quem chama calcula `todos_inteiros` a partir do estado atual
 * (sim_ids_inteiros), porque tarefas podem ser adicionadas na simulação. */
void id_rotulo(const char *id, bool todos_inteiros, char *buf, size_t tam) {
    int v;
    if (todos_inteiros && id_eh_inteiro(id, &v)) snprintf(buf, tam, "T%d", v);
    else snprintf(buf, tam, "%s", id);
}

/* Prefixo comum (sem diferenciar maiúsculas) dos `n` primeiros bytes. */
static bool prefixo_igual(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

static bool id_em_uso(const char *const *ids, int n, const char *id) {
    for (int i = 0; i < n; i++)
        if (id_iguais(ids[i], id)) return true;
    return false;
}

/* Sugestão de id para uma tarefa nova (substitui o proximo_id que existia
 * em ui.c e em ui_gui.c). Nada aqui supõe que os ids começam em 0 ou 1 nem
 * que são seguidos:
 *   1. todos inteiros: o maior + 1, como antes (0, 1, 2 -> 3; 7, 0, 3 -> 8;
 *      sem tarefas -> 0);
 *   2. os ids que não são inteiros têm todos o mesmo prefixo seguido de um
 *      número (T0, T1, T5): o prefixo + (maior número + 1) = T6, com a
 *      capitalização e a quantidade de dígitos do id de maior número
 *      ("T09" -> "T10", "t04" -> "t05");
 *   3. qualquer outro caso (A, b, C): n+1 como texto, pulando os valores
 *      que já estão em uso (id_iguais).
 * A sugestão é sempre conferida contra os ids existentes, então nunca é um
 * id repetido. */
void id_sugerir(const char *const *ids, int n, char *dst, size_t tam) {
    bool todos = true, estourou = false;
    int m = 0;
    for (int i = 0; i < n && todos; i++) {
        int v;
        if (!id_eh_inteiro(ids[i], &v)) todos = false;
        else if (v >= m) {
            if (v == INT_MAX) estourou = true;   /* INT_MAX + 1 não existe */
            else m = v + 1;
        }
    }
    if (todos && !estourou) {
        snprintf(dst, tam, "%d", m);
        return;
    }
    if (!todos) {
        const char *base = NULL;   /* id de texto com o maior número */
        size_t lp = 0;             /* tamanho do prefixo */
        long maior = 0;
        bool padrao = true;
        for (int i = 0; i < n && padrao; i++) {
            if (id_eh_inteiro(ids[i], NULL)) continue;
            size_t len = strlen(ids[i]), p = len;
            while (p > 0 && eh_digito(ids[i][p - 1])) p--;
            /* precisa de prefixo E de número, e o número cabe num long */
            if (p == 0 || p == len || len - p > 9) {
                padrao = false;
                break;
            }
            if (base && (p != lp || !prefixo_igual(base, ids[i], p))) {
                padrao = false;
                break;
            }
            long v = strtol(ids[i] + p, NULL, 10);
            if (!base || v > maior) {
                base = ids[i];
                lp = p;
                maior = v;
            }
        }
        if (padrao && base) {
            char sug[ID_MAX + 16];
            int digitos = (int)(strlen(base) - lp);
            int l = snprintf(sug, sizeof sug, "%.*s%0*ld", (int)lp, base, digitos, maior + 1);
            if (l > 0 && l < ID_MAX && !id_em_uso(ids, n, sug)) {
                snprintf(dst, tam, "%s", sug);
                return;
            }
        }
    }
    for (int k = n + 1;; k++) {   /* termina: no máximo n valores estão em uso */
        snprintf(dst, tam, "%d", k);
        if (!id_em_uso(ids, n, dst)) return;
    }
}

/* ---------------------------------------------------------------- terminal */
/* Cores ANSI: "\x1b[" é o caractere ESC seguido de um código que o terminal
 * entende. 38;2;R;G;B = cor de texto RGB de 24 bits; 48;2;R;G;B = cor de
 * fundo; 0 = volta ao normal; 1 = negrito; 2 = esmaecido. Tudo vira
 * no-op se as cores estiverem desligadas. */

void term_fg(unsigned rgb) {
    if (g_usar_cores)
        printf("\x1b[38;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

void term_bg(unsigned rgb) {
    if (g_usar_cores)
        printf("\x1b[48;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
}

void term_reset(void) {
    if (g_usar_cores) fputs("\x1b[0m", stdout);
}

void term_negrito(void) {
    if (g_usar_cores) fputs("\x1b[1m", stdout);
}

void term_fraco(void) {
    if (g_usar_cores) fputs("\x1b[2m", stdout);
}

void term_sublinhado(void) {
    if (g_usar_cores) fputs("\x1b[4m", stdout);
}

void term_limpar(void) {
    /* Sem cores assumimos saída não interativa (arquivo, pipe): não faz
     * sentido limpar, e as sequências sujariam o texto. */
    if (g_usar_cores) fputs("\x1b[H\x1b[2J\x1b[3J", stdout);
}

/* Largura do terminal em colunas, para o Gantt saber quantos ticks cabem
 * numa linha. Tenta (1) perguntar ao terminal via ioctl, (2) a variável
 * COLUMNS, (3) assume 100. Valores <= 20 são descartados por serem absurdos. */
int term_largura(void) {
#ifndef _WIN32
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) return ws.ws_col;
#endif
    const char *c = getenv("COLUMNS");
    int v;
    if (c && parse_int(c, &v) == 0 && v > 20) return v;
    return 100;
}

/* Escolhe preto ou branco para escrever sobre uma cor de fundo, calculando o
 * brilho percebido: o olho humano acha o verde mais claro que o azul, por
 * isso os pesos 299/587/114 (padrão ITU-R BT.601). Fundo claro -> texto
 * preto; fundo escuro -> texto branco. */
unsigned cor_contraste(unsigned rgb) {
    unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    /* Pesos aproximados da luminância (ITU-R BT.601). */
    unsigned lum = (299 * r + 587 * g + 114 * b) / 1000;
    return lum > 140 ? 0x000000 : 0xFFFFFF;
}

/* --------------------------------------------------------------------- rng */

/* Gerador pseudoaleatório xorshift64*. Cada chamada embaralha os bits do
 * estado com deslocamentos e XOR (>>12, <<25, >>27) e multiplica por uma
 * constante para espalhar melhor. O estado é um número de 64 bits que
 * fica DENTRO do snapshot da simulação, por isso voltar e avançar repete os
 * mesmos sorteios. Estado 0 travaria o gerador (0 xor 0 = 0), então
 * trocamos por uma constante. */
uint32_t rng_proximo(uint64_t *estado) {
    /* xorshift64*: simples, rápido e com estado de 64 bits fácil de copiar. */
    uint64_t x = *estado ? *estado : 0x9E3779B97F4A7C15ull;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *estado = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}

/* Inteiro sorteado em [0, n). O `%` introduz um viés minúsculo quando n não
 * divide 2^32, irrelevante para sortear entre poucas tarefas. */
int rng_intervalo(uint64_t *estado, int n) {
    if (n <= 1) return 0;
    return (int)(rng_proximo(estado) % (uint32_t)n);
}
