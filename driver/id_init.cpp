/*++

Copyright (c) 2004 Alexandr A. Telyatnikov (Alter)

Module Name:
    id_init.cpp

Abstract:
    This is the chip-specific init module for ATA/ATAPI IDE controllers
    with Busmaster DMA support

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

    Some parts of code were taken from FreeBSD 5.1 ATA driver by
         Søren Schmidt, Copyright (c) 1998-2004

--*/

extern "C" {

#include <ntddk.h>
//#include "miniport.h"

};
#include "stddef.h"

#include "atapi.h"               // includes scsi.h
#include "ntdddisk.h"
#include "ntddscsi.h"
#include "bsmaster.h"

BOOLEAN
AtapiChipDetect(
    IN PVOID HwDeviceExtension,
    IN PPCI_COMMON_CONFIG pciData, // optional
    IN ULONG DeviceNumber,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN BOOLEAN* simplexOnly
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    ULONG VendorID =  deviceExtension->DevID        & 0xffff;
    ULONG DeviceID = (deviceExtension->DevID >> 16) & 0xffff;
    ULONG RevID    =  deviceExtension->RevID;
    ULONG i, c;
    BUSMASTER_CONTROLLER_INFORMATION* DevTypeInfo;
    PHW_CHANNEL chan;
    ULONG ChipType;
    ULONG tmp32;

    KdPrint2((PRINT_PREFIX "AtapiChipDetect:\n" ));

    static BUSMASTER_CONTROLLER_INFORMATION const AcardAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0005, 1191, 0x00, ATA_UDMA2, "Acard ATP850" , ATPOLD | UNIATA_SIMPLEX_ONLY),
        PCI_DEV_HW_SPEC_BM( 0006, 1191, 0x00, ATA_UDMA4, "Acard ATP860A", 0     ),
        PCI_DEV_HW_SPEC_BM( 0007, 1191, 0x00, ATA_UDMA4, "Acard ATP860R", 0     ),
        PCI_DEV_HW_SPEC_BM( 0008, 1191, 0x00, ATA_UDMA6, "Acard ATP865A", 0     ),
        PCI_DEV_HW_SPEC_BM( 0009, 1191, 0x00, ATA_UDMA6, "Acard ATP865R", 0     ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL           , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const AcerAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 5229, 10b9, 0xc4, ATA_UDMA5, "AcerLabs Aladdin", ALINEW ),
        PCI_DEV_HW_SPEC_BM( 5229, 10b9, 0xc2, ATA_UDMA4, "AcerLabs Aladdin", ALINEW ),
        PCI_DEV_HW_SPEC_BM( 5229, 10b9, 0x20, ATA_UDMA2, "AcerLabs Aladdin", ALIOLD ),
        PCI_DEV_HW_SPEC_BM( 5229, 10b9, 0x00, ATA_WDMA2, "AcerLabs Aladdin", ALIOLD ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL              , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const AmdAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 7409, 1022, 0x00, ATA_UDMA4, "AMD 756" , AMDNVIDIA | 0x00            ),
        PCI_DEV_HW_SPEC_BM( 7411, 1022, 0x00, ATA_UDMA5, "AMD 766" , AMDNVIDIA | AMDCABLE|AMDBUG ),
        PCI_DEV_HW_SPEC_BM( 7441, 1022, 0x00, ATA_UDMA5, "AMD 768" , AMDNVIDIA | AMDCABLE        ),
        PCI_DEV_HW_SPEC_BM( 7469, 1022, 0x00, ATA_UDMA6, "AMD 8111", AMDNVIDIA | 0x00            ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL      , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const HptAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0004, 1103, 0x05, ATA_UDMA6, "HighPoint HPT372", HPT372 | 0x00       ),
        PCI_DEV_HW_SPEC_BM( 0004, 1103, 0x03, ATA_UDMA5, "HighPoint HPT370", HPT370 | 0x00       ),
        PCI_DEV_HW_SPEC_BM( 0004, 1103, 0x02, ATA_UDMA4, "HighPoint HPT368", HPT366 | 0x00       ),
        PCI_DEV_HW_SPEC_BM( 0004, 1103, 0x00, ATA_UDMA4, "HighPoint HPT366", HPT366 | HPTOLD     ),
        PCI_DEV_HW_SPEC_BM( 0005, 1103, 0x01, ATA_UDMA6, "HighPoint HPT372", HPT372 | 0x00       ),
        PCI_DEV_HW_SPEC_BM( 0005, 1103, 0x00, ATA_UDMA4, "HighPoint HPT372", HPT372 | HPTOLD     ),
        PCI_DEV_HW_SPEC_BM( 0008, 1103, 0x07, ATA_UDMA6, "HighPoint HPT374", HPT374 | 0x00       ),
        PCI_DEV_HW_SPEC_BM( 0008, 1103, 0x07, ATA_UDMA4, "HighPoint HPT374", HPT374 | HPTOLD     ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL              , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const IntelAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 1230, 8086, 0x00, ATA_WDMA2, "Intel PIIX" , 0 ),
        PCI_DEV_HW_SPEC_BM( 7010, 8086, 0x00, ATA_WDMA2, "Intel PIIX3", 0 ),
        PCI_DEV_HW_SPEC_BM( 7111, 8086, 0x00, ATA_UDMA2, "Intel PIIX4", 0 ),
        PCI_DEV_HW_SPEC_BM( 7199, 8086, 0x00, ATA_UDMA2, "Intel PIIX4", 0 ),
        PCI_DEV_HW_SPEC_BM( 84ca, 8086, 0x00, ATA_UDMA2, "Intel PIIX4", 0 ),
        PCI_DEV_HW_SPEC_BM( 7601, 8086, 0x00, ATA_UDMA2, "Intel ICH0" , 0 ),
        PCI_DEV_HW_SPEC_BM( 2421, 8086, 0x00, ATA_UDMA4, "Intel ICH"  , 0 ),
        PCI_DEV_HW_SPEC_BM( 2411, 8086, 0x00, ATA_UDMA4, "Intel ICH"  , 0 ),
        PCI_DEV_HW_SPEC_BM( 244a, 8086, 0x00, ATA_UDMA5, "Intel ICH2" , 0 ),
        PCI_DEV_HW_SPEC_BM( 244b, 8086, 0x00, ATA_UDMA5, "Intel ICH2" , 0 ),
        PCI_DEV_HW_SPEC_BM( 248a, 8086, 0x00, ATA_UDMA5, "Intel ICH3" , 0 ),
        PCI_DEV_HW_SPEC_BM( 248b, 8086, 0x00, ATA_UDMA5, "Intel ICH3" , 0 ),
        PCI_DEV_HW_SPEC_BM( 24cb, 8086, 0x00, ATA_UDMA5, "Intel ICH4" , 0 ),
        PCI_DEV_HW_SPEC_BM( 24ca, 8086, 0x00, ATA_UDMA5, "Intel ICH4" , 0 ),
        PCI_DEV_HW_SPEC_BM( 24db, 8086, 0x00, ATA_UDMA5, "Intel ICH5" , 0 ),
        PCI_DEV_HW_SPEC_BM( 24d1, 8086, 0x00, ATA_SA150, "Intel ICH5" , 0 ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL         , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const nVidiaAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 01bc, 10de, 0x00, ATA_UDMA5, "nVidia nForce"  , AMDNVIDIA | AMDBUG ),
        PCI_DEV_HW_SPEC_BM( 0065, 10de, 0x00, ATA_UDMA6, "nVidia nForce2" , AMDNVIDIA | AMDBUG ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL             , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const PromiseAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 4d33, 105a, 0x00, ATA_UDMA2, "Promise PDC20246", PROLD | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 4d38, 105a, 0x00, ATA_UDMA4, "Promise PDC20262", PRNEW | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 0d38, 105a, 0x00, ATA_UDMA4, "Promise PDC20263", PRNEW | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 0d30, 105a, 0x00, ATA_UDMA5, "Promise PDC20265", PRNEW | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 4d30, 105a, 0x00, ATA_UDMA5, "Promise PDC20267", PRNEW | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 4d68, 105a, 0x00, ATA_UDMA5, "Promise PDC20268", PRTX  | PRTX4  ),
        PCI_DEV_HW_SPEC_BM( 4d69, 105a, 0x00, ATA_UDMA6, "Promise PDC20269", PRTX  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 6268, 105a, 0x00, ATA_UDMA5, "Promise PDC20270", PRTX  | PRTX4  ),
        PCI_DEV_HW_SPEC_BM( 6269, 105a, 0x00, ATA_UDMA6, "Promise PDC20271", PRTX  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 1275, 105a, 0x00, ATA_UDMA6, "Promise PDC20275", PRTX  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 5275, 105a, 0x00, ATA_UDMA6, "Promise PDC20276", PRTX  | PRSX6K ),
        PCI_DEV_HW_SPEC_BM( 7275, 105a, 0x00, ATA_UDMA6, "Promise PDC20277", PRTX  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 3318, 105a, 0x00, ATA_SA150, "Promise PDC20318", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3319, 105a, 0x00, ATA_SA150, "Promise PDC20319", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3371, 105a, 0x00, ATA_SA150, "Promise PDC20371", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3375, 105a, 0x00, ATA_SA150, "Promise PDC20375", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3376, 105a, 0x00, ATA_SA150, "Promise PDC20376", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3377, 105a, 0x00, ATA_SA150, "Promise PDC20377", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3373, 105a, 0x00, ATA_SA150, "Promise PDC20378", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 3372, 105a, 0x00, ATA_SA150, "Promise PDC20379", PRMIO | PRSATA ),
        PCI_DEV_HW_SPEC_BM( 6617, 105a, 0x00, ATA_UDMA6, "Promise PDC20617", PRMIO | PRDUAL ),
        PCI_DEV_HW_SPEC_BM( 6626, 105a, 0x00, ATA_UDMA6, "Promise PDC20618", PRMIO | PRDUAL ),
        PCI_DEV_HW_SPEC_BM( 6629, 105a, 0x00, ATA_UDMA6, "Promise PDC20619", PRMIO | PRDUAL ),
        PCI_DEV_HW_SPEC_BM( 6620, 105a, 0x00, ATA_UDMA6, "Promise PDC20620", PRMIO | PRDUAL ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL              , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const ServerWorksAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0211, 1166, 0x00, ATA_UDMA2, "ServerWorks ROSB4", SWKS33   ),
        PCI_DEV_HW_SPEC_BM( 0212, 1166, 0x92, ATA_UDMA5, "ServerWorks CSB5" , SWKS100  ),
        PCI_DEV_HW_SPEC_BM( 0212, 1166, 0x00, ATA_UDMA4, "ServerWorks CSB5" , SWKS66   ),
        PCI_DEV_HW_SPEC_BM( 0213, 1166, 0x00, ATA_UDMA5, "ServerWorks CSB6" , SWKS100  ),
        PCI_DEV_HW_SPEC_BM( 0213, 1166, 0x00, ATA_UDMA4, "ServerWorks CSB6" , SWKS66   ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL               , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const SiliconImageAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0680, 1095, 0x00, ATA_UDMA6, "SiI 0680" , SII_SETCLK  ),
        PCI_DEV_HW_SPEC_BM( 0649, 1095, 0x00, ATA_UDMA5, "CMD 649"  , SII_INTR    ),
        PCI_DEV_HW_SPEC_BM( 0648, 1095, 0x00, ATA_UDMA4, "CMD 648"  , SII_INTR    ),
        PCI_DEV_HW_SPEC_BM( 0646, 1095, 0x07, ATA_UDMA2, "CMD 646U2", SII_ENINTR  ),
        PCI_DEV_HW_SPEC_BM( 0646, 1095, 0x00, ATA_WDMA2, "CMD 646"  , SII_ENINTR  ),
        PCI_DEV_HW_SPEC_BM( 0640, 1095, 0x00, ATA_PIO4 , "CMD 640"  , SII_ENINTR  ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL       , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const SiSAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0963, 1039, 0x00, ATA_UDMA6, "SiS 963"  , SIS133NEW ),
        PCI_DEV_HW_SPEC_BM( 0962, 1039, 0x00, ATA_UDMA6, "SiS 962"  , SIS133NEW ),

        PCI_DEV_HW_SPEC_BM( 0755, 1039, 0x00, ATA_UDMA6, "SiS 755"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0752, 1039, 0x00, ATA_UDMA6, "SiS 752"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0751, 1039, 0x00, ATA_UDMA6, "SiS 751"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0750, 1039, 0x00, ATA_UDMA6, "SiS 750"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0748, 1039, 0x00, ATA_UDMA6, "SiS 748"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0746, 1039, 0x00, ATA_UDMA6, "SiS 746"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0745, 1039, 0x00, ATA_UDMA5, "SiS 745"  , SIS100NEW ),
        PCI_DEV_HW_SPEC_BM( 0740, 1039, 0x00, ATA_UDMA5, "SiS 740"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0735, 1039, 0x00, ATA_UDMA5, "SiS 735"  , SIS100NEW ),
        PCI_DEV_HW_SPEC_BM( 0733, 1039, 0x00, ATA_UDMA5, "SiS 733"  , SIS100NEW ),
        PCI_DEV_HW_SPEC_BM( 0730, 1039, 0x00, ATA_UDMA5, "SiS 730"  , SIS100OLD ),

        PCI_DEV_HW_SPEC_BM( 0658, 1039, 0x00, ATA_UDMA6, "SiS 658"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0655, 1039, 0x00, ATA_UDMA6, "SiS 655"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0652, 1039, 0x00, ATA_UDMA6, "SiS 652"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0651, 1039, 0x00, ATA_UDMA6, "SiS 651"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0650, 1039, 0x00, ATA_UDMA6, "SiS 650"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0648, 1039, 0x00, ATA_UDMA6, "SiS 648"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0646, 1039, 0x00, ATA_UDMA6, "SiS 645DX", SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0645, 1039, 0x00, ATA_UDMA6, "SiS 645"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0640, 1039, 0x00, ATA_UDMA4, "SiS 640"  , SIS_SOUTH ),
        PCI_DEV_HW_SPEC_BM( 0635, 1039, 0x00, ATA_UDMA5, "SiS 635"  , SIS100NEW ),
        PCI_DEV_HW_SPEC_BM( 0633, 1039, 0x00, ATA_UDMA5, "SiS 633"  , SIS100NEW ),
        PCI_DEV_HW_SPEC_BM( 0630, 1039, 0x00, ATA_UDMA5, "SiS 630S" , SIS100OLD ),
        PCI_DEV_HW_SPEC_BM( 0630, 1039, 0x00, ATA_UDMA4, "SiS 630"  , SIS66,    ),
        PCI_DEV_HW_SPEC_BM( 0620, 1039, 0x00, ATA_UDMA4, "SiS 620"  , SIS66,    ),
                                
        PCI_DEV_HW_SPEC_BM( 0550, 1039, 0x00, ATA_UDMA5, "SiS 550"  , SIS66,    ),
        PCI_DEV_HW_SPEC_BM( 0540, 1039, 0x00, ATA_UDMA4, "SiS 540"  , SIS66,    ),
        PCI_DEV_HW_SPEC_BM( 0530, 1039, 0x00, ATA_UDMA4, "SiS 530"  , SIS66,    ),

        PCI_DEV_HW_SPEC_BM( 5513, 1039, 0xc2, ATA_UDMA2, "SiS 5513" , SIS33,    ),
        PCI_DEV_HW_SPEC_BM( 5513, 1039, 0x00, ATA_WDMA2, "SiS 5513" , SIS33,    ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL       , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const ViaAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 0586, 1106, 0x02, ATA_UDMA2, "VIA 82C586B", VIA33  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 0586, 1106, 0x00, ATA_WDMA2, "VIA 82C586" , VIA33  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 0596, 1106, 0x12, ATA_UDMA4, "VIA 82C596B", VIA66  | VIACLK ),
        PCI_DEV_HW_SPEC_BM( 0596, 1106, 0x00, ATA_UDMA2, "VIA 82C596" , VIA33  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 0686, 1106, 0x40, ATA_UDMA5, "VIA 82C686B", VIA100 | VIABUG ),
        PCI_DEV_HW_SPEC_BM( 0686, 1106, 0x10, ATA_UDMA4, "VIA 82C686A", VIA66  | VIACLK ),
        PCI_DEV_HW_SPEC_BM( 0686, 1106, 0x00, ATA_UDMA2, "VIA 82C686" , VIA33  | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 8231, 1106, 0x00, ATA_UDMA5, "VIA 8231"   , VIA100 | VIABUG ),
        PCI_DEV_HW_SPEC_BM( 3074, 1106, 0x00, ATA_UDMA5, "VIA 8233"   , VIA100 | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 3109, 1106, 0x00, ATA_UDMA5, "VIA 8233C"  , VIA100 | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 3147, 1106, 0x00, ATA_UDMA6, "VIA 8233A"  , VIA133 | 0x00   ),
        PCI_DEV_HW_SPEC_BM( 3177, 1106, 0x00, ATA_UDMA6, "VIA 8235"   , VIA133 | 0x00   ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1       , NULL         , -1 )
        };

    static BUSMASTER_CONTROLLER_INFORMATION const ViaSouthAdapters[] = {
        PCI_DEV_HW_SPEC_BM( 3112, 1106, 0x00, -1, "VIA 8361", VIASOUTH ),
        PCI_DEV_HW_SPEC_BM( 0305, 1106, 0x00, -1, "VIA 8363", VIASOUTH ),
        PCI_DEV_HW_SPEC_BM( 0391, 1106, 0x00, -1, "VIA 8371", VIASOUTH ),
        PCI_DEV_HW_SPEC_BM( 3102, 1106, 0x00, -1, "VIA 8662", VIASOUTH ),
        PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1, NULL      , -1 )
        };
                                  
    switch(VendorID) {
    case ATA_ACARD_ID: 
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&AcardAdapters[0];
        break;
    case ATA_ACER_LABS_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&AcerAdapters[0];
        break;
    case ATA_AMD_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&AmdAdapters[0];
        break;
    case ATA_HIGHPOINT_ID: 
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&HptAdapters[0];
        break;
    case ATA_INTEL_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&IntelAdapters[0];
        break;
    case ATA_NVIDIA_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&nVidiaAdapters[0];
        break;
    case ATA_PROMISE_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&PromiseAdapters[0];
        break;
    case ATA_SERVERWORKS_ID: 
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&ServerWorksAdapters[0];
        break;
    case ATA_SILICON_IMAGE_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&SiliconImageAdapters[0];
        break;
    case ATA_SIS_ID:
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&SiSAdapters[0];
        i = AtapiFindListedDev(DevTypeInfo, -1, HwDeviceExtension, SystemIoBusNumber, -1, NULL);
        goto for_ugly_chips;
    case ATA_VIA_ID: 
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&ViaSouthAdapters[0];
        i = AtapiFindListedDev(DevTypeInfo, -1, HwDeviceExtension, SystemIoBusNumber, slotNumber, NULL);
        if(i != -1) {
            deviceExtension->HwFlags |= VIASOUTH;
        }
        DevTypeInfo = (BUSMASTER_CONTROLLER_INFORMATION*)&ViaAdapters[0];
        i = AtapiFindListedDev(DevTypeInfo, -1, HwDeviceExtension, SystemIoBusNumber, slotNumber, NULL);
        goto for_ugly_chips;
    default:

        deviceExtension->MaxTransferMode = deviceExtension->BaseIoAddressBM_0 ? ATA_DMA : ATA_PIO4;
        /* do extra chipset specific setups */
        switch(deviceExtension->DevID) {

      //case ATA_CYPRESS_ID:
        case 0xc6931080:         /* 82c693 ATA controller */
            deviceExtension->MaxTransferMode = ATA_WDMA2;
            break;

        case 0x000116ca:         /* Cenatek Rocket Drive controller */
            deviceExtension->MaxTransferMode = ATA_WDMA2;
            break;

/*      case ATA_CYRIX_ID:
            DevTypeInfo = &CyrixAdapters[0];
            break;*/
        case 0x01021078:        /* Cyrix 5530 ATA33 controller */
            deviceExtension->MaxTransferMode = ATA_UDMA2;
            ConfigInfo->AlignmentMask = 0x0f;
            break;

        case 0x06401095:        /* CMD 640 known bad, no DMA */
        case 0x06011039:
            *simplexOnly = TRUE; 

            /* FALLTHROUGH */

        case 0x10001042:        /* RZ 100? known bad, no DMA */
        case 0x10011042:

            if(deviceExtension->BaseIoAddressBM_0)
                ScsiPortFreeDeviceBase(HwDeviceExtension,
                                       deviceExtension->BaseIoAddressBM_0);

            deviceExtension->BaseIoAddressBM_0 = 0;
            deviceExtension->BusMaster = FALSE;
            deviceExtension->MaxTransferMode = ATA_PIO4;
            break;

        default:
            return FALSE;
        }
        return TRUE;
    }

    i = Ata_is_dev_listed(DevTypeInfo, VendorID, DeviceID, RevID, -1);
