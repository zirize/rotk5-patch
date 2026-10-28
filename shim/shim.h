#ifndef ROTK5_SHIM_H
#define ROTK5_SHIM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

/* util.c */
void shim_log_init(void);
void shim_log(const char *fmt, ...);
void *hook_export(HMODULE mod, const char *name, void *hook);
void hook_imports(HMODULE mod, void *real, void *hook);
void hook_loaded_win16(void); /* after all hook_export calls */

/* vm.c: otvdm loads the shim as its CPU emulator DLL; pass that on to vm86.dll */
void vm_init(void);

/* caps.c: is `addr` in one of the Win16 thunk modules (*.exe16, *.dll16, *.drv16)? */
BOOL from_win16(void *addr);

/* caps.c */
void caps_init(HMODULE gdi32);

/* wing.c */
void wing_init(HMODULE gdi32);
void wing_replay(void); /* repeat the remembered blits */

/* scale.c */
void scale_init(HMODULE user32);
void scale_dc(HWND wnd, HDC dc); /* apply the window scale to a DC of `wnd` */
int scale_factor(void);
HWND scale_window(void); /* the game's main window, NULL until seen */

/* movie.c: called around every MCI command that goes to the real winmm */
typedef MCIERROR(WINAPI *mciSendCommandW_t)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
void movie_before(mciSendCommandW_t send, MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms);
void movie_after(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parms, MCIERROR err);
void movie_refit(void); /* re-place playing movies after the game window moved or rescaled */

/* cdrom.c */
void cdrom_init(HMODULE kernel32);
BOOL cdrom_map_element(BOOL wide, void **elem);

/* cdaudio.c */
void cdaudio_init(HMODULE winmm);

/* One MP3 file of 44.1 kHz stereo, addressed in CD frames (mp3.c). */
typedef struct mp3 mp3_t;
mp3_t *mp3_open(const char *path, DWORD *frames); /* NULL if missing or not 44.1 kHz stereo */
BOOL mp3_seek(mp3_t *m, DWORD frame);
DWORD mp3_read(mp3_t *m, BYTE *buf, DWORD frames); /* bytes of 16-bit PCM; short at the end */
void mp3_close(mp3_t *m);

#endif
