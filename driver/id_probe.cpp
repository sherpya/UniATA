/*++

Copyright (c) 2002-2003 Alexandr A. Telyatnikov (Alter)

Module Name:
    id_dma.cpp

Abstract:
    This module scans PCI and ISA buses for IDE controllers
    and determines teir Busmaster DMA capabilities

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

    Some parts of hardware-specific code were taken from FreeBSD 4.3 ATA driver by
         Søren Schmidt, Copyright (c) 1998,1999,2000,2001

    Some parts of device detection code were taken from from standard ATAPI.SYS from NT4 DDK by
         Mike Glass (MGlass)
         Chuck Park (ChuckP)

    Device search/init algorithm is completly rewritten by
         Alter, Copyright (c) 2002-2003

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

UCHAR         pciBuffer[256];
PBUSMASTER_CONTROLLER_INFORMATION BMList = NULL;
ULONG         BMListLen = 0;
ULONG         maxPciBus = 16;
PDRIVER_OBJECT SavedDriverObject = NULL;

// debug

ULONG
AtapiEnumBusMasterController__(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    );

ULONG
AtapiGetIoRange(
    IN PVOID HwDeviceExtension,
    IN PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    IN PCI_COMMON_CONFIG pciData,
    IN ULONG SystemIoBusNumber,
    IN ULONG rid,
    IN ULONG offset,
    IN ULONG length) //range id
{
    ULONG io_start = 0;
    KdPrint2((PRINT_PREFIX "  AtapiGetIoRange:\n"));

    if(ConfigInfo->NumberOfAccessRanges <= rid)
        return 0;

    io_start =
        // Get the system physical address for this IO range.
        ((ULONG)ScsiPortGetDeviceBase(HwDeviceExtension,
                    PCIBus /*ConfigInfo->AdapterInterfaceType*/,
                    SystemIoBusNumber /*ConfigInfo->SystemIoBusNumber*/,
                    ScsiPortConvertUlongToPhysicalAddress(
                        ScsiPortConvertPhysicalAddressToUlong((*ConfigInfo->AccessRanges)[rid].RangeStart) +
                                                                                                offset
                                                         ),
                    length,
                    TRUE) 
                           &
                    ~0x07/*PCI_ADDRESS_IOMASK*/);

    KdPrint2((PRINT_PREFIX "  AtapiGetIoRange: %x\n", io_start));
    if(io_start > offset) {
        return io_start;
    }

    io_start = (pciData.u.type0.BaseAddresses[rid] & ~0x07/*PCI_ADDRESS_IOMASK*/) + offset;
    if(io_start > offset) {
        (*ConfigInfo->AccessRanges)[rid].RangeStart =
            ScsiPortConvertUlongToPhysicalAddress(io_start);
        (*ConfigInfo->AccessRanges)[rid].RangeLength = length;
    } else {
        io_start = 0;
    }
    KdPrint2((PRINT_PREFIX "  AtapiGetIoRange: (2) %x\n", io_start));
    return io_start;

} // end AtapiGetIoRange()

VOID
AtapiEnumBusMasterController(
    IN PVOID DriverObject,
    PVOID Argument2
    )
{
    HW_INITIALIZATION_DATA hwInitializationData;

    // Zero out structure.
    AtapiZeroMemory(((PCHAR)&hwInitializationData), sizeof(HW_INITIALIZATION_DATA));

    // Set size of hwInitializationData.
    hwInitializationData.HwInitializationDataSize =
      sizeof(HW_INITIALIZATION_DATA);

    // Set entry points.
    hwInitializationData.HwInitialize = AtapiHwInitialize;
    hwInitializationData.HwResetBus = AtapiResetController;
    hwInitializationData.HwStartIo = AtapiStartIo;
    hwInitializationData.HwInterrupt = AtapiInterrupt;

    // Specify size of extensions.
    hwInitializationData.DeviceExtensionSize = sizeof(HW_DEVICE_EXTENSION);
    hwInitializationData.SpecificLuExtensionSize = sizeof(HW_LU_EXTENSION);

    // Indicate PIO device.
    hwInitializationData.MapBuffers = TRUE;

    hwInitializationData.HwFindAdapter = AtapiEnumBusMasterController__;
    hwInitializationData.NumberOfAccessRanges = 4;
    hwInitializationData.AdapterInterfaceType = Isa;

    KdPrint2((PRINT_PREFIX "\n\nATAPI IDE Look for PCI Controllers\n"));
    ScsiPortInitialize(DriverObject,
                       Argument2,
                       &hwInitializationData,
                       NULL);
}


ULONG
AtapiEnumBusMasterController__(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    )
{
    PCI_SLOT_NUMBER       slotData;
    PCI_COMMON_CONFIG     pciData;
    ULONG                 busNumber;
    ULONG                 slotNumber;
    ULONG                 funcNumber;
    BOOLEAN               no_buses = FALSE;
    ULONG                 busDataRead;

    UCHAR                 vendorString[5];
    UCHAR                 deviceString[5];
    PUCHAR                vendorStrPtr;
    PUCHAR                deviceStrPtr;

    UCHAR   BaseClass;                  // (ro)
    UCHAR   SubClass;                   // (ro)
    ULONG   VendorID;
    ULONG   DeviceID;
    ULONG   dev_id;

    ULONG   i;
    ULONG   pass=0;

    BOOLEAN found;
    BOOLEAN known;

    vendorStrPtr = vendorString;
    deviceStrPtr = deviceString;
    slotData.u.AsULONG = 0;

    for(pass=0; pass<3; pass++) {
        for(busNumber=0 ;busNumber<maxPciBus && !no_buses; busNumber++) {
            for(slotNumber=0; slotNumber<PCI_MAX_DEVICES  && !no_buses; slotNumber++) {
            for(funcNumber=0; funcNumber<PCI_MAX_FUNCTION && !no_buses; funcNumber++) {
                
//                KdPrint2((PRINT_PREFIX "-- BusID: %x:%x:%x\n",busNumber,slotNumber,funcNumber));
                slotData.u.bits.DeviceNumber   = slotNumber;
                slotData.u.bits.FunctionNumber = funcNumber;

                busDataRead = ScsiPortGetBusData(HwDeviceExtension,
                                            PCIConfiguration, busNumber, slotData.u.AsULONG, 
                                            &pciData, PCI_COMMON_HDR_LENGTH);
                // no more buses
                if(!busDataRead) {
                    no_buses = TRUE;
                    maxPciBus = busNumber;
                    break;
                }
                // no device in this slot
                if(busDataRead == 2)
                    continue;

                if(busDataRead < PCI_COMMON_HDR_LENGTH)
                    continue;

                VendorID  = pciData.VendorID;
                DeviceID  = pciData.DeviceID;
                BaseClass = pciData.BaseClass;
                SubClass  = pciData.SubClass;
                dev_id = VendorID | (DeviceID << 16);
    //            KdPrint2((PRINT_PREFIX "DevId = %8.8X Class = %4.4X/%4.4X\n", dev_id, BaseClass, SubClass ));

                if(BaseClass != PCI_DEV_CLASS_STORAGE)
                    continue;

                KdPrint2((PRINT_PREFIX "Storage Class\n"));
                // look for known chipsets
                found = FALSE;
                known = FALSE;
                for(i=0; i<NUM_BUSMASTER_ADAPTERS; i++) {
                    if(VendorID == BusMasterAdapters[i].nVendorId &&
                       DeviceID == BusMasterAdapters[i].nDeviceId) {
                        KdPrint2((PRINT_PREFIX "Known device\n"));
                        found = TRUE;
                        known = TRUE;
                        break;
                    }
                }

                switch(dev_id) {
                /* additional checks for some supported chipsets */
                case 0xc6931080:
                    if (SubClass != PCI_DEV_SUBCLASS_IDE)
                        found = FALSE;
                    break;

                /* unknown chipsets, try generic DMA if it seems possible */
                default:
                    KdPrint2((PRINT_PREFIX "Default device\n"));
                    if (BaseClass == PCI_DEV_CLASS_STORAGE &&
                        SubClass == PCI_DEV_SUBCLASS_IDE) {
                        found = TRUE;
                    }
                    break;
                }

                if(found) {

                    if(pass) {
                        // fill list of detected devices
                        // it'll be used for further init
                        KdPrint2((PRINT_PREFIX "found suitable device\n"));
                        PBUSMASTER_CONTROLLER_INFORMATION newBMListPtr = BMList+BMListLen;

                        if(pass == 1 && !IsMasterDev(&pciData))
                            continue;
                        if(pass == 2 && IsMasterDev(&pciData))
                            continue;

                        if(known) {
                            RtlCopyMemory(newBMListPtr, (PVOID)&(BusMasterAdapters[i]), sizeof(BUSMASTER_CONTROLLER_INFORMATION));
                        } else {
                            AtapiHexToString(VendorID, (PCHAR*)(&vendorStrPtr));
                            AtapiHexToString(DeviceID, (PCHAR*)(&deviceStrPtr));

                            RtlCopyMemory(&(newBMListPtr->VendorIdStr), (PCHAR)vendorStrPtr, 4);
                            RtlCopyMemory(&(newBMListPtr->DeviceIdStr), (PCHAR)deviceStrPtr, 4);
                            
                            newBMListPtr->nVendorId = VendorID;
                            newBMListPtr->VendorId = (PCHAR)&(newBMListPtr->VendorIdStr);
                            newBMListPtr->VendorIdLength = 4;
                            newBMListPtr->nDeviceId = DeviceID;
                            newBMListPtr->DeviceId = (PCHAR)&(newBMListPtr->DeviceIdStr);
                            newBMListPtr->DeviceIdLength = 4;
                        }
                        newBMListPtr->slotNumber = slotData.u.AsULONG;
                        newBMListPtr->MasterDev = IsMasterDev(&pciData) ? 1 : 0;
                        newBMListPtr->busNumber = busNumber;
                    }

                    BMListLen++;
                }
            }
            }
        }
        if(!pass) {
             if(!BMListLen)
                 break;
             BMList = (PBUSMASTER_CONTROLLER_INFORMATION)ExAllocatePool(NonPagedPool, 
                                         (BMListLen+1)*sizeof(BUSMASTER_CONTROLLER_INFORMATION));
             no_buses = FALSE;
             BMListLen=0;
        }
    }
    return(SP_RETURN_NOT_FOUND);
} // end AtapiEnumBusMasterController__()