for_ugly_chips:
    if(i == -1) {
        return FALSE;
    }
    deviceExtension->MaxTransferMode =  DevTypeInfo[i].MaxTransferMode;
    deviceExtension->HwFlags         |= DevTypeInfo[i].RaidFlags;

    if(deviceExtension->HwFlags & UNIATA_SIMPLEX_ONLY) {
        KdPrint2((PRINT_PREFIX "UNIATA_SIMPLEX_ONLY\n" ));
        *simplexOnly = TRUE; 
    }

    ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;

    switch(VendorID) {
    case ATA_PROMISE_ID:
        if(ChipType != PRMIO) {
            break;
        }
        deviceExtension->NumberChannels = 4;
        if(!pciData) {
            break;
        }
        deviceExtension->BaseMemAddress = AtapiGetIoRange(HwDeviceExtension, ConfigInfo, pciData, SystemIoBusNumber,
                                4, 0, 0x400);
        deviceExtension->BaseIoAddressBM_0 = (PIDE_BUSMASTER_REGISTERS)(0xffff0000 + sizeof(IDE_REGISTERS_1) + sizeof(IDE_REGISTERS_2));
        for(c=0; c<4; c++) {
            chan = &deviceExtension->chan[c];
            chan->BaseMemAddress = deviceExtension->BaseMemAddress;
            chan->BaseMemAddressOffset = 0xffff0000 + (c << 12);
            deviceExtension->BaseIoAddress1[c] = (PIDE_REGISTERS_1)(0xffff0000 + (c << 12));
            for (i=0; i<=sizeof(IDE_REGISTERS_1); i++) {
                chan->RegTranslation[i] = 0x200 + (i << 2) + (c << 7);
            }
            deviceExtension->BaseIoAddress2[c] = (PIDE_REGISTERS_2)(0xffff0000 + (c << 12) + sizeof(IDE_REGISTERS_1));
            chan->RegTranslation[i] = 0x238 + (c << 7);
            i++;
            deviceExtension->BaseIoAddressBM[c] = (PIDE_BUSMASTER_REGISTERS)(0xffff0000 + (c << 12) + sizeof(IDE_REGISTERS_1) + sizeof(IDE_REGISTERS_2));
            chan->RegTranslation[(ULONG)(&((PIDE_BUSMASTER_REGISTERS)i)->Command)] = 0x260 + (c << 7);
            chan->RegTranslation[(ULONG)(&((PIDE_BUSMASTER_REGISTERS)i)->PRD_Table)] = 0x244 + (c << 7);
            chan->RegTranslation[(ULONG)(&((PIDE_BUSMASTER_REGISTERS)i)->DeviceSpecific0)] = (c << 2);
        }
        break;
    case ATA_SIS_ID:
        if(ChipType != SIS_SOUTH)
            break;
        ChangePciConfig1(0x57, (a & 0x7f));
        GetPciConfig1(0x00, tmp32);
        if(tmp32 == ATA_SIS5518) {
            ChipType = SIS133NEW;
            deviceExtension->HwFlags = (deviceExtension->HwFlags & ~CHIPTYPE_MASK) | SIS133NEW;
            KdPrint2((PRINT_PREFIX "AtapiChipInit: SiS 96X DMA %#x controller\n", deviceExtension->MaxTransferMode));
        } else {
            static BUSMASTER_CONTROLLER_INFORMATION const SiSSouthAdapters[] = {
                PCI_DEV_HW_SPEC_BM( 0008, 1039, 0x10, -1, "SiS 961", 0 ),
                PCI_DEV_HW_SPEC_BM( ffff, ffff, 0xff, -1, NULL     , -1 )
                };
            if(AtapiFindListedDev((BUSMASTER_CONTROLLER_INFORMATION*)&SiSSouthAdapters[0],
                 -1, HwDeviceExtension, SystemIoBusNumber, -1, NULL)) {
                deviceExtension->HwFlags = (deviceExtension->HwFlags & ~CHIPTYPE_MASK) | SIS133OLD;
            } else {
                deviceExtension->HwFlags = (deviceExtension->HwFlags & ~CHIPTYPE_MASK) | SIS133OLD;
                deviceExtension->MaxTransferMode = ATA_UDMA5;
            }
            KdPrint2((PRINT_PREFIX "AtapiChipInit: SiS 961 DMA %#x controller\n", deviceExtension->MaxTransferMode));
        }
        ChangePciConfig1(0x57, (a | 0x80));
    }

    return TRUE;

} // end AtapiChipDetect()


