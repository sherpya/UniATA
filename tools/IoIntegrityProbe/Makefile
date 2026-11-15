CC = i686-w64-mingw32-gcc
CFLAGS = -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -O2 -s -std=c99 -Wall -Wextra -march=i586 -mtune=i586 -mno-sse -mno-sse2 -mno-mmx
LDFLAGS = -mconsole -static-libgcc -Wl,--no-insert-timestamp -Wl,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1

IoIntegrityProbe.exe: IoIntegrityProbe.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

.PHONY: all
all: IoIntegrityProbe.exe