/*ULONG
AtapiGetDeviceBase(
    ) {
}*/


ULONG 
ScsiPortGetBusDataByOffset(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  BusNumber,
    IN ULONG  SlotNumber,
    IN PVOID  Buffer,
    IN ULONG  Offset,
    IN ULONG  Length
    )
{
    UCHAR tmp[256];
    ULONG busDataRead;

    if(Offset+Length > 256)
        return 0;

    busDataRead = ScsiPortGetBusData(HwDeviceExtension,
                                     BusDataType,
                                     BusNumber,
                                     SlotNumber,
                                     &tmp,
                                     Offset+Length);
    if(busDataRead < Offset+Length) {
        if(busDataRead < Offset)
            return 0;
        return (Offset+Length-busDataRead);
    }
    RtlCopyMemory(Buffer, tmp+Offset, Length);
    return Length;
}

ULONG
AtapiFindDev(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  BusNumber,
    IN ULONG  SlotNumber,
    IN ULONG  dev_id,
    IN ULONG  RevID
    )
{
    PCI_COMMON_CONFIG     pciData;
    ULONG                 funcNumber;
    ULONG                 busDataRead;

    ULONG   VendorID;
    ULONG   DeviceID;
    PCI_SLOT_NUMBER       slotData;

    slotData.u.AsULONG = SlotNumber;
    for(funcNumber = 0; funcNumber < PCI_MAX_FUNCTION; funcNumber++) {
        
        slotData.u.bits.FunctionNumber = funcNumber;
        if(slotData.u.AsULONG == SlotNumber)
            continue;

        busDataRead = ScsiPortGetBusData(HwDeviceExtension,
                                         PCIConfiguration,
                                         BusNumber,
                                         slotData.u.AsULONG,
                                         &pciData,
                                         PCI_COMMON_HDR_LENGTH);

        if (busDataRead < PCI_COMMON_HDR_LENGTH) {
            continue;
        }

        VendorID  = pciData.VendorID;
        DeviceID  = pciData.DeviceID;

        if(dev_id != (VendorID | (DeviceID << 16)) )
            continue;
        if(RevID >= pciData.RevisionID)
            return 1;
    }
    return 0;
}

VOID
AtapiViaSouthBridgeFixup(
    IN PVOID  HwDeviceExtension,
    IN BUS_DATA_TYPE  BusDataType,
    IN ULONG  SystemIoBusNumber,
    IN ULONG  slotNumber
    )
{
    PCI_COMMON_CONFIG     pciData;
    ULONG                 funcNumber;
    ULONG                 busDataRead;

    ULONG   VendorID;
    ULONG   DeviceID;
    PCI_SLOT_NUMBER       slotData;
    ULONG dev_id;

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
	    break;
	}
    }
}

