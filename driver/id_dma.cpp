/*++

Copyright (c) 2002-2004 Alexander A. Telyatnikov (Alter)

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

    This module is a port from FreeBSD 4.3-5.1 ATA driver (ata-dma.c) by
         Søren Schmidt, Copyright (c) 1998-2004

    Changed defaulting-to-generic-PIO/DMA policy
    Added PIO settings for VIA
    Optimized VIA/AMD/nVidia init part
    Optimized Promise TX2 init part
    Optimized Intel init part
         by Alex A. Telyatnikov (Alter) (c) 2002-2004


--*/

extern "C" {

#include <ntddk.h>

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
#ifdef USE_OWN_DMA
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG c = lChannel;

    deviceExtension->chan[c].CopyDmaBuffer = FALSE;
    deviceExtension->chan[c].DmaBuffer =
        (PUCHAR)ExAllocatePool(NonPagedPool, ConfigInfo->MaximumTransferLength);
#endif //USE_OWN_DMA

    return;
} // end AtapiDmaAlloc()

//#ifdef USE_OWN_DMA
ULONG
AtapiVirtToPhysAddr_(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN PUCHAR data,
    IN PULONG count
    )
{
    PHYSICAL_ADDRESS ph_addr;
    ULONG addr;

    ph_addr = MmGetPhysicalAddress(data);
    addr = ScsiPortConvertPhysicalAddressToUlong(ph_addr);
    if(!addr) {
        *count = 0;
        return 0;
    }
    if(!Srb) {
        *count = sizeof(BM_DMA_ENTRY)*ATA_DMA_ENTRIES;
    } else {
        *count = PAGE_SIZE - (addr & (PAGE_SIZE-1));
    }
    return addr;
}
//#endif

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

    KdPrint2((PRINT_PREFIX "AtapiDmaSetup: mode %#x\n", deviceExtension->lun[lChannel*2+DeviceNumber].TransferMode ));
    if(!count) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: count=0\n" ));
        return FALSE;
    }

    dma_base = AtapiVirtToPhysAddr(HwDeviceExtension, NULL, (PUCHAR)&(AtaReq->dma_tab) /*chan->dma_tab*/, &i);

    if(!dma_base || !i || ((LONG)(dma_base) == -1)) {
        KdPrint2((PRINT_PREFIX "AtapiDmaSetup: No BASE\n" ));
        return FALSE;
    }
    AtaReq->dma_base = dma_base;

    dma_base = AtapiVirtToPhysAddr(HwDeviceExtension, Srb, data, &dma_count);
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
        dma_base = AtapiVirtToPhysAddr(HwDeviceExtension, Srb, data, &dma_count);
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

