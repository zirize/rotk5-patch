/*
 * 256-colour display check.
 *
 * At start-up the game calls GetDeviceCaps on the screen DC for RASTERCAPS
 * and SIZEPALETTE and shows "256 컬러 디스플레이 드라이브를 사용해 주십시오"
 * unless the display is palettised. A modern desktop is 32 bpp (Wine returns
 * RASTERCAPS 0xbe99 without RC_PALETTE and SIZEPALETTE 0), so for callers in
 * the Win16 layer the screen DC reports a 256-entry palette with 20 reserved
 * colours, as an 8 bpp Windows 3.1 display did.
 *
 * Right after creating its WinG buffer the game reads NUMCOLORS and the whole
 * system palette. Wine answers NUMCOLORS 2048 (gdi.exe16 maps -1 to it) and
 * GetSystemPaletteEntries 0, and the game then overruns a stack buffer and
 * crashes, so NUMCOLORS is 20 and the system palette is 256 entries: the 20
 * static colours at 0..9 and 246..255, black in between.
 */
#include "shim.h"

static int (WINAPI *real_GetDeviceCaps)(HDC, int);
static UINT (WINAPI *real_GetSystemPaletteEntries)(HDC, UINT, UINT, PALETTEENTRY *);

BOOL from_win16(void *addr)
{
    HMODULE mod;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, addr, &mod))
        return FALSE;
    DWORD n = GetModuleFileNameA(mod, path, sizeof path);
    return n > 2 && path[n - 2] == '1' && path[n - 1] == '6';
}

static int WINAPI hook_GetDeviceCaps(HDC dc, int cap)
{
    int v = real_GetDeviceCaps(dc, cap);
    if (!from_win16(__builtin_return_address(0)) ||
        real_GetDeviceCaps(dc, TECHNOLOGY) != DT_RASDISPLAY)
        return v;
    switch (cap) {
    case RASTERCAPS:  return v | RC_PALETTE;
    case SIZEPALETTE: return 256;
    case NUMRESERVED: return 20;
    case NUMCOLORS:   return 20;
    }
    return v;
}

static UINT WINAPI hook_GetSystemPaletteEntries(HDC dc, UINT start, UINT count, PALETTEENTRY *out)
{
    UINT n = real_GetSystemPaletteEntries(dc, start, count, out);
    if (n || !from_win16(__builtin_return_address(0)))
        return n;
    if (!out)
        return 256;
    PALETTEENTRY stock[20];
    GetPaletteEntries(GetStockObject(DEFAULT_PALETTE), 0, 20, stock);
    for (n = 0; n < count && start + n < 256; n++) {
        UINT i = start + n;
        PALETTEENTRY e = { 0, 0, 0, 0 };
        if (i < 10)
            e = stock[i];
        else if (i >= 246)
            e = stock[i - 236];
        out[n] = e;
    }
    return n;
}

void caps_init(HMODULE gdi32)
{
    real_GetDeviceCaps = hook_export(gdi32, "GetDeviceCaps", hook_GetDeviceCaps);
    real_GetSystemPaletteEntries =
        hook_export(gdi32, "GetSystemPaletteEntries", hook_GetSystemPaletteEntries);
}