/*++

Routine Description:

    This function is called by the OS-specific port driver after
    the necessary storage has been allocated, to gather information
    about the adapter's configuration.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Context - Address of adapter count
    BusInformation -
    ArgumentString - Used to determine whether driver is client of ntldr or crash dump utility.
    ConfigInfo - Configuration information structure describing HBA
    Again - Indicates search for adapters to continue

Return Value:

    ULONG

--*/
ULONG
AtapiFindBusMasterController(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    )
{
    PHW_DEVICE_EXTENSION  deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PCI_COMMON_CONFIG     pciData;
    ULONG                 slotNumber;
    ULONG                 busDataRead;
    ULONG                 SystemIoBusNumber;

    UCHAR                 vendorString[5];
    UCHAR                 deviceString[5];
    PUCHAR                vendorStrPtr;
    PUCHAR                deviceStrPtr;

    UCHAR   BaseClass;
    UCHAR   SubClass;
    ULONG   VendorID;
    ULONG   DeviceID;
    ULONG   RevID;
    ULONG   dev_id;
    PCI_SLOT_NUMBER       slotData;

    ULONG   i = (ULONG)Context;
    ULONG   channel = BMList[i].channel;
    ULONG   c = 0;
    PUCHAR  ioSpace;
    UCHAR   statusByte;
    ULONG   bm_offset = channel ? ATA_BM_OFFSET1 : 0;

    UCHAR   tmp8;
//    ULONG   irq;

    BOOLEAN found = FALSE;
    BOOLEAN MasterDev;
    BOOLEAN simplexOnly = FALSE;
    BOOLEAN skip_find_dev = FALSE;

    SCSI_PHYSICAL_ADDRESS IoBasePort1;
    SCSI_PHYSICAL_ADDRESS IoBasePort2;

    NTSTATUS status;
    PPORT_CONFIGURATION_INFORMATION_COMMON _ConfigInfo =
        (PPORT_CONFIGURATION_INFORMATION_COMMON)ConfigInfo;

//    PORT_CONFIGURATION_INFORMATION SavedConfigInfo = *ConfigInfo;
    *Again = FALSE;

    if (!deviceExtension) {
        return SP_RETURN_ERROR;
    }

    vendorStrPtr = vendorString;
    deviceStrPtr = deviceString;

/*    if(!WinVer_IsNT) {
        for(i=0;
    }*/

    slotNumber = BMList[i].slotNumber;
    SystemIoBusNumber = BMList[i].busNumber;

    KdPrint2((PRINT_PREFIX "IoBusNumber=%x\n",ConfigInfo->SystemIoBusNumber));
    KdPrint2((PRINT_PREFIX "slotNumber=%x\n",slotNumber));

    busDataRead = ScsiPortGetBusData(HwDeviceExtension,
                                     PCIConfiguration,
                                     SystemIoBusNumber,
                                     slotNumber,
                                     &pciData,
                                     PCI_COMMON_HDR_LENGTH);

    if (busDataRead < PCI_COMMON_HDR_LENGTH) {
        return SP_RETURN_ERROR;
    }

    KdPrint2((PRINT_PREFIX "busDataRead\n"));
    if (pciData.VendorID == PCI_INVALID_VENDORID) {
        KdPrint2((PRINT_PREFIX "PCI_INVALID_VENDORID\n"));
        return SP_RETURN_ERROR;
    }

    VendorID  = pciData.VendorID;
    DeviceID  = pciData.DeviceID;
    BaseClass = pciData.BaseClass;
    SubClass  = pciData.SubClass;
    RevID     = pciData.RevisionID;
    dev_id = VendorID | (DeviceID << 16);
    slotData.u.AsULONG = slotNumber;
    KdPrint2((PRINT_PREFIX "DevId = %8.8X Class = %4.4X/%4.4X\n", dev_id, BaseClass, SubClass ));

    if(BaseClass != PCI_DEV_CLASS_STORAGE)
        return(SP_RETURN_NOT_FOUND);

    KdPrint2((PRINT_PREFIX "Storage Class\n"));

    // look for known chipsets
    if(VendorID != BMList[i].nVendorId ||
       DeviceID != BMList[i].nDeviceId) {
        KdPrint2((PRINT_PREFIX "device not suitable\n"));
        return(SP_RETURN_NOT_FOUND);
    }

    found = TRUE;
    switch(dev_id) {
    /* additional checks for some supported chipsets */
    case 0xc6931080:
        if (SubClass != PCI_DEV_SUBCLASS_IDE)
            found = FALSE;
        break;

    /* unknown chipsets, try generic DMA if it seems possible */
    default:
        KdPrint2((PRINT_PREFIX "Default device\n"));
        if (found)
            break;
        if (BaseClass != PCI_DEV_CLASS_STORAGE ||
            SubClass != PCI_DEV_SUBCLASS_IDE) {
            found = FALSE;
        }
        break;
    }

    if(!found)
        return(SP_RETURN_NOT_FOUND);

    KdPrint2((PRINT_PREFIX "found suitable device\n"));

    /***********************************************************/
    /***********************************************************/
    /***********************************************************/

    deviceExtension->NumberChannels = 1;
    MasterDev = IsMasterDev(&pciData);

    if(MasterDev) {
        if((channel==0) && ConfigInfo->AtdiskPrimaryClaimed)
            return(SP_RETURN_NOT_FOUND);
        if((channel==1) && ConfigInfo->AtdiskSecondaryClaimed)
            return(SP_RETURN_NOT_FOUND);
    }

/*    if(!MasterDev) {
        IoBasePort1 = (*ConfigInfo->AccessRanges)[2].RangeStart;

        if ( !MasterDev &&
             ScsiPortConvertPhysicalAddressToUlong(IoBasePort1) )
            deviceExtension->NumberChannels = 2;
    }*/

    deviceExtension->BusMaster = FALSE;
    if(IsBusMaster(&pciData)) {
        deviceExtension->BaseIoAddressBM_0 = (PIDE_BUSMASTER_REGISTERS)
            (AtapiGetIoRange(HwDeviceExtension, ConfigInfo, pciData, SystemIoBusNumber,
                            4, bm_offset, MasterDev ? 0x08 : 0x10/*ATA_BMIOSIZE*/) - bm_offset); //range id
        if(deviceExtension->BaseIoAddressBM_0) {
            deviceExtension->BusMaster = TRUE;
        }
        KdPrint2((PRINT_PREFIX "  BusMasterAddress (base): %x\n", deviceExtension->BaseIoAddressBM_0));
    }

    if(!WinVer_IsNT) {
        //simplexOnly = TRUE;
    }

    if(deviceExtension->BusMaster && !MasterDev) {
        statusByte = ScsiPortReadPortUchar((PUCHAR)&(deviceExtension->BaseIoAddressBM_0->Status));
        if(statusByte & BM_STATUS_SIMPLEX_ONLY) {
            KdPrint2((PRINT_PREFIX "  simplexOnly\n"));
            simplexOnly = TRUE;
        }
    }

    ConfigInfo->AlignmentMask = 0x00000003;
    
    /* do extra chipset specific setups */
    switch (dev_id) {
    case 0x522910b9: /* AcerLabs Aladdin need to activate the ATAPI FIFO */
        ChangePciConfig1(0x53, ((a & ~0x01) | 0x02));
	break;

    case 0x0d30105a: /* Promise 66 & 100 (before TX2) need the clock changed */
    case 0x4d30105a:
    case 0x0d38105a:
    case 0x4d38105a:
        if(deviceExtension->BaseIoAddressBM_0) {
            statusByte = ScsiPortReadPortUchar((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + 0x11);
            ScsiPortWritePortUchar((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + 0x11, statusByte | 0x0a);
        }
	/* FALLTHROUGH */

    case 0x4d33105a: /* Promise (before TX2) need burst mode to be turned on */
        if(deviceExtension->BaseIoAddressBM_0) {
            statusByte = ScsiPortReadPortUchar((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + 0x1f);
            ScsiPortWritePortUchar((PUCHAR)(deviceExtension->BaseIoAddressBM_0) + 0x1f, statusByte | 0x01);
	}
	break;

    case 0x00041103: /* HighPoint HPT366/368/370/372 default setup */
	if (RevID <= 0x01) {
	    /* turn off interrupt prediction */
            ChangePciConfig1(0x51, (a & ~0x80));
	    break;
	}
	if (RevID <= 0x04) {
	    /* turn off interrupt prediction */
            ChangePciConfig1(0x51, (a & ~0x03));
            ChangePciConfig1(0x55, (a & ~0x03));
	    /* turn on interrupts */
            ChangePciConfig1(0x5a, (a & ~0x10));
	    /* set clocks etc */
            SetPciConfig1(0x5b, 0x22);
	    break;
	}
	/* FALLTHROUGH */

    case 0x00051103:	/* HighPoint HPT372 */
    case 0x00081103:	/* HighPoint HPT374 */
        /* turn off interrupt prediction */
        ChangePciConfig1(0x51, (a & ~0x03));
        ChangePciConfig1(0x55, (a & ~0x03));
        /* turn on interrupts */
        ChangePciConfig1(0x5a, (a & ~0x10));
        /* set clocks etc */
        ChangePciConfig1(0x5b, (a & ~0x01) | 0x20);
	break;

    case 0x05711106: /* VIA 82C586, '596, '686 default setup */

	/* prepare for ATA-66 on the 82C686a and rev 0x12 and newer 82C596's */
	if ((AtapiFindDev(HwDeviceExtension, PCIConfiguration,
                         SystemIoBusNumber, slotNumber, 0x06861106, 0) &&
	    !AtapiFindDev(HwDeviceExtension, PCIConfiguration,
                         SystemIoBusNumber, slotNumber, 0x06861106, 0x40)) 
                              ||
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration,
                         SystemIoBusNumber, slotNumber, 0x05961106, 0x12)) {
            ChangePciConfig4(0x50, (a | 0x030b030b));
	}

	/* the '686b might need the data corruption fix */
	if (AtapiFindDev(HwDeviceExtension, PCIConfiguration,
                         SystemIoBusNumber, slotNumber, 0x06861106, 0x40) ||
	    AtapiFindDev(HwDeviceExtension, PCIConfiguration,
                         SystemIoBusNumber, slotNumber, 0x82311106, 0x10))
	    AtapiViaSouthBridgeFixup(HwDeviceExtension, PCIConfiguration,
                                     SystemIoBusNumber, slotNumber);


	/* FALLTHROUGH */

    case 0x74091022: /* AMD 756 default setup */
    case 0x74111022: /* AMD 766 default setup */
    case 0x74411022: /* AMD 768 default setup */
    case 0x01bc10de: /* nVIDIA nForce default setup */

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

    case 0x02111166: /* ServerWorks ROSB4 enable UDMA33 */
        ChangePciConfig4(0x64, ((a & ~0x00002000) | 0x00004000));
	break;
	
    case 0x02121166: /* ServerWorks CSB5 enable UDMA66/100 depending on rev */
        ChangePciConfig1(0x5a, ((a & ~0x40) | ((RevID >= 0x92) ? 0x03 : 0x02)));
	break;

    case 0x06801095: /* Sil 0680 set ATA reference clock speed */
        GetPciConfig1(0x8a, tmp8);
	if ((tmp8 & 0x30) != 0x10)
	    ChangePciConfig1(0x8a, (a & 0x0F) | 0x10);
        GetPciConfig1(0x8a, tmp8);
	if ((tmp8 & 0x30) != 0x10) {
            KdPrint2((PRINT_PREFIX "Sil 0680 could not set clock"));
        }
	break;

    case 0x06461095: /* CMD 646 enable interrupts, set DMA read mode */
	SetPciConfig1(0x71, 0x01);
	break;

    case 0x01021078:	/* Cyrix 5530 ATA33 controller */
        ConfigInfo->AlignmentMask = 0x0f;
        break;

    case 0x06401095:	/* CMD 640 known bad, no DMA */
    case 0x06011039:
        simplexOnly = TRUE; 

        /* FALLTHROUGH */

    case 0x10001042:	/* RZ 100? known bad, no DMA */
    case 0x10011042:

        if(deviceExtension->BaseIoAddressBM_0)
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddressBM_0);

	deviceExtension->BaseIoAddressBM_0 = 0;
        deviceExtension->BusMaster = FALSE;
    }

    /*
     * the Cypress chip is a mess, it contains two ATA functions, but 
     * both channels are visible on the first one.
     * simply ignore the second function for now, as the right
     * solution (ignoring the second channel on the first function)
     * doesn't work with the crappy ATA interrupt setup on the alpha.
     */
    if (dev_id == 0xc6931080 && slotData.u.bits.FunctionNumber > 1) {
        goto exit_findbm;
    }

    if(simplexOnly || !MasterDev || !WinVer_IsNT) {
        deviceExtension->NumberChannels = 2;
    }
    deviceExtension->simplexOnly = simplexOnly;
    if((channel > 0) &&
       (deviceExtension->NumberChannels > 1)) {
        goto exit_findbm;
    }

    // Indicate number of buses.
    ConfigInfo->NumberOfBuses = (UCHAR)(deviceExtension->NumberChannels);
    if(!ConfigInfo->InitiatorBusId[0]) {
        ConfigInfo->InitiatorBusId[0] = (CHAR)(IoGetConfigurationInformation()->ScsiPortCount);
/*        if(ConfigInfo->NumberOfBuses > 1)
            ConfigInfo->InitiatorBusId[1] = (CHAR)(IoGetConfigurationInformation()->ScsiPortCount)+1;*/
    }
    // Indicate four devices can be attached to the adapter
    ConfigInfo->MaximumNumberOfTargets = (UCHAR)(/*deviceExtension->NumberChannels **/ 2);
    ConfigInfo->MaximumTransferLength = DEV_BSIZE*256;

    if (MasterDev) {
        if(simplexOnly || !WinVer_IsNT) {
            if (ConfigInfo->AdapterInterfaceType == MicroChannel) {
                ConfigInfo->InterruptMode2 =
                ConfigInfo->InterruptMode = LevelSensitive;
            } else {
                ConfigInfo->InterruptMode2 =
                ConfigInfo->InterruptMode = Latched;
            }
            ConfigInfo->BusInterruptLevel = 14;
            ConfigInfo->BusInterruptLevel2 = 15;
        } else {
            if (ConfigInfo->AdapterInterfaceType == MicroChannel) {
                ConfigInfo->InterruptMode = LevelSensitive;
            } else {
                ConfigInfo->InterruptMode = Latched;
            }
            ConfigInfo->BusInterruptLevel = (channel == 0 ? 14 : 15);
        }
    } else {
        ConfigInfo->SlotNumber = slotNumber;
        ConfigInfo->SystemIoBusNumber = SystemIoBusNumber;
        /* primary and secondary channels share the same interrupt */
        if(!ConfigInfo->BusInterruptVector || 
            (ConfigInfo->BusInterruptVector != pciData.u.type0.InterruptLine)) {
            ConfigInfo->BusInterruptVector = pciData.u.type0.InterruptLine;  // set default value
            if(!ConfigInfo->BusInterruptVector)
                ConfigInfo->BusInterruptVector = 10;
        }
        if(!simplexOnly) {
/*            ConfigInfo->TaggedQueuing = TRUE;
            ConfigInfo->MultipleRequestPerLu = TRUE;*/
        }
    }
    ConfigInfo->MultipleRequestPerLu = TRUE;
    ConfigInfo->AutoRequestSense = TRUE;
    ConfigInfo->TaggedQueuing = TRUE;
    // Save the Interrupe Mode for later use
    deviceExtension->InterruptMode = ConfigInfo->InterruptMode;
    deviceExtension->BusInterruptLevel = ConfigInfo->BusInterruptLevel;
    deviceExtension->Channel = channel;
    deviceExtension->slotNumber = slotNumber;
    deviceExtension->SystemIoBusNumber = SystemIoBusNumber;
    deviceExtension->DevID = dev_id;
    deviceExtension->RevID = RevID;
    deviceExtension->DevIndex = i;

    found = FALSE;

    if(deviceExtension->BusMaster /*&& found*/) {
        
        KdPrint2((PRINT_PREFIX "Reconstruct ConfigInfo\n"));
        ConfigInfo->MapBuffers = TRUE;
        ConfigInfo->NeedPhysicalAddresses = TRUE;
        if(!MasterDev) {
            ConfigInfo->Dma32BitAddresses = TRUE;
        }
//        if(MasterDev) {
            ConfigInfo->AdapterInterfaceType = PCIBus;
//        }

        ConfigInfo->Master = TRUE;
        ConfigInfo->DmaWidth = Width16Bits;
        ConfigInfo->CachesData = TRUE;
        ConfigInfo->ScatterGather = TRUE;

/*        if(!MasterDev && BMList[i].channel) {
            slotData.u.bits.FunctionNumber ++;
            slotNumber = slotData.u.AsULONG;
        }*/

        ConfigInfo->SlotNumber = slotNumber;
        ConfigInfo->SystemIoBusNumber = SystemIoBusNumber;
    }

    for (; channel < (BMList[i].channel + deviceExtension->NumberChannels); channel++, c++) {

        KdPrint2((PRINT_PREFIX "Channel %x\n",channel));

//        KeInitializeSpinLock(&(deviceExtension->chan[c].QueueSpinLock));
//        KeInitializeDpc(&(deviceExtension->chan[c].Dpc), AtapiDpcDispatch, (PVOID)((ULONG)deviceExtension | channel));
//        KeInitializeTimer(&(deviceExtension->chan[c].Timer));
        deviceExtension->chan[c].DeviceExtension = deviceExtension;
        deviceExtension->chan[c].lun[0] = &(deviceExtension->lun[c*2+0]);
        deviceExtension->chan[c].lun[1] = &(deviceExtension->lun[c*2+1]);

        // Check if the range specified is not used by another driver
        if(MasterDev) {
            (*ConfigInfo->AccessRanges)[channel * 2 + 0].RangeStart =
                ScsiPortConvertUlongToPhysicalAddress(channel ? IO_WD2 : IO_WD1);
            (*ConfigInfo->AccessRanges)[channel * 2 + 0].RangeLength = ATA_IOSIZE;

            (*ConfigInfo->AccessRanges)[channel * 2 + 1].RangeStart =
                ScsiPortConvertUlongToPhysicalAddress((channel ? IO_WD2 : IO_WD1) + ATA_ALTOFFSET);
            (*ConfigInfo->AccessRanges)[channel * 2 + 1].RangeLength = ATA_ALTIOSIZE;
/*        } else {
            AtapiGetIoRange(HwDeviceExtension, ConfigInfo, pciData, SystemIoBusNumber,
                            channel * 2 + 0, ATA_IOSIZE); //range id
            AtapiGetIoRange(HwDeviceExtension, ConfigInfo, pciData, SystemIoBusNumber,
                            channel * 2 + 1, ATA_ALTIOSIZE); //range id*/
        }

        IoBasePort1 = (*ConfigInfo->AccessRanges)[channel * 2 + 0].RangeStart;
        IoBasePort2 = (*ConfigInfo->AccessRanges)[channel * 2 + 1].RangeStart;

        if(!ScsiPortValidateRange(HwDeviceExtension,
                                  PCIBus /*ConfigInfo->AdapterInterfaceType*/,
                                  SystemIoBusNumber /*ConfigInfo->SystemIoBusNumber*/,
                                  IoBasePort1,
                                  ATA_IOSIZE,
                                  TRUE) )
            continue;
        
        if(!ScsiPortValidateRange(HwDeviceExtension,
                                  PCIBus /*ConfigInfo->AdapterInterfaceType*/,
                                  SystemIoBusNumber /*ConfigInfo->SystemIoBusNumber*/,
                                  IoBasePort2,
                                  ATA_ALTIOSIZE,
                                  TRUE) )
            continue;

        KdPrint2((PRINT_PREFIX "Getting IO ranges\n"));

        // Ok, translate adresses to io-space
        if(ScsiPortConvertPhysicalAddressToUlong(IoBasePort2)) {
            if(!(MasterDev /* || USE_16_BIT */)) {
                IoBasePort2 = ScsiPortConvertUlongToPhysicalAddress(
                                  ScsiPortConvertPhysicalAddressToUlong(IoBasePort2) + 2);
            }
        } else {
            IoBasePort2 = ScsiPortConvertUlongToPhysicalAddress(
                              ScsiPortConvertPhysicalAddressToUlong(IoBasePort1) + ATA_PCCARD_ALTOFFSET);
        }

        // Get the system physical address for this IO range.
        ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                        PCIBus /*ConfigInfo->AdapterInterfaceType*/,
                                        SystemIoBusNumber /*ConfigInfo->SystemIoBusNumber*/,
                                        IoBasePort1,
                                        ATA_IOSIZE,
                                        TRUE);
        KdPrint2((PRINT_PREFIX "IO range 1 %x\n",ioSpace));

        // Check if ioSpace accessible.
        if (!ioSpace) {
            continue;
        }

        if(deviceExtension->BusMaster) {
            deviceExtension->BaseIoAddressBM[c] = (PIDE_BUSMASTER_REGISTERS)
                ((ULONG)(deviceExtension->BaseIoAddressBM_0) + bm_offset + (c ? ATA_BM_OFFSET1 : 0));
        }

        KdPrint2((PRINT_PREFIX "Check drive 0\n"));
        // Check master.
        SelectDrive((PIDE_REGISTERS_1)ioSpace, 0);
        ScsiPortStallExecution(10);
        GetBaseStatus((PIDE_REGISTERS_1)ioSpace, statusByte);
        skip_find_dev = FALSE;
        if ((statusByte & 0xf8) == 0xf8 || 
            (statusByte == 0xa5)) {
            // Check slave.
            KdPrint2((PRINT_PREFIX "Check drive 1\n"));
            SelectDrive((PIDE_REGISTERS_1)ioSpace, 1);
            ScsiPortStallExecution(1);
            GetBaseStatus((PIDE_REGISTERS_1)ioSpace, statusByte);
            if ((statusByte & 0xf8) == 0xf8 || 
                (statusByte == 0xa5)) {
                // No controller at this base address.
                KdPrint2((PRINT_PREFIX "Empty channel\n"));
free_iospace_1:
                // Release all allocated resources
/*                ScsiPortFreeDeviceBase(HwDeviceExtension,
                                       ioSpace);
                __asm int 3;
                continue;*/
                skip_find_dev = TRUE;
            }
        }
        deviceExtension->BaseIoAddress1[c] = (PIDE_REGISTERS_1)(ioSpace);

        // Get the system physical address for the second IO range.
        ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                        PCIBus /*ConfigInfo->AdapterInterfaceType*/,
                                        SystemIoBusNumber /*ConfigInfo->SystemIoBusNumber*/,
                                        IoBasePort2,
                                        ATA_ALTIOSIZE,
                                        TRUE);
        KdPrint2((PRINT_PREFIX "IO range 2 %x\n",ioSpace));

        deviceExtension->BaseIoAddress2[c] = (PIDE_REGISTERS_2)(ioSpace);
        if(!deviceExtension->BaseIoAddress2[c]) {
            // Release all allocated resources
            ioSpace = (PUCHAR)(deviceExtension->BaseIoAddress1[c]);
            goto free_iospace_1;
        }

        // Search for devices on this controller.
        if (!skip_find_dev &&
            FindDevices(HwDeviceExtension,
                    FALSE,
                    c)) {
            KdPrint2((PRINT_PREFIX "Found some devices\n"));
            found = TRUE;
        } else {
/*            KeBugCheckEx(0xc000000e,
                         ScsiPortConvertPhysicalAddressToUlong(IoBasePort1),
                         ScsiPortConvertPhysicalAddressToUlong(IoBasePort2), 
                         (ULONG)(deviceExtension->BaseIoAddressBM[c]), skip_find_dev);*/
        }
        found = TRUE;

        deviceExtension->chan[c].PrimaryAddress = FALSE;
        // Claim primary or secondary ATA IO range.
/*        if (ScsiPortConvertPhysicalAddressToUlong(IoBasePort1) == IO_WD1) {
            ConfigInfo->AtdiskPrimaryClaimed = TRUE;
            deviceExtension->chan[c].PrimaryAddress = TRUE;

        } else
        if (ScsiPortConvertPhysicalAddressToUlong(IoBasePort1) == IO_WD2) {
            ConfigInfo->AtdiskSecondaryClaimed = TRUE;
        }*/
        if (MasterDev) {
            if (channel == 0) {
                ConfigInfo->AtdiskPrimaryClaimed = TRUE;
                deviceExtension->chan[c].PrimaryAddress = TRUE;

            } else
            if (channel == 1) {
                ConfigInfo->AtdiskSecondaryClaimed = TRUE;
            }
        }

        AtapiDmaAlloc(HwDeviceExtension, ConfigInfo, c);
    }

