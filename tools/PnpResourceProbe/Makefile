CC = i686-w64-mingw32-gcc
CPPFLAGS = -DWINVER=0x0501 -D_WIN32_WINNT=0x0501
CFLAGS = -O2 -s -std=c99 -Wall -Wextra -Werror -march=i586 -mtune=i586 -mno-sse -mno-sse2 -mno-mmx
LDFLAGS = -mconsole -static-libgcc -Wl,--no-insert-timestamp -Wl,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1
LDLIBS = -lcfgmgr32 -lsetupapi

.PHONY: all
all: PnpResourceProbe.exe

PnpResourceProbe.exe: PnpResourceProbe.c Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)
