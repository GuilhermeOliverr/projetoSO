/*
 * gfx_plataforma.c - a janela de cada sistema (ver gfx.h).
 *
 * Junta, nesta ordem, src/gfx/gfx_x11.c, gfx_win32.c e gfx_cocoa.c da
 * versão organizada em pastas. Cada trecho continua inteiro dentro do seu
 * #if/#else original, então em cada sistema só um deles é compilado; por
 * isso cada trecho mantém os seus próprios #include (no X11 o
 * _POSIX_C_SOURCE precisa vir antes de qualquer #include). Cada trecho
 * começa com um divisor e com o comentário de cabeçalho original.
 */

/* ===========================================================================
 * Seção gfx_x11.c (originalmente src/gfx/gfx_x11.c)
 * =========================================================================== */
/*
 * gfx_x11.c - janela no Linux (e no WSL do Windows 11) falando o protocolo
 * X11 DIRETO no socket do servidor gráfico, sem a libX11.
 *
 * Por que funciona sem biblioteca: no Linux com desktop quem desenha as
 * janelas é o servidor X (Xorg, ou XWayland nos sistemas com Wayland, ou o
 * WSLg no Windows 11). Qualquer programa conversa com ele por um socket
 * Unix (/tmp/.X11-unix/X0 para DISPLAY=:0), mandando mensagens binárias
 * definidas no documento "X Window System Protocol". A libX11 só monta
 * essas mensagens; aqui montamos nós mesmos, com write() e read().
 *
 * Mensagens usadas (o número é o "opcode" do protocolo):
 *   1 CreateWindow   8 MapWindow    16 InternAtom    18 ChangeProperty
 *   55 CreateGC      72 PutImage    101 GetKeyboardMapping
 * e os eventos que o servidor manda de volta (32 bytes cada): tecla, botão,
 * movimento, Expose, ConfigureNotify (novo tamanho) e ClientMessage (X da
 * janela).
 *
 * Usamos a ordem de bytes da própria máquina (o protocolo deixa o cliente
 * escolher), então os números vão para o socket como estão na memória.
 */
#if !defined(_WIN32) && !defined(__APPLE__)

#define _POSIX_C_SOURCE 200809L

#include "gfx.h"
#include "config.h"

#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int fd = -1;
static uint32_t id_base, janela, gc, raiz;
static int profundidade;           /* bits de cor da janela (24 em geral)    */
static int trocar_bytes;           /* servidor quer outra ordem de bytes nos pixels? */
static uint32_t max_req;           /* maior mensagem aceita, em blocos de 4  */
static uint8_t kmin, kmax;         /* faixa de códigos de tecla              */
static int kpk;                    /* keysyms por tecla                      */
static uint32_t *keysyms;          /* tabela tecla -> símbolo                */
static uint32_t atom_protocolos, atom_fechar;
static int escala = 1;             /* telas de alta densidade: 2 pixels por pixel */
static uint32_t *tira;             /* buffer de uma faixa da imagem já ampliada */
static size_t tam_tira;

/* ------------------------------------------------------- entrada e saída */

static bool enviar(const void *p, size_t n) {
    const char *c = p;
    while (n > 0) {
        ssize_t r = write(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        c += r;
        n -= (size_t)r;
    }
    return true;
}

static bool receber(void *p, size_t n) {
    char *c = p;
    while (n > 0) {
        ssize_t r = read(fd, c, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        c += r;
        n -= (size_t)r;
    }
    return true;
}

/* Arredonda para múltiplo de 4: o protocolo alinha tudo em blocos de 4. */
static size_t pad4(size_t n) { return (n + 3) & ~(size_t)3; }

/* Monta uma mensagem: cabeçalho de 4 bytes (opcode, 1 byte livre, tamanho
 * total em blocos de 4) seguido do corpo. */
static bool requisicao(uint8_t opcode, uint8_t detalhe, const void *corpo, size_t n) {
    uint8_t cab[4];
    uint16_t blocos = (uint16_t)((4 + pad4(n)) / 4);
    cab[0] = opcode;
    cab[1] = detalhe;
    memcpy(cab + 2, &blocos, 2);
    static const uint8_t zeros[4] = {0};
    return enviar(cab, 4) && enviar(corpo, n) && enviar(zeros, pad4(n) - n);
}

/* Lê respostas até chegar uma "reply" (tipo 1), guardando o corpo extra em
 * *extra (o chamador libera). Só é usada durante a abertura, antes de a
 * janela aparecer, então não há eventos para perder no caminho. */
static bool resposta(uint8_t r[32], uint8_t **extra) {
    for (;;) {
        if (!receber(r, 32)) return false;
        if (r[0] == 0) return false;                 /* erro do servidor */
        if (r[0] != 1) continue;                     /* evento: ignora   */
        uint32_t len;
        memcpy(&len, r + 4, 4);
        *extra = xmalloc((size_t)len * 4 + 1);
        return receber(*extra, (size_t)len * 4);
    }
}

static uint32_t novo_id(int k) { return id_base | (uint32_t)k; }

static uint32_t intern_atom(const char *nome) {
    size_t n = strlen(nome);
    uint8_t corpo[64] = {0};
    uint16_t l = (uint16_t)n;
    memcpy(corpo, &l, 2);
    memcpy(corpo + 4, nome, n);
    if (!requisicao(16, 0, corpo, 4 + n)) return 0;
    uint8_t r[32], *extra = NULL;
    if (!resposta(r, &extra)) return 0;
    free(extra);
    uint32_t a;
    memcpy(&a, r + 8, 4);
    return a;
}

static void propriedade(uint32_t prop, uint32_t tipo, int formato, const void *dados, uint32_t qtd) {
    size_t bytes = (size_t)qtd * (size_t)(formato / 8);
    uint8_t *corpo = xmalloc(20 + bytes);
    memcpy(corpo, &janela, 4);
    memcpy(corpo + 4, &prop, 4);
    memcpy(corpo + 8, &tipo, 4);
    corpo[12] = (uint8_t)formato;
    corpo[13] = corpo[14] = corpo[15] = 0;
    memcpy(corpo + 16, &qtd, 4);
    memcpy(corpo + 20, dados, bytes);
    requisicao(18, 0 /* Replace */, corpo, 20 + bytes);
    free(corpo);
}

/* ------------------------------------------------------- autenticação */

/* O servidor X normalmente exige um "cookie" (MIT-MAGIC-COOKIE-1) que fica
 * no arquivo ~/.Xauthority (ou no indicado por $XAUTHORITY). Formato de cada
 * registro: família (2 bytes, big-endian), endereço, número do display,
 * nome do método e dados, cada um precedido do tamanho (2 bytes, big-endian). */
static int ler_campo(FILE *f, char *buf, int tam) {
    int a = fgetc(f), b = fgetc(f);
    if (a == EOF || b == EOF) return -1;
    int n = (a << 8) | b;
    for (int i = 0; i < n; i++) {
        int c = fgetc(f);
        if (c == EOF) return -1;
        if (i < tam) buf[i] = (char)c;
    }
    return n < tam ? n : tam;
}

static int achar_cookie(const char *display, uint8_t cookie[16]) {
    char caminho[1024];
    const char *xa = getenv("XAUTHORITY");
    if (xa && *xa) snprintf(caminho, sizeof caminho, "%s", xa);
    else {
        const char *home = getenv("HOME");
        if (!home) return 0;
        snprintf(caminho, sizeof caminho, "%s/.Xauthority", home);
    }
    FILE *f = fopen(caminho, "rb");
    if (!f) return 0;
    char maquina[256] = "";
    gethostname(maquina, sizeof maquina - 1);
    int achou = 0;
    for (;;) {
        int a = fgetc(f), b = fgetc(f);
        if (a == EOF || b == EOF) break;
        int familia = (a << 8) | b;
        char end[256], num[32], nome[64], dados[64];
        int le = ler_campo(f, end, sizeof end);
        int ln = ler_campo(f, num, sizeof num - 1);
        int lm = ler_campo(f, nome, sizeof nome);
        int ld = ler_campo(f, dados, sizeof dados);
        if (le < 0 || ln < 0 || lm < 0 || ld < 0) break;
        num[ln] = '\0';
        /* 256 = FamilyLocal (vale para esta máquina), 65535 = qualquer uma */
        bool local = familia == 65535 || (familia == 256 && le == (int)strlen(maquina) &&
                                          !memcmp(end, maquina, (size_t)le));
        bool mesmo_display = ln == 0 || !strcmp(num, display);
        if (local && mesmo_display && lm == 18 && !memcmp(nome, "MIT-MAGIC-COOKIE-1", 18) && ld == 16) {
            memcpy(cookie, dados, 16);
            achou = 1;
            break;
        }
    }
    fclose(f);
    return achou;
}

/* ------------------------------------------------------- conexão */

/* DISPLAY=":0" ou ":0.0" -> socket /tmp/.X11-unix/X0. Tenta também o socket
 * "abstrato" do Linux, que alguns servidores usam no lugar do arquivo. */
static bool conectar(const char *disp, char *num, int tam_num, char *erro, int tam_erro) {
    if (!disp || !*disp) {
        snprintf(erro, (size_t)tam_erro, "a variável DISPLAY não está definida (sem servidor gráfico X11)");
        return false;
    }
    const char *dp = strrchr(disp, ':');
    if (!dp || dp[1] < '0' || dp[1] > '9') {
        snprintf(erro, (size_t)tam_erro, "DISPLAY '%s' em formato desconhecido", disp);
        return false;
    }
    if (dp != disp && strncmp(disp, "unix", 4) != 0) {
        snprintf(erro, (size_t)tam_erro, "DISPLAY '%s' aponta para outra máquina; só display local (:N) é suportado", disp);
        return false;
    }
    int i = 0;
    for (const char *p = dp + 1; *p >= '0' && *p <= '9' && i < tam_num - 1; p++) num[i++] = *p;
    num[i] = '\0';

    for (int abstrato = 0; abstrato < 2; abstrato++) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) break;
        struct sockaddr_un a;
        memset(&a, 0, sizeof a);
        a.sun_family = AF_UNIX;
        int n = snprintf(a.sun_path + abstrato, sizeof a.sun_path - 1, "/tmp/.X11-unix/X%s", num);
        socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (size_t)abstrato + (size_t)n);
        if (connect(fd, (struct sockaddr *)&a, len) == 0) return true;
        close(fd);
        fd = -1;
    }
    snprintf(erro, (size_t)tam_erro, "não foi possível conectar ao servidor gráfico do DISPLAY '%s'", disp);
    return false;
}

