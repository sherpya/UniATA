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

typedef struct _BUSMASTER_CONTROLLER_INFORMATION {
    PCHAR   VendorId;
    ULONG   VendorIdLength;
    ULONG   nVendorId;
    PCHAR   DeviceId;
    ULONG   DeviceIdLength;
    ULONG   nDeviceId;
    ULONG   nRevId;
    ULONG   MaxTransferMode;
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
#endif

}BUSMASTER_CONTROLLER_INFORMATION, *PBUSMASTER_CONTROLLER_INFORMATION;

/* defines for known chipset PCI id's */
#define ATA_ACARD_ID		0x1191
#define ATA_ATP850		0x00021191
#define ATA_ATP850A		0x00041191
#define ATA_ATP850R		0x00051191
#define ATA_ATP860A		0x00061191
#define ATA_ATP860R		0x00071191
#define ATA_ATP865A		0x00081191
#define ATA_ATP865R		0x00091191

#define ATA_AMD_ID		0x1022
#define ATA_AMD755		0x74011022
#define ATA_AMD756		0x74091022
#define ATA_AMD766		0x74111022
#define ATA_AMD768		0x74411022
#define ATA_AMD8111		0x74691022

#define ATA_ACER_LABS_ID	0x10b9
#define ATA_ALI_5229		0x522910b9

#define ATA_CYRIX_ID		0x1078
#define ATA_CYRIX_5530		0x01021078

#define ATA_CYPRESS_ID		0x1080
#define ATA_CYPRESS_82C693	0xc6931080

#define ATA_DEC_21150		0x00221011

#define ATA_HIGHPOINT_ID	0x1103
#define ATA_HPT366		0x00041103
#define ATA_HPT372		0x00051103
#define ATA_HPT302		0x00061103
#define ATA_HPT371		0x00071103
#define ATA_HPT374		0x00081103

#define ATA_INTEL_ID		0x8086
#define ATA_I960RM		0x09628086
#define ATA_I82371FB		0x12308086
#define ATA_I82371SB		0x70108086
#define ATA_I82371AB		0x71118086
#define ATA_I82443MX		0x71998086
#define ATA_I82451NX		0x84ca8086
#define ATA_I82372FB		0x76018086
#define ATA_I82801AB		0x24218086
#define ATA_I82801AA		0x24118086
#define ATA_I82801BA		0x244a8086
#define ATA_I82801BA_1		0x244b8086
#define ATA_I82801CA		0x248a8086
#define ATA_I82801CA_1		0x248b8086
#define ATA_I82801DB		0x24cb8086
#define ATA_I82801DB_1		0x24ca8086
#define ATA_I82801EB		0x24db8086
#define ATA_I82801EB_1		0x24d18086

#define ATA_NVIDIA_ID		0x10de
#define ATA_NFORCE1		0x01bc10de
#define ATA_NFORCE2		0x006510de

#define ATA_PROMISE_ID		0x105a
#define ATA_PDC20246		0x4d33105a
#define ATA_PDC20262		0x4d38105a
#define ATA_PDC20263		0x0d38105a
#define ATA_PDC20265		0x0d30105a
#define ATA_PDC20267		0x4d30105a
#define ATA_PDC20268		0x4d68105a
#define ATA_PDC20269		0x4d69105a
#define ATA_PDC20270		0x6268105a
#define ATA_PDC20271		0x6269105a
#define ATA_PDC20275		0x1275105a
#define ATA_PDC20276		0x5275105a
#define ATA_PDC20277		0x7275105a
#define ATA_PDC20318		0x3318105a
#define ATA_PDC20319		0x3319105a
#define ATA_PDC20371		0x3371105a
#define ATA_PDC20375		0x3375105a
#define ATA_PDC20376		0x3376105a
#define ATA_PDC20377		0x3377105a
#define ATA_PDC20378		0x3373105a
#define ATA_PDC20379		0x3372105a
#define ATA_PDC20617		0x6617105a
#define ATA_PDC20618		0x6626105a
#define ATA_PDC20619		0x6629105a
#define ATA_PDC20620		0x6620105a
#define ATA_PDC20621		0x6621105a

#define ATA_SERVERWORKS_ID	0x1166
#define ATA_ROSB4		0x02111166
#define ATA_CSB5		0x02121166
#define ATA_CSB6		0x02131166
#define ATA_CSB6_1		0x02171166

