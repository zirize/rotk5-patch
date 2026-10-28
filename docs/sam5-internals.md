# SAM5.EXE (Power-Up Kit) start-up and what the shim fixes

Observed under Wine 10.0 with `+relay` and on Windows 11 under otvdm v0.9.0, 2026-10-23.

## Display check

`GetDC(0)`, `GetDeviceCaps(RASTERCAPS)`, `GetDeviceCaps(SIZEPALETTE)`; without `RC_PALETTE`
it shows "256 컬러 디스플레이 드라이브를 사용해 주십시오" and exits. Wine on a 32 bpp desktop
returns `0xbe99` and `0`.
Shim fix (`caps.c`): for callers in the Win16 layer, screen DC reports `RC_PALETTE`,
SIZEPALETTE 256, NUMRESERVED 20.

## WinG buffer and system palette

Then `WinGRecommendDIBFormat`, `WinGCreateDC`, `WinGCreateBitmap`, `WinGGetDIBPointer`,
a `CreatePalette`/`SelectPalette`/`RealizePalette` on the screen DC, `GetDeviceCaps(NUMCOLORS)`
and `GetSystemPaletteEntries(dc, 0, 256, …)`. Wine gives NUMCOLORS 2048 (gdi.exe16 turns -1
into 2048) and 0 entries; the game then crashes in 16-bit code (`035f:0038`, stack
corrupted).
Shim fix (`caps.c`): NUMCOLORS 20; an empty system palette is returned as 256 entries, the
20 stock colours at 0–9 and 246–255, black between.

## Fades (palette animation)

Screens fade by one `WinGBitBlt` of the whole 640×480 buffer, then a loop of
`WinGSetDIBColorTable(0..255)` + `AnimatePalette(pal, 16, 230, …)` with no blit. On 8 bpp
hardware the palette change recolours the screen; on a 32 bpp desktop nothing changes until
the next blit, so a faded-in background stays black until something redraws it (user report:
"바탕이 자동으로 안 그려짐"). wing.dll16 blits with gdi32 `BitBlt`/`StretchBlt`.
Shim fix (`wing.c`): blits from wing.dll16 out of an 8 bpp DIB to a window are remembered
and repeated after every `AnimatePalette` from the Win16 layer; the DIB colour table already
holds the new colours. Plain blits with the same window, source and offset merge into one
bounding rect (the first map screen comes in strips; a 16-entry list lost the left ones);
otherwise a covering blit replaces older ones, at most 16 kept. `ROTK5_TRACE=1` logs them.

## CD check