/* Lê a tabela de teclado: para cada código de tecla física, os símbolos
 * (keysyms) com e sem Shift. Sem isto só saberíamos "tecla 38", não "a". */
static bool ler_teclado(void) {
    uint8_t corpo[4] = {kmin, (uint8_t)(kmax - kmin + 1), 0, 0};
    if (!requisicao(101, 0, corpo, 4)) return false;
    uint8_t r[32], *extra = NULL;
    if (!resposta(r, &extra)) return false;
    kpk = r[1];
    keysyms = (uint32_t *)extra;
    return kpk > 0;
}

bool gfx_abrir(const char *titulo, int w, int h, int *w_real, int *h_real, char *erro, int tam_erro) {
    char num[16];
    if (!conectar(getenv("DISPLAY"), num, sizeof num, erro, tam_erro)) return false;

    /* 1. Apresentação: ordem de bytes, versão 11.0 e o cookie (se houver). */
    uint8_t cookie[16];
    int tem_cookie = achar_cookie(num, cookie);
    uint16_t um = 1;
    uint8_t ini[12 + 20 + 16] = {0};
    ini[0] = *(uint8_t *)&um ? 'l' : 'B';   /* 'l' = little-endian */
    uint16_t maior = 11, menor = 0, ln = tem_cookie ? 18 : 0, ld = tem_cookie ? 16 : 0;
    memcpy(ini + 2, &maior, 2);
    memcpy(ini + 4, &menor, 2);
    memcpy(ini + 6, &ln, 2);
    memcpy(ini + 8, &ld, 2);
    size_t n = 12;
    if (tem_cookie) {
        memcpy(ini + 12, "MIT-MAGIC-COOKIE-1", 18);   /* 18 bytes + 2 de alinhamento */
        memcpy(ini + 32, cookie, 16);
        n = 48;
    }
    uint8_t cab[8];
    if (!enviar(ini, n) || !receber(cab, 8)) {
        snprintf(erro, (size_t)tam_erro, "o servidor gráfico fechou a conexão");
        goto falha;
    }
    uint16_t blocos;
    memcpy(&blocos, cab + 6, 2);
    uint8_t *s = xmalloc((size_t)blocos * 4 + 1);
    if (!receber(s, (size_t)blocos * 4)) {
        free(s);
        snprintf(erro, (size_t)tam_erro, "resposta incompleta do servidor gráfico");
        goto falha;
    }
    if (cab[0] != 1) {
        int l = cab[1] < blocos * 4 ? cab[1] : blocos * 4;
        snprintf(erro, (size_t)tam_erro, "o servidor gráfico recusou a conexão: %.*s", l, (char *)s);
        free(s);
        goto falha;
    }

    /* 2. Lê o que interessa da resposta: faixa de ids, limites, formatos de
     * pixel e a tela principal (janela raiz, profundidade de cor, visual). */
    uint16_t lvendor, lmax;
    memcpy(&id_base, s + 4, 4);
    memcpy(&lvendor, s + 16, 2);
    memcpy(&lmax, s + 18, 2);
    max_req = lmax;
    int nformatos = s[21];
    trocar_bytes = s[22] != (ini[0] == 'B');   /* 1 = servidor big-endian */
    kmin = s[26];
    kmax = s[27];
    uint8_t *p = s + 32 + pad4(lvendor);
    uint8_t *formatos = p;
    p += 8 * nformatos;
    uint8_t *tela0 = p;
    memcpy(&raiz, tela0, 4);
    uint16_t px_l, mm_l;
    memcpy(&px_l, tela0 + 20, 2);
    memcpy(&mm_l, tela0 + 24, 2);
    uint32_t visual_raiz;
    memcpy(&visual_raiz, tela0 + 32, 4);
    profundidade = tela0[38];
    int bpp = 0;
    for (int i = 0; i < nformatos; i++)
        if (formatos[i * 8] == profundidade) bpp = formatos[i * 8 + 1];
    /* Procura o visual da raiz para conferir que é TrueColor com 8 bits por
     * canal no layout RRGGBB, que é exatamente o nosso formato de pixel. */
    bool visual_ok = false;
    uint8_t *d = tela0 + 40;
    int nprof = tela0[39];
    for (int i = 0; i < nprof; i++) {
        uint16_t nvis;
        memcpy(&nvis, d + 2, 2);
        uint8_t *v = d + 8;
        for (int j = 0; j < nvis; j++, v += 24) {
            uint32_t id, rm, gm, bm;
            memcpy(&id, v, 4);
            memcpy(&rm, v + 8, 4);
            memcpy(&gm, v + 12, 4);
            memcpy(&bm, v + 16, 4);
            if (id == visual_raiz && v[4] == 4 && rm == 0xFF0000 && gm == 0xFF00 && bm == 0xFF) visual_ok = true;
        }
        d = v;
    }
    free(s);
    if (!visual_ok || bpp != 32) {
        snprintf(erro, (size_t)tam_erro, "formato de cor da tela não suportado (precisa de cor real 24 bits)");
        goto falha;
    }

    /* Telas de alta densidade (ex.: notebook 4K): dobra o tamanho de tudo
     * para o texto não ficar minúsculo. PROJETOSO_ESCALA=1/2/3 força. */
    if (mm_l > 0 && px_l * 254 / (mm_l * 10) >= 170) escala = 2;
    const char *ev = getenv("PROJETOSO_ESCALA");
    if (ev && ev[0] >= '1' && ev[0] <= '3' && !ev[1]) escala = ev[0] - '0';

    /* 3. Nomes que vamos usar ("atoms") e a tabela do teclado. Tudo isto
     * tem resposta, por isso é feito antes de a janela existir. */
    atom_protocolos = intern_atom("WM_PROTOCOLS");
    atom_fechar = intern_atom("WM_DELETE_WINDOW");
    uint32_t atom_nome = intern_atom("_NET_WM_NAME");
    uint32_t atom_utf8 = intern_atom("UTF8_STRING");
    if (!atom_protocolos || !atom_fechar || !ler_teclado()) {
        snprintf(erro, (size_t)tam_erro, "o servidor gráfico não respondeu como esperado");
        goto falha;
    }

    /* 4. Cria a janela. Valores: cor de fundo (bit 0x2) e quais eventos
     * queremos receber (bit 0x800): tecla, botão apertado/solto, movimento,
     * Expose e mudança de tamanho. */
    janela = novo_id(1);
    gc = novo_id(2);
    uint32_t cw[10];
    uint16_t x0 = 0, y0 = 0, ww = (uint16_t)(w * escala), hh = (uint16_t)(h * escala), borda = 0, classe = 1;
    uint8_t corpo[64];
    memcpy(corpo, &janela, 4);
    memcpy(corpo + 4, &raiz, 4);
    memcpy(corpo + 8, &x0, 2);
    memcpy(corpo + 10, &y0, 2);
    memcpy(corpo + 12, &ww, 2);
    memcpy(corpo + 14, &hh, 2);
    memcpy(corpo + 16, &borda, 2);
    memcpy(corpo + 18, &classe, 2);
    cw[0] = 0;                               /* visual: o mesmo da raiz */
    cw[1] = 0x2 | 0x800;
    cw[2] = 0xF4F5F7;                        /* fundo                   */
    cw[3] = 0x1 | 0x4 | 0x8 | 0x40 | 0x8000 | 0x20000;
    memcpy(corpo + 20, cw, 16);
    requisicao(1, 0 /* profundidade da raiz */, corpo, 36);

    /* Título (Latin-1 no WM_NAME, UTF-8 no _NET_WM_NAME), pedido para
     * receber o clique no X, tamanho mínimo e nome da aplicação. */
    char latin[256];
    int nl = 0;
    for (const char *t = titulo; *t && nl < 255;) {
        unsigned c = (unsigned char)*t;
        if (c < 0x80) { latin[nl++] = (char)c; t++; }
        else if ((c & 0xE0) == 0xC0 && t[1]) { latin[nl++] = (char)(((c & 0x1F) << 6) | (t[1] & 0x3F)); t += 2; }
        else { latin[nl++] = '?'; t++; while ((*t & 0xC0) == 0x80) t++; }
    }
    propriedade(39 /* WM_NAME */, 31 /* STRING */, 8, latin, (uint32_t)nl);
    if (atom_nome && atom_utf8) propriedade(atom_nome, atom_utf8, 8, titulo, (uint32_t)strlen(titulo));
    propriedade(atom_protocolos, 4 /* ATOM */, 32, &atom_fechar, 1);
    uint32_t dicas[18] = {0};
    dicas[0] = 16;                           /* PMinSize */
    dicas[5] = (uint32_t)(760 * escala);
    dicas[6] = (uint32_t)(480 * escala);
    propriedade(40 /* WM_NORMAL_HINTS */, 41 /* WM_SIZE_HINTS */, 32, dicas, 18);
    propriedade(67 /* WM_CLASS */, 31, 8, "projetoSO\0projetoSO", 20);

    uint8_t cgc[12] = {0};
    memcpy(cgc, &gc, 4);
    memcpy(cgc + 4, &janela, 4);
    requisicao(55, 0, cgc, 12);
    requisicao(8, 0, &janela, 4);            /* MapWindow: mostra a janela */

    *w_real = w;
    *h_real = h;
    return true;

falha:
    if (fd >= 0) close(fd);
    fd = -1;
    return false;
}

