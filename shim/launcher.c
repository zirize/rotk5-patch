/*
 * rotk5.exe: start SAM5.EXE (Win16; Wine's winevdm or otvdm hosts it) from the
 * launcher's own directory, suspended, load rotk5.dll into that process, then
 * let it run. 64-bit Windows cannot start a Win16 EXE itself, so when
 * otvdm\otvdmw.exe is in the game directory the game is run through it (no
 * otvdm install or registry change needed; under Wine it is skipped). Nothing in a Win16 game loads a
 * 32-bit DLL by name, so a drop-in DLL like rotk4's ddraw.dll cannot work here.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

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
    char dir[MAX_PATH], dll[MAX_PATH + 16], exe[2 * MAX_PATH + 48];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof dir);
    while (n && dir[n - 1] != '\\')
        n--;
    dir[n ? n - 1 : 0] = 0;
    snprintf(dll, sizeof dll, "%s\\rotk5.dll", dir);
    char otvdm[MAX_PATH + 32];
    snprintf(otvdm, sizeof otvdm, "%s\\otvdm\\otvdmw.exe", dir);
    /* Under Wine its own winevdm runs Win16 programs; otvdm is for Windows. */
    BOOL wine = GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version") != NULL;
    if (!wine && GetFileAttributesA(otvdm) != INVALID_FILE_ATTRIBUTES)
        snprintf(exe, sizeof exe, "\"%s\" \"%s\\SAM5.EXE\"", otvdm, dir);
    else
        snprintf(exe, sizeof exe, "\"%s\\SAM5.EXE\"", dir);

    STARTUPINFOA si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, exe, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, dir, &si, &pi))
        return fail("SAM5.EXE를 실행할 수 없습니다");

    size_t len = strlen(dll) + 1;
    void *mem = VirtualAllocEx(pi.hProcess, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    HANDLE th = NULL;
    if (mem && WriteProcessMemory(pi.hProcess, mem, dll, len, NULL))
        th = CreateRemoteThread(pi.hProcess, NULL, 0,
            (LPTHREAD_START_ROUTINE)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"),
            mem, 0, NULL);
    DWORD loaded = 0;
    if (th) {
        WaitForSingleObject(th, INFINITE);
        GetExitCodeThread(th, &loaded);
        CloseHandle(th);
    }
    if (!loaded) {
        TerminateProcess(pi.hProcess, 1);
        return fail("rotk5.dll을 불러올 수 없습니다");
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
