/*++

Copyright (c) 2002-2003 Alexander A. Telyatnikov (Alter)

Module Name:
    id_dma.cpp

Abstract:
    This is the miniport driver for ATAPI IDE controllers
    With Busmaster DMA support

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

    This module is a port from FreeBSD 4.3-4.6 ATA driver (ata-dma.c) by
         Søren Schmidt, Copyright (c) 1998,1999,2000,2001

    Changed defaulting-to-generic-PIO/DMA policy
    Added PIO settings for VIA
    Optimized VIA/AMD/nVidia init part
    Optimized Promise TX2 init part
         by Alex A. Telyatnikov (Alter), (c) 2002


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

static const ULONG valid_udma[7] = {0,0,2,0,4,5,6};

static const CHAR retry_Wdma[MAX_RETRIES+1] = {2, 2, 2,-1,-1,-1};
static const CHAR retry_Udma[MAX_RETRIES+1] = {6, 2,-1,-1,-1,-1};

VOID
cyrix_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    );

VOID
promise_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    );

VOID
hpt_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    );

VOID
via82c_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    );

ULONG
hpt_cable80(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG channel            // physical channel number (0-1)
    );

#define ATAPI_DEVICE(de, ldev)    (de->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE)

VOID
AtapiDmaAlloc(
    IN PVOID HwDeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN ULONG lChannel          // logical channel,
    )
{
    return;
/*    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;

    if(!ConfigInfo)
        return;

    if(deviceExtension->chan[lChannel].dma_tab)
        return;
    if( !deviceExtension->dma_tab_0 &&
       !(deviceExtension->HbaCtrlFlags & HBAFLAGS_DMA_DISABLED)) {
        deviceExtension->dma_tab_0 = (PBM_DMA_ENTRY)
            ScsiPortGetUncachedExtension(HwDeviceExtension, ConfigInfo, PAGE_SIZE*2);
    }
    if(!deviceExtension->dma_tab_0 ||
       ((LONG)(deviceExtension->dma_tab_0) == -1) ) {
        deviceExtension->dma_tab_0 = NULL;
        deviceExtension->HbaCtrlFlags |= HBAFLAGS_DMA_DISABLED;
        return;
    }
    if(!deviceExtension->chan[lChannel].dma_tab) {
        deviceExtension->chan[lChannel].dma_tab = (PBM_DMA_ENTRY)
            ((PUCHAR)(deviceExtension->dma_tab_0) + (lChannel ? PAGE_SIZE : 0));
    }*/
} // end AtapiDmaAlloc()

/*VOID
AtapiDmaAlloc(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);
    ULONG lChannel = GET_CHANNEL(Srb);
    PHW_CHANNEL chan = &(deviceExtension->chan[lChannel]);


}*/

BOOLEAN
AtapiDmaSetup(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN PUCHAR data,
    IN ULONG count
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG dma_count, dma_base;
    ULONG i;
    PHW_CHANNEL chan = &(deviceExtension->chan[lChannel]);
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);

    KdPrint2((PRINT_PREFIX "AtapiDmaSetup: mode %x\n", deviceExtension->lun[lChannel*2+DeviceNumber].TransferMode ));
    if(!count) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: count=0\n" ));
        return FALSE;
    }
/*    if(!chan->dma_tab) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: dma_tab=NULL\n" ));
        return FALSE;
    }*/
    
    dma_base = AtapiPhysToVirtAddr(HwDeviceExtension, NULL, &(AtaReq->dma_tab) /*chan->dma_tab*/, &i);
    if(!dma_base || !i || ((LONG)(dma_base) == -1)) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: No BASE\n" ));
        return FALSE;
    }
    AtaReq->dma_base = dma_base;

    dma_base = AtapiPhysToVirtAddr(HwDeviceExtension, Srb, data, &dma_count);
    if(!dma_count || ((LONG)(dma_base) == -1)) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: No 1st block\n" ));
        return FALSE;
    }
    dma_count = min(count, (PAGE_SIZE - ((ULONG)data & PAGE_MASK)));
    data += dma_count;
    count -= dma_count;
    i = 0;

    while (count) {
	AtaReq->dma_tab[i].base = dma_base;
	AtaReq->dma_tab[i].count = (dma_count & 0xffff);
	i++; 
	if (i >= ATA_DMA_ENTRIES) {
	    KdPrint2((PRINT_PREFIX "too many segments in DMA table\n" ));
	    return FALSE;
	}
        dma_base = AtapiPhysToVirtAddr(HwDeviceExtension, Srb, data, &dma_count);
        if(!dma_count || ((LONG)(dma_base) == -1)) {
            KdPrint2((PRINT_PREFIX "AtapiDmaSetup: No NEXT block\n" ));
            return FALSE;
        }
	dma_count = min(count, PAGE_SIZE);
	data += min(count, PAGE_SIZE);
	count -= min(count, PAGE_SIZE);
    }
    AtaReq->dma_tab[i].base = dma_base;
    AtaReq->dma_tab[i].count = (dma_count & 0xffff) | ATA_DMA_EOT;

    AtaReq->Flags |= REQ_FLAG_DMA_OPERATION;

    KdPrint2((PRINT_PREFIX "AtapiDmaSetup: OK\n" ));
    return TRUE;

} // end AtapiDmaSetup()

