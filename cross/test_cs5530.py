#!/usr/bin/env python3
"""Run the real CS5530 helpers with fake PCI config and I/O registers."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
platform = r'''
#pragma once
#include <cstdint>
#include <cstring>
typedef uint8_t UCHAR;
typedef uint16_t USHORT;
typedef uint32_t ULONG;
typedef uintptr_t ULONGIO_PTR;
typedef bool BOOLEAN;
typedef void VOID;
typedef void* PVOID;
#define TRUE true
#define FALSE false
#define PCIConfiguration 4
#define ATA_CYRIX_5530 0x01021078
#define ATA_UDMA0 0x40
#define ATA_UDMA2 0x42
#define ATA_WDMA0 0x20
#define ATA_WDMA2 0x22
#define UNIATA_CHAN_TIMINGS 0x40000000
#define RtlCopyMemory memcpy
#define KdPrint2(args) ((void)0)
struct IORES { ULONGIO_PTR Addr; bool MemIo; };
struct HW_CHANNEL {};
typedef HW_CHANNEL* PHW_CHANNEL;
struct HW_DEVICE_EXTENSION {
    ULONG DevID, BusMaster, slotNumber, SystemIoBusNumber, HwFlags;
    ULONG Channel, NumberChannels;
    IORES BaseIoAddressBM_0;
    HW_CHANNEL chan[2];
};
typedef HW_DEVICE_EXTENSION* PHW_DEVICE_EXTENSION;
ULONG HalGetBusData(ULONG, ULONG, ULONG, PVOID, ULONG);
ULONG ScsiPortSetBusDataByOffset(PVOID, ULONG, ULONG, ULONG, PVOID, ULONG, ULONG);
ULONG AtapiReadPortEx4(PHW_CHANNEL, ULONGIO_PTR, ULONG);
UCHAR AtapiReadPortEx1(PHW_CHANNEL, ULONGIO_PTR, ULONG);
VOID AtapiWritePortEx4(PHW_CHANNEL, ULONGIO_PTR, ULONG, ULONG);
VOID AtapiWritePortEx1(PHW_CHANNEL, ULONGIO_PTR, ULONG, UCHAR);
'''
harness = r'''
#include "stdafx.h"
#include "cs5530.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
static UCHAR pci[256][256];
struct Write { ULONG slot, offset, length, value; };
static std::vector<Write> writes;
static std::map<ULONGIO_PTR, ULONG> ports;
static std::vector<ULONGIO_PTR> ioWrites;
static int failWrite = -1, shortRead = -1, readCount;
static bool rejectMwi, ignoreCache;
ULONG HalGetBusData(ULONG type, ULONG bus, ULONG slot, PVOID out, ULONG length) {
    assert(type==PCIConfiguration && bus==0 && slot<256 && length<=256);
    if(readCount++==shortRead) return length ? length-1 : 0;
    memcpy(out,pci[slot],length);
    return length;
}
ULONG ScsiPortSetBusDataByOffset(PVOID, ULONG type, ULONG bus, ULONG slot,
                                PVOID in, ULONG offset, ULONG length) {
    assert(type==PCIConfiguration && bus==0 && slot<256 && offset+length<=256);
    if(int(writes.size())==failWrite) return 0;
    ULONG v=0; memcpy(&v,in,length);
    writes.push_back({slot,offset,length,v});
    if(ignoreCache && slot==18 && offset==12) return length;
    memcpy(pci[slot]+offset,in,length);
    if(rejectMwi && slot==18 && offset==4) pci[18][4]&=~0x10;
    return length;
}
static ULONGIO_PTR address(ULONGIO_PTR port, ULONG offset) {
    IORES* io=reinterpret_cast<IORES*>(port);
    assert(!io->MemIo);
    return io->Addr+offset;
}
ULONG AtapiReadPortEx4(PHW_CHANNEL, ULONGIO_PTR port, ULONG offset) {
    return ports[address(port,offset)];
}
UCHAR AtapiReadPortEx1(PHW_CHANNEL c, ULONGIO_PTR p, ULONG o) {
    return AtapiReadPortEx4(c,p,o);
}
VOID AtapiWritePortEx4(PHW_CHANNEL, ULONGIO_PTR port, ULONG offset, ULONG value) {
    auto a=address(port,offset); ports[a]=value; ioWrites.push_back(a);
}
VOID AtapiWritePortEx1(PHW_CHANNEL c, ULONGIO_PTR p, ULONG o, UCHAR v) {
    AtapiWritePortEx4(c,p,o,v);
}
static void put(ULONG slot, ULONG offset, ULONG value, ULONG length) {
    memcpy(pci[slot]+offset,&value,length);
}
static HW_DEVICE_EXTENSION setup() {
    memset(pci,0xff,sizeof(pci)); writes.clear(); ports.clear(); ioWrites.clear();
    failWrite=shortRead=-1; readCount=0; rejectMwi=ignoreCache=false;
    put(18,0,0x01001078,4); put(0,0,0x00011078,4);
    put(18,4,0x0103,2); put(18,12,0,1); put(0,0x40,0x47001000,4);
    HW_DEVICE_EXTENSION e={};
    e.DevID=ATA_CYRIX_5530; e.BusMaster=1; e.slotNumber=18+(2<<5);
    e.HwFlags=0x123; e.NumberChannels=2; e.BaseIoAddressBM_0.Addr=0xfc00;
    return e;
}
int main() {
    auto e=setup();
    assert(UniataCs5530Init(&e));
    assert(writes.size()==7);
    assert(writes[0].slot==18 && writes[0].offset==12 && writes[0].value==4);
    assert(writes[1].offset==4 && writes[1].length==2 && writes[1].value==0x0117);
    assert(writes[2].offset==0xd0 && writes[2].length==2 && writes[2].value==0x5006);
    const UCHAR master[]={0x1e,0x14,0,0xc1};
    for(unsigned i=0;i<4;++i) {
        assert(writes[3+i].slot==0 && writes[3+i].offset==0x40+i);
        assert(writes[3+i].length==1 && writes[3+i].value==master[i]);
    }
    assert(e.HwFlags==(0x123|UNIATA_CHAN_TIMINGS));
    auto reads=readCount;
    for(int n=0;n<reads;++n) {
        e=setup(); shortRead=n;
        assert(!UniataCs5530Init(&e));
        assert(!(e.HwFlags&UNIATA_CHAN_TIMINGS));
    }
    for(int n=0;n<7;++n) {
        e=setup(); failWrite=n;
        assert(!UniataCs5530Init(&e));
        assert(!(e.HwFlags&UNIATA_CHAN_TIMINGS));
    }
    e=setup(); rejectMwi=true; assert(UniataCs5530Init(&e));
    e=setup(); ignoreCache=true; assert(!UniataCs5530Init(&e));
    e=setup(); e.DevID=0x209a1022; assert(!UniataCs5530Init(&e) && writes.empty());
    e=setup(); e.BusMaster=0; assert(!UniataCs5530Init(&e) && writes.empty());
    e=setup(); put(18,0,0,4); assert(!UniataCs5530Init(&e) && writes.empty());
    e=setup(); put(0,0,0,4); assert(!UniataCs5530Init(&e) && writes.empty());

    const ULONG pios[2][5]={{0x9172,0x12171,0x20080,0x32010,0x40010},
        {0xd1329172,0x71212171,0x30200080,0x20102010,0x00100010}};
    const ULONG dmas[]={0x00921250,0x00911140,0x00911030,0x77771,0x12121,0x2020};
    unsigned cases=0;
    for(unsigned fmt=0;fmt<2;++fmt)
    for(unsigned channel=0;channel<2;++channel)
    for(unsigned split=0;split<2;++split)
    for(unsigned dev=0;dev<2;++dev)
    for(unsigned pio=0;pio<5;++pio)
    for(unsigned mode=0;mode<6;++mode) {
        e=setup();
        unsigned logical=channel;
        if(split) { e.Channel=channel; e.NumberChannels=1; logical=0; e.BaseIoAddressBM_0.Addr+=channel*8; }
        ULONG base=0xfc20+channel*16, old=(fmt<<31)|0x12345;
        ports[base+4]=old; ports[0xfc02+channel*8]=0x86;
        auto ataMode=mode<3 ? ATA_UDMA0+mode : ATA_WDMA0+mode-3;
        UniataCs5530Timing(&e,dev,logical,pio,ataMode);
        assert(ports.at(base+dev*8)==pios[fmt][pio]);
        assert(ports.at(base+dev*8+4)==(dmas[mode]|(fmt<<31)));
        assert(ports.at(0xfc02+channel*8)==(0x80u|(1u<<(5+dev))));
        if(dev) assert(ports.at(base+4)==((old&~0x100000u)|(dmas[mode]&0x100000u)));
        assert(ioWrites.size()==(dev?4u:3u)); ++cases;
    }
    e=setup();
    UniataCs5530Timing(&e,1,0,4,ATA_WDMA2);
    assert(!(ports.at(0xfc24)&0x100000));
    UniataCs5530Timing(&e,0,0,4,ATA_UDMA2);
    assert(ports.at(0xfc24)&0x100000);
    e=setup(); UniataCs5530Timing(&e,0,0,4,0x0c);
    assert(ioWrites.size()==1 && ioWrites[0]==0xfc20);
    for(int invalid=0;invalid<7;++invalid) {
        e=setup(); ULONG dev=0, logical=0, pio=4;
        switch(invalid) {
        case 0:e.DevID=0;break; case 1:dev=2;break; case 2:logical=2;break;
        case 3:pio=5;break; case 4:e.BaseIoAddressBM_0.MemIo=true;break;
        case 5:e.BaseIoAddressBM_0.Addr=0;break; case 6:e.NumberChannels=0;break;
        }
        UniataCs5530Timing(&e,dev,logical,pio,ATA_UDMA2);
        assert(ioWrites.empty());
    }
    printf("PASS CS5530 Linux helpers: PCI identity/failure/readback guards, %u timing combinations, device switching and PIO-only\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-cs5530-') as tmp:
    directory = Path(tmp)
    (directory/'stdafx.h').write_text(platform)
    (directory/'test.cpp').write_text(harness)
    for name in ('cs5530.cpp', 'cs5530.h'):
        shutil.copyfile(root/'driver'/name, directory/name)
    binary = directory/'test'
    subprocess.run(['g++', '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(directory/'test.cpp'), str(directory/'cs5530.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
