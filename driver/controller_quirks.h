/* Controller capabilities and operations selected by PCI ID. */
#ifndef UNIATA_CONTROLLER_QUIRKS_H
#define UNIATA_CONTROLLER_QUIRKS_H

#define UNIATA_QUIRK_LEGACY_BMDMA 0x00000001

struct UNIATA_CONTROLLER_QUIRKS {
    ULONG DeviceId;
    ULONG Flags;
    SCHAR MaxPioMode, MaxWdmaMode, MaxUdmaMode;
    BOOLEAN (*Initialize)(PHW_DEVICE_EXTENSION extension);
    VOID (*SetTiming)(PHW_DEVICE_EXTENSION extension, ULONG device,
                      ULONG channel, ULONG pioMode, ULONG transferMode);
};

const UNIATA_CONTROLLER_QUIRKS* UniataGetControllerQuirks(ULONG deviceId);
BOOLEAN UniataHasLegacyBusMaster(const PCI_COMMON_CONFIG* pci);
BOOLEAN UniataInitControllerQuirks(PHW_DEVICE_EXTENSION extension);
/* TRUE means a controller operation handled mode selection (including a
 * drive rejecting all modes); FALSE leaves the existing vendor path in charge.
 */
BOOLEAN UniataSetControllerTransferMode(PHW_DEVICE_EXTENSION extension,
    ULONG device, ULONG channel, SCHAR pioMode, SCHAR wdmaMode, SCHAR udmaMode);

BOOLEAN NTAPI AtaSetTransferMode(PHW_DEVICE_EXTENSION extension, ULONG device,
    ULONG channel, PHW_LU_EXTENSION lun, ULONG mode);
#endif
