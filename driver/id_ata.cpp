/*++

Copyright (c) 2002-2004 Alexandr A. Telyatnikov (Alter)

Module Name:
    id_ata.cpp

Abstract:
    This is the miniport driver for ATA/ATAPI IDE controllers
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

    The skeleton was taken from standard ATAPI.SYS from NT4 DDK by
         Mike Glass (MGlass)
         Chuck Park (ChuckP)

    Some parts of code were taken from FreeBSD 4.3-4.6 ATA driver by
         Søren Schmidt, Copyright (c) 1998,1999,2000,2001

    All parts of code are greatly changed/updated by
         Alter, Copyright (c) 2002-2004:

    1. Internal command queueing/reordering
    2. Drive identification
    3. Support for 2 _independent_ channels in a single PCI device
    4. Smart host<->drive transfer rate slowdown (for bad cable)
    5. W2k support (binary compatibility)
    6. HDD hot swap under NT4
    7. XP support (binary compatibility)
    
    etc. (See todo.txt)


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

UNICODE_STRING SavedRegPath;
WCHAR SavedRegPathBuffer[256];

UNICODE_STRING SavedSPString;
WCHAR SavedSPStringBuffer[256];

ULONG  MajorVersion = 0;
ULONG  MinorVersion = 0;
ULONG  BuildNumber  = 0;
ULONG  SPVersion = 0;

ULONG  SkipRaids = 1;

//UCHAR EnableDma = FALSE;
//UCHAR EnableReorder = FALSE;

VOID
AtapiHwInitialize__(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG lChannel
    );

BOOLEAN
AtapiResetController__(
    IN PVOID HwDeviceExtension,
    IN ULONG PathId,
    IN BOOLEAN CompleteAll
    );

VOID
AtapiCallBack_0(
    IN PVOID HwDeviceExtension
    );

VOID
AtapiCallBack_1(
    IN PVOID HwDeviceExtension
    );

BOOLEAN
AtapiEnableInterrupts__(
    IN PVOID HwDeviceExtension
    );

VOID
AtapiQueueTimerDpc(
    IN PVOID HwDeviceExtension,
    IN ULONG lChannel,
    IN PHW_TIMER HwScsiTimer,
    IN ULONG MiniportTimerValue
    );

BOOLEAN
AtapiCheckInterrupt__(
    IN PVOID HwDeviceExtension,
    IN UCHAR c
    );

SCSI_ADAPTER_CONTROL_STATUS
AtapiAdapterControl(
    IN PVOID HwDeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    );

VOID
AtapiWritePort4(
    IN PHW_CHANNEL chan,
    IN PULONG _port,
    IN ULONG data
    )
{
    if(!chan || !chan->MemIo) {
        ScsiPortWritePortUlong(_port, data);
        return;
    } else {
        ULONG port = (ULONG)_port;
        port -= chan->BaseMemAddressOffset;
        port = chan->RegTranslation[port];
        port += chan->BaseMemAddress;
        ScsiPortWriteRegisterUlong((PULONG)port, data);
    }
} // end AtapiWritePort4()

VOID
AtapiWritePort2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port,
    IN USHORT data
    )
{
    if(!chan || !chan->MemIo) {
        ScsiPortWritePortUshort(_port, data);
        return;
    } else {
        ULONG port = (ULONG)_port;
        port -= chan->BaseMemAddressOffset;
        port = chan->RegTranslation[port];
        port += chan->BaseMemAddress;
        ScsiPortWriteRegisterUshort((PUSHORT)port, data);
    }
} // end AtapiWritePort2()

VOID
AtapiWritePort1(
    IN PHW_CHANNEL chan,
    IN PUCHAR _port,
    IN UCHAR data
    )
{
    if(!chan || !chan->MemIo) {
        ScsiPortWritePortUchar(_port, data);
        return;
    } else {
        ULONG port = (ULONG)_port;
        port -= chan->BaseMemAddressOffset;
        port = chan->RegTranslation[port];
        port += chan->BaseMemAddress;
        ScsiPortWriteRegisterUchar((PUCHAR)port, data);
    }
} // end AtapiWritePort1()

ULONG
__fastcall
AtapiReadPort4(
    IN PHW_CHANNEL chan,
    IN PULONG _port
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        return ScsiPortReadPortUlong(_port);
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    return ScsiPortReadRegisterUlong((PULONG)port);
} // end AtapiReadPort4()

USHORT
__fastcall
AtapiReadPort2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        return ScsiPortReadPortUshort(_port);
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    return (USHORT)ScsiPortReadRegisterUshort((PUSHORT)port);
} // end AtapiReadPort2()

UCHAR
__fastcall
AtapiReadPort1(
    IN PHW_CHANNEL chan,
    IN PUCHAR _port
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        return ScsiPortReadPortUchar(_port);
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    return (UCHAR)ScsiPortReadRegisterUchar((PUCHAR)port);
} // end AtapiReadPort1()

VOID
__fastcall
AtapiWriteBuffer4(
    IN PHW_CHANNEL chan,
    IN PULONG _port,
    IN PVOID Buffer,
    IN ULONG Count
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        ScsiPortWritePortBufferUlong(_port, (PULONG)Buffer, Count);
        return;
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    ScsiPortWriteRegisterBufferUlong((PULONG)port, (PULONG)Buffer, Count);
    return;
} // end AtapiWriteBuffer4()

VOID
__fastcall
AtapiWriteBuffer2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port,
    IN PVOID Buffer,
    IN ULONG Count
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        ScsiPortWritePortBufferUshort(_port, (PUSHORT)Buffer, Count);
        return;
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    ScsiPortWriteRegisterBufferUshort((PUSHORT)port, (PUSHORT)Buffer, Count);
    return;
} // end AtapiWriteBuffer2()

VOID
__fastcall
AtapiReadBuffer4(
    IN PHW_CHANNEL chan,
    IN PULONG _port,
    IN PVOID Buffer,
    IN ULONG Count
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        ScsiPortReadPortBufferUlong(_port, (PULONG)Buffer, Count);
        return;
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    ScsiPortReadRegisterBufferUlong((PULONG)port, (PULONG)Buffer, Count);
    return;
} // end AtapiReadBuffer2()

VOID
__fastcall
AtapiReadBuffer2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port,
    IN PVOID Buffer,
    IN ULONG Count
    )
{
    ULONG port = (ULONG)_port;
    if(!chan || !chan->MemIo) {
        ScsiPortReadPortBufferUshort(_port, (PUSHORT)Buffer, Count);
        return;
    }
    port -= chan->BaseMemAddressOffset;
    port = chan->RegTranslation[port];
    port += chan->BaseMemAddress;
    ScsiPortReadRegisterBufferUshort((PUSHORT)port, (PUSHORT)Buffer, Count);
    return;
} // end AtapiReadBuffer2()

UCHAR
__fastcall
AtapiSuckPort2(
    IN PHW_CHANNEL chan,
    PIDE_REGISTERS_1 baseIoAddress1,
    PIDE_REGISTERS_2 baseIoAddress2
    )
{
    UCHAR statusByte;
    ULONG i;

    WaitOnBusyLong(chan, baseIoAddress2);
    for (i = 0; i < 0x10000; i++) {

        GetStatus(chan, baseIoAddress2, statusByte);
        if (statusByte & IDE_STATUS_DRQ) {
            // Suck out any remaining bytes and throw away.
            AtapiReadPort2(chan, (PUSHORT)&baseIoAddress1->i.Data);
        } else {
            break;
        }
    }
    if(i) {
        KdPrint2((PRINT_PREFIX "AtapiSuckPort2: overrun detected (%#x words)\n", i ));
    }
    return statusByte;
} // AtapiSuckPort2()

UCHAR
__fastcall
WaitOnBusy(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_2 BaseIoAddress
    )
{ \
    ULONG i;
    UCHAR Status;
    for (i=0; i<200; i++) { \
        GetStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10); \
            continue; \
        } else { \
            break; \
        } \
    }
    return Status;
}

UCHAR
__fastcall
WaitOnBusyLong(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_2 BaseIoAddress
    )
{ \
    ULONG i;
    UCHAR Status;
    for (i=0; i<20000; i++) { \
        GetStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10); \
            continue; \
        } else { \
            break; \
        } \
    }
    return Status;
}

UCHAR
__fastcall
WaitOnBaseBusy(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_1 BaseIoAddress
    )
{ \
    ULONG i; \
    UCHAR Status;
    for (i=0; i<200; i++) { \
        GetBaseStatus(chan,BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10); \
            continue; \
        } else { \
            break; \
        } \
    }
    return Status;
}

UCHAR
__fastcall
WaitOnBaseBusyLong(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_1 BaseIoAddress
    )
{ \
    ULONG i; \
    UCHAR Status;
    for (i=0; i<20000; i++) { \
        GetBaseStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10);
            continue; \
        } else { \
            break; \
        } \
    } \
    return Status;
}

UCHAR
__fastcall
WaitForIdleLong(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_2 BaseIoAddress
    )
{ \
    ULONG i;
    UCHAR Status;
    for (i=0; i<20000; i++) { \
        GetStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY ||
            (Status & ~(IDE_STATUS_ERROR | IDE_STATUS_INDEX)) != IDE_STATUS_IDLE) { \
            ScsiPortStallExecution(10); \
            continue; \
        } else { \
            break; \
        } \
    }
    return Status;
}

UCHAR
__fastcall
WaitForDrq(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_2 BaseIoAddress
    )
{ \
    ULONG i; \
    UCHAR Status;
    for (i=0; i<1000; i++) { \
        GetStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10); \
        } else if (Status & IDE_STATUS_DRQ) { \
            break; \
        } else { \
            ScsiPortStallExecution(10); \
        } \
    } \
    return Status;
}

UCHAR
__fastcall
WaitShortForDrq(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_2 BaseIoAddress
    )
{ \
    ULONG i; \
    UCHAR Status;
    for (i=0; i<2; i++) { \
        GetStatus(chan, BaseIoAddress, Status); \
        if (Status & IDE_STATUS_BUSY) { \
            ScsiPortStallExecution(10); \
        } else if (Status & IDE_STATUS_DRQ) { \
            break; \
        } else { \
            ScsiPortStallExecution(10); \
        } \
    } \
    return Status;
}

VOID
__fastcall
AtapiSoftReset(
    IN PHW_CHANNEL   chan,
    PIDE_REGISTERS_1 BaseIoAddress,
    ULONG            DeviceNumber
    )
{\
    SelectDrive(chan, BaseIoAddress,DeviceNumber); \
    ScsiPortStallExecution(10000);\
    AtapiWritePort1(chan, &BaseIoAddress->o.Command, IDE_COMMAND_ATAPI_RESET); \
    ScsiPortStallExecution(100000);\
    SelectDrive(chan, BaseIoAddress,DeviceNumber); \
    /*WaitOnBusy(chan, ((PIDE_REGISTERS_2)((PUCHAR)BaseIoAddress + 0x206)));*/ \
    /*ScsiPortStallExecution(500);*/\
}

/*
    Send command to device.
    Translate to 48-Lba form if required
*/
UCHAR
AtaCommand48(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,
    IN UCHAR command,
    IN ULONGLONG lba,
    IN USHORT count,
    IN USHORT feature,
    IN ULONG flags
    )
{
    PHW_CHANNEL          chan = &deviceExtension->chan[lChannel];
    PIDE_REGISTERS_1     baseIoAddress1 = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2 = deviceExtension->BaseIoAddress2[lChannel];
    UCHAR                statusByte;
    ULONG ldev = lChannel*2 + DeviceNumber;
    ULONG i;

    KdPrint2((PRINT_PREFIX "AtaCommand48: cntrlr %#x:%#x ldev %#x, cmd %#x, count %#x feature %#x\n",
                 deviceExtension->DevIndex, deviceExtension->Channel, ldev, command, count, feature ));

    SelectDrive(chan, baseIoAddress1, DeviceNumber);

    statusByte = WaitOnBusy(chan, baseIoAddress2);

    /* ready to issue command ? */
    if (statusByte & IDE_STATUS_BUSY) {
        KdPrint2((PRINT_PREFIX "AtaCommand48: Returning BUSY status\n"));
        return statusByte;
    }
    // !!! We should not check ERROR condition here
    // ERROR bit may be asserted durring previous operation
    // and not cleared after SELECT

    /* only use 48bit addressing if needed because of the overhead */
    if ((lba > 0xfffffffI64 || count > 256) &&
        deviceExtension->lun[ldev].IdentifyData.FeaturesSupport.Address48) {

        KdPrint2((PRINT_PREFIX "AtaCommand48: ldev %#x USE_LBA_48\n", ldev ));
        /* translate command into 48bit version */
        switch (command) {
        case IDE_COMMAND_READ:
            command = IDE_COMMAND_READ48; break;
        case IDE_COMMAND_READ_MULTIPLE:
            command = IDE_COMMAND_READ_MUL48; break;
        case IDE_COMMAND_READ_DMA:
            command = IDE_COMMAND_READ_DMA48; break;
        case IDE_COMMAND_READ_DMA_Q:
            command = IDE_COMMAND_READ_DMA_Q48; break;
        case IDE_COMMAND_WRITE:
            command = IDE_COMMAND_WRITE48; break;
        case IDE_COMMAND_WRITE_MULTIPLE:
            command = IDE_COMMAND_WRITE_MUL48; break;
        case IDE_COMMAND_WRITE_DMA:
            command = IDE_COMMAND_WRITE_DMA48; break;
        case IDE_COMMAND_WRITE_DMA_Q:
            command = IDE_COMMAND_WRITE_DMA_Q48; break;
        case IDE_COMMAND_FLUSH_CACHE:
            command = IDE_COMMAND_FLUSH_CACHE48; break;
        default:
            return -1;
        }

        chan->ChannelCtrlFlags |= CTRFLAGS_LBA48;

        AtapiWritePort1(chan, &baseIoAddress1->o.Feature,      (UCHAR)(feature>>8) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.Feature,      (UCHAR)feature);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockCount,   (UCHAR)(count>>8) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockCount,   (UCHAR)count & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockNumber,  (UCHAR)(lba>>24) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockNumber,  (UCHAR)lba & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderLow,  (UCHAR)(lba>>32) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderLow,  (UCHAR)(lba>>8) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderHigh, (UCHAR)(lba>>40) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderHigh, (UCHAR)(lba>>16) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.DriveSelect,  (DeviceNumber ? IDE_DRIVE_SELECT_2 : IDE_DRIVE_SELECT_1) );
    } else {

        chan->ChannelCtrlFlags &= ~CTRFLAGS_LBA48;
        
        AtapiWritePort1(chan, &baseIoAddress1->o.Feature,      (UCHAR)feature);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockCount,   (UCHAR)count);
        AtapiWritePort1(chan, &baseIoAddress1->o.BlockNumber,  (UCHAR)lba & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderLow,  (UCHAR)(lba>>8) & 0xff);
        AtapiWritePort1(chan, &baseIoAddress1->o.CylinderHigh, (UCHAR)(lba>>16) & 0xff);
        if(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_LBA_ENABLED) {
            KdPrint2((PRINT_PREFIX "AtaCommand48: ldev %#x USE_LBA\n", ldev ));
            AtapiWritePort1(chan, &baseIoAddress1->o.DriveSelect,  (UCHAR)((lba>>24) & 0xf) | IDE_USE_LBA | (DeviceNumber ? IDE_DRIVE_SELECT_2 : IDE_DRIVE_SELECT_1) );
        } else {
            KdPrint2((PRINT_PREFIX "AtaCommand48: ldev %#x USE_CHS\n", ldev ));
            AtapiWritePort1(chan, &baseIoAddress1->o.DriveSelect,  (UCHAR)((lba>>24) & 0xf) | (DeviceNumber ? IDE_DRIVE_SELECT_2 : IDE_DRIVE_SELECT_1) );
        }
    }

    // write command code to device
    AtapiWritePort1(chan, &baseIoAddress1->o.Command, command);

    switch (flags) {
    case ATA_WAIT_INTR:

        // caller requested wait for interrupt
        for(i=0;i<4;i++) {
            WaitOnBusy(chan, baseIoAddress2);
            statusByte = WaitForDrq(chan,baseIoAddress2);
            if (statusByte & IDE_STATUS_DRQ)
                break;
            ScsiPortStallExecution(500);
            KdPrint2((PRINT_PREFIX "AtaCommand48: retry waiting DRQ, statu %#x\n", statusByte));
        }

        return statusByte;

    case ATA_WAIT_IDLE:

        // caller requested wait for entering Wait state
        for (i=0; i<30 * 1000; i++) {

            GetStatus(chan, baseIoAddress2, statusByte);
            if (statusByte & IDE_STATUS_ERROR) {
                break;
            } else
            if ((statusByte & ~IDE_STATUS_INDEX ) == IDE_STATUS_IDLE) {
                break;
            } else {
                ScsiPortStallExecution(100);
            }
        }
        statusByte |= IDE_STATUS_BUSY;
        break;

    case ATA_WAIT_READY:
        statusByte = WaitOnBusyLong(chan, baseIoAddress2);
        break;
    case ATA_WAIT_BASE_READY:
        statusByte = WaitOnBaseBusyLong(chan, baseIoAddress1);
        break;
    case ATA_IMMEDIATE:
        chan->ExpectingInterrupt = TRUE;
        // !!!!!
        InterlockedExchange(&(deviceExtension->chan[lChannel].CheckIntr),
                                      CHECK_INTR_IDLE);
        statusByte = 0;
        break;
    }

    KdPrint2((PRINT_PREFIX "AtaCommand48: Status %#x\n", statusByte));

    return statusByte;
} // end AtaCommand48()

/*
    Send command to device.
    This is simply wrapper for AtaCommand48()
*/
UCHAR
AtaCommand(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,
    IN UCHAR command,
    IN USHORT cylinder,
    IN UCHAR head,
    IN UCHAR sector, 
    IN UCHAR count,
    IN UCHAR feature,
    IN ULONG flags
    )
{
    return AtaCommand48(deviceExtension, DeviceNumber, lChannel,
                        command, 
                        (ULONG)sector | ((ULONG)cylinder << 8) | ((ULONG)(head & 0x0f) << 24),
                        count, feature, flags);
} // end AtaCommand()

LONG
AtaPio2Mode(LONG pio)
{
    switch (pio) {
    default: return ATA_PIO;
    case 0: return ATA_PIO0;
    case 1: return ATA_PIO1;
    case 2: return ATA_PIO2;
    case 3: return ATA_PIO3;
    case 4: return ATA_PIO4;
    case 5: return ATA_PIO5;
    }
} // end AtaPio2Mode()

LONG
AtaPioMode(PIDENTIFY_DATA2 ident)
{
    if (ident->PioTimingsValid) {
        if (ident->AdvancedPIOModes & AdvancedPIOModes_5)
            return 5;
        if (ident->AdvancedPIOModes & AdvancedPIOModes_4)
            return 4;
        if (ident->AdvancedPIOModes & AdvancedPIOModes_3) 
            return 3;
    }        
    if (ident->PioCycleTimingMode == 2)
        return 2;
    if (ident->PioCycleTimingMode == 1)
        return 1;
    if (ident->PioCycleTimingMode == 0)
        return 0;
    return -1; 
} // end AtaPioMode()

LONG
AtaWmode(PIDENTIFY_DATA2 ident)
{
    if (ident->MultiWordDMASupport & 0x04)
        return 2;
    if (ident->MultiWordDMASupport & 0x02)
        return 1;
    if (ident->MultiWordDMASupport & 0x01)
        return 0;
    return -1;
} // end AtaWmode()

LONG
AtaUmode(PIDENTIFY_DATA2 ident)
{
    if (!ident->UdmaModesValid)
        return -1;
    if (ident->UltraDMASupport & 0x40)
        return 6;
    if (ident->UltraDMASupport & 0x20)
        return 5;
    if (ident->UltraDMASupport & 0x10)
        return 4;
    if (ident->UltraDMASupport & 0x08)
        return 3;
    if (ident->UltraDMASupport & 0x04)
        return 2;
    if (ident->UltraDMASupport & 0x02)
        return 1;
    if (ident->UltraDMASupport & 0x01)
        return 0;
    return -1;
} // end AtaUmode()

/*
    Get cost of insertion Req1 before Req2
 */
LONGLONG
AtapiGetCost(
    PHW_LU_EXTENSION LunExt,
    IN PATA_REQ AtaReq1,
    IN PATA_REQ AtaReq2
    )
{
    BOOLEAN write1;
    BOOLEAN write2;
    UCHAR flags1;
    UCHAR flags2;
    LONGLONG cost;
    // can't insert Req1 before tooooo old Req2
    if(!AtaReq2->ttl)
        return REORDER_COST_TTL;
    // check if reorderable
    flags1 = AtaReq1->Flags;
    flags2 = AtaReq2->Flags;
    if(!((flags2 & flags1) & REQ_FLAG_REORDERABLE_CMD))
        return REORDER_COST_DENIED;
    // if at least one Req is WRITE, target areas
    // can not intersect
    write1 = (flags1 & REQ_FLAG_RW_MASK) == REQ_FLAG_WRITE;
    write2 = (flags2 & REQ_FLAG_RW_MASK) == REQ_FLAG_WRITE;
    cost = AtaReq1->lba+AtaReq1->bcount - AtaReq2->lba;
    if( write1 || write2 ) {
        // check for intersection
        if((AtaReq1->lba < AtaReq2->lba+AtaReq2->bcount) &&
           (AtaReq1->lba+AtaReq1->bcount > AtaReq2->lba)) {
            // Intersection...
            return REORDER_COST_INTERSECT;
        }
    }
    if(cost < 0) {
        cost *= (1-LunExt->SeekBackMCost);
    } else {
        cost *= (LunExt->SeekBackMCost-1);
    }
    if( write1 == write2 ) {
        return cost;
    }
    return (cost * LunExt->RwSwitchMCost) + LunExt->RwSwitchCost;
} // end AtapiGetCost()

/*
    Insert command to proper place of command queue
    Perform reorder if necessary
 */
VOID
AtapiQueueRequest(
    IN PHW_CHANNEL chan,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);
    PATA_REQ AtaReq1;
    PATA_REQ AtaReq2;

    LONGLONG best_cost;
    LONGLONG new_cost;
    LONGLONG new_cost1;
    LONGLONG new_cost2;
    PATA_REQ BestAtaReq1;

    BOOLEAN use_reorder = TRUE/*EnableReorder*/;
#ifdef QUEUE_STATISTICS
    BOOLEAN reordered = FALSE;
#endif //QUEUE_STATISTICS

    PHW_LU_EXTENSION LunExt = chan->lun[GET_LDEV(Srb) & 1];
    AtaReq->Srb = Srb;

    // check if queue is empty
    if(LunExt->queue_depth) {
        AtaReq->ttl = (UCHAR)(max(LunExt->queue_depth, MIN_REQ_TTL));

        // init loop
        BestAtaReq1 =
        AtaReq2 = LunExt->last_req;

        if(use_reorder &&
           (Srb->SrbFlags & SRB_FLAGS_QUEUE_ACTION_ENABLE)) {
            switch(Srb->QueueAction) {
            case SRB_ORDERED_QUEUE_TAG_REQUEST:
                use_reorder = FALSE;
#ifdef QUEUE_STATISTICS
                chan->TryReorderTailCount++;
#endif //QUEUE_STATISTICS
                break;
            case SRB_HEAD_OF_QUEUE_TAG_REQUEST:
                BestAtaReq1 = LunExt->first_req;
                best_cost = -REORDER_COST_RESELECT;
#ifdef QUEUE_STATISTICS
                chan->TryReorderHeadCount++;
#endif //QUEUE_STATISTICS
                break;
            case SRB_SIMPLE_TAG_REQUEST:
            default:
                best_cost = AtapiGetCost(LunExt, BestAtaReq1, AtaReq);
                use_reorder |= TRUE;
            }
        } else
        if(use_reorder) {
            best_cost = AtapiGetCost(LunExt, BestAtaReq1, AtaReq);
        }

        if(use_reorder) {

#ifdef QUEUE_STATISTICS
            chan->TryReorderCount++;
#endif //QUEUE_STATISTICS

            // walk through comamnd queue and find the best
            // place for insertion of ne command
            while (AtaReq1 = AtaReq2->prev_req) {
                new_cost1 = AtapiGetCost(LunExt, AtaReq1, AtaReq);
                new_cost2 = AtapiGetCost(LunExt, AtaReq, AtaReq2);

                if(new_cost1 == REORDER_COST_INTERSECT ||
                   new_cost2 == REORDER_COST_INTERSECT)
                    chan->IntersectCount++;

                if(new_cost2 > REORDER_COST_RESELECT)
                    break;

                ASSERT(new_cost1 <= REORDER_COST_RESELECT);

                new_cost = AtapiGetCost(LunExt, AtaReq1, AtaReq2);
                new_cost = new_cost1 + new_cost2 - new_cost;

                // check for Stop Reordering
                if(new_cost > REORDER_COST_RESELECT*3)
                    break;

                if(new_cost < best_cost) {
                    best_cost = new_cost;
                    BestAtaReq1 = AtaReq1;
#ifdef QUEUE_STATISTICS
                    reordered = TRUE;
#endif //QUEUE_STATISTICS
                }
                AtaReq2 = AtaReq1;
            }
#ifdef QUEUE_STATISTICS
            if(reordered)
                chan->ReorderCount++;
#endif //QUEUE_STATISTICS
        }
        // Insert Req between Req2 & Req1
        AtaReq2 = BestAtaReq1->next_req;
        if(AtaReq2) {
            AtaReq2->prev_req = AtaReq;
            AtaReq->next_req = AtaReq2;
        } else {
            AtaReq->next_req = NULL;
            LunExt->last_req = AtaReq;
        }
//        LunExt->last_req->next_req = AtaReq;
        BestAtaReq1->next_req = AtaReq;
//        AtaReq->prev_req = LunExt->last_req;
        AtaReq->prev_req = BestAtaReq1;

        AtaReq1 = AtaReq;
        while(AtaReq1 = AtaReq1->next_req) {
            ASSERT(AtaReq1->ttl);
            AtaReq1->ttl--;
        }

    } else {
        // empty queue
        AtaReq->ttl = 0;
        AtaReq->prev_req =
        AtaReq->next_req = NULL;
        LunExt->first_req =
        LunExt->last_req = AtaReq;
    }
    LunExt->queue_depth++;
    chan->queue_depth++;
    // check if this is the 1st request in queue
    if(chan->queue_depth == 1) {
        chan->cur_req = LunExt->first_req;
    }

#ifdef QUEUE_STATISTICS
    ASSERT(LunExt->queue_depth);
    chan->QueueStat[min(MAX_QUEUE_STAT, LunExt->queue_depth)-1]++;
#endif //QUEUE_STATISTICS

    ASSERT(chan->queue_depth ==
            (chan->lun[0]->queue_depth + chan->lun[1]->queue_depth));
    ASSERT(!chan->queue_depth || chan->cur_req);

    return;
} // end AtapiQueueRequest()

/*
    Remove request from queue and get next request
 */
VOID
AtapiRemoveRequest(
    IN PHW_CHANNEL chan,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    if(!Srb)
        return;

    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);

    ULONG cdev = GET_LDEV(Srb) & 1;
    PHW_LU_EXTENSION LunExt = chan->lun[cdev];

    ASSERT(chan->queue_depth ==
            (chan->lun[0]->queue_depth + chan->lun[1]->queue_depth));
    ASSERT(!chan->queue_depth || chan->cur_req);

    // check if queue for the device is empty
    if(!LunExt->queue_depth)
        return;

    // check if request is under processing (busy)
    if(!AtaReq->ReqState)
        return;

    // remove reqest from queue
    if(AtaReq->prev_req) {
        AtaReq->prev_req->next_req =
            AtaReq->next_req;
    } else {
        LunExt->first_req = AtaReq->next_req;
    }
    if(AtaReq->next_req) {
        AtaReq->next_req->prev_req =
            AtaReq->prev_req;
    } else {
        LunExt->last_req = AtaReq->prev_req;
    }
    LunExt->queue_depth--;
    chan->queue_depth--;
    // set LastWrite flag for Lun
    LunExt->last_write = ((AtaReq->Flags & REQ_FLAG_RW_MASK) == REQ_FLAG_WRITE);

    // get request from longest queue to balance load
    if(chan->lun[0]->queue_depth > chan->lun[1]->queue_depth) {
        cdev = 0;
    } else {
        cdev = 1;
    }
    // get next request for processing
    chan->cur_req = chan->lun[cdev]->first_req;
    chan->cur_cdev = cdev;

