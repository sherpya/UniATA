/*++

Copyright (c) 2002-2004 Alexandr A. Telyatnikov (Alter)

Module Name:
    bsmaster.h

Abstract:
    This file contains DMA/UltraDMA and IDE BusMastering related definitions,
    internal structures and useful macros

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

    Code was created by
         Alter, Copyright (c) 2002-2004

    Some definitions were taken from FreeBSD 4.3-4.6 ATA driver by
         Søren Schmidt, Copyright (c) 1998,1999,2000,2001

--*/

#ifndef __IDE_BUSMASTER_H__
#define __IDE_BUSMASTER_H__

#include "tools.h"

//
//
//
#define         ATA_IDLE                0x0
#define         ATA_IMMEDIATE           0x1
#define         ATA_WAIT_INTR           0x2
#define         ATA_WAIT_READY          0x3
#define         ATA_ACTIVE              0x4
#define         ATA_ACTIVE_ATA          0x5
#define         ATA_ACTIVE_ATAPI        0x6
#define         ATA_REINITING           0x7
#define         ATA_WAIT_BASE_READY     0x8
#define         ATA_WAIT_IDLE           0x9


#include "bm_devs.h"

#define USE_OWN_DMA

//#define UNI_CPU_OPTIMIZATION

#define QUEUE_STATISTICS
#define IO_STATISTICS
#define MAX_QUEUE_STAT                  8

#define MAX_RETRIES                     6
#define RETRY_UDMA2                     1
#define RETRY_WDMA                      2
#define RETRY_PIO                       3


#define	IO_WD1	        0x1F0		/* Primary Fixed Disk Controller */
#define	IO_WD2	        0x170		/* Secondary Fixed Disk Controller */
#define IP_PC98_BANK    0x432

#define PCI_ADDRESS_IOMASK              0xfffffff0

#define ATA_BM_OFFSET1			0x08
#define ATA_DMA_ENTRIES			256     /* PAGESIZE/2/sizeof(BM_DMA_ENTRY)*/
#define ATA_DMA_EOT			0x80000000
#define ATA_IOSIZE			0x08
#define ATA_ALTOFFSET			0x206	/* alternate registers offset */
#define ATA_PCCARD_ALTOFFSET		0x0e	/* do for PCCARD devices */
#define ATA_ALTIOSIZE			0x01	/* alternate registers size */
#define ATA_BMIOSIZE			0x20
#define ATA_PC98_BANKIOSIZE             0x01

#define DEV_BSIZE                       512

#define ATAPI_MAGIC_LSB			0x14
#define ATAPI_MAGIC_MSB			0xeb

typedef struct _BUSMASTER_CTX {
    PBUSMASTER_CONTROLLER_INFORMATION* BMListPtr;
    ULONG* BMListLen;
} BUSMASTER_CTX, *PBUSMASTER_CTX;

#define PCI_DEV_CLASS_STORAGE           0x01
#define PCI_DEV_SUBCLASS_IDE            0x01

/* structure for holding DMA address data */
typedef struct BM_DMA_ENTRY {
    ULONG base;
    ULONG count;
} BM_DMA_ENTRY, *PBM_DMA_ENTRY;

typedef struct _IDE_BUSMASTER_REGISTERS {
    UCHAR Command;
    UCHAR DeviceSpecific0;
    UCHAR Status;
    UCHAR DeviceSpecific1;
    ULONG PRD_Table;
} IDE_BUSMASTER_REGISTERS, *PIDE_BUSMASTER_REGISTERS;

#define BM_STATUS_ACTIVE                0x01
#define BM_STATUS_ERR                   0x02
#define BM_STATUS_INTR                  0x04
#define BM_STATUS_MASK                  0x07
#define BM_STATUS_DRIVE_0_DMA           0x20
#define BM_STATUS_DRIVE_1_DMA           0x40
#define BM_STATUS_SIMPLEX_ONLY          0x80

#define BM_COMMAND_START_STOP		0x01
/*#define BM_COMMAND_WRITE		0x08
#define BM_COMMAND_READ		        0x00*/
#define BM_COMMAND_WRITE		0x00
#define BM_COMMAND_READ		        0x08

#define IsBusMaster(pciData) \
    ( ((pciData)->Command & (PCI_ENABLE_BUS_MASTER/* | PCI_ENABLE_IO_SPACE*/)) == \
          (PCI_ENABLE_BUS_MASTER/* | PCI_ENABLE_IO_SPACE*/))

