/*
 * rotk5.dll: in the game process before the game's first instruction runs
 * (loaded by otvdm as its vm DLL on Windows, see launcher.c; injected by
 * rotk5-wine.exe under Wine); installs the hooks.
 */
#include "shim.h"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        shim_log_init();
        vm_init();
        caps_init(GetModuleHandleA("gdi32.dll"));
        wing_init(GetModuleHandleA("gdi32.dll"));
        scale_init(LoadLibraryA("user32.dll"));
        cdrom_init(GetModuleHandleA("kernel32.dll"));
        cdaudio_init(LoadLibraryA("winmm.dll"));
        hook_loaded_win16();
        shim_log("hooks installed");
    }
    return TRUE;
}
