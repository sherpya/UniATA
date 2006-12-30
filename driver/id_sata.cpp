#include "stdafx.h"

UCHAR
UniataSataConnect(
    IN PVOID HwDeviceExtension,
    IN ULONG lChannel          // logical channel
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG Channel = deviceExtension->Channel + lChannel;
    PHW_CHANNEL chan = &deviceExtension->chan[lChannel];
    SATA_SSTATUS_REG SStatus;
    ULONG i;
/*
    UCHAR                signatureLow,
                         signatureHigh;
*/
    UCHAR                Status;

    KdPrint2((PRINT_PREFIX "UniataSataConnect:\n"));
    /* wait up to 1 second for "connect well" */
    for(i=0; i<100; i++) {
        SStatus.Reg = AtapiReadPort4(chan, IDX_SATA_SStatus);
        if(SStatus.SPD == SStatus_SPD_Gen1 ||
           SStatus.SPD == SStatus_SPD_Gen2) {
            deviceExtension->lun[lChannel*2].TransferMode = ATA_SA150 + (SStatus.SPD - 1);
            break;
        }
        AtapiStallExecution(10000);
    }
    if(i >= 100) {
        KdPrint2((PRINT_PREFIX "UniataSataConnect: SStatus %8.8x\n", SStatus.Reg));
        return 0xff;
    }
    /* clear SATA error register */
    AtapiWritePort4(chan, IDX_SATA_SError,
        AtapiReadPort4(chan, IDX_SATA_SError));

    Status = WaitOnBaseBusyLong(chan);
    if(Status & IDE_STATUS_BUSY) {
        return Status;
    }
/*
    signatureLow = AtapiReadPort1(chan, &deviceExtension->BaseIoAddress1[lChannel].i.CylinderLow);
    signatureHigh = AtapiReadPort1(chan, &deviceExtension->baseIoAddress1[lChannel].i.CylinderHigh);

    if (signatureLow == ATAPI_MAGIC_LSB && signatureHigh == ATAPI_MAGIC_MSB) {
    }
*/
    KdPrint2((PRINT_PREFIX "UniataSataConnect: OK, ATA status %x\n", Status));
    return IDE_STATUS_IDLE;
} // end UniataSataConnect()

UCHAR
UniataSataPhyEnable(
    IN PVOID HwDeviceExtension,
    IN ULONG lChannel          // logical channel
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    ULONG Channel = deviceExtension->Channel + lChannel;
    PHW_CHANNEL chan = &deviceExtension->chan[lChannel];
    SATA_SCONTROL_REG SControl;
    int loop, retry;

    KdPrint2((PRINT_PREFIX "UniataSataPhyEnable:\n"));
    SControl.Reg = AtapiReadPort4(chan, IDX_SATA_SControl);
    KdPrint2((PRINT_PREFIX "SControl %x\n", SControl.Reg));
    if(SControl.DET == SControl_DET_Idle) {
        return UniataSataConnect(HwDeviceExtension, lChannel);
    }

    for (retry = 0; retry < 10; retry++) {
        KdPrint2((PRINT_PREFIX "UniataSataPhyEnable: retry init %d\n", retry));
	for (loop = 0; loop < 10; loop++) {
	    SControl.Reg = 0;
	    SControl.DET = SControl_DET_Init;
            AtapiWritePort4(chan, IDX_SATA_SControl, SControl.Reg);
            AtapiStallExecution(100);
            SControl.Reg = AtapiReadPort4(chan, IDX_SATA_SControl);
            KdPrint2((PRINT_PREFIX "  SControl 8.8%x\n", SControl.Reg));
            if(SControl.DET == SControl_DET_Init) {
		break;
            }
	}
        AtapiStallExecution(5000);
        KdPrint2((PRINT_PREFIX "UniataSataPhyEnable: retry idle %d\n", retry));
	for (loop = 0; loop < 10; loop++) {
	    SControl.Reg = 0;
	    SControl.DET = SControl_DET_DoNothing;
	    SControl.IPM = SControl_IPM_NoPartialSlumber;
            AtapiWritePort4(chan, IDX_SATA_SControl, SControl.Reg);
            AtapiStallExecution(100);
            SControl.Reg = AtapiReadPort4(chan, IDX_SATA_SControl);
            KdPrint2((PRINT_PREFIX "  SControl 8.8%x\n", SControl.Reg));
            if(SControl.DET == SControl_DET_Idle) {
                return UniataSataConnect(HwDeviceExtension, lChannel);
	    }
	}
    }

    KdPrint2((PRINT_PREFIX "UniataSataPhyEnable: failed\n"));
    return 0xff;
} // end UniataSataPhyEnable()

BOOLEAN
UniataSataEvent(
    IN PVOID HwDeviceExtension,
    IN ULONG lChannel,          // logical channel
    IN ULONG Action
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    UCHAR Status;
    ULONG ldev = lChannel*2;

    switch(Action) {
    case UNIATA_SATA_EVENT_ATTACH:
        KdPrint2((PRINT_PREFIX "  CONNECTED\n"));
        Status = UniataSataConnect(HwDeviceExtension, lChannel);
        KdPrint2((PRINT_PREFIX "  Status %x\n", Status));
        if(Status != IDE_STATUS_IDLE) {
            return FALSE;
        }
        CheckDevice(HwDeviceExtension, lChannel, 0 /*dev*/, FALSE);
        return TRUE;
        break;
    case UNIATA_SATA_EVENT_DETACH:
        KdPrint2((PRINT_PREFIX "  DISCONNECTED\n"));
        deviceExtension->lun[ldev].DeviceFlags = 0;
        return TRUE;
        break;
    }
    return FALSE;
} // end UniataSataEvent()