VOID
AtapiDmaStart(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PIDE_BUSMASTER_REGISTERS BaseIoAddressBM = deviceExtension->BaseIoAddressBM[lChannel];
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);

    KdPrint2((PRINT_PREFIX "AtapiDmaStart: %s on %x:%x\n", 
        (Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? "read" : "write",
        lChannel, DeviceNumber ));

    switch(deviceExtension->DevID) {
    case 0x0d38105a:  /* Promise Fasttrak 66 */
    case 0x4d38105a:  /* Promise Ultra/Fasttrak 66 */
    case 0x0d30105a:  /* Promise OEM ATA 100 */
    case 0x4d30105a:  /* Promise Ultra/Fasttrak 100 */
        ScsiPortWritePortUchar((PUCHAR)(BaseIoAddressBM)+0x11, 
              ScsiPortReadPortUchar((PUCHAR)(BaseIoAddressBM)+0x11) |
                  (DeviceNumber ? 0x08 : 0x02));
        ScsiPortWritePortUlong((PULONG)((PUCHAR)(BaseIoAddressBM) + (DeviceNumber ? 0x24 : 0x20)), 
              ((Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? 0x05000000 : 0x06000000) | (Srb->DataTransferLength >> 1)
              );
        break;
    }

    // set flag
    deviceExtension->chan[lChannel].ChannelCtrlFlags |= CTRFLAGS_DMA_ACTIVE;
    // set pointer to Pointer Table
    ScsiPortWritePortUlong(&(BaseIoAddressBM->PRD_Table), 
          AtaReq->dma_base
          );
    // set transfer direction
    ScsiPortWritePortUchar(&(BaseIoAddressBM->Command), 
          (Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? BM_COMMAND_READ : BM_COMMAND_WRITE);
    // clear Error & Intr bits (writeing 1 clears bits)
    // set DMA capability bit
    ScsiPortWritePortUchar(&(BaseIoAddressBM->Status), 
         ScsiPortReadPortUchar(&(BaseIoAddressBM->Status)) |
	     (BM_STATUS_INTR | BM_STATUS_ERR) /*|
	     (DeviceNumber ? BM_STATUS_DRIVE_1_DMA : BM_STATUS_DRIVE_0_DMA)*/);
    // begin transaction
    ScsiPortWritePortUchar(&(BaseIoAddressBM->Command), 
         ScsiPortReadPortUchar(&(BaseIoAddressBM->Command)) |
             BM_COMMAND_START_STOP);

    // Enable interrupts
//    ScsiPortWritePortUchar(&(deviceExtension->BaseIoAddress2[lChannel]->AlternateStatus), IDE_DC_A_4BIT );

} // end AtapiDmaStart()

UCHAR
AtapiDmaDone(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PIDE_BUSMASTER_REGISTERS BaseIoAddressBM = deviceExtension->BaseIoAddressBM[lChannel];

    KdPrint2((PRINT_PREFIX "AtapiDmaDone:\n" ));

    switch(deviceExtension->DevID) {
    case 0x0d38105a:  /* Promise Fasttrak 66 */
    case 0x4d38105a:  /* Promise Ultra/Fasttrak 66 */
    case 0x0d30105a:  /* Promise OEM ATA 100 */
    case 0x4d30105a:  /* Promise Ultra/Fasttrak 100 */
        ScsiPortWritePortUlong((PULONG)((PUCHAR)(BaseIoAddressBM) + (DeviceNumber ? 0x24 : 0x20)), 
              0
              );
        ScsiPortWritePortUchar((PUCHAR)(BaseIoAddressBM)+0x11, 
              ScsiPortReadPortUchar((PUCHAR)(BaseIoAddressBM)+0x11) &
                  ~(DeviceNumber ? 0x08 : 0x02));
        break;
    }

    // begin transaction
    ScsiPortWritePortUchar(&(BaseIoAddressBM->Command), 
         ScsiPortReadPortUchar(&(BaseIoAddressBM->Command)) &
             ~BM_COMMAND_START_STOP);
    // clear flag
    deviceExtension->chan[lChannel].ChannelCtrlFlags &= ~CTRFLAGS_DMA_ACTIVE;

    return ScsiPortReadPortUchar(&(BaseIoAddressBM->Status)) & BM_STATUS_MASK;

} // end AtapiDmaDone()

VOID
AtapiDmaReinit(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG ldev,
    IN PATA_REQ AtaReq
    )
{
    if(AtaReq->Flags & REQ_FLAG_FORCE_DOWNRATE) {
        KdPrint2((PRINT_PREFIX 
                    "AtapiDmaReinit: FORCE_DOWNRATE on Device %d\n", ldev & 1));
        AtapiDmaInit(deviceExtension, ldev & 1, ldev >> 1,
                     (CHAR)AtaPioMode(&(deviceExtension->lun[ldev].IdentifyData)), 
                     min( retry_Wdma[AtaReq->retry],
                          (CHAR)AtaWmode(&(deviceExtension->lun[ldev].IdentifyData)) ),
                     min( retry_Udma[AtaReq->retry],
                          (CHAR)AtaUmode(&(deviceExtension->lun[ldev].IdentifyData)) ) );
//            deviceExtension->lun[ldev].DeviceFlags &= ~DFLAGS_FORCE_DOWNRATE;
    } else
    if(/*!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_FORCE_DOWNRATE) &&*/
        (deviceExtension->lun[ldev].OrigTransferMode >
         deviceExtension->lun[ldev].TransferMode)) {
        // restore IO mode
        KdPrint2((PRINT_PREFIX 
                    "AtapiDmaReinit: restore IO mode on Device %d\n", ldev & 1));
        AtapiDmaInit__(deviceExtension, ldev);
    }
} // end AtapiDmaReinit()

VOID
AtapiDmaInit__(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG ldev
    )
{
    PHW_LU_EXTENSION lun = &(deviceExtension->lun[ldev]);

    if(deviceExtension->lun[ldev].IdentifyData.SupportDma) {
        KdPrint2((PRINT_PREFIX 
                    "AtapiHwInitialize: Set (U)DMA on Device %d\n", ldev & 1));
        AtapiDmaInit(deviceExtension, ldev & 1, ldev >> 1,
                     (CHAR)AtaPioMode(&(lun->IdentifyData)),
                     (CHAR)AtaWmode(&(lun->IdentifyData)),
                     (CHAR)AtaUmode(&(lun->IdentifyData)) );
    } else {
        KdPrint2((PRINT_PREFIX 
                    "AtapiHwInitialize: Set PIO on Device %d\n", ldev & 1));
        AtapiDmaInit(deviceExtension, ldev & 1, ldev >> 1,
                     (CHAR)AtaPioMode(&(lun->IdentifyData)), -1, -1);
    }
} // end AtapiDmaInit__()

VOID
AtapiDmaInit(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
                               // is always 0 except simplex-only controllers
    IN CHAR apiomode,
    IN CHAR wdmamode,
    IN CHAR udmamode
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG Channel = deviceExtension->Channel + lChannel;
    LONG statusByte = 0;
    ULONG dev = Channel*2 + DeviceNumber;
    ULONG ldev = lChannel*2 + DeviceNumber;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    LONG i;
//    CHAR ph_piomode;
    static const UCHAR via_modes[5][7] = {
        { 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00 },	/* ATA33 */
        { 0x00, 0x00, 0xea, 0x00, 0xe8, 0x00, 0x00 },	/* ATA66 */
        { 0x00, 0x00, 0xf4, 0x00, 0xf1, 0xf0, 0x00 },	/* ATA100 */
        { 0xfc, 0x00, 0xf6, 0x00, 0xf2, 0xf1, 0xf0 },   /* VIA ATA133 */
        { 0x00, 0x00, 0xc0, 0x00, 0xc5, 0xc6, 0xc7 }};  /* AMD/nVIDIA */
    UCHAR *reg_val = NULL;

    USHORT reg50;

    /* set our most pessimistic default mode */
    deviceExtension->lun[ldev].TransferMode = ATA_PIO;
    if(!deviceExtension->BaseIoAddressBM[lChannel]) {
        wdmamode = udmamode = -1;
    }
/*    if(!deviceExtension->BaseIoAddressBM[lChannel] ||
        (wdmamode < 0 && udmamode < 0)) {
        // try to init advanced PIO mode if no DMA available
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            ph_piomode = (CHAR)AtaPio2Mode(apiomode),
                            ATA_C_F_SETXFER,ATA_WAIT_BASE_READY);
        if (!(statusByte & IDE_STATUS_ERROR)) {
            deviceExtension->lun[ldev].TransferMode = ph_piomode;
            return;
        }
        // hm... can't init max PIO mode
        // try downclock...
        for(;apiomode != 0xff; apiomode--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ph_piomode = (CHAR)AtaPio2Mode(apiomode),
                                ATA_C_F_SETXFER,ATA_WAIT_BASE_READY);
            if (statusByte == IDE_STATUS_SUCCESS) {
                deviceExtension->lun[ldev].TransferMode = ph_piomode;
                return;
            }
        }
//        BrutePoint();
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            ATA_PIO,
                            ATA_C_F_SETXFER,ATA_WAIT_BASE_READY);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO;
        return;
    }*/
/*    if (ATAPI_DEVICE(deviceExtension, ldev)) {
        udmamode = -1;
        wdmamode = -1;
    }*/

    if (udmamode > 2 && !deviceExtension->lun[ldev].IdentifyData.HwResCableId) {
        KdPrint2((PRINT_PREFIX "AtapiDmaInit: DMA limited to UDMA33, non-ATA66 compliant cable"));
        udmamode = 2;
    }

    switch(deviceExtension->DevID) {

    case 0x24db8086:	/* Intel ICH5 */
    case 0x24cb8086:	/* Intel ICH4 */
    case 0x248a8086:	/* Intel ICH3 mobile */
    case 0x248b8086:	/* Intel ICH3 */
    case 0x244a8086:	/* Intel ICH2 mobile */ 
    case 0x244b8086:	/* Intel ICH2 */
        if (udmamode >= 5) {
            ULONG  mask48, new48;
            USHORT word54;

            GetPciConfig2(0x54, word54);
            if (word54 & (0x10 << dev)) {

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA5,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

        	if (!(statusByte & IDE_STATUS_ERROR)) {
        	    mask48 = (1 << dev) + (3 << (16 + (dev << 2)));
        	    new48 = (1 << dev) + (1 << (16 + (dev << 2)));
                    ChangePciConfig4(0x48, (a & ~mask48) | new48);
                    SetPciConfig2(0x54, word54 | (0x1000<<dev));
        	    deviceExtension->lun[ldev].TransferMode = ATA_UDMA5;
                    return;
        	}
            }
        }
        /* make sure eventual ATA100 mode from the BIOS is disabled */
        ChangePciConfig2(0x54, a & ~(0x1000<<dev));

        /* FALLTHROUGH */

    case 0x24118086:    /* Intel ICH */
    case 0x76018086:	/* Intel ICH */
	if (udmamode >= 4) {
	    ULONG  mask48, new48;
	    USHORT word54;

            GetPciConfig2(0x54, word54);
	    if (word54 & (0x10 << dev)) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA4,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

        	if (!(statusByte & IDE_STATUS_ERROR)) {
		    mask48 = (1 << dev) + (3 << (16 + (dev << 2)));
		    new48 = (1 << dev) + (2 << (16 + (dev << 2)));
                    ChangePciConfig4(0x48, (a & ~mask48) | new48);
                    SetPciConfig2(0x54, word54 | (1<<dev));
        	    deviceExtension->lun[ldev].TransferMode = ATA_UDMA4;
		    return;
		}
	    }
	}           
	/* make sure eventual ATA66 mode from the BIOS is disabled */
        ChangePciConfig2(0x54, a & ~(1<<dev));

	/* FALLTHROUGH */

    case 0x71118086:	/* Intel PIIX4 */
    case 0x84CA8086:	/* Intel PIIX4 */
    case 0x71998086:	/* Intel PIIX4e */
    case 0x24218086:	/* Intel ICH0 */
	if (udmamode >= 2) {
	    ULONG mask48, new48;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            if (!(statusByte & IDE_STATUS_ERROR)) {
		mask48 = (1 << dev) + (3 << (16 + (dev << 2)));
		new48 = (1 << dev) + (2 << (16 + (dev << 2)));
                ChangePciConfig4(0x48, (a & ~mask48) | new48);
        	    deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		return;
	    }
	}
	/* make sure eventual ATA33 mode from the BIOS is disabled */
        ChangePciConfig2(0x48, a & ~(1<<dev));

	/* FALLTHROUGH */

    case 0x70108086:	/* Intel PIIX3 */
	if (wdmamode >= 2 && apiomode >= 4) {
	    ULONG mask40, new40, mask44, new44;

	    /* if SITRE not set doit for both channels */
            GetPciConfig4(0x40, new40);
	    if (!(( new40 >> (Channel<<8)) & 0x4000)) {
                //GetPciConfig4(0x40, new40);
                GetPciConfig4(0x44, new44);
		if (!(new40 & 0x00004000)) {
		    new44 &= ~0x0000000f;
		    new44 |= ((new40&0x00003000)>>10)|((new40&0x00000300)>>8);
		}
		if (!(new40 & 0x40000000)) {
		    new44 &= ~0x000000f0;
		    new44 |= ((new40&0x30000000)>>22)|((new40&0x03000000)>>20);
		}
		new40 |= 0x40004000;
                SetPciConfig4(0x40, new40);
                SetPciConfig4(0x44, new44);
	    }
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            if (!(statusByte & IDE_STATUS_ERROR)) {
		if (!(DeviceNumber & 1)) {
		    mask40 = 0x0000330f;
		    new40 = 0x00002307;
		    mask44 = 0;
		    new44 = 0;
		} else {
		    mask40 = 0x000000f0;
		    new40 = 0x00000070;
		    mask44 = 0x0000000f;
		    new44 = 0x0000000b;
		}
		if (Channel) {
		    mask40 <<= 16;
		    new40 <<= 16;
		    mask44 <<= 4;
		    new44 <<= 4;
		}
                ChangePciConfig4(0x40, (a & ~mask40) | new40);
                ChangePciConfig4(0x44, (a & ~mask44) | new44);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	goto try_generic_dma;

    case 0x12308086:	/* Intel PIIX */
	if (wdmamode >= 2 && apiomode >= 4) {
	    ULONG word40;

            GetPciConfig4(0x40, word40);
	    word40 >>= Channel * 16;

	    /* Check for timing config usable for DMA on controller */
	    if (!((word40 & 0x3300) == 0x2300 &&
		  ((word40 >> ((!(DeviceNumber & 1)) ? 0 : 4)) & 1) == 1))
		break;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	goto try_generic_dma;

    case 0x522910b9:	/* AcerLabs Aladdin IV/V */
	/* the older Aladdin doesn't support ATAPI DMA on both master & slave */
	if (deviceExtension->RevID < 0xC2 &&
	    ATAPI_DEVICE(deviceExtension, lChannel*2) &&
	    ATAPI_DEVICE(deviceExtension, lChannel*2 + 1)) {
	    // 2 devices on this channel - NO DMA
	    break;
	}
        SetPciConfig4(0x58 + (Channel<<2), 0x00310001);
	if (udmamode >= 5 && deviceExtension->RevID >= 0xC4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_UDMA5,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		ULONG word54;
	
                GetPciConfig4(0x54, word54);
                ChangePciConfig1(0x4b, a | 0x01);
		word54 &= ~(0x000f000f << (dev << 2));
		word54 |= (0x000f0005 << (dev << 2));
                SetPciConfig4(0x54, word54);
                ChangePciConfig1(0x53, a | 0x03);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA5;
		return;
	    }
	}
	if (udmamode >= 4 && deviceExtension->RevID >= 0xC2) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_UDMA4,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		ULONG word54;
	
                GetPciConfig4(0x54, word54);
                ChangePciConfig1(0x4b, a | 0x01);
		word54 &= ~(0x000f000f << (dev << 2));
		word54 |= (0x00080005 << (dev << 2));
                SetPciConfig4(0x54, word54);
                ChangePciConfig1(0x53, a | 0x03);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA4;
		return;
	    }
	}
	if (udmamode >= 2 && deviceExtension->RevID >= 0x20) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		ULONG word54;
	
                GetPciConfig4(0x54, word54);
		word54 &= ~(0x000f000f << (dev << 2));
		word54 |= (0x000a0005 << (dev << 2));
                SetPciConfig4(0x54, word54);
                ChangePciConfig1(0x53, a | 0x03);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		deviceExtension->chan[lChannel].ChannelCtrlFlags |= CTRFLAGS_DMA_RO;
		return;
	    }
	}

	/* make sure eventual UDMA mode from the BIOS is disabled */
        ChangePciConfig2(0x56, a & ~(0x0008 << (dev << 2)) );

	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                ChangePciConfig1(0x53, a | 0x03);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		deviceExtension->chan[lChannel].ChannelCtrlFlags |= CTRFLAGS_DMA_RO;
		return;
	    }
	}
        ChangePciConfig1(0x53, (a & ~0x01) | 0x02);

        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
			    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

        if (!(statusByte & IDE_STATUS_ERROR)) {
	    ULONG word54;
	    ULONG timing;

            GetPciConfig4(0x54, word54);

	    switch(ATA_PIO0 + apiomode) {
	    case ATA_PIO0: timing = 0x006d0003;
	    case ATA_PIO1: timing = 0x00580002;
	    case ATA_PIO2: timing = 0x00440001;
	    case ATA_PIO3: timing = 0x00330001;
	    case ATA_PIO4: timing = 0x00310001;
	    default:	   timing = 0x006d0003;
	    }

            SetPciConfig4(0x58 + (Channel<<2), timing);
	    word54 &= ~(0x000f000f << (dev << 2));
	    word54 |= (0x00000004 << (dev << 2));
            SetPciConfig4(0x54, word54);
	    deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
	    return;
	}
	break;

    case 0x01bc10de:	/* nVIDIA nForce */
    case 0x006510de:	/* nVIDIA nForce2 */
    case 0x74411022:	/* AMD 768 */
    case 0x74111022:	/* AMD 766 */
    case 0x74091022:	/* AMD 756 */
    case 0x05711106:	/* VIA 82C571, 82C586, 82C596, 82C686, 8231, 8233, 8235 */
        {
        UCHAR reg = 0x53-(UCHAR)dev;

	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x31471106, 0) ||     /* 8233a */
            AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x31771106, 0)) {     /* 8235 */
            udmamode = min(udmamode, 6);
            reg_val = (PUCHAR)&via_modes[3];
        } else
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x06861106, 0x40) ||  /* 82C686b */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x82311106, 0) ||     /* 8231 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x30741106, 0) ||     /* 8233 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x31091106, 0 )) {    /* 8233c */
            udmamode = min(udmamode, 5);
            reg_val = (PUCHAR)&via_modes[2];
	} else
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x06861106, 0x11) ||  /* 82C686a */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x05961106, 0x12 )) { /* 82C596b */
            udmamode = min(udmamode, 4);
            reg_val = (PUCHAR)&via_modes[1];
	} else
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x06861106, 0)) {     /* 82C686 */
            udmamode = min(udmamode, 2);
            reg_val = (PUCHAR)&via_modes[1];
	} else
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x05961106, 0) ||     /* 82C596a */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x05861106, 0) ||     /* 82C586 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber, 
                         slotNumber, 0x05861106, 0x03)) {  /* 82C586b+ */

            udmamode = min(udmamode, 2);
            reg_val = (PUCHAR)&via_modes[0];
	} else
	if (deviceExtension->DevID == 0x74411022 ||        /* AMD 768 */
	    deviceExtension->DevID == 0x74111022) {        /* AMD 766 */
            udmamode = min(udmamode, 5);
            reg_val = (PUCHAR)&via_modes[4];
	} else
	if (deviceExtension->DevID == 0x74091022) {        /* AMD 756 */
            udmamode = min(udmamode, 4);
            reg_val = (PUCHAR)&via_modes[4];
	} else
	if (deviceExtension->DevID == 0x01bc10de) {        /* nVIDIA */
            udmamode = min(udmamode, 5);
            reg_val = (PUCHAR)&via_modes[4];
            reg += 0x10;
	} else
	if (deviceExtension->DevID == 0x006510de) {        /* nVIDIA 2 */
            udmamode = min(udmamode, 6);
            reg_val = (PUCHAR)&via_modes[4];
            reg += 0x10;
        } else
            udmamode = 0;

        if(udmamode > 0 || wdmamode > 0) {
            SetPciConfig1(reg-0x08, 0x20);
        }

        for(i = udmamode; i>=0; i--) {
            if(!valid_udma[i])
                continue;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_UDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig1(reg, (UCHAR)reg_val[i]);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA0+(UCHAR)i;
                return;
            }
        }
	
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig1(reg, 0xb0);
                SetPciConfig1(reg-0x08, 0x31);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}

        /* try generic DMA */
	if (!ATAPI_DEVICE(deviceExtension, ldev) &&
	    wdmamode >= 0 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_DMA,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_DMA;
		return;
	    }
	}
	/* set PIO mode timings */
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        		    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
        via82c_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
	return;
    }
    case 0x55131039:	/* SiS 5591 */
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06301039, 0x30) ||     /* SiS 630 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06331039, 0) ||        /* SiS 633 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06351039, 0) ||        /* SiS 635 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06401039, 0) ||        /* SiS 640 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06451039, 0) ||        /* SiS 645 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06501039, 0) ||        /* SiS 650 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07301039, 0) ||        /* SiS 730 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07331039, 0) ||        /* SiS 733 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07351039, 0) ||        /* SiS 735 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07401039, 0) ||        /* SiS 740 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07451039, 0) ||        /* SiS 745 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x07501039, 0 )) {       /* SiS 750 */

	    UCHAR reg = 0x40 + (UCHAR)(dev << 1);
	    USHORT val;
	    
	    GetPciConfig2(reg, val);
	    val &= 0x0fff;

	    if (udmamode >= 5) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA5,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig2(reg, val | 0x8000);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA5;
		    return;
		}
	    }
	    if (udmamode >= 4) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA4,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig2(reg, val | 0x9000);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA4;
		    return;
		}
	    }
	    if (udmamode >= 2) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig2(reg, val | 0xb000);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		    return;
		}
	    }
	} else
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x05301039, 0) ||        /* SiS 530 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x05401039, 0) ||        /* SiS 540 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06201039, 0) ||        /* SiS 620 */
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration, SystemIoBusNumber,
                         slotNumber, 0x06301039, 0 )) {       /* SiS 630 */

	    UCHAR reg = 0x40 + (UCHAR)(dev << 1);
	    USHORT val;
	    
	    GetPciConfig2(reg, val);
	    val &= 0x0fff;

	    if (udmamode >= 4) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA4,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig2(reg, val | 0x9000);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA4;
		    return;
		}
	    }
	    if (udmamode >= 2) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        			    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig2(reg, val | 0xa000);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		    return;
		}
	    }
	} else if (udmamode >= 2 && deviceExtension->RevID > 0xc1) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig2(0x40 + (dev << 1), 0xa301);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
                return;
	    }
	}
	if (wdmamode >=2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig2(0x40 + (dev << 1), 0x0301);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	goto try_generic_dma;

    case 0x06801095:	/* Sil 0680 ATA133 controller */
	{
        static const UCHAR sil_modes[7] = 
            { 0x00, 0x00, 0x07, 0x00, 0x03, 0x02, 0x01 };

        UCHAR ureg = 0xac + ((UCHAR)DeviceNumber * 0x02) + ((UCHAR)Channel * 0x10);
        UCHAR uval;
        UCHAR mreg = Channel ? 0x84 : 0x80;
        UCHAR mask = DeviceNumber ? 0x30 : 0x03;
        UCHAR mode;

        GetPciConfig1(ureg, uval);
        GetPciConfig1(mreg, mode);

        udmamode = min(udmamode, 6);

        /* enable UDMA mode */
        for(i = udmamode; i>=0; i--) {
            if(!valid_udma[i])
                continue;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_UDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig1(ureg, (uval & 0x3f) | sil_modes[i]);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA0+(UCHAR)i;
                return;
            }
        }

        /* disable UDMA mode and enable WDMA mode */
        SetPciConfig1(mreg, (mode & ~mask) | (DeviceNumber ? 0x20 : 0x02));
        if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig2(ureg - 0x4, 0x10c1);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                return;
            }
        }

        /* restore PIO mode */
        SetPciConfig1(mreg, mode);
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	break;

    case 0x06491095:	/* CMD 649 ATA100 controller */
	if (udmamode >= 5) {
	    UCHAR umode;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_UDMA5,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig1(Channel ? 0x7b : 0x73, umode);
		umode &= ~(!(DeviceNumber & 1) ? 0x35 : 0xca);
		umode |= (!(DeviceNumber & 1) ? 0x05 : 0x0a);
                SetPciConfig1(Channel ? 0x7b : 0x73, umode);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA5;
		return;
	    }
	}
	/* FALLTHROUGH */

    case 0x06481095:	/* CMD 648 ATA66 controller */
	if (udmamode >= 4) {
	    UCHAR umode;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_UDMA4,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig1(Channel ? 0x7b : 0x73, umode);
		umode &= ~(!(DeviceNumber & 1) ? 0x35 : 0xca);
		umode |= (!(DeviceNumber & 1) ? 0x15 : 0x4a);
                SetPciConfig1(Channel ? 0x7b : 0x73, umode);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA4;
		return;
	    }
	}
	if (udmamode >= 2) {
	    UCHAR umode;

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig1(Channel ? 0x7b : 0x73, umode);
		umode &= ~(!(DeviceNumber & 1) ? 0x35 : 0xca);
		umode |= (!(DeviceNumber & 1) ? 0x11 : 0x42);
                SetPciConfig1(Channel ? 0x7b : 0x73, umode);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		return;
	    }
	}
	/* make sure eventual UDMA mode from the BIOS is disabled */
        ChangePciConfig1(Channel ? 0x7b : 0x73, a & ~(!(DeviceNumber & 1) ? 0x35 : 0xca));

	/* FALLTHROUGH */

    case 0x06461095:	/* CMD 646 ATA controller */
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		ULONG offset = (dev < 3) ? (dev << 1) : 7;

                SetPciConfig1(0x54+offset, 0x3f);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	goto try_generic_dma;

    case 0xc6931080:	/* Cypress 82c693 ATA controller */
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig2(Channel ? 0x4e:0x4c, 0x2020);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	goto try_generic_dma;

    case 0x01021078:	/* Cyrix 5530 ATA33 controller */
	
	// ConfigInfo->AlignmentMask = 0x0f;	/* DMA engine requires 16 byte alignment */
	// we have done this durring detection/init 
	if(!deviceExtension->BaseIoAddressBM[lChannel])
	    break;

	if (udmamode >= 2) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		cyrix_timing(deviceExtension, dev, ATA_UDMA2);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		return;
	    }
	}
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		cyrix_timing(deviceExtension, dev, ATA_WDMA2);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
        /* try generic DMA */
	if (!ATAPI_DEVICE(deviceExtension, ldev) &&
	    wdmamode >= 0 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_DMA,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_DMA;
		return;
	    }
	}
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        		    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

        cyrix_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
	return;

    case 0x02111166:	/* ServerWorks ROSB4 ATA33 controller */

        udmamode = min(udmamode, 2);
	/* FALLTHROUGH */

    case 0x02121166:	/* ServerWorks CSB5 ATA66/100 controller */
	
        udmamode = min(udmamode, 5);
	if(deviceExtension->RevID < 0x92) {
	    udmamode = min(udmamode, 4);
	}
	
	if (ATAPI_DEVICE(deviceExtension, ldev)) {
	    // ATAPI doesn't want to work in UDMA
	    // (at least with such settings)
	    udmamode = -1;
	    wdmamode = -1;
	}

        for(i=udmamode; i>0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		USHORT reg56;

                ChangePciConfig1(0x54, a | (0x01 << dev));
                GetPciConfig2(0x56, reg56);
		reg56 &= ~(0xf << (dev * 4));
		reg56 |= ((UCHAR)i << (dev * 4));
                SetPciConfig2(0x56, reg56);
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
		return;
	    }
        }

	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		//int offset = (Channel * 2) + (DeviceNumber == 0);
		ULONG offset = dev ^ 0x1;
		ULONG word44;

                GetPciConfig4(0x44, word44);
                ChangePciConfig1(0x54, a & ~(0x01 << dev));
		word44 &= ~(0xff << (offset * 8));
		word44 |= (0x20 << (offset * 8));
                SetPciConfig4(0x44, word44);
