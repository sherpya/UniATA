/* Version globals normally supplied by CrossNtK.lib. This build targets XP. */
#include "stdafx.h"
extern "C" {
ULONG MajorVersion = 0;
ULONG MinorVersion = 0;
ULONG BuildNumber = 0;
ULONG SPVersion = 0;

/* These compiler/CRT helpers are not exported by the XP kernel. */
unsigned long __cdecl _byteswap_ulong(unsigned long value)
{
    return __builtin_bswap32(value);
}

unsigned short __cdecl _byteswap_ushort(unsigned short value)
{
    return __builtin_bswap16(value);
}
}
