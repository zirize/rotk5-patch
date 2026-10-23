/*
 * Integer scaling of the game window (x1, x2 or x3), chosen from the menu
 * "화면" that the shim adds at the left of the game's menu bar and kept in rotk5.ini
 * [window] scale (default 3). A scale whose window would not fit the
 * monitor's work area is greyed out, and the scale in use is the largest one
 * that fits, up to the stored choice; it is worked out again whenever the
 * game sets its window size (its 640x480 / 800x600 choice) or the display
 * changes, so the stored choice comes back when it fits again. The window is
 * moved back inside the work area. Movies are scaled in movie.c.
 *
 * The game keeps working in its own 640x480 / 800x600 coordinates. Every DC
 * the Win16 layer gets for the main window (GetDC, GetDCEx, BeginPaint) has a
 * world transform of `scale`, so WinG blits, BitBlt and TextOut are enlarged
 * by GDI with plain pixel repetition. The other direction is converted back:
 * GetClientRect, ScreenToClient, ClientToScreen, MapWindowPoints, the rect of
 * InvalidateRect (RedrawWindow in Wine's user.exe16) and BeginPaint's
 * rcPaint, and the coordinates of mouse messages (in a subclass of the window
 * procedure). MoveWindow/SetWindowPos sizes from the game are taken as the
 * game's client size and enlarged. The window loses its sizing border and
 * maximize box, so its size only changes through the menu.
 *
 * Wine's DPI scaling does the same job about five times slower (a start-up
 * fade step took 162 ms at 144 DPI against 31 ms at 96), so the prefix stays
 * at 96 DPI.
 */
#include "shim.h"
#include <stdio.h>
#include <stdlib.h>

#define ID_SCALE1 0x7e51 /* 0x7e51..0x7e53, far from the game's own command IDs */
#define MAX_SCALE 3

static HWND main_wnd;
static WNDPROC game_proc;
static int scale = 1; /* in use */
static int want = 3;  /* chosen in the menu, saved */
static int client_w, client_h; /* game's client size, 0 until it sets one */
static char ini[MAX_PATH];