exit_findbm:

    if(!found) {
        if(deviceExtension->BaseIoAddress1[0])
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddress1[0]);
        if(deviceExtension->BaseIoAddress2[0])
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddress2[0]);

        if(deviceExtension->BaseIoAddress1[1])
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddress1[1]);
        if(deviceExtension->BaseIoAddress2[1])
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddress2[1]);

        if(deviceExtension->BaseIoAddressBM_0)
            ScsiPortFreeDeviceBase(HwDeviceExtension,
                                   deviceExtension->BaseIoAddressBM_0);
        
/*        if(channel == 1) {
            BMList[i].channel = 1;
            *ConfigInfo = SavedConfigInfo;
            return AtapiFindBusMasterController(HwDeviceExtension,
                                                Context,
                                                BusInformation,
                                                ArgumentString,
                                                ConfigInfo,
                                                Again
                                                );
        }*/
        
        return SP_RETURN_NOT_FOUND;
    } else {
/*        if(channel == 1) {
            BMList[i].channel = 1;
            *Again = TRUE;
        }*/
        KeInitializeSpinLock(&(deviceExtension->DpcSpinLock));
        deviceExtension->ActiveDpcChan =
        deviceExtension->FirstDpcChan = -1;
        deviceExtension->DpcTime1 =
        deviceExtension->DpcTime2 = 0;

        BMList[i].Isr2Enable = FALSE;

        if(!MasterDev && (deviceExtension->NumberChannels > 1) &&   // do not touch MasterDev
           !BMList[i].Isr2DevObj) {                       // handle re-init under w2k+

            PISR2_DEVICE_EXTENSION Isr2DevExt;

//            __asm int 3;

            status = IoCreateDevice(SavedDriverObject, sizeof(ISR2_DEVICE_EXTENSION),
                                    NULL/*devname*/, FILE_DEVICE_UNKNOWN,
                                    0, FALSE, &(BMList[i].Isr2DevObj));

            if(!NT_SUCCESS(status)) {
                found = FALSE;
                goto exit_findbm;
            }

            BMList[i].Isr2Vector = HalGetInterruptVector(
                ConfigInfo->AdapterInterfaceType,
                ConfigInfo->SystemIoBusNumber,
                ConfigInfo->BusInterruptLevel,
                ConfigInfo->BusInterruptVector,
                &(BMList[i].Isr2Irql),
                &(BMList[i].Isr2Affinity));

            Isr2DevExt = (PISR2_DEVICE_EXTENSION)(BMList[i].Isr2DevObj->DeviceExtension);
            Isr2DevExt->HwDeviceExtension = deviceExtension;
            Isr2DevExt->DevIndex = i;

//            deviceExtension->QueueNewIrql = BMList[i].Isr2Irql;
            
            status = IoConnectInterrupt(
                &(BMList[i].Isr2InterruptObject),
                AtapiInterrupt2,
                Isr2DevExt,
                NULL,
                BMList[i].Isr2Vector,
                BMList[i].Isr2Irql,
                BMList[i].Isr2Irql,
                ConfigInfo->InterruptMode,
                TRUE,
                BMList[i].Isr2Affinity,
                FALSE);

            if(!NT_SUCCESS(status)) {
                IoDeleteDevice(BMList[i].Isr2DevObj);
                found = FALSE;
                goto exit_findbm;
            }

        }
    }

    return SP_RETURN_FOUND;

} // end AtapiFindBusMasterController()



