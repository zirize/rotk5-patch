/*
 * The movie CD drive, without a CD.
 *
 * The game looks for its CD by asking DOS (int 21h AX=4408h, "is drive N
 * removable?") for every drive A..Z and then plays E:\OPEN.AVI-style paths
 * from the drive it found through MCI. Wine's and otvdm's krnl386 answer that
 * call with GetDriveType (krnl386.exe16 is already loaded when the shim comes
 * in; util.c's hook_loaded_win16 patches its imports as well). When the game directory has MOVIE\OPEN.AVI, Win16
 * callers are told that one unused drive letter is a CD-ROM and every real
 * CD-ROM or removable drive is fixed, and paths on that letter are rewritten
 * to MOVIE\: in MCI_OPEN elements (cdaudio.c calls cdrom_map_element) and in
 * the file calls the Win16 layer makes (the game checks D:\OPEN.AVI with
 * OpenFile first, which krnl386 turns into SearchPath/CreateFile). Without
 * MOVIE\ nothing changes and the game uses a real (or mounted) CD.
 */
#include "shim.h"
#include <stdio.h>

static char fake;                 /* the drive letter, 0 when off */
static char movie_dir[MAX_PATH + 16]; /* game dir\MOVIE\ */
static UINT (WINAPI *real_GetDriveTypeA)(LPCSTR);
static UINT (WINAPI *real_GetDriveTypeW)(LPCWSTR);
static HANDLE (WINAPI *real_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE (WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static DWORD (WINAPI *real_GetFileAttributesA)(LPCSTR);
static DWORD (WINAPI *real_GetFileAttributesW)(LPCWSTR);
static DWORD (WINAPI *real_SearchPathA)(LPCSTR, LPCSTR, LPCSTR, DWORD, LPSTR, LPSTR *);
static DWORD (WINAPI *real_SearchPathW)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPWSTR, LPWSTR *);
static HANDLE (WINAPI *real_FindFirstFileA)(LPCSTR, LPWIN32_FIND_DATAA);
static HANDLE (WINAPI *real_FindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static HFILE (WINAPI *real_OpenFile)(LPCSTR, LPOFSTRUCT, UINT);

static UINT drive_type(int letter, UINT real)
{
    if (letter >= 'a' && letter <= 'z')
        letter -= 32;
    if (letter == fake)
        return DRIVE_CDROM;
    return real == DRIVE_CDROM || real == DRIVE_REMOVABLE ? DRIVE_FIXED : real;
}

static UINT WINAPI hook_GetDriveTypeA(LPCSTR root)
{
    UINT t = real_GetDriveTypeA(root);
    if (fake && root && root[0] && from_win16(__builtin_return_address(0)))
        t = drive_type(root[0], t);
    return t;
}

static UINT WINAPI hook_GetDriveTypeW(LPCWSTR root)
{
    UINT t = real_GetDriveTypeW(root);
    if (fake && root && root[0] && root[0] < 128 && from_win16(__builtin_return_address(0)))
        t = drive_type(root[0], t);
    return t;
}

/* Path on the fake drive → MOVIE\ path in `out`; otherwise `in` itself. */
static const char *map_a(const char *in, char *out, size_t n)
{
    if (!fake || !in || (in[0] & ~32) != fake || in[1] != ':')
        return in;
    const char *rest = in + 2;
    while (*rest == '\\' || *rest == '/')
        rest++;
    snprintf(out, n, "%s%s", movie_dir, rest);
    return out;
}

static const WCHAR *map_w(const WCHAR *in, WCHAR *out, size_t n)
{
    char a[MAX_PATH], b[MAX_PATH * 2];
    if (!fake || !in || in[0] >= 128 || (in[0] & ~32) != fake || in[1] != ':')
        return in;
    WideCharToMultiByte(CP_ACP, 0, in, -1, a, sizeof a, NULL, NULL);
    MultiByteToWideChar(CP_ACP, 0, map_a(a, b, sizeof b), -1, out, (int)n);
    return out;
}

#define WIN16 from_win16(__builtin_return_address(0))
#define MAP_A(p) (WIN16 ? map_a(p, buf, sizeof buf) : p)
#define MAP_W(p) (WIN16 ? map_w(p, wbuf, MAX_PATH * 2) : p)

static HANDLE WINAPI hook_CreateFileA(LPCSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t)
{
    char buf[MAX_PATH * 2];
    return real_CreateFileA(MAP_A(f), a, s, sa, c, fl, t);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR f, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD fl, HANDLE t)
{
    WCHAR wbuf[MAX_PATH * 2];
    return real_CreateFileW(MAP_W(f), a, s, sa, c, fl, t);
}

static DWORD WINAPI hook_GetFileAttributesA(LPCSTR f)
{
    char buf[MAX_PATH * 2];
    return real_GetFileAttributesA(MAP_A(f));
}

static DWORD WINAPI hook_GetFileAttributesW(LPCWSTR f)
{
    WCHAR wbuf[MAX_PATH * 2];
    return real_GetFileAttributesW(MAP_W(f));
}

static DWORD WINAPI hook_SearchPathA(LPCSTR path, LPCSTR f, LPCSTR ext, DWORD n, LPSTR out, LPSTR *part)
{
    char buf[MAX_PATH * 2];
    return real_SearchPathA(path, MAP_A(f), ext, n, out, part);
}

static DWORD WINAPI hook_SearchPathW(LPCWSTR path, LPCWSTR f, LPCWSTR ext, DWORD n, LPWSTR out, LPWSTR *part)
{
    WCHAR wbuf[MAX_PATH * 2];
    return real_SearchPathW(path, MAP_W(f), ext, n, out, part);
}

static HANDLE WINAPI hook_FindFirstFileA(LPCSTR f, LPWIN32_FIND_DATAA d)
{
    char buf[MAX_PATH * 2];
    return real_FindFirstFileA(MAP_A(f), d);
}

static HANDLE WINAPI hook_FindFirstFileW(LPCWSTR f, LPWIN32_FIND_DATAW d)
{
    WCHAR wbuf[MAX_PATH * 2];
    return real_FindFirstFileW(MAP_W(f), d);
}

static HFILE WINAPI hook_OpenFile(LPCSTR f, LPOFSTRUCT o, UINT style)
{
    char buf[MAX_PATH * 2];
    return real_OpenFile(MAP_A(f), o, style);
}

/* If *elem (char* or WCHAR*) names a file on the fake drive, point it at MOVIE\ instead. */
BOOL cdrom_map_element(BOOL wide, void **elem)
{
    static char a[MAX_PATH * 2];
    static WCHAR w[MAX_PATH * 2];
    char path[MAX_PATH];
    if (!fake || !*elem)
        return FALSE;
    if (wide)
        WideCharToMultiByte(CP_ACP, 0, *elem, -1, path, sizeof path, NULL, NULL);
    else
        lstrcpynA(path, *elem, sizeof path);
    if (map_a(path, a, sizeof a) == path)
        return FALSE;
    if (wide) {
        MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH * 2);
        *elem = w;
    } else {
        *elem = a;
    }
    shim_log("cdrom: %s -> %s", path, a);
    return TRUE;
}

void cdrom_init(HMODULE kernel32)
{
    char dir[MAX_PATH], test[MAX_PATH + 32];
    DWORD n = GetCurrentDirectoryA(sizeof dir, dir), drives = GetLogicalDrives();
    if (!n || n >= sizeof dir - 16)
        return;
    snprintf(movie_dir, sizeof movie_dir, "%s%sMOVIE\\", dir, dir[n - 1] == '\\' ? "" : "\\");
    snprintf(test, sizeof test, "%sOPEN.AVI", movie_dir);
    if (GetFileAttributesA(test) == INVALID_FILE_ATTRIBUTES)
        return;
    for (char c = 'D'; c <= 'Z'; c++)
        if (!(drives & (1u << (c - 'A')))) {
            fake = c;
            break;
        }
    if (!fake)
        return;
#define HOOK(f) real_##f = hook_export(kernel32, #f, hook_##f)
    HOOK(GetDriveTypeA);
    HOOK(GetDriveTypeW);
    HOOK(CreateFileA);
    HOOK(CreateFileW);
    HOOK(GetFileAttributesA);
    HOOK(GetFileAttributesW);
    HOOK(SearchPathA);
    HOOK(SearchPathW);
    HOOK(FindFirstFileA);
    HOOK(FindFirstFileW);
    HOOK(OpenFile);
#undef HOOK
    shim_log("cdrom: movies from %s as drive %c:", movie_dir, fake);
}