//    chan->first_req->prev_req = NULL;
    AtaReq->ReqState = REQ_STATE_NONE;

    ASSERT(chan->queue_depth ==
            (chan->lun[0]->queue_depth + chan->lun[1]->queue_depth));
    ASSERT(!chan->queue_depth || chan->cur_req);

    return;
} // end AtapiRemoveRequest()

/*
    Get currently processed request
    (from head of the queue)
 */
PSCSI_REQUEST_BLOCK
AtapiGetCurRequest(
    IN PHW_CHANNEL chan
    )
{
//    if(!chan->lun[]->queue_depth) {
    if(!chan->cur_req) {
        return NULL;
    }

    return chan->cur_req->Srb;
} // end AtapiGetCurRequest()

/*VOID
AtapiDpcDispatch(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    )
{
    ULONG lChannel = ((ULONG)DeferredContext) & 1;
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)((ULONG)DeferredContext & ~1);

    PHW_TIMER HwScsiTimer = (PHW_TIMER)(deviceExtension->chan[lChannel].HwScsiTimer);

    KdPrint2((PRINT_PREFIX "AtapiDpcDispatch: HwScsiTimer = %#x, deviceExtension = %#x\n",
              HwScsiTimer, deviceExtension));

    HwScsiTimer(deviceExtension);
}*/

VOID
AtapiTimerDpc(
    IN PVOID HwDeviceExtension
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_TIMER HwScsiTimer;
    LARGE_INTEGER time;
    ULONG MiniportTimerValue;
    BOOLEAN recall = FALSE;

GetNextDpc:

    HwScsiTimer = deviceExtension->HwScsiTimer1;
    deviceExtension->ActiveDpcChan = deviceExtension->FirstDpcChan;

    deviceExtension->HwScsiTimer1 = deviceExtension->HwScsiTimer2;
    deviceExtension->DpcTime1 = deviceExtension->DpcTime2;
    deviceExtension->HwScsiTimer2 = NULL;
    deviceExtension->DpcTime2 = 0;
    if(deviceExtension->HwScsiTimer1) {
        recall = TRUE;
        deviceExtension->ActiveDpcChan = deviceExtension->ActiveDpcChan ^ 1;
        KdPrint2((PRINT_PREFIX "AtapiTimerDpc: change default channel to %#x\n",
                  deviceExtension->ActiveDpcChan));
    } else {
        deviceExtension->FirstDpcChan =
        deviceExtension->ActiveDpcChan = -1;
    }

    HwScsiTimer(HwDeviceExtension);

    KeQuerySystemTime(&time);
    KdPrint2((PRINT_PREFIX "AtapiTimerDpc: KeQuerySystemTime=%#x%#x\n", time.HighPart, time.LowPart));

    if(!deviceExtension->HwScsiTimer1) {
        deviceExtension->FirstDpcChan =
        deviceExtension->ActiveDpcChan = -1;
    } else
    if(time.QuadPart >= deviceExtension->DpcTime1) {
        // call now
        KdPrint2((PRINT_PREFIX "AtapiTimerDpc: get next DPC, DpcTime1=%#x%#x\n",
                     (ULONG)(deviceExtension->DpcTime1 >> 32), (ULONG)(deviceExtension->DpcTime1)));
        goto GetNextDpc;
    } else
    if(recall) {
        deviceExtension->ActiveDpcChan = -1;
        MiniportTimerValue = (ULONG)(time.QuadPart - deviceExtension->DpcTime1)/10;
        if(!MiniportTimerValue)
            MiniportTimerValue = 1;
        KdPrint2((PRINT_PREFIX "AtapiTimerDpc: recall AtapiQueueTimerDpc\n"));
        ScsiPortNotification(RequestTimerCall, HwDeviceExtension,
    //                         HwScsiTimer,
                             AtapiTimerDpc,
    //                         AtapiQueueTimerDpc,
                             MiniportTimerValue
                             );
    }

} // end AtapiTimerDpc()

/*
    Wrapper for ScsiPort, that implements smart Dpc
    queueing. We need it to allow parallel functioning
    of IDE channles with shared interrupt. Standard Dpc mechanism
    cancels previous Dpc request (if any), but we need Dpc queue.
*/
VOID
AtapiQueueTimerDpc(
    IN PVOID HwDeviceExtension,
    IN ULONG lChannel,
    IN PHW_TIMER HwScsiTimer,
    IN ULONG MiniportTimerValue
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    LARGE_INTEGER time;
//    BOOLEAN UseRequestTimerCall = TRUE;

    KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: dt=%d for Chn %#x\n", MiniportTimerValue, lChannel));
    KeQuerySystemTime(&time);
    KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: KeQuerySystemTime=%#x%#x\n", time.HighPart, time.LowPart));
    time.QuadPart += MiniportTimerValue*10;
    KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: KeQuerySystemTime2=%#x%#x\n", time.HighPart, time.LowPart));

    if(deviceExtension->ActiveDpcChan != -1) {
        if(lChannel != deviceExtension->ActiveDpcChan) {
            KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: next DPC for another Chn %#x\n", lChannel));
//            deviceExtension->FirstDpcChan = lChannel;
        } else {
/*            deviceExtension->DpcTime1 = time.QuadPart;
            deviceExtension->HwScsiTimer1 = HwScsiTimer;*/
            KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: next DPC while current is active for Chn %#x\n", lChannel));
        }
NextDpcAnotherChn:
        deviceExtension->HwScsiTimer2 = HwScsiTimer;
        deviceExtension->DpcTime2 = time.QuadPart;
        return;
    } else
    if(deviceExtension->FirstDpcChan != -1) {
        if(lChannel != deviceExtension->FirstDpcChan) {
            if(time.QuadPart < deviceExtension->DpcTime1) {
                deviceExtension->FirstDpcChan = lChannel;
                deviceExtension->HwScsiTimer2 = deviceExtension->HwScsiTimer1;
                deviceExtension->DpcTime2 = deviceExtension->DpcTime1;
                deviceExtension->HwScsiTimer1 = HwScsiTimer;
                deviceExtension->DpcTime1 = time.QuadPart;
            } else {
                goto NextDpcAnotherChn;
            }
        } else {
            if(time.QuadPart < deviceExtension->DpcTime2 ||
               !deviceExtension->HwScsiTimer2) {
                deviceExtension->HwScsiTimer1 = HwScsiTimer;
                deviceExtension->DpcTime1 = time.QuadPart;
            } else {
                deviceExtension->FirstDpcChan = lChannel ^ 1;
                deviceExtension->HwScsiTimer1 = deviceExtension->HwScsiTimer2;
                deviceExtension->DpcTime1 = deviceExtension->DpcTime2;
                deviceExtension->HwScsiTimer2 = HwScsiTimer;
                deviceExtension->DpcTime2 = time.QuadPart;
            }
        }
    } else {
        deviceExtension->FirstDpcChan = lChannel;
        deviceExtension->HwScsiTimer1 = HwScsiTimer;
        deviceExtension->DpcTime1 = time.QuadPart;
    }

    KeQuerySystemTime(&time);
    KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: KeQuerySystemTime3=%#x%#x\n", time.HighPart, time.LowPart));
    if(deviceExtension->DpcTime1 <= time.QuadPart) {
        MiniportTimerValue = 1;
    } else {
        MiniportTimerValue = (ULONG)((deviceExtension->DpcTime1 - time.QuadPart) / 10);
    }

    KdPrint2((PRINT_PREFIX "AtapiQueueTimerDpc: dt=%d for Chn %#x\n", MiniportTimerValue, lChannel));
    ScsiPortNotification(RequestTimerCall, HwDeviceExtension,
                         AtapiTimerDpc,
                         MiniportTimerValue);

} // end AtapiQueueTimerDpc()


/*++

Routine Description:

    Issue IDENTIFY command to a device.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    DeviceNumber - Indicates which device.
    Command - Either the standard (EC) or the ATAPI packet (A1) IDENTIFY.

Return Value:

    TRUE if all goes well.

--*/
BOOLEAN
IssueIdentify(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,
    IN UCHAR Command
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL          chan = &deviceExtension->chan[lChannel];
    PIDE_REGISTERS_1     baseIoAddress1 = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2 = deviceExtension->BaseIoAddress2[lChannel];
    ULONG                waitCount = 50000;
    ULONG                j;
    UCHAR                statusByte;
    UCHAR                statusByte2;
    UCHAR                signatureLow,
                         signatureHigh;
    ULONG                ldev = (lChannel * 2) + DeviceNumber;
    PHW_LU_EXTENSION     LunExt = &(deviceExtension->lun[ldev]);

    SelectDrive(chan, baseIoAddress1, DeviceNumber);
    ScsiPortStallExecution(10);
    statusByte = WaitOnBusyLong(chan, baseIoAddress2);
    // Check that the status register makes sense.
    GetBaseStatus(chan, baseIoAddress1, statusByte2);

    if (Command == IDE_COMMAND_IDENTIFY) {
        // Mask status byte ERROR bits.
        statusByte &= ~(IDE_STATUS_ERROR | IDE_STATUS_INDEX);
        KdPrint2((PRINT_PREFIX "IssueIdentify: Checking for IDE. Status (%#x)\n", statusByte));
        // Check if register value is reasonable.

        if (statusByte != IDE_STATUS_IDLE) {
            // Reset the controller.
            KdPrint2((PRINT_PREFIX "IssueIdentify: statusByte != IDE_STATUS_IDLE\n"));
            //KdPrint2((PRINT_PREFIX "IssueIdentify: Reset ATAPI\n"));
            //AtapiSoftReset(chan,baseIoAddress1,DeviceNumber);

            SelectDrive(chan, baseIoAddress1, DeviceNumber);
            WaitOnBusyLong(chan, baseIoAddress2);

            signatureLow = AtapiReadPort1(chan,&baseIoAddress1->i.CylinderLow);
            signatureHigh = AtapiReadPort1(chan,&baseIoAddress1->i.CylinderHigh);

            if (signatureLow == ATAPI_MAGIC_LSB &&
                signatureHigh == ATAPI_MAGIC_MSB) {
                // Device is Atapi.
                KdPrint2((PRINT_PREFIX "IssueIdentify: this is ATAPI (ldev %d)\n", ldev));
                return FALSE;
            }

            // We really should wait up to 31 seconds
            // The ATA spec. allows device 0 to come back from BUSY in 31 seconds!
            // (30 seconds for device 1)
            do {
                // Wait for Busy to drop.
                ScsiPortStallExecution(100);
                GetStatus(chan, baseIoAddress2, statusByte);

            } while ((statusByte & IDE_STATUS_BUSY) && waitCount--);
            GetBaseStatus(chan, baseIoAddress1, statusByte2);

            SelectDrive(chan, baseIoAddress1, DeviceNumber);
            // Another check for signature, to deal with one model Atapi that doesn't assert signature after
            // a soft reset.
            signatureLow = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderLow);
            signatureHigh = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderHigh);

            if (signatureLow == ATAPI_MAGIC_LSB &&
                signatureHigh == ATAPI_MAGIC_MSB) {
                KdPrint2((PRINT_PREFIX "IssueIdentify: this is ATAPI (2) (ldev %d)\n", ldev));
                // Device is Atapi.
                return FALSE;
            }

            statusByte &= ~IDE_STATUS_INDEX;
            if (statusByte != IDE_STATUS_IDLE) {
                // Give up on this.
                KdPrint2((PRINT_PREFIX "IssueIdentify: no dev (ldev %d)\n", ldev));
                return FALSE;
            }
        }
    } else {
        KdPrint2((PRINT_PREFIX "IssueIdentify: Checking for ATAPI. Status (%#x)\n", statusByte));
        statusByte = WaitForIdleLong(chan, baseIoAddress2);
        KdPrint2((PRINT_PREFIX "IssueIdentify: Checking for ATAPI (2). Status (%#x)\n", statusByte));
    }

    for (j = 0; j < 4*2; j++) {
        // Send IDENTIFY command.
        statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel, Command, 0, 0, 0, (j >= 4) ? 0x200 : 0, 0, ATA_WAIT_INTR);
        // Clear interrupt

        if (statusByte & IDE_STATUS_DRQ) {
            // Read status to acknowledge any interrupts generated.
            KdPrint2((PRINT_PREFIX "IssueIdentify: IDE_STATUS_DRQ (%#x)\n", statusByte));
            GetBaseStatus(chan, baseIoAddress1, statusByte);
            // One last check for Atapi.
            signatureLow = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderLow);
            signatureHigh = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderHigh);

            if (signatureLow == ATAPI_MAGIC_LSB &&
                signatureHigh == ATAPI_MAGIC_MSB) {
                KdPrint2((PRINT_PREFIX "IssueIdentify: this is ATAPI (3) (ldev %d)\n", ldev));
                // Device is Atapi.
                return FALSE;
            }
            break;
        } else {
            KdPrint2((PRINT_PREFIX "IssueIdentify: !IDE_STATUS_DRQ (%#x)\n", statusByte));
            if (Command == IDE_COMMAND_IDENTIFY) {
                // Check the signature. If DRQ didn't come up it's likely Atapi.
                signatureLow = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderLow);
                signatureHigh = AtapiReadPort1(chan, &baseIoAddress1->i.CylinderHigh);

                if (signatureLow == ATAPI_MAGIC_LSB &&
                    signatureHigh == ATAPI_MAGIC_MSB) {
                    // Device is Atapi.
                    KdPrint2((PRINT_PREFIX "IssueIdentify: this is ATAPI (4) (ldev %d)\n", ldev));
                    return FALSE;
                }
            }
            // Device didn't respond correctly. It will be given one more chances.
            KdPrint2((PRINT_PREFIX "IssueIdentify: DRQ never asserted (%#x). Error reg (%#x)\n",
                        statusByte, AtapiReadPort1(chan, &baseIoAddress1->i.Error)));
            GetBaseStatus(chan, baseIoAddress1, statusByte);
            AtapiSoftReset(chan, baseIoAddress1,DeviceNumber);

            GetBaseStatus(chan, baseIoAddress1, statusByte);
            //GetStatus(chan, baseIoAddress2,statusByte);
            KdPrint2((PRINT_PREFIX "IssueIdentify: Status after soft reset (%#x)\n", statusByte));
        }
    }
    // Check for error on really stupid master devices that assert random
    // patterns of bits in the status register at the slave address.
    if ((Command == IDE_COMMAND_IDENTIFY) && (statusByte & IDE_STATUS_ERROR)) {
        KdPrint2((PRINT_PREFIX "IssueIdentify: Exit on error (%#x)\n", statusByte));
        return FALSE;
    }

    KdPrint2((PRINT_PREFIX "IssueIdentify: Status before read words %#x\n", statusByte));
    // Suck out 256 words. After waiting for one model that asserts busy
    // after receiving the Packet Identify command.
    statusByte = WaitForDrq(chan, baseIoAddress2);
    statusByte = WaitOnBusyLong(chan, baseIoAddress2);

    if (!(statusByte & IDE_STATUS_DRQ)) {
        KdPrint2((PRINT_PREFIX "IssueIdentify: !IDE_STATUS_DRQ (2) (%#x)\n", statusByte));
        GetBaseStatus(chan, baseIoAddress1, statusByte);
        return FALSE;
    }
    GetBaseStatus(chan, baseIoAddress1, statusByte);

    ReadBuffer(chan, baseIoAddress1, (PUSHORT)&deviceExtension->FullIdentifyData, 256);
    // Work around for some IDE and one model Atapi that will present more than
    // 256 bytes for the Identify data.
    statusByte = AtapiSuckPort2(chan, baseIoAddress1, baseIoAddress2);

    statusByte = WaitForDrq(chan, baseIoAddress2);
    GetBaseStatus(chan, baseIoAddress1, statusByte);

    KdPrint2((PRINT_PREFIX "IssueIdentify: Status after read words %#x\n", statusByte));

    // Check out a few capabilities / limitations of the device.
    if (deviceExtension->FullIdentifyData.RevovableStatus & 1) {
        // Determine if this drive supports the MSN functions.
        KdPrint2((PRINT_PREFIX "IssueIdentify: Marking drive %d as removable. SFE = %d\n",
                    ldev,
                    deviceExtension->FullIdentifyData.RevovableStatus));
        LunExt->DeviceFlags |= DFLAGS_REMOVABLE_DRIVE;
    }
    if (deviceExtension->FullIdentifyData.MaximumBlockTransfer) {
        // Determine max. block transfer for this device.
        LunExt->MaximumBlockXfer =
            (UCHAR)(deviceExtension->FullIdentifyData.MaximumBlockTransfer & 0xFF);
    }
    LunExt->NumOfSectors = 0;
    if (Command == IDE_COMMAND_IDENTIFY) {
        ULONGLONG NumOfSectors=0;
        ULONGLONG NativeNumOfSectors=0;
        ULONGLONG cylinders=0;
        // Read very-old-style drive geometry
        NumOfSectors = deviceExtension->FullIdentifyData.NumberOfCylinders *
                       deviceExtension->FullIdentifyData.NumberOfHeads *
                       deviceExtension->FullIdentifyData.SectorsPerTrack;
        // Check for HDDs > 8Gb
        if ((deviceExtension->FullIdentifyData.NumberOfCylinders == 0x3fff) &&
/*            (deviceExtension->FullIdentifyData.TranslationFieldsValid) &&*/
            (NumOfSectors < deviceExtension->FullIdentifyData.UserAddressableSectors)) {
            cylinders = 
                (deviceExtension->FullIdentifyData.UserAddressableSectors /
                    (deviceExtension->FullIdentifyData.NumberOfHeads *
                       deviceExtension->FullIdentifyData.SectorsPerTrack));


            NumOfSectors = cylinders *
                           deviceExtension->FullIdentifyData.NumberOfHeads *
                           deviceExtension->FullIdentifyData.SectorsPerTrack;
        }
        // Check for LBA mode
        if ( deviceExtension->FullIdentifyData.SupportLba
                            ||
            (deviceExtension->FullIdentifyData.MajorRevision &&
/*             deviceExtension->FullIdentifyData.TranslationFieldsValid &&*/
             deviceExtension->FullIdentifyData.UserAddressableSectors)) {
            LunExt->DeviceFlags |= DFLAGS_LBA_ENABLED;
        } else {
            LunExt->DeviceFlags |= DFLAGS_ORIG_GEOMETRY;
            goto skip_lba_staff;
        }
        // Check for LBA48 support
        if(deviceExtension->FullIdentifyData.FeaturesSupport.Address48 &&
           deviceExtension->FullIdentifyData.FeaturesEnabled.Address48 &&
           (deviceExtension->FullIdentifyData.UserAddressableSectors48 > NumOfSectors)
           ) {
//            BrutePoint();
            cylinders = 
                (deviceExtension->FullIdentifyData.UserAddressableSectors48 /
                    (deviceExtension->FullIdentifyData.NumberOfHeads *
                       deviceExtension->FullIdentifyData.SectorsPerTrack));
            NativeNumOfSectors = cylinders *
                           deviceExtension->FullIdentifyData.NumberOfHeads *
                           deviceExtension->FullIdentifyData.SectorsPerTrack;
            if(NativeNumOfSectors > NumOfSectors)
                NumOfSectors = NativeNumOfSectors;
        }
        if(NumOfSectors < 0x2100000 /*&& NumOfSectors > 31*1000*1000*/) {
            // check for native LBA size
            // some drives report ~32Gb in Identify Block
            AtaCommand(deviceExtension, DeviceNumber, lChannel, IDE_COMMAND_READ_NATIVE_SIZE,
                         0, IDE_USE_LBA, 0, 0, 0, ATA_WAIT_READY);

            NativeNumOfSectors = (ULONG)AtapiReadPort1(chan, &baseIoAddress1->i.BlockNumber) |
                                ((ULONG)AtapiReadPort1(chan, &baseIoAddress1->i.CylinderLow) << 8) |
                                ((ULONG)AtapiReadPort1(chan, &baseIoAddress1->i.CylinderHigh) << 16) |
                               (((ULONG)AtapiReadPort1(chan, &baseIoAddress1->i.DriveSelect) & 0xf) << 24);

            if(NativeNumOfSectors > NumOfSectors) {
                statusByte = AtaCommand48(deviceExtension, DeviceNumber, lChannel,
                             IDE_COMMAND_SET_NATIVE_SIZE, NativeNumOfSectors, 0, 0, ATA_WAIT_READY);
                if(!(statusByte & IDE_STATUS_ERROR)) {
                    NumOfSectors = NativeNumOfSectors;
                }
            }
        }
        // fill IdentifyData with bogus geometry
        while ((cylinders > 0xffff) && (deviceExtension->FullIdentifyData.SectorsPerTrack < 0x80)) {
            cylinders /= 2;
            deviceExtension->FullIdentifyData.SectorsPerTrack *= 2;
            deviceExtension->FullIdentifyData.CurrentSectorsPerTrack *= 2;
        }
        while ((cylinders > 0xffff) && (deviceExtension->FullIdentifyData.NumberOfHeads < 0x80)) {
            cylinders /= 2;
            deviceExtension->FullIdentifyData.NumberOfHeads *= 2;
            deviceExtension->FullIdentifyData.NumberOfCurrentHeads *= 2;
        }
        while ((cylinders > 0xffff) && (deviceExtension->FullIdentifyData.SectorsPerTrack < 0x8000)) {
            cylinders /= 2;
            deviceExtension->FullIdentifyData.SectorsPerTrack *= 2;
            deviceExtension->FullIdentifyData.CurrentSectorsPerTrack *= 2;
        }
        while ((cylinders > 0xffff) && (deviceExtension->FullIdentifyData.NumberOfHeads < 0x8000)) {
            cylinders /= 2;
            deviceExtension->FullIdentifyData.NumberOfHeads *= 2;
            deviceExtension->FullIdentifyData.NumberOfCurrentHeads *= 2;
        }
        deviceExtension->FullIdentifyData.NumberOfCurrentCylinders =
        deviceExtension->FullIdentifyData.NumberOfCylinders = (USHORT)cylinders;

skip_lba_staff:

        if(NumOfSectors)
            LunExt->NumOfSectors = NumOfSectors;
/*        if(deviceExtension->FullIdentifyData.MajorRevision &&
           deviceExtension->FullIdentifyData.DoubleWordIo) {
            LunExt->DeviceFlags |= DFLAGS_DWORDIO_ENABLED;
        }*/
    }

    ScsiPortMoveMemory(&LunExt->IdentifyData,
                       &deviceExtension->FullIdentifyData,sizeof(IDENTIFY_DATA2));

    if ((LunExt->IdentifyData.DrqType & ATAPI_DRQT_INTR) &&
        (Command != IDE_COMMAND_IDENTIFY)) {

        // This device interrupts with the assertion of DRQ after receiving
        // Atapi Packet Command
        LunExt->DeviceFlags |= DFLAGS_INT_DRQ;
        KdPrint2((PRINT_PREFIX "IssueIdentify: Device interrupts on assertion of DRQ.\n"));

    } else {
        KdPrint2((PRINT_PREFIX "IssueIdentify: Device does not interrupt on assertion of DRQ.\n"));
    }

    if(Command != IDE_COMMAND_IDENTIFY) {
        if(LunExt->IdentifyData.DeviceType == ATAPI_TYPE_TAPE) {
            // This is a tape.
            LunExt->DeviceFlags |= DFLAGS_TAPE_DEVICE;
            KdPrint2((PRINT_PREFIX "IssueIdentify: Device is a tape drive.\n"));
        } else
        if(LunExt->IdentifyData.DeviceType == ATAPI_TYPE_CDROM ||
            LunExt->IdentifyData.DeviceType == ATAPI_TYPE_OPTICAL) {
            KdPrint2((PRINT_PREFIX "IssueIdentify: Device is CD/Optical drive.\n"));
            // set CD default costs
            LunExt->RwSwitchCost  = REORDER_COST_SWITCH_RW_CD;
            LunExt->RwSwitchMCost = REORDER_MCOST_SWITCH_RW_CD;
            LunExt->SeekBackMCost = REORDER_MCOST_SEEK_BACK_CD;
            statusByte = WaitForDrq(chan, baseIoAddress2);
        } else {
            KdPrint2((PRINT_PREFIX "IssueIdentify: ATAPI drive type %#x.\n",
                LunExt->IdentifyData.DeviceType));
        }
    } else {
        KdPrint2((PRINT_PREFIX "IssueIdentify: hard drive.\n"));
    }

    GetBaseStatus(chan, baseIoAddress1, statusByte);
    KdPrint2((PRINT_PREFIX "IssueIdentify: final Status on exit (%#x)\n", statusByte));
    return TRUE;

} // end IssueIdentify()


/*++

Routine Description:
    Set drive parameters using the IDENTIFY data.

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage
    DeviceNumber - Indicates which device.

Return Value:
    TRUE if all goes well.

--*/
BOOLEAN
SetDriveParameters(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PIDE_REGISTERS_1     baseIoAddress1 = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2 = deviceExtension->BaseIoAddress2[lChannel];
    PIDENTIFY_DATA2      identifyData   = &deviceExtension->lun[(lChannel * 2) + DeviceNumber].IdentifyData;
//    ULONG i;
    UCHAR statusByte;
    UCHAR errorByte;

    if(deviceExtension->lun[(lChannel * 2) + DeviceNumber].DeviceFlags &
          (DFLAGS_LBA_ENABLED | DFLAGS_ORIG_GEOMETRY))
       return TRUE;

    KdPrint2((PRINT_PREFIX "SetDriveParameters: Number of heads %#x\n", identifyData->NumberOfHeads));
    KdPrint2((PRINT_PREFIX "SetDriveParameters: Sectors per track %#x\n", identifyData->SectorsPerTrack));

    // Send SET PARAMETER command.
    statusByte = AtaCommand(deviceExtension, DeviceNumber, lChannel,
                            IDE_COMMAND_SET_DRIVE_PARAMETERS, 0,
                            (identifyData->NumberOfHeads - 1), 0,
                            (UCHAR)identifyData->SectorsPerTrack, 0, ATA_WAIT_IDLE);
    

    if (statusByte & IDE_STATUS_ERROR) {
        errorByte = AtapiReadPort1(&deviceExtension->chan[lChannel], &baseIoAddress1->i.Error);
        KdPrint2((PRINT_PREFIX "SetDriveParameters: Error bit set. Status %#x, error %#x\n",
                    errorByte, statusByte));
        return FALSE;
    }

    if ((statusByte & ~IDE_STATUS_INDEX ) == IDE_STATUS_IDLE) {
        return TRUE;
    }

    return FALSE;

} // end SetDriveParameters()


/*++

Routine Description:
    Reset IDE controller and/or Atapi device.

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage

Return Value:
    Nothing.


--*/
BOOLEAN
AtapiResetController(
    IN PVOID HwDeviceExtension,
    IN ULONG PathId
    )
{
    return AtapiResetController__(HwDeviceExtension, PathId, TRUE);
} // end AtapiResetController()