/*++

Routine Description:

    This function is called by the OS-specific port driver after
    the necessary storage has been allocated, to gather information
    about the adapter's configuration.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    Context - Address of adapter count
    ArgumentString - Used to determine whether driver is client of ntldr or crash dump utility.
    ConfigInfo - Configuration information structure describing HBA
    Again - Indicates search for adapters to continue

Return Value:

    ULONG

--*/
ULONG
AtapiFindController(
    IN PVOID HwDeviceExtension,
    IN PVOID Context,
    IN PVOID BusInformation,
    IN PCHAR ArgumentString,
    IN OUT PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    OUT PBOOLEAN Again
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL          chan = &(deviceExtension->chan[0]);
    PULONG               adapterCount    = (PULONG)Context;
    PUCHAR               ioSpace;
    ULONG                i;
    ULONG                irq;
    ULONG                portBase;
    ULONG                retryCount;
/*    PCI_SLOT_NUMBER      slotData;
    PPCI_COMMON_CONFIG   pciData;*/
//    ULONG                pciBuffer;
    BOOLEAN              atapiOnly;
    UCHAR                statusByte;
    BOOLEAN              preConfig = FALSE;
    //
    // The following table specifies the ports to be checked when searching for
    // an IDE controller.  A zero entry terminates the search.
    CONST ULONG AdapterAddresses[5] = {IO_WD1, IO_WD2, IO_WD1-8, IO_WD2-8, 0};
    CONST UCHAR Channels[5] = {0, 1, 0, 1, 0};

    // The following table specifies interrupt levels corresponding to the
    // port addresses in the previous table.
    CONST ULONG InterruptLevels[5] = {14, 15, 11, 10, 0};

    if (!deviceExtension) {
        return SP_RETURN_ERROR;
    }
    // Check to see if this is a special configuration environment.
    portBase = irq = 0;
    if (ArgumentString) {

        irq = AtapiParseArgumentString(ArgumentString, "Interrupt");
        if (irq ) {

            // Both parameters must be present to proceed
            portBase = AtapiParseArgumentString(ArgumentString, "BaseAddress");
            if (!portBase) {

                // Try a default search for the part.
                irq = 0;
            }
        }
    }

    // Scan though the adapter address looking for adapters.
    if (ScsiPortConvertPhysicalAddressToUlong((*ConfigInfo->AccessRanges)[0].RangeStart) != 0) {
        ioSpace =  (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                         ConfigInfo->AdapterInterfaceType,
                                         ConfigInfo->SystemIoBusNumber,
                                         (*ConfigInfo->AccessRanges)[0].RangeStart,
                                         (*ConfigInfo->AccessRanges)[0].RangeLength,
                                         (BOOLEAN) !((*ConfigInfo->AccessRanges)[0].RangeInMemory));
        *Again = FALSE;
        // Since we have pre-configured information we only need to go through this loop once
        preConfig = TRUE;
        portBase = ScsiPortConvertPhysicalAddressToUlong((*ConfigInfo->AccessRanges)[0].RangeStart);
    }

    while (AdapterAddresses[*adapterCount] != 0) {

        retryCount = 4;

        for (i = 0; i < 4; i++) {
            // Zero device fields to ensure that if earlier devices were found,
            // but not claimed, the fields are cleared.
            deviceExtension->lun[i].DeviceFlags &= ~(DFLAGS_ATAPI_DEVICE | DFLAGS_DEVICE_PRESENT | DFLAGS_TAPE_DEVICE);
        }
        // Get the system physical address for this IO range.

        // Check if configInfo has the default information
        // if not, we go and find ourselves
        if (preConfig == FALSE) {

            if (portBase) {
                ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                                ConfigInfo->AdapterInterfaceType,
                                                ConfigInfo->SystemIoBusNumber,
                                                ScsiPortConvertUlongToPhysicalAddress(portBase),
                                                8,
                                                TRUE);
            } else {
                ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                                ConfigInfo->AdapterInterfaceType,
                                                ConfigInfo->SystemIoBusNumber,
                                                ScsiPortConvertUlongToPhysicalAddress(AdapterAddresses[*adapterCount]),
                                                8,
                                                TRUE);
            }

        }// ConfigInfo check

        // Update the adapter count.
        (*adapterCount)++;

        // Check if ioSpace accessible.
        if (!ioSpace) {
            continue;
        }
        // check if Primary/Secondary Master IDE claimed
        if((ioSpace == (PUCHAR)IO_WD1) &&
           (ConfigInfo->AtdiskPrimaryClaimed)) {
            continue;
        } else
        if((ioSpace == (PUCHAR)IO_WD2) &&
           (ConfigInfo->AtdiskSecondaryClaimed)) {
            continue;
        }

