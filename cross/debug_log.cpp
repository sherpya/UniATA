/* Compact diagnostics through NT DbgPrint. No UART initialization or I/O:
 * COM1 belongs exclusively to the Windows kernel debugger transport.
 */
#include "stdafx.h"
#undef DbgPrint
#define UNIATA_DEBUG_TAG "[UniATA]"

static volatile LONG DebugBusy;
static BOOLEAN DebugInitialized;

static BOOLEAN DebugText(const CHAR* text, ULONG length)
{
    // Messages are bounded below; a literal format prevents reinterpreting %.
    DbgPrint("%.*s", (int)length, text);
    return TRUE;
}

#include "debug_filter.h"

ULONG __cdecl UniataDebugPrint(const CHAR* format, ...)
{
    if(InterlockedCompareExchange(&DebugBusy, 1, 0) != 0)
        return 0;
    if(!DebugInitialized) {
        DebugInitialized = TRUE;
        const CHAR banner[] = "[UniATA] compact NT debug logging\n";
        DebugText(banner, sizeof(banner)-1);
    }
    DEBUG_COMPACT_KIND kind = DebugCompactClassify(format);
    if(kind == DebugDrop) {
        DebugCompactSkipped();
    } else {
        CHAR buffer[256] = {0};
        va_list args;
        va_start(args, format);
        int length = _vsnprintf(buffer, sizeof(buffer)-1, format, args);
        va_end(args);
        buffer[sizeof(buffer)-1] = 0;
        if(length < 0 || length >= (int)sizeof(buffer)-1) {
            const CHAR suffix[] = "...[cut]\n";
            memcpy(buffer+sizeof(buffer)-sizeof(suffix), suffix, sizeof(suffix));
            length = sizeof(buffer)-1;
        }
        if(DebugCompactFormatted(kind, format, buffer, (ULONG)length))
            DebugText(buffer, (ULONG)length);
        else
            DebugCompactSkipped();
    }
    InterlockedExchange(&DebugBusy, 0);
    return 0;
}