static HDC (WINAPI *real_GetDC)(HWND);
static HDC (WINAPI *real_GetDCEx)(HWND, HRGN, DWORD);
static HDC (WINAPI *real_BeginPaint)(HWND, PAINTSTRUCT *);
static BOOL (WINAPI *real_GetClientRect)(HWND, RECT *);
static BOOL (WINAPI *real_ScreenToClient)(HWND, POINT *);
static BOOL (WINAPI *real_ClientToScreen)(HWND, POINT *);
static int (WINAPI *real_MapWindowPoints)(HWND, HWND, POINT *, UINT);
static BOOL (WINAPI *real_MoveWindow)(HWND, int, int, int, int, BOOL);
static BOOL (WINAPI *real_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
static BOOL (WINAPI *real_RedrawWindow)(HWND, const RECT *, HRGN, UINT);

static void track(HWND wnd);

void scale_dc(HWND wnd, HDC dc)
{
    if (!dc || wnd != main_wnd)
        return;
    if (scale == 1) {
        if (GetGraphicsMode(dc) == GM_ADVANCED)
            ModifyWorldTransform(dc, NULL, MWT_IDENTITY);
        return;
    }
    XFORM x = { (FLOAT)scale, 0, 0, (FLOAT)scale, 0, 0 };
    SetGraphicsMode(dc, GM_ADVANCED);
    SetWorldTransform(dc, &x);
    SetStretchBltMode(dc, COLORONCOLOR);
}

/* Outer window size for a client of the game's size at the current scale. */
/* Window size for a client of cw x ch, by the system's formula. */
static void adjust(int cw, int ch, int *w, int *h)
{
    RECT r = { 0, 0, cw, ch };
    AdjustWindowRectEx(&r, GetWindowLongA(main_wnd, GWL_STYLE), GetMenu(main_wnd) != NULL,
                       GetWindowLongA(main_wnd, GWL_EXSTYLE));
    *w = r.right - r.left;
    *h = r.bottom - r.top;
}

/* Outer window size for a client of the game's size at the current scale. The border as
 * the window really has it (Windows 11 draws it thinner than AdjustWindowRectEx says). */
static void outer_size(int cw, int ch, int *w, int *h)
{
    RECT wr, cr;
    if (GetWindowRect(main_wnd, &wr) && real_GetClientRect(main_wnd, &cr) && cr.right > 0) {
        *w = cw * scale + (wr.right - wr.left) - cr.right;
        *h = ch * scale + (wr.bottom - wr.top) - cr.bottom;
    } else {
        adjust(cw * scale, ch * scale, w, h);
    }
}

/* After a resize: if the client still is not the size wanted (menu bar wrapping, border
 * guesses), correct by the difference once. */
static void fix_client(void)
{
    RECT wr, cr;
    if (!client_w || !GetWindowRect(main_wnd, &wr) || !real_GetClientRect(main_wnd, &cr))
        return;
    int dw = client_w * scale - cr.right, dh = client_h * scale - cr.bottom;
    if (dw || dh)
        real_SetWindowPos(main_wnd, NULL, 0, 0, wr.right - wr.left + dw, wr.bottom - wr.top + dh,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/* Take an outer size the game asked for as its client size. */
static void game_outer_to_client(int w, int h)
{
    /* The game draws 640x480 or 800x592 (its WinG blits). It sizes its window with
     * 16-bit metrics that Wine's AdjustWindowRectEx does not match (636x476 came out),
     * so a result close to one of those is taken as that. */
    static const int modes[][2] = { { 640, 480 }, { 800, 592 } };
    int ow, oh;
    adjust(0, 0, &ow, &oh); /* non-client part */
    client_w = w - ow;
    client_h = h - oh;
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++)
        if (abs(client_w - modes[i][0]) <= 16 && abs(client_h - modes[i][1]) <= 16) {
            client_w = modes[i][0];
            client_h = modes[i][1];
        }
}

int scale_factor(void)
{
    return scale;
}

HWND scale_window(void)
{
    return main_wnd;
}

/* Does the window fit the work area of its monitor at scale `s`? */
static BOOL fits(int s)
{
    MONITORINFO mi = { .cbSize = sizeof mi };
    int w, h, old = scale;
    if (!client_w || !GetMonitorInfoA(MonitorFromWindow(main_wnd, MONITOR_DEFAULTTONEAREST), &mi))
        return TRUE;
    scale = s;
    outer_size(client_w, client_h, &w, &h);
    scale = old;
    return s == 1 || (w <= mi.rcWork.right - mi.rcWork.left && h <= mi.rcWork.bottom - mi.rcWork.top);
}

/* Largest scale up to `want` whose window fits. */
static int effective_scale(void)
{
    int s = want;
    while (s > 1 && !fits(s))
        s--;
    return s;
}

/* Move a w x h window at x,y so that it lies inside its monitor's work area. */
static void keep_inside(int *x, int *y, int w, int h)
{
    MONITORINFO mi = { .cbSize = sizeof mi };
    if (!GetMonitorInfoA(MonitorFromWindow(main_wnd, MONITOR_DEFAULTTONEAREST), &mi))
        return;
    RECT *r = &mi.rcWork;
    if (*x + w > r->right)
        *x = r->right - w;
    if (*y + h > r->bottom)
        *y = r->bottom - h;
    if (*x < r->left)
        *x = r->left;
    if (*y < r->top)
        *y = r->top;
}

static void update_menu(void)
{
    HMENU m = GetMenu(main_wnd);
    if (!m)
        return;
    for (int s = 1; s <= MAX_SCALE; s++)
        EnableMenuItem(m, ID_SCALE1 + s - 1, MF_BYCOMMAND | (fits(s) ? MF_ENABLED : MF_GRAYED));
    CheckMenuRadioItem(m, ID_SCALE1, ID_SCALE1 + MAX_SCALE - 1, ID_SCALE1 + scale - 1, MF_BYCOMMAND);
}

/* Resize the window for the scale that fits now; repaint if it changed. */
static void apply_scale(void)
{
    RECT c, wr;
    int w, h, s;
    if (!client_w) {
        real_GetClientRect(main_wnd, &c);
        client_w = c.right / scale;
        client_h = c.bottom / scale;
    }
    s = effective_scale();
    scale = s;
    outer_size(client_w, client_h, &w, &h);
    GetWindowRect(main_wnd, &wr);
    int x = wr.left, y = wr.top;
    keep_inside(&x, &y, w, h);
    real_SetWindowPos(main_wnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    fix_client();
    update_menu();
    InvalidateRect(main_wnd, NULL, FALSE);
    wing_replay();
    movie_refit();
    shim_log("scale: %dx (chosen %dx), client %dx%d, window %dx%d", s, want, client_w, client_h, w, h);
}

static LRESULT CALLBACK scale_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && LOWORD(wp) >= ID_SCALE1 && LOWORD(wp) < ID_SCALE1 + MAX_SCALE) {
        int s = LOWORD(wp) - ID_SCALE1 + 1;
        if (fits(s)) {
            char v[2] = { (char)('0' + s), 0 };
            want = s;
            WritePrivateProfileStringA("window", "scale", v, ini);
            apply_scale();
        }
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        LRESULT r = CallWindowProcA(game_proc, wnd, msg, wp, lp);
        apply_scale();
        return r;
    }
    if (msg == WM_INITMENU)
        update_menu();
    if (msg == WM_MOVE)
        movie_refit();
    if (scale > 1 && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL)
        lp = MAKELPARAM((short)LOWORD(lp) / scale, (short)HIWORD(lp) / scale);
    else if (scale > 1 && msg == WM_SIZE)
        lp = MAKELPARAM(LOWORD(lp) / scale, HIWORD(lp) / scale);
    return CallWindowProcA(game_proc, wnd, msg, wp, lp);
}

/* The first captioned top-level window the Win16 layer touches is the game's. */
static void track(HWND wnd)
{
    if (main_wnd || !wnd || GetAncestor(wnd, GA_ROOT) != wnd ||
        (GetWindowLongA(wnd, GWL_STYLE) & WS_CAPTION) != WS_CAPTION)
        return;
    main_wnd = wnd;
    SetWindowLongA(wnd, GWL_STYLE, GetWindowLongA(wnd, GWL_STYLE) & ~(WS_THICKFRAME | WS_MAXIMIZEBOX));
    game_proc = (WNDPROC)SetWindowLongPtrA(wnd, GWLP_WNDPROC, (LONG_PTR)scale_proc);
    HMENU bar = GetMenu(wnd), view = CreatePopupMenu();
    if (bar && view) {
        AppendMenuW(view, MF_STRING, ID_SCALE1, L"x&1");
        AppendMenuW(view, MF_STRING, ID_SCALE1 + 1, L"x&2");
        AppendMenuW(view, MF_STRING, ID_SCALE1 + 2, L"x&3");
        /* Leftmost, so the game's own menus (help last) keep their order; the game
         * never touches its menu by position. */
        InsertMenuW(bar, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)view, L"화면(&V)");
        DrawMenuBar(wnd);
    }
    update_menu();
    shim_log("scale: main window %p, %dx", wnd, scale);
}