BOOLEAN
AtapiResetController__(
    IN PVOID HwDeviceExtension,
    IN ULONG PathId,
    IN BOOLEAN CompleteAll
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG                numberChannels  = deviceExtension->NumberChannels;
    PHW_CHANNEL          chan = NULL;
    PIDE_REGISTERS_1 baseIoAddress1;
    PIDE_REGISTERS_2 baseIoAddress2;
//    BOOLEAN result;
//    BOOLEAN result2 = TRUE;
    ULONG i,j;
    UCHAR statusByte;
    PSCSI_REQUEST_BLOCK CurSrb;

    KdPrint2((PRINT_PREFIX "AtapiResetController: Reset IDE\n"));

    if(!deviceExtension->simplexOnly) {
        // we shall reset both channels on SimplexOnly devices,
        // It's not worth doing so on normal controllers
        j = PathId;
        numberChannels = j+1;
    } else {
        j=0;
        numberChannels = deviceExtension->NumberChannels;
    }

    for (; j < numberChannels; j++) {

        chan = &deviceExtension->chan[j];
        while(CurSrb = AtapiGetCurRequest(chan)) {

            PATA_REQ AtaReq = (PATA_REQ)(CurSrb->SrbExtension);

            // Check and see if we are processing an internal srb
            if (AtaReq->OriginalSrb) {
                AtaReq->Srb = AtaReq->OriginalSrb;
                AtaReq->OriginalSrb = NULL;
            }

            // Remove current request from queue
            AtapiRemoveRequest(chan, CurSrb);

            // Check if request is in progress.
            if (AtaReq->Srb) {
                // Complete outstanding request with SRB_STATUS_BUS_RESET.
                UCHAR PathId   = CurSrb->PathId;
                UCHAR TargetId = CurSrb->TargetId;
                UCHAR Lun = CurSrb->Lun;

                CurSrb->SrbStatus = (CompleteAll ? SRB_STATUS_BUS_RESET : SRB_STATUS_ABORTED) | SRB_STATUS_AUTOSENSE_VALID;
                CurSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

                if (CurSrb->SenseInfoBuffer) {

                    PSENSE_DATA  senseBuffer = (PSENSE_DATA)CurSrb->SenseInfoBuffer;

                    senseBuffer->ErrorCode = 0x70;
                    senseBuffer->Valid     = 1;
                    senseBuffer->AdditionalSenseLength = 0xb;
                    if(CompleteAll) {
                        senseBuffer->SenseKey = SCSI_SENSE_UNIT_ATTENTION;
                        senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_BUS_RESET;
                        senseBuffer->AdditionalSenseCodeQualifier = SCSI_SENSEQ_SCSI_BUS;
                    } else {
                        senseBuffer->SenseKey = SCSI_SENSE_ABORTED_COMMAND;
                        senseBuffer->AdditionalSenseCode = 0;
                        senseBuffer->AdditionalSenseCodeQualifier = 0;
                    }
                }

                ScsiPortNotification(RequestComplete,
                                     deviceExtension,
                                     CurSrb);

                // Clear request tracking fields.
                AtaReq->WordsLeft = 0;
                AtaReq->DataBuffer = NULL;
                // Indicate ready for next request.
    //            ScsiPortNotification(NextRequest, deviceExtension, NULL);
                ScsiPortNotification(NextLuRequest,
                                     deviceExtension, 
                                     PathId,
                                     TargetId,
                                     Lun);
            }
            if(!CompleteAll)
                break;
        }

        
        // Clear expecting interrupt flag.
        chan->ExpectingInterrupt = FALSE;
        chan->RDP = FALSE;
        chan->ChannelCtrlFlags = 0;
        InterlockedExchange(&(chan->CheckIntr),
                                      CHECK_INTR_IDLE);
        
        baseIoAddress1 = deviceExtension->BaseIoAddress1[j];
        baseIoAddress2 = deviceExtension->BaseIoAddress2[j];

        // Reset controller
        AtapiDisableInterrupts(deviceExtension, j);
        AtapiWritePort1(chan, &baseIoAddress2->AlternateStatus,IDE_DC_DISABLE_INTERRUPTS |
                                                                IDE_DC_RESET_CONTROLLER );

        ScsiPortStallExecution(10000);
        // Disable interrupts
        AtapiEnableInterrupts(deviceExtension, j);
        AtapiDisableInterrupts(deviceExtension, j);
        ScsiPortStallExecution(100000);

        // Reset DMA engine if active
        if(chan->ChannelCtrlFlags & CTRFLAGS_DMA_ACTIVE) {
            AtapiDmaDone(HwDeviceExtension, 0, j, NULL);
        }

        // Do special processing for ATAPI and IDE disk devices.
        for (i = 0; i < 2; i++) {

            // Check if device present.
            if (!(deviceExtension->lun[i + (j * 2)].DeviceFlags & DFLAGS_DEVICE_PRESENT)) {
                //if(!CheckDevice(HwDeviceExtension, i, j, FALSE))
                continue;
            }

            // Check for ATAPI disk.
            if (deviceExtension->lun[i + (j * 2)].DeviceFlags & DFLAGS_ATAPI_DEVICE) {
                // Issue soft reset and issue identify.
                GetStatus(chan, baseIoAddress2, statusByte);
                KdPrint2((PRINT_PREFIX "AtapiResetController: Status before Atapi reset (%#x).\n",
                            statusByte));
                AtapiSoftReset(chan, baseIoAddress1, i);

                GetStatus(chan, baseIoAddress2, statusByte);

                if (statusByte == IDE_STATUS_SUCCESS) {

                    IssueIdentify(HwDeviceExtension,
                                  i, j,
                                  IDE_COMMAND_ATAPI_IDENTIFY);
                } else {

                    KdPrint2((PRINT_PREFIX 
                               "AtapiResetController: Status after soft reset %#x\n",
                               statusByte));
                }

            } else {
                // Write IDE reset controller bits.
                SelectDrive(chan, baseIoAddress1, i);
                ScsiPortStallExecution(10);
                statusByte = WaitOnBusyLong(chan, baseIoAddress2);

                if (statusByte != IDE_STATUS_IDLE && statusByte != IDE_STATUS_SUCCESS) {
//                    result2 = FALSE;
                    KdPrint2((PRINT_PREFIX "AtapiResetController: IdeHardReset failed\n"));
                } else
                if(!IssueIdentify(HwDeviceExtension,
                                  i, j,
                                  IDE_COMMAND_IDENTIFY)) {
//                    result2 = FALSE;
                    KdPrint2((PRINT_PREFIX "AtapiResetController: IDE IssueIdentify failed\n"));
                } else
                // Set disk geometry parameters.
                if (!SetDriveParameters(HwDeviceExtension, i, j)) {
                    KdPrint2((PRINT_PREFIX "AtapiResetController: SetDriveParameters failed\n"));
                }
            }
        }

        // Enable interrupts
        ScsiPortStallExecution(10);
        AtapiEnableInterrupts(deviceExtension, j);

        // Call the HwInitialize routine to setup multi-block.
        AtapiHwInitialize__(deviceExtension, j);
    }

    return TRUE;

} // end AtapiResetController()


/*++

Routine Description:
    This routine maps ATAPI and IDE errors to specific SRB statuses.

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:
    SRB status

--*/
ULONG
MapError(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG lChannel = GET_CHANNEL(Srb);
    PHW_CHANNEL chan = &(deviceExtension->chan[lChannel]);
    PIDE_REGISTERS_1     baseIoAddress1  = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2  = deviceExtension->BaseIoAddress2[lChannel];
//    ULONG i;
    UCHAR errorByte;
    UCHAR srbStatus;
    UCHAR scsiStatus;
    ULONG ldev = GET_LDEV(Srb);

    // Read the error register.

    errorByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);
    KdPrint2((PRINT_PREFIX 
               "MapError: Error register is %#x\n",
               errorByte));

    if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE) {

        switch (errorByte >> 4) {
        case SCSI_SENSE_NO_SENSE:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: No sense information\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_RECOVERED_ERROR:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Recovered error\n"));
            scsiStatus = 0;
            srbStatus = SRB_STATUS_SUCCESS;
            break;

        case SCSI_SENSE_NOT_READY:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Device not ready\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_MEDIUM_ERROR:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Media error\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_HARDWARE_ERROR:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Hardware error\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_ILLEGAL_REQUEST:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Illegal request\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_UNIT_ATTENTION:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Unit attention\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_DATA_PROTECT:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Data protect\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_BLANK_CHECK:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Blank check\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        case SCSI_SENSE_ABORTED_COMMAND:
            KdPrint2((PRINT_PREFIX 
                        "Atapi: Command Aborted\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;
            break;

        default:

            KdPrint2((PRINT_PREFIX 
                       "ATAPI: Invalid sense information\n"));
            scsiStatus = 0;
            srbStatus = SRB_STATUS_ERROR;
            break;
        }

    } else {

        scsiStatus = 0;

        // Save errorByte,to be used by SCSIOP_REQUEST_SENSE.
        chan->ReturningMediaStatus = errorByte;

        if (errorByte & IDE_ERROR_MEDIA_CHANGE_REQ) {
            KdPrint2((PRINT_PREFIX 
                       "IDE: Media change\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
                senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_MEDIUM_CHANGED;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

        } else if (errorByte & IDE_ERROR_COMMAND_ABORTED) {
            KdPrint2((PRINT_PREFIX 
                       "IDE: Command abort\n"));
            srbStatus = SRB_STATUS_ABORTED;
            scsiStatus = SCSISTAT_CHECK_CONDITION;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_ABORTED_COMMAND;
                senseBuffer->AdditionalSenseCode = 0;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

            deviceExtension->lun[ldev].ErrorCount++;

        } else if (errorByte & IDE_ERROR_END_OF_MEDIA) {

            KdPrint2((PRINT_PREFIX 
                       "IDE: End of media\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
                senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_MEDIA_STATE;
                senseBuffer->AdditionalSenseCodeQualifier = SCSI_SENSEQ_END_OF_MEDIUM;
                senseBuffer->EndOfMedia = 1;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

            if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED)){
                deviceExtension->lun[ldev].ErrorCount++;
            }

        } else if (errorByte & IDE_ERROR_ILLEGAL_LENGTH) {

            KdPrint2((PRINT_PREFIX 
                       "IDE: Illegal length\n"));
            srbStatus = SRB_STATUS_INVALID_REQUEST;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_ILLEGAL_REQUEST;
                senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_INVALID_VALUE;
                senseBuffer->AdditionalSenseCodeQualifier = SCSI_SENSEQ_PARAM_INVALID_VALUE;
                senseBuffer->IncorrectLength = 1;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

        } else if (errorByte & IDE_ERROR_BAD_BLOCK) {

            KdPrint2((PRINT_PREFIX 
                       "IDE: Bad block\n"));
            srbStatus = SRB_STATUS_ERROR;
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_MEDIUM_ERROR;
                senseBuffer->AdditionalSenseCode = 0;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

        } else if (errorByte & IDE_ERROR_ID_NOT_FOUND) {

            KdPrint2((PRINT_PREFIX 
                       "IDE: Id not found\n"));
            srbStatus = SRB_STATUS_ERROR;
            scsiStatus = SCSISTAT_CHECK_CONDITION;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_MEDIUM_ERROR;
                senseBuffer->AdditionalSenseCode = 0;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

            deviceExtension->lun[ldev].ErrorCount++;

        } else if (errorByte & IDE_ERROR_MEDIA_CHANGE) {

            KdPrint2((PRINT_PREFIX 
                       "IDE: Media change\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;

            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
                senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_MEDIUM_CHANGED;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }

        } else if (errorByte & IDE_ERROR_DATA_ERROR) {

            KdPrint2((PRINT_PREFIX 
                   "IDE: Data error\n"));
            scsiStatus = SCSISTAT_CHECK_CONDITION;
            srbStatus = SRB_STATUS_ERROR;

            if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED)){
                deviceExtension->lun[ldev].ErrorCount++;
            }

            // Build sense buffer
            if (Srb->SenseInfoBuffer) {

                PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->SenseInfoBuffer;

                senseBuffer->ErrorCode = 0x70;
                senseBuffer->Valid     = 1;
                senseBuffer->AdditionalSenseLength = 0xb;
                senseBuffer->SenseKey =  SCSI_SENSE_MEDIUM_ERROR;
                senseBuffer->AdditionalSenseCode = 0;
                senseBuffer->AdditionalSenseCodeQualifier = 0;

                srbStatus |= SRB_STATUS_AUTOSENSE_VALID;
            }
        }

        if (deviceExtension->lun[ldev].ErrorCount >= MAX_ERRORS) {
//            deviceExtension->DWordIO = FALSE;

            deviceExtension->lun[ldev].DeviceFlags &= ~DFLAGS_DWORDIO_ENABLED;
            deviceExtension->lun[ldev].MaximumBlockXfer = 0;
            BrutePoint();

            KdPrint2((PRINT_PREFIX 
                        "MapError: Disabling 32-bit PIO and Multi-sector IOs\n"));

            // Log the error.
            ScsiPortLogError( HwDeviceExtension,
                              Srb,
                              Srb->PathId,
                              Srb->TargetId,
                              Srb->Lun,
                              SP_BAD_FW_WARNING,
                              4);

            // Reprogram to not use Multi-sector.
//            for (i = 0; i < 4; i++) {
            UCHAR statusByte;

            if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT &&
                 !(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

                statusByte = AtaCommand(deviceExtension, ldev & 0x1, lChannel, IDE_COMMAND_SET_MULTIPLE, 0, 0, 0, 0, 0, ATA_WAIT_BASE_READY);

                // Check for errors. Reset the value to 0 (disable MultiBlock) if the
                // command was aborted.
                if (statusByte & IDE_STATUS_ERROR) {

                    // Read the error register.
                    errorByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);

                    KdPrint2((PRINT_PREFIX "MapError: Error setting multiple mode. Status %#x, error byte %#x\n",
                                statusByte,
                                errorByte));

                    // Adjust the devExt. value, if necessary.
                    deviceExtension->lun[ldev].MaximumBlockXfer = 0;
                    BrutePoint();

                }
            }
//            }
        }
    }

    // Set SCSI status to indicate a check condition.
    Srb->ScsiStatus = scsiStatus;

    return srbStatus;

} // end MapError()


/*++

Routine Description:

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage

Return Value:
    TRUE - if initialization successful.
    FALSE - if initialization unsuccessful.

--*/
BOOLEAN
AtapiHwInitialize(
    IN PVOID HwDeviceExtension
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG                numberChannels  = deviceExtension->NumberChannels;
    ULONG c;

    for (c = 0; c < numberChannels; c++) {
        FindDevices(HwDeviceExtension, FALSE, c);
        AtapiHwInitialize__(deviceExtension, c);
    }
    return TRUE;
} // end AtapiHwInitialize()

VOID
AtapiHwInitialize__(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG lChannel
    )
{
    ULONG i;
    UCHAR statusByte, errorByte;
    PHW_CHANNEL chan = &(deviceExtension->chan[lChannel]);
    PIDE_REGISTERS_1     baseIoAddress1;
    PIDE_REGISTERS_2     baseIoAddress2;

    for (i = lChannel*2; i < (lChannel+1)*2; i++) {

        // skip empty slots
        if (!(deviceExtension->lun[i].DeviceFlags & DFLAGS_DEVICE_PRESENT)) {
            //if(!CheckDevice(deviceExtension, lChannel, i, FALSE))
            continue;
        }

        baseIoAddress1 = deviceExtension->BaseIoAddress1[lChannel];
        baseIoAddress2 = deviceExtension->BaseIoAddress2[lChannel];

        AtapiDisableInterrupts(deviceExtension, lChannel);
        ScsiPortStallExecution(1);

        if (!(deviceExtension->lun[i].DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

            // Enable media status notification
            IdeMediaStatus(TRUE,deviceExtension,(UCHAR)i);

            // If supported, setup Multi-block transfers.
            if (deviceExtension->lun[i].MaximumBlockXfer) {

                statusByte = AtaCommand(deviceExtension, i & 1, lChannel,
                                    IDE_COMMAND_SET_MULTIPLE, 0, 0, 0,
                                    deviceExtension->lun[i].MaximumBlockXfer, 0, ATA_WAIT_BASE_READY);

                // Check for errors. Reset the value to 0 (disable MultiBlock) if the
                // command was aborted.
                if (statusByte & IDE_STATUS_ERROR) {

                    // Read the error register.
                    errorByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);

                    KdPrint2((PRINT_PREFIX "AtapiHwInitialize: Error setting multiple mode. Status %#x, error byte %#x\n",
                                statusByte,
                                errorByte));

                    // Adjust the devExt. value, if necessary.
                    deviceExtension->lun[i].MaximumBlockXfer = 0;

                } else {
                    KdPrint2((PRINT_PREFIX 
                                "AtapiHwInitialize: Using Multiblock on Device %d. Blocks / int - %d\n",
                                i,
                                deviceExtension->lun[i].MaximumBlockXfer));
                }
            }

            if(deviceExtension->lun[i].IdentifyData.MajorRevision) {
            
                // If supported, setup read/write cacheing
                statusByte = AtaCommand(deviceExtension, i & 1, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    0, ATA_C_F_ENAB_RCACHE, ATA_WAIT_BASE_READY);

                // Check for errors.
                if (statusByte & IDE_STATUS_ERROR) {
                    KdPrint2((PRINT_PREFIX 
                                "AtapiHwInitialize: Enable read/write cacheing on Device %d failed\n",
                                i));
                    deviceExtension->lun[i].DeviceFlags &= ~DFLAGS_RCACHE_ENABLED;
                } else {
                    deviceExtension->lun[i].DeviceFlags |= DFLAGS_RCACHE_ENABLED;
                }

                // If supported & allowed, setup write cacheing
                statusByte = AtaCommand(deviceExtension, i & 1, lChannel,
                                    IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                    0, ATA_C_F_ENAB_WCACHE, ATA_WAIT_BASE_READY);
                // Check for errors.
                if (statusByte & IDE_STATUS_ERROR) {
                    KdPrint2((PRINT_PREFIX 
                                "AtapiHwInitialize: Enable write cacheing on Device %d failed\n",
                                i));
                    deviceExtension->lun[i].DeviceFlags &= ~DFLAGS_WCACHE_ENABLED;
                } else {
                    deviceExtension->lun[i].DeviceFlags |= DFLAGS_WCACHE_ENABLED;
                }
            }

        } else if (!(deviceExtension->lun[i].DeviceFlags & DFLAGS_CHANGER_INITED)){

            ULONG j;
            BOOLEAN isSanyo = FALSE;
            CCHAR vendorId[26];

            // Attempt to identify any special-case devices - psuedo-atapi changers, atapi changers, etc.
            for (j = 0; j < 13; j += 2) {

                // Build a buffer based on the identify data.
                vendorId[j] = ((PUCHAR)deviceExtension->lun[i].IdentifyData.ModelNumber)[j + 1];
                vendorId[j+1] = ((PUCHAR)deviceExtension->lun[i].IdentifyData.ModelNumber)[j];
            }

            if (!AtapiStringCmp (vendorId, "CD-ROM  CDR", 11)) {

                // Inquiry string for older model had a '-', newer is '_'
                if (vendorId[12] == 'C') {

                    // Torisan changer. Set the bit. This will be used in several places
                    // acting like 1) a multi-lun device and 2) building the 'special' TUR's.
                    deviceExtension->lun[i].DeviceFlags |= (DFLAGS_CHANGER_INITED | DFLAGS_SANYO_ATAPI_CHANGER);
                    deviceExtension->lun[i].DiscsPresent = 3;
                    isSanyo = TRUE;
                }
            }
        }

        AtapiDmaInit__(deviceExtension, i);
        deviceExtension->lun[i].OrigTransferMode =
            deviceExtension->lun[i].TransferMode;

        // We need to get our device ready for action before
        // returning from this function

        // According to the atapi spec 2.5 or 2.6, an atapi device
        // clears its status BSY bit when it is ready for atapi commands.
        // However, some devices (Panasonic SQ-TC500N) are still
        // not ready even when the status BSY is clear.  They don't react
        // to atapi commands.
        //
        // Since there is really no other indication that tells us
        // the drive is really ready for action.  We are going to check BSY
        // is clear and then just wait for an arbitrary amount of time!
        //
        if (deviceExtension->lun[i].DeviceFlags & DFLAGS_ATAPI_DEVICE) {
            ULONG waitCount;

            // have to get out of the loop sometime!
            // 10000 * 100us = 1000,000us = 1000ms = 1s
            waitCount = 10000;
            GetStatus(chan, baseIoAddress2, statusByte);
            while ((statusByte & IDE_STATUS_BUSY) && waitCount) {

                // Wait for Busy to drop.
                ScsiPortStallExecution(100);
                GetStatus(chan, baseIoAddress2, statusByte);
                waitCount--;
            }

            // 5000 * 100us = 500,000us = 500ms = 0.5s
            waitCount = 5000;
            do {
                ScsiPortStallExecution(100);
            } while (waitCount--);
        }
        AtapiEnableInterrupts(deviceExtension, lChannel);
        ScsiPortStallExecution(10);
    }

    return;

} // end AtapiHwInitialize()


VOID
AtapiHwInitializeChanger(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN PMECHANICAL_STATUS_INFORMATION_HEADER MechanismStatus)
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG                ldev = GET_LDEV(Srb);

    if (MechanismStatus) {
        deviceExtension->lun[ldev].DiscsPresent = MechanismStatus->NumberAvailableSlots;
        if (deviceExtension->lun[ldev].DiscsPresent > 1) {
            deviceExtension->lun[ldev].DeviceFlags |= DFLAGS_ATAPI_CHANGER;
        }
    }
    return;
} // end AtapiHwInitializeChanger()


/*++

Routine Description:
    This routine will parse the string for a match on the keyword, then
    calculate the value for the keyword and return it to the caller.

Arguments:
    String - The ASCII string to parse.
    KeyWord - The keyword for the value desired.

Return Values:
    Zero if value not found
    Value converted from ASCII to binary.

--*/
ULONG
AtapiParseArgumentString(
    IN PCHAR String,
    IN PCHAR KeyWord
    )
{
    PCHAR cptr;
    PCHAR kptr;
    ULONG value;
    ULONG stringLength = 0;
    ULONG keyWordLength = 0;
    ULONG index;

    if (!String) {
        return 0;
    }
    if (!KeyWord) {
        return 0;
    }

    // Calculate the string length and lower case all characters.
    cptr = String;
    while (*cptr) {
        if (*cptr >= 'A' && *cptr <= 'Z') {
            *cptr = *cptr + ('a' - 'A');
        }
        cptr++;
        stringLength++;
    }

    // Calculate the keyword length and lower case all characters.
    cptr = KeyWord;
    while (*cptr) {

        if (*cptr >= 'A' && *cptr <= 'Z') {
            *cptr = *cptr + ('a' - 'A');
        }
        cptr++;
        keyWordLength++;
    }

    if (keyWordLength > stringLength) {

        // Can't possibly have a match.
        return 0;
    }

    // Now setup and start the compare.
    cptr = String;

ContinueSearch:

    // The input string may start with white space.  Skip it.
    while (*cptr == ' ' || *cptr == '\t') {
        cptr++;
    }

    if (*cptr == '\0') {
        // end of string.
        return 0;
    }

    kptr = KeyWord;
    while (*cptr++ == *kptr++) {

        if (*(cptr - 1) == '\0') {
            // end of string
            return 0;
        }
    }

    if (*(kptr - 1) == '\0') {

        // May have a match backup and check for blank or equals.
        cptr--;
        while (*cptr == ' ' || *cptr == '\t') {
            cptr++;
        }

        // Found a match.  Make sure there is an equals.
        if (*cptr != '=') {

            // Not a match so move to the next semicolon.
            while (*cptr) {
                if (*cptr++ == ';') {
                    goto ContinueSearch;
                }
            }
            return 0;
        }
        // Skip the equals sign.
        cptr++;

        // Skip white space.
        while ((*cptr == ' ') || (*cptr == '\t')) {
            cptr++;
        }

        if (*cptr == '\0') {
            // Early end of string, return not found
            return 0;
        }

        if (*cptr == ';') {
            // This isn't it either.
            cptr++;
            goto ContinueSearch;
        }

        value = 0;
        if ((*cptr == '0') && (*(cptr + 1) == 'x')) {
            // Value is in Hex.  Skip the "0x"
            cptr += 2;
            for (index = 0; *(cptr + index); index++) {

                if (*(cptr + index) == ' ' ||
                    *(cptr + index) == '\t' ||
                    *(cptr + index) == ';') {
                     break;
                }

                if ((*(cptr + index) >= '0') && (*(cptr + index) <= '9')) {
                    value = (16 * value) + (*(cptr + index) - '0');
                } else {
                    if ((*(cptr + index) >= 'a') && (*(cptr + index) <= 'f')) {
                        value = (16 * value) + (*(cptr + index) - 'a' + 10);
                    } else {
                        // Syntax error, return not found.
                        return 0;
                    }
                }
            }
        } else {

            // Value is in Decimal.
            for (index = 0; *(cptr + index); index++) {

                if (*(cptr + index) == ' ' ||
                    *(cptr + index) == '\t' ||
                    *(cptr + index) == ';') {
                     break;
                }

                if ((*(cptr + index) >= '0') && (*(cptr + index) <= '9')) {
                    value = (10 * value) + (*(cptr + index) - '0');
                } else {

                    // Syntax error return not found.
                    return 0;
                }
            }
        }

        return value;
    } else {

        // Not a match check for ';' to continue search.
        while (*cptr) {
            if (*cptr++ == ';') {
                goto ContinueSearch;
            }
        }

        return 0;
    }
} // end AtapiParseArgumentString()_


VOID
AtapiCallBack__(
    IN PVOID HwDeviceExtension,
    IN UCHAR lChannel
    )
{

    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL chan = &(deviceExtension->chan[lChannel]);

    PSCSI_REQUEST_BLOCK  srb = AtapiGetCurRequest(chan);
    PATAPI_REGISTERS_2   baseIoAddress2;
    UCHAR statusByte;

    KdPrint2((PRINT_PREFIX "AtapiCallBack:\n"));
    // If the last command was DSC restrictive, see if it's set. If so, the device is
    // ready for a new request. Otherwise, reset the timer and come back to here later.

    // If ISR decided to wait for BUSY or DRQ in DPC, we shall also get here.
    // In this case chan->ExpectingInterrupt == TRUE, but interrupts are disabled, thus,
    // we shall have no problem with interrupt handler.
    if (!srb || chan->ExpectingInterrupt) {
        KdPrint2((PRINT_PREFIX "AtapiCallBack: Calling ISR directly due to BUSY\n"));
        if(!AtapiInterrupt__(HwDeviceExtension, lChannel)) {
            InterlockedExchange(&(chan->CheckIntr), CHECK_INTR_IDLE);
            KdPrint2((PRINT_PREFIX "AtapiCallBack: What's fucking this ???!!!\n"));
        }
        goto ReturnCallback;
    }

#if DBG
    if (!IS_RDP((srb->Cdb[0]))) {
        KdPrint2((PRINT_PREFIX "AtapiCallBack: Invalid CDB marked as RDP - %#x\n", srb->Cdb[0]));
    }
#endif
    baseIoAddress2 = (PATAPI_REGISTERS_2)deviceExtension->BaseIoAddress2[GET_CHANNEL(srb)];
    if (!(chan->RDP)) {
        goto ReturnEnableIntr;
    }
    GetStatus(chan, baseIoAddress2, statusByte);
    if (statusByte & IDE_STATUS_DSC) {

        UCHAR PathId   = srb->PathId;
        UCHAR TargetId = srb->TargetId;
        UCHAR Lun      = srb->Lun;

        KdPrint2((PRINT_PREFIX "AtapiCallBack: Found DSC for RDP - %#x\n", srb->Cdb[0]));
        AtapiRemoveRequest(chan, srb);
        ScsiPortNotification(RequestComplete, deviceExtension, srb);
        // Clear current SRB.
        srb = AtapiGetCurRequest(chan);
        chan->RDP = FALSE;

        // Ask for next request.
        ScsiPortNotification(NextLuRequest,
                             deviceExtension, 
                             PathId,
                             TargetId,
                             Lun);
        ScsiPortNotification(NextRequest, deviceExtension, NULL);

        if(srb) {
            AtapiStartIo__(HwDeviceExtension, srb, FALSE);
        }

//        goto ReturnEnableIntr;

    } else {
        KdPrint2((PRINT_PREFIX "AtapiCallBack: Requesting another timer for Op %#x\n",
                    srb->Cdb[0]));

        AtapiQueueTimerDpc(HwDeviceExtension, lChannel,
                             lChannel ? AtapiCallBack_1 : AtapiCallBack_0,
                             1000);
//        return;
        goto ReturnCallback;
    }

ReturnEnableIntr:

    if(InterlockedExchangeAdd(&(chan->DisableIntr), 0)) {
        KdPrint2((PRINT_PREFIX "AtapiCallBack: CallDisableInterrupts\n"));
        chan->ChannelCtrlFlags |= CTRFLAGS_ENABLE_INTR_REQ;
        ScsiPortNotification(CallDisableInterrupts, HwDeviceExtension,
                             AtapiEnableInterrupts__);
    }

ReturnCallback:

    lChannel ^= 1;

    if((ULONG)InterlockedCompareExchange((PVOID*)&(chan->CheckIntr),
                                  (PVOID)CHECK_INTR_ACTIVE,
                                  (PVOID)CHECK_INTR_DETECTED) == CHECK_INTR_DETECTED) {
        if(!AtapiInterrupt__(HwDeviceExtension, lChannel)) {
            InterlockedExchange(&(chan->CheckIntr), CHECK_INTR_IDLE);
        }
    }

    KdPrint2((PRINT_PREFIX "AtapiCallBack: return\n"));
    return;

} // end AtapiCallBack__()

