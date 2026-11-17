#!/usr/bin/env python3
"""Exercise production quirk classification and dispatch with fake operations."""
from pathlib import Path
import subprocess
import tempfile
import shutil
root = Path(__file__).resolve().parents[1]
platform = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#define NTAPI
#define TRUE true
#define FALSE false
#define min(a,b) ((a)<(b)?(a):(b))
#define KdPrint2(args) ((void)0)
typedef uint8_t UCHAR;
typedef int8_t SCHAR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef bool BOOLEAN;
typedef void VOID;
#define PCI_ENABLE_BUS_MASTER 4
#define ATA_CYRIX_5530 0x01021078
#define ATA_AMD5536 0x209a1022
#define ATA_PIO0 0x08
#define ATA_PIO4 0x0c
#define ATA_WDMA0 0x20
#define ATA_UDMA0 0x40
#define DMA_MODE_NONE 0
struct PCI_COMMON_CONFIG {
    uint16_t VendorID, DeviceID, Command;
    uint8_t HeaderType, BaseClass, SubClass, ProgIf;
    union { struct { uint32_t BaseAddresses[6]; } type0; } u;
};
typedef PCI_COMMON_CONFIG Pci;
struct HW_LU_EXTENSION {};
typedef HW_LU_EXTENSION* PHW_LU_EXTENSION;
struct HW_CHANNEL { ULONG MaxTransferMode; PHW_LU_EXTENSION lun[2]; };
struct HW_DEVICE_EXTENSION {
    ULONG DevID, BusMaster, MaxTransferMode, NumberChannels, Channel;
    BOOLEAN UnknownDev;
    HW_CHANNEL chan[2];
};
typedef HW_DEVICE_EXTENSION* PHW_DEVICE_EXTENSION;
'''
classification = r'''
static Pci captured() {
    Pci p = {};
    p.VendorID = 0x1078; p.DeviceID = 0x0102; p.Command = 5;
    p.BaseClass = p.SubClass = 1;
    p.u.type0.BaseAddresses[4] = 0xf001;
    return p;
}
static bool original(const Pci &p) {
    return (p.ProgIf & 0x80) && ((p.ProgIf & 5) != 5);
}
static void rejected(Pci p) {
    assert(!UniataHasLegacyBusMaster(&p));
    assert(bool(IsMasterDev(&p)) == original(p));
}
static void classification() {
    // Both known compatibility-mode controllers accept ProgIf 0 or 0x80 only.
    const uint16_t vendors[] = {0, 0x1078, 0x8086, 0x1022, 0xffff};
    const uint16_t devices[] = {0, 2, 0x0100, 0x0102, 0x0103, 0x208f, 0x209a, 0xffff};
    for (auto v : vendors) for (auto d : devices) for (unsigned pi=0; pi<256; ++pi) {
        Pci p = captured(); p.VendorID=v; p.DeviceID=d; p.ProgIf=pi;
        bool quirk = ((v==0x1078 && d==0x0102) || (v==0x1022 && d==0x209a)) && (pi==0 || pi==0x80);
        assert(bool(UniataHasLegacyBusMaster(&p)) == quirk);
        assert(bool(IsMasterDev(&p)) == (original(p) || quirk));
    }
    // Header shape, legacy BARs and I/O BAR bounds must all match.
    Pci p = captured(); p.HeaderType=1; rejected(p);
    p=captured(); p.BaseClass=6; rejected(p);
    p=captured(); p.SubClass=0x80; rejected(p);
    for (unsigned i=0; i<4; ++i) {
        p=captured(); p.u.type0.BaseAddresses[i]=0x1f1; rejected(p);
    }
    const uint32_t invalid[] = {0, 1, 0x10, 0xf000, 0xf003, 0xf005,
                                0xf009, 0x10001, 0xfffffff1, 0xffffffff};
    for (auto bar : invalid) {
        p=captured(); p.u.type0.BaseAddresses[4]=bar; rejected(p);
    }
    const uint32_t valid[] = {0x11, 0xc001, 0xf001, 0xfff1};
    for (auto bar : valid) {
        p=captured(); p.u.type0.BaseAddresses[4]=bar; p.HeaderType=0x80;
        assert(bool(IsMasterDev(&p)) == bool(true));
    }
    // Compatibility classification does not itself enable PCI DMA.
    // The existing probe still uses the separate Command-bit predicate.
    for (unsigned command=0; command<8; ++command) {
        p=captured(); p.Command=command;
        assert(bool(IsMasterDev(&p)) == bool(true));
        assert(bool(IsBusMaster(&p)) == bool(command & 4));
    }
}
'''
harness = r'''
#include "stdafx.h"
#include "controller_quirks.h"
#include "cs5530.h"
#include <cassert>
#include <vector>
#include <cstdio>
static bool initSuccess;
static unsigned initCalls, timingCalls, acceptedMode, lastMode, lastPio;
static std::vector<unsigned> attempts;
BOOLEAN UniataCs5530Init(PHW_DEVICE_EXTENSION) { ++initCalls; return initSuccess; }
BOOLEAN UniataCs5536Init(PHW_DEVICE_EXTENSION) { ++initCalls; return initSuccess; }
BOOLEAN UniataCs5536SetMode(PHW_DEVICE_EXTENSION, ULONG, ULONG, ULONG pio, ULONG mode) {
    attempts.push_back(mode); lastPio=pio; lastMode=mode; return mode==acceptedMode;
}
VOID UniataCs5530Timing(PHW_DEVICE_EXTENSION, ULONG d, ULONG c, ULONG pio, ULONG mode) {
    assert(d<2 && c<2); ++timingCalls; lastMode=mode; lastPio=pio;
}
BOOLEAN NTAPI AtaSetTransferMode(PHW_DEVICE_EXTENSION e, ULONG d, ULONG c,
                                PHW_LU_EXTENSION lun, ULONG mode) {
    assert(lun==e->chan[c].lun[d]); attempts.push_back(mode); return mode==acceptedMode;
}
static HW_DEVICE_EXTENSION setup() {
    HW_DEVICE_EXTENSION e={}; e.DevID=ATA_CYRIX_5530; e.BusMaster=1;
    e.MaxTransferMode=0x42; e.NumberChannels=2;
    e.chan[0].MaxTransferMode=e.chan[1].MaxTransferMode=0x42;
    initSuccess=true; initCalls=timingCalls=0; attempts.clear();
    return e;
}
static void dispatch() {
    auto e=setup();
    assert(UniataGetControllerQuirks(e.DevID));
    assert(!UniataGetControllerQuirks(0x70108086));
    assert(UniataInitControllerQuirks(&e) && initCalls==1 && e.BusMaster==1);
    e=setup(); initSuccess=false;
    assert(!UniataInitControllerQuirks(&e));
    assert(!e.BusMaster && e.MaxTransferMode==ATA_PIO4);
    assert(e.chan[0].MaxTransferMode==ATA_PIO4 && e.chan[1].MaxTransferMode==ATA_PIO4);
    for(unsigned why=0;why<3;++why) {
        e=setup();
        if(why==0) e.DevID=0x70108086;
        if(why==1) e.UnknownDev=true;
        if(why==2) e.BusMaster=0;
        assert(UniataInitControllerQuirks(&e) && !initCalls);
        if(why<2) assert(!UniataSetControllerTransferMode(&e,0,0,4,2,2));
        assert(attempts.empty() && !timingCalls);
    }
    for(unsigned channel=0;channel<2;++channel)
    for(unsigned device=0;device<2;++device)
    for(unsigned mode: {0x42u,0x41u,0x40u,0x22u,0x21u,0x20u,0x0cu}) {
        e=setup(); acceptedMode=mode;
        assert(UniataSetControllerTransferMode(&e,device,channel,4,2,2));
        assert(timingCalls==1 && lastMode==mode && lastPio==4);
        const unsigned order[]={0x42,0x41,0x40,0x22,0x21,0x20,0x0c};
        for(unsigned n=0;n<attempts.size();++n) assert(attempts[n]==order[n]);
        assert(attempts.back()==mode);
    }
    e=setup(); acceptedMode=0x42;
    assert(UniataSetControllerTransferMode(&e,0,0,6,4,6));
    assert(attempts.size()==1 && lastPio==4 && lastMode==0x42);
    e=setup(); acceptedMode=ATA_PIO0;
    assert(UniataSetControllerTransferMode(&e,0,0,0,-1,-1));
    assert(attempts.size()==1 && attempts[0]==ATA_PIO0 && timingCalls==1);
    e=setup(); acceptedMode=0xff;
    assert(UniataSetControllerTransferMode(&e,0,0,4,2,2));
    assert(attempts.size()==7 && !timingCalls);
}
int main() {
    classification(); dispatch();
    auto e=setup(); e.DevID=ATA_AMD5536;
    assert(UniataGetControllerQuirks(e.DevID)->Flags & UNIATA_QUIRK_OWN_INIT);
    assert(UniataLimitControllerChannels(&e) && e.NumberChannels==1);
    e.Channel=1; assert(!UniataLimitControllerChannels(&e));
    e=setup(); e.Channel=1;
    assert(UniataLimitControllerChannels(&e) && e.NumberChannels==1);
    e.Channel=2; assert(!UniataLimitControllerChannels(&e));
    e=setup(); e.DevID=0x70108086; e.Channel=2;
    assert(UniataLimitControllerChannels(&e) && e.NumberChannels==2);
    for(unsigned accepted: {0x45u,0x43u,0x40u,0x22u,0x0cu}) {
        e=setup(); e.DevID=ATA_AMD5536; acceptedMode=accepted;
        assert(UniataSetControllerTransferMode(&e,0,0,6,4,6));
        assert(attempts.front()==0x45 && attempts.back()==accepted && lastPio==4);
        assert(timingCalls==0); // complete-mode callback, not the CS5530 timing callback
    }
    puts("PASS controller quirks: 10240 PCI combinations, BAR guards, init fallback and negotiated-mode dispatch");
}
'''

header=(root/'driver/bsmaster.h').read_bytes().decode('latin1')
macros=header[header.index('#define IsBusMaster('):header.index('//#define INT_Q_SIZE')]
with tempfile.TemporaryDirectory(prefix='uniata-quirks-') as tmp:
    path=Path(tmp)
    (path/'stdafx.h').write_text(platform)
    for name in ('controller_quirks.h','controller_quirks.cpp','cs5530.h','cs5536.h'):
        shutil.copyfile(root/'driver'/name,path/name)
    # Headers precede classification; its tests precede the main dispatch test.
    split=harness.index('static bool initSuccess;')
    (path/'test.cpp').write_text(harness[:split]+macros+classification+harness[split:])
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',
                    str(path/'test.cpp'),str(path/'controller_quirks.cpp'),
                    '-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
