/*
 * Logging and export hooking.
 *
 * The Win16 layer (Wine's gdi.exe16, user.exe16, wing.dll16 …, otvdm's
 * equivalents) is a set of 32-bit DLLs loaded after this one, so patching the
 * export table of gdi32/winmm makes those modules bind to our functions when
 * they resolve their imports. Win16 modules already loaded when the shim
 * comes in (Wine's winevdm.exe loads krnl386.exe16 first; otvdm, which loads
 * the shim as its vm DLL, has krnl386.exe16 and the first drivers in) keep the
 * real functions unless their import tables are patched too
 * (hook_loaded_win16). Each hook also checks its caller, so only the Win16
 * layer is affected.
 */
#include "shim.h"
#include <stdarg.h>
#include <stdio.h>
#include <tlhelp32.h>

static struct { void *real, *hook; } hooked[64]; /* every hook_export, for hook_loaded_win16 */
static int nhooked;

static char log_path[MAX_PATH + 16];

/* rotk5.ini [debug] log=1 in the game directory: also append to rotk5.log there. */
void shim_log_init(void)
{
    char dir[MAX_PATH], ini[MAX_PATH + 16];
    DWORD n = GetCurrentDirectoryA(sizeof dir, dir);
    if (!n || n >= sizeof dir - 16)
        return;
    snprintf(ini, sizeof ini, "%s\\rotk5.ini", dir);
    if (GetPrivateProfileIntA("debug", "log", 0, ini))
        snprintf(log_path, sizeof log_path, "%s\\rotk5.log", dir);
}

void shim_log(const char *fmt, ...)
{
    char buf[512];
    int n = snprintf(buf, sizeof buf, "rotk5-shim: ");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    if (log_path[0]) {
        HANDLE f = CreateFileA(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w;
            char line[16];
            int k = snprintf(line, sizeof line, "%08lu ", GetTickCount());
            WriteFile(f, line, k, &w, NULL);
            WriteFile(f, buf + 12, lstrlenA(buf + 12), &w, NULL);
            WriteFile(f, "\r\n", 2, &w, NULL);
            CloseHandle(f);
        }
    }
}

/* Point export `name` of `mod` at `hook`; returns the original function. */
void *hook_export(HMODULE mod, const char *name, void *hook)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
    DWORD *names = (DWORD *)(base + exp->AddressOfNames);
    WORD *ords = (WORD *)(base + exp->AddressOfNameOrdinals);
    DWORD *funcs = (DWORD *)(base + exp->AddressOfFunctions);

    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (strcmp((char *)(base + names[i]), name))
            continue;
        DWORD *slot = &funcs[ords[i]];
        void *orig = base + *slot;
        /* A forwarder points into the export directory; resolve it to the real target. */
        if (*slot >= dir->VirtualAddress && *slot < dir->VirtualAddress + dir->Size)
            orig = (void *)GetProcAddress(mod, name);
        DWORD old;
        VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old);
        *slot = (DWORD)((BYTE *)hook - base); /* wraps mod 2^32, fine on 32-bit */
        VirtualProtect(slot, sizeof *slot, old, &old);
        if (nhooked < (int)(sizeof hooked / sizeof *hooked))
            hooked[nhooked].real = orig, hooked[nhooked++].hook = hook;
        else
            shim_log("too many hooks; %s not patched in loaded modules", name);
        return orig;
    }
    shim_log("export %s not found", name);
    return NULL;
}

/* In module `mod`, point every import slot that holds `real` at `hook`. */
void hook_imports(HMODULE mod, void *real, void *hook)
{
    BYTE *base = (BYTE *)mod;
    if (!mod || !real)
        return;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress)
        return;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++)
        for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(base + d->FirstThunk); t->u1.Function; t++)
            if ((void *)t->u1.Function == real) {
                DWORD old;
                VirtualProtect(&t->u1.Function, sizeof t->u1.Function, PAGE_READWRITE, &old);
                t->u1.Function = (DWORD_PTR)hook;
                VirtualProtect(&t->u1.Function, sizeof t->u1.Function, old, &old);
            }
}

/* Point the imports of every Win16 module already loaded (*16) at the hooks. */
void hook_loaded_win16(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32 me = { .dwSize = sizeof me };
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        size_t n = strlen(me.szModule);
        if (n < 3 || me.szModule[n - 2] != '1' || me.szModule[n - 1] != '6')
            continue;
        for (int i = 0; i < nhooked; i++)
            hook_imports(me.hModule, hooked[i].real, hooked[i].hook);
        shim_log("imports patched in loaded %s", me.szModule);
    }
    CloseHandle(snap);
}