//                SetPciConfig4(0x44, 0x20);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	goto try_generic_dma;

    case 0x4d68105a:	/* Promise TX2 ATA100 controllers */
    case 0x6268105a:	/* Promise TX2 ATA100 controllers */

	udmamode = min(udmamode, 5);
	/* FALLTHROUGH */

    case 0x4d69105a:	/* Promise TX2 ATA133 controllers */
    case 0x5275105a:	/* Promise TX2 ATA133 controllers */
    case 0x6269105a:	/* Promise TX2 ATA133 controllers */
    case 0x7275105a:	/* Promise TX2 ATA133 controllers */

        if(!deviceExtension->BaseIoAddressBM[lChannel])
            break;

        udmamode = min(udmamode, 6);

        for(i=udmamode; i>0; i--) {
            if(!valid_udma[i])
                continue;
            if(udmamode > 2)
                ScsiPortWritePortUchar(&deviceExtension->BaseIoAddressBM[lChannel]->DeviceSpecific0, 0x0b);
            if ((udmamode <= 2) ||
                !(ScsiPortReadPortUchar(&deviceExtension->BaseIoAddressBM[lChannel]->DeviceSpecific1) & 0x04)){
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                		    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                   return;
                }
            }
	}
	goto try_generic_dma;

    case 0x4d33105a:	/* Promise Ultra/FastTrak 33 controllers */

        udmamode = min(udmamode, 2);
	/* FALLTHROUGH */

    case 0x0d38105a:	/* Promise FastTrak 66 controllers */
    case 0x4d38105a:	/* Promise Ultra/FastTrak 66 controllers */

        udmamode = min(udmamode, 4);
	/* FALLTHROUGH */

    case 0x4d30105a:	/* Promise Ultra/FastTrak 100 controllers */
    case 0x0d30105a:	/* Promise OEM ATA100 controllers */

        if (ATAPI_DEVICE(deviceExtension, ldev)) {
            udmamode =
            wdmamode = -1;
        } else {
            udmamode = min(udmamode, 5);
        }
        
        for(i=udmamode; i>=2; i--) {

            if(!valid_udma[i])
                continue;
            if(i>2)
                GetPciConfig4(0x50, reg50);
            if ((i <= 2) || 
                !(reg50 & (Channel ? 1<<11 : 1<<10))){
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                		    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    promise_timing(deviceExtension, dev, (UCHAR)(ATA_UDMA + i));
                    deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                    return;
                }
            }
        }

	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		promise_timing(deviceExtension, dev, ATA_WDMA2);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
        /* try generic DMA */
	if (wdmamode >= 0 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_DMA,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_DMA;
		return;
	    }
	}
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        		    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
	promise_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
	return;

    case 0x00041103:	/* HighPoint HPT366/368/370/372 controllers */
    case 0x00051103:	/* HighPoint HPT372 controllers */
    case 0x00081103:	/* HighPoint HPT374 controllers */
	
	if((deviceExtension->DevID == 0x00041103 && deviceExtension->RevID < 0x03)) {
            udmamode = min(udmamode, 4);
        } else
	if((deviceExtension->DevID == 0x00041103 && deviceExtension->RevID < 0x05) ||
           (deviceExtension->DevID == 0x00051103 && deviceExtension->RevID < 0x01) ||
           (deviceExtension->DevID == 0x00081103 && deviceExtension->RevID < 0x07)) {
            udmamode = min(udmamode, 5);
        } else {
            udmamode = min(udmamode, 6);
        }

        if(!hpt_cable80(deviceExtension, Channel)) {
	    udmamode = min(udmamode, 2);
        }

        if(ATAPI_DEVICE(deviceExtension, ldev)) {
            udmamode = 
            wdmamode = -1;
        }

        for(i=udmamode; i>0; i--) {
            if(!valid_udma[i])
                continue;
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                hpt_timing(deviceExtension, dev, (UCHAR)(ATA_UDMA + i));       // ???
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }
	
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
		hpt_timing(deviceExtension, dev, ATA_WDMA2);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
        /* try generic DMA, use hpt_timing() */
	if (wdmamode >= 0 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    			    ATA_DMA,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_DMA;
		return;
	    }
	}
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
        		    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
	hpt_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
	return;

    case 0x00071191:	/* Acard ATP860R controller */
    case 0x00061191:	/* Acard ATP860 controller */
        udmamode = min(udmamode, 4);
	/* FALLTHROUGH */

    case 0x00091191:	/* Acard ATP865R controller */
    case 0x00081191:	/* Acard ATP865 controller */
	{
        udmamode = min(udmamode, 6);
        
        static const UCHAR acard_modes[7] = 
            { 0x00, 0x00, 0x03, 0x00, 0x05, 0x06, 0x07 };
        USHORT reg44;
        UCHAR  reg = 0x40 + (UCHAR)dev;
        static const UCHAR  reg4x = 0x31;
        static const UCHAR  reg4a = 0xa6;

	if (ATAPI_DEVICE(deviceExtension, ldev))
	    break;

        for(i=udmamode; i>0; i--) {
            if(!acard_modes[i])
                continue;

            //ChangePciConfig1(0x54, a | (0x01 << dev));
            GetPciConfig2(0x44, reg44);
            reg44 &= ~(0x000f << (dev << 2));
            reg44 |= ((USHORT)(acard_modes[i]) << (dev * 4));
            SetPciConfig2(0x44, reg44);
            SetPciConfig1(0x4a, reg4a);
            SetPciConfig1(reg,  reg4x);

            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }

	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig2(0x44, reg44);
		reg44 &= ~(0x000f << (dev << 2));
                SetPciConfig2(0x44, reg44);
                SetPciConfig1(0x4a, reg4a);
                SetPciConfig1(reg,  reg4x);

                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	}
	break;
    case 0x00051191:	/* Acard ATP850UF controller */
	{
	UCHAR reg54;
        UCHAR  reg = 0x40 + (UCHAR)dev*2;
        static const UCHAR  reg4a = 0xa6;
        static const USHORT reg4x = 0x0301;
		
	if (ATAPI_DEVICE(deviceExtension, ldev))
	    break;

	if (udmamode >= 2) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		        ATA_UDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig1(0x54, reg54);
		reg54 |= (0x03 << (dev << 1));
                SetPciConfig1(0x54, reg54);
                SetPciConfig1(0x4a, reg4a);
                SetPciConfig2(reg,  reg4x);

                deviceExtension->lun[ldev].TransferMode = ATA_UDMA2;
		return;
	    }
	}
	if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            if (!(statusByte & IDE_STATUS_ERROR)) {
                GetPciConfig1(0x54, reg54);
		reg54 &= ~(0x03 << (dev << 1));
                SetPciConfig1(0x54, reg54);
                SetPciConfig1(0x4a, reg4a);
                SetPciConfig2(reg,  reg4x);

                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
		return;
	    }
	}
	/* we could set PIO mode timings, but we assume the BIOS did that */
	}
	break;

    case 0x000116ca:	/* Cenatek Rocket Drive controller */

	if (wdmamode >= 0 &&
	    (ScsiPortReadPortUchar(&deviceExtension->BaseIoAddressBM[lChannel]->Status) & 
	     (DeviceNumber ? BM_STATUS_DRIVE_1_DMA : BM_STATUS_DRIVE_0_DMA)))
            deviceExtension->lun[ldev].TransferMode = ATA_DMA;
	else
            deviceExtension->lun[ldev].TransferMode = ATA_PIO;
	return;

