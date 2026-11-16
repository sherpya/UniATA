/* PCI resource ownership for split, compatibility-mode IDE channels.
 *
 * ScsiPort owns this adapter's PDO/FDO lifecycle and reports only BAR4.
 * It has no disks, ISR, DMA adapter, port mappings or hardware reset path.
 * The two normal UniATA adapters retain all ATA I/O and interrupt handling.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "stdafx.h"
#include "controller_quirks.h"

#ifndef UNIATA_CORE
struct UNIATA_PCI_OWNER_EXTENSION {
    ULONG Index;
};

static ULONG NTAPI
UniataPciOwnerFind(PVOID extension, PVOID context, PVOID busInfo,
                   PCHAR argument, PPORT_CONFIGURATION_INFORMATION config,
                   PBOOLEAN again)
{
    ULONG index = PtrToUlong(context);
    PCI_COMMON_CONFIG pci;
    (void)busInfo;
    (void)argument;
    *again = FALSE;
    if(index >= BMListLen || !extension || !config->AccessRanges ||
       !config->NumberOfAccessRanges)
        return SP_RETURN_NOT_FOUND;
    PBUSMASTER_CONTROLLER_INFORMATION controller = &BMList[index];
    if(!controller->ScsiPortPciClaim || (controller->ChanInitOk & 0x80) ||
       config->AdapterInterfaceType != Isa || config->SystemIoBusNumber != 0)
        return SP_RETURN_NOT_FOUND;

    RtlZeroMemory(&pci, sizeof(pci));
    if(HalGetBusData(PCIConfiguration, controller->busNumber,
                    controller->slotNumber, &pci, PCI_COMMON_HDR_LENGTH) != PCI_COMMON_HDR_LENGTH ||
       pci.VendorID != controller->nVendorId || pci.DeviceID != controller->nDeviceId ||
       !UniataHasLegacyBusMaster(&pci))
        return SP_RETURN_NOT_FOUND;

    // Enumerate through ISA bus 0, then report the verified PCI identity.
    // XP uses the returned configuration for IoReportDetectedDevice; ScsiPort
    // owns resource reporting and FDO attachment. No prior HAL claim is made.
    config->AdapterInterfaceType = PCIBus;
    config->SystemIoBusNumber = controller->busNumber;
    config->SlotNumber = controller->slotNumber;
    // Only the shared bus-master window belongs here: no legacy ATA ports/IRQs.
    for(ULONG r = 0; r < config->NumberOfAccessRanges; ++r)
        RtlZeroMemory(&(*config->AccessRanges)[r], sizeof(ACCESS_RANGE));
    (*config->AccessRanges)[0].RangeStart = ScsiPortConvertUlongToPhysicalAddress(
        pci.u.type0.BaseAddresses[4] & ~0xfUL);
    (*config->AccessRanges)[0].RangeLength = 16;
    config->BusInterruptLevel = config->BusInterruptVector = 0;
    config->BusInterruptLevel2 = config->BusInterruptVector2 = 0;
    config->DmaChannel = config->DmaPort = SP_UNINITIALIZED_VALUE;
    config->DmaChannel2 = config->DmaPort2 = SP_UNINITIALIZED_VALUE;
    config->Master = config->ScatterGather = config->Dma32BitAddresses = FALSE;
    config->NeedPhysicalAddresses = config->MapBuffers = FALSE;
    config->AtdiskPrimaryClaimed = config->AtdiskSecondaryClaimed = FALSE;
    config->MaximumTransferLength = 512;
    config->NumberOfPhysicalBreaks = 0;
    config->AlignmentMask = 0;
    // Target 0 is the adapter itself; no logical units or disk are enumerated.
    config->NumberOfBuses = 1;
    config->MaximumNumberOfTargets = 1;
    config->InitiatorBusId[0] = 0;
    ((UNIATA_PCI_OWNER_EXTENSION*)extension)->Index = index;
    KdPrint2(("[UniATA] PCI owner: bus %lu slot %#lx, BAR4 %#lx+16, no IRQ\n",
        controller->busNumber, controller->slotNumber,
        (*config->AccessRanges)[0].RangeStart.LowPart));
    return SP_RETURN_FOUND;
}

static BOOLEAN NTAPI
UniataPciOwnerInitialize(PVOID extension)
{
    ULONG index = ((UNIATA_PCI_OWNER_EXTENSION*)extension)->Index;
    if(index >= BMListLen || !BMList[index].ScsiPortPciClaim)
        return FALSE;
    // Also prevents ScsiPort's enumeration loop from detecting this slot twice.
    // The caller clears this flag if initialization subsequently fails.
    BMList[index].ChanInitOk |= 0x80;
    return TRUE;
}

static BOOLEAN NTAPI
UniataPciOwnerReset(PVOID extension, ULONG path)
{
    (void)extension;
    (void)path;
    // No hardware reset: the channel adapters own outstanding ATA requests.
    return TRUE;
}

static BOOLEAN NTAPI
UniataPciOwnerStartIo(PVOID extension, PSCSI_REQUEST_BLOCK srb)
{
    srb->ScsiStatus = 0;
    srb->DataTransferLength = 0;
    switch(srb->Function) {
    case SRB_FUNCTION_EXECUTE_SCSI:
        srb->SrbStatus = SRB_STATUS_NO_DEVICE;
        break;
    case SRB_FUNCTION_SHUTDOWN:
    case SRB_FUNCTION_FLUSH:
        srb->SrbStatus = SRB_STATUS_SUCCESS;
        break;
    default:
        srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
        break;
    }
    ScsiPortNotification(RequestComplete, extension, srb);
    ScsiPortNotification(NextRequest, extension, NULL);
    return TRUE;
}

NTSTATUS
UniataClaimPciResourcesWithScsiPort(PVOID driverObject, PVOID registryPath, ULONG index)
{
    if(index >= BMListLen || !BMList[index].ScsiPortPciClaim)
        return STATUS_INVALID_PARAMETER;
    PBUSMASTER_CONTROLLER_INFORMATION controller = &BMList[index];
    if(controller->ChanInitOk & 0x80)
        return STATUS_SUCCESS;

    HW_INITIALIZATION_DATA_COMMON init;
    RtlZeroMemory(&init, sizeof(init));
    init.comm.HwInitializationDataSize = sizeof(init.comm) + sizeof(init.w2k);
    // XP's native PCI enumeration rejects a slot with no PCI IRQ before
    // HwFindAdapter, even with HwInterrupt == NULL. A compatibility-mode
    // CS5530 has only BAR4 on its PCI resource list: IRQ14/15 are channel
    // resources. Use legacy enumeration and publish PCI identity in Find.
    init.comm.AdapterInterfaceType = Isa;
    init.comm.HwFindAdapter = (PHW_FIND_ADAPTER)UniataPciOwnerFind;
    init.comm.HwInitialize = (PHW_INITIALIZE)UniataPciOwnerInitialize;
    init.comm.HwStartIo = (PHW_STARTIO)UniataPciOwnerStartIo;
    init.comm.HwResetBus = (PHW_RESET_BUS)UniataPciOwnerReset;
    init.comm.DeviceExtensionSize = sizeof(UNIATA_PCI_OWNER_EXTENSION);
    init.comm.NumberOfAccessRanges = 1;
    // HwInterrupt intentionally NULL; this adapter reserves no interrupt.
    NTSTATUS status = (NTSTATUS)ScsiPortInitialize(driverObject, registryPath,
        &init.comm, UlongToPtr(index));
    KdPrint2(("[UniATA] PCI owner ScsiPortInitialize status %#lx, flags %#x\n",
        (ULONG)status, controller->ChanInitOk));
    if(!NT_SUCCESS(status)) {
        controller->ChanInitOk &= ~0x80;
        return status;
    }
    // ScsiPort can return success because another adapter already initialized.
    if(!(controller->ChanInitOk & 0x80))
        return STATUS_DEVICE_DOES_NOT_EXIST;
    return STATUS_SUCCESS;
}
#endif