/* PutImage: manda a imagem em faixas horizontais, porque uma mensagem tem
 * tamanho máximo (max_req blocos de 4 bytes; 256 KB no servidor padrão).
 * Com escala 2 cada pixel vira um quadrado 2x2 aqui mesmo. */
void gfx_mostrar(const uint32_t *pix, int w, int h) {
    if (fd < 0) return;
    int W = w * escala, H = h * escala;
    size_t bytes_linha = (size_t)W * 4;
    int linhas = (int)(((size_t)max_req * 4 - 24) / bytes_linha);
    if (linhas < 1) return;   /* janela absurdamente larga */
    if (linhas > H) linhas = H;
    size_t precisa = (size_t)linhas * W;
    if (precisa > tam_tira) {
        free(tira);
        tira = xmalloc(precisa * 4);
        tam_tira = precisa;
    }
    for (int y = 0; y < H; y += linhas) {
        int nl = H - y < linhas ? H - y : linhas;
        for (int j = 0; j < nl; j++) {
            const uint32_t *orig = pix + (size_t)((y + j) / escala) * w;
            uint32_t *dst = tira + (size_t)j * W;
            for (int x = 0; x < W; x++) {
                uint32_t c = orig[x / escala];
                if (trocar_bytes) c = (c >> 24) | ((c >> 8) & 0xFF00) | ((c << 8) & 0xFF0000) | (c << 24);
                dst[x] = c;
            }
        }
        uint8_t cab[24];
        size_t dados = (size_t)nl * bytes_linha;
        uint16_t blocos = (uint16_t)((24 + dados) / 4), uw = (uint16_t)W, uh = (uint16_t)nl, zx = 0;
        int16_t dy = (int16_t)y;
        cab[0] = 72;
        cab[1] = 2;                          /* ZPixmap: pixels inteiros */
        memcpy(cab + 2, &blocos, 2);
        memcpy(cab + 4, &janela, 4);
        memcpy(cab + 8, &gc, 4);
        memcpy(cab + 12, &uw, 2);
        memcpy(cab + 14, &uh, 2);
        memcpy(cab + 16, &zx, 2);
        memcpy(cab + 18, &dy, 2);
        cab[20] = 0;
        cab[21] = (uint8_t)profundidade;
        cab[22] = cab[23] = 0;
        if (!enviar(cab, 24) || !enviar(tira, dados)) return;
    }
}

