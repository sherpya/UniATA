#!/usr/bin/env python3
"""Exercise the production CS5536 backend with modeled PCI config and ATA commands."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
platform = r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <vector>
typedef uint8_t UCHAR;
typedef int8_t SCHAR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef bool BOOLEAN;
typedef void VOID;
typedef void* PVOID;
#define TRUE true
#define FALSE false
#define NTAPI
#define min(a,b) ((a)<(b)?(a):(b))
#define KdPrint2(args) ((void)0)
#define ATA_AMD5536 0x209a1022
#define ATA_UDMA0 0x40
#define ATA_UDMA2 0x42
#define ATA_UDMA5 0x45
#define ATA_WDMA0 0x20
#define ATA_WDMA2 0x22
#define ATA_PIO0 0x08
#define PCIConfiguration 4
#define DFLAGS_DEVICE_PRESENT 1
struct PCI_COMMON_CONFIG {};
struct HW_LU_EXTENSION { ULONG DeviceFlags, PataPioMode; };
typedef HW_LU_EXTENSION* PHW_LU_EXTENSION;
struct HW_CHANNEL { ULONG MaxTransferMode; PHW_LU_EXTENSION lun[2]; };
struct HW_DEVICE_EXTENSION {
    ULONG DevID, Channel, NumberChannels, SystemIoBusNumber, slotNumber, MaxTransferMode;
    HW_CHANNEL chan[1];
};
typedef HW_DEVICE_EXTENSION* PHW_DEVICE_EXTENSION;
ULONG HalGetBusData(ULONG,ULONG,ULONG,PVOID,ULONG);
ULONG ScsiPortSetBusDataByOffset(PVOID,ULONG,ULONG,ULONG,PVOID,ULONG,ULONG);
'''
harness=r'''
#include "stdafx.h"
#include "controller_quirks.h"
#include "cs5536.h"
#include <cassert>
#include <cstdio>
#include <vector>
static ULONG pci[64];
struct Write { ULONG offset, value; };
static std::vector<Write> writes;
static int readCalls, failRead, failWrite, ignoredWrite, commands;
static bool acceptCommand;
static HW_LU_EXTENSION luns[2];
ULONG HalGetBusData(ULONG type,ULONG bus,ULONG slot,PVOID buffer,ULONG length) {
    assert(type==PCIConfiguration && bus==0 && slot==15+(2<<5));
    assert(length<=256 && !(length&3));
    if(readCalls++==failRead) return length-1;
    memcpy(buffer,pci,length); return length;
}
ULONG ScsiPortSetBusDataByOffset(PVOID,ULONG type,ULONG bus,ULONG slot,
                               PVOID buffer,ULONG offset,ULONG length) {
    assert(type==PCIConfiguration && bus==0 && slot==15+(2<<5));
    assert(length==4 && (offset==0x48 || offset==0x4c || offset==0x50));
    ULONG value; memcpy(&value,buffer,4);
    int index=writes.size(); writes.push_back({offset,value});
    if(index==failWrite) return 0;
    if(index!=ignoredWrite) pci[offset/4]=value;
    return 4;
}
BOOLEAN NTAPI AtaSetTransferMode(PHW_DEVICE_EXTENSION e,ULONG d,ULONG c,
                                PHW_LU_EXTENSION lun,ULONG) {
    assert(d<2 && c==0 && lun==e->chan[0].lun[d]);
    ++commands; return acceptCommand;
}
static HW_DEVICE_EXTENSION setup() {
    for(unsigned i=0;i<64;++i) pci[i]=0xa5a55a5a;
    pci[0]=ATA_AMD5536; pci[0x40/4]=0x30002;
    writes.clear(); readCalls=commands=0; failRead=failWrite=ignoredWrite=-1;
    acceptCommand=true; memset(luns,0,sizeof(luns));
    HW_DEVICE_EXTENSION e={}; e.DevID=ATA_AMD5536; e.NumberChannels=1;
    e.slotNumber=15+(2<<5); e.MaxTransferMode=0x46; e.chan[0].MaxTransferMode=0x46;
    e.chan[0].lun[0]=&luns[0]; e.chan[0].lun[1]=&luns[1];
    return e;
}
int main() {
    auto e=setup(); assert(UniataCs5536Init(&e));
    assert(e.MaxTransferMode==0x45 && e.chan[0].MaxTransferMode==0x45 && writes.empty());
    for(unsigned cable: {0u,0x10000u,0x20000u,0x30000u}) {
        e=setup(); pci[0x40/4]=cable|2; assert(UniataCs5536Init(&e));
        assert(e.MaxTransferMode==(cable?0x45u:0x42u));
    }
    e=setup(); e.MaxTransferMode=e.chan[0].MaxTransferMode=0x22;
    assert(UniataCs5536Init(&e) && e.MaxTransferMode==0x22 && e.chan[0].MaxTransferMode==0x22);
    for(unsigned bad=0;bad<7;++bad) {
        e=setup();
        switch(bad) {case 0:e.DevID=0;break;case 1:e.Channel=1;break;
        case 2:e.NumberChannels=2;break;case 3:pci[0]=0;break;
        case 4:pci[0x40/4]&=~2u;break;case 5:failRead=0;break;case 6:failRead=1;break;}
        assert(!UniataCs5536Init(&e) && writes.empty() && commands==0);
    }
    const unsigned pio[]={0x98,0x55,0x32,0x21,0x20},addr[]={2,1,0,0,0};
    const unsigned cmd[]={0x99,0x92,0x90,0x22,0x20};
    const unsigned u[]={0xc2,0xc1,0xc0,0xc4,0xc5,0xc6},mw[]={0x67,0x21,0x20};
    unsigned cases=0;
    for(unsigned d=0;d<2;++d) for(unsigned p=0;p<5;++p)
    for(unsigned peer=0;peer<6;++peer) for(unsigned m=0;m<10;++m) {
        e=setup();
        if(peer<5) { luns[d^1].DeviceFlags=DFLAGS_DEVICE_PRESENT; luns[d^1].PataPioMode=peer; }
        ULONG before[64]; memcpy(before,pci,sizeof(pci));
        unsigned mode=m<6?0x40+m:m<9?0x20+m-6:8+p;
        assert(UniataCs5536SetMode(&e,d,0,p,mode));
        unsigned shift=d?16:24,ashift=d?4:6;
        unsigned timing=m<6||m==9?pio[p]:mw[m-6];
        unsigned command=peer<5?min(p,peer):p;
        unsigned dtc=(before[18]&~(0xffu<<shift))|(timing<<shift);
        unsigned cast=(before[19]&~((3u<<ashift)|0xff000000u))|(addr[p]<<ashift)|(cmd[command]<<24);
        unsigned etc=m<6?(before[20]&~(0xffu<<shift))|(u[m]<<shift):before[20]&~(0xc0u<<shift);
        assert(pci[18]==dtc && pci[19]==cast && pci[20]==etc);
        for(unsigned i=0;i<64;++i) if(i!=18 && i!=19 && i!=20) assert(pci[i]==before[i]);
        assert(writes.size()==3 && commands==1 && luns[d].PataPioMode==p);
        ++cases;
    }
    // A request rejected by the disk restores the previous controller state.
    e=setup(); acceptCommand=false; ULONG before[64]; memcpy(before,pci,sizeof(pci));
    assert(!UniataCs5536SetMode(&e,0,0,4,0x45) && commands==1);
    assert(memcmp(before,pci,sizeof(pci))==0);
    // A partial/ignored config write must not enable the requested mode on disk.
    for(int n=0;n<3;++n) for(int kind=0;kind<2;++kind) {
        e=setup(); memcpy(before,pci,sizeof(pci));
        if(kind) ignoredWrite=n; else failWrite=n;
        assert(!UniataCs5536SetMode(&e,0,0,4,0x45) && commands==0);
        assert(memcmp(before,pci,sizeof(pci))==0);
    }
    for(int n=0;n<7;++n) { // four initial reads and three write readbacks
        e=setup(); failRead=n;
        assert(!UniataCs5536SetMode(&e,0,0,4,0x45) && commands==0);
    }
    for(unsigned bad=0;bad<8;++bad) {
        e=setup(); unsigned d=0,c=0,p=4,m=0x45;
        switch(bad) {case 0:e.DevID=0;break;case 1:e.Channel=1;break;
        case 2:c=1;break;case 3:d=2;break;case 4:p=5;break;
        case 5:m=0x46;break;case 6:pci[0x40/4]=2;break;case 7:pci[0x40/4]=0;break;}
        assert(!UniataCs5536SetMode(&e,d,c,p,m) && writes.empty() && commands==0);
    }
    // UDMA-to-PIO transition clears enable bits, preserving the other drive.
    e=setup(); assert(UniataCs5536SetMode(&e,0,0,4,0x45));
    assert(UniataCs5536SetMode(&e,0,0,4,0x0c));
    assert(!(pci[20]&0xc0000000));
    printf("PASS CS5536: %u timing/peer combinations, aligned DWORD writes, cable/channel caps, rollback and failure guards\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-cs5536-') as tmp:
    path=Path(tmp)
    (path/'stdafx.h').write_text(platform)
    (path/'test.cpp').write_text(harness)
    for name in ('cs5536.cpp','cs5536.h','controller_quirks.h'):
        shutil.copyfile(root/'driver'/name,path/name)
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                    str(path/'test.cpp'),str(path/'cs5536.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