#define PCI_IDE_PROGIF_NATIVE_1         0x01
#define PCI_IDE_PROGIF_NATIVE_2         0x04
#define PCI_IDE_PROGIF_NATIVE_ALL       0x05

#define IsMasterDev(pciData) \
    ( ((pciData)->ProgIf & 0x80) && \
      ((pciData)->ProgIf & PCI_IDE_PROGIF_NATIVE_ALL) != PCI_IDE_PROGIF_NATIVE_ALL )

//#define INT_Q_SIZE 32
#define MIN_REQ_TTL 4

struct _ATA_REQ;

typedef struct _ATA_REQ {
//    ULONG               reqId;          // serial
    struct _ATA_REQ*    next_req;
    struct _ATA_REQ*    prev_req;

    PSCSI_REQUEST_BLOCK Srb;            // Current request on controller.

    PUSHORT             DataBuffer;     // Data buffer pointer.
    ULONG               WordsLeft;      // Data words left.
    LONGLONG            lba;
    ULONG               bcount;

    UCHAR               retry;
    UCHAR               ttl;
//    UCHAR               tag;
    UCHAR               Flags;
    UCHAR               ReqState;

    PSCSI_REQUEST_BLOCK OriginalSrb;    // Mechanism Status Srb Data

    ULONG               dma_base;
    BM_DMA_ENTRY        dma_tab[ATA_DMA_ENTRIES];

} ATA_REQ, *PATA_REQ;

#define REQ_FLAG_FORCE_DOWNRATE         0x01
#define REQ_FLAG_DMA_OPERATION          0x02
#define REQ_FLAG_REORDERABLE_CMD        0x04
#define REQ_FLAG_RW_MASK                0x08
#define     REQ_FLAG_READ                   0x08
#define     REQ_FLAG_WRITE                  0x00

// Request states
#define REQ_STATE_NONE                  0x00
#define REQ_STATE_QUEUED                0x10

#define REQ_STATE_PREPARE_TO_TRANSFER   0x20
#define REQ_STATE_READY_TO_TRANSFER     0x30

#define REQ_STATE_EXPECTING_INTR        0x40
#define REQ_STATE_ATAPI_EXPECTING_CMD_INTR     0x41
#define REQ_STATE_ATAPI_EXPECTING_DATA_INTR    0x42
#define REQ_STATE_ATAPI_DO_NOTHING_INTR        0x43

#define REQ_STATE_PROCESSING_INTR       0x50

#define REQ_STATE_DPC_INTR_REQ          0x51
#define REQ_STATE_DPC_RESET_REQ         0x52
#define REQ_STATE_DPC_COMPLETE_REQ      0x53

#define REQ_STATE_DPC_WAIT_BUSY0        0x57
#define REQ_STATE_DPC_WAIT_BUSY         0x58
#define REQ_STATE_DPC_WAIT_DRQ          0x59
#define REQ_STATE_DPC_WAIT_DRQ0         0x5a
#define REQ_STATE_DPC_WAIT_DRQ_ERR      0x5b

#define REQ_STATE_TRANSFER_COMPLETE     0x7f

// Command actions:
#define CMD_ACTION_PREPARE              0x01
#define CMD_ACTION_EXEC                 0x02
#define CMD_ACTION_ALL                  (CMD_ACTION_PREPARE | CMD_ACTION_EXEC)

// predefined Reorder costs
#define REORDER_COST_MAX               ((0x1I64 << 60) - 1)
#define REORDER_COST_TTL               (REORDER_COST_MAX - 1)
#define REORDER_COST_INTERSECT         (REORDER_COST_MAX - 2)
#define REORDER_COST_DENIED            (REORDER_COST_MAX - 3)
#define REORDER_COST_RESELECT          (REORDER_COST_MAX/4)

#define REORDER_COST_SWITCH_RW_CD      (REORDER_COST_MAX/8)
#define REORDER_MCOST_SWITCH_RW_CD     (0)
#define REORDER_MCOST_SEEK_BACK_CD     (16)

#define REORDER_COST_SWITCH_RW_HDD     (0)
#define REORDER_MCOST_SWITCH_RW_HDD    (4)
#define REORDER_MCOST_SEEK_BACK_HDD    (2)

