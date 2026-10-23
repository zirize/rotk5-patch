/*
 * Movie scaling.
 *
 * The game plays OPEN.AVI/END.AVI with MCI avivideo in a WS_POPUP owned by its
 * main window (MCI_OPEN with MCI_DGV_OPEN_WS | MCI_DGV_OPEN_PARENT), 320x240,
 * then MCI_WINDOW, MCI_WHERE, MCI_SEEK, MCI_PLAY. With the window scaled
 * (scale.c), right before MCI_PLAY the movie is moved into a popup of the
 * shim's own (MCI_WINDOW with MCI_DGV_WINDOW_HWND), sized to the movie times
 * the scale and centred on the game's client area, and the MCI destination is
 * set to all of it, so MCIAVI stretches the frames. In the game's process on
 * Windows 11 (otvdm) MCIAVI ignored a larger destination in its own default
 * window, while it stretched into a window it was given. The popup does not
 * follow its owner by itself, so movie_refit() repeats this when the game
 * window moves or its scale changes.
 */
#include "shim.h"
#include <digitalv.h>

#define MAX_MOVIES 4

static MCIDEVICEID movies[MAX_MOVIES]; /* open avivideo devices with the game window as parent */
static BOOL placed[MAX_MOVIES]; /* fitted once, i.e. its window is up */
static HWND frames[MAX_MOVIES];  /* the shim's popup each movie draws into */
static mciSendCommandW_t real_send;

static BOOL is_movie(MCIDEVICEID id)
{
    for (int i = 0; i < MAX_MOVIES; i++)
        if (id && movies[i] == id)
            return TRUE;
    return FALSE;
}

static int slot(MCIDEVICEID id)
{
    for (int i = 0; i < MAX_MOVIES; i++)
        if (id && movies[i] == id)
            return i;
    return -1;
}

/* Clicks fall through to the game window (same thread), which skips the movie on a click. */
static LRESULT CALLBACK frame_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcA(wnd, msg, wp, lp);
}

static HWND make_frame(HWND owner)
{
    static ATOM cls;
    if (!cls) {
        WNDCLASSA wc = { 0, frame_proc, 0, 0, GetModuleHandleA(NULL), NULL, LoadCursorA(NULL, (LPCSTR)IDC_ARROW),
                         (HBRUSH)GetStockObject(BLACK_BRUSH), NULL, "rotk5movie" };
        cls = RegisterClassA(&wc);
    }
    return CreateWindowExA(0, "rotk5movie", "", WS_POPUP | WS_CLIPCHILDREN, 0, 0, 1, 1, owner, NULL,
                           GetModuleHandleA(NULL), NULL);
}

static void fit_movie(mciSendCommandW_t send, MCIDEVICEID id)
{
    int s = scale_factor(), i = slot(id);
    HWND game = scale_window();
    MCI_DGV_RECT_PARMS src = { 0 };
    if (!game || i < 0 || send(id, MCI_WHERE, MCI_DGV_WHERE_SOURCE, (DWORD_PTR)&src))
        return;
    int w = (src.rc.right - src.rc.left) * s, h = (src.rc.bottom - src.rc.top) * s;
    RECT c;
    POINT o = { 0, 0 };
    GetClientRect(game, &c);
    ClientToScreen(game, &o);
    if (!frames[i]) {
        MCI_DGV_STATUS_PARMSW st = { .dwItem = MCI_DGV_STATUS_HWND };
        HWND def = send(id, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&st) ? NULL : (HWND)st.dwReturn;
        if (!(frames[i] = make_frame(game)))
            return;
        MCI_DGV_WINDOW_PARMSW mw = { .hWnd = frames[i] };
        send(id, MCI_WINDOW, MCI_DGV_WINDOW_HWND, (DWORD_PTR)&mw);
        if (def && IsWindow(def))
            ShowWindow(def, SW_HIDE);
    }
    SetWindowPos(frames[i], HWND_TOP, o.x + (c.right - w) / 2, o.y + (c.bottom - h) / 2, w, h,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MCI_DGV_PUT_PARMS put = { .rc = { 0, 0, w, h } }; /* left, top, width, height */
    MCIERROR err = send(id, MCI_PUT, MCI_DGV_PUT_DESTINATION | MCI_DGV_RECT, (DWORD_PTR)&put);
    shim_log("movie: device %u in %p, %dx%d at scale %d, put %lu", id, frames[i], w, h, s, err);
}

void movie_before(mciSendCommandW_t send, MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms)
{
    (void)flags; (void)parms;
    real_send = send;
    if (msg == MCI_PLAY && is_movie(id)) {
        fit_movie(send, id);
        for (int i = 0; i < MAX_MOVIES; i++)
            if (movies[i] == id)
                placed[i] = TRUE;
    }
}

void movie_refit(void)
{
    for (int i = 0; i < MAX_MOVIES; i++)
        if (movies[i] && placed[i] && real_send)
            fit_movie(real_send, movies[i]);
}

void movie_after(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms, MCIERROR err)
{
    int i;
    if (err)
        return;
    if (msg == MCI_OPEN && parms && (flags & MCI_DGV_OPEN_PARENT)) {
        MCI_DGV_OPEN_PARMSW *o = (MCI_DGV_OPEN_PARMSW *)parms;
        /* The parent comes from a 16-bit HWND: only the low word is set. */
        if (!scale_window() || LOWORD((DWORD_PTR)o->hWndParent) != LOWORD((DWORD_PTR)scale_window()))
            return;
        for (i = 0; i < MAX_MOVIES && movies[i]; i++)
            ;
        if (i < MAX_MOVIES) {
            movies[i] = o->wDeviceID;
            placed[i] = FALSE;
        }
    } else if (msg == MCI_CLOSE) {
        for (i = 0; i < MAX_MOVIES; i++)
            if (movies[i] == id) {
                movies[i] = 0, placed[i] = FALSE;
                if (frames[i])
                    DestroyWindow(frames[i]);
                frames[i] = NULL;
            }
    }
}