try_generic_dma:

    default:		/* unknown controller chip */
	/* better not try generic DMA on ATAPI devices it almost never works */
	if (ATAPI_DEVICE(deviceExtension, ldev))
	    break;

	/* if controller says its setup for DMA take the easy way out */
	/* the downside is we dont know what DMA mode we are in */
	if ((udmamode >= 0 || /*wdmamode > 1*/ wdmamode >= 0) &&
	     deviceExtension->BaseIoAddressBM[lChannel] &&
	    (ScsiPortReadPortUchar(&deviceExtension->BaseIoAddressBM[lChannel]->Status) &
	     (!(ldev & 1) ? 
	      BM_STATUS_DRIVE_0_DMA : BM_STATUS_DRIVE_1_DMA))) {
	    deviceExtension->lun[ldev].TransferMode = ATA_DMA;
	    return;
	}

	/* well, we have no support for this, but try anyways */
	if ((wdmamode >= 0 && apiomode >= 4) && deviceExtension->BaseIoAddressBM[lChannel]) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
            		    ATA_WDMA + wdmamode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA + wdmamode;
		return;
	    }
	}
    }
    statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                        IDE_COMMAND_SET_FEATURES, 0, 0, 0,
    		    ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
    if (!(statusByte & IDE_STATUS_ERROR)) {
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
    } else {
        deviceExtension->lun[ldev].TransferMode = ATA_PIO;
    }
    return;
}