VOID
AtapiCallBack_0(
    IN PVOID HwDeviceExtension
    )
{
    AtapiCallBack__(HwDeviceExtension, 0);
} // end AtapiCallBack_0()

VOID
AtapiCallBack_1(
    IN PVOID HwDeviceExtension
    )
{
    AtapiCallBack__(HwDeviceExtension, 1);
} // end AtapiCallBack_1()


/*++

Routine Description:

    This is the interrupt service routine for ATAPI IDE miniport driver.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage

Return Value:

    TRUE if expecting an interrupt.

--*/
BOOLEAN
AtapiInterrupt(
    IN PVOID HwDeviceExtension
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG c;
    ULONG _c;
    BOOLEAN status = FALSE;
    ULONG c_state;
    ULONG i_res;

    KdPrint2((PRINT_PREFIX "Intr: VendorID+DeviceID/Rev %#x/%#x\n", deviceExtension->DevID, deviceExtension->RevID));

    for(_c=0; _c<deviceExtension->NumberChannels; _c++) {
        c = (_c+deviceExtension->FirstChannelToCheck) % deviceExtension->NumberChannels;
        KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): cntrlr %#x chan %#x\n",deviceExtension->DevIndex, c));

        if(InterlockedExchangeAdd(&(deviceExtension->chan[c].DisableIntr), 0)) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): disabled INTR on ch %d\n", c));
            continue;
        }
        // lock channel. Wait, while 2nd ISR checks interrupt on this channel
        do {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): try lock\n"));
            // c_state = deviceExtension->chan[c].CheckIntr;
            // if (deviceExtension->chan[c].CheckIntr == CHECK_INTR_DETECTED) {
            //     deviceExtension->chan[c].CheckIntr = CHECK_INTR_ACTIVE;
            // }
            c_state = (ULONG)InterlockedCompareExchange((PVOID*)&(deviceExtension->chan[c].CheckIntr),
                                          (PVOID)CHECK_INTR_ACTIVE,
                                          (PVOID)CHECK_INTR_DETECTED);
            if(c_state == CHECK_INTR_IDLE) {
                // c_state = deviceExtension->chan[c].CheckIntr;
                // if (deviceExtension->chan[c].CheckIntr == CHECK_INTR_IDLE) {
                //     deviceExtension->chan[c].CheckIntr = CHECK_INTR_ACTIVE
                // }
                c_state = (ULONG)InterlockedCompareExchange((PVOID*)&(deviceExtension->chan[c].CheckIntr),
                                              (PVOID)CHECK_INTR_ACTIVE,
                                              (PVOID)CHECK_INTR_IDLE);
            }
        } while(c_state == CHECK_INTR_CHECK);
        KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): locked\n"));
        // check if already serviced
        if(c_state == CHECK_INTR_ACTIVE) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): CHECK_INTR_ACTIVE\n"));
            continue;
        }

        if((c_state == CHECK_INTR_DETECTED) ||
           (i_res = AtapiCheckInterrupt__(deviceExtension, (UCHAR)c))) {

            if(i_res == 2) {
                KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): Catch unexpected\n"));
                InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
                return TRUE;
            }
            if(!WinVer_IsNT && BMList[deviceExtension->DevIndex].MasterDev) {
                AtapiDisableInterrupts(deviceExtension, !c);
            }

            if(AtapiInterrupt__(HwDeviceExtension, (UCHAR)c)) {
                deviceExtension->LastInterruptedChannel = (UCHAR)c;
                KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): return status TRUE\n"));
                status = TRUE;
            } else {
                KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): set CHECK_INTR_IDLE\n"));
                InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
            }

            // re-enable interrupts on other channel
            if(!WinVer_IsNT && BMList[deviceExtension->DevIndex].MasterDev) {
                AtapiEnableInterrupts(deviceExtension, !c);
            }

        } else {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): set CHECK_INTR_IDLE (2)\n"));
            InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
        }

    }
    KdPrint2((PRINT_PREFIX "AtapiInterrupt(base): exit with status %#x\n", status));
    if(status) {
        deviceExtension->FirstChannelToCheck++;
        if(deviceExtension->FirstChannelToCheck >= deviceExtension->NumberChannels)
            deviceExtension->FirstChannelToCheck = 0;
    }
    return status;
} // end AtapiInterrupt()

//ULONG i2c = 0;

BOOLEAN
AtapiInterrupt2(
    IN PKINTERRUPT Interrupt,
    IN PVOID Isr2HwDeviceExtension
    )
{

    PISR2_DEVICE_EXTENSION Isr2DeviceExtension = (PISR2_DEVICE_EXTENSION)Isr2HwDeviceExtension;
    PHW_DEVICE_EXTENSION deviceExtension = Isr2DeviceExtension->HwDeviceExtension;
    ULONG c;
    BOOLEAN status = FALSE;
    ULONG c_count = 0;
    ULONG i_res;

//    i2c++;
/*    if(i2c > 0x200) {
        __asm int 3;
    }*/

    if(!BMList[deviceExtension->DevIndex].Isr2Enable)
        return FALSE;

    for(c=0; c<deviceExtension->NumberChannels; c++) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt2: cntrlr %#x chan %#x\n",deviceExtension->DevIndex, c));

        if(InterlockedExchangeAdd(&(deviceExtension->chan[c].DisableIntr), 0)) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt2: disabled INTR\n"));
            continue;
        }

        if((ULONG)InterlockedCompareExchange((PVOID*)&(deviceExtension->chan[c].CheckIntr),
                                      (PVOID)CHECK_INTR_CHECK,
                                      (PVOID)CHECK_INTR_IDLE) != CHECK_INTR_IDLE) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt2: !CHECK_INTR_IDLE\n"));
            // hunt on unexpected intr (Some devices generte double interrupts,
            // some controllers (at least CMD649) interrupt twice with small delay.
            // If interrupts are disabled, they queue interrupt and re-issue it later,
            // when we do not expect it.
            continue;
        }

        c_count++;
        if(i_res = AtapiCheckInterrupt__(deviceExtension, (UCHAR)c)) {

            KdPrint2((PRINT_PREFIX "AtapiInterrupt2: intr\n"));
            if(i_res == 2) {
                KdPrint2((PRINT_PREFIX "AtapiInterrupt2: Catch unexpected\n"));
                InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
                return TRUE;
            }

            status = TRUE;
            InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_DETECTED);
        } else {
            InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
        }
    }
    KdPrint2((PRINT_PREFIX "AtapiInterrupt2: status %d, c_count %d\n", status, c_count));
    if(status && (c_count != deviceExtension->NumberChannels)) {
        // there is an active ISR/DPC for one channel, but
        // we have an interrupt from another one
        // Lets inform current ISR/DPC about new interrupt
        InterlockedExchange(&(deviceExtension->ReCheckIntr), CHECK_INTR_DETECTED);
    } else {
        status = FALSE;
    }
    KdPrint2((PRINT_PREFIX "AtapiInterrupt2: return %d\n", status));
    return status;
    
} // end AtapiInterrupt2()

BOOLEAN
AtapiInterruptDpc(
    IN PVOID HwDeviceExtension
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG c;

    for(c=0; c<deviceExtension->NumberChannels; c++) {
        KdPrint2((PRINT_PREFIX "AtapiInterruptDpc: %#x\n",c));

        if(!(deviceExtension->chan[c].ChannelCtrlFlags & CTRFLAGS_DPC_REQ)) {

            if((ULONG)InterlockedCompareExchange((PVOID*)&(deviceExtension->chan[c].CheckIntr),
                                          (PVOID)CHECK_INTR_ACTIVE,
                                          (PVOID)CHECK_INTR_DETECTED) != CHECK_INTR_DETECTED) {
                continue;
            }
                        
        } else {
            deviceExtension->chan[c].ChannelCtrlFlags &= ~CTRFLAGS_DPC_REQ;
        }

        if(!AtapiInterrupt__(HwDeviceExtension, (UCHAR)c)) {
            InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
        }
    }
    return TRUE;
} // end AtapiInterruptDpc()


BOOLEAN
AtapiEnableInterrupts__(
    IN PVOID HwDeviceExtension
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    KdPrint2((PRINT_PREFIX "AtapiEnableInterrupts():\n"));
    ULONG c;

    for(c=0; c<deviceExtension->NumberChannels; c++) {
        KdPrint2((PRINT_PREFIX "AtapiEnableInterrupts(2): %#x\n",c));

        if(deviceExtension->chan[c].ChannelCtrlFlags & CTRFLAGS_ENABLE_INTR_REQ) {
            deviceExtension->chan[c].ChannelCtrlFlags &= ~CTRFLAGS_ENABLE_INTR_REQ;
            AtapiEnableInterrupts(HwDeviceExtension, c);
            InterlockedExchange(&(deviceExtension->chan[c].CheckIntr),
                                          CHECK_INTR_IDLE);
        } else {
            if((ULONG)InterlockedCompareExchange((PVOID*)&(deviceExtension->chan[c].CheckIntr),
                                          (PVOID)CHECK_INTR_ACTIVE,
                                          (PVOID)CHECK_INTR_DETECTED) != CHECK_INTR_DETECTED) {
                continue;
            }
            if(!AtapiInterrupt__(HwDeviceExtension, (UCHAR)c)) {
                InterlockedExchange(&(deviceExtension->chan[c].CheckIntr), CHECK_INTR_IDLE);
            }
        }
    }
    return TRUE;

} // end AtapiEnableInterrupts__()


VOID
AtapiEnableInterrupts(
    IN PVOID HwDeviceExtension,
    IN ULONG c
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    KdPrint2((PRINT_PREFIX "AtapiEnableInterrupts_%d: %d\n",c, deviceExtension->chan[c].DisableIntr));
    if(!InterlockedDecrement(&deviceExtension->chan[c].DisableIntr)) {
        AtapiWritePort1(&deviceExtension->chan[c], &deviceExtension->BaseIoAddress2[c]->AlternateStatus,
                               IDE_DC_A_4BIT );
        deviceExtension->chan[c].ChannelCtrlFlags &= ~CTRFLAGS_INTR_DISABLED;
    }
    return;
} // end AtapiEnableInterrupts()

VOID
AtapiDisableInterrupts(
    IN PVOID HwDeviceExtension,
    IN ULONG c
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    KdPrint2((PRINT_PREFIX "AtapiDisableInterrupts_%d: %d\n",c, deviceExtension->chan[c].DisableIntr));
    // mark channel as busy
    if(InterlockedIncrement(&deviceExtension->chan[c].DisableIntr)) {
        AtapiWritePort1(&deviceExtension->chan[c], &deviceExtension->BaseIoAddress2[c]->AlternateStatus,
                               IDE_DC_DISABLE_INTERRUPTS | IDE_DC_A_4BIT );
        deviceExtension->chan[c].ChannelCtrlFlags |= CTRFLAGS_INTR_DISABLED;
    }

    return;
} // end AtapiDisableInterrupts()


/*
    Check hardware for interrupt state
 */
BOOLEAN
AtapiCheckInterrupt__(
    IN PVOID HwDeviceExtension,
    IN UCHAR c
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL chan = &(deviceExtension->chan[c]);

    ULONG VendorID  = deviceExtension->DevID & 0xffff;
    ULONG ChipType  = deviceExtension->HwFlags & CHIPTYPE_MASK;

    PATAPI_REGISTERS_1 baseIoAddress1;
    PATAPI_REGISTERS_2 baseIoAddress2;
    ULONG status;
    UCHAR dma_status = 0;
    UCHAR statusByte;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    UCHAR Channel;
    UCHAR lChannel;
    BOOLEAN DmaTransfer = FALSE;
    BOOLEAN OurInterrupt = FALSE;

    KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__:\n"));

    if(chan->ChannelCtrlFlags & CTRFLAGS_DMA_ACTIVE) {
        DmaTransfer = TRUE;
        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: cntrlr %#x, lch %#x DmaTransfer = TRUE\n", deviceExtension->DevIndex, c));
    }

    lChannel = c;
    Channel = (UCHAR)(deviceExtension->Channel + lChannel);

    baseIoAddress1 =    (PATAPI_REGISTERS_1)deviceExtension->BaseIoAddress1[lChannel];
    baseIoAddress2 =    (PATAPI_REGISTERS_2)deviceExtension->BaseIoAddress2[lChannel];

    // do controller-specific interrupt servicing staff
    if(deviceExtension->UnknownDev)
        goto check_unknown;

    switch(VendorID) {
    case ATA_HIGHPOINT_ID:
        if (!DmaTransfer)
            break;
        if (((dma_status = GetDmaStatus(deviceExtension, lChannel)) &
            (BM_STATUS_ACTIVE | BM_STATUS_INTR)) != BM_STATUS_INTR)
            return FALSE;
        AtapiWritePort1(chan, &(deviceExtension->BaseIoAddressBM[lChannel]->Status), dma_status | BM_STATUS_INTR);
        goto skip_dma_stat_check;

    case ATA_PROMISE_ID: {
        switch(ChipType) {
        case PROLD:
        case PRNEW:
            status = AtapiReadPort4(chan, (PULONG)((PUCHAR)(deviceExtension->BaseIoAddressBM_0)+0x1c));
            if (!DmaTransfer)
                break;
            if (!(status & 
                  ((Channel) ? 0x00004000 : 0x00000400)))
                return FALSE;
            break;
        case PRTX:
            AtapiWritePort1(chan, (PUCHAR)(&deviceExtension->BaseIoAddressBM[lChannel]->DeviceSpecific0), 0x0b);
            status = AtapiReadPort1(chan, (PUCHAR)(&deviceExtension->BaseIoAddressBM[lChannel]->DeviceSpecific1));
            if (!DmaTransfer)
                break;
            if(!(status & 0x20))
                return FALSE;
            break;
        case PRMIO:
            status = AtapiReadPort4(chan, (PULONG)(deviceExtension->BaseMemAddress)+0x0040);
            if(!(status & (1 << Channel))) {
                return FALSE;
            }
            AtapiWritePort4(chan, (PULONG)&(deviceExtension->BaseIoAddressBM[lChannel]->DeviceSpecific0), 0x00000001);
            break;
        }
        break; }
    case ATA_SILICON_IMAGE_ID:

        if(!(deviceExtension->HwFlags & SII_INTR))
            break;
        GetPciConfig1(0x71, dma_status);
        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: 0x71 = %#x\n", dma_status));
        if (!(dma_status &
              (Channel ? 0x08 : 0x04))) {
            return FALSE;
        }
        if (!DmaTransfer) {
            KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: cmd our\n"));
            OurInterrupt = 2;
        }
        SetPciConfig1(0x71, (Channel ? 0x08 : 0x04));
        break;

    case ATA_ACARD_ID:
        if (!DmaTransfer)
            break;
        dma_status = GetDmaStatus(deviceExtension, lChannel);
        if (!((dma_status = GetDmaStatus(deviceExtension, lChannel)) & BM_STATUS_INTR))
            return FALSE;
        AtapiWritePort1(chan, &(deviceExtension->BaseIoAddressBM[lChannel]->Status), dma_status | BM_STATUS_INTR);
        ScsiPortStallExecution(1);
        AtapiWritePort1(chan, &(deviceExtension->BaseIoAddressBM[lChannel]->Command),
            AtapiReadPort1(chan, &(deviceExtension->BaseIoAddressBM[lChannel])->Command) & ~BM_COMMAND_START_STOP);
        goto skip_dma_stat_check;
    }
check_unknown:
    if (DmaTransfer) {
        if (!((dma_status = GetDmaStatus(deviceExtension, lChannel)) & BM_STATUS_INTR)) {
            KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: DmaTransfer + !BM_STATUS_INTR\n"));
            return FALSE;
        }
//        AtapiWritePort1(chan, &(deviceExtension->BaseIoAddressBM[lChannel]->Status), dma_status | BM_STATUS_INTR);
    }
skip_dma_stat_check:
    ScsiPortStallExecution(1);

    /* if drive is busy it didn't interrupt */
    GetStatus(chan, baseIoAddress2, statusByte);
    KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: status %#x\n", statusByte));
    if (statusByte & IDE_STATUS_BUSY) {
        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: unexpected BUSY\n"));
        return FALSE;
    }
/*    if (statusByte & IDE_STATUS_BUSY) {
        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: BUSY\n"));
        return INTERRUPT_NONE;
    }*/

    /* clear interrupt and get status */
    GetBaseStatus(chan, baseIoAddress1, statusByte);
    KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: base status %#x\n", statusByte));
    if(!(statusByte & (IDE_STATUS_DRQ | IDE_STATUS_DRDY))) {
        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: no DRQ/DRDY set\n"));
        return FALSE;
    }

    if(DmaTransfer) {
        dma_status = AtapiDmaDone(HwDeviceExtension, -1/*DeviceNumber*/, lChannel, NULL/*srb*/);
    }

    if (!(chan->ExpectingInterrupt)) {

        KdPrint2((PRINT_PREFIX "AtapiCheckInterrupt__: Unexpected interrupt.\n"));
        return OurInterrupt;
    }
    ASSERT(!chan->queue_depth || chan->cur_req);

    return TRUE;

} // end AtapiCheckInterrupt__()


BOOLEAN
AtapiInterrupt__(
    IN PVOID HwDeviceExtension,
    IN UCHAR c
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL chan = &(deviceExtension->chan[c]);
    // Get current Srb
    PSCSI_REQUEST_BLOCK srb = AtapiGetCurRequest(chan);
    PATA_REQ AtaReq = srb ? (PATA_REQ)(srb->SrbExtension) : NULL;

    PATAPI_REGISTERS_1 baseIoAddress1;
    PATAPI_REGISTERS_2 baseIoAddress2;
    ULONG wordCount = 0, wordsThisInterrupt = 256;
    ULONG status;
    UCHAR dma_status = 0;
    ULONG i;
    ULONG k;
    UCHAR statusByte,interruptReason;
    BOOLEAN commandComplete = FALSE;
    BOOLEAN atapiDev = FALSE;
    ULONG slotNumber = deviceExtension->slotNumber;
    ULONG SystemIoBusNumber = deviceExtension->SystemIoBusNumber;
    UCHAR Channel;
    UCHAR lChannel;
    UCHAR DeviceNumber;
    BOOLEAN DmaTransfer = FALSE;
    UCHAR error = 0;
    ULONG TimerValue = 1000;
    BOOLEAN InDpc = (KeGetCurrentIrql() == DISPATCH_LEVEL) /*||
                    (chan->ChannelCtrlFlags & CTRFLAGS_INTR_DISABLED)*/;
    BOOLEAN UseDpc = deviceExtension->UseDpc;
    BOOLEAN DataOverrun = FALSE;

    if(InDpc) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: InDpc = TRUE\n"));
        ASSERT((chan->ChannelCtrlFlags & CTRFLAGS_INTR_DISABLED));
    }

    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR OldReqState = REQ_STATE_NONE;
    ULONG ldev;
    PHW_LU_EXTENSION LunExt;

    KdPrint2((PRINT_PREFIX "AtapiInterrupt: cntrlr %#x, irql %#x\n", deviceExtension->DevIndex, KeGetCurrentIrql()));

    if((chan->ChannelCtrlFlags & CTRFLAGS_DMA_ACTIVE) ||
       (AtaReq && (AtaReq->Flags & REQ_FLAG_DMA_OPERATION)) ) {
        DmaTransfer = TRUE;
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: DmaTransfer = TRUE\n"));
    }

    lChannel = c;
    Channel = (UCHAR)(deviceExtension->Channel + lChannel);

    baseIoAddress1 =    (PATAPI_REGISTERS_1)deviceExtension->BaseIoAddress1[lChannel];
    baseIoAddress2 =    (PATAPI_REGISTERS_2)deviceExtension->BaseIoAddress2[lChannel];

    if(AtaReq && InDpc) {
        switch(AtaReq->ReqState) {
        case REQ_STATE_DPC_INTR_REQ:

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: REQ_STATE_DPC_INTR_REQ (%d) for channel %#x\n",TimerValue,c));
CallTimerDpc:
            AtaReq->ReqState = REQ_STATE_PROCESSING_INTR;
            AtapiQueueTimerDpc(HwDeviceExtension, c,
                                 c ? AtapiCallBack_1 : AtapiCallBack_0,
                                 TimerValue);
            return TRUE;

/*        case REQ_STATE_DPC_RESET_REQ:
            goto IntrResetController;*/

/*        case REQ_STATE_DPC_COMPLETE_REQ:
            goto IntrCompleteReq;*/

        }
    }

    // check if we are in ISR DPC
    if(InDpc) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: CTRFLAGS_INTR_DISABLED\n"));
        goto ServiceInterrupt;
    }

    if (DmaTransfer) {
        dma_status = GetDmaStatus(deviceExtension, lChannel);
    }

    if (!(chan->ExpectingInterrupt)) {

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Unexpected interrupt for this channel.\n"));
        return FALSE;
    }

    // change request state
    if(AtaReq) {
        OldReqState = AtaReq->ReqState;
        AtaReq->ReqState = REQ_STATE_PROCESSING_INTR;
    }

    // We don't want using DPC for fast operations, like
    // DMA completion, sending CDB, short ATAPI transfers, etc.
    // !!!! BUT !!!!
    // We MUST use DPC, because of interprocessor synchronization
    // on multiprocessor platforms

    if(DmaTransfer)
        goto ServiceInterrupt;
    if(OldReqState == REQ_STATE_ATAPI_EXPECTING_CMD_INTR)
        goto ServiceInterrupt;
    if(OldReqState == REQ_STATE_ATAPI_EXPECTING_DATA_INTR)
        goto ServiceInterrupt;
    if(OldReqState == REQ_STATE_ATAPI_DO_NOTHING_INTR) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: do nothing on interrupt\n"));
        return TRUE;
    }

    if(!UseDpc)
        goto ServiceInterrupt;

//DisableInterrupts:

    if(InDpc) {
        TimerValue = 1;
        goto CallTimerDpc;
    }

    KdPrint2((PRINT_PREFIX "AtapiInterrupt: try service interrupt\n"));
///    goto ServiceInterrupt;
    KdPrint2((PRINT_PREFIX "AtapiInterrupt: direct DPC call\n"));

PostToDpc:

    KdPrint2((PRINT_PREFIX "AtapiInterrupt: start DPC init...\n"));
    // disable interrupts for this channel
    AtapiDisableInterrupts(deviceExtension, lChannel);
    // go to ISR DPC
    chan->ChannelCtrlFlags |= CTRFLAGS_DPC_REQ;

    ScsiPortNotification(CallEnableInterrupts, HwDeviceExtension,
                         /*c ?*/ AtapiInterruptDpc/*_1 : AtapiInterruptDpc_0*/);
    KdPrint2((PRINT_PREFIX "AtapiInterrupt: DPC inited\n"));
    return TRUE;

ServiceInterrupt:

    if (srb) {
        PathId   = srb->PathId;  
        TargetId = srb->TargetId;
        Lun      = srb->Lun;     
    } else {
        PathId = (UCHAR)c;
        TargetId =
        Lun      = 0;
        goto enqueue_next_req;
    }

    ldev = GET_LDEV2(PathId, TargetId, Lun);
    DeviceNumber = (UCHAR)(ldev & 1);
    LunExt = &(deviceExtension->lun[ldev]);
    atapiDev = (LunExt->DeviceFlags & DFLAGS_ATAPI_DEVICE) ? TRUE : FALSE;
    KdPrint2((PRINT_PREFIX "dev_type %s\n", atapiDev ? "ATAPI" : "IDE"));

    if(AtaReq && InDpc) {
        switch(AtaReq->ReqState) {
        case REQ_STATE_DPC_WAIT_DRQ0:
            goto PIO_wait_DRQ0;
        case REQ_STATE_DPC_WAIT_BUSY:
            goto PIO_wait_busy;
        case REQ_STATE_DPC_WAIT_DRQ:
            goto PIO_wait_DRQ;
        case REQ_STATE_DPC_WAIT_DRQ_ERR:
            goto continue_err;
        }
    }

    // make additional delay for old devices (if we are not in DPC)
    if(!LunExt->IdentifyData.MajorRevision &&
       !InDpc) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: additional delay 10us for old devices\n"));
        ScsiPortStallExecution(10);
    }

    /* clear interrupt and get status */
    GetBaseStatus(chan, baseIoAddress1, statusByte);
    KdPrint2((PRINT_PREFIX "AtapiInterrupt: Entered with status (%#x)\n", statusByte));

    if(!UseDpc) {
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: operate like in DPC\n"));
        InDpc = TRUE;
//        AtapiDisableInterrupts(HwDeviceExtension, c);
    }

    if (!atapiDev) {
        if (statusByte & IDE_STATUS_BUSY) {
            if (deviceExtension->DriverMustPoll) {
                // Crashdump is polling and we got caught with busy asserted.
                // Just go away, and we will be polled again shortly.
                KdPrint2((PRINT_PREFIX "AtapiInterrupt: Hit BUSY while polling during crashdump.\n"));
                goto ReturnEnableIntr;
            }
            // Ensure BUSY is non-asserted.
            // make a very small idle before falling to DPC
            k = InDpc ? 1000 : 2;
            for (i = 0; i < k; i++) {

                GetBaseStatus(chan, baseIoAddress1, statusByte);
                if (!(statusByte & IDE_STATUS_BUSY)) {
                    break;
                }
                ScsiPortStallExecution(10);
            }

            if (!InDpc && i == 2) {

                KdPrint2((PRINT_PREFIX "AtapiInterrupt: BUSY on entry. Status %#x, Base IO %#x\n", statusByte,
                            baseIoAddress1));

                goto PostToDpc;
            } else 
            if (InDpc && i == 1000) {
                // reset the controller.
                KdPrint2((PRINT_PREFIX 
                            "AtapiInterrupt: Resetting due to BUSY on entry - %#x. Base Io %#x.\n",
                            statusByte,
                            baseIoAddress1));
                goto IntrPrepareResetController;
            }
        }
    } else {
        if(!LunExt->IdentifyData.MajorRevision &&
            InDpc) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: additional delay 10us for old devices (2)\n"));
            ScsiPortStallExecution(10);
        }
    }

    // Check for error conditions.
    if ((statusByte & IDE_STATUS_ERROR) ||
        (dma_status & BM_STATUS_ERR)) {

        error = AtapiReadPort1(chan, &baseIoAddress1->i.Error);
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Error %#x\n", error));
        if(AtaReq) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: Bad Lba %#I64x\n", AtaReq->lba));
        } else {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: Bad Lba unknown\n"));
        }

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: wait ready after error\n"));

        ScsiPortStallExecution(100);
continue_err:

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Intr on DRQ %x\n",
            LunExt->DeviceFlags & DFLAGS_INT_DRQ));

        for (k = 0; k < 500; k++) {
            GetStatus(chan, baseIoAddress2,statusByte);
            if (!(statusByte & IDE_STATUS_DRQ)) {
/*                if(!InDpc) {
                    // goto DPC
                    AtaReq->ReqState = REQ_STATE_DPC_WAIT_DRQ_ERR;
                    TimerValue = 500;
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: go to DPC (busy)\n"));
                    goto PostToDpc;
                }*/
                ScsiPortStallExecution(50);
            } else {
                break;
            }
        }

        if (!atapiDev) {
            /* if this is a UDMA CRC error, reinject request */

            AtaReq->retry++;
            if(AtaReq->retry < MAX_RETRIES) {
#ifdef IO_STATISTICS
                chan->lun[DeviceNumber]->ModeErrorCount[AtaReq->retry]++;
#endif //IO_STATISTICS
                if(DmaTransfer /*&&
                   (error & IDE_ERROR_ICRC)*/) {
                    if(AtaReq->retry < MAX_RETRIES) {
//fallback_pio:
                        AtaReq->Flags &= ~REQ_FLAG_DMA_OPERATION;
                        AtaReq->Flags |= REQ_FLAG_FORCE_DOWNRATE;
//                        LunExt->DeviceFlags |= DFLAGS_FORCE_DOWNRATE;
                        AtaReq->ReqState = REQ_STATE_QUEUED;
                        goto reenqueue_req;
                    }
                } else {
                    if(!(AtaReq->Flags & REQ_FLAG_FORCE_DOWNRATE)) {
                        AtaReq->retry++;
                    }
                    KdPrint2((PRINT_PREFIX "Errors in PIO mode\n"));
                }
            }
        } else {
            interruptReason = (AtapiReadPort1(chan, &baseIoAddress1->i.InterruptReason) & 0x3);
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: ATAPI Error, int reason %x\n", interruptReason));
        }

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Error\n"));
        if (srb->Cdb[0] != SCSIOP_REQUEST_SENSE) {
            // Fail this request.
            status = SRB_STATUS_ERROR;
            goto CompleteRequest;
        }
