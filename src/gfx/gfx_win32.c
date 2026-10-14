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