#define ATA_SILICON_IMAGE_ID	0x1095
#define ATA_SII0680		0x06801095
#define ATA_CMD646		0x06461095
#define ATA_CMD648		0x06481095
#define ATA_CMD649		0x06491095

#define ATA_SIS_ID		0x1039
#define ATA_SISSOUTH		0x00081039
#define ATA_SIS5511		0x55111039
#define ATA_SIS5513		0x55131039
#define ATA_SIS5518		0x55181039
#define ATA_SIS5571		0x55711039
#define ATA_SIS5591		0x55911039
#define ATA_SIS5596		0x55961039
#define ATA_SIS5597		0x55971039
#define ATA_SIS5598		0x55981039
#define ATA_SIS5600		0x56001039
#define ATA_SIS530		0x05301039
#define ATA_SIS540		0x05401039
#define ATA_SIS550		0x05501039
#define ATA_SIS620		0x06201039
#define ATA_SIS630		0x06301039
#define ATA_SIS635		0x06351039
#define ATA_SIS633		0x06331039
#define ATA_SIS640		0x06401039
#define ATA_SIS645		0x06451039
#define ATA_SIS646		0x06461039
#define ATA_SIS648		0x06481039
#define ATA_SIS650		0x06501039
#define ATA_SIS651		0x06511039
#define ATA_SIS652		0x06521039
#define ATA_SIS655		0x06551039
#define ATA_SIS658		0x06581039
#define ATA_SIS730		0x07301039
#define ATA_SIS733		0x07331039
#define ATA_SIS735		0x07351039
#define ATA_SIS740		0x07401039
#define ATA_SIS745		0x07451039
#define ATA_SIS746		0x07461039
#define ATA_SIS748		0x07481039
#define ATA_SIS750		0x07501039
#define ATA_SIS751		0x07511039
#define ATA_SIS752		0x07521039
#define ATA_SIS755		0x07551039
#define ATA_SIS961		0x09611039
#define ATA_SIS962		0x09621039
#define ATA_SIS963		0x09631039

#define ATA_VIA_ID		0x1106
#define ATA_VIA82C571		0x05711106
#define ATA_VIA82C586		0x05861106
#define ATA_VIA82C596		0x05961106
#define ATA_VIA82C686		0x06861106
#define ATA_VIA8231		0x82311106
#define ATA_VIA8233		0x30741106
#define ATA_VIA8233A		0x31471106
#define ATA_VIA8233C		0x31091106
#define ATA_VIA8235		0x31771106
#define ATA_VIA8361		0x31121106
#define ATA_VIA8363		0x03051106
#define ATA_VIA8371		0x03911106
#define ATA_VIA8662		0x31021106

#define ATA_ITE_ID		0x1283
#define ATA_IT8172G		0x81721283

#define ATA_OPTI_ID             0x1045
#define ATA_OPTI82C621          0xc6211045
#define ATA_OPTI82C625          0xd5681045

/* chipset setup related defines          */
/* Used in HW_DEVICE_EXTENSION.InitMethod */

#define CHIPTYPE_MASK   0x000000ff
#define CHIPFLAG_MASK   0xffffff00

#define UNIATA_RAID_CONTROLLER  0x80000000
#define UNIATA_SIMPLEX_ONLY     0x40000000

#define ATPOLD		0x0100

#define ALIOLD		0x0100
#define ALINEW		0x0200

#define HPT366		0
#define HPT370		1
#define HPT372		2
#define HPT374		3
#define HPTOLD		0x0100

#define PROLD		0
#define PRNEW		1
#define PRTX		2
#define PRMIO		3
#define PRIDX		4
#define PRTX4		0x0100
#define PRSX4K		0x0200
#define PRSX6K		0x0400
#define PRSATA		0x0800
#define PRDUAL		0x1000

#define SWKS33		0
#define SWKS66		1
#define SWKS100		2

#define SII_INTR	0x0100
#define SII_SETCLK	0x0200
#define SII_ENINTR	0x0400
#define SII_CMD         0x0800

#define SIS_SOUTH	1
#define SIS133NEW	2
#define SIS133OLD	3
#define SIS100NEW	4
#define SIS100OLD	5
#define SIS66		6
#define SIS33		7

#define VIA33		0
#define VIA66		1
#define VIA100		2
#define VIA133		3
#define AMDNVIDIA	4
#define AMDCABLE	0x0100
#define AMDBUG		0x0200
#define NVIDIA		0x0400
#define VIACLK		0x0800
#define VIABUG		0x1000
#define VIASOUTH	0x2000
#define VIAAST		0x4000
#define VIAPRQ		0x8000

