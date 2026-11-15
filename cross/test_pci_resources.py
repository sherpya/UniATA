#!/usr/bin/env python3
"""Exercise the production legacy PCI claim against allocation-failure stubs."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'driver/id_probe.cpp').read_bytes().decode('latin1')
a = s.index('NTSTATUS\nNTAPI\nUniataClaimLegacyPCIIDE(')
b = s.index('} // end UniataClaimLegacyPCIIDE()', a) + 1
function = s[a:b]
platform = r"""
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <set>
#include <cstdio>
typedef int32_t NTSTATUS;
typedef unsigned long ULONG;
#define NTAPI
#define KdPrint2(args) ((void)0)
#define NT_SUCCESS(x) ((x)>=0)
#define STATUS_UNSUCCESSFUL -1
#define STATUS_INSUFFICIENT_RESOURCES -2
#define PagedPool 1
#define FILE_DEVICE_UNKNOWN 0
#define FALSE 0
#define PCIBus 5
struct CM_RESOURCE_LIST {
    unsigned Count;
    struct { unsigned InterfaceType, BusNumber;
        struct { unsigned Count; } PartialResourceList; } List[1];
};
typedef CM_RESOURCE_LIST* PCM_RESOURCE_LIST;
struct UNICODE_STRING {};
struct PCIIDE_DEVICE_EXTENSION {};
struct BM { void* PciIdeDevObj; unsigned busNumber, slotNumber, ChanInitOk; };
BM BMList[1];
void* SavedDriverObject;
UNICODE_STRING SavedRegPath;
static int scenario;
static unsigned deletes, halCalls;
static std::set<void*> allocations;
void RtlInitUnicodeString(UNICODE_STRING*, const wchar_t*) {}
void RtlZeroMemory(void* p, size_t n) { memset(p,0,n); }
void* ExAllocatePool(int, size_t n) {
    if(scenario==2) return NULL;
    void* p=malloc(n); assert(p); allocations.insert(p); return p;
}
void ExFreePool(void* p) { assert(allocations.erase(p)==1); free(p); }
NTSTATUS IoCreateDevice(void*, size_t, UNICODE_STRING*, int, int, int, void** out) {
    if(scenario==1) return -3;
    *out=reinterpret_cast<void*>(0x1234); return 0;
}
void IoDeleteDevice(void* p) { assert(p==reinterpret_cast<void*>(0x1234)); ++deletes; }
NTSTATUS HalAssignSlotResources(UNICODE_STRING*, UNICODE_STRING*, void*, void*,
                               int, unsigned bus, unsigned slot, PCM_RESOURCE_LIST* out) {
    ++halCalls;
    assert(bus==0 && slot==82 && *out && (*out)->Count==1);
    assert((*out)->List[0].InterfaceType==PCIBus);
    assert((*out)->List[0].PartialResourceList.Count==0);
    if(scenario==3) { *out=NULL; return -4; }
    if(scenario==4) return -4; // failure before replacing the caller's input
    if(scenario==5) return 0; // unusual HAL retaining input on success
    if(scenario==6) { *out=NULL; return 0; }
    *out=static_cast<PCM_RESOURCE_LIST>(ExAllocatePool(PagedPool,sizeof(CM_RESOURCE_LIST)));
    return 0;
}
"""
harness = r"""
int main() {
    for(scenario=0;scenario<7;++scenario) {
        BMList[0]={NULL,0,82,0}; deletes=halCalls=0;
        NTSTATUS s=UniataClaimLegacyPCIIDE(0);
        bool success=(scenario==0 || scenario>=5);
        assert(NT_SUCCESS(s)==success);
        assert(allocations.empty());
        assert(deletes==((scenario>=2 && scenario<=4)?1u:0u));
        assert(halCalls==((scenario==1 || scenario==2)?0u:1u));
        assert(BMList[0].ChanInitOk==(success?0x80u:0u));
        if(success) {
            assert(UniataClaimLegacyPCIIDE(0)==STATUS_UNSUCCESSFUL);
            assert(allocations.empty() && halCalls==1);
        } else assert(BMList[0].PciIdeDevObj==NULL);
    }
    puts("PASS PCI claim: success, alias/null output, allocation/HAL failures and repeat call; no leaks/double frees");
}
"""
with tempfile.TemporaryDirectory(prefix='uniata-resources-') as tmp:
    source=Path(tmp)/'test.cpp'; source.write_text(platform+function+harness)
    exe=Path(tmp)/'test'
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                    str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
