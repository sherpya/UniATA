/* Runtime controller quirks: no board names or build-time hardware switches.
 * Unlisted controllers retain the existing UniATA paths.
 */
#include "stdafx.h"
#include "controller_quirks.h"
#include "cs5530.h"
#include "cs5536.h"

static const UNIATA_CONTROLLER_QUIRKS ControllerQuirks[] = {
    { ATA_CYRIX_5530, UNIATA_QUIRK_LEGACY_BMDMA | UNIATA_QUIRK_SCSIPORT_PCI_CLAIM, 4, 2, 2,
      UniataCs5530Init, UniataCs5530Timing, NULL, 2 },
    { ATA_AMD5536, UNIATA_QUIRK_LEGACY_BMDMA | UNIATA_QUIRK_SCSIPORT_PCI_CLAIM | UNIATA_QUIRK_OWN_INIT, 4, 2, 5,
      UniataCs5536Init, NULL, UniataCs5536SetMode, 1 }
};

const UNIATA_CONTROLLER_QUIRKS* UniataGetControllerQuirks(ULONG deviceId)
{
    for(ULONG i = 0; i < sizeof(ControllerQuirks)/sizeof(ControllerQuirks[0]); ++i) {
        if(ControllerQuirks[i].DeviceId == deviceId)
            return &ControllerQuirks[i];
    }
    return NULL;
}

BOOLEAN UniataHasLegacyBusMaster(const PCI_COMMON_CONFIG* pci)
{
    ULONG id = ((ULONG)pci->DeviceID << 16) | pci->VendorID;
    const UNIATA_CONTROLLER_QUIRKS* quirks = UniataGetControllerQuirks(id);
    if(!quirks || !(quirks->Flags & UNIATA_QUIRK_LEGACY_BMDMA))
        return FALSE;

    // Some legacy IDE controllers omit the ProgIf bus-master capability bit.
    // Require a complete compatibility-mode header and a valid I/O BAR4;
    // classification does not replace the separate PCI Command enable check.
    if((pci->HeaderType & 0x7f) != 0 || pci->BaseClass != 1 ||
       pci->SubClass != 1 || (pci->ProgIf & ~0x80) != 0)
        return FALSE;
    for(ULONG bar = 0; bar < 4; ++bar) {
        if(pci->u.type0.BaseAddresses[bar]) return FALSE;
    }
    ULONG base = pci->u.type0.BaseAddresses[4];
    return (base & 0x0f) == 1 && base >= 0x11 && base <= 0xfff1;
}

BOOLEAN UniataLimitControllerChannels(PHW_DEVICE_EXTENSION extension)
{
    const UNIATA_CONTROLLER_QUIRKS* q = UniataGetControllerQuirks(extension->DevID);
    if(extension->UnknownDev || !q || !q->MaxChannels) return TRUE;
    if(extension->Channel >= q->MaxChannels) return FALSE;
    extension->NumberChannels = min(extension->NumberChannels, q->MaxChannels - extension->Channel);
    return extension->NumberChannels != 0;
}

BOOLEAN UniataInitControllerQuirks(PHW_DEVICE_EXTENSION extension)
{
    const UNIATA_CONTROLLER_QUIRKS* quirks = UniataGetControllerQuirks(extension->DevID);
    if(extension->UnknownDev || !extension->BusMaster || !quirks || !quirks->Initialize)
        return TRUE;
    if(quirks->Initialize(extension))
        return TRUE;

    // Some callers ignore AtapiChipInit's result: cap every logical channel
    // as well so a partial/failed initialization cannot enable DMA later.
    KdPrint2(("[UniATA] controller %#lx initialization failed; disabling DMA\n",
        extension->DevID));
    extension->BusMaster = DMA_MODE_NONE;
    extension->MaxTransferMode = ATA_PIO0 + quirks->MaxPioMode;
    for(ULONG channel = 0; channel < extension->NumberChannels; ++channel)
        extension->chan[channel].MaxTransferMode = extension->MaxTransferMode;
    return FALSE;
}

static BOOLEAN UniataTryControllerMode(const UNIATA_CONTROLLER_QUIRKS* quirks,
    PHW_DEVICE_EXTENSION extension, ULONG device, ULONG channel,
    PHW_LU_EXTENSION lun, ULONG pioMode, ULONG mode)
{
    if(quirks->SetMode) return quirks->SetMode(extension, device, channel, pioMode, mode);
    if(!AtaSetTransferMode(extension, device, channel, lun, mode)) return FALSE;
    quirks->SetTiming(extension, device, channel, pioMode, mode);
    return TRUE;
}

BOOLEAN UniataSetControllerTransferMode(PHW_DEVICE_EXTENSION extension,
    ULONG device, ULONG channel, SCHAR pioMode, SCHAR wdmaMode, SCHAR udmaMode)
{
    const UNIATA_CONTROLLER_QUIRKS* quirks = UniataGetControllerQuirks(extension->DevID);
    if(extension->UnknownDev || !quirks || (!quirks->SetTiming && !quirks->SetMode))
        return FALSE;

    if(pioMode < 0) pioMode = 0;
    pioMode = min(pioMode, quirks->MaxPioMode);
    wdmaMode = min(wdmaMode, quirks->MaxWdmaMode);
    udmaMode = min(udmaMode, quirks->MaxUdmaMode);
    PHW_LU_EXTENSION lun = extension->chan[channel].lun[device];
    LONG mode;
    for(mode = udmaMode; mode >= 0; --mode) {
        if(UniataTryControllerMode(quirks, extension, device, channel, lun, pioMode, ATA_UDMA0 + mode)) {
            return TRUE;
        }
    }
    for(mode = wdmaMode; mode >= 0; --mode) {
        if(UniataTryControllerMode(quirks, extension, device, channel, lun, pioMode, ATA_WDMA0 + mode)) {
            return TRUE;
        }
    }
    UniataTryControllerMode(quirks, extension, device, channel, lun, pioMode, ATA_PIO0 + pioMode);
    return TRUE;
}