#ifdef USER_MODE
  #define PCI_DEV_HW_SPEC_BM(idhi, idlo, rev, mode, name, flags) \
    { #idlo, 4, 0x##idlo, #idhi, 4, 0x##idhi, rev, mode, name, flags}
#else
  #define PCI_DEV_HW_SPEC_BM(idhi, idlo, rev, mode, name, flags) \
    { #idlo, 4, 0x##idlo, #idhi, 4, 0x##idhi, rev, mode, NULL, flags}
#endif

BUSMASTER_CONTROLLER_INFORMATION const BusMasterAdapters[] = {
    PCI_DEV_HW_SPEC_BM( 24db, 8086, 0, 0, "Intel ICH5",                              0),
    PCI_DEV_HW_SPEC_BM( 24cb, 8086, 0, 0, "Intel ICH4",                              0),
    PCI_DEV_HW_SPEC_BM( 248a, 8086, 0, 0, "Intel ICH3 mobile ATA-100 controller",    0),
    PCI_DEV_HW_SPEC_BM( 248b, 8086, 0, 0, "Intel ICH3 ATA-100 controller",           0),
    PCI_DEV_HW_SPEC_BM( 244a, 8086, 0, 0, "Intel ICH2 mobile ATA-100 controller",    0),
    PCI_DEV_HW_SPEC_BM( 244b, 8086, 0, 0, "Intel ICH2 ATA-100 controller",           0),
    PCI_DEV_HW_SPEC_BM( 2411, 8086, 0, 0, "Intel ICH (2411) ATA-66 controller",      0),
    PCI_DEV_HW_SPEC_BM( 7601, 8086, 0, 0, "Intel ICH (7601) ATA-66 controller",      0),
    PCI_DEV_HW_SPEC_BM( 7111, 8086, 0, 0, "Intel PIIX4 (7111) ATA-33 controller",    0),
    PCI_DEV_HW_SPEC_BM( 84ca, 8086, 0, 0, "Intel PIIX4 (84ca) ATA-33 controller",    0),
    PCI_DEV_HW_SPEC_BM( 7199, 8086, 0, 0, "Intel PIIX4e ATA-33 controller",          0),
    PCI_DEV_HW_SPEC_BM( 2421, 8086, 0, 0, "Intel ICH0 ATA-33 controller",            0),
    PCI_DEV_HW_SPEC_BM( 7010, 8086, 0, 0, "Intel PIIX3 ATA controller",              0),
    PCI_DEV_HW_SPEC_BM( 1230, 8086, 0, 0, "Intel PIIX ATA controller",               0),
                                                                                        
    PCI_DEV_HW_SPEC_BM( 5229, 10b9, 0, 0, "AcerLabs Aladdin ATA/ATA-33 controller",  0),
                                                                                        
    PCI_DEV_HW_SPEC_BM( 01bc, 10de, 0, 0, "nVIDIA nForce",                           0),
    PCI_DEV_HW_SPEC_BM( 0065, 10de, 0, 0, "nVIDIA nForce 2",                         0),
    PCI_DEV_HW_SPEC_BM( 7441, 1022, 0, 0, "AMD 768",                                 0),
    PCI_DEV_HW_SPEC_BM( 7411, 1022, 0, 0, "AMD 766",                                 0),
    PCI_DEV_HW_SPEC_BM( 7409, 1022, 0, 0, "AMD 756",                                 0),
    PCI_DEV_HW_SPEC_BM( 0571, 1106, 0, 0, "VIA ATA-XXX controller",                  0),
                                          
    PCI_DEV_HW_SPEC_BM( 5513, 1039, 0, 0, "SiS ATA-XXX controller",                  0),
                                                                                        
    PCI_DEV_HW_SPEC_BM( 0680, 1095, 0, 0, "Sil 0680 ATA-133 controller",             0),
                                          
    PCI_DEV_HW_SPEC_BM( 0649, 1095, 0, 0, "CMD 649 ATA100 controller",               0),
    PCI_DEV_HW_SPEC_BM( 0648, 1095, 0, 0, "CMD 648 ATA66 controller",                0),
    PCI_DEV_HW_SPEC_BM( 0646, 1095, 0, 0, "CMD 646 ATA controller",                  0),
                                          
    PCI_DEV_HW_SPEC_BM( c693, 1080, 0, 0, "Cypress 82C693 ATA controller",           0),
                                          
    PCI_DEV_HW_SPEC_BM( 0102, 1078, 0, 0, "Cyrix 5530 ATA33 controller",             0),
                                                                                        
    PCI_DEV_HW_SPEC_BM( 0211, 1166, 0, 0, "ServerWorks ROSB4 ATA33 controller",      0),
    PCI_DEV_HW_SPEC_BM( 0212, 1166, 0, 0, "ServerWorks CSB5 ATA66/100 controller",   0),
                                                                                     
    PCI_DEV_HW_SPEC_BM( 4d68, 105a, 0, 0, "Promise TX2 ATA-100 controller",          UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 6268, 105a, 0, 0, "Promise TX2 ATA-100 controller",          UNIATA_RAID_CONTROLLER),
                                          
    PCI_DEV_HW_SPEC_BM( 4d69, 105a, 0, 0, "Promise TX2 ATA-133 controller",          UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 5275, 105a, 0, 0, "Promise TX2 ATA-133 controller",          UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 6269, 105a, 0, 0, "Promise TX2 ATA-133 controller",          UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 7275, 105a, 0, 0, "Promise TX2 ATA-133 controller",          UNIATA_RAID_CONTROLLER),
                                                                                     
    PCI_DEV_HW_SPEC_BM( 4d33, 105a, 0, 0, "Promise Ultra/FastTrak-33 controller",    UNIATA_RAID_CONTROLLER),
                                                                                     
    PCI_DEV_HW_SPEC_BM( 0d38, 105a, 0, 0, "Promise FastTrak 66 controller",          0),
    PCI_DEV_HW_SPEC_BM( 4d38, 105a, 0, 0, "Promise Ultra/FastTrak-66 controller",    UNIATA_RAID_CONTROLLER),
                                          
    PCI_DEV_HW_SPEC_BM( 4d30, 105a, 0, 0, "Promise Ultra/FastTrak-100 controller",   UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0d30, 105a, 0, 0, "Promise OEM ATA-100 controllers",         UNIATA_RAID_CONTROLLER),
                                                                                     
    PCI_DEV_HW_SPEC_BM( 0004, 1103, 0, 0, "HighPoint HPT366/368/370/372 controller", UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0005, 1103, 0, 0, "HighPoint HPT372 controller",             UNIATA_RAID_CONTROLLER),
    PCI_DEV_HW_SPEC_BM( 0008, 1103, 0, 0, "HighPoint HPT374 controller",             UNIATA_RAID_CONTROLLER),
                                          
    PCI_DEV_HW_SPEC_BM( 0007, 1191, 0, 0, "Acard ATP860R controller",                0),
    PCI_DEV_HW_SPEC_BM( 0006, 1191, 0, 0, "Acard ATP860 controller",                 0),
    PCI_DEV_HW_SPEC_BM( 0009, 1191, 0, 0, "Acard ATP865R controller",                0),
    PCI_DEV_HW_SPEC_BM( 0008, 1191, 0, 0, "Acard ATP865 controller",                 0),
    PCI_DEV_HW_SPEC_BM( 0005, 1191, 0, 0, "Acard ATP850UF controller",               0),
                                          
    PCI_DEV_HW_SPEC_BM( 0001, 16ca, 0, 0, "Cenatek Rocket Drive controller",         0)
};

#define NUM_BUSMASTER_ADAPTERS (sizeof(BusMasterAdapters) / sizeof(BUSMASTER_CONTROLLER_INFORMATION))

/*
    Looks for device with specified Device/Vendor and Revision
    in specified device list and returnts its index.
    If no matching record found, -1 is returned
*/
__inline
ULONG
Ata_is_dev_listed(
    PBUSMASTER_CONTROLLER_INFORMATION BusMasterAdapters,
    ULONG VendorId,
    ULONG DeviceId,
    ULONG RevId, // min suitable revision
    ULONG lim
    )
{
    for(ULONG k=0; k<lim; k++) {
        if(BusMasterAdapters[k].nVendorId == 0xffff &&
           BusMasterAdapters[k].nDeviceId == 0xffff &&
           BusMasterAdapters[k].nRevId    == 0xff) {
            return -1;
        }
        if(BusMasterAdapters[k].nVendorId == VendorId &&
           BusMasterAdapters[k].nDeviceId == DeviceId &&
           (!RevId || BusMasterAdapters[k].nRevId <= RevId) )
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