VOID
cyrix_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    ULONG reg20 = 0x0000e132;
    ULONG reg24 = 0x00017771;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    switch (mode) {
    case ATA_PIO0:	reg20 = 0x0000e132; break;
    case ATA_PIO1:	reg20 = 0x00018121; break;
    case ATA_PIO2:	reg20 = 0x00024020; break;
    case ATA_PIO3:	reg20 = 0x00032010; break;
    case ATA_PIO4:
    case ATA_PIO5:	reg20 = 0x00040010; break;
    case ATA_WDMA2:	reg24 = 0x00002020; break;
    case ATA_UDMA2:	reg24 = 0x00911030; break;
    }
    ScsiPortWritePortUlong((PULONG)((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + (dev*8)) + 0x20, reg20);
    ScsiPortWritePortUlong((PULONG)((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + (dev*8)) + 0x24, reg24);
}
 
VOID
promise_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    ULONG timing = 0;

    struct promise_timing {
	UCHAR  pa:4;
	UCHAR  prefetch:1;
	UCHAR  iordy:1;
	UCHAR  errdy:1;
	UCHAR  syncin:1;
	UCHAR  pb:5;
	UCHAR  mb:3;
	UCHAR  mc:4;
	UCHAR  dmaw:1;
	UCHAR  dmar:1;
	UCHAR  iordyp:1;
	UCHAR  dmarqp:1;
	UCHAR  reserved:8;
    } *t = (struct promise_timing*)&timing;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    t->iordy = 1;
    t->iordyp = 1;
    if (mode >= ATA_DMA) {
	t->prefetch = 1;
	t->errdy = 1;
	t->syncin = 1;
    }

    switch(deviceExtension->DevID) {
    case 0x4d33105a:  /* Promise Ultra/Fasttrak 33 */
	switch (mode) {
	default:
	case ATA_PIO0:  t->pa =  9; t->pb = 19; t->mb = 7; t->mc = 15; break;
	case ATA_PIO1:  t->pa =  5; t->pb = 12; t->mb = 7; t->mc = 15; break;
	case ATA_PIO2:  t->pa =  3; t->pb =  8; t->mb = 7; t->mc = 15; break;
	case ATA_PIO3:  t->pa =  2; t->pb =  6; t->mb = 7; t->mc = 15; break;
	case ATA_PIO4:
	case ATA_PIO5:  t->pa =  1; t->pb =  4; t->mb = 7; t->mc = 15; break;
	case ATA_WDMA2: t->pa =  3; t->pb =  7; t->mb = 3; t->mc =  3; break;
	case ATA_UDMA2: t->pa =  3; t->pb =  7; t->mb = 1; t->mc =  1; break;
	}
	break;

    case 0x0d38105a:  /* Promise Fasttrak 66 */
    case 0x4d38105a:  /* Promise Ultra/Fasttrak 66 */
    case 0x0d30105a:  /* Promise OEM ATA 100 */
    case 0x4d30105a:  /* Promise Ultra/Fasttrak 100 */
	switch (mode) {
	default:
	case ATA_PIO0:  t->pa = 15; t->pb = 31; t->mb = 7; t->mc = 15; break;
	case ATA_PIO1:  t->pa = 10; t->pb = 24; t->mb = 7; t->mc = 15; break;
	case ATA_PIO2:  t->pa =  6; t->pb = 16; t->mb = 7; t->mc = 15; break;
	case ATA_PIO3:  t->pa =  4; t->pb = 12; t->mb = 7; t->mc = 15; break;
	case ATA_PIO4:
	case ATA_PIO5:  t->pa =  2; t->pb =  8; t->mb = 7; t->mc = 15; break;
	case ATA_WDMA2: t->pa =  6; t->pb = 14; t->mb = 6; t->mc =  6; break;
	case ATA_UDMA2: t->pa =  6; t->pb = 14; t->mb = 2; t->mc =  2; break;
	case ATA_UDMA4: t->pa =  3; t->pb =  7; t->mb = 1; t->mc =  1; break;
	case ATA_UDMA5: t->pa =  3; t->pb =  7; t->mb = 1; t->mc =  1; break;
	}
	break;
    }
    SetPciConfig4(0x60 + (dev<<2), timing);
}