BOOLEAN
AtapiDmaPioSync(
    PVOID  HwDeviceExtension,
    PSCSI_REQUEST_BLOCK Srb,
    PUCHAR data,
    ULONG  count
    )
{
#ifndef USE_OWN_DMA
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG dma_base;
    PUCHAR DmaBuffer;
    ULONG dma_count;
    ULONG len;
    PATA_REQ AtaReq;

    // This must never be called after DMA operation !!!
    KdPrint2((PRINT_PREFIX "AtapiDmaPioSync: data %#x, len %#x\n", data, count));
    
    if(!Srb) {
        KdPrint2((PRINT_PREFIX "AtapiDmaPioSync: !Srb\n" ));
        return FALSE;
    }

    AtaReq = (PATA_REQ)(Srb->SrbExtension);

    // do nothing on PCI (This can be changed. We cannot guarantee,
    // that CommonBuffer will always point to User's buffer,
    // however, this usually happens on PCI-32)
    if(deviceExtension->OrigAdapterInterfaceType == PCIBus) {
        return TRUE;
    }
    // do nothing for DMA
    if(AtaReq->Flags & REQ_FLAG_DMA_OPERATION) {
        return TRUE;
    }

    if(!data) {
        KdPrint2((PRINT_PREFIX "AtapiDmaPioSync: !data\n" ));
        return FALSE;
    }

    while(count) {
        dma_base = AtapiVirtToPhysAddr(HwDeviceExtension, Srb, data, &dma_count);
        if(!dma_base) {
            KdPrint2((PRINT_PREFIX "AtapiDmaPioSync: !dma_base for data %#x\n", data));
            return FALSE;
        }
        DmaBuffer = (PUCHAR)ScsiPortGetVirtualAddress(HwDeviceExtension,
                                                      ScsiPortConvertUlongToPhysicalAddress(dma_base));
        if(!DmaBuffer) {
            KdPrint2((PRINT_PREFIX "AtapiDmaPioSync: !DmaBuffer for dma_base %#x\n", dma_base));
            return FALSE;
        }
        len = min(dma_count, count);
        memcpy(DmaBuffer, data, len);
        count -= len;
        data += len;
    }
#endif //USE_OWN_DMA

    return TRUE;
} // end AtapiDmaPioSync()

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
    PHW_CHANNEL chan = &deviceExtension->chan[lChannel];

    ULONG VendorID =  deviceExtension->DevID        & 0xffff;
    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;

    KdPrint2((PRINT_PREFIX "AtapiDmaStart: %s on %#x:%#x\n", 
        (Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? "read" : "write",
        lChannel, DeviceNumber ));

    // set flag
    chan->ChannelCtrlFlags |= CTRFLAGS_DMA_ACTIVE;

    switch(VendorID) {
    case ATA_PROMISE_ID:
        if(ChipType == PRNEW) {
            if(chan->ChannelCtrlFlags & CTRFLAGS_LBA48) {
                AtapiWritePort1(chan, (PUCHAR)(BaseIoAddressBM)+0x11, 
                      AtapiReadPort1(chan, (PUCHAR)(BaseIoAddressBM)+0x11) |
                          (DeviceNumber ? 0x08 : 0x02));
/*                AtapiWritePort4(chan, (PULONG)((PUCHAR)(BaseIoAddressBM) + (DeviceNumber ? 0x24 : 0x20)), 
                      ((Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? 0x05000000 : 0x06000000) | (Srb->DataTransferLength >> 1)
                      );*/
                AtapiWritePort4(chan, (PULONG)((PUCHAR)(BaseIoAddressBM) + 0x20), 
                      ((Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? 0x05000000 : 0x06000000) | (Srb->DataTransferLength >> 1)
                      );
            }
        } else
        if(deviceExtension->MemIo) {
            // begin transaction
            AtapiWritePort4(chan,
                 (PULONG)&(BaseIoAddressBM->Command), 
                 (AtapiReadPort4(chan,
                     (PULONG)&(BaseIoAddressBM->Command)) & ~0x000000c0) |
                     ((Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? 0x00000080 : 0x000000c0) );
            return;
        }
        break;
    }

    // set pointer to Pointer Table
    AtapiWritePort4(chan, &(BaseIoAddressBM->PRD_Table), 
          AtaReq->dma_base
          );
    // set transfer direction
    AtapiWritePort1(chan, &(BaseIoAddressBM->Command), 
          (Srb->SrbFlags & SRB_FLAGS_DATA_IN) ? BM_COMMAND_READ : BM_COMMAND_WRITE);
    // clear Error & Intr bits (writeing 1 clears bits)
    // set DMA capability bit
    AtapiWritePort1(chan, &(BaseIoAddressBM->Status), 
         AtapiReadPort1(chan, &(BaseIoAddressBM->Status)) |
             (BM_STATUS_INTR | BM_STATUS_ERR) /*|
             (DeviceNumber ? BM_STATUS_DRIVE_1_DMA : BM_STATUS_DRIVE_0_DMA)*/);
    // begin transaction
    AtapiWritePort1(chan, &(BaseIoAddressBM->Command), 
         AtapiReadPort1(chan, &(BaseIoAddressBM->Command)) |
             BM_COMMAND_START_STOP);
    return;

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
    PHW_CHANNEL chan = &deviceExtension->chan[lChannel];
    UCHAR dma_status;

    ULONG VendorID =  deviceExtension->DevID        & 0xffff;
    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;

    KdPrint2((PRINT_PREFIX "AtapiDmaDone:\n" ));

    switch(VendorID) {
    case ATA_PROMISE_ID:
        if(ChipType == PRNEW) {
            if(chan->ChannelCtrlFlags & CTRFLAGS_LBA48) {
                AtapiWritePort1(chan, (PUCHAR)(BaseIoAddressBM)+0x11, 
                      AtapiReadPort1(chan, (PUCHAR)(BaseIoAddressBM)+0x11) &
                          ~(DeviceNumber ? 0x08 : 0x02));
/*                AtapiWritePort4(chan, (PULONG)((PUCHAR)(BaseIoAddressBM) + (DeviceNumber ? 0x24 : 0x20)), 
                      0
                      );*/
                AtapiWritePort4(chan, (PULONG)((PUCHAR)(BaseIoAddressBM) + 0x20), 
                      0
                      );
            }
        } else
        if(deviceExtension->MemIo) {
            // end transaction
            AtapiWritePort4(chan,
                 (PULONG)&(BaseIoAddressBM->Command), 
                 (AtapiReadPort4(chan,
                     (PULONG)&(BaseIoAddressBM->Command)) & ~0x00000080) );
            // clear flag
            chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_ACTIVE;
            return 0;
        }
        break;
    }

    // get status
    dma_status = AtapiReadPort1(chan, &(BaseIoAddressBM->Status)) & BM_STATUS_MASK;
    // end transaction
    AtapiWritePort1(chan, &(BaseIoAddressBM->Command), 
         AtapiReadPort1(chan, &(BaseIoAddressBM->Command)) &
             ~BM_COMMAND_START_STOP);
    // clear interrupt and error status
    AtapiWritePort1(chan, &(BaseIoAddressBM->Status), BM_STATUS_ERR | BM_STATUS_INTR);
    // clear flag
    chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_ACTIVE;

    return dma_status;

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
/*        for(i=AtaUmode(&(lun->IdentifyData)); i>=0; i--) {
            AtapiDmaInit(deviceExtension, ldev & 1, ldev >> 1,
                         (CHAR)AtaPioMode(&(lun->IdentifyData)),
                         (CHAR)AtaWmode(&(lun->IdentifyData)),
                         UDMA_MODE0+(CHAR)i );
        }
        for(i=AtaWmode(&(lun->IdentifyData)); i>=0; i--) {
            AtapiDmaInit(deviceExtension, ldev & 1, ldev >> 1,
                         (CHAR)AtaPioMode(&(lun->IdentifyData)),
                         (CHAR)AtaWmode(&(lun->IdentifyData)),
                         UDMA_MODE0+(CHAR)i );
        }*/
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
    PHW_CHANNEL chan = &deviceExtension->chan[lChannel];
    LONG statusByte = 0;
    ULONG dev = Channel*2 + DeviceNumber;
    ULONG ldev = lChannel*2 + DeviceNumber;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    LONG i;

    ULONG VendorID  =  deviceExtension->DevID        & 0xffff;
    ULONG DeviceID  = (deviceExtension->DevID >> 16) & 0xffff;
    ULONG RevID     =  deviceExtension->RevID;
    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;
    ULONG ChipFlags = deviceExtension->HwFlags & CHIPFLAG_MASK;

    UCHAR *reg_val = NULL;

    /* set our most pessimistic default mode */
    deviceExtension->lun[ldev].TransferMode = ATA_PIO;
    if(!deviceExtension->BaseIoAddressBM[lChannel]) {
        wdmamode = udmamode = -1;
    }

    // Limit transfer mode
    if(deviceExtension->MaxTransferMode >= ATA_UDMA) {
        udmamode = min( udmamode, (CHAR)(deviceExtension->MaxTransferMode - ATA_UDMA));
    } else
    if(deviceExtension->MaxTransferMode >= ATA_WDMA) {
        udmamode = -1;
        wdmamode = min( wdmamode, (CHAR)(deviceExtension->MaxTransferMode - ATA_WDMA));
    } else {
        wdmamode = udmamode = -1;
    }

    if (udmamode > 2 && !deviceExtension->lun[ldev].IdentifyData.HwResCableId) {
        KdPrint2((PRINT_PREFIX "AtapiDmaInit: DMA limited to UDMA33, non-ATA66 compliant cable"));
        udmamode = 2;
    }

    if(deviceExtension->UnknownDev) {
        KdPrint2((PRINT_PREFIX "Unknown chip, omit Vendor/Dev checks"));
        goto try_generic_dma;
    }

    switch(VendorID) {
    case ATA_ACARD_ID: {
        /*********/
        /* Acard */
        /*********/
        static const USHORT reg4a = 0xa6;
        UCHAR  reg = 0x40 + (UCHAR)dev;

        if(ChipType == ATPOLD) {
            /* Old Acard 850 */
            static const USHORT reg4x = 0x0301;

            for(i=udmamode; i>=0; i--) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
set_old_acard:
                    ChangePciConfig1(0x54, a | (0x01 << dev) | ((i+1) << (dev*2)));
                    SetPciConfig1(0x4a, reg4a);
                    SetPciConfig2(reg,  reg4x);
                    return;
                }

            }
            if (wdmamode >= 2 && apiomode >= 4) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                    goto set_old_acard;
                }
            }
        } else {
            /* New Acard 86X */
            static const UCHAR reg4x = 0x31;

            for(i=udmamode; i>=0; i--) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
set_new_acard:
                    ChangePciConfig2(0x44, (a & ~(0x000f << (dev * 4))) | ((i+1) << (dev*4)));
                    SetPciConfig1(0x4a, reg4a);
                    SetPciConfig1(reg,  reg4x);
                    return;
                }

            }
            if (wdmamode >= 2 && apiomode >= 4) {
                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                    goto set_new_acard;
                }
            }
        }
        break; }
    case ATA_ACER_LABS_ID: {
        /************************/
        /* Acer Labs Inc. (ALI) */
        /************************/
        static const UCHAR ali_udma[] = {0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x0f};
        static const ULONG ali_pio[]  =
                { 0x006d0003, 0x00580002, 0x00440001, 0x00330001,
                  0x00310001, 0x00440001};
        /* the older Aladdin doesn't support ATAPI DMA on both master & slave */
        if ((ChipFlags & ALIOLD) &&
            (udmamode >= 0 || wdmamode >= 0)) {
            if(ATAPI_DEVICE(deviceExtension, lChannel*2) &&
               ATAPI_DEVICE(deviceExtension, lChannel*2 + 1)) {
                // 2 devices on this channel - NO DMA
                chan->MaxTransferMode =
                    min(chan->MaxTransferMode, ATA_PIO4);
                udmamode = wdmamode = -1;
                break;
            }
        }
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA0 + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                ULONG word54;
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA0 + i);
        
                GetPciConfig4(0x54, word54);
                word54 &= ~(0x000f000f << (dev * 4));
                word54 |= (((ali_udma[i]<<16) | 5) << (dev * 4));
                SetPciConfig4(0x54, word54);
                ChangePciConfig1(0x53, a | 0x03);
                SetPciConfig4(0x58 + (Channel<<2), 0x00310001);
                return;
            }
        }
        /* make sure eventual UDMA mode from the BIOS is disabled */
        ChangePciConfig2(0x56, a & ~(0x0008 << (dev * 4)) );
        if (wdmamode >= 2 && apiomode >= 4) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                ChangePciConfig1(0x53, a | 0x03);
                deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                chan->ChannelCtrlFlags |= CTRFLAGS_DMA_RO;
                return;
            }
        }
        ChangePciConfig1(0x53, (a & ~0x01) | 0x02);

        for(i=apiomode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_PIO0 + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_PIO0 + i);
                ChangePciConfig4(0x54, a & ~(0x0008000f << (dev * 4)));
                SetPciConfig4(0x58 + (Channel<<2), ali_pio[i]);
                return;
            }
        }
        break; }
    case ATA_AMD_ID:
    case ATA_NVIDIA_ID:
    case ATA_VIA_ID: {
        /********************/
        /* AMD, nVidia, VIA */
        /********************/
        static const UCHAR via_modes[5][7] = {
            { 0xc2, 0xc1, 0xc0, 0x00, 0x00, 0x00, 0x00 },        /* ATA33 */
            { 0xee, 0xec, 0xea, 0xe9, 0xe8, 0x00, 0x00 },        /* ATA66 */
            { 0xf7, 0xf6, 0xf4, 0xf2, 0xf1, 0xf0, 0x00 },        /* ATA100 */
            { 0xf7, 0xf7, 0xf6, 0xf4, 0xf2, 0xf1, 0xf0 },        /* VIA ATA133 */
            { 0xc2, 0xc1, 0xc0, 0xc4, 0xc5, 0xc6, 0xc7 }};       /* AMD/nVIDIA */
        static const via_pio[] =
            { 0xff, 0xff, 0xff, 0x55, 0x51, 0xff, 0x55 };
        const UCHAR *reg_val = NULL;
        UCHAR reg = 0x53-(UCHAR)dev;

        reg_val = &via_modes[ChipType][0];

        if(ChipFlags & NVIDIA)
            reg += 0x10;

        for(i = udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_UDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig1(reg-0x08, 0x51);
                SetPciConfig1(reg, (UCHAR)reg_val[i]);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA0+(UCHAR)i;
                return;
            }
        }
        for(i = wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_WDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                SetPciConfig1(reg-0x08, i ? 0x51 : 0x55);
                SetPciConfig1(reg, 0x8b);
                deviceExtension->lun[ldev].TransferMode = ATA_UDMA0+(UCHAR)i;
                return;
            }
        }
        /* set PIO mode timings */
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
        if(apiomode >= 0)
            SetPciConfig1(reg-0x08, via_pio[apiomode]);
        via82c_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
        return;

        break; }
    case ATA_CYRIX_ID: {
        /*********/
        /* Cyrix */
        /*********/
        ULONG cyr_piotiming[] = 
            { 0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010 };
        ULONG cyr_wdmatiming[] = { 0x00077771, 0x00012121, 0x00002020 };
        ULONG cyr_udmatiming[] = { 0x00921250, 0x00911140, 0x00911030 };

        if(apiomode >= 4)
            apiomode = 4;
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                AtapiWritePort4(chan, (PULONG)(((PUCHAR)(&(deviceExtension->BaseIoAddressBM_0->Command))+0x24+(dev << 3))), 
                      cyr_udmatiming[udmamode]);
                return;
            }
        }
        for(i=wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_WDMA + i);
                AtapiWritePort4(chan, (PULONG)((PUCHAR)(&(deviceExtension->BaseIoAddressBM_0->Command))+0x24+(dev << 3)), 
                      cyr_wdmatiming[wdmamode]);
                return;
            }
        }
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            (UCHAR)(ATA_PIO0 + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
        if (!(statusByte & IDE_STATUS_ERROR)) {
            deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_PIO0 + i);
            AtapiWritePort4(chan, (PULONG)((PUCHAR)(&(deviceExtension->BaseIoAddressBM_0->Command))+0x24+(dev << 3)), 
                  cyr_piotiming[apiomode]);
            return;
        }

        break; }
    case ATA_CYPRESS_ID:
        /***********/
        /* Cypress */
        /***********/
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
    case ATA_HIGHPOINT_ID: {
        /********************/
        /* High Point (HPT) */
        /********************/
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                hpt_timing(deviceExtension, dev, (UCHAR)(ATA_UDMA + i));       // ???
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }
        
        for(i=wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA0+i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                hpt_timing(deviceExtension, dev, (UCHAR)(ATA_WDMA0+i));
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_WDMA0+i);
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
        break; }
    case ATA_INTEL_ID: {
        /*********/
        /* Intel */
        /*********/
        if(deviceExtension->DevID == ATA_I82371FB) {
            if (wdmamode >= 2 && apiomode >= 4) {
                ULONG word40;

                GetPciConfig4(0x40, word40);
                word40 >>= Channel * 16;

                /* Check for timing config usable for DMA on controller */
                if (!((word40 & 0x3300) == 0x2300 &&
                      ((word40 >> ((!(DeviceNumber & 1)) ? 0 : 4)) & 1) == 1)) {
                    udmamode = wdmamode = -1;
                    break;
                }

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_WDMA2,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

                if (!(statusByte & IDE_STATUS_ERROR)) {
                    deviceExtension->lun[ldev].TransferMode = ATA_WDMA2;
                    return;
                }
            }
            break;
        }
        if(udmamode >= ATA_SA150) {
            deviceExtension->lun[ldev].TransferMode = udmamode;
            return;
        }
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
        break; }
    case ATA_PROMISE_ID:
        /***********/
        /* Promise */
        /***********/
        if(ChipType < PRTX) {
            if (ATAPI_DEVICE(deviceExtension, ldev)) {
                udmamode =
                wdmamode = -1;
            }
        }
        if(udmamode > ATA_SA150) {
            deviceExtension->lun[ldev].TransferMode = udmamode;
            return;
        }
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                promise_timing(deviceExtension, dev, (UCHAR)(ATA_UDMA + i));       // ???
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }
        
        for(i=wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA0+i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                promise_timing(deviceExtension, dev, (UCHAR)(ATA_WDMA0+i));
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_WDMA0+i);
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
        promise_timing(deviceExtension, dev, ATA_PIO0 + apiomode);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
        return;
        break;
    case ATA_SERVERWORKS_ID: {
        /***************/
        /* ServerWorks */
        /***************/
        static const UCHAR sw_wdma_modes[] = { 0x34, 0x22, 0x20 };
        static const UCHAR sw_pio_modes[] = { 0x5d, 0x47, 0x34, 0x22, 0x20, 0x34 };
        USHORT reg56;
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                ChangePciConfig1(0x54, a | (0x01 << dev));
                GetPciConfig2(0x56, reg56);
                reg56 &= ~(0xf << (dev * 4));
                reg56 |= ((UCHAR)i << (dev * 4));
                SetPciConfig2(0x56, reg56);
                SetPciConfig4(0x44, 0x20);
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }
        
        for(i=wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA0+i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                ULONG offset = dev ^ 0x1;

                ChangePciConfig1(0x54, a & ~(0x01 << dev));
                SetPciConfig4(0x44, sw_wdma_modes[i]);
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_WDMA0+i);
                return;
            }
        }
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
        SetPciConfig4(0x44, sw_pio_modes[apiomode]);
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
        return;
        break; }
    case ATA_SILICON_IMAGE_ID: {
        /********************/
        /* SiliconImage/CMD */
        /********************/
        if(ChipFlags & SII_SETCLK) {

            static const UCHAR sil_modes[7] = 
                { 0xf, 0xb, 0x7, 0x5, 0x3, 0x2, 0x1 };
            static const USHORT sil_wdma_modes[3] = 
                { 0x2208, 0x10c2, 0x10c1 };
            static const USHORT sil_pio_modes[6] = 
                { 0x328a, 0x2283, 0x1104, 0x10c3, 0x10c1, 0x10c1 };

            UCHAR ureg = 0xac + ((UCHAR)DeviceNumber * 0x02) + ((UCHAR)Channel * 0x10);
            UCHAR uval;
            UCHAR mreg = Channel ? 0x84 : 0x80;
            UCHAR mask = DeviceNumber ? 0x30 : 0x03;
            UCHAR mode;

            GetPciConfig1(ureg, uval);
            GetPciConfig1(mreg, mode);

            /* enable UDMA mode */
            for(i = udmamode; i>=0; i--) {

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_UDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig1(mreg, mode | mask);
                    SetPciConfig1(ureg, (uval & 0x3f) | sil_modes[i]);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA0+(UCHAR)i;
                    return;
                }
            }
            for(i = wdmamode; i>=0; i--) {

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_WDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig1(mreg, mode | (mask & 0x22));
                    SetPciConfig2(ureg - 0x4, sil_wdma_modes[i]);
                    deviceExtension->lun[ldev].TransferMode = ATA_WDMA0+(UCHAR)i;
                    return;
                }
            }
            /* restore PIO mode */
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_PIO0+(UCHAR)apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            SetPciConfig1(mreg, mode | (mask & 0x11));
            SetPciConfig2(ureg - 0x8, sil_pio_modes[i]);
            deviceExtension->lun[ldev].TransferMode = ATA_PIO0+(UCHAR)apiomode;
            return;

        } else {

            static const UCHAR cmd_modes[2][6] = {
                { 0x31, 0x21, 0x011, 0x25, 0x15, 0x05 },
                { 0xc2, 0x82, 0x042, 0x8a, 0x4a, 0x0a } };
            static const UCHAR cmd_wdma_modes[] = { 0x87, 0x32, 0x3f };
            static const UCHAR cmd_pio_modes[] = { 0xa9, 0x57, 0x44, 0x32, 0x3f };
            ULONG treg = 0x54 + (dev < 3) ? (dev << 1) : 7;

            udmamode = min(udmamode, 5);
            /* enable UDMA mode */
            for(i = udmamode; i>=0; i--) {
                UCHAR umode;

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_UDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    GetPciConfig1(Channel ? 0x7b : 0x73, umode);
                    umode &= ~(!(DeviceNumber & 1) ? 0x35 : 0xca);
                    umode |= ( cmd_modes[DeviceNumber & 1][i]);
                    SetPciConfig1(Channel ? 0x7b : 0x73, umode);
                    deviceExtension->lun[ldev].TransferMode = ATA_UDMA5;
                    return;
                }
            }
            /* make sure eventual UDMA mode from the BIOS is disabled */
            ChangePciConfig1(Channel ? 0x7b : 0x73, a & ~(!(DeviceNumber & 1) ? 0x35 : 0xca));

            for(i = wdmamode; i>=0; i--) {

                statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    ATA_WDMA0+(UCHAR)i,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
                if (!(statusByte & IDE_STATUS_ERROR)) {
                    SetPciConfig1(treg, cmd_wdma_modes[i]);
                    ChangePciConfig1(Channel ? 0x7b : 0x73, a & ~(!(DeviceNumber & 1) ? 0x35 : 0xca));
                    deviceExtension->lun[ldev].TransferMode = ATA_WDMA0+(UCHAR)i;
                    return;
                }
            }
            /* set PIO mode timings */
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                ATA_PIO0+(UCHAR)apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);

            SetPciConfig1(treg, cmd_pio_modes[apiomode]);
            ChangePciConfig1(Channel ? 0x7b : 0x73, a & ~(!(DeviceNumber & 1) ? 0x35 : 0xca));
            deviceExtension->lun[ldev].TransferMode = ATA_PIO0+(UCHAR)apiomode;
            return;

        }
        return;
        break; }
    case ATA_SIS_ID: {
        /*******/
        /* SiS */
        /*******/
        PULONG sis_modes;
        static const ULONG sis_modes_new133[] =
                { 0x28269008, 0x0c266008, 0x04263008, 0x0c0a3008, 0x05093008,
                  0x22196008, 0x0c0a3008, 0x05093008, 0x050939fc, 0x050936ac,
                  0x0509347c, 0x0509325c, 0x0509323c, 0x0509322c, 0x0509321c};
        static const ULONG sis_modes_old133[] =
                { 0x00cb, 0x0067, 0x0044, 0x0033, 0x0031, 0x0044, 0x0033, 0x0031,
                  0x8f31, 0x8a31, 0x8731, 0x8531, 0x8331, 0x8231, 0x8131 };
        static const ULONG sis_modes_old[] =
                { 0x0c0b, 0x0607, 0x0404, 0x0303, 0x0301, 0x0404, 0x0303, 0x0301,
                  0xf301, 0xd301, 0xb301, 0xa301, 0x9301, 0x8301 };
	static const ULONG sis_modes_new100[] =
		{ 0x00cb, 0x0067, 0x0044, 0x0033, 0x0031, 0x0044, 0x0033, 0x0031,
		  0x8b31, 0x8731, 0x8531, 0x8431, 0x8231, 0x8131 };

        ULONG reg;
        UCHAR reg57;
        ULONG reg_size;
        ULONG offs;

        switch(ChipType) {
        case SIS133NEW:
            sis_modes = (PULONG)(&sis_modes_new133[0]);
            reg_size = 4;
            GetPciConfig1(0x57, reg57);
            reg = (reg57 & 0x40 ? 0x70 : 0x40) + (dev * 4);
            break;
        case SIS133OLD:
            sis_modes = (PULONG)(&sis_modes_old133[0]);
            reg_size = 2;
            reg = 0x40 + (dev * 2);
            break;
        case SIS100NEW:
            sis_modes = (PULONG)(&sis_modes_new100[0]);
            reg_size = 2;
	    reg = 0x40 + (dev * 2);
	    break;
        case SIS100OLD:
        case SIS66:
        case SIS33:
            sis_modes = (PULONG)(&sis_modes_old[0]);
            reg_size = 2;
            reg = 0x40 + (dev * 2);
            break;
        }

        offs = 5+3;
        for(i=udmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_UDMA + i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                if(reg_size == 4) {
                    SetPciConfig4(reg, sis_modes[offs+i]);
                } else {
                    SetPciConfig2(reg, (USHORT)sis_modes[offs+i]);
                }
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_UDMA + i);
                return;
            }
        }
        
        offs = 5;
        for(i=wdmamode; i>=0; i--) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA0+i),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            if (!(statusByte & IDE_STATUS_ERROR)) {
                if(reg_size == 4) {
                    SetPciConfig4(reg, sis_modes[offs+i]);
                } else {
                    SetPciConfig2(reg, (USHORT)sis_modes[offs+i]);
                }
                deviceExtension->lun[ldev].TransferMode = (UCHAR)(ATA_WDMA0+i);
                return;
            }
        }
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                            ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
        if(reg_size == 4) {
            SetPciConfig4(reg, sis_modes[apiomode]);
        } else {
            SetPciConfig2(reg, (USHORT)sis_modes[apiomode]);
        }
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
        return;
        break; }
    case 0x16ca:
        /* Cenatek Rocket Drive controller */
        if (wdmamode >= 0 &&
            (AtapiReadPort1(chan, &deviceExtension->BaseIoAddressBM[lChannel]->Status) & 
             (DeviceNumber ? BM_STATUS_DRIVE_1_DMA : BM_STATUS_DRIVE_0_DMA))) {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_WDMA0+wdmamode),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            deviceExtension->lun[ldev].TransferMode = ATA_WDMA+wdmamode;
        } else {
            statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                (UCHAR)(ATA_PIO+apiomode),  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
            deviceExtension->lun[ldev].TransferMode = ATA_PIO+apiomode;
        }
        return;
    }