retryIdentifier:

        // Select master.
        SelectDrive((PIDE_REGISTERS_1)ioSpace, 0);

        // Check if card at this address.
        ScsiPortWritePortUchar(&((PIDE_REGISTERS_1)ioSpace)->o.CylinderLow, 0xAA);

        // Check if indentifier can be read back.
        if ((statusByte = ScsiPortReadPortUchar(&((PIDE_REGISTERS_1)ioSpace)->i.CylinderLow)) != 0xAA) {

            KdPrint2((PRINT_PREFIX "AtapiFindController: Identifier read back from Master (%x)\n",
                        statusByte));

            statusByte = ScsiPortReadPortUchar(&((PATAPI_REGISTERS_2)ioSpace)->AlternateStatus);

            if (statusByte & IDE_STATUS_BUSY) {

                i = 0;

                // Could be the TEAC in a thinkpad. Their dos driver puts it in a sleep-mode that
                // warm boots don't clear.
                do {
                    ScsiPortStallExecution(1000);
                    statusByte = ScsiPortReadPortUchar(&((PATAPI_REGISTERS_1)ioSpace)->i.Status);
                    KdPrint2((PRINT_PREFIX 
                                "AtapiFindController: First access to status %x\n",
                                statusByte));
                } while ((statusByte & IDE_STATUS_BUSY) && ++i < 10);

                if (retryCount-- && (!(statusByte & IDE_STATUS_BUSY))) {
                    goto retryIdentifier;
                }
            }

            // Select slave.
            SelectDrive((PIDE_REGISTERS_1)ioSpace, 1);

            // See if slave is present.
            ScsiPortWritePortUchar(&((PIDE_REGISTERS_1)ioSpace)->o.CylinderLow, 0xAA);

            if ((statusByte = ScsiPortReadPortUchar(&((PIDE_REGISTERS_1)ioSpace)->i.CylinderLow)) != 0xAA) {

                KdPrint2((PRINT_PREFIX 
                            "AtapiFindController: Identifier read back from Slave (%x)\n",
                            statusByte));

                // No controller at this base address.
                ScsiPortFreeDeviceBase(HwDeviceExtension,
                                       ioSpace);
                continue;
            }
        }

        // Record base IO address.
        deviceExtension->BaseIoAddress1[0] = (PIDE_REGISTERS_1)(ioSpace);

        // Fill in the access array information only if default params are not in there.
        if (preConfig == FALSE) {

            // An adapter has been found request another call, only if we didn't get preconfigured info.
            *Again = TRUE;

            if (portBase) {
                (*ConfigInfo->AccessRanges)[0].RangeStart = ScsiPortConvertUlongToPhysicalAddress(portBase);
            } else {
                (*ConfigInfo->AccessRanges)[0].RangeStart =
                    ScsiPortConvertUlongToPhysicalAddress(AdapterAddresses[*adapterCount - 1]);
            }

            (*ConfigInfo->AccessRanges)[0].RangeLength = 8;
            (*ConfigInfo->AccessRanges)[0].RangeInMemory = FALSE;

            // Indicate the interrupt level corresponding to this IO range.
            if (irq) {
                ConfigInfo->BusInterruptLevel = irq;
            } else {
                ConfigInfo->BusInterruptLevel = InterruptLevels[*adapterCount - 1];
            }

            if (ConfigInfo->AdapterInterfaceType == MicroChannel) {
                ConfigInfo->InterruptMode = LevelSensitive;
            } else {
                ConfigInfo->InterruptMode = Latched;
            }
        }

        // Get the system physical address for the second IO range.
        if (portBase) {
            ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                            ConfigInfo->AdapterInterfaceType,
                                            ConfigInfo->SystemIoBusNumber,
                                            ScsiPortConvertUlongToPhysicalAddress(portBase + ATA_ALTOFFSET),
                                            ATA_ALTIOSIZE,
                                            TRUE);
        } else {
            ioSpace = (PUCHAR)ScsiPortGetDeviceBase(HwDeviceExtension,
                                            ConfigInfo->AdapterInterfaceType,
                                            ConfigInfo->SystemIoBusNumber,
                                            ScsiPortConvertUlongToPhysicalAddress(AdapterAddresses[*adapterCount - 1] + ATA_ALTOFFSET),
                                            ATA_ALTIOSIZE,
                                            TRUE);
        }

        deviceExtension->BaseIoAddress2[0] = (PIDE_REGISTERS_2)(ioSpace);

        deviceExtension->NumberChannels = 1;

        ConfigInfo->NumberOfBuses = 1;
        ConfigInfo->MaximumNumberOfTargets = 2;

        // Indicate maximum transfer length is 64k.
        ConfigInfo->MaximumTransferLength = 0x10000;

        KdPrint2((PRINT_PREFIX 
                   "AtapiFindController: Found IDE at %x\n",
                   deviceExtension->BaseIoAddress1[0]));

        // For Daytona, the atdisk driver gets the first shot at the
        // primary and secondary controllers.
        if (preConfig == FALSE) {

            if (*adapterCount - 1 < 2) {

                // Determine whether this driver is being initialized by the
                // system or as a crash dump driver.
                if (ArgumentString) {

                    if (AtapiParseArgumentString(ArgumentString, "dump") == 1) {
                        KdPrint2((PRINT_PREFIX 
                                   "AtapiFindController: Crash dump\n"));
                        atapiOnly = FALSE;
                        deviceExtension->DriverMustPoll = TRUE;
                    } else {
                        KdPrint2((PRINT_PREFIX 
                                   "AtapiFindController: Atapi Only\n"));
                        atapiOnly = TRUE;
                        deviceExtension->DriverMustPoll = FALSE;
                    }
                } else {

                    KdPrint2((PRINT_PREFIX 
                               "AtapiFindController: Atapi Only\n"));
                    atapiOnly = TRUE;
                    deviceExtension->DriverMustPoll = FALSE;
                }

            } else {
                atapiOnly = FALSE;
            }

            // If this is a PCI machine, pick up all devices.
//            deviceExtension->DWordIO = FALSE;

        } else {

            atapiOnly = FALSE;
            deviceExtension->DriverMustPoll = FALSE;

        }// preConfig check

        // Save the Interrupe Mode for later use
        deviceExtension->InterruptMode = ConfigInfo->InterruptMode;

        // Search for devices on this controller.
        if (FindDevices(HwDeviceExtension,
                        atapiOnly,
                        0)) {

            // Claim primary or secondary ATA IO range.
            if (portBase) {
                switch (portBase) {
                case IO_WD2:
                    ConfigInfo->AtdiskSecondaryClaimed = TRUE;
                    chan->PrimaryAddress = FALSE;
                    break;
                case IO_WD1:
                    ConfigInfo->AtdiskPrimaryClaimed = TRUE;
                    chan->PrimaryAddress = TRUE;
                    break;
                default:
                    break;
                }
            } else {
                if (*adapterCount == 1) {
                    ConfigInfo->AtdiskPrimaryClaimed = TRUE;
                    chan->PrimaryAddress = TRUE;
                } else if (*adapterCount == 2) {
                    ConfigInfo->AtdiskSecondaryClaimed = TRUE;
                    chan->PrimaryAddress = FALSE;
                }
            }

            return(SP_RETURN_FOUND);
        }
    }

    // The entire table has been searched and no adapters have been found.
    // There is no need to call again and the device base can now be freed.
    // Clear the adapter count for the next bus.
    *Again = FALSE;
    *(adapterCount) = 0;

    return(SP_RETURN_NOT_FOUND);

} // end AtapiFindController()


