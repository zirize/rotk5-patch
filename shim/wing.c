/*
 * Palette animation on a true-colour desktop.
 *
 * The game fades screens in and out by drawing into its WinG buffer once,
 * blitting it with WinGBitBlt, and then looping WinGSetDIBColorTable +
 * AnimatePalette with no further blit. On an 8 bpp display AnimatePalette
 * changes the hardware palette, so the pixels already on screen change
 * colour; on a 32 bpp desktop nothing changes until the next blit, and the
 * screen stays black (or stale) after a fade-in.
 *
 * So the blits wing.dll16 makes from an 8 bpp DIB section to a window are
 * remembered, and after each AnimatePalette from the Win16 layer they are
 * repeated: the DIB's colour table already holds the new colours. The first
 * map screen arrives as many strips, so plain blits with the same window,
 * source and source-to-window offset are merged into one bounding rect.
 *
 * ROTK5_TRACE=1 in the environment logs every such blit and AnimatePalette.
 */
#include "shim.h"
#include <string.h>

#define MAX_BLITS 16

struct blit {
    HWND wnd;
    HDC src;
    int x, y, w, h, sx, sy, sw, sh; /* sw/sh 0: BitBlt */
};

static BOOL (WINAPI *real_BitBlt)(HDC, int, int, int, int, HDC, int, int, DWORD);
static BOOL (WINAPI *real_StretchBlt)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
static BOOL (WINAPI *real_AnimatePalette)(HPALETTE, UINT, UINT, const PALETTEENTRY *);
static struct blit blits[MAX_BLITS];
static int nblits;
static BOOL trace;

static BOOL is_8bpp_dib(HDC dc)
{
    DIBSECTION ds;
    HGDIOBJ bmp = GetCurrentObject(dc, OBJ_BITMAP);
    return bmp && GetObjectA(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBmih.biBitCount == 8;
}

/* `b` covers `o` on the same window: `o` is fully redrawn by `b`. */
static BOOL covers(const struct blit *b, const struct blit *o)
{
    return b->wnd == o->wnd && b->x <= o->x && b->y <= o->y &&
           b->x + b->w >= o->x + o->w && b->y + b->h >= o->y + o->h;
}

/* Grow plain blit `o` to include `b` if both map the same source to the window 1:1. */
static BOOL merge(struct blit *o, const struct blit *b)
{
    if (o->wnd != b->wnd || o->src != b->src || o->sw || b->sw ||
        o->x - o->sx != b->x - b->sx || o->y - o->sy != b->y - b->sy)
        return FALSE;
    int r = max(o->x + o->w, b->x + b->w), bt = max(o->y + o->h, b->y + b->h);
    o->x = min(o->x, b->x);
    o->y = min(o->y, b->y);
    o->w = r - o->x;
    o->h = bt - o->y;
    o->sx = o->x - (b->x - b->sx);
    o->sy = o->y - (b->y - b->sy);
    return TRUE;
}

static void remember(HDC dst, const struct blit *b0)
{
    struct blit b = *b0;
    int i, j;
    if (!(b.wnd = WindowFromDC(dst)) || !is_8bpp_dib(b.src))
        return;
    if (trace)
        shim_log("wing: blit %p src %p %d,%d %dx%d from %d,%d %dx%d", b.wnd, b.src, b.x, b.y, b.w, b.h,
                 b.sx, b.sy, b.sw, b.sh);
    for (i = 0; i < nblits; i++)
        if (merge(&blits[i], &b))
            return;
    for (i = j = 0; i < nblits; i++)
        if (!covers(&b, &blits[i]))
            blits[j++] = blits[i];
    nblits = j;
    if (nblits == MAX_BLITS)
        memmove(blits, blits + 1, --nblits * sizeof *blits);
    blits[nblits++] = b;
}

static BOOL from_wing(void *addr)
{
    HMODULE mod;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &mod) ||
        !GetModuleFileNameA(mod, path, sizeof path))
        return FALSE;
    const char *base = strrchr(path, '\\');
    return !lstrcmpiA(base ? base + 1 : path, "wing.dll16");
}

static BOOL WINAPI hook_BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop)
{
    BOOL ok = real_BitBlt(dst, x, y, w, h, src, sx, sy, rop);
    if (ok && rop == SRCCOPY && from_wing(__builtin_return_address(0))) {
        struct blit b = { NULL, src, x, y, w, h, sx, sy, 0, 0 };
        remember(dst, &b);
    }
    return ok;
}

static BOOL WINAPI hook_StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy,
                                   int sw, int sh, DWORD rop)
{
    BOOL ok = real_StretchBlt(dst, x, y, w, h, src, sx, sy, sw, sh, rop);
    if (ok && rop == SRCCOPY && from_wing(__builtin_return_address(0))) {
        struct blit b = { NULL, src, x, y, w, h, sx, sy, sw, sh };
        remember(dst, &b);
    }
    return ok;
}

void wing_replay(void)
{
    for (int i = 0; i < nblits; i++) {
        struct blit *b = &blits[i];
        HDC dc;
        if (!IsWindow(b->wnd) || !(dc = GetDC(b->wnd)))
            continue;
        scale_dc(b->wnd, dc);
        if (b->sw)
            real_StretchBlt(dc, b->x, b->y, b->w, b->h, b->src, b->sx, b->sy, b->sw, b->sh, SRCCOPY);
        else
            real_BitBlt(dc, b->x, b->y, b->w, b->h, b->src, b->sx, b->sy, SRCCOPY);
        ReleaseDC(b->wnd, dc);
    }
}

static BOOL WINAPI hook_AnimatePalette(HPALETTE pal, UINT start, UINT count, const PALETTEENTRY *entries)
{
    BOOL ok = real_AnimatePalette(pal, start, count, entries);
    if (!from_win16(__builtin_return_address(0)))
        return ok;
    if (trace)
        for (int i = 0; i < nblits; i++)
            shim_log("wing: AnimatePalette %p %u+%u, replay %p %d,%d %dx%d", pal, start, count,
                     blits[i].wnd, blits[i].x, blits[i].y, blits[i].w, blits[i].h);
    wing_replay();
    return ok;
}

void wing_init(HMODULE gdi32)
{
    trace = GetEnvironmentVariableA("ROTK5_TRACE", NULL, 0) > 0;
    real_BitBlt = hook_export(gdi32, "BitBlt", hook_BitBlt);
    real_StretchBlt = hook_export(gdi32, "StretchBlt", hook_StretchBlt);
    real_AnimatePalette = hook_export(gdi32, "AnimatePalette", hook_AnimatePalette);
}