/* Converte código de tecla + estado (Shift, CapsLock, NumLock, AltGr) no
 * caractere ou tecla especial, como a libX11 faria. */
static int traduzir_tecla(uint8_t codigo, uint16_t estado, int *mod) {
    if (codigo < kmin || codigo > kmax) return 0;
    const uint32_t *k = keysyms + (size_t)(codigo - kmin) * kpk;
    uint32_t k0 = k[0], k1 = kpk > 1 && k[1] ? k[1] : k[0];
    bool shift = estado & 1, caps = estado & 2, numlock = estado & 0x10;
    *mod = (shift ? GM_SHIFT : 0) | (estado & 4 ? GM_CTRL : 0);
    uint32_t ks = shift ? k1 : k0;
    if ((estado & 0x80) && kpk >= 5 && k[4]) ks = shift && kpk >= 6 && k[5] ? k[5] : k[4];   /* AltGr */
    if (numlock && k1 >= 0xFF80 && k1 <= 0xFFBD) ks = shift ? k0 : k1;                    /* teclado numérico */
    if (caps && !shift && ks == k0 && k1 != k0) ks = k1;                                  /* CapsLock */
    switch (ks) {
    case 0xFF0D: case 0xFF8D: return GK_ENTER;
    case 0xFF1B: return GK_ESC;
    case 0xFF08: return GK_BACKSPACE;
    case 0xFFFF: case 0xFF9F: return GK_DELETE;
    case 0xFF09: case 0xFE20: return GK_TAB;
    case 0xFF51: case 0xFF96: return GK_ESQ;
    case 0xFF53: case 0xFF98: return GK_DIR;
    case 0xFF52: case 0xFF97: return GK_CIMA;
    case 0xFF54: case 0xFF99: return GK_BAIXO;
    case 0xFF50: case 0xFF95: return GK_INICIO;
    case 0xFF57: case 0xFF9C: return GK_FIM;
    case 0xFF55: case 0xFF9A: return GK_PGCIMA;
    case 0xFF56: case 0xFF9B: return GK_PGBAIXO;
    case 0xFFAA: return '*';
    case 0xFFAB: return '+';
    case 0xFFAC: return ',';
    case 0xFFAD: return '-';
    case 0xFFAE: return '.';
    case 0xFFAF: return '/';
    default: break;
    }
    if (ks >= 0xFFB0 && ks <= 0xFFB9) return '0' + (int)(ks - 0xFFB0);
    if (ks >= 0x20 && ks <= 0xFF) return (int)ks;                  /* Latin-1 = Unicode */
    if (ks >= 0x1000100 && ks <= 0x110FFFF) return (int)(ks - 0x1000000);
    return 0;   /* Shift, Ctrl, teclas mortas de acento etc.: ignoradas */
}

static int16_t i16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static uint16_t u16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