static BOOL from_game(HWND wnd)
{
    track(wnd);
    return main_wnd && wnd == main_wnd;
}

#define FROM_WIN16 from_win16(__builtin_return_address(0))

static HDC WINAPI hook_GetDC(HWND wnd)
{
    HDC dc = real_GetDC(wnd);
    if (FROM_WIN16 && from_game(wnd))
        scale_dc(wnd, dc);
    return dc;
}

static HDC WINAPI hook_GetDCEx(HWND wnd, HRGN rgn, DWORD flags)
{
    HDC dc = real_GetDCEx(wnd, rgn, flags);
    if (FROM_WIN16 && from_game(wnd))
        scale_dc(wnd, dc);
    return dc;
}

static HDC WINAPI hook_BeginPaint(HWND wnd, PAINTSTRUCT *ps)
{
    HDC dc = real_BeginPaint(wnd, ps);
    if (dc && FROM_WIN16 && from_game(wnd) && scale > 1) {
        scale_dc(wnd, dc);
        RECT *r = &ps->rcPaint;
        r->left /= scale;
        r->top /= scale;
        r->right = (r->right + scale - 1) / scale;
        r->bottom = (r->bottom + scale - 1) / scale;
    }
    return dc;
}

static BOOL WINAPI hook_GetClientRect(HWND wnd, RECT *r)
{
    BOOL ok = real_GetClientRect(wnd, r);
    if (ok && FROM_WIN16 && from_game(wnd) && scale > 1) {
        r->right /= scale;
        r->bottom /= scale;
    }
    return ok;
}

