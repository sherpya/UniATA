/* Compatibility for the MinGW-w64 DDK headers used by this XP x86 build. */
#ifndef UNIATA_CROSS_COMPAT_H
#define UNIATA_CROSS_COMPAT_H

/* wdm.h supplies these, while intrin-impl.h would also define them in C++. */
#define __INTRINSIC_DEFINED_InterlockedBitTestAndSet
#define __INTRINSIC_DEFINED_InterlockedBitTestAndReset

/* MinGW wdm.h uses this Vista type unconditionally in IO_STACK_LOCATION,
 * but declares it only with Vista or later NTDDI_VERSION. Its ABI is 4 bytes.
 * The XP build does not access this power-manager field. */
#include <ntdef.h>
/* wdm.h redeclares the same value with a different token spelling. */
#undef FILE_SHARE_VALID_FLAGS
/* XP exports the original three-argument/varargs swprintf, not _vswprintf.
 * Suppress MinGW's C++ overloads, which would pull in the latter CRT symbol. */
#define _INC_SWPRINTF_INL
extern "C" int __cdecl swprintf(wchar_t *buffer, const wchar_t *format, ...);
#if NTDDI_VERSION < 0x06000000
typedef struct _SYSTEM_POWER_STATE_CONTEXT {
    union {
        struct {
            ULONG Reserved1 : 8;
            ULONG TargetSystemState : 4;
            ULONG EffectiveSystemState : 4;
            ULONG CurrentSystemState : 4;
            ULONG IgnoreHibernationPath : 1;
            ULONG PseudoTransition : 1;
            ULONG Reserved2 : 10;
        };
        ULONG ContextAsUlong;
    };
} SYSTEM_POWER_STATE_CONTEXT, *PSYSTEM_POWER_STATE_CONTEXT;
#endif
#endif