/*typedef struct _ATA_QUEUE {
    struct _ATA_REQ*    head_req; // index
    struct _ATA_REQ*    tail_req; // index
    ULONG               req_count;
    ULONG               dma_base;
    BM_DMA_ENTRY        dma_tab[ATA_DMA_ENTRIES];
} ATA_QUEUE, *PATA_QUEUE;*/

struct _HW_DEVICE_EXTENSION;
struct _HW_LU_EXTENSION;

// Channel extension
typedef struct _HW_CHANNEL {

    PATA_REQ            cur_req;
    ULONG               cur_cdev;
/*    PATA_REQ            first_req;
    PATA_REQ            last_req;*/
    ULONG               queue_depth;
    ULONG               ChannelSelectWaitCount;

    BOOLEAN             ExpectingInterrupt;    // Indicates expecting an interrupt
    BOOLEAN             RDP;    // Indicate last tape command was DSC Restrictive.
    // Indicates whether '0x1f0' is the base address. Used
    // in SMART Ioctl calls.
    BOOLEAN             PrimaryAddress;
    // Placeholder for the sub-command value of the last
    // SMART command.
    UCHAR               SmartCommand;
    // Placeholder for status register after a GET_MEDIA_STATUS command
    UCHAR               ReturningMediaStatus;

    BOOLEAN             CopyDmaBuffer;
    BOOLEAN             MemIo;
    UCHAR               Padding0[1];

    MECHANICAL_STATUS_INFORMATION_HEADER MechStatusData;
    SENSE_DATA          MechStatusSense;
    ULONG               MechStatusRetryCount;
    SCSI_REQUEST_BLOCK  InternalSrb;

    ULONG               MaxTransferMode; // may differ from Controller's value due to 40-pin cable

    ULONG               ChannelCtrlFlags;
    ULONG               ResetInProgress; // flag
    LONG                DisableIntr;
    LONG                CheckIntr;

    ULONG               lChannel;

#define CHECK_INTR_ACTIVE               0x03
#define CHECK_INTR_DETECTED             0x02
#define CHECK_INTR_CHECK                0x01
#define CHECK_INTR_IDLE                 0x00

/*    PHW_TIMER           CurDpc;
    LARGE_INTEGER       ActivationTime;*/

/*    KDPC                Dpc;
    KTIMER              Timer;
    PHW_TIMER           HwScsiTimer;*/
//    KSPIN_LOCK          QueueSpinLock;
//    KIRQL               QueueOldIrql;
    struct _HW_DEVICE_EXTENSION* DeviceExtension;
    struct _HW_LU_EXTENSION* lun[2];

    PUCHAR              DmaBuffer;

#ifdef QUEUE_STATISTICS
    LONGLONG QueueStat[MAX_QUEUE_STAT];
    LONGLONG ReorderCount;
    LONGLONG IntersectCount;
    LONGLONG TryReorderCount;
    LONGLONG TryReorderHeadCount;
    LONGLONG TryReorderTailCount; /* in-order requests */
#endif //QUEUE_STATISTICS

    //PBM_DMA_ENTRY dma_tab;//[PAGE_SIZE/sizeof(BM_DMA_ENTRY)];	/* DMA transfer table */
//    BM_DMA_ENTRY _dma_tab[PAGE_SIZE/sizeof(BM_DMA_ENTRY)];	/* DMA transfer table */
//    PBM_DMA_ENTRY dma_tab;//[PAGE_SIZE/sizeof(BM_DMA_ENTRY)];	/* DMA transfer table */
//    ULONG dma_base;

    ULONG BaseMemAddress;
    ULONG BaseMemAddressOffset;
    ULONG RegTranslation[sizeof(IDE_REGISTERS_1) +
                         sizeof(IDE_REGISTERS_2) +
                         sizeof(IDE_BUSMASTER_REGISTERS)];

} HW_CHANNEL, *PHW_CHANNEL;

#define CTRFLAGS_DMA_ACTIVE             0x01
#define CTRFLAGS_DMA_RO                 0x02
#define CTRFLAGS_DMA_OPERATION          0x04
#define CTRFLAGS_INTR_DISABLED          0x08
#define CTRFLAGS_DPC_REQ                0x10
#define CTRFLAGS_ENABLE_INTR_REQ        0x20
#define CTRFLAGS_LBA48                  0x40