#ifdef IO_STATISTICS
    } else
    if(AtaReq->Flags & REQ_FLAG_FORCE_DOWNRATE) {
        KdPrint2((PRINT_PREFIX "Some higher mode doesn't work right :((\n"));
        chan->lun[DeviceNumber]->RecoverCount[AtaReq->retry]++;
        if(chan->lun[DeviceNumber]->RecoverCount[AtaReq->retry] >= chan->lun[DeviceNumber]->IoCount/3) {
            deviceExtension->lun[DeviceNumber].OrigTransferMode =
                deviceExtension->lun[DeviceNumber].TransferMode;
        }
#endif //IO_STATISTICS
    }
#ifdef IO_STATISTICS
    chan->lun[DeviceNumber]->IoCount++;
#endif //IO_STATISTICS

    // check reason for this interrupt.
    if (atapiDev) {

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: ATAPI branch\n"));
        // ATAPI branch

        interruptReason = (AtapiReadPort1(chan, &baseIoAddress1->i.InterruptReason) & 0x3);
        if(DmaTransfer) {
            wordsThisInterrupt = 256*512;
        } else {
            wordsThisInterrupt = 256;
        }

    } else {

        // ATA branch

        if(DmaTransfer) {
            statusByte |= IDE_STATUS_DRQ;
        }
        if (statusByte & IDE_STATUS_DRQ) {

            if(DmaTransfer) {
                wordsThisInterrupt = 256*512;
            } else
            if (LunExt->MaximumBlockXfer) {
                wordsThisInterrupt = 256 * LunExt->MaximumBlockXfer;
            }

            if (srb->SrbFlags & SRB_FLAGS_DATA_IN) {

                interruptReason =  0x2;

            } else if (srb->SrbFlags & SRB_FLAGS_DATA_OUT) {
                interruptReason = 0x0;

            } else {
                status = SRB_STATUS_ERROR;
                goto CompleteRequest;
            }

        } else if (statusByte & IDE_STATUS_BUSY) {

            AtapiEnableInterrupts(deviceExtension, lChannel);
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: return FALSE on ATA IDE_STATUS_BUSY\n"));
            return FALSE;

        } else {

            if (AtaReq->WordsLeft) {

                // Funky behaviour seen with PCI IDE (not all, just one).
PIO_wait_DRQ0:
                // The ISR hits with DRQ low, but comes up later.
                for (k = 0; k < 5000; k++) {
                    GetStatus(chan, baseIoAddress2,statusByte);
                    if (statusByte & IDE_STATUS_DRQ) {
                        break;
                    }
                    if(!InDpc) {
                        // goto DPC
                        AtaReq->ReqState = REQ_STATE_DPC_WAIT_DRQ0;
                        TimerValue = 100;
                        KdPrint2((PRINT_PREFIX "AtapiInterrupt: go to DPC (drq0)\n"));
                        goto PostToDpc;
                    }
                    ScsiPortStallExecution(100);
                }
                if (k == 5000) {
                    // reset the controller.
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: Resetting due to DRQ not up. Status %#x, Base IO %#x\n",
                                statusByte, baseIoAddress1));
IntrPrepareResetController:
                    AtapiResetController__(HwDeviceExtension, lChannel, FALSE);
                    goto ReturnEnableIntr;

                } else {
                    interruptReason = (srb->SrbFlags & SRB_FLAGS_DATA_IN) ? 0x2 : 0x0;
                }

            } else {
                // Command complete - verify, write, or the SMART enable/disable.
                // Also get_media_status
                interruptReason = 0x3;
            }
        }
    }

    KdPrint2((PRINT_PREFIX "AtapiInterrupt: i-reason=%d, status=%#x\n", interruptReason, statusByte));
    if (interruptReason == 0x1 && (statusByte & IDE_STATUS_DRQ)) {
        // Write the packet.
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Writing Atapi packet.\n"));
        // Send CDB to device.
        WriteBuffer(chan, baseIoAddress1, (PUSHORT)srb->Cdb, 6);
        AtaReq->ReqState = REQ_STATE_ATAPI_EXPECTING_DATA_INTR;

        if(chan->ChannelCtrlFlags & CTRFLAGS_DMA_OPERATION) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: AtapiDmaStart().\n"));
            AtapiDmaStart(HwDeviceExtension, ldev & 1, lChannel, srb);
        }

        goto ReturnEnableIntr;

    } else if (interruptReason == 0x0 && (statusByte & IDE_STATUS_DRQ)) {

        // Write the data.
        if (atapiDev) {

            // Pick up bytes to transfer and convert to words.
            wordCount =
                AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountLow);

            wordCount |=
                AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountHigh) << 8;

            // Covert bytes to words.
            wordCount >>= 1;
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: get W wordCount %#x\n", wordCount));

            if (wordCount != AtaReq->WordsLeft) {
                KdPrint2((PRINT_PREFIX 
                           "AtapiInterrupt: %d words requested; %d words xferred\n",
                           AtaReq->WordsLeft,
                           wordCount));
            }

            // Verify this makes sense.
            if (wordCount > AtaReq->WordsLeft) {
                wordCount = AtaReq->WordsLeft;
                KdPrint2((PRINT_PREFIX 
                           "AtapiInterrupt: Write underrun\n"));
                DataOverrun = TRUE;
            }

        } else {

            // IDE path. Check if words left is at least 256.
            if (AtaReq->WordsLeft < wordsThisInterrupt) {

               // Transfer only words requested.
               wordCount = AtaReq->WordsLeft;

            } else {

               // Transfer next block.
               wordCount = wordsThisInterrupt;
            }
        }

        if (DmaTransfer && (chan->ChannelCtrlFlags & CTRFLAGS_DMA_OPERATION)) {
            ASSERT(AtaReq->WordsLeft == wordCount);
            AtaReq->WordsLeft = 0;
            status = SRB_STATUS_SUCCESS;
            chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_OPERATION;
            goto CompleteRequest;
        }
        // Ensure that this is a write command.
        if (srb->SrbFlags & SRB_FLAGS_DATA_OUT) {

           KdPrint2((PRINT_PREFIX 
                      "AtapiInterrupt: Write interrupt\n"));

           statusByte = WaitOnBusy(chan, &baseIoAddress2->ide);

           if (atapiDev || !(LunExt->DeviceFlags & DFLAGS_DWORDIO_ENABLED) /*!deviceExtension->DWordIO*/) {

               WriteBuffer(chan, baseIoAddress1,
                           AtaReq->DataBuffer,
                           wordCount);
           } else {

               WriteBuffer2(chan, baseIoAddress1,
                           (PULONG)(AtaReq->DataBuffer),
                           wordCount / 2);
           }
        } else {

            KdPrint2((PRINT_PREFIX 
                        "AtapiInterrupt: Int reason %#x, but srb is for a write %#x.\n",
                        interruptReason,
                        srb));

            // Fail this request.
            status = SRB_STATUS_ERROR;
            goto CompleteRequest;
        }

        // Advance data buffer pointer and bytes left.
        AtaReq->DataBuffer += wordCount;
        AtaReq->WordsLeft -= wordCount;

        if (atapiDev) {
            AtaReq->ReqState = REQ_STATE_ATAPI_EXPECTING_DATA_INTR;
        }

        goto ReturnEnableIntr;

    } else if (interruptReason == 0x2 && (statusByte & IDE_STATUS_DRQ)) {


        if (atapiDev) {

            // Pick up bytes to transfer and convert to words.
            wordCount =
                AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountLow) |
                (AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountHigh) << 8);

            // Covert bytes to words.
            wordCount >>= 1;
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: get R wordCount %#x\n", wordCount));

            if (wordCount != AtaReq->WordsLeft) {
                KdPrint2((PRINT_PREFIX 
                           "AtapiInterrupt: %d words requested; %d words xferred\n",
                           AtaReq->WordsLeft,
                           wordCount));
            }

            // Verify this makes sense.
            if (wordCount > AtaReq->WordsLeft) {
                wordCount = AtaReq->WordsLeft;
                DataOverrun = TRUE;
            }

        } else {

            // Check if words left is at least 256.
            if (AtaReq->WordsLeft < wordsThisInterrupt) {

               // Transfer only words requested.
               wordCount = AtaReq->WordsLeft;

            } else {

               // Transfer next block.
               wordCount = wordsThisInterrupt;
            }
        }

        if (DmaTransfer && (chan->ChannelCtrlFlags & CTRFLAGS_DMA_OPERATION)) {
            ASSERT(AtaReq->WordsLeft == wordCount);
            AtaReq->WordsLeft = 0;
            status = SRB_STATUS_SUCCESS;
            chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_OPERATION;
            goto CompleteRequest;
        }
        // Ensure that this is a read command.
        if (srb->SrbFlags & SRB_FLAGS_DATA_IN) {

/*           KdPrint2((
                      "AtapiInterrupt: Read interrupt\n"));*/

            statusByte = WaitOnBusy(chan, &baseIoAddress2->ide);

            if (atapiDev || !(LunExt->DeviceFlags & DFLAGS_DWORDIO_ENABLED) /*!deviceExtension->DWordIO*/) {
                KdPrint2((PRINT_PREFIX 
                           "IdeIntr: Read %#x words\n", wordCount));

                ReadBuffer(chan, baseIoAddress1,
                          AtaReq->DataBuffer,
                          wordCount);
                KdPrint2(("IdeIntr: Read AtaReq->DataBuffer %#x, srb->DataBuffer %#x\n", AtaReq->DataBuffer, (srb ? srb->DataBuffer : (void*)-1) ));
                KdDump(AtaReq->DataBuffer, wordCount*2);

                if(DataOverrun) {
                    AtapiSuckPort2(chan, &baseIoAddress1->ide, &baseIoAddress2->ide);
                }

            } else {
                KdPrint2((PRINT_PREFIX 
                          "IdeIntr: Read %#x Dwords\n", wordCount/2));

                ReadBuffer2(chan, baseIoAddress1,
                           (PULONG)(AtaReq->DataBuffer),
                           wordCount / 2);
            }
        } else {

            KdPrint2((PRINT_PREFIX 
                        "AtapiInterrupt: Int reason %#x, but srb is for a read %#x.\n",
                        interruptReason,
                        srb));

            // Fail this request.
            status = SRB_STATUS_ERROR;
            goto CompleteRequest;
        }

        // Advance data buffer pointer and bytes left.
        AtaReq->DataBuffer += wordCount;
        AtaReq->WordsLeft -= wordCount;

        // Check for read command complete.
        if (AtaReq->WordsLeft == 0) {

            if (atapiDev) {

                // Work around to make many atapi devices return correct sector size
                // of 2048. Also certain devices will have sector count == 0x00, check
                // for that also.
                if ((srb->Cdb[0] == SCSIOP_READ_CAPACITY) &&
                    (LunExt->IdentifyData.DeviceType == ATAPI_TYPE_CDROM)) {

                    AtaReq->DataBuffer -= wordCount;
                    if (AtaReq->DataBuffer[0] == 0x00) {

                        *((ULONG *) &(AtaReq->DataBuffer[0])) = 0xFFFFFF7F;

                    }

                    *((ULONG *) &(AtaReq->DataBuffer[2])) = 0x00080000;
                    AtaReq->DataBuffer += wordCount;
                }
            } else {

                // Completion for IDE drives.
                if (AtaReq->WordsLeft) {
                    status = SRB_STATUS_DATA_OVERRUN;
                } else {
                    status = SRB_STATUS_SUCCESS;
                }

                goto CompleteRequest;

            }
        } else {
            if (atapiDev) {
                AtaReq->ReqState = REQ_STATE_ATAPI_EXPECTING_DATA_INTR;
            }
        }

        goto ReturnEnableIntr;

    } else if (interruptReason == 0x3  && !(statusByte & IDE_STATUS_DRQ)) {

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: interruptReason = CompleteRequest\n"));
        // Command complete.
        if(DmaTransfer) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: CompleteRequest, was DmaTransfer\n"));
            AtaReq->WordsLeft = 0;
        }
        if (AtaReq->WordsLeft) {
            status = SRB_STATUS_DATA_OVERRUN;
        } else {
            status = SRB_STATUS_SUCCESS;
        }

CompleteRequest:

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: CompleteRequest\n"));
        // Check and see if we are processing our secret (mechanism status/request sense) srb
        if (AtaReq->OriginalSrb) {

            ULONG srbStatus;

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: OriginalSrb != NULL\n"));
            if (srb->Cdb[0] == SCSIOP_MECHANISM_STATUS) {

                if (status == SRB_STATUS_SUCCESS) {
                    // Bingo!!
                    AtapiHwInitializeChanger (HwDeviceExtension,
                                              srb,
                                              (PMECHANICAL_STATUS_INFORMATION_HEADER) srb->DataBuffer);

                    // Get ready to issue the original srb
                    srb = AtaReq->Srb = AtaReq->OriginalSrb;
                    AtaReq->OriginalSrb = NULL;

                } else {
                    // failed!  Get the sense key and maybe try again
                    srb = AtaReq->Srb = BuildRequestSenseSrb (
                                                          HwDeviceExtension,
                                                          AtaReq->OriginalSrb);
                }
                
                srbStatus = AtapiSendCommand(HwDeviceExtension, srb, CMD_ACTION_ALL);

                KdPrint2((PRINT_PREFIX "AtapiInterrupt: chan->ExpectingInterrupt %d (1)\n", chan->ExpectingInterrupt));

                if (srbStatus == SRB_STATUS_PENDING) {
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: send orig SRB_STATUS_PENDING (1)\n"));
                    goto ReturnEnableIntr;
                }

            } else { // srb->Cdb[0] == SCSIOP_REQUEST_SENSE)

                PSENSE_DATA senseData = (PSENSE_DATA) srb->DataBuffer;

                if (status == SRB_STATUS_DATA_OVERRUN) {
                    // Check to see if we at least get mininum number of bytes
                    if ((srb->DataTransferLength - AtaReq->WordsLeft) >
                        (offsetof (SENSE_DATA, AdditionalSenseLength) + sizeof(senseData->AdditionalSenseLength))) {
                        status = SRB_STATUS_SUCCESS;
                    }
                }

                if (status == SRB_STATUS_SUCCESS) {
                    if ((senseData->SenseKey != SCSI_SENSE_ILLEGAL_REQUEST) &&
                        chan->MechStatusRetryCount) {

                        // The sense key doesn't say the last request is illegal, so try again
                        chan->MechStatusRetryCount--;
                        srb = AtaReq->Srb = BuildMechanismStatusSrb (
                                                              HwDeviceExtension,
                                                              AtaReq->OriginalSrb);
                    } else {

                        // last request was illegal.  No point trying again
                        AtapiHwInitializeChanger (HwDeviceExtension,
                                                  srb,
                                                  (PMECHANICAL_STATUS_INFORMATION_HEADER) NULL);

                        // Get ready to issue the original srb
                        srb = AtaReq->Srb = AtaReq->OriginalSrb;
                        AtaReq->OriginalSrb = NULL;
                    }

                    srbStatus = AtapiSendCommand(HwDeviceExtension, srb, CMD_ACTION_ALL);

                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: chan->ExpectingInterrupt %d (2)\n", chan->ExpectingInterrupt));

                    if (srbStatus == SRB_STATUS_PENDING) {
                        KdPrint2((PRINT_PREFIX "AtapiInterrupt: send orig SRB_STATUS_PENDING (2)\n"));
                        goto ReturnEnableIntr;
                    }
                }
            }

            // If we get here, it means AtapiSendCommand() has failed
            // Can't recover.  Pretend the original srb has failed and complete it.

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: Error. complete OriginalSrb\n"));

            if (AtaReq->OriginalSrb) {
                KdPrint2((PRINT_PREFIX "AtapiInterrupt: call AtapiHwInitializeChanger()\n"));
                AtapiHwInitializeChanger (HwDeviceExtension,
                                          srb,
                                          (PMECHANICAL_STATUS_INFORMATION_HEADER) NULL);
                srb = AtaReq->Srb = AtaReq->OriginalSrb;
                AtaReq->OriginalSrb = NULL;
            }

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: chan->ExpectingInterrupt %d (3)\n", chan->ExpectingInterrupt));

            // fake an error and read no data
            status = SRB_STATUS_ERROR;
            srb->ScsiStatus = 0;
            AtaReq->DataBuffer = (PUSHORT)(srb->DataBuffer);
            AtaReq->WordsLeft = srb->DataTransferLength;
            chan->RDP = FALSE;

        } else if (status == SRB_STATUS_ERROR) {

            // Map error to specific SRB status and handle request sense.
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: Error. Begin mapping...\n"));
            status = MapError(deviceExtension,
                              srb);

            chan->RDP = FALSE;

        } else if(!DmaTransfer) {

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: PIO completion\n"));
PIO_wait_busy:
            // Wait for busy to drop.
            for (i = 0; i < 30; i++) {
                GetStatus(chan, baseIoAddress2,statusByte);
                if (!(statusByte & IDE_STATUS_BUSY)) {
                    break;
                }
                if(!InDpc) {
                    // goto DPC
                    AtaReq->ReqState = REQ_STATE_DPC_WAIT_BUSY;
                    TimerValue = 500;
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: go to DPC (busy)\n"));
                    goto PostToDpc;
                }
                ScsiPortStallExecution(500);
            }

            if (i == 30) {

                // reset the controller.
                KdPrint2((PRINT_PREFIX 
                            "AtapiInterrupt: Resetting due to BSY still up - %#x. Base Io %#x\n",
                            statusByte,
                            baseIoAddress1));
                goto IntrPrepareResetController;
            }
            // Check to see if DRQ is still up.
            if (statusByte & IDE_STATUS_DRQ) {

PIO_wait_DRQ:
                for (i = 0; i < 200; i++) {
                    GetStatus(chan, baseIoAddress2,statusByte);
                    if (!(statusByte & IDE_STATUS_DRQ)) {
                        break;
                    }
                    if(!InDpc) {
                        // goto DPC
                        KdPrint2((PRINT_PREFIX "AtapiInterrupt: go to DPC (drq)\n"));
                        AtaReq->ReqState = REQ_STATE_DPC_WAIT_DRQ;
                        TimerValue = 100;
                        goto PostToDpc;
                    }
                    ScsiPortStallExecution(100);
                }

                if (i == 200) {
                    // reset the controller.
                    KdPrint2((PRINT_PREFIX   "AtapiInterrupt: Resetting due to DRQ still up - %#x\n",
                                statusByte));
                    goto IntrPrepareResetController;
                }
            }
            if(atapiDev) {
                KdPrint2(("IdeIntr: Read AtaReq->DataBuffer %#x, srb->DataBuffer %#x, len %#x\n",
                     AtaReq->DataBuffer, (srb ? srb->DataBuffer : (void*)(-1)), srb->DataTransferLength ));
                KdDump(srb->DataBuffer, srb->DataTransferLength);
            }
            if(!AtapiDmaPioSync(HwDeviceExtension, srb, (PUCHAR)(srb->DataBuffer), srb->DataTransferLength)) {
                KdPrint2(("IdeIntr: Can't sync DMA and PIO buffers\n"));
            }
        }

        // Clear interrupt expecting flag.
        chan->ExpectingInterrupt = FALSE;
        InterlockedExchange(&(chan->CheckIntr),
                                      CHECK_INTR_IDLE);

        // Sanity check that there is a current request.
        if (srb != NULL) {
            // Set status in SRB.
            srb->SrbStatus = (UCHAR)status;

            // Check for underflow.
            if (AtaReq->WordsLeft) {

                // Subtract out residual words and update if filemark hit,
                // setmark hit , end of data, end of media...
                if (!(LunExt->DeviceFlags & DFLAGS_TAPE_DEVICE)) {
                    if (status == SRB_STATUS_DATA_OVERRUN) {
                        srb->DataTransferLength -= AtaReq->WordsLeft;
                    } else {
                        srb->DataTransferLength = 0;
                    }
                } else {
                    srb->DataTransferLength -= AtaReq->WordsLeft;
                }
            }

            if (srb->Function != SRB_FUNCTION_IO_CONTROL) {

CompleteRDP:
                // Indicate command complete.
                if (!(chan->RDP)) {
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: RequestComplete\n"));
IntrCompleteReq:
/*                    if(!InDpc) {
                        // AtapiRemoveRequest() can be called at DISPATCH_LEVEL only,
                        // thus we should use CallDisableInterrupts
                        AtaReq->ReqState = REQ_STATE_DPC_COMPLETE_REQ;
                        goto DisableInterrupts;
                    }*/

                    if (status == SRB_STATUS_SUCCESS &&
                        srb->SenseInfoBuffer &&
                        srb->SenseInfoBufferLength >= sizeof(SENSE_DATA)) {

                        PSENSE_DATA  senseBuffer = (PSENSE_DATA)srb->SenseInfoBuffer;

                        KdPrint2((PRINT_PREFIX "AtapiInterrupt: set AutoSense\n"));
                        senseBuffer->ErrorCode = 0;
                        senseBuffer->Valid     = 1;
                        senseBuffer->AdditionalSenseLength = 0xb;
                        senseBuffer->SenseKey =  0;
                        senseBuffer->AdditionalSenseCode = 0;
                        senseBuffer->AdditionalSenseCodeQualifier = 0;

                        srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                    }
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: remove srb %#x, status %x\n", srb, status));
                    AtapiRemoveRequest(chan, srb);
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: RequestComplete, srb %#x\n", srb));
                    ScsiPortNotification(RequestComplete,
                                         deviceExtension,
                                         srb);
                }
            } else {

                KdPrint2((PRINT_PREFIX "AtapiInterrupt: IOCTL completion\n"));
                PSENDCMDOUTPARAMS cmdOutParameters = (PSENDCMDOUTPARAMS)(((PUCHAR)srb->DataBuffer) + sizeof(SRB_IO_CONTROL));

                if (status != SRB_STATUS_SUCCESS) {
                    error = AtapiReadPort1(chan, &baseIoAddress1->i.Error);
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: error %#x\n", error));
                }

                // Build the SMART status block depending upon the completion status.
                cmdOutParameters->cBufferSize = wordCount;
                cmdOutParameters->DriverStatus.bDriverError = (error) ? SMART_IDE_ERROR : 0;
                cmdOutParameters->DriverStatus.bIDEError = error;

                // If the sub-command is return smart status, jam the value from cylinder low and high, into the
                // data buffer.
                if (chan->SmartCommand == RETURN_SMART_STATUS) {
                    cmdOutParameters->bBuffer[0] = RETURN_SMART_STATUS;
                    cmdOutParameters->bBuffer[1] = AtapiReadPort1(chan, &baseIoAddress1->i.InterruptReason);
                    cmdOutParameters->bBuffer[2] = AtapiReadPort1(chan, &baseIoAddress1->i.Unused1);
                    cmdOutParameters->bBuffer[3] = AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountLow);
                    cmdOutParameters->bBuffer[4] = AtapiReadPort1(chan, &baseIoAddress1->i.ByteCountHigh);
                    cmdOutParameters->bBuffer[5] = AtapiReadPort1(chan, &baseIoAddress1->i.DriveSelect);
                    cmdOutParameters->bBuffer[6] = SMART_CMD;
                    cmdOutParameters->cBufferSize = 8;
                }

                // Indicate command complete.
                goto IntrCompleteReq;
            }

        } else {

            KdPrint2((PRINT_PREFIX "AtapiInterrupt: No SRB!\n"));
        }

        if (chan->RDP) {
            // Check DSC
            for (i = 0; i < 5; i++) {
                GetStatus(chan, baseIoAddress2,statusByte);
                if (statusByte & IDE_STATUS_DSC) {
                    KdPrint2((PRINT_PREFIX "AtapiInterrupt: Clear RDP\n"));
                    chan->RDP = FALSE;
                    goto CompleteRDP;
                }
                ScsiPortStallExecution(50);
            }
        }

        if (chan->RDP) {
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: RequestTimerCall 2000\n"));

            TimerValue = 2000;
            goto CallTimerDpc;
        }

//            ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
enqueue_next_req:
        // Get next request
        srb = AtapiGetCurRequest(chan);

reenqueue_req:

        KdPrint2((PRINT_PREFIX "AtapiInterrupt: NextRequest, srb=%#x\n",srb));
        if(!srb) {
            ScsiPortNotification(NextRequest,
                                 deviceExtension,
                                 NULL);
        } else {
            ScsiPortNotification(NextLuRequest,
                                 deviceExtension, 
                                 PathId,
                                 TargetId,
                                 Lun);

            AtapiStartIo__(HwDeviceExtension, srb, FALSE);
        }

        goto ReturnEnableIntr;

    } else {

        // Unexpected int. Catch it
        KdPrint2((PRINT_PREFIX "AtapiInterrupt: Unexpected ATAPI interrupt. InterruptReason %#x. Status %#x.\n",
                    interruptReason,
                    statusByte));

        //return TRUE;
    }

ReturnEnableIntr:

    KdPrint2((PRINT_PREFIX "AtapiInterrupt: ReturnEnableIntr\n",srb));

    if(UseDpc) {
        if(InterlockedExchangeAdd(&(chan->DisableIntr), 0)) {
            chan->ChannelCtrlFlags |= CTRFLAGS_ENABLE_INTR_REQ;
            KdPrint2((PRINT_PREFIX "AtapiInterrupt: call AtapiEnableInterrupts__()\n"));
            ScsiPortNotification(CallDisableInterrupts, HwDeviceExtension,
                                 /*Channel ?*/ AtapiEnableInterrupts__/*_1 : AtapiEnableInterrupts_0*/);
        }
    } else {
//        KdPrint2((PRINT_PREFIX "AtapiInterrupt: call AtapiEnableInterrupts()\n"));
//        AtapiEnableInterrupts(HwDeviceExtension, c);
    }
    InterlockedExchange(&(chan->CheckIntr), CHECK_INTR_IDLE);
    return TRUE;

} // end AtapiInterrupt__()


