/*
 * rotk5.dll: injected into the game process by rotk5.exe before the game's
 * first instruction runs; installs the hooks.
 */
#include "shim.h"

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)inst; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        shim_log_init();
        caps_init(GetModuleHandleA("gdi32.dll"));
        wing_init(GetModuleHandleA("gdi32.dll"));
        scale_init(LoadLibraryA("user32.dll"));
        cdrom_init(GetModuleHandleA("kernel32.dll"));
        cdaudio_init(LoadLibraryA("winmm.dll"));
        shim_log("hooks installed");
    }
    return TRUE;
}