// Logical unit extension
typedef struct _HW_LU_EXTENSION {
    IDENTIFY_DATA2 IdentifyData;
    ULONGLONG      NumOfSectors;
    ULONG          DeviceFlags;    // Flags word for each possible device.
    ULONG          DiscsPresent;   // Indicates number of platters on changer-ish devices.
    BOOLEAN        DWordIO;        // Indicates use of 32-bit PIO
    UCHAR          ReturningMediaStatus;
    UCHAR          TransferMode;
    UCHAR          OrigTransferMode;
    UCHAR          MaximumBlockXfer;
    UCHAR          Padding0[3];    // padding
    ULONG          ErrorCount;     // Count of errors. Used to turn off features.
 //   ATA_QUEUE      cmd_queue;
    LONGLONG       ReadCmdCost;
    LONGLONG       WriteCmdCost;
    LONGLONG       OtherCmdCost;
    LONGLONG       RwSwitchCost;
    LONGLONG       RwSwitchMCost;
    LONGLONG       SeekBackMCost;
    //
    PATA_REQ       first_req;
    PATA_REQ       last_req;
    ULONG          queue_depth;
    ULONG          last_write;

    ULONG          LunSelectWaitCount;

#ifdef IO_STATISTICS

    LONGLONG ModeErrorCount[MAX_RETRIES];
    LONGLONG RecoverCount[MAX_RETRIES];
    LONGLONG IoCount;

#endif//IO_STATISTICS
} HW_LU_EXTENSION, *PHW_LU_EXTENSION;

#define IDE_MAX_CHAN          8
#define IDE_MAX_LUN_PER_CHAN  2
#define IDE_MAX_LUN           (IDE_MAX_CHAN*IDE_MAX_LUN_PER_CHAN)

// Device extension
typedef struct _HW_DEVICE_EXTENSION {
    CHAR Signature[32];
    PIDE_REGISTERS_1 BaseIoAddress1[IDE_MAX_CHAN];    // Base register locations
    PIDE_REGISTERS_2 BaseIoAddress2[IDE_MAX_CHAN];
    ULONG BusInterruptLevel;    // Interrupt level
    ULONG InterruptMode;        // Interrupt Mode (Level or Edge)
    ULONG BusInterruptVector;
    // Number of channels being supported by one instantiation
    // of the device extension. Normally (and correctly) one, but
    // with so many broken PCI IDE controllers being sold, we have
    // to support them.
    ULONG NumberChannels;
    ULONG FirstChannelToCheck;
    HW_LU_EXTENSION lun[IDE_MAX_LUN];
    HW_CHANNEL chan[IDE_MAX_CHAN];
    UCHAR LastInterruptedChannel;
    // Indicates the number of blocks transferred per int. according to the
    // identify data.
    BOOLEAN DriverMustPoll;    // Driver is being used by the crash dump utility or ntldr.
//    BOOLEAN DWordIO;         // Indicates use of 32-bit PIO
    BOOLEAN BusMaster;
    BOOLEAN UseDpc;            // Indicates use of DPC on long waits
    IDENTIFY_DATA FullIdentifyData;    // Identify data for device
    // BusMaster specific data
//    PBM_DMA_ENTRY dma_tab_0;
    KSPIN_LOCK  DpcSpinLock;
    ULONG       ActiveDpcChan;
    ULONG       FirstDpcChan;
    PHW_TIMER   HwScsiTimer1;
    PHW_TIMER   HwScsiTimer2;
    LONGLONG    DpcTime1;
    LONGLONG    DpcTime2;

    ULONG          queue_depth;

    PDEVICE_OBJECT Isr2DevObj;

    PIDE_BUSMASTER_REGISTERS  BaseIoAddressBM_0;
    PIDE_BUSMASTER_REGISTERS  BaseIoAddressBM[IDE_MAX_CHAN];

    // Device identification
    ULONG DevID;
    ULONG RevID;
    ULONG slotNumber;
    ULONG SystemIoBusNumber;
    ULONG DevIndex;

    ULONG InitMethod; // vendor specific

    ULONG Channel;

    ULONG HbaCtrlFlags;
    BOOLEAN simplexOnly;
    BOOLEAN MemIo;
    BOOLEAN UnknownDev;
    BOOLEAN MasterDev;
    LONG  ReCheckIntr;

    ULONG MaxTransferMode;
    ULONG HwFlags;
    INTERFACE_TYPE OrigAdapterInterfaceType;
    ULONG MaximumDmaTransferLength;

    ULONG BaseMemAddress;

} HW_DEVICE_EXTENSION, *PHW_DEVICE_EXTENSION;