/*++

Routine Description:

    This routine handles SMART enable, disable, read attributes and threshold commands.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:

    SRB status

--*/
ULONG
IdeSendSmartCommand(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG                c               = GET_CHANNEL(Srb);
    PHW_CHANNEL          chan            = &(deviceExtension->chan[c]);
    PATA_REQ             AtaReq          = (PATA_REQ)(Srb->SrbExtension);
    PIDE_REGISTERS_1     baseIoAddress1  = deviceExtension->BaseIoAddress1[c];
    PIDE_REGISTERS_2     baseIoAddress2  = deviceExtension->BaseIoAddress2[c];
    PSENDCMDOUTPARAMS    cmdOutParameters = (PSENDCMDOUTPARAMS)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));
    SENDCMDINPARAMS      cmdInParameters = *(PSENDCMDINPARAMS)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));
    PIDEREGS             regs            = &cmdInParameters.irDriveRegs;
    ULONG                i;
    UCHAR                statusByte,targetId;


    if (cmdInParameters.irDriveRegs.bCommandReg == SMART_CMD) {

        targetId = cmdInParameters.bDriveNumber;

        //TODO optimize this check
        if ((!(deviceExtension->lun[targetId].DeviceFlags & DFLAGS_DEVICE_PRESENT)) ||
             (deviceExtension->lun[targetId].DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

            return SRB_STATUS_SELECTION_TIMEOUT;
        }

        chan->SmartCommand = cmdInParameters.irDriveRegs.bFeaturesReg;

        // Determine which of the commands to carry out.
        if ((cmdInParameters.irDriveRegs.bFeaturesReg == READ_ATTRIBUTES) ||
            (cmdInParameters.irDriveRegs.bFeaturesReg == READ_THRESHOLDS)) {

            statusByte = WaitOnBusy(chan, baseIoAddress2);

            if (statusByte & IDE_STATUS_BUSY) {
                KdPrint2((PRINT_PREFIX 
                            "IdeSendSmartCommand: Returning BUSY status\n"));
                return SRB_STATUS_BUSY;
            }

            // Zero the ouput buffer as the input buffer info. has been saved off locally (the buffers are the same).
            for (i = 0; i < (sizeof(SENDCMDOUTPARAMS) + READ_ATTRIBUTE_BUFFER_SIZE - 1); i++) {
                ((PUCHAR)cmdOutParameters)[i] = 0;
            }

            // Set data buffer pointer and words left.
            AtaReq->DataBuffer = (PUSHORT)cmdOutParameters->bBuffer;
            AtaReq->WordsLeft = READ_ATTRIBUTE_BUFFER_SIZE / 2;

            AtaCommand(deviceExtension, targetId & 0x1, c,
                       regs->bCommandReg,
                       (USHORT)(regs->bCylLowReg) | (((USHORT)(regs->bCylHighReg)) << 8),
                       0,
                       regs->bSectorNumberReg,
                       regs->bSectorCountReg,
                       regs->bFeaturesReg,
                       ATA_IMMEDIATE);
            // Wait for interrupt.

            return SRB_STATUS_PENDING;

        } else if ((cmdInParameters.irDriveRegs.bFeaturesReg == ENABLE_SMART) ||
                   (cmdInParameters.irDriveRegs.bFeaturesReg == DISABLE_SMART) ||
                   (cmdInParameters.irDriveRegs.bFeaturesReg == RETURN_SMART_STATUS) ||
                   (cmdInParameters.irDriveRegs.bFeaturesReg == ENABLE_DISABLE_AUTOSAVE) ||
                   (cmdInParameters.irDriveRegs.bFeaturesReg == EXECUTE_OFFLINE_DIAGS) ||
                   (cmdInParameters.irDriveRegs.bFeaturesReg == SAVE_ATTRIBUTE_VALUES)) {

            statusByte = WaitOnBusy(chan, baseIoAddress2);

            if (statusByte & IDE_STATUS_BUSY) {
                KdPrint2((PRINT_PREFIX 
                            "IdeSendSmartCommand: Returning BUSY status\n"));
                return SRB_STATUS_BUSY;
            }

            // Zero the ouput buffer as the input buffer info. has been saved off locally (the buffers are the same).
            for (i = 0; i < (sizeof(SENDCMDOUTPARAMS) - 1); i++) {
                ((PUCHAR)cmdOutParameters)[i] = 0;
            }

            // Set data buffer pointer and indicate no data transfer.
            AtaReq->DataBuffer = (PUSHORT)cmdOutParameters->bBuffer;
            AtaReq->WordsLeft = 0;

            AtaCommand(deviceExtension, targetId & 0x1, c,
                       regs->bCommandReg,
                       (USHORT)(regs->bCylLowReg) | (((USHORT)(regs->bCylHighReg)) << 8),
                       0,
                       regs->bSectorNumberReg,
                       regs->bSectorCountReg,
                       regs->bFeaturesReg,
                       ATA_IMMEDIATE);

            // Wait for interrupt.
            return SRB_STATUS_PENDING;
        }
    }

    return SRB_STATUS_INVALID_REQUEST;

} // end IdeSendSmartCommand()


/*++

Routine Description:

    This routine handles IDE read and writes.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:

    SRB status

--*/
ULONG
IdeReadWrite(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN ULONG CmdAction
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    UCHAR                lChannel = GET_CHANNEL(Srb);
    PHW_CHANNEL          chan = &(deviceExtension->chan[lChannel]);
    PATA_REQ             AtaReq = (PATA_REQ)(Srb->SrbExtension);
    ULONG                ldev = GET_LDEV(Srb);
    UCHAR                DeviceNumber = (UCHAR)(ldev & 1);
    PIDE_REGISTERS_1     baseIoAddress1  = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2  = deviceExtension->BaseIoAddress2[lChannel];
    ULONG                startingSector;
    ULONG                wordCount;
    UCHAR                statusByte,statusByte2;
    UCHAR                drvSelect,sectorNumber;
    USHORT               cylinder;
    ULONG                tmp;
    UCHAR                cmd;
    ULONGLONG            lba;
    BOOLEAN              use_dma = FALSE;

    AtaReq->Flags |= REQ_FLAG_REORDERABLE_CMD;

    if((CmdAction & CMD_ACTION_PREPARE) &&
       (AtaReq->ReqState != REQ_STATE_READY_TO_TRANSFER)) {

        // Set data buffer pointer and words left.
        AtaReq->DataBuffer = (PUSHORT)Srb->DataBuffer;

        MOV_SWP_DW2DD(AtaReq->bcount, ((PCDB)Srb->Cdb)->CDB10.TransferBlocks);
        ASSERT(Srb->DataTransferLength == AtaReq->bcount * DEV_BSIZE);
        AtaReq->WordsLeft = min(Srb->DataTransferLength,
                                AtaReq->bcount * DEV_BSIZE) / 2;

        // Set up 1st block.
        MOV_DD_SWP(startingSector, ((PCDB)Srb->Cdb)->CDB10.LBA);

        KdPrint2((PRINT_PREFIX "IdeReadWrite (REQ): Starting sector is %#x, Number of WORDS %#x, DevSize %#x\n",
                   startingSector,
                   AtaReq->WordsLeft,
                   AtaReq->bcount));

        if(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_LBA_ENABLED) {
            lba = startingSector;
        } else {
            tmp = deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack *
                               deviceExtension->lun[ldev].IdentifyData.NumberOfHeads;
            cylinder =    (USHORT)(startingSector / tmp);
            drvSelect =   (UCHAR)((startingSector % tmp) / deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack); 
            sectorNumber = (UCHAR)(startingSector % deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack) + 1;

            lba = (sectorNumber&0xff) | (((ULONG)cylinder&0xffff)<<8) | (((ULONG)drvSelect&0xf)<<24);
        }
        AtaReq->lba = lba;

        AtapiDmaReinit(deviceExtension, ldev, AtaReq);

        if (/*EnableDma &&*/
            (deviceExtension->lun[ldev].TransferMode >= ATA_DMA)) {
            use_dma = TRUE;
        }
        if (use_dma) {
            // this will set REQ_FLAG_DMA_OPERATION in AtaReq->Flags on success
            if(!AtapiDmaSetup(HwDeviceExtension, DeviceNumber, lChannel, Srb,
                          (PUCHAR)(AtaReq->DataBuffer),
                          ((Srb->DataTransferLength + 0x1FF) & ~0x1FF))) {
                use_dma = FALSE;
            }
        }
        AtaReq->ReqState = REQ_STATE_READY_TO_TRANSFER;
    } else { // exec_only
        KdPrint2((PRINT_PREFIX "IdeReadWrite (ExecOnly): \n"));
        lba = AtaReq->lba;
        if(AtaReq->Flags & REQ_FLAG_DMA_OPERATION)
            use_dma = TRUE;
    }
    if(!(CmdAction & CMD_ACTION_EXEC)) {
        return SRB_STATUS_PENDING;
    }

    // Check if write request.
    if (Srb->SrbFlags & SRB_FLAGS_DATA_IN) {

        // Prepare read command.
        if(use_dma) {
            cmd = IDE_COMMAND_READ_DMA;
        } else
        if (deviceExtension->lun[ldev].MaximumBlockXfer) {
            cmd = IDE_COMMAND_READ_MULTIPLE;
        } else {
            cmd = IDE_COMMAND_READ;
        }
    } else {

        // Prepare write command.
        if (use_dma) {
            wordCount = Srb->DataTransferLength/2;
            cmd = IDE_COMMAND_WRITE_DMA;
        } else
        if (deviceExtension->lun[ldev].MaximumBlockXfer) {
            wordCount = 256 * deviceExtension->lun[ldev].MaximumBlockXfer;

            if (AtaReq->WordsLeft < wordCount) {
               // Transfer only words requested.
               wordCount = AtaReq->WordsLeft;
            }
            cmd = IDE_COMMAND_WRITE_MULTIPLE;

        } else {
            wordCount = 256;
            cmd = IDE_COMMAND_WRITE;
        }
    }

    if(use_dma) {
        chan->ChannelCtrlFlags |= CTRFLAGS_DMA_OPERATION;
    } else {
        chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_OPERATION;
    }

    // Send IO command.
    KdPrint2((PRINT_PREFIX "IdeReadWrite: Count %#x(%#x)\n", ((Srb->DataTransferLength + 0x1FF) / 0x200),
                                                           ((wordCount*2 + DEV_BSIZE-1) / DEV_BSIZE)));
    if ((Srb->SrbFlags & SRB_FLAGS_DATA_IN) ||
        use_dma) {
        statusByte2 = AtaCommand48(deviceExtension, DeviceNumber, lChannel,
                     cmd, lba,
                     (UCHAR)((Srb->DataTransferLength + DEV_BSIZE-1) / DEV_BSIZE),
//                     (UCHAR)((wordCount*2 + DEV_BSIZE-1) / DEV_BSIZE),
                     0, ATA_IMMEDIATE);
        GetStatus(chan, baseIoAddress2, statusByte2);
        if(statusByte2 & IDE_STATUS_ERROR) {
            statusByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);
            KdPrint2((PRINT_PREFIX "IdeReadWrite: status %#x, error %#x\n", statusByte2, statusByte));
            return SRB_STATUS_ERROR;
        }
        if(use_dma) {
            AtapiDmaStart(HwDeviceExtension, DeviceNumber, lChannel, Srb);
        }
        return SRB_STATUS_PENDING;
    }

    statusByte = AtaCommand48(deviceExtension, DeviceNumber, lChannel,
                 cmd, lba,
                 (UCHAR)((Srb->DataTransferLength + DEV_BSIZE-1) / DEV_BSIZE),
//                 (UCHAR)((wordCount*2 + DEV_BSIZE-1) / DEV_BSIZE),
                 0, ATA_WAIT_INTR);

    if (!(statusByte & IDE_STATUS_DRQ)) {

        KdPrint2((PRINT_PREFIX 
                   "IdeReadWrite: DRQ never asserted (%#x) original status (%#x)\n",
                   statusByte,
                   statusByte2));

        AtaReq->WordsLeft = 0;

        // Clear interrupt expecting flag.
        chan->ExpectingInterrupt = FALSE;
        InterlockedExchange(&(chan->CheckIntr),
                                      CHECK_INTR_IDLE);

        // Clear current SRB.
        AtapiRemoveRequest(chan, Srb);

        return SRB_STATUS_TIMEOUT;
    }

    chan->ExpectingInterrupt = TRUE;
    InterlockedExchange(&(chan->CheckIntr),
                                  CHECK_INTR_IDLE);

    // Write next 256*N words.
    if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DWORDIO_ENABLED)) {
        KdPrint2((PRINT_PREFIX 
                   "IdeReadWrite: Write %#x words\n", wordCount));

        WriteBuffer(chan, baseIoAddress1,
                  AtaReq->DataBuffer,
                  wordCount);

    } else {

        KdPrint2((PRINT_PREFIX 
                   "IdeReadWrite: Write %#x Dwords\n", wordCount/2));

        WriteBuffer2(chan, baseIoAddress1,
                   (PULONG)(AtaReq->DataBuffer),
                   wordCount / 2);
    }

    // Adjust buffer address and words left count.
    AtaReq->WordsLeft -= wordCount;
    AtaReq->DataBuffer += wordCount;

    // Wait for interrupt.
    return SRB_STATUS_PENDING;

} // end IdeReadWrite()


/*++

Routine Description:
    This routine handles IDE Verify.

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:
    SRB status

--*/
ULONG
IdeVerify(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    UCHAR                lChannel = GET_CHANNEL(Srb);
    PHW_CHANNEL          chan = &(deviceExtension->chan[lChannel]);
    PATA_REQ             AtaReq = (PATA_REQ)(Srb->SrbExtension);
    ULONG                ldev = GET_LDEV(Srb);
    PIDE_REGISTERS_1     baseIoAddress1  = deviceExtension->BaseIoAddress1[lChannel];
    PIDE_REGISTERS_2     baseIoAddress2  = deviceExtension->BaseIoAddress2[lChannel];
    ULONG                startingSector;
    ULONG                sectors;
    ULONG                endSector;
    USHORT               sectorCount;
    UCHAR                /*cylinderHigh,cylinderLow,*/drvSelect,sectorNumber;
    USHORT               cylinder;
    ULONG                tmp;
    ULONGLONG            lba;

    // Drive has these number sectors.
    if(!(sectors = (ULONG)(deviceExtension->lun[ldev].NumOfSectors)))
        sectors = deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack *
                  deviceExtension->lun[ldev].IdentifyData.NumberOfHeads *
                  deviceExtension->lun[ldev].IdentifyData.NumberOfCylinders;

    KdPrint2((PRINT_PREFIX 
                "IdeVerify: Total sectors %#x\n",
                sectors));

    // Get starting sector number from CDB.
    MOV_DD_SWP(startingSector, ((PCDB)Srb->Cdb)->CDB10.LBA);

    KdPrint2((PRINT_PREFIX 
                "IdeVerify: Starting sector %#x. Number of blocks %#x\n",
                startingSector,
                ((PCDB)Srb->Cdb)->CDB10.LBA));

    MOV_DW_SWP(sectorCount, ((PCDB)Srb->Cdb)->CDB10.TransferBlocks);
    endSector = startingSector + sectorCount;

    KdPrint2((PRINT_PREFIX 
                "IdeVerify: Ending sector %#x\n",
                endSector));

    if (endSector > sectors) {

        // Too big, round down.
        KdPrint2((PRINT_PREFIX 
                    "IdeVerify: Truncating request to %#x blocks\n",
                    sectors - startingSector - 1));

        sectorCount = (USHORT)(sectors - startingSector - 1);

    } else {

        // Set up sector count register. Round up to next block.
        if (sectorCount > 0xFF) {
            sectorCount = (USHORT)0xFF;
        }
    }

    // Set data buffer pointer and words left.
    AtaReq->DataBuffer = (PUSHORT)Srb->DataBuffer;
    AtaReq->WordsLeft = Srb->DataTransferLength / 2;

    // Indicate expecting an interrupt.
//    chan->ExpectingInterrupt = TRUE;
    InterlockedExchange(&(chan->CheckIntr),
                                  CHECK_INTR_IDLE);

    if(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_LBA_ENABLED) {
        lba = startingSector;
    } else {
        tmp = deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack *
                           deviceExtension->lun[ldev].IdentifyData.NumberOfHeads;
        cylinder =    (USHORT)(startingSector / tmp);
        drvSelect =   (UCHAR)((startingSector % tmp) / deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack); 
        sectorNumber = (UCHAR)(startingSector % deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack) + 1;

        lba = (sectorNumber&0xff) | (((ULONG)cylinder&0xffff)<<8) | (((ULONG)drvSelect&0xf)<<24);
    }

    AtaCommand48(deviceExtension, ldev & 0x01, GET_CHANNEL(Srb),
                 IDE_COMMAND_VERIFY, lba,
                 sectorCount,
                 0, ATA_IMMEDIATE);

    // Wait for interrupt.
    return SRB_STATUS_PENDING;

} // end IdeVerify()


/*++

Routine Description:
    Send ATAPI packet command to device.

Arguments:
    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:

--*/
ULONG
AtapiSendCommand(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN ULONG CmdAction
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    UCHAR                lChannel = GET_CHANNEL(Srb);
    PHW_CHANNEL          chan = &(deviceExtension->chan[lChannel]);
    PATA_REQ             AtaReq = (PATA_REQ)(Srb->SrbExtension);
    ULONG                ldev = GET_LDEV(Srb);
    PATAPI_REGISTERS_1   baseIoAddress1  = (PATAPI_REGISTERS_1)deviceExtension->BaseIoAddress1[lChannel];
    PATAPI_REGISTERS_2   baseIoAddress2 =  (PATAPI_REGISTERS_2)deviceExtension->BaseIoAddress2[lChannel];
    ULONG i;
    ULONG flags;
    UCHAR statusByte,byteCountLow,byteCountHigh;
    BOOLEAN use_dma = FALSE;

    KdPrint2((PRINT_PREFIX "AtapiSendCommand: req state %#x\n", AtaReq->ReqState));
    if(AtaReq->ReqState < REQ_STATE_PREPARE_TO_TRANSFER)
        AtaReq->ReqState = REQ_STATE_PREPARE_TO_TRANSFER;

    if(CmdAction == CMD_ACTION_PREPARE) {
        KdPrint2((PRINT_PREFIX "AtapiSendCommand: CMD_ACTION_PREPARE\n"));
        switch (Srb->Cdb[0]) {
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:
            // all right
            break;
        default:
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: SRB_STATUS_BUSY\n"));
            return SRB_STATUS_BUSY;
        }
        //
        if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_CHANGER_INITED) &&
            !AtaReq->OriginalSrb) {
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: SRB_STATUS_BUSY (2)\n"));
            return SRB_STATUS_BUSY;
        }
    }

    if((CmdAction & CMD_ACTION_PREPARE) &&
       (AtaReq->ReqState != REQ_STATE_READY_TO_TRANSFER)) {

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: prepare...\n"));
        // Set data buffer pointer and words left.
        AtaReq->DataBuffer = (PUSHORT)Srb->DataBuffer;
        AtaReq->WordsLeft = Srb->DataTransferLength / 2;

        // check if reorderable
        switch(Srb->Cdb[0]) {
        case SCSIOP_READ12:
        case SCSIOP_WRITE12:

            MOV_DD_SWP(AtaReq->bcount, ((PCDB)Srb->Cdb)->CDB12READWRITE.NumOfBlocks);
            goto GetLba;

        case SCSIOP_READ:
        case SCSIOP_WRITE:

            MOV_SWP_DW2DD(AtaReq->bcount, ((PCDB)Srb->Cdb)->CDB10.TransferBlocks);
GetLba:
            MOV_DD_SWP(AtaReq->lba, ((PCDB)Srb->Cdb)->CDB10.LBA);

            AtaReq->Flags |= REQ_FLAG_REORDERABLE_CMD;
            AtaReq->Flags &= ~REQ_FLAG_RW_MASK;
            AtaReq->Flags |= (Srb->Cdb[0] == SCSIOP_WRITE || Srb->Cdb[0] == SCSIOP_WRITE12) ?
                              REQ_FLAG_WRITE : REQ_FLAG_READ;
            break;
        }

        // check if DMA read/write
        switch(Srb->Cdb[0]) {
        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
            if(chan->ChannelCtrlFlags & CTRFLAGS_DMA_RO)
                break;
            /* FALLTHROUGH */
        case SCSIOP_READ:
        case SCSIOP_READ12:
            if (/*!EnableDma ||*/
                (deviceExtension->lun[ldev].TransferMode < ATA_DMA)) {
                break;
            }

            KdPrint2((PRINT_PREFIX "AtapiSendCommand: AtapiDmaReinit()\n"));
            AtapiDmaReinit(deviceExtension, ldev, AtaReq);

            if(AtapiDmaSetup(HwDeviceExtension, ldev & 1, lChannel, Srb,
                          (PUCHAR)(AtaReq->DataBuffer),
                          ((Srb->DataTransferLength + 0x1FF) & ~0x1FF))) {
                KdPrint2((PRINT_PREFIX "AtapiSendCommand: use dma\n"));
                use_dma = TRUE;
            }
        }
    
    } else {
        if(AtaReq->Flags & REQ_FLAG_DMA_OPERATION) {
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: use dma (2)\n"));
            use_dma = TRUE;
        }
    }

    if(!(CmdAction & CMD_ACTION_EXEC)) {
        KdPrint2((PRINT_PREFIX "AtapiSendCommand: SRB_STATUS_PENDING\n"));
        return SRB_STATUS_PENDING;
    }
    KdPrint2((PRINT_PREFIX "AtapiSendCommand: CMD_ACTION_EXEC\n"));

    // We need to know how many platters our atapi cd-rom device might have.
    // Before anyone tries to send a srb to our target for the first time,
    // we must "secretly" send down a separate mechanism status srb in order to
    // initialize our device extension changer data.  That's how we know how
    // many platters our target has.

    if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_CHANGER_INITED) &&
        !AtaReq->OriginalSrb) {

        ULONG srbStatus;

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: BuildMechanismStatusSrb()\n"));
        // Set this flag now. If the device hangs on the mech. status
        // command, we will not have the chance to set it.
        deviceExtension->lun[ldev].DeviceFlags |= DFLAGS_CHANGER_INITED;

        chan->MechStatusRetryCount = 3;
        AtaReq->OriginalSrb = Srb;
        AtaReq->Srb = BuildMechanismStatusSrb (
                                        HwDeviceExtension,
                                        Srb);

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: AtapiSendCommand recursive\n"));
        srbStatus = AtapiSendCommand(HwDeviceExtension, AtaReq->Srb, CMD_ACTION_ALL);
        if (srbStatus == SRB_STATUS_PENDING) {
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: SRB_STATUS_PENDING (2)\n"));
            return srbStatus;
        } else {
            AtaReq->Srb = AtaReq->OriginalSrb;
            AtaReq->OriginalSrb = NULL;
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: AtapiHwInitializeChanger()\n"));
            AtapiHwInitializeChanger (HwDeviceExtension, Srb,
                                      (PMECHANICAL_STATUS_INFORMATION_HEADER) NULL);
            // fall out
        }
    }

    KdPrint2((PRINT_PREFIX "AtapiSendCommand: Command %#x to TargetId %d lun %d\n",
               Srb->Cdb[0], Srb->TargetId, Srb->Lun));
    
    // Make sure command is to ATAPI device.
    flags = deviceExtension->lun[ldev].DeviceFlags;
    if (flags & (DFLAGS_SANYO_ATAPI_CHANGER | DFLAGS_ATAPI_CHANGER)) {
        if ((Srb->Lun) > (deviceExtension->lun[ldev].DiscsPresent - 1)) {

            // Indicate no device found at this address.
            AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
            return SRB_STATUS_SELECTION_TIMEOUT;
        }
    } else if (Srb->Lun > 0) {
        AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
        return SRB_STATUS_SELECTION_TIMEOUT;
    }

    if (!(flags & DFLAGS_ATAPI_DEVICE)) {
        AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
        return SRB_STATUS_SELECTION_TIMEOUT;
    }

    // Select device 0 or 1.
    SelectDrive(chan, baseIoAddress1, ldev & 0x1);

    // Verify that controller is ready for next command.
    GetStatus(chan, baseIoAddress2,statusByte);
    KdPrint2((PRINT_PREFIX "AtapiSendCommand: Entered with status %#x\n", statusByte));

    if (statusByte & IDE_STATUS_BUSY) {
        KdPrint2((PRINT_PREFIX "AtapiSendCommand: Device busy (%#x)\n", statusByte));
        AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
        return SRB_STATUS_BUSY;
    }
    if (statusByte & IDE_STATUS_ERROR) {
        if (Srb->Cdb[0] != SCSIOP_REQUEST_SENSE) {

            KdPrint2((PRINT_PREFIX "AtapiSendCommand: Error on entry: (%#x)\n", statusByte));
            // Read the error reg. to clear it and fail this request.
            AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
            return MapError(deviceExtension, Srb);
        }
    }
    // If a tape drive doesn't have DSC set and the last command is restrictive, don't send
    // the next command. See discussion of Restrictive Delayed Process commands in QIC-157.
    if ((!(statusByte & IDE_STATUS_DSC)) &&
          (flags & (DFLAGS_TAPE_DEVICE | DFLAGS_ATAPI_DEVICE)) && chan->RDP) {
        ScsiPortStallExecution(1000);
        KdPrint2((PRINT_PREFIX "AtapiSendCommand: DSC not set. %#x\n",statusByte));
        AtaReq->ReqState = REQ_STATE_QUEUED;
        return SRB_STATUS_PENDING;
    }

    if (IS_RDP(Srb->Cdb[0])) {
        chan->RDP = TRUE;
        KdPrint2((PRINT_PREFIX "AtapiSendCommand: %#x mapped as DSC restrictive\n", Srb->Cdb[0]));
    } else {
        chan->RDP = FALSE;
    }
    if (statusByte & IDE_STATUS_DRQ) {

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: Entered with status (%#x). Attempting to recover.\n",
                    statusByte));
        // Try to drain the data that one preliminary device thinks that it has
        // to transfer. Hopefully this random assertion of DRQ will not be present
        // in production devices.
        for (i = 0; i < 0x10000; i++) {
            GetStatus(chan, baseIoAddress2, statusByte);
            if (statusByte & IDE_STATUS_DRQ) {
                AtapiReadPort2(chan, (PUSHORT)&baseIoAddress1->i.Data);
            } else {
                break;
            }
        }

        if (i == 0x10000) {
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: DRQ still asserted.Status (%#x)\n", statusByte));
            AtapiSoftReset(chan, &baseIoAddress1->ide, ldev & 1);
            KdPrint2((PRINT_PREFIX "AtapiSendCommand: Issued soft reset to Atapi device. \n"));
            // Re-initialize Atapi device.
            IssueIdentify(HwDeviceExtension, ldev & 1, GET_CHANNEL(Srb),
                          IDE_COMMAND_ATAPI_IDENTIFY);
            // Inform the port driver that the bus has been reset.
            ScsiPortNotification(ResetDetected, HwDeviceExtension, 0);
            // Clean up device extension fields that AtapiStartIo won't.
            chan->ExpectingInterrupt = FALSE;
            chan->RDP = FALSE;
            InterlockedExchange(&(deviceExtension->chan[GET_CHANNEL(Srb)].CheckIntr),
                                          CHECK_INTR_IDLE);

            AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
            return SRB_STATUS_BUS_RESET;

        }
    }

    if (flags & (DFLAGS_SANYO_ATAPI_CHANGER | DFLAGS_ATAPI_CHANGER)) {
        // As the cdrom driver sets the LUN field in the cdb, it must be removed.
        Srb->Cdb[1] &= ~0xE0;
        if ((Srb->Cdb[0] == SCSIOP_TEST_UNIT_READY) && (flags & DFLAGS_SANYO_ATAPI_CHANGER)) {
            // Torisan changer. TUR's are overloaded to be platter switches.
            Srb->Cdb[7] = Srb->Lun;
        }
    }

    // SETUP DMA !!!!!

    if(use_dma) {
        chan->ChannelCtrlFlags |= CTRFLAGS_DMA_OPERATION;
    } else {
        chan->ChannelCtrlFlags &= ~CTRFLAGS_DMA_OPERATION;
    }

    statusByte = WaitOnBusy(chan, &baseIoAddress2->ide);

    AtapiWritePort1(chan, &baseIoAddress1->o.Feature,
                            use_dma ? ATA_F_DMA : 0);

    // Write transfer byte count to registers.
    byteCountLow = (UCHAR)(Srb->DataTransferLength & 0xFF);
    byteCountHigh = (UCHAR)(Srb->DataTransferLength >> 8);

    if (Srb->DataTransferLength >= 0x10000) {
        byteCountLow = byteCountHigh = 0xFF;
    }

    AtapiWritePort1(chan, &baseIoAddress1->o.ByteCountLow, byteCountLow);
    AtapiWritePort1(chan, &baseIoAddress1->o.ByteCountHigh, byteCountHigh);

    if (flags & DFLAGS_INT_DRQ) {

        // This device interrupts when ready to receive the packet.

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: Wait for int. to send packet. Status (%#x)\n",
                   statusByte));

        chan->ExpectingInterrupt = TRUE;
        AtaReq->ReqState = REQ_STATE_ATAPI_EXPECTING_CMD_INTR;
        InterlockedExchange(&(chan->CheckIntr),
                                      CHECK_INTR_IDLE);

        // Write ATAPI packet command.
        AtapiWritePort1(chan, &baseIoAddress1->o.Command, IDE_COMMAND_ATAPI_PACKET);

        return SRB_STATUS_PENDING;

    } else {

        KdPrint2((PRINT_PREFIX "AtapiSendCommand: Poll for int. to send packet. Status (%#x)\n",
                   statusByte));

        chan->ExpectingInterrupt = TRUE;
        AtaReq->ReqState = REQ_STATE_ATAPI_DO_NOTHING_INTR;
        InterlockedExchange(&(chan->CheckIntr),
                                      CHECK_INTR_IDLE);

        AtapiDisableInterrupts(deviceExtension, lChannel);

        // Write ATAPI packet command.
        AtapiWritePort1(chan, &baseIoAddress1->o.Command, IDE_COMMAND_ATAPI_PACKET);

        // Wait for DRQ.
        WaitOnBusy(chan, &baseIoAddress2->ide);
        statusByte = WaitForDrq(chan, &baseIoAddress2->ide);

        // Need to read status register and clear interrupt (if any)
        GetBaseStatus(chan, &baseIoAddress1->ide, statusByte);

        AtapiEnableInterrupts(deviceExtension, lChannel);

        if (!(statusByte & IDE_STATUS_DRQ)) {

            KdPrint2((PRINT_PREFIX "AtapiSendCommand: DRQ never asserted (%#x)\n", statusByte));
            AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
            return SRB_STATUS_ERROR;
        }
    }

    // Need to read status register and clear interrupt (if any)
    GetBaseStatus(chan, &baseIoAddress1->ide, statusByte);
    KdPrint2((PRINT_PREFIX "AtapiSendCommand: status (%#x)\n", statusByte));

    // Send CDB to device.
    statusByte = WaitOnBusy(chan, &baseIoAddress2->ide);

    // Indicate expecting an interrupt and wait for it.
    chan->ExpectingInterrupt = TRUE;
    InterlockedExchange(&(chan->CheckIntr),
                                  CHECK_INTR_IDLE);
    AtaReq->ReqState = REQ_STATE_ATAPI_EXPECTING_DATA_INTR;

    WriteBuffer(chan, baseIoAddress1,
                (PUSHORT)Srb->Cdb,
                6);

    KdPrint2((PRINT_PREFIX "AtapiSendCommand: ExpectingInterrupt (%#x)\n", chan->ExpectingInterrupt));

    if(chan->ChannelCtrlFlags & CTRFLAGS_DMA_OPERATION) {
        AtapiDmaStart(HwDeviceExtension, ldev & 1, lChannel, Srb);
    }
    return SRB_STATUS_PENDING;

} // end AtapiSendCommand()