static BOOL WINAPI hook_ScreenToClient(HWND wnd, POINT *p)
{
    BOOL ok = real_ScreenToClient(wnd, p);
    if (ok && FROM_WIN16 && from_game(wnd) && scale > 1) {
        p->x = p->x >= 0 ? p->x / scale : (p->x - scale + 1) / scale;
        p->y = p->y >= 0 ? p->y / scale : (p->y - scale + 1) / scale;
    }
    return ok;
}

static BOOL WINAPI hook_ClientToScreen(HWND wnd, POINT *p)
{
    if (p && FROM_WIN16 && from_game(wnd) && scale > 1) {
        p->x *= scale;
        p->y *= scale;
    }
    return real_ClientToScreen(wnd, p);
}

static int WINAPI hook_MapWindowPoints(HWND from, HWND to, POINT *pts, UINT n)
{
    BOOL win16 = FROM_WIN16, f = win16 && from && from_game(from), t = win16 && to && from_game(to);
    UINT i;
    if (scale == 1 || f == t)
        return real_MapWindowPoints(from, to, pts, n);
    if (f)
        for (i = 0; i < n; i++)
            pts[i].x *= scale, pts[i].y *= scale;
    int ret = real_MapWindowPoints(from, to, pts, n);
    if (t)
        for (i = 0; i < n; i++)
            pts[i].x /= scale, pts[i].y /= scale;
    return ret;
}

static BOOL WINAPI hook_MoveWindow(HWND wnd, int x, int y, int w, int h, BOOL repaint)
{
    BOOL game = FROM_WIN16 && from_game(wnd);
    if (game) {
        game_outer_to_client(w, h);
        scale = effective_scale();
        outer_size(client_w, client_h, &w, &h);
        keep_inside(&x, &y, w, h);
        update_menu();
    }
    BOOL ok = real_MoveWindow(wnd, x, y, w, h, repaint);
    if (game)
        fix_client();
    return ok;
}

static BOOL WINAPI hook_SetWindowPos(HWND wnd, HWND after, int x, int y, int w, int h, UINT flags)
{
    BOOL game = !(flags & SWP_NOSIZE) && FROM_WIN16 && from_game(wnd);
    if (game) {
        game_outer_to_client(w, h);
        scale = effective_scale();
        outer_size(client_w, client_h, &w, &h);
        if (!(flags & SWP_NOMOVE))
            keep_inside(&x, &y, w, h);
        update_menu();
    }
    BOOL ok = real_SetWindowPos(wnd, after, x, y, w, h, flags);
    if (game)
        fix_client();
    return ok;
}

static BOOL WINAPI hook_RedrawWindow(HWND wnd, const RECT *r, HRGN rgn, UINT flags)
{
    if (r && scale > 1 && FROM_WIN16 && from_game(wnd)) {
        RECT s = { r->left * scale, r->top * scale, r->right * scale, r->bottom * scale };
        return real_RedrawWindow(wnd, &s, rgn, flags);
    }
    return real_RedrawWindow(wnd, r, rgn, flags);
}

void scale_init(HMODULE user32)
{
    DWORD n = GetCurrentDirectoryA(sizeof ini - 16, ini);
    snprintf(ini + n, sizeof ini - n, "%srotk5.ini", n && ini[n - 1] == '\\' ? "" : "\\");
    want = GetPrivateProfileIntA("window", "scale", 3, ini);
    if (want < 1 || want > MAX_SCALE)
        want = 3;
    scale = want; /* until the game sets its size and fits() can be checked */

    real_GetDC = hook_export(user32, "GetDC", hook_GetDC);
    real_GetDCEx = hook_export(user32, "GetDCEx", hook_GetDCEx);
    real_BeginPaint = hook_export(user32, "BeginPaint", hook_BeginPaint);
    real_GetClientRect = hook_export(user32, "GetClientRect", hook_GetClientRect);
    real_ScreenToClient = hook_export(user32, "ScreenToClient", hook_ScreenToClient);
    real_ClientToScreen = hook_export(user32, "ClientToScreen", hook_ClientToScreen);
    real_MapWindowPoints = hook_export(user32, "MapWindowPoints", hook_MapWindowPoints);
    real_MoveWindow = hook_export(user32, "MoveWindow", hook_MoveWindow);
    real_SetWindowPos = hook_export(user32, "SetWindowPos", hook_SetWindowPos);
    real_RedrawWindow = hook_export(user32, "RedrawWindow", hook_RedrawWindow);
}