bool gfx_esperar(GfxEvento *ev, int timeout_ms) {
    if (fd < 0) return false;
    for (;;) {
        struct pollfd pf = {fd, POLLIN, 0};
        int r = poll(&pf, 1, timeout_ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        uint8_t e[32];
        if (!receber(e, 32)) {
            /* Servidor caiu (ex.: sessão gráfica encerrada): trata como fechar. */
            close(fd);
            fd = -1;
            memset(ev, 0, sizeof *ev);
            ev->tipo = GE_FECHAR;
            return true;
        }
        uint8_t tipo = e[0] & 0x7F;
        if (tipo == 1 || tipo == 35) {        /* resposta ou evento genérico: descarta o corpo */
            uint32_t len;
            memcpy(&len, e + 4, 4);
            uint8_t lixo[256];
            for (size_t falta = (size_t)len * 4; falta > 0;) {
                size_t k = falta < sizeof lixo ? falta : sizeof lixo;
                if (!receber(lixo, k)) return false;
                falta -= k;
            }
            continue;
        }
        memset(ev, 0, sizeof *ev);
        switch (tipo) {
        case 0:   /* erro de uma requisição: não deveria acontecer */
            fprintf(stderr, "aviso: servidor gráfico informou o erro %d (requisição %d)\n", e[1], e[10]);
            continue;
        case 2:   /* KeyPress */
            ev->tecla = traduzir_tecla(e[1], u16(e + 28), &ev->mod);
            if (!ev->tecla) continue;
            ev->tipo = GE_TECLA;
            return true;
        case 4:   /* ButtonPress: 1 = esquerdo, 4/5 = roda, 6/7 = roda lateral */
        case 5: { /* ButtonRelease */
            int b = e[1];
            ev->x = i16(e + 24) / escala;
            ev->y = i16(e + 26) / escala;
            ev->mod = (u16(e + 28) & 1) ? GM_SHIFT : 0;
            if (b == 1) {
                ev->tipo = tipo == 4 ? GE_CLIQUE : GE_SOLTOU;
                return true;
            }
            if (tipo == 4 && b >= 4 && b <= 7) {
                ev->tipo = GE_RODA;
                ev->delta = (b == 4 || b == 6) ? 1 : -1;
                if (b >= 6) ev->mod |= GM_SHIFT;
                return true;
            }
            continue;
        }
        case 6:   /* MotionNotify */
            ev->tipo = GE_MOVEU;
            ev->x = i16(e + 24) / escala;
            ev->y = i16(e + 26) / escala;
            return true;
        case 12:  /* Expose: só reage ao último de uma sequência (count = 0) */
            if (u16(e + 16) != 0) continue;
            ev->tipo = GE_EXPOR;
            return true;
        case 22:  /* ConfigureNotify: tamanho pode ter mudado */
            ev->tipo = GE_TAMANHO;
            ev->w = u16(e + 20) / escala;
            ev->h = u16(e + 22) / escala;
            return true;
        case 33: { /* ClientMessage: o X da janela */
            uint32_t a;
            memcpy(&a, e + 12, 4);
            if (a != atom_fechar) continue;
            ev->tipo = GE_FECHAR;
            return true;
        }
        default:
            continue;
        }
    }
}

/* No Linux não há letras de unidade: pendrives são pastas em /media. */
int gfx_unidades(GfxUnidade *u, int max) {
    (void)u;
    (void)max;
    return 0;
}

void gfx_fechar(void) {
    if (fd >= 0) close(fd);   /* o servidor destrói a janela ao perder a conexão */
    fd = -1;
    free(keysyms);
    free(tira);
    keysyms = NULL;
    tira = NULL;
    tam_tira = 0;
}

#else
/* Em outros sistemas este arquivo fica vazio (ISO C exige alguma declaração). */
typedef int gfx_x11_nao_usado;
#endif

/* ===========================================================================
 * Seção gfx_win32.c (originalmente src/gfx/gfx_win32.c)
 * =========================================================================== */
/*
 * gfx_win32.c - janela no Windows com a API Win32 (user32 e gdi32), que
 * faz parte do próprio Windows: nada para instalar além do compilador.
 *
 * Como funciona uma janela Win32: o Windows chama a nossa função
 * `procedimento` (o "window procedure") para cada mensagem (tecla, clique,
 * redesenho...). Lá só traduzimos a mensagem para um GfxEvento e guardamos
 * numa fila; gfx_esperar tira da fila. Para desenhar, StretchDIBits copia
 * o nosso vetor de pixels para a janela.
 */
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gfx.h"

#include <stdio.h>
#include <string.h>

static HWND janela;
static const uint32_t *ultimo;     /* última imagem mostrada (para WM_PAINT) */
static int uw, uh;

/* Fila circular de eventos gerados dentro do window procedure. */
#define TAM_FILA 256
static GfxEvento fila[TAM_FILA];
static int f_ini, f_n;

static void empilhar(const GfxEvento *e) {
    if (f_n == TAM_FILA) return;   /* fila cheia: descarta (só com mouse muito rápido) */
    fila[(f_ini + f_n) % TAM_FILA] = *e;
    f_n++;
}

static int modificadores(void) {
    return (GetKeyState(VK_SHIFT) < 0 ? GM_SHIFT : 0) | (GetKeyState(VK_CONTROL) < 0 ? GM_CTRL : 0);
}

static LRESULT CALLBACK procedimento(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    GfxEvento e;
    memset(&e, 0, sizeof e);
    switch (msg) {
    case WM_CLOSE:          /* não destrói: o programa decide o que fazer */
        e.tipo = GE_FECHAR;
        empilhar(&e);
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) return 0;
        e.tipo = GE_TAMANHO;
        e.w = LOWORD(lp);
        e.h = HIWORD(lp);
        empilhar(&e);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 780;
        mm->ptMinTrackSize.y = 520;
        return 0;
    }
    case WM_ERASEBKGND:     /* nós pintamos tudo: evita piscar */
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        if (ultimo) {
            BITMAPINFO bi;
            memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader;
            bi.bmiHeader.biWidth = uw;
            bi.bmiHeader.biHeight = -uh;     /* negativo = linhas de cima para baixo */
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;     /* 0x00RRGGBB é o formato nativo */
            bi.bmiHeader.biCompression = BI_RGB;
            StretchDIBits(dc, 0, 0, uw, uh, 0, 0, uw, uh, ultimo, &bi, DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_KEYDOWN: {
        /* Teclas sem caractere. Enter, Backspace, Tab e Esc também geram
         * WM_CHAR; tratamos aqui e ignoramos lá para não contar duas vezes. */
        int t = 0;
        switch (wp) {
        case VK_RETURN: t = GK_ENTER; break;
        case VK_ESCAPE: t = GK_ESC; break;
        case VK_BACK: t = GK_BACKSPACE; break;
        case VK_DELETE: t = GK_DELETE; break;
        case VK_TAB: t = GK_TAB; break;
        case VK_LEFT: t = GK_ESQ; break;
        case VK_RIGHT: t = GK_DIR; break;
        case VK_UP: t = GK_CIMA; break;
        case VK_DOWN: t = GK_BAIXO; break;
        case VK_HOME: t = GK_INICIO; break;
        case VK_END: t = GK_FIM; break;
        case VK_PRIOR: t = GK_PGCIMA; break;
        case VK_NEXT: t = GK_PGBAIXO; break;
        default: break;
        }
        if (t) {
            e.tipo = GE_TECLA;
            e.tecla = t;
            e.mod = modificadores();
            empilhar(&e);
            return 0;
        }
        break;
    }
    case WM_CHAR:
        /* UTF-16; caracteres de controle (< 32) já foram tratados acima. */
        if (wp >= 32 && wp != 127 && (wp < 0xD800 || wp > 0xDFFF)) {
            e.tipo = GE_TECLA;
            e.tecla = (int)wp;
            e.mod = modificadores();
            empilhar(&e);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
        e.tipo = msg == WM_LBUTTONDOWN ? GE_CLIQUE : msg == WM_LBUTTONUP ? GE_SOLTOU : GE_MOVEU;
        e.x = (short)LOWORD(lp);
        e.y = (short)HIWORD(lp);
        e.mod = modificadores();
        /* Capturar o mouse garante receber o "soltou" mesmo fora da janela
         * (ao arrastar a barra de rolagem). */
        if (msg == WM_LBUTTONDOWN) SetCapture(hw);
        if (msg == WM_LBUTTONUP) ReleaseCapture();
        empilhar(&e);
        return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};   /* vem em coordenadas da tela */
        ScreenToClient(hw, &p);
        int d = (short)HIWORD(wp);
        e.tipo = GE_RODA;
        e.x = p.x;
        e.y = p.y;
        e.delta = d > 0 ? 1 : -1;
        e.mod = modificadores() | (msg == WM_MOUSEHWHEEL ? GM_SHIFT : 0);
        if (msg == WM_MOUSEHWHEEL) e.delta = -e.delta;
        empilhar(&e);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

bool gfx_abrir(const char *titulo, int w, int h, int *w_real, int *h_real, char *erro, int tam_erro) {
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = procedimento;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = L"projetoSO";
    if (!RegisterClassExW(&wc)) {
        snprintf(erro, (size_t)tam_erro, "o Windows recusou registrar a janela (erro %lu)", GetLastError());
        return false;
    }
    /* O tamanho pedido é o da área de desenho; AdjustWindowRect soma a
     * barra de título e as bordas. */
    RECT r = {0, 0, w, h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    wchar_t wt[256];
    if (!MultiByteToWideChar(CP_UTF8, 0, titulo, -1, wt, 256)) wcscpy(wt, L"projetoSO");
    janela = CreateWindowExW(0, L"projetoSO", wt, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!janela) {
        snprintf(erro, (size_t)tam_erro, "o Windows não conseguiu criar a janela (erro %lu)", GetLastError());
        return false;
    }
    ShowWindow(janela, SW_SHOW);
    SetForegroundWindow(janela);
    RECT c;
    GetClientRect(janela, &c);
    *w_real = c.right - c.left;
    *h_real = c.bottom - c.top;
    return true;
}

void gfx_mostrar(const uint32_t *pix, int w, int h) {
    ultimo = pix;
    uw = w;
    uh = h;
    InvalidateRect(janela, NULL, FALSE);
    UpdateWindow(janela);   /* pinta agora (chama WM_PAINT direto) */
}

bool gfx_esperar(GfxEvento *ev, int timeout_ms) {
    DWORD inicio = GetTickCount();
    for (;;) {
        /* Despacha tudo o que o Windows tem; o procedimento enche a fila. */
        MSG m;
        while (f_n == 0 && PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);   /* gera WM_CHAR a partir de WM_KEYDOWN */
            DispatchMessageW(&m);
        }
        if (f_n > 0) {
            *ev = fila[f_ini];
            f_ini = (f_ini + 1) % TAM_FILA;
            f_n--;
            return true;
        }
        if (timeout_ms == 0) return false;
        DWORD espera = INFINITE;
        if (timeout_ms > 0) {
            DWORD passou = GetTickCount() - inicio;
            if (passou >= (DWORD)timeout_ms) return false;
            espera = (DWORD)timeout_ms - passou;
        }
        if (MsgWaitForMultipleObjects(0, NULL, FALSE, espera, QS_ALLINPUT) == WAIT_TIMEOUT) return false;
    }
}

/* GetLogicalDrives devolve um bit por letra (bit 0 = A:, bit 4 = E:...). */
int gfx_unidades(GfxUnidade *u, int max) {
    DWORD bits = GetLogicalDrives();
    int n = 0;
    for (int i = 0; i < 26 && n < max; i++) {
        if (!(bits & (1u << i))) continue;
        char raiz[4] = {(char)('A' + i), ':', '\\', '\0'};
        const char *tipo;
        switch (GetDriveTypeA(raiz)) {
        case DRIVE_REMOVABLE: tipo = "pendrive / removível"; break;
        case DRIVE_FIXED: tipo = "disco local"; break;
        case DRIVE_REMOTE: tipo = "unidade de rede"; break;
        case DRIVE_CDROM: tipo = "CD/DVD"; break;
        default: tipo = "unidade"; break;
        }
        snprintf(u[n].raiz, sizeof u[n].raiz, "%c:", 'A' + i);
        snprintf(u[n].tipo, sizeof u[n].tipo, "%s", tipo);
        n++;
    }
    return n;
}

void gfx_fechar(void) {
    if (janela) DestroyWindow(janela);
    janela = NULL;
    ultimo = NULL;
}

#else
/* Em outros sistemas este arquivo fica vazio (ISO C exige alguma declaração). */
typedef int gfx_win32_nao_usado;
#endif

/* ===========================================================================
 * Seção gfx_cocoa.c (originalmente src/gfx/gfx_cocoa.c)
 * =========================================================================== */
/*
 * gfx_cocoa.c - janela no macOS usando o Cocoa (AppKit), que já vem no
 * sistema, escrito em C puro.
 *
 * O Cocoa é feito em Objective-C, mas toda chamada Objective-C
 * `[objeto mensagem:arg]` é, por baixo, a função C
 * `objc_msgSend(objeto, seletor, arg)` da biblioteca de runtime do sistema
 * (libobjc). Então dá para usar o Cocoa de C chamando objc_msgSend com o
 * tipo certo de cada mensagem. Também criamos duas classes em tempo de
 * execução (objc_allocateClassPair): uma NSView que desenha a nossa imagem
 * e um "delegate" que avisa quando a janela é fechada ou redimensionada.
 *
 * Declaramos aqui só os tipos e funções usados, em vez de incluir os
 * cabeçalhos do Objective-C, para o arquivo ser C11 puro.
 */
#ifdef __APPLE__

#include "gfx.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- runtime do Objective-C (libobjc) ---- */
typedef struct objc_object *id;
typedef struct objc_selector *SEL;
typedef struct objc_class *Class;
typedef signed char OBool;   /* BOOL ocupa 1 byte nas duas arquiteturas */
typedef void (*IMP)(void);

extern id objc_getClass(const char *nome);
extern SEL sel_registerName(const char *nome);
extern Class objc_allocateClassPair(Class super, const char *nome, size_t extra);
extern void objc_registerClassPair(Class c);
extern OBool class_addMethod(Class c, SEL s, IMP f, const char *tipos);
extern void objc_msgSend(void);
#if defined(__x86_64__)
/* No Intel, mensagens que devolvem struct grande (NSRect) usam outra função. */
extern void objc_msgSend_stret(void);
#endif
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

/* ---- Core Graphics ---- */
typedef double CGFloat;
typedef struct { CGFloat x, y; } CGPoint;
typedef struct { CGFloat w, h; } CGSize;
typedef struct { CGPoint origem; CGSize tam; } CGRect;
typedef struct CGContext *CGContextRef;
typedef struct CGColorSpace *CGColorSpaceRef;
typedef struct CGDataProvider *CGDataProviderRef;
typedef struct CGImage *CGImageRef;

extern CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
extern void CGColorSpaceRelease(CGColorSpaceRef);
extern CGDataProviderRef CGDataProviderCreateWithData(void *info, const void *dados, size_t tam,
                                                      void (*liberar)(void *, const void *, size_t));
extern void CGDataProviderRelease(CGDataProviderRef);
extern CGImageRef CGImageCreate(size_t w, size_t h, size_t bits_comp, size_t bits_pixel, size_t bytes_linha,
                                CGColorSpaceRef cs, uint32_t info, CGDataProviderRef prov,
                                const CGFloat *decode, bool interpolar, int intencao);
extern void CGImageRelease(CGImageRef);
extern void CGContextDrawImage(CGContextRef, CGRect, CGImageRef);
extern void CGContextSetInterpolationQuality(CGContextRef, int);

extern id const NSDefaultRunLoopMode;

/* Constantes do AppKit/Core Graphics usadas (valores dos cabeçalhos). */
#define ESTILO_JANELA 15         /* titled | closable | miniaturizable | resizable */
#define BUFFERED 2
#define KCG_INFO (6u | (2u << 12)) /* kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little */
#define EV_MOUSE_DOWN 1
#define EV_MOUSE_UP 2
#define EV_MOUSE_MOVED 5
#define EV_MOUSE_DRAGGED 6
#define EV_KEY_DOWN 10
#define EV_SCROLL 22
#define MOD_SHIFT (1u << 17)
#define MOD_CTRL (1u << 18)
#define MOD_CMD (1u << 20)

/* Atalhos para objc_msgSend com cada assinatura que usamos. Converter o
 * ponteiro de função para o tipo exato é obrigatório (no Apple Silicon um
 * cast errado passa os argumentos nos registradores errados). A conversão
 * é feita a partir de um ponteiro (`envio`), e não do nome da função, para
 * o gcc não reclamar de "chamada por tipo incompatível". */
static IMP const envio = objc_msgSend;
#if defined(__x86_64__)
static IMP const envio_stret = objc_msgSend_stret;
#endif
static SEL S(const char *n) { return sel_registerName(n); }
static id cls(const char *n) { return objc_getClass(n); }
static id msg(id o, const char *s) { return ((id(*)(id, SEL))envio)(o, S(s)); }
static id msg_id(id o, const char *s, id a) { return ((id(*)(id, SEL, id))envio)(o, S(s), a); }
static void msg_b(id o, const char *s, OBool b) { ((void (*)(id, SEL, OBool))envio)(o, S(s), b); }
static void msg_l(id o, const char *s, long v) { ((void (*)(id, SEL, long))envio)(o, S(s), v); }
static unsigned long msg_ul(id o, const char *s) { return ((unsigned long (*)(id, SEL))envio)(o, S(s)); }
static double msg_d(id o, const char *s) { return ((double (*)(id, SEL))envio)(o, S(s)); }
static id texto_ns(const char *utf8) {
    return ((id(*)(id, SEL, const char *))envio)(cls("NSString"), S("stringWithUTF8String:"), utf8);
}

static CGRect quadro(id view) {
    CGRect r;
#if defined(__x86_64__)
    ((void (*)(CGRect *, id, SEL))envio_stret)(&r, view, S("frame"));
#else
    r = ((CGRect(*)(id, SEL))envio)(view, S("frame"));
#endif
    return r;
}

static id app, janela, vista;
static const uint32_t *ultimo;
static int uw, uh;
static double acumulado_roda;      /* trackpad manda frações de "clique" */

#define TAM_FILA 256
static GfxEvento fila[TAM_FILA];
static int f_ini, f_n;

static void empilhar(const GfxEvento *e) {
    if (f_n == TAM_FILA) return;
    fila[(f_ini + f_n) % TAM_FILA] = *e;
    f_n++;
}

/* ---- métodos das classes criadas em tempo de execução ---- */

/* -[ProjetoSOView drawRect:]: desenha a última imagem ocupando a view
 * inteira. Em tela Retina a view tem metade dos pixels físicos; sem
 * interpolação cada pixel vira um quadrado nítido. */
static void desenhar(id self, SEL cmd, CGRect sujo) {
    (void)cmd;
    (void)sujo;
    if (!ultimo) return;
    id ctxns = msg(cls("NSGraphicsContext"), "currentContext");
    CGContextRef ctx = (CGContextRef)msg(ctxns, "CGContext");
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, ultimo, (size_t)uw * uh * 4, NULL);
    CGImageRef img = CGImageCreate((size_t)uw, (size_t)uh, 8, 32, (size_t)uw * 4, cs, KCG_INFO, prov, NULL, false, 0);
    CGContextSetInterpolationQuality(ctx, 1 /* kCGInterpolationNone */);
    CGRect r = quadro(self);
    r.origem.x = r.origem.y = 0;
    CGContextDrawImage(ctx, r, img);
    CGImageRelease(img);
    CGDataProviderRelease(prov);
    CGColorSpaceRelease(cs);
}

static OBool sim(id self, SEL cmd) {
    (void)self;
    (void)cmd;
    return 1;
}

/* -[ProjetoSODelegado windowShouldClose:]: devolve NÃO e avisa o programa,
 * que decide como encerrar. */
static OBool deve_fechar(id self, SEL cmd, id remetente) {
    (void)self;
    (void)cmd;
    (void)remetente;
    GfxEvento e;
    memset(&e, 0, sizeof e);
    e.tipo = GE_FECHAR;
    empilhar(&e);
    return 0;
}

static void redimensionou(id self, SEL cmd, id notificacao) {
    (void)self;
    (void)cmd;
    (void)notificacao;
    CGRect r = quadro(vista);
    GfxEvento e;
    memset(&e, 0, sizeof e);
    e.tipo = GE_TAMANHO;
    e.w = (int)r.tam.w;
    e.h = (int)r.tam.h;
    empilhar(&e);
}

bool gfx_abrir(const char *titulo, int w, int h, int *w_real, int *h_real, char *erro, int tam_erro) {
    void *pool = objc_autoreleasePoolPush();
    app = msg(cls("NSApplication"), "sharedApplication");
    if (!app) {
        snprintf(erro, (size_t)tam_erro, "não foi possível iniciar o AppKit");
        objc_autoreleasePoolPop(pool);
        return false;
    }
    msg_l(app, "setActivationPolicy:", 0);   /* app normal: aparece no Dock e recebe teclado */

    Class cv = objc_allocateClassPair((Class)cls("NSView"), "ProjetoSOView", 0);
    class_addMethod(cv, S("drawRect:"), (IMP)desenhar, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    class_addMethod(cv, S("acceptsFirstResponder"), (IMP)sim, "c@:");
    class_addMethod(cv, S("isOpaque"), (IMP)sim, "c@:");
    objc_registerClassPair(cv);
    Class cd = objc_allocateClassPair((Class)cls("NSObject"), "ProjetoSODelegado", 0);
    class_addMethod(cd, S("windowShouldClose:"), (IMP)deve_fechar, "c@:@");
    class_addMethod(cd, S("windowDidResize:"), (IMP)redimensionou, "v@:@");
    objc_registerClassPair(cd);

    CGRect r = {{0, 0}, {(CGFloat)w, (CGFloat)h}};
    janela = msg((id)cls("NSWindow"), "alloc");
    janela = ((id(*)(id, SEL, CGRect, unsigned long, unsigned long, OBool))envio)(
        janela, S("initWithContentRect:styleMask:backing:defer:"), r, ESTILO_JANELA, BUFFERED, 0);
    if (!janela) {
        snprintf(erro, (size_t)tam_erro, "o macOS não conseguiu criar a janela");
        objc_autoreleasePoolPop(pool);
        return false;
    }
    vista = msg(msg((id)cv, "alloc"), "init");
    id delegado = msg(msg((id)cd, "alloc"), "init");
    msg_id(janela, "setTitle:", texto_ns(titulo));
    msg_id(janela, "setContentView:", vista);
    msg_id(janela, "setDelegate:", delegado);
    msg_b(janela, "setAcceptsMouseMovedEvents:", 1);
    CGSize minimo = {780, 520};
    ((void (*)(id, SEL, CGSize))envio)(janela, S("setContentMinSize:"), minimo);
    msg(janela, "center");
    msg_id(janela, "makeKeyAndOrderFront:", NULL);
    msg(app, "finishLaunching");
    msg_b(app, "activateIgnoringOtherApps:", 1);

    CGRect q = quadro(vista);
    *w_real = (int)q.tam.w;
    *h_real = (int)q.tam.h;
    objc_autoreleasePoolPop(pool);
    return true;
}

void gfx_mostrar(const uint32_t *pix, int w, int h) {
    void *pool = objc_autoreleasePoolPush();
    ultimo = pix;
    uw = w;
    uh = h;
    msg_b(vista, "setNeedsDisplay:", 1);
    msg(vista, "displayIfNeeded");   /* desenha agora (chama drawRect:) */
    objc_autoreleasePoolPop(pool);
}

/* Tecla física (keyCode do macOS) -> tecla especial. */
static int tecla_especial(unsigned short k) {
    switch (k) {
    case 36: case 76: return GK_ENTER;
    case 53: return GK_ESC;
    case 51: return GK_BACKSPACE;
    case 117: return GK_DELETE;
    case 48: return GK_TAB;
    case 123: return GK_ESQ;
    case 124: return GK_DIR;
    case 125: return GK_BAIXO;
    case 126: return GK_CIMA;
    case 115: return GK_INICIO;
    case 119: return GK_FIM;
    case 116: return GK_PGCIMA;
    case 121: return GK_PGBAIXO;
    default: return 0;
    }
}

/* Converte um NSEvent; devolve true se gerou um GfxEvento em *e. */
static bool traduzir(id ev, GfxEvento *e) {
    unsigned long tipo = msg_ul(ev, "type");
    unsigned long flags = msg_ul(ev, "modifierFlags");
    memset(e, 0, sizeof *e);
    e->mod = (flags & MOD_SHIFT ? GM_SHIFT : 0) | (flags & MOD_CTRL ? GM_CTRL : 0);
    if (tipo == EV_KEY_DOWN) {
        unsigned short k = ((unsigned short (*)(id, SEL))envio)(ev, S("keyCode"));
        if ((flags & MOD_CMD) && k == 12) {   /* Cmd+Q */
            e->tipo = GE_FECHAR;
            return true;
        }
        int t = tecla_especial(k);
        if (!t) {
            const char *c = ((const char *(*)(id, SEL))envio)(msg(ev, "characters"), S("UTF8String"));
            if (!c || !*c) return false;
            /* decodifica o primeiro caractere UTF-8 */
            const unsigned char *p = (const unsigned char *)c;
            unsigned u = p[0];
            if (u >= 0xC0 && u < 0xE0 && p[1]) u = ((u & 0x1F) << 6) | (p[1] & 0x3F);
            else if (u >= 0xE0 && u < 0xF0 && p[1] && p[2]) u = ((u & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            if (u < 32 || u == 127 || (u >= 0xF700 && u <= 0xF8FF)) return false;   /* F1..F12 etc. */
            t = (int)u;
        }
        e->tipo = GE_TECLA;
        e->tecla = t;
        return true;
    }
    if (tipo == EV_MOUSE_DOWN || tipo == EV_MOUSE_UP || tipo == EV_MOUSE_MOVED ||
        tipo == EV_MOUSE_DRAGGED || tipo == EV_SCROLL) {
        if (msg(ev, "window") != janela) return false;
        CGPoint p = ((CGPoint(*)(id, SEL))envio)(ev, S("locationInWindow"));
        CGRect q = quadro(vista);
        /* O Cocoa mede y de baixo para cima; a nossa imagem, de cima para baixo. */
        e->x = (int)p.x;
        e->y = (int)(q.tam.h - p.y);
        if (e->y < 0 && tipo != EV_MOUSE_UP && tipo != EV_MOUSE_DRAGGED) return false;   /* barra de título */
        if (tipo == EV_SCROLL) {
            double dy = msg_d(ev, "scrollingDeltaY"), dx = msg_d(ev, "scrollingDeltaX");
            bool lateral = dx * dx > dy * dy;
            double d = lateral ? -dx : dy;
            OBool preciso = ((OBool(*)(id, SEL))envio)(ev, S("hasPreciseScrollingDeltas"));
            acumulado_roda += preciso ? d / 12.0 : d;
            if (acumulado_roda > -1 && acumulado_roda < 1) return false;
            e->tipo = GE_RODA;
            e->delta = acumulado_roda > 0 ? 1 : -1;
            acumulado_roda -= e->delta;
            if (lateral) e->mod |= GM_SHIFT;
            return true;
        }
        e->tipo = tipo == EV_MOUSE_DOWN ? GE_CLIQUE : tipo == EV_MOUSE_UP ? GE_SOLTOU : GE_MOVEU;
        return true;
    }
    return false;
}

bool gfx_esperar(GfxEvento *ev, int timeout_ms) {
    void *pool = objc_autoreleasePoolPush();
    id limite = timeout_ms < 0 ? msg(cls("NSDate"), "distantFuture")
                               : ((id(*)(id, SEL, double))envio)(cls("NSDate"), S("dateWithTimeIntervalSinceNow:"),
                                                                         timeout_ms / 1000.0);
    bool ok = false;
    while (!ok) {
        if (f_n > 0) {
            *ev = fila[f_ini];
            f_ini = (f_ini + 1) % TAM_FILA;
            f_n--;
            ok = true;
            break;
        }
        id e = ((id(*)(id, SEL, unsigned long long, id, id, OBool))envio)(
            app, S("nextEventMatchingMask:untilDate:inMode:dequeue:"), ~0ULL, limite, NSDefaultRunLoopMode, 1);
        if (!e) break;   /* tempo esgotado */
        GfxEvento g;
        bool nosso = traduzir(e, &g);
        /* Teclas ficam só conosco (senão o macOS apita); o resto também vai
         * para o AppKit, que cuida de arrastar, redimensionar e fechar. */
        if (msg_ul(e, "type") != EV_KEY_DOWN) msg_id(app, "sendEvent:", e);
        if (nosso) empilhar(&g);
    }
    objc_autoreleasePoolPop(pool);
    return ok;
}

/* No macOS não há letras de unidade: pendrives são pastas em /Volumes. */
int gfx_unidades(GfxUnidade *u, int max) {
    (void)u;
    (void)max;
    return 0;
}

void gfx_fechar(void) {
    if (janela) msg(janela, "close");
    janela = NULL;
    ultimo = NULL;
}

#else
/* Em outros sistemas este arquivo fica vazio (ISO C exige alguma declaração). */
typedef int gfx_cocoa_nao_usado;
#endif
