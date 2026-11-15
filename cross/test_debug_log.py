#!/usr/bin/env python3
"""Run the real KD sink against a fake NT DbgPrint; no UART backend exists."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
platform = '\n#pragma once\n#include <cstdint>\n#include <cstdarg>\n#include <cstdio>\n#include <cstring>\n#define __cdecl\ntypedef uint8_t UCHAR;\ntypedef char CHAR;\ntypedef uint8_t BOOLEAN;\ntypedef uint32_t ULONG;\ntypedef int32_t LONG;\ntypedef uintptr_t ULONG_PTR;\ntypedef void VOID;\ntypedef UCHAR* PUCHAR;\ntypedef const CHAR* PCCHAR;\n#define TRUE 1\n#define FALSE 0\n#define _snprintf snprintf\nint TestVsnprintf(char* buffer, size_t size, const char* format, va_list args);\n#define _vsnprintf TestVsnprintf\n#define InterlockedCompareExchange(p, n, old) __sync_val_compare_and_swap(p, old, n)\n#define InterlockedExchange(p, n) __sync_lock_test_and_set(p, n)\nextern BOOLEAN InDriverEntry;\nULONG __cdecl DbgPrint(const CHAR*, ...);\nULONG __cdecl UniataDebugPrint(const CHAR*, ...);\n'
harness = r'''
#include "stdafx.h"
#include <string>
#include <cassert>
BOOLEAN InDriverEntry=TRUE;
static std::string output;
static unsigned formats;
static bool reenter;
int TestVsnprintf(char* p, size_t n, const char* f, va_list a) {
    ++formats; return vsnprintf(p,n,f,a);
}
ULONG DbgPrint(const CHAR* f, ...) {
    char b[512]; va_list a; va_start(a,f);
    int n=vsnprintf(b,sizeof(b),f,a); va_end(a);
    assert(n>=0 && n<(int)sizeof(b)); output.append(b,n);
    if(reenter) { reenter=false; UniataDebugPrint("REENTRANT\n"); }
    return 0;
}
int main() {
    reenter=true;
    UniataDebugPrint("first %s\n", "100% literal %s");
    assert(output.find("[UniATA]")!=std::string::npos);
    assert(output.find("100% literal %s")!=std::string::npos);
    assert(output.find("REENTRANT")==std::string::npos);
    UniataDebugPrint("%s",std::string(1024,'X').c_str());
    assert(output.find("...[cut]\n")!=std::string::npos);
    InDriverEntry=FALSE;
    UniataDebugPrint("Leave UNIATA MiniPort DriverEntry with status 0\n");
    unsigned before=formats;
    for(unsigned i=0;i<400000;++i) UniataDebugPrint("ordinary noise %u\n",i);
    assert(formats==before && output.find("ordinary noise")==std::string::npos);
    for(unsigned i=0;i<256;++i) {
        UniataDebugPrint("AtaCommand48: cntrlr 0:0 dev 0, cmd 0xc8, lba %u\n",i);
        UniataDebugPrint("AtapiDmaStart: lchan=0\n");
        UniataDebugPrint("AtapiInterrupt: CompleteRequest, srbstatus 1\n");
        UniataDebugPrint("AtapiInterrupt: RequestComplete, srb 0x123\n");
    }
    assert(output.find("srb_done=256")!=std::string::npos);
    assert(output.find("cmd=256 dma_start=256 irq_done=256")!=std::string::npos);
    for(unsigned i=0;i<8192;++i) UniataDebugPrint("DMA timeout %u\n",i);
    assert(output.find("DMA timeout 0\n")!=std::string::npos);
    assert(output.find("DMA timeout 8191\n")!=std::string::npos);
    assert(output.size()<30000);
    puts("PASS KD sink: real DbgPrint routing, reentry guard, bounded formatting, compact counters/errors; no UART implementation linked");
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-kd-') as tmp:
    p=Path(tmp); (p/'stdafx.h').write_text(platform); (p/'test.cpp').write_text(harness)
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',

                    '-I'+tmp,str(root/'cross/debug_log.cpp'),str(p/'test.cpp'),
                    '-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