VOID
hpt_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    ULONG timing;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    if (deviceExtension->RevID == 0x00081103 &&
            deviceExtension->RevID >= 0x07) {
	switch (mode) {						/* HPT374 */
	case ATA_PIO0:	timing = 0x0ac1f48a; break;
	case ATA_PIO1:	timing = 0x0ac1f465; break;
	case ATA_PIO2:	timing = 0x0a81f454; break;
	case ATA_PIO3:	timing = 0x0a81f443; break;
	case ATA_PIO4:	timing = 0x0a81f442; break;
	case ATA_WDMA2: timing = 0x22808242; break;
	case ATA_UDMA2: timing = 0x120c8242; break;
	case ATA_UDMA4: timing = 0x12ac8242; break;
	case ATA_UDMA5: timing = 0x12848242; break;
	case ATA_UDMA6: timing = 0x12808242; break;
	default:	timing = 0x0d029d5e;
	}
    }
    else if ((deviceExtension->RevID == 0x00041103 &&
                  deviceExtension->RevID >= 0x05) ||
	     (deviceExtension->RevID == 0x00051103 &&
	          deviceExtension->RevID >= 0x01)) {
	switch (mode) {						/* HPT372 */
	case ATA_PIO0:	timing = 0x0d029d5e; break;
	case ATA_PIO1:	timing = 0x0d029d26; break;
	case ATA_PIO2:	timing = 0x0c829ca6; break;
	case ATA_PIO3:	timing = 0x0c829c84; break;
	case ATA_PIO4:	timing = 0x0c829c62; break;
	case ATA_WDMA2: timing = 0x2c829262; break;
	case ATA_UDMA2: timing = 0x1c91dc62; break;
	case ATA_UDMA4: timing = 0x1c8ddc62; break;
	case ATA_UDMA5: timing = 0x1c6ddc62; break;
	case ATA_UDMA6: timing = 0x1c81dc62; break;
	default:	timing = 0x0d029d5e;
	}
    }
    else if (deviceExtension->RevID == 0x00041103 &&
                 deviceExtension->RevID >= 0x03) {
	switch (mode) {						/* HPT370 */
	case ATA_PIO0:	timing = 0x06914e57; break;
	case ATA_PIO1:	timing = 0x06914e43; break;
	case ATA_PIO2:	timing = 0x06514e33; break;
	case ATA_PIO3:	timing = 0x06514e22; break;
	case ATA_PIO4:	timing = 0x06514e21; break;
	case ATA_WDMA2: timing = 0x26514e21; break;
	case ATA_UDMA2: timing = 0x16494e31; break;
	case ATA_UDMA4: timing = 0x16454e31; break;
	case ATA_UDMA5: timing = 0x16454e31; break;
	default:	timing = 0x06514e57;
	}
    }
    else {							/* HPT36[68] */
	UCHAR reg41;

        GetPciConfig1(0x41 + (dev << 2), reg41);

	switch (reg41) {
	case 0x85:	/* 25Mhz */
	    switch (mode) {
	    case ATA_PIO0:	timing = 0x40d08585; break;
	    case ATA_PIO1:	timing = 0x40d08572; break;
	    case ATA_PIO2:	timing = 0x40ca8542; break;
	    case ATA_PIO3:	timing = 0x40ca8532; break;
	    case ATA_PIO4:	timing = 0x40ca8521; break;
	    case ATA_WDMA2:	timing = 0x20ca8521; break;
	    case ATA_UDMA2:	timing = 0x10cf8521; break;
	    case ATA_UDMA4:	timing = 0x10c98521; break;
	    default:		timing = 0x01208585;
	    }
	    break;
	default:
	case 0xa7:	/* 33MHz */
	    switch (mode) {
	    case ATA_PIO0:	timing = 0x40d0a7aa; break;
	    case ATA_PIO1:	timing = 0x40d0a7a3; break;
	    case ATA_PIO2:	timing = 0x40d0a753; break;
	    case ATA_PIO3:	timing = 0x40c8a742; break;
	    case ATA_PIO4:	timing = 0x40c8a731; break;
	    case ATA_WDMA2:	timing = 0x20c8a731; break;
	    case ATA_UDMA2:	timing = 0x10caa731; break;
	    case ATA_UDMA4:	timing = 0x10c9a731; break;
	    default:		timing = 0x0120a7a7;
	    }
	    break;
	case 0xd9:	/* 40Mhz */
	    switch (mode) {
	    case ATA_PIO0:	timing = 0x4018d9d9; break;
	    case ATA_PIO1:	timing = 0x4010d9c7; break;
	    case ATA_PIO2:	timing = 0x4010d997; break;
	    case ATA_PIO3:	timing = 0x4010d974; break;
	    case ATA_PIO4:	timing = 0x4008d963; break;
	    case ATA_WDMA2:	timing = 0x2008d943; break;
	    case ATA_UDMA2:	timing = 0x100bd943; break;
	    case ATA_UDMA4:	timing = 0x100fd943; break;
	    default:		timing = 0x0120d9d9;
	    }
	}
    }

    SetPciConfig4(0x40 + (dev<<2), timing);
}