/*
    Do some 'magic staff' for VIA SouthBridge
    This will prevent data losses
*/
VOID
AtapiViaSouthBridgeFixup(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  SystemIoBusNumber,
    IN ULONG  slotNumber
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PCI_COMMON_CONFIG     pciData;
    ULONG                 funcNumber;
    ULONG                 busDataRead;

    ULONG   VendorID;
    ULONG   DeviceID;
    PCI_SLOT_NUMBER       slotData;
    ULONG dev_id;
    BOOLEAN found = FALSE;

    slotData.u.AsULONG = slotNumber;
    for(funcNumber = 0; funcNumber < PCI_MAX_FUNCTION; funcNumber++) {
        
        slotData.u.bits.FunctionNumber = funcNumber;

        busDataRead = ScsiPortGetBusData(HwDeviceExtension,
                                         PCIConfiguration,
                                         SystemIoBusNumber,
                                         slotData.u.AsULONG,
                                         &pciData,
                                         PCI_COMMON_HDR_LENGTH);

        if (busDataRead < PCI_COMMON_HDR_LENGTH) {
            continue;
        }

        VendorID  = pciData.VendorID;
        DeviceID  = pciData.DeviceID;
        dev_id = (VendorID | (DeviceID << 16));

        if (dev_id == 0x03051106 ||         /* VIA VT8363 */
            dev_id == 0x03911106 ||         /* VIA VT8371 */
            dev_id == 0x31021106 ||         /* VIA VT8662 */
            dev_id == 0x31121106) {         /* VIA VT8361 */
            UCHAR reg76;
            
            GetPciConfig1(0x76, reg76);
     
            if ((reg76 & 0xf0) != 0xd0) {
                SetPciConfig1(0x75, 0x80);
                SetPciConfig1(0x76, (reg76 & 0x0f) | 0xd0);
            }
            found = TRUE;
            break;
        }
    }
    if(!found) {
        deviceExtension->HwFlags &= ~VIABUG;
    }
} // end AtapiViaSouthBridgeFixup()