/*++

Routine Description:
    Program ATA registers for IDE disk transfer.

Arguments:
    HwDeviceExtension - ATAPI driver storage.
    Srb - System request block.

Return Value:
    SRB status (pending if all goes well).

--*/

#ifdef DBG
ULONG check_point = 0;
#define SetCheckPoint(cp)  { check_point = (cp) ; }
#else
#define SetCheckPoint(cp)
#endif

ULONG
IdeSendCommand(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN ULONG CmdAction
    )
{
    SetCheckPoint(1);
    KdPrint2((PRINT_PREFIX "** Ide: Command: entryway\n"));
    SetCheckPoint(2);

    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    SetCheckPoint(3);
    UCHAR                lChannel;
    PHW_CHANNEL          chan;
    PIDE_REGISTERS_1     baseIoAddress1;
    PIDE_REGISTERS_2     baseIoAddress2;
    PCDB cdb;

    SetCheckPoint(4);

    UCHAR statusByte,errorByte;
    ULONG status;
    ULONG i;
    PMODE_PARAMETER_HEADER   modeData;
    ULONG ldev;
    PATA_REQ AtaReq;
    SetCheckPoint(5);
    //ULONG __ebp__ = 0;

    SetCheckPoint(0x20);
    KdPrint2((PRINT_PREFIX "** Ide: Command:\n\n"));
/*    __asm {
        mov eax,ebp
        mov __ebp__, eax
    }*/
    /*KdPrint2((PRINT_PREFIX "** Ide: Command EBP %#x, pCdb %#x, cmd %#x\n",
        __ebp__, &(Srb->Cdb[0]), Srb->Cdb[0]));
    KdPrint2((PRINT_PREFIX "** Ide: Command %s\n",
        (CmdAction == CMD_ACTION_PREPARE) ? "Prep " : ""));
    KdPrint2((PRINT_PREFIX "** Ide: Command Srb %#x\n",
        Srb));
    KdPrint2((PRINT_PREFIX "** Ide: Command SrbExt %#x\n",
        Srb->SrbExtension));
    KdPrint2((PRINT_PREFIX "** Ide: Command to device %d\n",
        Srb->TargetId));*/

    SetCheckPoint(0x30);
    AtaReq = (PATA_REQ)(Srb->SrbExtension);

    KdPrint2((PRINT_PREFIX "** Ide: Command &AtaReq %#x\n",
        &AtaReq));
    KdPrint2((PRINT_PREFIX "** Ide: Command AtaReq %#x\n",
        AtaReq));
    KdPrint2((PRINT_PREFIX "** --- **\n"));

    lChannel = GET_CHANNEL(Srb);
    chan = &(deviceExtension->chan[lChannel]);
    baseIoAddress1  = deviceExtension->BaseIoAddress1[lChannel];
    baseIoAddress2  = deviceExtension->BaseIoAddress2[lChannel];
    ldev = GET_LDEV(Srb);

    SetCheckPoint(0x40);
    if(AtaReq->ReqState < REQ_STATE_PREPARE_TO_TRANSFER)
        AtaReq->ReqState = REQ_STATE_PREPARE_TO_TRANSFER;

    if(CmdAction == CMD_ACTION_PREPARE) {
        switch (Srb->Cdb[0]) {
        case SCSIOP_INQUIRY:
        case SCSIOP_READ_CAPACITY:
        case SCSIOP_READ:
        case SCSIOP_WRITE:
        case SCSIOP_REQUEST_SENSE:
            // all right
            KdPrint2((PRINT_PREFIX "** Ide: Command continue prep\n"));
            SetCheckPoint(50);
            break;
        default:
            SetCheckPoint(0);
            KdPrint2((PRINT_PREFIX "** Ide: Command break prep\n"));
            return SRB_STATUS_BUSY;
        }
    }

    SetCheckPoint(0x100 | Srb->Cdb[0]);
    switch (Srb->Cdb[0]) {
    case SCSIOP_INQUIRY:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_INQUIRY PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        // Filter out all TIDs but 0 and 1 since this is an IDE interface
        // which support up to two devices.
        if ((Srb->Lun != 0) ||
            (Srb->PathId >= deviceExtension->NumberChannels) ||
            (Srb->TargetId > 2) /*||
            (!deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT)*/) {

            KdPrint2((PRINT_PREFIX 
                       "IdeSendCommand: SCSIOP_INQUIRY rejected\n"));
            // Indicate no device found at this address.
            status = SRB_STATUS_SELECTION_TIMEOUT;
            break;

        } else {

            KdPrint2((PRINT_PREFIX 
                       "IdeSendCommand: SCSIOP_INQUIRY ok\n"));
            PINQUIRYDATA    inquiryData  = (PINQUIRYDATA)(Srb->DataBuffer);
            PIDENTIFY_DATA2 identifyData = &(deviceExtension->lun[ldev].IdentifyData);


            if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT)) {
                if(!CheckDevice(HwDeviceExtension, lChannel, ldev & 1, FALSE)) {
                    KdPrint2((PRINT_PREFIX 
                               "IdeSendCommand: SCSIOP_INQUIRY rejected (2)\n"));
                    // Indicate no device found at this address.
                    status = SRB_STATUS_SELECTION_TIMEOUT;
                    break;
                }
            }

            // Zero INQUIRY data structure.
            RtlZeroMemory((PCHAR)(Srb->DataBuffer), Srb->DataTransferLength);

            // Standard IDE interface only supports disks.
            inquiryData->DeviceType = DIRECT_ACCESS_DEVICE;

            // Set the removable bit, if applicable.
            if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_REMOVABLE_DRIVE) {
                inquiryData->RemovableMedia = 1;
            }
            // Set the Relative Addressing (LBA) bit, if applicable.
            if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_LBA_ENABLED) {
                inquiryData->RelativeAddressing = 1;
            }
            // Set the CommandQueue bit
            inquiryData->CommandQueue = 1;

            // Fill in vendor identification fields.
            for (i = 0; i < 20; i += 2) {
               inquiryData->VendorId[i] =
                   ((PUCHAR)identifyData->ModelNumber)[i + 1];
               inquiryData->VendorId[i+1] =
                   ((PUCHAR)identifyData->ModelNumber)[i];
            }

            // Initialize unused portion of product id.
            for (i = 0; i < 4; i++) {
               inquiryData->ProductId[12+i] = ' ';
            }

            // Move firmware revision from IDENTIFY data to
            // product revision in INQUIRY data.
            for (i = 0; i < 4; i += 2) {
               inquiryData->ProductRevisionLevel[i] =
                   ((PUCHAR)identifyData->FirmwareRevision)[i+1];
               inquiryData->ProductRevisionLevel[i+1] =
                   ((PUCHAR)identifyData->FirmwareRevision)[i];
            }

            status = SRB_STATUS_SUCCESS;
        }

        break;

    case SCSIOP_MODE_SENSE:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_MODE_SENSE PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        // This is used to determine of the media is write-protected.
        // Since IDE does not support mode sense then we will modify just the portion we need
        // so the higher level driver can determine if media is protected.
        if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED) {

            SelectDrive(chan, baseIoAddress1, ldev & 0x1);
            AtapiWritePort1(chan, &baseIoAddress1->o.Command,IDE_COMMAND_GET_MEDIA_STATUS);
            statusByte = WaitOnBusy(chan, baseIoAddress2);

            if (!(statusByte & IDE_STATUS_ERROR)){

                // no error occured return success, media is not protected
                chan->ExpectingInterrupt = FALSE;
                InterlockedExchange(&(chan->CheckIntr),
                                              CHECK_INTR_IDLE);
                status = SRB_STATUS_SUCCESS;

            } else {

                // error occured, handle it locally, clear interrupt
                errorByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);

                GetBaseStatus(chan, baseIoAddress1, statusByte);
                chan->ExpectingInterrupt = FALSE;
                InterlockedExchange(&(chan->CheckIntr),
                                              CHECK_INTR_IDLE);
                status = SRB_STATUS_SUCCESS;

                if (errorByte & IDE_ERROR_DATA_ERROR) {

                   //media is write-protected, set bit in mode sense buffer
                   modeData = (PMODE_PARAMETER_HEADER)Srb->DataBuffer;

                   Srb->DataTransferLength = sizeof(MODE_PARAMETER_HEADER);
                   modeData->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
                }
            }
            status = SRB_STATUS_SUCCESS;
        } else {
            status = SRB_STATUS_INVALID_REQUEST;
        }
        break;

    case SCSIOP_TEST_UNIT_READY:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_TEST_UNIT_READY PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED) {

            // Select device 0 or 1.
            SelectDrive(chan, baseIoAddress1, ldev & 0x1);
            AtapiWritePort1(chan, &baseIoAddress1->o.Command,IDE_COMMAND_GET_MEDIA_STATUS);

            // Wait for busy. If media has not changed, return success
            statusByte = WaitOnBusy(chan, baseIoAddress2);

            if (!(statusByte & IDE_STATUS_ERROR)){
                chan->ExpectingInterrupt = FALSE;
                InterlockedExchange(&(chan->CheckIntr),
                                              CHECK_INTR_IDLE);
                status = SRB_STATUS_SUCCESS;
            } else {
                errorByte = AtapiReadPort1(chan, &baseIoAddress1->i.Error);
                if (errorByte == IDE_ERROR_DATA_ERROR){

                    // Special case: If current media is write-protected,
                    // the 0xDA command will always fail since the write-protect bit
                    // is sticky,so we can ignore this error
                    GetBaseStatus(chan, baseIoAddress1, statusByte);
                    chan->ExpectingInterrupt = FALSE;
                    InterlockedExchange(&(chan->CheckIntr),
                                                  CHECK_INTR_IDLE);
                    status = SRB_STATUS_SUCCESS;

                } else {

                    // Request sense buffer to be build
                    chan->ExpectingInterrupt = TRUE;
                    InterlockedExchange(&(chan->CheckIntr),
                                                  CHECK_INTR_IDLE);
                    status = SRB_STATUS_PENDING;
               }
            }
        } else {
            status = SRB_STATUS_SUCCESS;
        }

        break;

    case SCSIOP_READ_CAPACITY:

        KdPrint2((PRINT_PREFIX 
                   "** IdeSendCommand: SCSIOP_INQUIRY PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        // Claim 512 byte blocks (big-endian).
        ((PREAD_CAPACITY_DATA)Srb->DataBuffer)->BytesPerBlock = 0x20000;

        // Calculate last sector.
        if(!(i = (ULONG)deviceExtension->lun[ldev].NumOfSectors)) {
            i = deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack *
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads *
                deviceExtension->lun[ldev].IdentifyData.NumberOfCylinders;
        }
        i--;

        ((PREAD_CAPACITY_DATA)Srb->DataBuffer)->LogicalBlockAddress =
            (((PUCHAR)&i)[0] << 24) |  (((PUCHAR)&i)[1] << 16) |
            (((PUCHAR)&i)[2] << 8) | ((PUCHAR)&i)[3];

        KdPrint2((PRINT_PREFIX 
                   "** IDE disk %#x - #sectors %#x, #heads %#x, #cylinders %#x\n",
                   Srb->TargetId,
                   deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack,
                   deviceExtension->lun[ldev].IdentifyData.NumberOfHeads,
                   deviceExtension->lun[ldev].IdentifyData.NumberOfCylinders));


        status = SRB_STATUS_SUCCESS;
        break;

    case SCSIOP_VERIFY:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_VERIFY PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        status = IdeVerify(HwDeviceExtension,Srb);

        break;

    case SCSIOP_READ:
    case SCSIOP_WRITE:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_READ/SCSIOP_WRITE PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        AtaReq->Flags &= ~REQ_FLAG_RW_MASK;
        AtaReq->Flags |= (Srb->Cdb[0] == SCSIOP_WRITE) ? REQ_FLAG_WRITE : REQ_FLAG_READ;
        status = IdeReadWrite(HwDeviceExtension,
                              Srb, CmdAction);
        break;

    case SCSIOP_START_STOP_UNIT:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_START_STOP_UNIT PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        //Determine what type of operation we should perform
        cdb = (PCDB)Srb->Cdb;

        if (cdb->START_STOP.LoadEject == 1){

            // Eject media,
            // first select device 0 or 1.
            SelectDrive(chan, baseIoAddress1, ldev & 0x1);
            AtapiWritePort1(chan, &baseIoAddress1->o.Command,IDE_COMMAND_MEDIA_EJECT);
        }
        status = SRB_STATUS_SUCCESS;
        break;

    // Note: I don't implement this, because NTFS driver too often issues this command
    // It is awful performance degrade. However, if somebody wants, I will implement
    // SCSIOP_FLUSH_BUFFER/SCSIOP_SYNCHRONIZE_CACHE optionally.

#if 0
    case SCSIOP_FLUSH_BUFFER:
    case SCSIOP_SYNCHRONIZE_CACHE:

        SelectDrive(chan, baseIoAddress1, ldev & 0x1);
        AtapiWritePort1(chan, &baseIoAddress1->o.Command,IDE_COMMAND_FLUSH_CACHE);
        status = SRB_STATUS_SUCCESS;
//        status = SRB_STATUS_PENDING;
        statusByte = WaitOnBusy(chan, baseIoAddress2);
        break;
#endif

    case SCSIOP_REQUEST_SENSE:
        // this function makes sense buffers to report the results
        // of the original GET_MEDIA_STATUS command

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: SCSIOP_REQUEST_SENSE PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->PathId, Srb->Lun, Srb->TargetId));
        if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED) {
            status = IdeBuildSenseBuffer(HwDeviceExtension,Srb);
            break;
        }

    default:

        KdPrint2((PRINT_PREFIX 
                   "IdeSendCommand: Unsupported command %#x\n",
                   Srb->Cdb[0]));

        status = SRB_STATUS_INVALID_REQUEST;

    } // end switch

    if(status == SRB_STATUS_PENDING) {
        KdPrint2((PRINT_PREFIX "IdeSendCommand: SRB_STATUS_PENDING\n"));
        if(CmdAction & CMD_ACTION_EXEC) {
            KdPrint2((PRINT_PREFIX "IdeSendCommand: REQ_STATE_EXPECTING_INTR\n"));
            AtaReq->ReqState = REQ_STATE_EXPECTING_INTR;
        }
    } else {
        KdPrint2((PRINT_PREFIX "IdeSendCommand: REQ_STATE_TRANSFER_COMPLETE\n"));
        AtaReq->ReqState = REQ_STATE_TRANSFER_COMPLETE;
    }

    return status;

} // end IdeSendCommand()


/*++

Routine Description:
    Enables disables media status notification

Arguments:
    HwDeviceExtension - ATAPI driver storage.

--*/
VOID
IdeMediaStatus(
    BOOLEAN EnableMSN,
    IN PVOID HwDeviceExtension,
    UCHAR ldev
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PIDE_REGISTERS_1     baseIoAddress;
    PHW_CHANNEL          chan;
    UCHAR lChannel = ldev >> 1;
    UCHAR statusByte,errorByte;

    baseIoAddress = deviceExtension->BaseIoAddress1[lChannel];

    chan = &(deviceExtension->chan[lChannel]);

    if (EnableMSN == TRUE){

        // If supported enable Media Status Notification support
        if ((deviceExtension->lun[ldev].DeviceFlags & DFLAGS_REMOVABLE_DRIVE)) {

            // enable
            statusByte = AtaCommand(deviceExtension, ldev & 1, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                0, ATA_C_F_ENAB_MEDIASTAT, ATA_WAIT_BASE_READY);

            if (statusByte & IDE_STATUS_ERROR) {
                // Read the error register.
                errorByte = AtapiReadPort1(chan, &baseIoAddress->i.Error);

                KdPrint2((PRINT_PREFIX 
                            "IdeMediaStatus: Error enabling media status. Status %#x, error byte %#x\n",
                             statusByte,
                             errorByte));
            } else {
                deviceExtension->lun[ldev].DeviceFlags |= DFLAGS_MEDIA_STATUS_ENABLED;
                KdPrint2((PRINT_PREFIX "IdeMediaStatus: Media Status Notification Supported\n"));
                chan->ReturningMediaStatus = 0;

            }

        }
    } else { // end if EnableMSN == TRUE

        // disable if previously enabled
        if ((deviceExtension->lun[ldev].DeviceFlags & DFLAGS_MEDIA_STATUS_ENABLED)) {

            statusByte = AtaCommand(deviceExtension, ldev & 1, lChannel,
                                IDE_COMMAND_SET_FEATURES, 0, 0, 0,
                                0, ATA_C_F_DIS_MEDIASTAT, ATA_WAIT_BASE_READY);
            deviceExtension->lun[ldev].DeviceFlags &= ~DFLAGS_MEDIA_STATUS_ENABLED;
        }


    }


} // end IdeMediaStatus()


/*++

Routine Description:

    Builts an artificial sense buffer to report the results of a GET_MEDIA_STATUS
    command. This function is invoked to satisfy the SCSIOP_REQUEST_SENSE.
Arguments:

    HwDeviceExtension - ATAPI driver storage.
    Srb - System request block.

Return Value:

    SRB status (ALWAYS SUCCESS).

--*/
ULONG
IdeBuildSenseBuffer(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
//    ULONG status;
    PSENSE_DATA  senseBuffer = (PSENSE_DATA)Srb->DataBuffer;
    UCHAR ReturningMediaStatus = deviceExtension->chan[GET_CHANNEL(Srb)].ReturningMediaStatus;

    if (senseBuffer){

        if(ReturningMediaStatus & IDE_ERROR_MEDIA_CHANGE) {

            senseBuffer->ErrorCode = 0x70;
            senseBuffer->Valid     = 1;
            senseBuffer->AdditionalSenseLength = 0xb;
            senseBuffer->SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
            senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_MEDIUM_CHANGED;
            senseBuffer->AdditionalSenseCodeQualifier = 0;
        } else if(ReturningMediaStatus & IDE_ERROR_MEDIA_CHANGE_REQ) {

            senseBuffer->ErrorCode = 0x70;
            senseBuffer->Valid     = 1;
            senseBuffer->AdditionalSenseLength = 0xb;
            senseBuffer->SenseKey =  SCSI_SENSE_UNIT_ATTENTION;
            senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_MEDIUM_CHANGED;
            senseBuffer->AdditionalSenseCodeQualifier = 0;
        } else if(ReturningMediaStatus & IDE_ERROR_END_OF_MEDIA) {

            senseBuffer->ErrorCode = 0x70;
            senseBuffer->Valid     = 1;
            senseBuffer->AdditionalSenseLength = 0xb;
            senseBuffer->SenseKey =  SCSI_SENSE_NOT_READY;
            senseBuffer->AdditionalSenseCode = SCSI_ADSENSE_NO_MEDIA_IN_DEVICE;
            senseBuffer->AdditionalSenseCodeQualifier = 0;
        } else if(ReturningMediaStatus & IDE_ERROR_DATA_ERROR) {

            senseBuffer->ErrorCode = 0x70;
            senseBuffer->Valid     = 1;
            senseBuffer->AdditionalSenseLength = 0xb;
            senseBuffer->SenseKey =  SCSI_SENSE_DATA_PROTECT;
            senseBuffer->AdditionalSenseCode = 0;
            senseBuffer->AdditionalSenseCodeQualifier = 0;
        }
        return SRB_STATUS_SUCCESS;
    }
    return SRB_STATUS_ERROR;

}// End of IdeBuildSenseBuffer

/*++

Routine Description:

    This routine is called from the SCSI port driver synchronized
    with the kernel to start an IO request.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Srb - IO request packet

Return Value:

    TRUE

--*/
BOOLEAN
AtapiStartIo(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    return AtapiStartIo__(HwDeviceExtension, Srb, TRUE);
}