try_generic_dma:

    /* unknown controller chip */

    /* better not try generic DMA on ATAPI devices it almost never works */
    if (ATAPI_DEVICE(deviceExtension, ldev)) {
        udmamode =
        wdmamode = -1;
    }

    /* if controller says its setup for DMA take the easy way out */
    /* the downside is we dont know what DMA mode we are in */
    if ((udmamode >= 0 || /*wdmamode > 1*/ wdmamode >= 0) &&
         deviceExtension->BaseIoAddressBM[lChannel] &&
        (AtapiReadPort1(chan, &deviceExtension->BaseIoAddressBM[lChannel]->Status) &
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
    statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                        IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                        ATA_PIO0 + apiomode,  ATA_C_F_SETXFER, ATA_WAIT_BASE_READY);
    if (!(statusByte & IDE_STATUS_ERROR)) {
        deviceExtension->lun[ldev].TransferMode = ATA_PIO0 + apiomode;
    } else {
        deviceExtension->lun[ldev].TransferMode = ATA_PIO;
    }
    return;
} // end AtapiDmaInit()


VOID
cyrix_timing(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
//    ASSERT(dev/2 >= deviceExtension->Channel);
//    PHW_CHANNEL chan = &(deviceExtension->chan[dev/2-deviceExtension->Channel]);
    ULONG reg20 = 0x0000e132;
    ULONG reg24 = 0x00017771;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    switch (mode) {
    case ATA_PIO0:        reg20 = 0x0000e132; break;
    case ATA_PIO1:        reg20 = 0x00018121; break;
    case ATA_PIO2:        reg20 = 0x00024020; break;
    case ATA_PIO3:        reg20 = 0x00032010; break;
    case ATA_PIO4:
    case ATA_PIO5:        reg20 = 0x00040010; break;
    case ATA_WDMA2:       reg24 = 0x00002020; break;
    case ATA_UDMA2:       reg24 = 0x00911030; break;
    }
    AtapiWritePort4(NULL, (PULONG)((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + (dev*8)) + 0x20, reg20);
    AtapiWritePort4(NULL, (PULONG)((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + (dev*8)) + 0x24, reg24);
} // cyrix_timing()
 
VOID
promise_timing(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;
    ULONG ChipFlags = deviceExtension->HwFlags & CHIPFLAG_MASK;

    ULONG timing = 0;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    switch(ChipType) {
    case PROLD:
        switch (mode) {
        default:
        case ATA_PIO0:  timing = 0x004ff329; break;
        case ATA_PIO1:  timing = 0x004fec25; break;
        case ATA_PIO2:  timing = 0x004fe823; break;
        case ATA_PIO3:  timing = 0x004fe622; break;
        case ATA_PIO4:  timing = 0x004fe421; break;
        case ATA_WDMA0: timing = 0x004567f3; break;
        case ATA_WDMA1: timing = 0x004467f3; break;
        case ATA_WDMA2: timing = 0x004367f3; break;
        case ATA_UDMA0: timing = 0x004367f3; break;
        case ATA_UDMA1: timing = 0x004247f3; break;
        case ATA_UDMA2: timing = 0x004127f3; break;
        }
        break;

    case PRNEW:
        switch (mode) {
        default:
        case ATA_PIO0:  timing = 0x004fff2f; break;
        case ATA_PIO1:  timing = 0x004ff82a; break;
        case ATA_PIO2:  timing = 0x004ff026; break;
        case ATA_PIO3:  timing = 0x004fec24; break;
        case ATA_PIO4:  timing = 0x004fe822; break;
        case ATA_WDMA0: timing = 0x004acef6; break;
        case ATA_WDMA1: timing = 0x0048cef6; break;
        case ATA_WDMA2: timing = 0x0046cef6; break;
        case ATA_UDMA0: timing = 0x0046cef6; break;
        case ATA_UDMA1: timing = 0x00448ef6; break;
        case ATA_UDMA2: timing = 0x00436ef6; break;
        case ATA_UDMA3: timing = 0x00424ef6; break;
        case ATA_UDMA4: timing = 0x004127f3; break;
        case ATA_UDMA5: timing = 0x004127f3; break;
        }
        break;
    default:
        return;
    }
    SetPciConfig4(0x60 + (dev<<2), timing);
} // end promise_timing()


VOID
hpt_timing(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG dev,               // physical device number (0-3)
    IN CHAR  mode
    )
{
    PVOID HwDeviceExtension = (PVOID)deviceExtension;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;

    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;
    ULONG ChipFlags = deviceExtension->HwFlags & CHIPFLAG_MASK;

    ULONG timing;

    if(mode == ATA_PIO5)
        mode = ATA_PIO4;

    switch(ChipType) {
    case HPT374:

        switch (mode) {                                                /* HPT374 */
        case ATA_PIO0:  timing = 0x0ac1f48a; break;
        case ATA_PIO1:  timing = 0x0ac1f465; break;
        case ATA_PIO2:  timing = 0x0a81f454; break;
        case ATA_PIO3:  timing = 0x0a81f443; break;
        case ATA_PIO4:  timing = 0x0a81f442; break;
        case ATA_WDMA0: timing = 0x228082ea; break;
        case ATA_WDMA1: timing = 0x22808254; break;
        case ATA_WDMA2: timing = 0x22808242; break;
        case ATA_UDMA0: timing = 0x121882ea; break;
        case ATA_UDMA1: timing = 0x12148254; break;
        case ATA_UDMA2: timing = 0x120c8242; break;
        case ATA_UDMA3: timing = 0x128c8242; break;
        case ATA_UDMA4: timing = 0x12ac8242; break;
        case ATA_UDMA5: timing = 0x12848242; break;
        case ATA_UDMA6: timing = 0x12448242; break;
        default:        timing = 0x0d029d5e;
        }
        break;

    case HPT372:

        switch (mode) {                                                /* HPT372 */
        case ATA_PIO0:  timing = 0x0d029d5e; break;
        case ATA_PIO1:  timing = 0x0d029d26; break;
        case ATA_PIO2:  timing = 0x0c829ca6; break;
        case ATA_PIO3:  timing = 0x0c829c84; break;
        case ATA_PIO4:  timing = 0x0c829c62; break;
        case ATA_WDMA0: timing = 0x2c82922e; break;
        case ATA_WDMA1: timing = 0x2c829266; break;
        case ATA_WDMA2: timing = 0x2c829262; break;
        case ATA_UDMA0: timing = 0x1c82dc62; break;
        case ATA_UDMA1: timing = 0x1c9adc62; break;
        case ATA_UDMA2: timing = 0x1c91dc62; break;
        case ATA_UDMA3: timing = 0x1c8edc62; break;
        case ATA_UDMA4: timing = 0x1c8ddc62; break;
        case ATA_UDMA5: timing = 0x1c6ddc62; break;
        case ATA_UDMA6: timing = 0x1c81dc62; break;
        default:        timing = 0x0d029d5e;
        }
        break;

    case HPT370:

        switch (mode) {                                                /* HPT370 */
        case ATA_PIO0:  timing = 0x06914e57; break;
        case ATA_PIO1:  timing = 0x06914e43; break;
        case ATA_PIO2:  timing = 0x06514e33; break;
        case ATA_PIO3:  timing = 0x06514e22; break;
        case ATA_PIO4:  timing = 0x06514e21; break;
        case ATA_WDMA0: timing = 0x26514e97; break;
        case ATA_WDMA1: timing = 0x26514e33; break;
        case ATA_WDMA2: timing = 0x26514e21; break;
        case ATA_UDMA0: timing = 0x16514e31; break;
        case ATA_UDMA1: timing = 0x164d4e31; break;
        case ATA_UDMA2: timing = 0x16494e31; break;
        case ATA_UDMA3: timing = 0x166d4e31; break;
        case ATA_UDMA4: timing = 0x16454e31; break;
        case ATA_UDMA5: timing = 0x16454e31; break;
        default:        timing = 0x06514e57;
        }

    case HPT366: {
        UCHAR reg41;

        GetPciConfig1(0x41 + (dev << 2), reg41);

        switch (reg41) {
        case 0x85:        /* 25Mhz */
            switch (mode) {
            case ATA_PIO0:        timing = 0x40d08585; break;
            case ATA_PIO1:        timing = 0x40d08572; break;
            case ATA_PIO2:        timing = 0x40ca8542; break;
            case ATA_PIO3:        timing = 0x40ca8532; break;
            case ATA_PIO4:        timing = 0x40ca8521; break;
            case ATA_WDMA2:       timing = 0x20ca8521; break;
            case ATA_UDMA2:       timing = 0x10cf8521; break;
            case ATA_UDMA4:       timing = 0x10c98521; break;
            default:              timing = 0x01208585;
            }
            break;
        default:
        case 0xa7:        /* 33MHz */
            switch (mode) {
            case ATA_PIO0:        timing = 0x40d0a7aa; break;
            case ATA_PIO1:        timing = 0x40d0a7a3; break;
            case ATA_PIO2:        timing = 0x40d0a753; break;
            case ATA_PIO3:        timing = 0x40c8a742; break;
            case ATA_PIO4:        timing = 0x40c8a731; break;
            case ATA_WDMA0:       timing = 0x20c8a797; break;
            case ATA_WDMA1:       timing = 0x20c8a732; break;
            case ATA_WDMA2:       timing = 0x20c8a731; break;
            case ATA_UDMA0:       timing = 0x10c8a731; break;
            case ATA_UDMA1:       timing = 0x10cba731; break;
            case ATA_UDMA2:       timing = 0x10caa731; break;
            case ATA_UDMA3:       timing = 0x10cfa731; break;
            case ATA_UDMA4:       timing = 0x10c9a731; break;
            default:              timing = 0x0120a7a7;
            }
            break;
        case 0xd9:        /* 40Mhz */
            switch (mode) {
            case ATA_PIO0:        timing = 0x4018d9d9; break;
            case ATA_PIO1:        timing = 0x4010d9c7; break;
            case ATA_PIO2:        timing = 0x4010d997; break;
            case ATA_PIO3:        timing = 0x4010d974; break;
            case ATA_PIO4:        timing = 0x4008d963; break;
            case ATA_WDMA2:       timing = 0x2008d943; break;
            case ATA_UDMA2:       timing = 0x100bd943; break;
            case ATA_UDMA4:       timing = 0x100fd943; break;
            default:              timing = 0x0120d9d9;
            }
        }
        break; }
    }

    SetPciConfig4(0x40 + (dev<<2), timing);
} // end hpt_timing()


#define FIT(v,min,max) (((v)>(max)?(max):(v))<(min)?(min):(v))

VOID
via82c_timing(
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

    // Newer chips dislike this:
    if(!(deviceExtension->HwFlags & VIAAST)) {
        /* PIO address setup */
        GetPciConfig1(0x4c, t);
        t = (t & ~(3 << ((3 - dev) << 1))) | (FIT(setup - 1, 0, 3) << ((3 - dev) << 1));
        SetPciConfig1(0x4c, t);
    }

    /* PIO active & recover */
    SetPciConfig1(0x4b-dev, (FIT(active - 1, 0, 0xf) << 4) | FIT(recover - 1, 0, 0xf) );
} // end via82c_timing()

