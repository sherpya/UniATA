/* Included by debug_log.cpp after DebugText; protected by DebugBusy.
 * Sample existing diagnostics without adding logging to the I/O path.
 */
enum DEBUG_COMPACT_KIND {
    DebugDrop = -1, DebugFull, DebugCommand, DebugIrqDone,
    DebugSyncDone, DebugEvent
};
static BOOLEAN DebugRuntime = FALSE;
static ULONG DebugCommands, DebugDmaStarts, DebugIrqCompletions;
static ULONG DebugSyncCompletions, DebugEvents, DebugSuppressed;
static ULONG DebugSrbCompletions;
static CHAR DebugLastCommand[256];
static ULONG DebugLastCommandLength;
struct DEBUG_EVENT_SAMPLE { const CHAR* format; ULONG count; };
static DEBUG_EVENT_SAMPLE DebugEventSamples[64];
static ULONG DebugNextEventSlot;

static BOOLEAN DebugStarts(const CHAR* value, const CHAR* prefix)
{
    while(*prefix) {
        if(*value++ != *prefix++) return FALSE;
    }
    return TRUE;
}

static CHAR DebugLower(CHAR c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static BOOLEAN DebugContains(const CHAR* text, const CHAR* word)
{
    for(; *text; text++) {
        const CHAR* p = text;
        const CHAR* q = word;
        while(*p && *q && DebugLower(*p) == DebugLower(*q)) { p++; q++; }
        if(!*q) return TRUE;
    }
    return FALSE;
}

static VOID DebugSummary()
{
    CHAR text[256];
    int n = _snprintf(text, sizeof(text),
        UNIATA_DEBUG_TAG " progress cmd=%lu dma_start=%lu irq_done=%lu sync_done=%lu events=%lu suppressed=%lu"
        " srb_done=%lu"
        "\n",
        (unsigned long)DebugCommands, (unsigned long)DebugDmaStarts,
        (unsigned long)DebugIrqCompletions, (unsigned long)DebugSyncCompletions,
        (unsigned long)DebugEvents, (unsigned long)DebugSuppressed
        , (unsigned long)DebugSrbCompletions
        );
    if(n > 0 && n < (int)sizeof(text) && DebugText(text, (ULONG)n) && DebugLastCommandLength)
        DebugText(DebugLastCommand, DebugLastCommandLength);
}

static VOID DebugCompactSkipped()
{
    DebugSuppressed++;
    // Also expose activity in a log/IRQ loop with no completed disk I/O.
    if(DebugSuppressed && !(DebugSuppressed & 0x3ffff)) DebugSummary();
}

static DEBUG_COMPACT_KIND DebugCompactClassify(const CHAR* format)
{
    if(InDriverEntry) return DebugFull;
    if(!DebugRuntime) {
        DebugRuntime = TRUE;
        const CHAR text[] = UNIATA_DEBUG_TAG " compact runtime: first 8, then every 256 commands/completions; repeated events sampled\n";
        DebugText(text, sizeof(text)-1);
    }
    if(DebugContains(format, "Leave UNIATA MiniPort DriverEntry") ||
       DebugStarts(format, "[UniATA]"))
        return DebugFull;
    if(DebugStarts(format, "AtaCommand48: cntrlr ")) {
        DebugCommands++;
        return DebugCommand;
    }
    if(DebugStarts(format, "AtapiDmaStart: lchan=")) {
        DebugDmaStarts++;
        return DebugDrop;
    }
    if(DebugStarts(format, "AtapiInterrupt: RequestComplete, srb ")) {
        DebugSrbCompletions++;
        if(DebugSrbCompletions <= 8 || !(DebugSrbCompletions & 0xff)) DebugSummary();
        return DebugDrop;
    }
    if(DebugStarts(format, "AtapiInterrupt: CompleteRequest, srbstatus ")) {
        DebugIrqCompletions++;
        return DebugIrqDone;
    }
    if(DebugStarts(format, "AtapiStartIo: Srb ")) {
        DebugSyncCompletions++;
        return DebugSyncDone;
    }
    if(DebugStarts(format, "AtaSetTransferMode:") ||
       DebugStarts(format, "AtapiDmaReinit: set ") ||
       DebugStarts(format, "AtapiDmaReinit: restore ") ||
       DebugStarts(format, "AtapiFind") || DebugStarts(format, "UniataFind") ||
       DebugStarts(format, "AtapiHwInitialize:") ||
       DebugContains(format, "error") || DebugContains(format, "failed") ||
       DebugContains(format, "timeout") || DebugContains(format, "timed out") ||
       DebugContains(format, "reset") || DebugContains(format, "abort") ||
       DebugContains(format, "invalid") || DebugContains(format, "overrun") ||
       DebugContains(format, "unexpected") || DebugContains(format, "retry") ||
       DebugContains(format, "downrate") || DebugContains(format, "BM_STATUS_ERR") ||
       DebugContains(format, "SRB_STATUS_") || DebugContains(format, "!AtaReq"))
        return DebugEvent;
    return DebugDrop;
}

static BOOLEAN DebugSampleCount(ULONG count)
{
    return count <= 8 || !(count & 0xff);
}

static BOOLEAN DebugSampleEvent(const CHAR* format)
{
    ULONG slot;
    for(slot=0; slot<64; slot++) {
        if(DebugEventSamples[slot].format == format) break;
    }
    if(slot == 64) {
        slot = DebugNextEventSlot++ & 63;
        DebugEventSamples[slot].format = format;
        DebugEventSamples[slot].count = 0;
    }
    ULONG count = ++DebugEventSamples[slot].count;
    // Preserve first occurrences, but prevent a repeating error/IRQ storm
    // from turning back into an unbounded per-interrupt serial trace.
    return count <= 4 || !(count & (count - 1));
}

static BOOLEAN DebugCompactFormatted(DEBUG_COMPACT_KIND kind,
                                      const CHAR* format, const CHAR* text, ULONG length)
{
    if(kind == DebugFull) return TRUE;
    if(kind == DebugCommand) {
        memcpy(DebugLastCommand, text, length);
        DebugLastCommandLength = length;
        if(DebugSampleCount(DebugCommands)) DebugSummary();
        return FALSE;
    }
    if((kind == DebugIrqDone && DebugContains(text, "srbstatus 1\n")) ||
       (kind == DebugSyncDone && DebugContains(text, "complete with status 0x1\n"))) {
        ULONG count = (kind == DebugIrqDone) ? DebugIrqCompletions : DebugSyncCompletions;
        if(DebugSampleCount(count)) DebugSummary();
        return FALSE;
    }
    // A non-success completion is retained even though its format has no
    // error keyword. Some such statuses are expected unsupported requests.
    DebugEvents++;
    if(!DebugSampleEvent(format)) return FALSE;
    DebugSummary();
    return TRUE;
}