/*++

Routine Description:

    This routine is called from AtapiFindController to identify
    devices attached to an IDE controller.

Arguments:

    HwDeviceExtension - HBA miniport driver's adapter data storage
    AtapiOnly - Indicates that routine should return TRUE only if
        an ATAPI device is attached to the controller.

Return Value:

    TRUE - True if devices found.

--*/
BOOLEAN
FindDevices(
    IN PVOID HwDeviceExtension,
    IN BOOLEAN AtapiOnly,
    IN ULONG   Channel
    )
{
    PHW_DEVICE_EXTENSION deviceExtension = (PHW_DEVICE_EXTENSION)HwDeviceExtension;
    PHW_CHANNEL          chan = &(deviceExtension->chan[Channel]);
    PIDE_REGISTERS_1     baseIoAddress1 = deviceExtension->BaseIoAddress1[Channel];
    PIDE_REGISTERS_2     baseIoAddress2 = deviceExtension->BaseIoAddress2[Channel];
    BOOLEAN              deviceResponded = FALSE,
                         skipSetParameters = FALSE;
    ULONG                waitCount = 10000;
    ULONG                deviceNumber;
    ULONG                i;
    UCHAR                signatureLow,
                         signatureHigh;
    UCHAR                statusByte;

    // Disable interrupts
    AtapiDisableInterrupts(deviceExtension, Channel);
//    ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus,IDE_DC_DISABLE_INTERRUPTS | IDE_DC_A_4BIT );

    // Clear expecting interrupt flag and current SRB field.
    chan->ExpectingInterrupt = FALSE;
//    chan->CurrentSrb = NULL;

    // Search for devices.
    for (deviceNumber = 0; deviceNumber < 2; deviceNumber++) {
        ULONG ldev = GET_LDEV2(Channel, deviceNumber, 0);

        // Reset controller
        AtapiSoftReset(baseIoAddress1,deviceNumber);
        WaitOnBusy(baseIoAddress2,statusByte);

        // Select the device.
        SelectDrive(baseIoAddress1, deviceNumber);

        ScsiPortStallExecution(10);
        WaitOnBaseBusy(baseIoAddress1,statusByte);

        // Check here for some SCSI adapters that incorporate IDE emulation.
        GetBaseStatus(baseIoAddress1, statusByte);
/*        if (statusByte == 0xFF) {
            continue;
        }*/
        if(statusByte & IDE_STATUS_BUSY)
            continue;

        signatureLow = ScsiPortReadPortUchar(&baseIoAddress1->i.CylinderLow);
        signatureHigh = ScsiPortReadPortUchar(&baseIoAddress1->i.CylinderHigh);

        if (signatureLow == ATAPI_MAGIC_LSB && signatureHigh == ATAPI_MAGIC_MSB) {

            // ATAPI signature found.
            // Issue the ATAPI identify command if this
            // is not for the crash dump utility.

            if (!deviceExtension->DriverMustPoll) {

                // Issue ATAPI packet identify command.
                if (IssueIdentify(HwDeviceExtension,
                                  deviceNumber,
                                  Channel,
                                  IDE_COMMAND_ATAPI_IDENTIFY)) {

                    // Indicate ATAPI device.
                    KdPrint2((PRINT_PREFIX "FindDevices: Device %x is ATAPI\n",
                               deviceNumber));

                    deviceExtension->lun[ldev].DeviceFlags |= (DFLAGS_DEVICE_PRESENT | 
                                                                                       DFLAGS_ATAPI_DEVICE);

                    deviceResponded = TRUE;

                    GetStatus(baseIoAddress2, statusByte);
                    if (statusByte & IDE_STATUS_ERROR) {
                        AtapiSoftReset(baseIoAddress1, deviceNumber);
                    }

                } else {

                    // Indicate no working device.
                    KdPrint2((PRINT_PREFIX "FindDevices: Device %x not responding\n",
                               deviceNumber));

                    deviceExtension->lun[ldev].DeviceFlags &= ~DFLAGS_DEVICE_PRESENT;
                }
                GetBaseStatus(baseIoAddress1, statusByte);

            }

        } else {

            // Issue IDE Identify. If an Atapi device is actually present, the signature
            // will be asserted, and the drive will be recognized as such.
            if (IssueIdentify(HwDeviceExtension,
                              deviceNumber,
                              Channel,
                              IDE_COMMAND_IDENTIFY)) {

                // IDE drive found.
                KdPrint2((PRINT_PREFIX "FindDevices: Device %x is IDE\n",
                           deviceNumber));

                deviceExtension->lun[ldev].DeviceFlags |= DFLAGS_DEVICE_PRESENT;

                if (!AtapiOnly) {
                    deviceResponded = TRUE;
                } else {
                    KdPrint2((PRINT_PREFIX "FindDevices: AtapiOnly...\n"));
                }

                // Indicate IDE - not ATAPI device.
                deviceExtension->lun[ldev].DeviceFlags &= ~DFLAGS_ATAPI_DEVICE;
            }
            GetBaseStatus(baseIoAddress1, statusByte);
        }
    }

    for (i = 0; i < 2; i++) {
        ULONG ldev = GET_LDEV2(Channel, i, 0);

        if ((  deviceExtension->lun[ldev].DeviceFlags & DFLAGS_DEVICE_PRESENT) &&
             !(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_LBA_ENABLED) &&
             !(deviceExtension->lun[ldev].DeviceFlags & DFLAGS_ATAPI_DEVICE) && deviceResponded) {

            // This hideous hack is to deal with ESDI devices that return
            // garbage geometry in the IDENTIFY data.
            // This is ONLY for the crashdump environment as
            // these are ESDI devices.
            if (deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack ==
                    0x35 &&
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads ==
                    0x07) {

                KdPrint2((PRINT_PREFIX 
                           "FindDevices: Found nasty Compaq ESDI!\n"));

                // Change these values to something reasonable.
                deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack =
                    0x34;
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads =
                    0x0E;
            }

            if (deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack ==
                    0x35 &&
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads ==
                    0x0F) {

                KdPrint2((PRINT_PREFIX 
                           "FindDevices: Found nasty Compaq ESDI!\n"));

                // Change these values to something reasonable.
                deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack =
                    0x34;
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads =
                    0x0F;
            }


            if (deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack ==
                    0x36 &&
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads ==
                    0x07) {

                KdPrint2((PRINT_PREFIX "FindDevices: Found nasty UltraStor ESDI!\n"));

                // Change these values to something reasonable.
                deviceExtension->lun[ldev].IdentifyData.SectorsPerTrack =
                    0x3F;
                deviceExtension->lun[ldev].IdentifyData.NumberOfHeads =
                    0x10;
                skipSetParameters = TRUE;
            }


            if (!skipSetParameters) {

                WaitOnBusy(baseIoAddress2,statusByte);

                // Select the device.
                SelectDrive(baseIoAddress1, i & 0x01);
                GetStatus(baseIoAddress2, statusByte);

                if (statusByte & IDE_STATUS_ERROR) {

                    // Reset the device.
                    KdPrint2((PRINT_PREFIX 
                                "FindDevices: Resetting controller before SetDriveParameters.\n"));

                    ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus,IDE_DC_RESET_CONTROLLER );
                    ScsiPortStallExecution(500 * 1000);
                    ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus,IDE_DC_REENABLE_CONTROLLER);
                    SelectDrive(baseIoAddress1, i & 0x01);

                    do {
                        // Wait for Busy to drop.
                        ScsiPortStallExecution(100);
                        GetStatus(baseIoAddress2, statusByte);

                    } while ((statusByte & IDE_STATUS_BUSY) && waitCount--);
                }

                WaitOnBusy(baseIoAddress2,statusByte);

                KdPrint2((PRINT_PREFIX 
                            "FindDevices: Status before SetDriveParameters: (%x) (%x)\n",
                            statusByte,
                            ScsiPortReadPortUchar(&baseIoAddress1->i.DriveSelect)));

                // Use the IDENTIFY data to set drive parameters.
                if (!SetDriveParameters(HwDeviceExtension,i,Channel)) {

                    KdPrint2((PRINT_PREFIX 
                               "FindDevices: Set drive parameters for device %d failed\n",
                               i));
                    // Don't use this device as writes could cause corruption.
                    deviceExtension->lun[ldev].DeviceFlags = 0;
                    continue;

                }
                if (deviceExtension->lun[ldev].DeviceFlags & DFLAGS_REMOVABLE_DRIVE) {

                    // Pick up ALL IDE removable drives that conform to Yosemite V0.2...
                    AtapiOnly = FALSE;
                }

                // Indicate that a device was found.
                if (!AtapiOnly) {
                    deviceResponded = TRUE;
                }
            }
        }
    }

    // Make sure master device is selected on exit.
    SelectDrive(baseIoAddress1, 0);

    // Reset the controller. This is a feeble attempt to leave the ESDI
    // controllers in a state that ATDISK driver will recognize them.
    // The problem in ATDISK has to do with timings as it is not reproducible
    // in debug. The reset should restore the controller to its poweron state
    // and give the system enough time to settle.
    if (!deviceResponded) {

        ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus,IDE_DC_RESET_CONTROLLER );
        ScsiPortStallExecution(50 * 1000);
        ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus,IDE_DC_REENABLE_CONTROLLER);
    }

    GetBaseStatus(baseIoAddress1, statusByte);
    // Enable interrupts
    AtapiEnableInterrupts(deviceExtension, Channel);
//    ScsiPortWritePortUchar(&baseIoAddress2->AlternateStatus, IDE_DC_A_4BIT );

    KdPrint2((PRINT_PREFIX 
               "FindDevices: returning %d\n",
               deviceResponded));

    return deviceResponded;

} // end FindDevices()
