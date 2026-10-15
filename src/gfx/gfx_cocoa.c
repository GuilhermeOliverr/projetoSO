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
