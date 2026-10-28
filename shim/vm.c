/*
 * otvdm's CPU emulator interface. otvdm's krnl386 (and its system.drv,
 * win87em) load the DLL named by otvdm.ini [otvdm] vm= (default vm86.dll) and
 * look up these exports. rotk5.exe sets vm= to rotk5.dll, which is how the shim
 * gets into the game on Windows; each export here jumps to the same export of
 * the real vm86.dll from otvdm's dll\ folder (found through otvdm's
 * SetDllDirectory). Under Wine nothing calls them and vm86.dll is not found.
 */
#include "shim.h"

static const char *const names[] = {
    "disassemble_debug", "init_vm86", "load_x87function",
    "wine_call_to_16_regs_vm86", "wine_call_to_16_vm86",
};
void *vm_real[5] __attribute__((used));

/* The signatures do not matter: each export only jumps on, arguments untouched. */
#define VM_EXPORT(i, name) \
    __asm__(".globl _" #name "\n_" #name ":\n\tjmp *_vm_real+" #i "*4\n");
VM_EXPORT(0, disassemble_debug)
VM_EXPORT(1, init_vm86)
VM_EXPORT(2, load_x87function)
VM_EXPORT(3, wine_call_to_16_regs_vm86)
VM_EXPORT(4, wine_call_to_16_vm86)

void vm_init(void)
{
    HMODULE vm = LoadLibraryA("vm86.dll");
    if (!vm)
        return;
    for (int i = 0; i < 5; i++)
        vm_real[i] = (void *)GetProcAddress(vm, names[i]);
    shim_log("vm: loaded by otvdm, passing on to vm86.dll");
}