typedef struct _ISR2_DEVICE_EXTENSION {
    PHW_DEVICE_EXTENSION HwDeviceExtension;
    ULONG DevIndex;
} ISR2_DEVICE_EXTENSION, *PISR2_DEVICE_EXTENSION;

#define HBAFLAGS_DMA_DISABLED           0x01

extern UCHAR         pciBuffer[256];
extern PBUSMASTER_CONTROLLER_INFORMATION BMList;
extern ULONG         BMListLen;

extern const CHAR retry_Wdma[MAX_RETRIES+1];
extern const CHAR retry_Udma[MAX_RETRIES+1];

extern VOID
AtapiEnumBusMasterController(
    IN PVOID DriverObject,
    PVOID Argument2
    );

extern ULONG
AtapiFindBusMasterController(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    );

extern NTSTATUS
AtapiConnectIntr2(
    IN PVOID HwDeviceExtension
    );

extern NTSTATUS
AtapiDisconnectIntr2(
    IN PVOID HwDeviceExtension
    );

extern ULONG 
ScsiPortGetBusDataByOffset(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  BusNumber,
    IN ULONG  SlotNumber,
    IN PVOID  Buffer,
    IN ULONG  Offset,
    IN ULONG  Length
    );

extern ULONG
AtapiFindListedDev(
    PBUSMASTER_CONTROLLER_INFORMATION BusMasterAdapters,
    ULONG     lim,
    IN PVOID  HwDeviceExtension,
    IN ULONG  BusNumber,
    IN ULONG  SlotNumber,
    OUT PCI_SLOT_NUMBER* _slotData // optional
    );

extern ULONG
AtapiFindDev(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  BusNumber,
    IN ULONG  SlotNumber,
    IN ULONG  dev_id,
    IN ULONG  RevID
    );

extern VOID
AtapiDmaAlloc(
    IN PVOID HwDeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN ULONG lChannel          // logical channel,
    );

extern BOOLEAN
AtapiDmaSetup(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb,
    IN PUCHAR data,
    IN ULONG count
    );

extern BOOLEAN
AtapiDmaPioSync(
    PVOID  HwDeviceExtension,
    PSCSI_REQUEST_BLOCK Srb,
    PUCHAR data,
    ULONG  count
    );

extern VOID
AtapiDmaStart(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb
    );

extern UCHAR
AtapiDmaDone(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
    IN PSCSI_REQUEST_BLOCK Srb
    );

extern VOID
AtapiDmaReinit(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG ldev,
    IN PATA_REQ AtaReq
    );

extern VOID
AtapiDmaInit__(
    IN PHW_DEVICE_EXTENSION deviceExtension,
    IN ULONG ldev
    );

extern VOID
AtapiDmaInit(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG lChannel,          // logical channel,
                               // is always 0 except simplex-only controllers
    IN CHAR apiomode,
    IN CHAR wdmamode,
    IN CHAR udmamode
    );

extern BOOLEAN
AtapiInterrupt2(
    IN PKINTERRUPT Interrupt,
    IN PVOID HwDeviceExtension
    );

extern PDRIVER_OBJECT SavedDriverObject;

extern BOOLEAN
AtapiChipDetect(
    IN PVOID HwDeviceExtension,
    IN PPCI_COMMON_CONFIG pciData, // optional
    IN ULONG DeviceNumber,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN BOOLEAN* simplexOnly
    );

extern BOOLEAN
AtapiChipInit(
    IN PVOID HwDeviceExtension,
    IN ULONG DeviceNumber,
    IN ULONG c
    );

extern ULONG
AtapiGetIoRange(
    IN PVOID HwDeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PPCI_COMMON_CONFIG pciData,
    IN ULONG SystemIoBusNumber,
    IN ULONG rid,
    IN ULONG offset,
    IN ULONG length //range id
    );

/****************** 1 *****************/
#define GetPciConfig1(offs, op) {                                \
    ScsiPortGetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &op,                              \
                               offs,                             \
                               1);                               \
}

#define SetPciConfig1(offs, op) {                                \
    UCHAR _a = op;                                              \
    ScsiPortSetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &_a,                              \
                               offs,                             \
                               1);                               \
}

#define ChangePciConfig1(offs, _op) {                            \
    UCHAR a = 0;                                                 \
    GetPciConfig1(offs, a);                                      \
    a = (UCHAR)(_op);                                            \
    SetPciConfig1(offs, a);                                      \
}

