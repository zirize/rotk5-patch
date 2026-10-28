/*
 * rotk5.exe: start SAM5.EXE with the shim. 64-bit Windows cannot start a Win16
 * EXE itself, so the game runs through otvdm\otvdmw.exe in the game directory
 * (no otvdm install or registry change needed). otvdm loads its CPU emulator
 * DLL by the name in otvdm\otvdm.ini [otvdm] vm=; that is set to this
 * directory's rotk5.dll (rewritten on every start, so the folder can move),
 * which installs the hooks and passes the emulator calls on to otvdm's own
 * vm86.dll (vm.c). Nothing is written into another process.
 * Under Wine, whose winevdm has no such setting, rotk5-wine.exe is started
 * instead (wine-launcher.c).
 */
#include <windows.h>
#include <stdio.h>

static int fail(const char *what)
{
    char msg[256];
    snprintf(msg, sizeof msg, "%s (error %lu)", what, GetLastError());
    MessageBoxA(NULL, msg, "rotk5", MB_ICONERROR);
    return 1;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)inst; (void)prev; (void)cmd; (void)show;
    char dir[MAX_PATH], dll[MAX_PATH + 16], ini[MAX_PATH + 32], exe[2 * MAX_PATH + 48];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof dir);
    while (n && dir[n - 1] != '\\')
        n--;
    dir[n ? n - 1 : 0] = 0;

    BOOL wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
    if (wine)
        snprintf(exe, sizeof exe, "\"%s\\rotk5-wine.exe\"", dir);
    else {
        snprintf(dll, sizeof dll, "%s\\rotk5.dll", dir);
        snprintf(ini, sizeof ini, "%s\\otvdm\\otvdm.ini", dir);
        snprintf(exe, sizeof exe, "\"%s\\otvdm\\otvdmw.exe\" \"%s\\SAM5.EXE\"", dir, dir);
        if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES)
            return fail("rotk5.dll이 없습니다");
        if (!WritePrivateProfileStringA("otvdm", "vm", dll, ini))
            return fail("otvdm\\otvdm.ini에 쓸 수 없습니다 (otvdm 폴더를 확인해 주세요)");
    }

    STARTUPINFOA si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, exe, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
        return fail(wine ? "rotk5-wine.exe를 실행할 수 없습니다" : "otvdm으로 SAM5.EXE를 실행할 수 없습니다");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
