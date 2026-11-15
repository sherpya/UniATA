#!/usr/bin/env python3
"""Exercise the actual PRD builder, accounting and next-chunk invalidation.

Hardware DMA is simulated using the generated PRDs, with a patterned disk
and buffer guards. Both physical layouts and READ/WRITE are tested.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'driver/id_dma.cpp').read_bytes().decode('latin1')
start = source.index('BOOLEAN\nNTAPI\nAtapiDmaSetup(')
end = source.index('} // end AtapiDmaSetup()', start) + 1
builder = source[start:end]
platform = r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#define min(a,b) ((a)<(b)?(a):(b))
typedef uint8_t UCHAR, *PUCHAR;
typedef uint32_t ULONG, *PULONG;
typedef uintptr_t ULONG_PTR;
typedef int32_t LONG;
typedef uint64_t ULONGLONG;
typedef bool BOOLEAN;
typedef void VOID, *PVOID;
#define IN
#define NTAPI
#define TRUE true
#define FALSE false
#define PAGE_SIZE 4096u
#define PAGE_MASK 4095u
#define ATA_DMA_ENTRIES 256u
#define ATA_AHCI_DMA_ENTRIES 128u
#define ATA_DMA_EOT 0x80000000u
#define ATA_DMA 0x10
#define UNIATA_AHCI 0x100
#define REQ_FLAG_DMA_OPERATION 1
#define REQ_FLAG_DMA_DBUF 0x20
#define REQ_FLAG_DMA_DBUF_PRD 0x40
#define PRINT_PREFIX ""
#define KdPrint2(args) ((void)0)
#define ASSERT assert
struct PRD { ULONG base, count; };
struct AHCI_PRD { ULONG base, baseu, Reserved1, DBC_ULONG, DBC; };
struct AHCI_CMD { AHCI_PRD prd_tab[128]; };
struct ATA_REQ {
    ULONG WordsTransfered, WordsLeft, dma_entries, Flags;
    struct { ULONG dma_base; } ata;
    struct { AHCI_CMD* ahci_cmd_ptr; ULONGLONG ahci_base64; } ahci;
    PRD dma_tab[256];
};
typedef ATA_REQ* PATA_REQ;
struct SRB { PATA_REQ SrbExtension; };
typedef SRB* PSCSI_REQUEST_BLOCK;
struct LUN { ULONG TransferMode; };
struct CHANNEL {
    LUN* lun[2]; ULONG DB_PRD_PhAddr, DB_IO_PhAddr; PUCHAR DB_IO;
};
typedef CHANNEL* PHW_CHANNEL;
struct EXT {
    ULONG HwFlags, DmaSegmentLength, DmaSegmentAlignmentMask;
    ULONG MaximumDmaTransferLength, AlignmentMask; bool Host64; CHANNEL chan[1];
};
typedef EXT* PHW_DEVICE_EXTENSION;
alignas(4096) static UCHAR memory[512*1024];
static bool fragmented;
static ULONG physical(PUCHAR p) {
    uintptr_t offset=p-memory;
    assert(offset<sizeof(memory));
    return 0x100000+(offset/4096)*(fragmented?8192:4096)+offset%4096;
}
static PUCHAR virtualAddress(ULONG pa) {
    ULONG offset=pa-0x100000, stride=fragmented?8192:4096;
    assert(offset%stride<4096);
    return memory+(offset/stride)*4096+offset%stride;
}
static ULONG AtapiVirtToPhysAddr(PVOID, PSCSI_REQUEST_BLOCK srb, PUCHAR p,
                                PULONG count, PULONG high) {
    *high=0;
    if(!srb) { *count=2048; return 0x80000; }
    ULONG pa=physical(p); *count=4096-(pa&4095); return pa;
}
#include "dma_account.h"
#include "dma_chunk.h"
'''
harness = r'''
static unsigned char pattern(unsigned n) {
    return ((n*13) ^ (n>>8) ^ (n>>12) ^ (n>>16))&255;
}
static bool transfer(unsigned bytes, unsigned offset, bool write) {
    memset(memory,0xa5,sizeof(memory));
    PUCHAR buffer=memory+4096+offset;
    std::vector<UCHAR> disk(bytes+8192,0x5a);
    for(unsigned n=0;n<bytes;++n) {
        if(write) buffer[n]=pattern(n);
        else disk[4096+n]=pattern(n);
    }
    ATA_REQ req={}; SRB srb={&req}; LUN lun={0x42}; EXT ext={};
    ext.DmaSegmentLength=65536; ext.DmaSegmentAlignmentMask=65535;
    ext.MaximumDmaTransferLength=63*1024; ext.AlignmentMask=15;
    ext.chan[0].lun[0]=&lun;
    unsigned diskOffset=4096, chunks=0;
    while(req.WordsTransfered*2<bytes) {
        unsigned position=req.WordsTransfered*2;
        if(position) UniataPrepareNextDmaChunk(&req);
        unsigned count=min(bytes-position,63u*1024);
        req.WordsLeft=count/2;
        assert(AtapiDmaSetup(&ext,0,0,&srb,buffer+position,count));
        unsigned remaining=count;
        for(unsigned i=0;i<req.dma_entries && remaining;++i) {
            auto d=req.dma_tab[i]; unsigned n=d.count&65535;
            assert(n); // the CS5530 cannot use zero for a 64-KiB PRD
            n=min(n,remaining);
            assert((d.base&65535)+n<=65536);
            // Simulate the device transferring only the ATA command length,
            // even if an obsolete PRD describes a longer DMA buffer.
            for(unsigned k=0;k<n;++k) {
                PUCHAR p=virtualAddress(d.base+k);
                assert(p>=buffer && p<buffer+bytes);
                if(write) disk[diskOffset+k]=*p;
                else *p=disk[diskOffset+k];
            }
            diskOffset+=n; remaining-=n;
        }
        assert(!remaining);
        UniataAccountDmaTransfer(&req,false);
        req.Flags&=~REQ_FLAG_DMA_OPERATION;
        assert(++chunks<=16);
    }
    assert(diskOffset==4096+bytes);
    for(unsigned n=0;n<4096+offset;++n) assert(memory[n]==0xa5);
    for(unsigned n=4096+offset+bytes;n<sizeof(memory);++n) assert(memory[n]==0xa5);
    for(unsigned n=0;n<4096;++n) assert(disk[n]==0x5a && disk[4096+bytes+n]==0x5a);
    bool correct=true;
    for(unsigned n=0;n<bytes;++n)
        if((write?disk[4096+n]:buffer[n])!=pattern(n)) correct=false;
    return correct;
}
int main() {
    unsigned cases=0;
    for(unsigned frag=0;frag<2;++frag)
    for(unsigned write=0;write<2;++write)
    for(unsigned offset: {0u,16u,512u,4080u}) {
        fragmented=frag;
        for(unsigned size: {512u,4096u,63u*1024,63u*1024+512,64u*1024,128u*1024,256u*1024}) {
            bool ok=transfer(size,offset,write);
#if TEST_FIXED
            assert(ok);
#else
            assert(ok==(size<=63u*1024));
#endif
            ++cases;
        }
    }
    printf("PASS PRD_FIX=%d: %u READ/WRITE payload cases, %s; buffer/disk guards intact\n",
        TEST_FIXED,cases,TEST_FIXED?
        "all payloads match":"stale-PRD corruption reproduced for every split request");
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-prd-') as tmp:
    p=Path(tmp)/'test.cpp'
    p.write_text(platform+builder+harness)
    for enabled in (0,1):
        helper = (root/'driver/dma_chunk.h').read_text()
        if not enabled:
            helper = helper.replace('request->dma_entries = 0;', '(void)request;')
        (Path(tmp)/'dma_chunk.h').write_text(helper)
        binary=Path(tmp)/f'test-{enabled}'
        subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                        '-Wno-conversion-null',
                        f'-DTEST_FIXED={enabled}', '-I'+tmp, '-I'+str(root/'driver'),
                        str(p),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