/****************** 2 *****************/
#define GetPciConfig2(offs, op) {                                \
    ScsiPortGetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &op,                              \
                               offs,                             \
                               2);                               \
}

#define SetPciConfig2(offs, op) {                                \
    USHORT _a = op;                                              \
    ScsiPortSetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &_a,                              \
                               offs,                             \
                               2);                               \
}

#define ChangePciConfig2(offs, _op) {                            \
    USHORT a = 0;                                                \
    GetPciConfig2(offs, a);                                      \
    a = (USHORT)(_op);                                           \
    SetPciConfig2(offs, a);                                      \
}

/****************** 4 *****************/
#define GetPciConfig4(offs, op) {                                \
    ScsiPortGetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &op,                              \
                               offs,                             \
                               4);                               \
}

#define SetPciConfig4(offs, op) {                                \
    ULONG _a = op;                                               \
    ScsiPortSetBusDataByOffset(HwDeviceExtension,                \
                               PCIConfiguration,                 \
                               SystemIoBusNumber,                \
                               slotNumber,                       \
                               &_a,                              \
                               offs,                             \
                               4);                               \
}

#define ChangePciConfig4(offs, _op) {                            \
    ULONG a = 0;                                                 \
    GetPciConfig4(offs, a);                                      \
    a = _op;                                                     \
    SetPciConfig4(offs, a);                                      \
}

#define GetDmaStatus(de, c) \
    (((de)->BaseIoAddressBM[c]) ? ScsiPortReadPortUchar(&((de)->BaseIoAddressBM[c]->Status)) : 0)

#ifdef USE_OWN_DMA
#define AtapiVirtToPhysAddr(hwde, srb, phaddr, plen) \
    AtapiVirtToPhysAddr_(hwde, srb, phaddr, plen);
#else
#define AtapiVirtToPhysAddr(hwde, srb, phaddr, plen) \
    (ScsiPortConvertPhysicalAddressToUlong/*(ULONG)ScsiPortGetVirtualAddress*/(/*hwde,*/  \
                    ScsiPortGetPhysicalAddress(hwde, srb, phaddr, plen)))
#endif //USE_OWN_DMA

VOID
AtapiWritePort4(
    IN PHW_CHANNEL chan,
    IN PULONG port,
    IN ULONG data
    );

VOID
AtapiWritePort2(
    IN PHW_CHANNEL chan,
    IN PUSHORT port,
    IN USHORT data
    );

VOID
AtapiWritePort1(
    IN PHW_CHANNEL chan,
    IN PUCHAR port,
    IN UCHAR data
    );

ULONG
__fastcall
AtapiReadPort4(
    IN PHW_CHANNEL chan,
    IN PULONG port
    );

USHORT
__fastcall
AtapiReadPort2(
    IN PHW_CHANNEL chan,
    IN PUSHORT port
    );

UCHAR
__fastcall
AtapiReadPort1(
    IN PHW_CHANNEL chan,
    IN PUCHAR port
    );

VOID
__fastcall
AtapiWriteBuffer4(
    IN PHW_CHANNEL chan,
    IN PULONG _port,
    IN PVOID Buffer,
    IN ULONG Count
    );

VOID
__fastcall
AtapiWriteBuffer2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port,
    IN PVOID Buffer,
    IN ULONG Count
    );

VOID
__fastcall
AtapiReadBuffer4(
    IN PHW_CHANNEL chan,
    IN PULONG _port,
    IN PVOID Buffer,
    IN ULONG Count
    );

VOID
__fastcall
AtapiReadBuffer2(
    IN PHW_CHANNEL chan,
    IN PUSHORT _port,
    IN PVOID Buffer,
    IN ULONG Count
    );

/*#define GET_CHANNEL(Srb)  (Srb->TargetId >> 1)
#define GET_LDEV(Srb)  (Srb->TargetId)
#define GET_LDEV2(P, T, L)  (T)*/

#define GET_CHANNEL(Srb)  (Srb->PathId)
#define GET_LDEV(Srb)  (Srb->TargetId | (Srb->PathId << 1))
#define GET_LDEV2(P, T, L)  (T | (P<<1))
#define GET_CDEV(Srb)  (Srb->TargetId)

extern ULONG SkipRaids;
extern ULONG ForceSimplex;

extern BOOLEAN InDriverEntry;

#endif //__IDE_BUSMASTER_H__
