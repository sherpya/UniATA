/*++

Copyright (c) 2002-2004 Alexandr A. Telyatnikov (Alter)

Module Name:
    bm_devs.h

Abstract:
    This file contains list of 'well known' PCI IDE controllers

Author:
    Alexander A. Telyatnikov (Alter)

Environment:
    kernel mode only

Notes:

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Revision History:

--*/

#ifndef __IDE_BUSMASTER_DEVICES_H__
#define __IDE_BUSMASTER_DEVICES_H__

#ifdef USER_MODE
  #define PCI_DEV_HW_SPEC_BM(idhi, idlo, name, flags) \
    { #idlo, 4, 0x##idlo, #idhi, 4, 0x##idhi, name, flags}
#else
  #define PCI_DEV_HW_SPEC_BM(idhi, idlo, name, flags) \
    { #idlo, 4, 0x##idlo, #idhi, 4, 0x##idhi, NULL, flags}
#endif

typedef struct _BUSMASTER_CONTROLLER_INFORMATION {
    PCHAR   VendorId;
    ULONG   VendorIdLength;
    ULONG   nVendorId;
    PCHAR   DeviceId;
    ULONG   DeviceIdLength;
    ULONG   nDeviceId;
    PCHAR   FullDevName;
    ULONG   RaidFlags;
    CHAR    VendorIdStr[4];
    CHAR    DeviceIdStr[4];
    ULONG   slotNumber;
    ULONG   busNumber;
    CHAR    channel;
//    CHAR    numOfChannes;
    CHAR    MasterDev;
#ifndef USER_MODE
    CHAR    ChanInitOk;             // 0x01 - primary,  0x02 - secondary
    BOOLEAN Isr2Enable;
    PDEVICE_OBJECT Isr2DevObj;
    KIRQL      Isr2Irql;
    KAFFINITY  Isr2Affinity;
    ULONG      Isr2Vector;
    PKINTERRUPT Isr2InterruptObject;

    // for w2k MasterDev
/*    KIRQL      Isr2Irql2;
    KAFFINITY  Isr2Affinity2;
    ULONG      Isr2Vector2;
    PKINTERRUPT Isr2InterruptObject2;*/
#endif

}BUSMASTER_CONTROLLER_INFORMATION, *PBUSMASTER_CONTROLLER_INFORMATION;

#define UNIATA_RAID_CONTROLLER  0x01

BUSMASTER_CONTROLLER_INFORMATION const BusMasterAdapters[] = {
    PCI_DEV_HW_SPEC_BM( 24db, 8086, "Intel ICH5"                      , 0),
    PCI_DEV_HW_SPEC_BM( 24cb, 8086, "Intel ICH4"                      , 0),
    PCI_DEV_HW_SPEC_BM( 248a, 8086, "Intel ICH3 mobile ATA-100 controller", 0),
    PCI_DEV_HW_SPEC_BM( 248b, 8086, "Intel ICH3 ATA-100 controller", 0),
    PCI_DEV_HW_SPEC_BM( 244a, 8086, "Intel ICH2 mobile ATA-100 controller", 0),
    PCI_DEV_HW_SPEC_BM( 244b, 8086, "Intel ICH2 ATA-100 controller", 0),
    PCI_DEV_HW_SPEC_BM( 2411, 8086, "Intel ICH (2411) ATA-66 controller", 0),
    PCI_DEV_HW_SPEC_BM( 7601, 8086, "Intel ICH (7601) ATA-66 controller", 0),
    PCI_DEV_HW_SPEC_BM( 7111, 8086, "Intel PIIX4 (7111) ATA-33 controller", 0),
    PCI_DEV_HW_SPEC_BM( 84ca, 8086, "Intel PIIX4 (84ca) ATA-33 controller", 0),
    PCI_DEV_HW_SPEC_BM( 7199, 8086, "Intel PIIX4e ATA-33 controller", 0),
    PCI_DEV_HW_SPEC_BM( 2421, 8086, "Intel ICH0 ATA-33 controller", 0),
    PCI_DEV_HW_SPEC_BM( 7010, 8086, "Intel PIIX3 ATA controller", 0),
    PCI_DEV_HW_SPEC_BM( 1230, 8086, "Intel PIIX ATA controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 5229, 10b9, "AcerLabs Aladdin ATA/ATA-33 controller ", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 01bc, 10de, "nVIDIA nForce", 0),
    PCI_DEV_HW_SPEC_BM( 0065, 10de, "nVIDIA nForce 2", 0),
    PCI_DEV_HW_SPEC_BM( 7441, 1022, "AMD 768", 0),
    PCI_DEV_HW_SPEC_BM( 7411, 1022, "AMD 766", 0),
    PCI_DEV_HW_SPEC_BM( 7409, 1022, "AMD 756", 0),
    PCI_DEV_HW_SPEC_BM( 0571, 1106, "VIA ATA-XXX controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 5513, 1039, "SiS 5591 ATA-33 controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0680, 1095, "Sil 0680 ATA-133 controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0649, 1095, "CMD 649 ATA100 controller", 0),
    PCI_DEV_HW_SPEC_BM( 0648, 1095, "CMD 648 ATA66 controller", 0),
    PCI_DEV_HW_SPEC_BM( 0646, 1095, "CMD 646 ATA controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( c693, 1080, "Cypress 82C693 ATA controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0102, 1078, "Cyrix 5530 ATA33 controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0211, 1166, "ServerWorks ROSB4 ATA33 controller", 0),
    PCI_DEV_HW_SPEC_BM( 0212, 1166, "ServerWorks CSB5 ATA66/100 controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 4d68, 105a, "Promise TX2 ATA-100 controller", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 6268, 105a, "Promise TX2 ATA-100 controller", UNIATA_RAID_CONTROLLER),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 4d69, 105a, "Promise TX2 ATA-133 controller", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 5275, 105a, "Promise TX2 ATA-133 controller", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 6269, 105a, "Promise TX2 ATA-133 controller", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 7275, 105a, "Promise TX2 ATA-133 controller", UNIATA_RAID_CONTROLLER),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 4d33, 105a, "Promise Ultra/FastTrak-33 controllers", UNIATA_RAID_CONTROLLER),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0d38, 105a, "Promise FastTrak 66 controllers", 0),
    PCI_DEV_HW_SPEC_BM( 4d38, 105a, "Promise Ultra/FastTrak-66 controllers", UNIATA_RAID_CONTROLLER),

    PCI_DEV_HW_SPEC_BM( 4d30, 105a, "Promise Ultra/FastTrak-100 controllers", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0d30, 105a, "Promise OEM ATA-100 controllers", UNIATA_RAID_CONTROLLER),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0004, 1103, "HighPoint HPT366/368/370/372 controllers", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0005, 1103, "HighPoint HPT372 controllers", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0008, 1103, "HighPoint HPT374 controllers", UNIATA_RAID_CONTROLLER),

    PCI_DEV_HW_SPEC_BM( 0007, 1191, "Acard ATP860R controller", 0),
    PCI_DEV_HW_SPEC_BM( 0006, 1191, "Acard ATP860 controller", 0),
    PCI_DEV_HW_SPEC_BM( 0009, 1191, "Acard ATP865R controller", 0),
    PCI_DEV_HW_SPEC_BM( 0008, 1191, "Acard ATP865 controller", 0),
    PCI_DEV_HW_SPEC_BM( 0005, 1191, "Acard ATP850UF controller", 0),
                                                                                  
    PCI_DEV_HW_SPEC_BM( 0001, 16ca, "Cenatek Rocket Drive controller", 0) 
};

#define NUM_BUSMASTER_ADAPTERS (sizeof(BusMasterAdapters) / sizeof(BUSMASTER_CONTROLLER_INFORMATION))

__inline
ULONG
Ata_is_dev_listed(
    PBUSMASTER_CONTROLLER_INFORMATION BusMasterAdapters,
    ULONG VendorId,
    ULONG DeviceId,
    ULONG lim
    )
{
    for(ULONG k=0; k<lim; k++) {
        if(BusMasterAdapters[k].nVendorId == VendorId &&
           BusMasterAdapters[k].nDeviceId == DeviceId)
            return k;
    }
    return -1;
}

#define Ata_is_supported_dev(pciData) \
    ((pciData)->BaseClass == PCI_DEV_CLASS_STORAGE && \
     (pciData)->SubClass == PCI_DEV_SUBCLASS_IDE)

#define IOCTL_SCSI_MINIPORT_UNIATA_FIND_DEVICES    ((FILE_DEVICE_SCSI << 16) + 0x09a0)
#define IOCTL_SCSI_MINIPORT_UNIATA_DELETE_DEVICE   ((FILE_DEVICE_SCSI << 16) + 0x09a1)
#define IOCTL_SCSI_MINIPORT_UNIATA_SET_MAX_MODE    ((FILE_DEVICE_SCSI << 16) + 0x09a2)

typedef struct _SETTRANSFERMODE {
    ULONG Mode;
} SETTRANSFERMODE, *PSETTRANSFERMODE;

#endif //__IDE_BUSMASTER_H__
