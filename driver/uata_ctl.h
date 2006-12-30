/*++

Copyright (c) 2004-2005 Alexandr A. Telyatnikov (Alter)

Module Name:
    uata_ctl.h

Abstract:
    This header contains definitions for private UniATA SRB_IOCTL.

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

#ifndef __UNIATA_IO_CONTROL_CODES__H__
#define __UNIATA_IO_CONTROL_CODES__H__

//#include "scsi.h"

#pragma pack(push, 8)

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#define AHCI_MAX_PORT                   32
#define IDE_MAX_CHAN          8
#define IDE_MAX_LUN_PER_CHAN  2
#define IDE_MAX_LUN           (AHCI_MAX_PORT*IDE_MAX_LUN_PER_CHAN)

#define MAX_QUEUE_STAT        8


#ifndef UNIATA_CORE

#define IOCTL_SCSI_MINIPORT_UNIATA_FIND_DEVICES    ((FILE_DEVICE_SCSI << 16) + 0x09a0)
#define IOCTL_SCSI_MINIPORT_UNIATA_DELETE_DEVICE   ((FILE_DEVICE_SCSI << 16) + 0x09a1)
#define IOCTL_SCSI_MINIPORT_UNIATA_SET_MAX_MODE    ((FILE_DEVICE_SCSI << 16) + 0x09a2)
#define IOCTL_SCSI_MINIPORT_UNIATA_GET_MODE        ((FILE_DEVICE_SCSI << 16) + 0x09a3)
#define IOCTL_SCSI_MINIPORT_UNIATA_ADAPTER_INFO    ((FILE_DEVICE_SCSI << 16) + 0x09a4)
//#define IOCTL_SCSI_MINIPORT_UNIATA_LUN_IDENT       ((FILE_DEVICE_SCSI << 16) + 0x09a5)
#define IOCTL_SCSI_MINIPORT_UNIATA_RESET_DEVICE    ((FILE_DEVICE_SCSI << 16) + 0x09a6)

typedef struct _SETTRANSFERMODE {
    ULONG MaxMode;
    ULONG OrigMode;
    BOOLEAN ApplyImmediately;
    UCHAR Reserved[3];
} SETTRANSFERMODE, *PSETTRANSFERMODE;

typedef struct _GETTRANSFERMODE {
    ULONG MaxMode;
    ULONG OrigMode;
    ULONG CurrentMode;
    ULONG Reserved;
} GETTRANSFERMODE, *PGETTRANSFERMODE;

typedef struct _CHANINFO {
    ULONG               MaxTransferMode; // may differ from Controller's value due to 40-pin cable
    ULONG               ChannelCtrlFlags;
//#ifdef QUEUE_STATISTICS
    LONGLONG QueueStat[MAX_QUEUE_STAT];
    LONGLONG ReorderCount;
    LONGLONG IntersectCount;
    LONGLONG TryReorderCount;
    LONGLONG TryReorderHeadCount;
    LONGLONG TryReorderTailCount; /* in-order requests */
//#endif //QUEUE_STATISTICS
} CHANINFO, *PCHANINFO;

typedef struct _ADAPTERINFO {
    // Device identification
    ULONG HeaderLength;
    ULONG DevID;
    ULONG RevID;
    ULONG slotNumber;
    ULONG SystemIoBusNumber;
    ULONG DevIndex;

    ULONG Channel;

    ULONG   HbaCtrlFlags;
    BOOLEAN simplexOnly;
    BOOLEAN MemIo;
    BOOLEAN UnknownDev;
    BOOLEAN MasterDev;

    ULONG   MaxTransferMode;
    ULONG   HwFlags;
    ULONG   OrigAdapterInterfaceType;

    CHAR    DeviceName[64];

    ULONG BusInterruptLevel;    // Interrupt level
    ULONG InterruptMode;        // Interrupt Mode (Level or Edge)
    ULONG BusInterruptVector;
    // Number of channels being supported by one instantiation
    // of the device extension. Normally (and correctly) one, but
    // with so many broken PCI IDE controllers being sold, we have
    // to support them.
    ULONG NumberChannels;

    BOOLEAN ChanInfoValid;

    CHANINFO Chan[AHCI_MAX_PORT];

} ADAPTERINFO, *PADAPTERINFO;

typedef struct _UNIATA_CTL {
    SRB_IO_CONTROL hdr;
    SCSI_ADDRESS   addr;
    union {
        UCHAR            RawData[1];
        SETTRANSFERMODE  SetMode;
        GETTRANSFERMODE  GetMode;
        ADAPTERINFO      AdapterInfo;
//        IDENTIFY_DATA2   LunIdent;
    };
} UNIATA_CTL, *PUNIATA_CTL;

#endif //UNIATA_CORE

#ifdef __cplusplus
};
#endif //__cplusplus

#pragma pack(pop)

#endif //__UNIATA_IO_CONTROL_CODES__H__