#define FIT(v,min,max) (((v)>(max)?(max):(v))<(min)?(min):(v))

VOID
via82c_timing (
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    USHORT T = 1000 / /* PciBusClockMHz()*/ 33;

    USHORT setup   = 0;
    USHORT active  = 0;
    USHORT recover = 0;
    USHORT cycle   = 0;

    UCHAR t;

    switch(mode) {
    default:
    case ATA_PIO0:  setup = 70; active = 165; recover = 150; cycle = 600; break;
    case ATA_PIO1:  setup = 50; active = 125; recover = 100; cycle = 383; break;
    case ATA_PIO2:  setup = 30; active = 100; recover = 90;  cycle = 240; break;
    case ATA_PIO3:  setup = 30; active = 80;  recover = 70;  cycle = 180; break;
    case ATA_PIO4:  setup = 25; active = 70;  recover = 25;  cycle = 120; break;
    case ATA_PIO5:  setup = 20; active = 50;  recover = 30;  cycle = 100; break;
    }

    setup   = (setup  -1)/(T+1);
    active  = (active -1)/(T+1);
    recover = (recover-1)/(T+1);
    cycle   = (cycle  -1)/(T+1);

    if (active + recover < cycle) {
        active += (cycle - (active + recover)) / 2;
        recover = cycle - active;
    }

    /* PIO address setup */
    GetPciConfig1(0x4c, t);
    t = (t & ~(3 << ((3 - dev) << 1))) | (FIT(setup - 1, 0, 3) << ((3 - dev) << 1));
    SetPciConfig1(0x4c, t);

    /* PIO active & recover */
    SetPciConfig1(0x4b-dev, (FIT(active - 1, 0, 0xf) << 4) | FIT(recover - 1, 0, 0xf) );
}

ULONG
hpt_cable80(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG channel               // physical channel number (0-1)
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    UCHAR reg, val, res;
    PCI_SLOT_NUMBER slotData;

    slotData.u.AsULONG = deviceExtension->slotNumber;

    if(deviceExtension->DevID == 0x00081103 && slotData.u.bits.FunctionNumber == 1)  {
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
}