`MCI_OPEN` cdaudio by type ID (flags `0x3000`), `MCI_STATUS NUMBER_OF_TRACKS`, `MCI_CLOSE`;
failure shows "CD-ROM을 넣어 주십시오" with retry. Only the track count was seen being
checked so far (21 on the PK disc). MMSYSTEM.701 is translated to `winmm.mciSendCommandW`
(otvdm's MMSYSTEM also uses `mciSendCommandA`; both are hooked, sharing one path).
Shim fix (`cdaudio.c`, from rotk4): cdaudio emulated from `CD\Audio.cue` (+ BIN or
`TrackNN.mp3`), or the cue named by `rotk5.ini` `[cdaudio] cue=`.

## CD drive for the movies

The game asks DOS for every drive A..Z whether it is removable (int 21h AX=4408h, krnl386 →
GetDriveTypeW), takes the CD drive, then checks `X:\OPEN.AVI` with KERNEL OpenFile (→
SearchPathA/CreateFile in krnl386) before MCI opens it. A missing file gives "실행 에러".
Shim fix (`cdrom.c`): when `MOVIE\OPEN.AVI` exists in the game directory, Win16 callers see
the first unused letter from D: as a CD-ROM and real CD/removable drives as fixed; paths on
that letter in CreateFile, GetFileAttributes, SearchPath, FindFirstFile, OpenFile (A/W) and in
MCI_OPEN elements are rewritten to `MOVIE\`. Wine's winevdm.exe loads krnl386.exe16 before
the shim, so its import table is patched too (`hook_imports`); otvdm loads it later.

## Opening movie

`MCI_OPEN` avivideo with element `E:\OPEN.AVI`, `MCI_DGV_OPEN_WS` style `WS_POPUP` and
`MCI_DGV_OPEN_PARENT` = the main window — the movie is a popup by design, 320×240, placed
centred over the game window. Then `MCI_WINDOW` (state), `MCI_WHERE` (source), `MCI_SEEK TO`
(fails under Wine with 297, harmless so far), `MCI_PLAY NOTIFY`, `MCI_CLOSE`. The file comes
from the CD drive (previous section). A click in the game window ends the movie early.
After the movie the game reopens cdaudio and shows its own 640×480 / 800×600 choice.
The `hWndParent` reaching winmm is the 16-bit handle under Wine (low word only, `0x5a` for
window `0x1005a`) and the full handle under otvdm, so compare low words.
Shim fix (`movie.c`): right before `MCI_PLAY` the movie is given a popup of the shim's own
(`MCI_WINDOW` + `MCI_DGV_WINDOW_HWND`, owned by the game window; MCIAVI's default window is
hidden), sized to the source rect × scale and centred on the game client, and `MCI_PUT`
destination covers it; repeated on the game window's WM_MOVE and on a scale change, since a
popup does not follow its owner. Own window because on Windows 11/otvdm MCIAVI accepted a
larger destination in its default window (`MCI_WHERE` read it back) but kept drawing
320×240 at the top-left, whether the window was resized with SetWindowPos or `MCI_PUT
WINDOW`; a standalone test (same AVI, same command order, DPI-aware or not) stretched fine,
so it is something in the otvdm process. Destroyed after `MCI_CLOSE`. The popup answers
WM_NCHITTEST with HTTRANSPARENT, so a click on the movie reaches the game window, which skips
the movie on a click (MCIAVI's own window swallowed it).

## Sound effects

Each effect: `MCI_OPEN` waveaudio by type ID (`0x20a`, flags `0x3200`) with element
`<NAME>.wav` from the game directory, `MCI_SET` time format ms, `MCI_PLAY FROM|NOTIFY`,
`MCI_STATUS MODE` (polled), `MCI_CLOSE`; seen in 휴양 (`SEIKOU`, `SIPPAI`, `SAISOKU`). Works
under Wine unchanged; Wine answers MODE = stop (`0x20d`) right after PLAY, harmless so far.
A waveaudio device opened by name at start-up (id 1) gets no further commands.

## Window scaling

Game coordinates stay 640×480 / 800×600. USER calls the game makes that matter: GetDC,
BeginPaint, InvalidateRect (→ `RedrawWindow` in Wine's user.exe16), GetClientRect,
GetCursorPos + ScreenToClient, ClientToScreen, MoveWindow, SetWindowPos; it also draws with
GDI BitBlt/TextOut besides WinG.
Shim fix (`scale.c`): window DCs from the Win16 layer get a world transform ×scale
(COLORONCOLOR); client rects, points, invalid rects, rcPaint, mouse messages and WM_SIZE are
converted back; game window sizes are taken as client sizes, snapped to the game's two
frame sizes 640×480 / 800×592 (Wine's AdjustWindowRectEx gave 636×476 from the game's
16-bit numbers). The outer size uses the window's measured border, then the client is
checked and corrected once (Windows 11's border is thinner than AdjustWindowRectEx says; both
cut 4 px off the picture before). No sizing border or maximize box. Menu "화면" (leftmost; SAM5 imports no menu API, so
shifting its menus is safe) x1/x2/x3
(IDs 0x7e51–0x7e53) sets `rotk5.ini` `[window] scale` (default 3); the scale in use is the
largest one up to that whose window fits the monitor's work area (others greyed), worked out
again when the game changes size or the display changes; the window is kept inside the work
area. Model: rotk4's `scale.c`.

## Injection

A Win16 EXE loads no 32-bit DLL by name, and Wine has no AppInit_DLLs.

- Windows (`launcher.c`): otvdm's krnl386 (and its system.drv, win87em) loads the CPU emulator
  DLL named by `otvdm\otvdm.ini` `[otvdm] vm=` (default `vm86.dll`; `kernel.c` of otvdm's krnl386)
  with `LoadLibraryA`, after krnl386 itself and system/comm/timer.drv are in, before
  gdi/user. `rotk5.exe` writes `vm=<game dir>\rotk5.dll` (absolute: otvdm's
  `SetDllDirectory(dll\)` drops the current directory from the search) and starts
  `otvdm\otvdmw.exe SAM5.EXE`. `rotk5.dll` exports the five vm86.dll entry points
  (`rotk5.def`) as jumps to the real `vm86.dll`, which it loads from otvdm's `dll\` (`vm.c`).
  Nothing is written into another process: the old suspended start + remote `LoadLibraryA`
  launcher was quarantined by Defender (`docs/testing.md`).
- Wine (`wine-launcher.c` → `rotk5-wine.exe`; `rotk5.exe` starts it when `wine_get_version`
  exists): starts `SAM5.EXE` suspended (Wine's winevdm hosts it), loads `rotk5.dll` with a
  remote `LoadLibraryA`, then resumes. Not shipped in the Windows zip or dist 7z.

The DLL patches export tables of gdi32, user32, winmm and kernel32, so the Win16 layer, loaded
later, binds to the hooks (`util.c`); the `*16` modules already loaded (krnl386.exe16 under
both hosts, plus system/comm/timer.drv16) get their import tables patched
(`hook_loaded_win16`). Hooks check the caller's module name (`*16`) so the hosts' own 32-bit
code is unaffected.