ULONG
hpt_cable80(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG channel               // physical channel number (0-1)
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;

    UCHAR reg, val, res;
    PCI_SLOT_NUMBER slotData;

    slotData.u.AsULONG = deviceExtension->slotNumber;

    if(ChipType == HPT374 && slotData.u.bits.FunctionNumber == 1)  {
        reg = channel ? 0x57 : 0x53;
        GetPciConfig1(reg, val);
        SetPciConfig1(reg, val | 0x80);
    }
    else {
        reg = 0x5b;
        GetPciConfig1(reg, val);
        SetPciConfig1(reg, val & 0xfe);
    }
    GetPciConfig1(0x5a, res);
    res = res & (channel ? 0x01 : 0x02);
    SetPciConfig1(reg, val);
    return !res;
} // end hpt_cable80()


BOOLEAN
AtapiChipInit(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG c
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    ULONG VendorID =  deviceExtension->DevID        & 0xffff;
    ULONG DeviceID = (deviceExtension->DevID >> 16) & 0xffff;
    ULONG RevID    =  deviceExtension->RevID;
//    ULONG i;
//    BUSMASTER_CONTROLLER_INFORMATION* DevTypeInfo;
    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;
    ULONG ChipFlags = deviceExtension->HwFlags & CHIPFLAG_MASK;
    PHW_CHANNEL chan;
    UCHAR tmp8;

    KdPrint2((PRINT_PREFIX "AtapiChipInit:\n" ));

    if(deviceExtension->UnknownDev) {
        KdPrint2((PRINT_PREFIX "  Unknown chip, exiting\n" ));
        return TRUE;
    }

    if(c == -1) {
        for(c=0; c<deviceExtension->NumberChannels; c++) {
            chan = &deviceExtension->chan[c];
            chan->MaxTransferMode = deviceExtension->MaxTransferMode;
        }
        c = -1;
    } else {
        chan = &deviceExtension->chan[c];
        chan->MaxTransferMode = deviceExtension->MaxTransferMode;
    }

    switch(VendorID) {
//  case ATA_ACARD_ID: 
//      break;
    case ATA_ACER_LABS_ID:
        /* deactivate the ATAPI FIFO and enable ATAPI UDMA */
//        ChangePciConfig1(0x53, ((a & ~0x01) | 0x02));  // 4.9
        ChangePciConfig1(0x53, (a | 0x03));
        if(ChipFlags & ALINEW) {
            /* enable cable detection and UDMA support on newer chips */
            ChangePciConfig1(0x4b, (a | 0x09));
            if(c == -1) {
                for(c=0; c<deviceExtension->NumberChannels; c++) {
                    chan = &deviceExtension->chan[c];
                    GetPciConfig1(0x4a, tmp8);
                    if(!(tmp8 & (1 << c))) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                GetPciConfig1(0x4a, tmp8);
                if(!(tmp8 & (1 << c))) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        } else {
            // ATAPI DMA R/O
            if(c == -1) {
                for(c=0; c<deviceExtension->NumberChannels; c++) {
                    deviceExtension->chan[c].ChannelCtrlFlags |= CTRFLAGS_DMA_RO;
                }
            } else {
                deviceExtension->chan[c].ChannelCtrlFlags |= CTRFLAGS_DMA_RO;
            }
        }
        break;
    case ATA_AMD_ID:
        /* set prefetch, postwrite */
        if(ChipFlags & AMDBUG) {
            ChangePciConfig1(0x41, (a & 0x0f));
        } else {
            ChangePciConfig1(0x41, (a | 0xf0));
        }
        if(deviceExtension->MaxTransferMode < ATA_UDMA2)
            break;
        if(ChipFlags & AMDCABLE) {
            if(c == -1) {
                for(c=0; c<deviceExtension->NumberChannels; c++) {
                    chan = &deviceExtension->chan[c];
                    GetPciConfig1(0x42, tmp8);
                    if(!(tmp8 & (1 << c))) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                GetPciConfig1(0x42, tmp8);
                if(!(tmp8 & (1 << c))) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        }
        break;
    case ATA_HIGHPOINT_ID: 
        if(ChipFlags & HPTOLD) {
            /* turn off interrupt prediction */
            ChangePciConfig1(0x51, (a & ~0x80));
        } else {
            /* turn off interrupt prediction */
            ChangePciConfig1(0x51, (a & ~0x03));
            ChangePciConfig1(0x55, (a & ~0x03));
            /* turn on interrupts */
            ChangePciConfig1(0x5a, (a & ~0x10));
            /* set clocks etc */
            if(ChipType < HPT372) {
                SetPciConfig1(0x5b, 0x22);
            } else {
                ChangePciConfig1(0x5b, ((a & 0x01) | 0x20));
            }
        }
        if(c == -1) {
            for(c=0; c<deviceExtension->NumberChannels; c++) {
                chan = &deviceExtension->chan[c];
                if(!hpt_cable80(deviceExtension, c)) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        } else {
            chan = &deviceExtension->chan[c];
            if(!hpt_cable80(deviceExtension, c)) {
                chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
            }
        }
        break;
    case ATA_INTEL_ID: {
        USHORT reg54;
        if(deviceExtension->MaxTransferMode >= ATA_SA150)
            break;
        if(deviceExtension->MaxTransferMode < ATA_UDMA2)
            break;
        if(c == -1) {
            for(c=0; c<deviceExtension->NumberChannels; c++) {
                chan = &deviceExtension->chan[c];
                GetPciConfig2(0x54, reg54);
                if( ((reg54 >> (c*2)) & 30) != 30) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        } else {
            chan = &deviceExtension->chan[c];
            GetPciConfig2(0x54, reg54);
            if( ((reg54 >> (c*2)) & 30) != 30) {
                chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
            }
        }
        break; }
    case ATA_NVIDIA_ID:
        /* set prefetch, postwrite */
        if(ChipFlags & AMDBUG) {
            ChangePciConfig1(0x51, (a & 0x0f));
        } else {
            ChangePciConfig1(0x51, (a | 0xf0));
        }
        break;
    case ATA_PROMISE_ID: {
        USHORT Reg50;
        switch(ChipType) {
        case PROLD:
        case PRNEW:
            if(c == -1) {
                for(c=0; c<2; c++) {
                    chan = &deviceExtension->chan[c];
                    GetPciConfig2(0x50, Reg50);
                    if(Reg50 & (1 << (c+10))) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                GetPciConfig2(0x50, Reg50);
                if(Reg50 & (1 << (c+10))) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        case PRTX:
            if(c == -1) {
                for(c=0; c<2; c++) {
                    chan = &deviceExtension->chan[c];
                    AtapiWritePort1(chan, &deviceExtension->BaseIoAddressBM[c]->DeviceSpecific0, 0x0b);
                    if(AtapiReadPort1(chan, &deviceExtension->BaseIoAddressBM[c]->DeviceSpecific1) & 0x04) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                AtapiWritePort1(chan, &deviceExtension->BaseIoAddressBM[c]->DeviceSpecific0, 0x0b);
                if(AtapiReadPort1(chan, &deviceExtension->BaseIoAddressBM[c]->DeviceSpecific1) & 0x04) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        case PRMIO:
            deviceExtension->MemIo = TRUE;
            if(ChipFlags & PRSATA) {
                AtapiWritePort4(NULL, (PULONG)((PUCHAR)(deviceExtension->BaseMemAddress) + 0x6c), 0x00ff0033);
            }
            if(c == -1) {
                for(c=0; c<4; c++) {
                    chan = &deviceExtension->chan[c];
                    AtapiWritePort4(chan, (PULONG)(&deviceExtension->BaseIoAddressBM[c]->Command), 
                        (AtapiReadPort4(chan, (PULONG)(deviceExtension->BaseIoAddressBM[c]->Command)) & ~0x00000f8f) | c );
                    AtapiWritePort4(chan, (PULONG)(&deviceExtension->BaseIoAddressBM[c]->DeviceSpecific0), 0x00000001);
                    if(chan->MaxTransferMode < ATA_SA150 &&
                       (AtapiReadPort4(chan, (PULONG)(deviceExtension->BaseIoAddressBM[c]->Command)) & 0x01000000)) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                AtapiWritePort4(chan, (PULONG)(&deviceExtension->BaseIoAddressBM[c]->Command), 
                    (AtapiReadPort4(chan, (PULONG)(&deviceExtension->BaseIoAddressBM[c]->Command)) & ~0x00000f8f) | c );
                AtapiWritePort4(chan, (PULONG)(&deviceExtension->BaseIoAddressBM[c]->DeviceSpecific0), 0x00000001);
                if(chan->MaxTransferMode < ATA_SA150 &&
                   (AtapiReadPort4(chan, (PULONG)(deviceExtension->BaseIoAddressBM[c]->Command)) & 0x01000000)) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
            break;
        }
        break; }
    case ATA_SERVERWORKS_ID: 
        if(ChipType > SWKS33) {
            ChangePciConfig1(0x5a, ((a & ~0x40) | ((ChipType == SWKS100) ? 0x03 : 0x02)));
        } else {
            ChangePciConfig4(0x64, ((a & ~0x00002000) | 0x00004000));
        }
        break;
    case ATA_SILICON_IMAGE_ID:
        if(ChipFlags & SII_ENINTR) {
            SetPciConfig1(0x71, 0x01);
        }
        if(ChipFlags & SII_SETCLK) {
            GetPciConfig1(0x8a, tmp8);
            if ((tmp8 & 0x30) != 0x10)
                ChangePciConfig1(0x8a, (a & 0x0F) | 0x10);
            GetPciConfig1(0x8a, tmp8);
            if ((tmp8 & 0x30) != 0x10) {
                KdPrint2((PRINT_PREFIX "Sil 0680 could not set ATA133 clock"));
            }
        }
        break;
    case ATA_SIS_ID:
        switch(ChipType) {
        case SIS33:
            break;
        case SIS66:
        case SIS100OLD:
            ChangePciConfig1(0x52, (a | 0x04));
            break;
        case SIS100NEW:
        case SIS133OLD:
            ChangePciConfig1(0x49, (a | 0x01));
            break;
        case SIS133NEW:
            ChangePciConfig2(0x50, (a & 0xfff7));
            ChangePciConfig2(0x52, (a & 0xfff7));
            break;
        }
        if(ChipType == SIS133NEW) {
            USHORT tmp16;
            if(c == -1) {
                for(c=0; c<2; c++) {
                    chan = &deviceExtension->chan[c];
                    GetPciConfig2(c ? 0x52 : 0x50, tmp16);
                    if(tmp16 & 0x8000) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                GetPciConfig2(c ? 0x52 : 0x50, tmp16);
                if(tmp16 & 0x8000) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        } else {
            if(c == -1) {
                for(c=0; c<2; c++) {
                    chan = &deviceExtension->chan[c];
                    GetPciConfig1(48, tmp8);
                    if(tmp8 & (0x10 << c)) {
                        chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                    }
                }
            } else {
                chan = &deviceExtension->chan[c];
                GetPciConfig1(48, tmp8);
                if(tmp8 & (0x10 << c)) {
                    chan->MaxTransferMode = min(deviceExtension->MaxTransferMode, ATA_UDMA2);
                }
            }
        }
        break;
    case ATA_VIA_ID: 
        /* prepare for ATA-66 on the 82C686a and 82C596b */
        if(ChipFlags & VIACLK) {
            ChangePciConfig4(0x50, (a | 0x030b030b));
        }
        /* the southbridge might need the data corruption fix */
        if(ChipFlags & VIABUG) {
            AtapiViaSouthBridgeFixup(HwDeviceExtension, PCIConfiguration,
                                     SystemIoBusNumber, slotNumber);
        }
        /* set prefetch, postwrite */
        ChangePciConfig1(0x41, (a | 0xf0));

        /* set fifo configuration half'n'half */
        ChangePciConfig1(0x43, ((a & 0x90) | 0x2a));

        /* set status register read retry */
        ChangePciConfig1(0x44, (a | 0x08));

        /* set DMA read & end-of-sector fifo flush */
        ChangePciConfig1(0x46, ((a & 0x0c) | 0xf0));

        /* set sector size */
        SetPciConfig2(0x60, DEV_BSIZE);
        SetPciConfig2(0x68, DEV_BSIZE);

        break;
    }

    return TRUE;
}