BOOLEAN
AtapiStartIo__(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN BOOLEAN TopLevel
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    UCHAR                lChannel;
    PHW_CHANNEL          chan;
    ULONG status;
    ULONG ldev;
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    PATA_REQ AtaReq;
    PSCSI_REQUEST_BLOCK tmpSrb;

    BMList[deviceExtension->DevIndex].Isr2Enable = TRUE;
//    deviceExtension->QueueNewIrql = max(deviceExtension->QueueNewIrql, KeGetCurrentIrql());

/*                KeBugCheckEx(0xc000000e,
                             (Srb->PathId<<16) | (Srb->TargetId<<8) | (Srb->Lun),
                             Srb->Function, 
                             TopLevel, 0x80000001);
*/
    do {

        lChannel = GET_CHANNEL(Srb);
        chan = &(deviceExtension->chan[lChannel]);
        ldev = GET_LDEV(Srb);

        KdPrint2((PRINT_PREFIX 
                   "** AtapiStartIo: Function %#x, PATH:LUN:TID = %#x:%#x:%#x\n",
                   Srb->Function, Srb->PathId, Srb->Lun, Srb->TargetId));

//        if(Srb->PathId == 1) {
//            __asm int 3;
//        }

        // Determine which function.
        switch (Srb->Function) {

        case SRB_FUNCTION_EXECUTE_SCSI:

            if(!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT)) {

                //if(!CheckDevice(HwDeviceExtension, lChannel, ldev & 1, FALSE)) {
                    KdPrint2((PRINT_PREFIX 
                               "IdeSendCommand: SCSIOP_INQUIRY rejected (2)\n"));
                    // Indicate no device found at this address.
                    KdPrint2((PRINT_PREFIX "SRB_STATUS_SELECTION_TIMEOUT\n"));
                    status = SRB_STATUS_SELECTION_TIMEOUT;
                    break;
                //}
            }

            if(TopLevel) {
                KdPrint2((PRINT_PREFIX "TopLevel\n"));
                RtlZeroMemory(Srb->SrbExtension, sizeof(ATA_REQ));
            }

            if(TopLevel && chan->queue_depth > 0) {

                KdPrint2((PRINT_PREFIX "Non-empty queue\n"));
                if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE) {
                    KdPrint2((PRINT_PREFIX "Try ATAPI prepare\n"));
                    status = AtapiSendCommand(HwDeviceExtension, Srb, CMD_ACTION_PREPARE);
                } else {
                    KdPrint2((PRINT_PREFIX "Try IDE prepare\n"));
                    status = IdeSendCommand(HwDeviceExtension, Srb, CMD_ACTION_PREPARE);
                }
                /*KeBugCheckEx(0xc000000e,
                             (Srb->PathId<<16) | (Srb->TargetId<<8) | (Srb->Lun),
                             Srb->Function, 
                             status, 0x80000001);*/
                if(status == SRB_STATUS_BUSY)
                    status = SRB_STATUS_PENDING;
                // Insert requests AFTER they have been initialized on
                // CMD_ACTION_PREPARE stage
                // we should not check TopLevel here (it is always TRUE)
                AtapiQueueRequest(chan, Srb);

                KdPrint2((PRINT_PREFIX "AtapiStartIo: Already have %d request(s)!\n", chan->queue_depth));

            } else {
                // Send command to device.
                KdPrint2((PRINT_PREFIX "Send to device\n"));
                if(TopLevel) {
                    KdPrint2((PRINT_PREFIX "TopLevel (2), srb %#x\n", Srb));
                    AtaReq = (PATA_REQ)(Srb->SrbExtension);
                    KdPrint2((PRINT_PREFIX "TopLevel (3), AtaReq %#x\n", AtaReq));
                    ASSERT(!AtaReq->Flags);
                    AtapiQueueRequest(chan, Srb);
//                    AtaReq = (PATA_REQ)(Srb->SrbExtension);
                    ASSERT(!AtaReq->Flags);
                    AtaReq->ReqState = REQ_STATE_QUEUED;
                    ASSERT(!AtaReq->Flags);
                }
                if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE) {
                    KdPrint2((PRINT_PREFIX "Try ATAPI send\n"));
                    status = AtapiSendCommand(HwDeviceExtension, Srb, CMD_ACTION_ALL);
                } else {
                    KdPrint2((PRINT_PREFIX "Try IDE send\n"));
/*                    {
                        ULONG __ebp__ = 0;
                        ULONG __esp__ = 0;

                        KdPrint2((PRINT_PREFIX "** before IdeSendCommand:\n"));
                        __asm {
                            mov eax,ebp
                            mov __ebp__, eax
                            mov eax,esp
                            mov __esp__, eax
                        }
                        KdPrint2((PRINT_PREFIX "** before Ide: EBP:%#x ESP:%#x\n", __ebp__, __esp__));
                    }*/
                    status = IdeSendCommand(HwDeviceExtension, Srb, CMD_ACTION_ALL);
                }
/*                KeBugCheckEx(0xc000000e,
                             (Srb->PathId<<16) | (Srb->TargetId<<8) | (Srb->Lun),
                             Srb->Function, 
                             status, 0x80000002);*/

            }
            TopLevel = FALSE;

            break;

        case SRB_FUNCTION_ABORT_COMMAND:

            tmpSrb = ScsiPortGetSrb(HwDeviceExtension, Srb->PathId, Srb->TargetId, Srb->Lun,
                               Srb->QueueTag);
            // Verify that SRB to abort is still outstanding.
            if((tmpSrb != Srb->NextSrb) ||
               !chan->queue_depth) {

                KdPrint2((PRINT_PREFIX "AtapiStartIo: SRB to abort already completed\n"));

                // Complete abort SRB.
                status = SRB_STATUS_ABORT_FAILED;
                break;
            }

            AtaReq = (PATA_REQ)(tmpSrb->SrbExtension);
            if(AtaReq->ReqState > REQ_STATE_READY_TO_TRANSFER) {
                if (!AtapiResetController__(deviceExtension, lChannel, FALSE)) {
                      KdPrint2((PRINT_PREFIX "AtapiStartIo: Abort command failed\n"));
                    // Log reset failure.
                    ScsiPortLogError(HwDeviceExtension, NULL, 0, 0, 0, SP_INTERNAL_ADAPTER_ERROR, 5 << 8);
                    status = SRB_STATUS_ERROR;

                } else {
                    status = SRB_STATUS_SUCCESS;
                }
            } else {
                KdPrint2((PRINT_PREFIX "AtapiInterrupt: remove aborted srb %#x\n", tmpSrb));
                if (tmpSrb->SenseInfoBuffer &&
                    tmpSrb->SenseInfoBufferLength >= sizeof(SENSE_DATA)) {

                    PSENSE_DATA  senseBuffer = (PSENSE_DATA)tmpSrb->SenseInfoBuffer;

                    senseBuffer->ErrorCode = 0;
                    senseBuffer->Valid     = 1;
                    senseBuffer->AdditionalSenseLength = 0xb;
                    senseBuffer->SenseKey =  SCSI_SENSE_ABORTED_COMMAND;
                    senseBuffer->AdditionalSenseCode = 0;
                    senseBuffer->AdditionalSenseCodeQualifier = 0;

                    tmpSrb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
                }
                AtapiRemoveRequest(chan, tmpSrb);
                // Indicate command complete.
                ScsiPortNotification(RequestComplete,
                                     deviceExtension,
                                     tmpSrb);
                status = SRB_STATUS_SUCCESS;
            }
            break;

            // Abort function indicates that a request timed out.
            // Call reset routine. Card will only be reset if
            // status indicates something is wrong.
            // Fall through to reset code.

        case SRB_FUNCTION_RESET_BUS:

            // Reset Atapi and SCSI bus.
            KdPrint2((PRINT_PREFIX "AtapiStartIo: Reset bus request received\n"));
            if (!AtapiResetController__(deviceExtension, lChannel, TRUE)) {
                  KdPrint2((PRINT_PREFIX "AtapiStartIo: Reset bus failed\n"));
                // Log reset failure.
                ScsiPortLogError(HwDeviceExtension, NULL, 0, 0, 0, SP_INTERNAL_ADAPTER_ERROR, 5 << 8);
                status = SRB_STATUS_ERROR;

            } else {
                status = SRB_STATUS_SUCCESS;
            }

            break;

    /*    case SRB_FUNCTION_SHUTDOWN:
        case SRB_FUNCTION_FLUSH:

            // Flush device's cache.
            KdPrint2((PRINT_PREFIX "AtapiStartIo: Device flush received\n"));

            if (chan->CurrentSrb) {

                KdPrint2((PRINT_PREFIX "AtapiStartIo (SRB_FUNCTION_FLUSH): Already have a request!\n"));
                Srb->SrbStatus = SRB_STATUS_BUSY;
                ScsiPortNotification(RequestComplete,
                                     deviceExtension,
                                     Srb);
                return FALSE;
            }

            if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE) {
                status = SRB_STATUS_SUCCESS;
            } else {
                status = AtaCommand(deviceExtension, ldev & 1, GET_CHANNEL(Srb),
                           IDE_COMMAND_FLUSH_CACHE, 0, 0, 0, 0, 0, ATA_WAIT_INTR);
                if (status & IDE_STATUS_DRQ) {
                    status = SRB_STATUS_SUCCESS;
                } else {
                    status = SRB_STATUS_SELECTION_TIMEOUT;
                }
            }
            break;*/

        case SRB_FUNCTION_IO_CONTROL:

            if(TopLevel) {
                AtapiQueueRequest(chan, Srb);
                AtaReq = (PATA_REQ)(Srb->SrbExtension);
                AtaReq->ReqState = REQ_STATE_QUEUED;
            }

            if(TopLevel && chan->queue_depth > 1) {

                status = SRB_STATUS_PENDING;
                KdPrint2((PRINT_PREFIX "AtapiStartIo: Already have a request! (2)\n"));
                break;
            }
            TopLevel = FALSE;

            if (AtapiStringCmp( (PCHAR)(((PSRB_IO_CONTROL)(Srb->DataBuffer))->Signature),"SCSIDISK",sizeof("SCSIDISK")-1)) {

                KdPrint2((PRINT_PREFIX "AtapiStartIo: IoControl signature incorrect. Send %s, expected %s\n",
                            ((PSRB_IO_CONTROL)(Srb->DataBuffer))->Signature,
                            "SCSIDISK"));

                status = SRB_STATUS_INVALID_REQUEST;
                break;
            }

            switch (((PSRB_IO_CONTROL)(Srb->DataBuffer))->ControlCode) {

                case IOCTL_SCSI_MINIPORT_SMART_VERSION: {

                    PGETVERSIONINPARAMS versionParameters = (PGETVERSIONINPARAMS)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));
                    UCHAR deviceNumber;

                    // Version and revision per SMART 1.03

                    versionParameters->bVersion = 1;
                    versionParameters->bRevision = 1;
                    versionParameters->bReserved = 0;

                    // Indicate that support for IDE IDENTIFY, ATAPI IDENTIFY and SMART commands.
                    versionParameters->fCapabilities = (CAP_ATA_ID_CMD | CAP_ATAPI_ID_CMD | CAP_SMART_CMD);

                    // This is done because of how the IOCTL_SCSI_MINIPORT
                    // determines 'targetid's'. Disk.sys places the real target id value
                    // in the DeviceMap field. Once we do some parameter checking, the value passed
                    // back to the application will be determined.

                    deviceNumber = versionParameters->bIDEDeviceMap;

                    if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT) ||
                        (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

                        status = SRB_STATUS_SELECTION_TIMEOUT;
                        break;
                    }

                    // NOTE: This will only set the bit
                    // corresponding to this drive's target id.
                    // The bit mask is as follows:
                    //
                    //     -Sec Pri
                    //     S M S M
                    //     3 2 1 0

                    if (deviceExtension->NumberChannels == 1) {
                        if (chan->PrimaryAddress) {
                            deviceNumber = 1 << ldev;
                        } else {
                            deviceNumber = 4 << ldev;
                        }
                    } else {
                        deviceNumber = 1 << ldev;
                    }

                    versionParameters->bIDEDeviceMap = deviceNumber;

                    status = SRB_STATUS_SUCCESS;
                    break;
                }

                case IOCTL_SCSI_MINIPORT_IDENTIFY: {

                    PSENDCMDOUTPARAMS cmdOutParameters = (PSENDCMDOUTPARAMS)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));
                    SENDCMDINPARAMS   cmdInParameters = *(PSENDCMDINPARAMS)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));
                    ULONG             i;
                    UCHAR             targetId;

                    if (cmdInParameters.irDriveRegs.bCommandReg == ID_CMD) {

                        // Extract the target.
                        targetId = cmdInParameters.bDriveNumber;

                        if (!(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT) ||
                             (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

                            status = SRB_STATUS_SELECTION_TIMEOUT;
                            break;
                        }

                        // Zero the output buffer
                        for (i = 0; i < (sizeof(SENDCMDOUTPARAMS) + IDENTIFY_BUFFER_SIZE - 1); i++) {
                            ((PUCHAR)cmdOutParameters)[i] = 0;
                        }

                        // Build status block.
                        cmdOutParameters->cBufferSize = IDENTIFY_BUFFER_SIZE;
                        cmdOutParameters->DriverStatus.bDriverError = 0;
                        cmdOutParameters->DriverStatus.bIDEError = 0;

                        // Extract the identify data from the device extension.
                        ScsiPortMoveMemory (cmdOutParameters->bBuffer, &deviceExtension->lun[targetId].IdentifyData, IDENTIFY_DATA_SIZE);

                        status = SRB_STATUS_SUCCESS;


                    } else {
                        status = SRB_STATUS_INVALID_REQUEST;
                    }
                    break;
                }

                case  IOCTL_SCSI_MINIPORT_READ_SMART_ATTRIBS:
                case  IOCTL_SCSI_MINIPORT_READ_SMART_THRESHOLDS:
                case  IOCTL_SCSI_MINIPORT_ENABLE_SMART:
                case  IOCTL_SCSI_MINIPORT_DISABLE_SMART:
                case  IOCTL_SCSI_MINIPORT_RETURN_STATUS:
                case  IOCTL_SCSI_MINIPORT_ENABLE_DISABLE_AUTOSAVE:
                case  IOCTL_SCSI_MINIPORT_SAVE_ATTRIBUTE_VALUES:
                case  IOCTL_SCSI_MINIPORT_EXECUTE_OFFLINE_DIAGS:

                    status = IdeSendSmartCommand(HwDeviceExtension,Srb);
                    break;

                case  IOCTL_SCSI_MINIPORT_UNIATA_FIND_DEVICES:

                    KdPrint2((PRINT_PREFIX "AtapiStartIo: rescan bus\n"));
                    FindDevices(HwDeviceExtension, FALSE, Srb->PathId);
                    status = SRB_STATUS_SUCCESS;
                    break;

                case  IOCTL_SCSI_MINIPORT_UNIATA_DELETE_DEVICE: {

                    KdPrint2((PRINT_PREFIX "AtapiStartIo: remove %#x:%#x\n", Srb->PathId, Srb->TargetId));
                    ULONG ldev = GET_LDEV2(Srb->PathId, Srb->TargetId, 0);

                    deviceExtension->lun[ldev].DeviceFlags = 0;
                    status = SRB_STATUS_SUCCESS;
                    break;
                }
                case  IOCTL_SCSI_MINIPORT_UNIATA_SET_MAX_MODE: {

                    PSETTRANSFERMODE  setTransferMode = (PSETTRANSFERMODE)(((PUCHAR)Srb->DataBuffer) + sizeof(SRB_IO_CONTROL));

/*                    deviceExtension->lun[targetId].TransferMode =
                    deviceExtension->lun[targetId].OrigTransferMode = (UCHAR)(setTransferMode->Mode);*/
                    status = SRB_STATUS_SUCCESS;
                    break;
                }
                default :

                    status = SRB_STATUS_INVALID_REQUEST;
                    break;

            }

            break;

        default:

            // Indicate unsupported command.
            status = SRB_STATUS_INVALID_REQUEST;

//            break;

        } // end switch

        PathId   = Srb->PathId;
        TargetId = Srb->TargetId;
        Lun      = Srb->Lun;

        if (status != SRB_STATUS_PENDING) {

            KdPrint2((PRINT_PREFIX 
                       "AtapiStartIo: Srb %#x complete with status %#x\n",
                       Srb,
                       status));

            // Set status in SRB.
            Srb->SrbStatus = (UCHAR)status;

            AtapiRemoveRequest(chan, Srb);
            // Indicate command complete.
            ScsiPortNotification(RequestComplete,
                                 deviceExtension,
                                 Srb);

            // Remove current Srb & get next one
            if(Srb = AtapiGetCurRequest(chan)) {
                AtaReq = (PATA_REQ)(Srb->SrbExtension);
                if(AtaReq->ReqState > REQ_STATE_QUEUED) {
                    // current request is under precessing, thus
                    // we should do nothing here
                    Srb = NULL;
                }
            }
        }

    } while (Srb && (status != SRB_STATUS_PENDING));


    // Indicate ready for next request.
    ScsiPortNotification(NextRequest,
                         deviceExtension,
                         NULL);

    ScsiPortNotification(NextLuRequest,
                         deviceExtension, 
                         PathId,
                         TargetId,
                         Lun);

    return TRUE;

} // end AtapiStartIo__()


/*++

Routine Description:

    Installable driver initialization entry point for system.

Arguments:

    Driver Object

Return Value:

    Status from ScsiPortInitialize()

--*/
ULONG
DriverEntry(
    IN PVOID DriverObject,
    IN PVOID Argument2
    )
{
    HW_INITIALIZATION_DATA_COMMON hwInitializationData;
    ULONG                  adapterCount;
    ULONG                  i, c;
    ULONG                  statusToReturn, newStatus;
    PUNICODE_STRING        RegistryPath = (PUNICODE_STRING)Argument2;
    BOOLEAN                ReEnter = FALSE;
    WCHAR                  a;

    KdPrint2((PRINT_PREFIX "\n\nATAPI IDE MiniPort Driver\n"));

//    __asm int 3;

    // get OS version
    PsGetVersion(&MajorVersion, &MinorVersion, &BuildNumber, &SavedSPString);
    SavedSPString.Length = min(255*sizeof(WCHAR), SavedSPString.Length);
    SavedSPString.MaximumLength = 255*sizeof(WCHAR);
    RtlCopyMemory(SavedSPStringBuffer, SavedSPString.Buffer, SavedSPString.Length);
    SavedSPString.Buffer = (PWCHAR)&SavedSPStringBuffer;
    SavedSPString.Buffer[SavedSPString.Length/sizeof(WCHAR)] = 0;
    KdPrint2((PRINT_PREFIX "\n\nOS Version: %x.%x (%d), %ws\n",
        MajorVersion, MinorVersion, BuildNumber, SavedSPString.Buffer));

    for(i=SavedSPString.Length/sizeof(WCHAR)-1; i>0; i--) {
        a = SavedSPString.Buffer[i];
        if(a >= '0' && a <= '9') {
            SPVersion = (a - '0') << 8;
            if(i != SavedSPString.Length/sizeof(WCHAR)-1) {
                SPVersion++; // (0x0600 -> 0x0601) for sp6a
            }
            break;
        }
    }
    KdPrint2((PRINT_PREFIX "ServicePack %d\n", SPVersion >> 8));

    // get registry path to settings
    RtlCopyMemory(&SavedRegPath, RegistryPath, sizeof(UNICODE_STRING));
    SavedRegPath.Buffer = (PWCHAR)&SavedRegPathBuffer;
    SavedRegPath.Length = min(RegistryPath->Length, 255*sizeof(WCHAR));
    SavedRegPath.MaximumLength = 255*sizeof(WCHAR);
    RtlCopyMemory(SavedRegPath.Buffer, RegistryPath->Buffer, SavedRegPath.Length);
    SavedRegPath.Buffer[SavedRegPath.Length/sizeof(WCHAR)] = 0;

    if(!SavedDriverObject) {
        SavedDriverObject = (PDRIVER_OBJECT)DriverObject;
    } else {
        ReEnter = TRUE;
    }

    SkipRaids = AtapiRegCheckDevValue(NULL, L"SkipRaids", 1);

    statusToReturn = 0xffffffff;

//    __asm int 3;

    // Zero out structure.
    RtlZeroMemory(((PCHAR)&hwInitializationData), sizeof(hwInitializationData));

    // Set size of hwInitializationData.
    hwInitializationData.comm.HwInitializationDataSize =
      sizeof(hwInitializationData.comm) +
      (WinVer_IsNT ? 0 : sizeof(hwInitializationData.w2k));

    // Set entry points.
    hwInitializationData.comm.HwInitialize = AtapiHwInitialize;
    hwInitializationData.comm.HwResetBus = AtapiResetController;
    hwInitializationData.comm.HwStartIo = AtapiStartIo;
    hwInitializationData.comm.HwInterrupt = AtapiInterrupt;

    // Specify size of extensions.
    hwInitializationData.comm.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitializationData.comm.SpecificLuExtensionSize = sizeof(HW_LU_EXTENSION);
    hwInitializationData.comm.SrbExtensionSize = sizeof(ATA_REQ);

    // Indicate PIO device.
    hwInitializationData.comm.MapBuffers = TRUE;
    if(!WinVer_IsNT) {
        hwInitializationData.comm.NeedPhysicalAddresses = TRUE;
        hwInitializationData.w2k.HwAdapterControl = AtapiAdapterControl;
    }

    KdPrint2((PRINT_PREFIX "\n\nATAPI IDE enum supported BusMaster Devices\n"));

    if(!ReEnter)
        AtapiEnumBusMasterController(DriverObject, Argument2);

    // Look for legacy ISA-bridged PCI IDE controller (onboard)
    for (i=0; i <BMListLen; i++) {

        if(!BMList[i].MasterDev)
            break;
        if(ReEnter) {
            if(BMList[i].ChanInitOk & 0x03) {
                statusToReturn =
                newStatus = STATUS_SUCCESS;
            }
            continue;
        }
        for(c=0; c<2; c++) {
            hwInitializationData.comm.HwFindAdapter = AtapiFindBusMasterController;
            hwInitializationData.comm.NumberOfAccessRanges = 5;
            hwInitializationData.comm.AdapterInterfaceType = Isa;

            BMList[i].channel = (UCHAR)c;

            newStatus = ScsiPortInitialize(DriverObject,
                                           Argument2,
                                           &hwInitializationData.comm,
                                           (PVOID)i);
            if (newStatus < statusToReturn)
                statusToReturn = newStatus;
            if (newStatus == STATUS_SUCCESS) {
                BMList[i].ChanInitOk |= 0x01 << c;
                if(BMList[i].MasterDev && !WinVer_IsNT) {
                    c = 1;
                    BMList[i].ChanInitOk |= 0x01 << c;
                }
            }
        }
    }

/*    KeBugCheckEx(0xc000000e,
                 (i << 16) | BMList[0].ChanInitOk,
                 c, 
                 newStatus, statusToReturn);*/

    // Look for PCI IDE controller
    for (; i <BMListLen; i++) {

/*        if(BMList[i].MasterDev)
            continue;*/

        hwInitializationData.comm.HwFindAdapter = AtapiFindBusMasterController;
        hwInitializationData.comm.NumberOfAccessRanges = 5;
        hwInitializationData.comm.AdapterInterfaceType = PCIBus;

        hwInitializationData.comm.VendorId             = BMList[i].VendorId;
        hwInitializationData.comm.VendorIdLength       = (USHORT) BMList[i].VendorIdLength;
        hwInitializationData.comm.DeviceId             = BMList[i].DeviceId;
        hwInitializationData.comm.DeviceIdLength       = (USHORT) BMList[i].DeviceIdLength;

        BMList[i].channel = 0/*(UCHAR)c*/;

        newStatus = ScsiPortInitialize(DriverObject,
                                       Argument2,
                                       &hwInitializationData.comm,
                                       (PVOID)i);
        if (newStatus < statusToReturn)
            statusToReturn = newStatus;
    }

/*    KeBugCheckEx(0xc000000e,
                 i,
                 c, 
                 newStatus, statusToReturn);*/

    // --------------

    hwInitializationData.comm.VendorId             = 0;
    hwInitializationData.comm.VendorIdLength       = 0;
    hwInitializationData.comm.DeviceId             = 0;
    hwInitializationData.comm.DeviceIdLength       = 0;

    // The adapter count is used by the find adapter routine to track how
    // which adapter addresses have been tested.

    // Indicate 2 access ranges and reset FindAdapter.
    hwInitializationData.comm.NumberOfAccessRanges = 2;
    hwInitializationData.comm.HwFindAdapter = AtapiFindController;

    // Indicate ISA bustype.
    hwInitializationData.comm.AdapterInterfaceType = Isa;
    adapterCount = 0;

    // Call initialization for ISA bustype.
    KdPrint2((PRINT_PREFIX "\n\nATAPI IDE Look for ISA Controllers\n"));
    newStatus =  ScsiPortInitialize(DriverObject,
                                    Argument2,
                                    &hwInitializationData.comm,
                                    &adapterCount);
    if (newStatus < statusToReturn)
        statusToReturn = newStatus;

    // Set up for MCA
    KdPrint2((PRINT_PREFIX "\n\nATAPI IDE Look for MCA Controllers\n"));
    hwInitializationData.comm.AdapterInterfaceType = MicroChannel;
    adapterCount = 0;

    newStatus =  ScsiPortInitialize(DriverObject,
                                    Argument2,
                                    &hwInitializationData.comm,
                                    &adapterCount);
    if (newStatus < statusToReturn)
        statusToReturn = newStatus;

    return statusToReturn;

} // end DriverEntry()


PSCSI_REQUEST_BLOCK
BuildMechanismStatusSrb(
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PSCSI_REQUEST_BLOCK srb;
    PCDB cdb;
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);

    srb = &(deviceExtension->chan[GET_CHANNEL(Srb)].InternalSrb);

    RtlZeroMemory((PCHAR) srb, sizeof(SCSI_REQUEST_BLOCK));

    srb->PathId     = (UCHAR)(Srb->PathId);
    srb->TargetId   = (UCHAR)(Srb->TargetId);
    srb->Function   = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Length     = sizeof(SCSI_REQUEST_BLOCK);

    // Set flags to disable synchronous negociation.
    srb->SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER;

    // Set timeout to 4 seconds.
    srb->TimeOutValue = 4;

    srb->CdbLength          = 6;
    srb->DataBuffer         = &(deviceExtension->chan[GET_CHANNEL(Srb)].MechStatusData);
    srb->DataTransferLength = sizeof(MECHANICAL_STATUS_INFORMATION_HEADER);
    srb->SrbExtension       = AtaReq;

    // Set CDB operation code.
    cdb = (PCDB)srb->Cdb;
    cdb->MECH_STATUS.OperationCode       = SCSIOP_MECHANISM_STATUS;
    cdb->MECH_STATUS.AllocationLength[1] = sizeof(MECHANICAL_STATUS_INFORMATION_HEADER);

    return srb;
} // end BuildMechanismStatusSrb()


PSCSI_REQUEST_BLOCK
BuildRequestSenseSrb (
    IN PVOID HwDeviceExtension,
    IN PSCSI_REQUEST_BLOCK Srb
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PSCSI_REQUEST_BLOCK srb;
    PCDB cdb;
    PATA_REQ AtaReq = (PATA_REQ)(Srb->SrbExtension);

    srb = &(deviceExtension->chan[GET_CHANNEL(Srb)].InternalSrb);

    RtlZeroMemory((PCHAR) srb, sizeof(SCSI_REQUEST_BLOCK));

    srb->PathId     = (UCHAR)(Srb->PathId);
    srb->TargetId   = (UCHAR)(Srb->TargetId);
    srb->Function   = SRB_FUNCTION_EXECUTE_SCSI;
    srb->Length     = sizeof(SCSI_REQUEST_BLOCK);

    // Set flags to disable synchronous negociation.
    srb->SrbFlags = SRB_FLAGS_DATA_IN | SRB_FLAGS_DISABLE_SYNCH_TRANSFER;

    // Set timeout to 2 seconds.
    srb->TimeOutValue = 4;

    srb->CdbLength          = 6;
    srb->DataBuffer         = &(deviceExtension->chan[GET_CHANNEL(Srb)].MechStatusSense);
    srb->DataTransferLength = sizeof(SENSE_DATA);
    srb->SrbExtension       = AtaReq;

    // Set CDB operation code.
    cdb = (PCDB)srb->Cdb;
    cdb->CDB6INQUIRY.OperationCode    = SCSIOP_REQUEST_SENSE;
    cdb->CDB6INQUIRY.AllocationLength = sizeof(SENSE_DATA);

    return srb;
} // end BuildRequestSenseSrb()


ULONG
AtapiRegCheckDevValue(
    IN PVOID HwDeviceExtension,
    IN PWSTR Name,
    IN ULONG Default
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    WCHAR name0[11];
    WCHAR name1[11+4+5];
    WCHAR name2[11+4+4+10];
    WCHAR name3[11+4+4+5+20];

    IN ULONG VendorID;
    IN ULONG DeviceID;
    IN ULONG SlotNumber;

    ULONG val = Default;

    KdPrint(( " Parameter = %ws\n", Name));

    if(deviceExtension) {
        VendorID   =  deviceExtension->DevID        & 0xffff;
        DeviceID   = (deviceExtension->DevID >> 16) & 0xffff;
        SlotNumber = deviceExtension->slotNumber;
    } else {
        VendorID   = 0xffff;
        DeviceID   = 0xffff;
        SlotNumber = 0xffffffff;
    }

    swprintf(name0, L"Parameters");
    swprintf(name1, L"Parameters\\Ven_%4.4x", VendorID);
    swprintf(name2, L"Parameters\\Ven_%4.4x\\Dev_%4.4x", VendorID, DeviceID);
    swprintf(name3, L"Parameters\\Ven_%4.4x\\Dev_%4.4x\\Slot_%8.8x", VendorID, DeviceID, SlotNumber);

    val = AtapiRegCheckParameterValue(
        HwDeviceExtension, name0, Name, val);
    val = AtapiRegCheckParameterValue(
        HwDeviceExtension, name1, Name, val);
    val = AtapiRegCheckParameterValue(
        HwDeviceExtension, name2, Name, val);
    val = AtapiRegCheckParameterValue(
        HwDeviceExtension, name3, Name, val);

    return val;

} // end AtapiRegCheckDevValue()

/*
    The user must specify that Xxx is to run on the platform
    by setting the registry value HKEY_LOCAL_MACHINE\System\CurrentControlSet\
    Services\UniATA\Xxx:REG_DWORD:Zzz.

    The user can override the global setting to enable or disable Xxx on a
    specific cdrom device by setting the key HKEY_LOCAL_MACHINE\System\
    CurrentControlSet\Services\UniATA\Parameters\Device<N>\Xxx:REG_DWORD to one or zero.

    If this registry value does not exist or contains the value zero then
    the timer to check for media change does not run.

    Arguments:

    RegistryPath - pointer to the unicode string inside
                   ...\CurrentControlSet\Services\UniATA
    DeviceNumber - The number of the HBA device object

    Returns:    Registry Key value
 */
ULONG
AtapiRegCheckParameterValue(
    IN PVOID HwDeviceExtension,
    IN PWSTR PathSuffix,
    IN PWSTR Name,
    IN ULONG Default
    )
{
#define ITEMS_TO_QUERY 2 // always 1 greater than what is searched 

    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    NTSTATUS          status;
    LONG              zero = Default;

    RTL_QUERY_REGISTRY_TABLE parameters[ITEMS_TO_QUERY];

//    LONG              tmp = 0;
    LONG              doRun = 0;

    PUNICODE_STRING   RegistryPath = &SavedRegPath;

    UNICODE_STRING    paramPath;

    // <SavedRegPath>\<PathSuffix> -> <Name>
//    KdPrint(( "AtapiCheckRegValue: %ws -> %ws\n", PathSuffix, Name));
//    KdPrint(( "AtapiCheckRegValue: RegistryPath %ws\n", RegistryPath->Buffer));

    paramPath.Length = 0;
    paramPath.MaximumLength = RegistryPath->Length +
        (wcslen(PathSuffix)+2)*sizeof(WCHAR);
    paramPath.Buffer = (PUSHORT)ExAllocatePool(NonPagedPool, paramPath.MaximumLength);
    if(!paramPath.Buffer) {
        KdPrint(("AtapiCheckRegValue: couldn't allocate paramPath\n"));
        return FALSE;
    }

    RtlZeroMemory(paramPath.Buffer, paramPath.MaximumLength);
    RtlAppendUnicodeToString(&paramPath, RegistryPath->Buffer);
    RtlAppendUnicodeToString(&paramPath, L"\\");
    RtlAppendUnicodeToString(&paramPath, PathSuffix);

    // Check for the Xxx value.
    RtlZeroMemory(parameters, (sizeof(RTL_QUERY_REGISTRY_TABLE)*ITEMS_TO_QUERY));

    parameters[0].Flags         = RTL_QUERY_REGISTRY_DIRECT;
    parameters[0].Name          = Name;
    parameters[0].EntryContext  = &doRun;
    parameters[0].DefaultType   = REG_DWORD;
    parameters[0].DefaultData   = &zero;
    parameters[0].DefaultLength = sizeof(ULONG);

    status = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE | RTL_REGISTRY_OPTIONAL,
                                    paramPath.Buffer, parameters, NULL, NULL);
    KdPrint(( "AtapiCheckRegValue: %ws -> %ws is %#x\n", paramPath.Buffer, Name, doRun));

    ExFreePool(paramPath.Buffer);

    return doRun;

#undef ITEMS_TO_QUERY

} // end AtapiRegCheckParameterValue()


SCSI_ADAPTER_CONTROL_STATUS
AtapiAdapterControl(
    IN PVOID HwDeviceExtension,
    IN SCSI_ADAPTER_CONTROL_TYPE ControlType,
    IN PVOID Parameters
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PSCSI_SUPPORTED_CONTROL_TYPE_LIST pControlTypeList;
    ULONG                numberChannels  = deviceExtension->NumberChannels;
    ULONG c;

    switch(ControlType) {
        case ScsiQuerySupportedControlTypes: {
            BOOLEAN supportedTypes[ScsiAdapterControlMax] = {
                TRUE,       // ScsiQuerySupportedControlTypes
                TRUE,       // ScsiStopAdapter
                TRUE,       // ScsiRestartAdapter
                FALSE,      // ScsiSetBootConfig
                FALSE       // ScsiSetRunningConfig
            };

            ULONG max = ScsiAdapterControlMax;
            ULONG i;

            pControlTypeList = (PSCSI_SUPPORTED_CONTROL_TYPE_LIST) Parameters;

            if(pControlTypeList->MaxControlType < max) {
                max = pControlTypeList->MaxControlType;
            }

            for(i = 0; i < max; i++) {
                pControlTypeList->SupportedTypeList[i] = supportedTypes[i];
            }


            break;

        }
        case ScsiStopAdapter: {

            // Shut down all interrupts on the adapter.  They'll get re-enabled
            // by the initialization routines.
            for (c = 0; c < numberChannels; c++) {
                AtapiResetController(deviceExtension, c);
                AtapiDisableInterrupts(deviceExtension, c);
            }
            BMList[deviceExtension->DevIndex].Isr2Enable = FALSE;
            break;
        }

        case ScsiRestartAdapter: {

            // Enable all the interrupts on the adapter while port driver call
            // for power up an HBA that was shut down for power management
            for (c = 0; c < numberChannels; c++) {
                AtapiChipInit(HwDeviceExtension, -1, c);
                FindDevices(HwDeviceExtension, FALSE, c);
                AtapiEnableInterrupts(deviceExtension, c);
                AtapiHwInitialize__(deviceExtension, c);
            }
            BMList[deviceExtension->DevIndex].Isr2Enable = TRUE;

            break;
        }

        default: {
            return ScsiAdapterControlUnsuccessful;
        }
    }

    return ScsiAdapterControlSuccess;
} // end AtapiAdapterControl()


