CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
LDFLAGS = -static-libgcc -s

DLL_SRCS = dllmain.c caps.c wing.c scale.c cdrom.c cdaudio.c movie.c mp3.c util.c vm.c

all: rotk5.dll rotk5.exe rotk5-wine.exe

rotk5.dll: $(DLL_SRCS) rotk5.def shim.h minimp3/minimp3.h minimp3/minimp3_ex.h
	$(CC) $(CFLAGS) -shared -o $@ $(DLL_SRCS) rotk5.def $(LDFLAGS) -lgdi32 -luser32 -lwinmm

rotk5.exe: launcher.c
	$(CC) $(CFLAGS) -mwindows -o $@ launcher.c $(LDFLAGS)

rotk5-wine.exe: wine-launcher.c
	$(CC) $(CFLAGS) -mwindows -o $@ wine-launcher.c $(LDFLAGS)

clean:
	rm -f rotk5.dll rotk5.exe rotk5-wine.exe

.PHONY: all clean
