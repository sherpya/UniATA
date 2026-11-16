#!/usr/bin/env python3
"""Exercise the production PCI owner and DriverEntry legacy loop with fake OS calls.
The harness checks disjoint resources/failure handling and models XP's observed
native-PCI IRQ gate. It does not emulate XP's PnP matching or resource arbiter.
"""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
probe = (root/'driver/id_probe.cpp').read_bytes().decode('latin1')
ata = (root/'driver/id_ata.cpp').read_bytes().decode('latin1')
a = probe.index('BOOLEAN\nUniataUseScsiPortPciClaim(')
b = probe.index('} // end UniataUseScsiPortPciClaim()',a)+1
policy = probe[a:b]
a = ata.index('    for (i=0; i <BMListLen; i++) {',ata.index('// Look for legacy ISA-bridged'))
b = ata.index('/*    KeBugCheckEx(',a)
loop = ata[a:b]
platform = r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstdlib>
typedef uint32_t ULONG; typedef uint16_t USHORT; typedef uint8_t UCHAR;
typedef int32_t NTSTATUS; typedef bool BOOLEAN; typedef BOOLEAN* PBOOLEAN;
typedef void* PVOID; typedef char* PCHAR;
#define NTAPI
#define TRUE true
#define FALSE false
#define KdPrint2(args) ((void)0)
#define PRINT_PREFIX ""
#define NT_SUCCESS(s) ((NTSTATUS)(s)>=0)
#define WinVer_XP 0x501
#define WinVer_2k 0x500
#define WinVer_NT 0x400
#define STATUS_SUCCESS 0u
#define STATUS_CONFLICTING_ADDRESSES ((NTSTATUS)0xc0000018)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define STATUS_DEVICE_DOES_NOT_EXIST ((NTSTATUS)0xc00000c0)
#define Isa 1
#define PCIBus 5
#define PCIConfiguration 4
#define PCI_COMMON_HDR_LENGTH 64u
#define SP_UNINITIALIZED_VALUE (~0u)
#define SP_RETURN_FOUND 1u
#define SP_RETURN_NOT_FOUND 0u
#define DEVNUM_NOT_SPECIFIED (~0u)
#define UNIATA_QUIRK_SCSIPORT_PCI_CLAIM 2u
#define SRB_FUNCTION_EXECUTE_SCSI 0
#define SRB_FUNCTION_SHUTDOWN 7
#define SRB_FUNCTION_FLUSH 8
#define SRB_STATUS_NO_DEVICE 8
#define SRB_STATUS_INVALID_REQUEST 6
#define SRB_STATUS_SUCCESS 1
#define RequestComplete 0
#define NextRequest 1
static ULONG version=WinVer_XP;
#define WinVer_Id() version
BOOLEAN WinVer_WDM_Model,g_Dump,g_opt_Verbose;
struct UNIATA_CONTROLLER_QUIRKS { ULONG Flags; } quirk={2};
const UNIATA_CONTROLLER_QUIRKS* UniataGetControllerQuirks(ULONG id) {
 return id==0x01021078?&quirk:NULL;
}
struct BM {
 ULONG nVendorId,nDeviceId,busNumber,slotNumber,channel,ChanInitOk;
 BOOLEAN MasterDev,ScsiPortPciClaim; void* PciIdeDevObj;
 const char *VendorId,*DeviceId; ULONG VendorIdLength,DeviceIdLength;
};
typedef BM* PBUSMASTER_CONTROLLER_INFORMATION;
BM BMList[1]; ULONG BMListLen=1;
struct Physical { ULONG LowPart; int32_t HighPart; };
Physical ScsiPortConvertUlongToPhysicalAddress(ULONG n) { return {n,0}; }
struct ACCESS_RANGE { Physical RangeStart; ULONG RangeLength; BOOLEAN RangeInMemory; };
struct Config {
 ACCESS_RANGE (*AccessRanges)[6];
 ULONG NumberOfAccessRanges,AdapterInterfaceType,SystemIoBusNumber,SlotNumber;
 ULONG BusInterruptLevel,BusInterruptVector,BusInterruptLevel2,BusInterruptVector2;
 ULONG DmaChannel,DmaPort,DmaChannel2,DmaPort2;
 BOOLEAN Master,ScatterGather,Dma32BitAddresses,NeedPhysicalAddresses,MapBuffers;
 BOOLEAN AtdiskPrimaryClaimed,AtdiskSecondaryClaimed;
 ULONG MaximumTransferLength,NumberOfPhysicalBreaks,AlignmentMask;
 ULONG NumberOfBuses,MaximumNumberOfTargets,InitiatorBusId[8];
};
typedef Config* PPORT_CONFIGURATION_INFORMATION;
struct PCI_COMMON_CONFIG {
 USHORT VendorID,DeviceID;
 struct { struct { ULONG BaseAddresses[6]; } type0; } u;
};
struct SCSI_REQUEST_BLOCK { UCHAR Function,SrbStatus,ScsiStatus; ULONG DataTransferLength; };
typedef SCSI_REQUEST_BLOCK* PSCSI_REQUEST_BLOCK;
typedef ULONG (*PHW_FIND_ADAPTER)(PVOID,PVOID,PVOID,PCHAR,Config*,PBOOLEAN);
typedef BOOLEAN (*PHW_INITIALIZE)(PVOID);
typedef BOOLEAN (*PHW_STARTIO)(PVOID,PSCSI_REQUEST_BLOCK);
typedef BOOLEAN (*PHW_RESET_BUS)(PVOID,ULONG);
struct HW_INITIALIZATION_DATA_COMMON {
 struct {
  ULONG HwInitializationDataSize,AdapterInterfaceType;
  PVOID VendorId,DeviceId; USHORT VendorIdLength,DeviceIdLength;
  PHW_FIND_ADAPTER HwFindAdapter; PHW_INITIALIZE HwInitialize;
  PHW_STARTIO HwStartIo; PHW_RESET_BUS HwResetBus;
  PVOID HwInterrupt; ULONG DeviceExtensionSize,NumberOfAccessRanges;
 } comm;
 struct { ULONG unused; } w2k;
};
void RtlZeroMemory(void* p,size_t n) { memset(p,0,n); }
void* UlongToPtr(ULONG x) { return reinterpret_cast<void*>(uintptr_t(x)); }
ULONG PtrToUlong(void* p) { return uintptr_t(p); }
static PCI_COMMON_CONFIG pci;
static bool headerValid;
static ULONG pciReadLength;
ULONG HalGetBusData(int type,ULONG bus,ULONG slot,void* p,ULONG n) {
 assert(type==PCIConfiguration && bus==BMList[0].busNumber && slot==0x52 && n==64);
 memcpy(p,&pci,sizeof pci);return pciReadLength;
}
BOOLEAN UniataHasLegacyBusMaster(const PCI_COMMON_CONFIG* p) {
 // Header/quirk classification itself is covered by test_controller_quirks.py.
 auto bar=p->u.type0.BaseAddresses[4];
 return headerValid && (bar&15)==1 && bar>=0x11 && bar<=0xfff1;
}
static std::vector<unsigned> notifications;
void ScsiPortNotification(unsigned n,void*,void*) { notifications.push_back(n); }
ULONG ScsiPortInitialize(PVOID,PVOID,decltype(HW_INITIALIZATION_DATA_COMMON::comm)*,PVOID);
'''
stubs = r'''
static int failMode,channelFailure;
static ULONG helperCalls;
static std::vector<int> calls;
struct Global { BOOLEAN AtDiskPrimaryAddressClaimed,AtDiskSecondaryAddressClaimed; } globals;
ULONG UniataFindBusMasterController(PVOID,PVOID,PVOID,PCHAR,Config*,PBOOLEAN) { return 0; }
ULONG UniataFindCompatBusMasterController1(PVOID,PVOID,PVOID,PCHAR,Config*,PBOOLEAN) { return 0; }
ULONG UniataFindCompatBusMasterController2(PVOID,PVOID,PVOID,PCHAR,Config*,PBOOLEAN) { return 0; }
ULONG AtapiRegCheckDevValue(void*,ULONG,ULONG,const wchar_t*,ULONG fallback) {return fallback;}
void _PrintNtConsole(const char*) {}
NTSTATUS UniataClaimLegacyPCIIDE(ULONG i) { ++helperCalls; BMList[i].ChanInitOk|=0x80;return 0; }
void checkOwnerConfig(const Config& config) {
 const auto& r=(*config.AccessRanges)[0];
 assert(config.AdapterInterfaceType==PCIBus && config.SlotNumber==0x52);
 assert(config.SystemIoBusNumber==BMList[0].busNumber);
 assert(r.RangeStart.LowPart==(pci.u.type0.BaseAddresses[4]&~15u));
 assert(!r.RangeStart.HighPart && r.RangeLength==16 && !r.RangeInMemory);
 for(unsigned i=1;i<config.NumberOfAccessRanges;++i) assert(!(*config.AccessRanges)[i].RangeLength);
 assert(!config.BusInterruptLevel && !config.BusInterruptVector);
 assert(!config.BusInterruptLevel2 && !config.BusInterruptVector2);
 assert(config.DmaChannel==~0u && config.DmaChannel2==~0u);
 assert(config.DmaPort==~0u && config.DmaPort2==~0u);
 assert(!config.Master && !config.MapBuffers && !config.NeedPhysicalAddresses);
 assert(!config.AtdiskPrimaryClaimed && !config.AtdiskSecondaryClaimed);
 assert(config.NumberOfBuses==1 && config.MaximumNumberOfTargets==1 && !config.InitiatorBusId[0]);
}
Config initial(ACCESS_RANGE (*ranges)[6]) {
 Config c={}; c.AccessRanges=ranges;c.NumberOfAccessRanges=6;
 c.AdapterInterfaceType=Isa;c.SystemIoBusNumber=0;c.SlotNumber=0;
 c.BusInterruptLevel=14;c.BusInterruptVector=14;c.BusInterruptLevel2=15;c.BusInterruptVector2=15;
 c.DmaChannel=1;c.DmaChannel2=5;c.Master=true;
 c.AtdiskPrimaryClaimed=c.AtdiskSecondaryClaimed=true;
 (*ranges)[0]={{0x1f0,0},8,false};(*ranges)[1]={{0x3f6,0},1,false};
 return c;
}
ULONG ScsiPortInitialize(void*,void*,decltype(HW_INITIALIZATION_DATA_COMMON::comm)* init,void* context) {
 auto& b=BMList[0];
 if(init->HwFindAdapter==UniataPciOwnerFind) {
  calls.push_back(-1);
  // XP ScsiPortInitLegacyAdapter (VA 1bcb6 in the retained XP binary)
  // skips a native PCI match with no IRQ before invoking HwFindAdapter.
  // This is the CS5530 PCI resource list, even though channels use IRQ14/15.
  if(init->AdapterInterfaceType==PCIBus && init->VendorIdLength &&
     init->DeviceIdLength && init->VendorId && init->DeviceId)
   return STATUS_DEVICE_DOES_NOT_EXIST;
  assert(init->AdapterInterfaceType==Isa && init->NumberOfAccessRanges==1);
  assert(!init->VendorIdLength && !init->VendorId);
  assert(!init->DeviceIdLength && !init->DeviceId);
  assert(!init->HwInterrupt && init->DeviceExtensionSize==sizeof(UNIATA_PCI_OWNER_EXTENSION));
  if(failMode==1)return 0xc0000018; // allocation failure before callbacks
  if(failMode==2)return 0; // success due to some unrelated adapter
  ACCESS_RANGE ranges[6]={};Config config=initial(&ranges);BOOLEAN again=true;
  UNIATA_PCI_OWNER_EXTENSION ext={};
  if(failMode==4)config.SystemIoBusNumber=1;
  if(init->HwFindAdapter(&ext,context,NULL,NULL,&config,&again)!=SP_RETURN_FOUND)
   return STATUS_DEVICE_DOES_NOT_EXIST;
  assert(!again);checkOwnerConfig(config);
  assert(init->HwInitialize(&ext));
  if(failMode==3)return 0xc0000001; // failure after HwInitialize
  assert(init->HwFindAdapter(&ext,context,NULL,NULL,&config,&again)==SP_RETURN_NOT_FOUND);
  return 0;
 }
 calls.push_back(b.channel);
 assert(init->AdapterInterfaceType==Isa && init->NumberOfAccessRanges==6);
 if(b.ScsiPortPciClaim)assert((b.ChanInitOk&0x80) && !helperCalls && !uintptr_t(context));
 if(channelFailure==int(b.channel)+1)return 0xc0000001;
 b.ChanInitOk |= 1u<<b.channel;
 return 0;
}
ULONG runLoop() {
 ULONG i=0,c=0,alt=0,pref_alt=0,newStatus=0,statusToReturn=~0u;
 BOOLEAN IgnoreIsaCompatiblePci=false,ReEnter=false,PrimaryClaimed=false,SecondaryClaimed=false;
 Global* GlobalConfig=&globals;HW_INITIALIZATION_DATA_COMMON hwInitializationData={};
 void *DriverObject=NULL,*Argument2=NULL;
'''
harness = r'''
 return statusToReturn;
}
void reset() {
 BMList[0]={0x1078,0x0102,0,0x52,0,0,true,false,NULL,"1078","0102",4,4};
 version=WinVer_XP;WinVer_WDM_Model=g_Dump=false;quirk.Flags=2;
 pci={};pci.VendorID=0x1078;pci.DeviceID=0x0102;pci.u.type0.BaseAddresses[4]=0xf001;
 headerValid=true;pciReadLength=64;helperCalls=0;failMode=0;channelFailure=0;
 calls.clear();notifications.clear();globals={};
}
int main() {
 reset();
 for(ULONG os:{0x351u,0x400u,0x500u,0x501u,0x502u,0x600u}) {
  version=os;
  for(unsigned f=0;f<32;++f) {
   WinVer_WDM_Model=f&1;g_Dump=f&2;BMList[0].MasterDev=!(f&4);
   assert(bool(UniataUseScsiPortPciClaim(&BMList[0],f&8,f&16))==
          ((os==0x501 || os==0x502) && !f));
  }
 }
 reset();quirk.Flags=0;assert(!UniataUseScsiPortPciClaim(&BMList[0],false,false));
 reset();BMList[0].nDeviceId=0x103;assert(!UniataUseScsiPortPciClaim(&BMList[0],false,false));
 reset();BMList[0].PciIdeDevObj=&globals;assert(!UniataUseScsiPortPciClaim(&BMList[0],false,false));
 // Integration regression first: native PCI probing never reaches the callback without an IRQ on XP.
 reset();assert(runLoop()==0 && BMList[0].ChanInitOk==0x83 && !helperCalls);
 assert((calls==std::vector<int>{-1,0,1}));
 unsigned rangesChecked=0;
 for(ULONG base=0x10;base<=0xfff0;base+=0x10) {
  reset();BMList[0].ScsiPortPciClaim=true;pci.u.type0.BaseAddresses[4]=base|1;
  ACCESS_RANGE ranges[6]={};Config config=initial(&ranges);BOOLEAN again=true;
  UNIATA_PCI_OWNER_EXTENSION ext={};
  assert(UniataPciOwnerFind(&ext,NULL,NULL,NULL,&config,&again)==SP_RETURN_FOUND);
  assert(!again);checkOwnerConfig(config);++rangesChecked;
 }
 for(unsigned fault=0;fault<11;++fault) {
  reset();BMList[0].ScsiPortPciClaim=true;ACCESS_RANGE ranges[6]={};Config config=initial(&ranges);
  UNIATA_PCI_OWNER_EXTENSION ext={};BOOLEAN again=true;void* context=NULL;
  switch(fault) {
   case 0:context=UlongToPtr(1);break;
   case 1:config.SystemIoBusNumber=1;break;
   case 2:pci.VendorID=0x1234;break;
   case 3:config.AdapterInterfaceType=PCIBus;break;
   case 4:pciReadLength=0;break;
   case 5:pci.DeviceID=0x103;break;
   case 6:headerValid=false;break;
   case 7:pci.u.type0.BaseAddresses[4]=0;break;
   case 8:config.AccessRanges=NULL;break;
   case 9:config.NumberOfAccessRanges=0;break;
   case 10:BMList[0].ChanInitOk=0x80;break;
  }
  assert(UniataPciOwnerFind(&ext,context,NULL,NULL,&config,&again)==SP_RETURN_NOT_FOUND);
  assert(!again && config.BusInterruptLevel==14); // no publication on failure
 }
 // ISA enumeration bus 0 must still publish the actual PCI bus and slot.
 reset();BMList[0].busNumber=2;
 assert(runLoop()==0 && BMList[0].ChanInitOk==0x83);
 reset();assert(runLoop()==0 && BMList[0].ChanInitOk==0x83 && !helperCalls);
 assert((calls==std::vector<int>{-1,0,1}));
 assert(UniataClaimPciResourcesWithScsiPort(NULL,NULL,0)==0 && calls.size()==3);
 for(int mode=1;mode<=4;++mode) {
  reset();failMode=mode;runLoop();assert(!helperCalls && !(BMList[0].ChanInitOk&0x80));
  assert((calls==std::vector<int>{-1}));
 }
 // A channel failure must not invalidate the independent PCI resource owner.
 reset();channelFailure=1;runLoop();assert(BMList[0].ChanInitOk==0x82);
 assert((calls==std::vector<int>{-1,0,1}));
 reset();BMList[0].nDeviceId=0x7010;runLoop();assert(helperCalls==1 && BMList[0].ChanInitOk==0x83);
 assert((calls==std::vector<int>{0,1}));
 reset();UNIATA_PCI_OWNER_EXTENSION ext={};
 for(unsigned function=0;function<256;++function) {
  SCSI_REQUEST_BLOCK srb={(UCHAR)function,0xff,0xff,512};notifications.clear();
  assert(UniataPciOwnerStartIo(&ext,&srb));
  assert(!srb.DataTransferLength && !srb.ScsiStatus);
  auto expected=function==0?SRB_STATUS_NO_DEVICE:
      (function==7 || function==8)?SRB_STATUS_SUCCESS:SRB_STATUS_INVALID_REQUEST;
  assert(srb.SrbStatus==expected && (notifications==std::vector<unsigned>{0,1}));
 }
 assert(UniataPciOwnerReset(&ext,0));
 printf("PASS separate PCI owner: %u BAR ranges, no IRQ/DMA/disks, XP no-IRQ PCI regression, policy, failure cleanup, production loop ordering and 256 SRB functions\n",rangesChecked);
}
'''
with tempfile.TemporaryDirectory(prefix='uniata-pci-owner-') as tmp:
    d=Path(tmp)
    (d/'stdafx.h').write_text(platform)
    (d/'controller_quirks.h').write_text('#pragma once\n')
    # Quoted stdafx.h includes must resolve to the fake OS shim, not the DDK.
    (d/'pci_owner.cpp').write_bytes((root/'driver/pci_owner.cpp').read_bytes())
    src=d/'test.cpp'
    src.write_text('#include "stdafx.h"\n'+policy+'\n#include "pci_owner.cpp"\n'+stubs+loop+harness)
    exe=d/'test'
    subprocess.run(['g++','-std=c++11','-O2','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
